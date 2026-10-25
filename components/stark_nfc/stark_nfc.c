/*
 * stark_nfc.c — the NFC service's target port (stark_nfc.h): binds the pure
 * PN532 driver to stark_nfc_transport and FreeRTOS time, and runs the NFC
 * task. Every PN532 command after init is issued by that task, so the driver
 * is never shared; the pure poll state machine and reader decide what it
 * does (STARK-0403). Target-only.
 */
#include "stark_nfc.h"
#include <stdatomic.h>
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nfc_ident.h"
#include "nfc_poll.h"
#include "nfc_reader.h"
#include "nfc_writer.h"
#include "pn532.h"
#include "sdkconfig.h"
#include "stark_event.h"
#include "stark_log.h"
#include "stark_nfc_transport.h"

#define NFC_CORE 0 /* the UI task runs on core 1 */

static pn532_t s_pn532;
static _Atomic int s_module = STARK_NFC_NOT_FITTED;
static atomic_bool s_want_poll;

static StackType_t s_stack[CONFIG_STARK_NFC_TASK_STACK / sizeof(StackType_t)];
static StaticTask_t s_tcb;
static TaskHandle_t s_task;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock; /* guards s_snap and s_has_tag */
static stark_nfc_read_t s_snap;
static bool s_has_tag;
static stark_nfc_read_t s_work; /* the NFC task's own read */
static nfc_poll_t s_poll;
static bool s_field;
static atomic_bool s_write_pending; /* the request below belongs to the task while set */
static nfc_tag_t s_write_tag;
static uint8_t s_write_msg[NFC_NDEF_MAX];
static size_t s_write_len;
static _Atomic int s_write_err = STARK_OK;

static stark_err_t io_write(void *ctx, const uint8_t *buf, size_t len)
{
    (void)ctx;
    return stark_nfc_transport_write(buf, len, CONFIG_STARK_NFC_I2C_TIMEOUT_MS);
}

static stark_err_t io_read(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    return stark_nfc_transport_read(buf, len, CONFIG_STARK_NFC_I2C_TIMEOUT_MS);
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void io_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

static const pn532_io_t k_io = {io_write, io_read, io_now, io_delay, NULL};

static void publish(int32_t what)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_NFC;
    e.system.arg = what;
    (void)stark_event_publish(&e);
}

static void set_tag(const stark_nfc_read_t *r)
{
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    if (r != NULL) {
        s_snap = *r;
    }
    s_has_tag = r != NULL;
    (void)xSemaphoreGive(s_lock);
}

static void log_tag(const stark_nfc_read_t *r)
{
    char uid[2 * NFC_UID_MAX + 1];
    for (unsigned i = 0; i < r->tag.uid_len; i++) {
        snprintf(uid + 2 * i, 3, "%02X", r->tag.uid[i]);
    }
    uid[2 * r->tag.uid_len] = '\0';
    STARK_LOGI("nfc", "tag %s uid=%s atqa=%04X sak=%02X", nfc_tech_name(r->tag.tech), uid,
               (unsigned)r->tag.atqa, (unsigned)r->tag.sak);
    if (r->read_err != STARK_OK) {
        STARK_LOGW("nfc", "read stopped: %s", stark_err_str(r->read_err));
    } else if (r->ndef_present) {
        STARK_LOGI("nfc", "ndef %u B", (unsigned)r->ndef_len);
    } else if (nfc_tech_is_type2(r->tag.tech)) {
        STARK_LOGI("nfc", "no ndef");
    }
}

/* The field follows s_want_poll; turning it off forgets the tag. */
static void apply_field(void)
{
    bool want = atomic_load(&s_want_poll);
    if (want == s_field || stark_nfc_module() != STARK_NFC_READY) {
        return;
    }
    if (pn532_field(&s_pn532, want) != STARK_OK) {
        return; /* the next poll's error counts towards a module loss */
    }
    s_field = want;
    STARK_LOGI("nfc", "field %s", want ? "on" : "off");
    if (!want) {
        nfc_poll_init(&s_poll);
        set_tag(NULL);
    }
}

static void poll_once(uint32_t now)
{
    if (s_poll.state == NFC_POLL_RECOVERING) {
        if (!nfc_poll_recover_due(&s_poll, now)) {
            return;
        }
        uint8_t ic = 0;
        uint8_t ver = 0;
        uint8_t rev = 0;
        stark_err_t err = pn532_detect(&s_pn532, &ic, &ver, &rev);
        if (nfc_poll_recovered(&s_poll, err, now) == NFC_POLL_BACK) {
            atomic_store(&s_module, STARK_NFC_READY);
            s_field = false; /* detection leaves it off; apply_field() turns it on again */
            STARK_LOGI("nfc", "module back");
            publish(STARK_NFC_EVT_MODULE);
        }
        return;
    }
    bool found = false;
    nfc_tag_t tag;
    stark_err_t err = nfc_reader_poll(&s_pn532, &found, &tag);
    switch (nfc_poll_result(&s_poll, err, found, &tag, now)) {
        case NFC_POLL_TAG:
            nfc_reader_read(&s_pn532, &tag, &s_work);
            set_tag(&s_work);
            log_tag(&s_work);
            publish(STARK_NFC_EVT_TAG);
            return;
        case NFC_POLL_LEFT:
            set_tag(NULL);
            STARK_LOGI("nfc", "tag left");
            publish(STARK_NFC_EVT_LEFT);
            break;
        case NFC_POLL_LOST:
            set_tag(NULL);
            atomic_store(&s_module, STARK_NFC_LOST);
            STARK_LOGW("nfc", "module lost");
            publish(STARK_NFC_EVT_MODULE);
            return;
        case NFC_POLL_NONE:
        case NFC_POLL_BACK:
        default:
            break;
    }
    if (found) {
        nfc_reader_release(&s_pn532);
    }
}

/* A confirmed write, then the tag read again so the snapshot shows the result. */
static void write_once(uint32_t now)
{
    uint16_t pages = 0;
    stark_err_t err = nfc_writer_write(&s_pn532, &s_write_tag, s_write_msg, s_write_len, &pages);
    atomic_store(&s_write_err, err);
    if (err == STARK_OK) {
        STARK_LOGI("nfc", "write ok %u pages", (unsigned)pages);
    } else {
        STARK_LOGW("nfc", "write failed: %s (%u pages written)", stark_err_str(err),
                   (unsigned)pages);
    }
    atomic_store(&s_write_pending, false);
    publish(err == STARK_OK ? STARK_NFC_EVT_WRITE_DONE : STARK_NFC_EVT_WRITE_FAILED);
    nfc_poll_init(&s_poll); /* the next poll reads whatever is there now */
    poll_once(now);
}

static void nfc_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* NOTE: wake on the poll period whenever polling is wanted, not only
         * while the field is on — after a recovery the field is off until
         * apply_field() turns it on again, and waiting forever would stop
         * polling for good (caught by storage/v04-nfc-app). */
        bool polling = atomic_load(&s_want_poll) || s_field || s_poll.state == NFC_POLL_RECOVERING;
        (void)ulTaskNotifyTake(pdTRUE,
                               polling ? pdMS_TO_TICKS(CONFIG_STARK_NFC_POLL_MS) : portMAX_DELAY);
        if (s_poll.state != NFC_POLL_RECOVERING) {
            apply_field();
        }
        if (!atomic_load(&s_want_poll)) {
            if (s_poll.state == NFC_POLL_RECOVERING) {
                nfc_poll_init(&s_poll); /* stopped while lost: try again at the next start */
            }
            continue;
        }
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (atomic_load(&s_write_pending) && s_field && s_poll.state != NFC_POLL_RECOVERING) {
            write_once(now);
        } else {
            poll_once(now);
        }
    }
}

static stark_err_t want_poll(bool on)
{
    if (s_task == NULL) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    atomic_store(&s_want_poll, on);
    xTaskNotifyGive(s_task);
    return STARK_OK;
}

stark_err_t stark_nfc_start(void)
{
    if (stark_nfc_module() == STARK_NFC_NOT_FITTED || stark_nfc_module() == STARK_NFC_ABSENT) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return want_poll(true);
}

stark_err_t stark_nfc_stop(void)
{
    return want_poll(false);
}

stark_err_t stark_nfc_write_ndef(const uint8_t *uid, uint8_t uid_len, const uint8_t *msg,
                                 size_t len)
{
    if (uid == NULL || uid_len == 0 || uid_len > NFC_UID_MAX || (len > 0 && msg == NULL) ||
        len > NFC_NDEF_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_task == NULL || !atomic_load(&s_want_poll)) {
        return STARK_ERR_STATE;
    }
    if (atomic_load(&s_write_pending)) {
        return STARK_ERR_BUSY;
    }
    memset(&s_write_tag, 0, sizeof s_write_tag);
    memcpy(s_write_tag.uid, uid, uid_len);
    s_write_tag.uid_len = uid_len;
    if (len > 0) {
        memcpy(s_write_msg, msg, len);
    }
    s_write_len = len;
    atomic_store(&s_write_pending, true);
    xTaskNotifyGive(s_task);
    return STARK_OK;
}

stark_err_t stark_nfc_write_result(void)
{
    return (stark_err_t)atomic_load(&s_write_err);
}

stark_err_t stark_nfc_take(stark_nfc_read_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return STARK_ERR_NOT_FOUND;
    }
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    bool has = s_has_tag;
    if (has) {
        *out = s_snap;
    }
    (void)xSemaphoreGive(s_lock);
    return has ? STARK_OK : STARK_ERR_NOT_FOUND;
}

stark_err_t stark_nfc_init(void)
{
    stark_err_t err = stark_nfc_transport_open();
    if (err == STARK_ERR_NOT_SUPPORTED) {
        atomic_store(&s_module, STARK_NFC_NOT_FITTED);
        STARK_LOGI("nfc", "not fitted");
        return STARK_OK;
    }
    if (err != STARK_OK) {
        atomic_store(&s_module, STARK_NFC_ABSENT);
        STARK_LOGW("nfc", "no module (%s)", stark_err_str(err));
        return STARK_OK;
    }
    pn532_bind(&s_pn532, &k_io);
    uint8_t ic = 0;
    uint8_t ver = 0;
    uint8_t rev = 0;
    err = pn532_detect(&s_pn532, &ic, &ver, &rev);
    if (err == STARK_OK) {
        atomic_store(&s_module, STARK_NFC_READY);
        STARK_LOGI("nfc", "pn532 fw %u.%u", (unsigned)ver, (unsigned)rev);
        nfc_poll_init(&s_poll);
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
        s_task = xTaskCreateStaticPinnedToCore(
            nfc_task, "stark_nfc", (uint32_t)(sizeof s_stack / sizeof s_stack[0]), NULL,
            CONFIG_STARK_NFC_TASK_PRIO, s_stack, &s_tcb, NFC_CORE);
        if (s_task == NULL) {
            return STARK_ERR_NO_MEM;
        }
    } else if (err == STARK_ERR_NOT_SUPPORTED) {
        atomic_store(&s_module, STARK_NFC_ABSENT);
        STARK_LOGW("nfc", "unexpected chip 0x%02x", (unsigned)ic);
    } else {
        atomic_store(&s_module, STARK_NFC_ABSENT);
        STARK_LOGI("nfc", "no module (%s)", stark_err_str(err));
    }
    return STARK_OK;
}

stark_nfc_module_t stark_nfc_module(void)
{
    return (stark_nfc_module_t)atomic_load(&s_module);
}
