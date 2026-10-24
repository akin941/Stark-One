/*
 * stark_ir.c — the RMT port of stark_ir (stark_ir.h). Target-only; every
 * decision about timings, protocols and the capture ring is the pure core
 * (ir_symbols.c, ir_codec.c, ir_capture.c — host-tested).
 *
 * The RX done callback runs in the ISR: it only hands the symbol count to
 * the IR task through a queue. The task converts, decodes, stores, logs one
 * line and re-arms the receiver. Transmissions (STARK-0304) also run on the
 * IR task, after the pure gate (ir_tx_guard.c) has accepted them.
 */
#include "stark_ir.h"
#include <stdatomic.h>
#include <stdbool.h>
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ir_capture.h"
#include "ir_codec.h"
#include "ir_symbols.h"
#include "ir_tx_guard.h"
#include "sdkconfig.h"
#include "stark_board.h"
#include "stark_event.h"
#include "stark_log.h"

#define IR_RESOLUTION_HZ 1000000u                   /* 1 tick = 1 us */
#define IR_RX_SYMBOLS    (IR_MAX_DURATIONS / 2 + 2) /* a full capture and its end marker */
#define IR_RX_MEM_BLOCK  64u                        /* hardware memory; the driver ping-pongs it */
#define IR_GLITCH_NS     1250u                      /* shorter pulses are noise */
#define IR_IDLE_NS       12000000u                  /* 12 ms of idle ends a frame */
#define IR_CORE          0                          /* the UI task runs on core 1 */

#define IR_TX_SYMBOLS    (IR_MAX_DURATIONS / 2 + 34) /* + long-duration splits and the end */
#define IR_TX_MEM_BLOCK  64u

typedef enum { MSG_RX = 1, MSG_TX = 2 } ir_msg_kind_t;

typedef struct {
    uint8_t kind;
    uint16_t symbols;
} ir_msg_t;

static bool s_fitted;
static bool s_ready;
static rmt_channel_handle_t s_rx;
static atomic_bool s_rx_on;
static rmt_symbol_word_t s_rx_buf[IR_RX_SYMBOLS];
static const rmt_receive_config_t k_rx_cfg = {
    .signal_range_min_ns = IR_GLITCH_NS,
    .signal_range_max_ns = IR_IDLE_NS,
};

static rmt_channel_handle_t s_tx;
static rmt_encoder_handle_t s_copy;
static atomic_bool s_tx_busy;
static ir_signal_t s_tx_signal; /* owned by the IR task while s_tx_busy */
static uint8_t s_tx_repeats;
static ir_tx_plan_t s_tx_plan;
static rmt_symbol_word_t s_tx_buf[IR_TX_SYMBOLS];
static portMUX_TYPE s_tx_lock = portMUX_INITIALIZER_UNLOCKED; /* the rate-limit state */
static bool s_tx_have_last;
static uint32_t s_tx_last_end_ms;

static StaticQueue_t s_queue_buf;
static uint8_t s_queue_storage[4 * sizeof(ir_msg_t)];
static QueueHandle_t s_queue;
static StaticTask_t s_tcb;
static StackType_t s_stack[CONFIG_STARK_IR_TASK_STACK / sizeof(StackType_t)];

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED; /* s_ring and s_stats */
static ir_capture_t s_ring;
static stark_ir_stats_t s_stats;
static ir_signal_t s_work; /* IR task only */

static bool on_recv_done(rmt_channel_handle_t chan, const rmt_rx_done_event_data_t *edata,
                         void *ctx)
{
    (void)chan;
    (void)ctx;
    ir_msg_t msg = {.kind = MSG_RX, .symbols = (uint16_t)edata->num_symbols};
    BaseType_t woken = pdFALSE;
    (void)xQueueSendFromISR(s_queue, &msg, &woken);
    return woken == pdTRUE;
}

static void count(uint32_t *field)
{
    portENTER_CRITICAL(&s_lock);
    (*field)++;
    portEXIT_CRITICAL(&s_lock);
}

static void publish(int32_t what)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_IR;
    e.system.arg = what;
    (void)stark_event_publish(&e);
}

static void arm(void)
{
    if (atomic_load(&s_rx_on)) {
        (void)rmt_receive(s_rx, s_rx_buf, sizeof s_rx_buf, &k_rx_cfg);
    }
}

/* One received frame: timings, decode, ring, event, one log line. */
static void handle_rx(uint16_t symbols)
{
    count(&s_stats.frames);
    s_work = (ir_signal_t){0};
    bool fits = symbols < IR_RX_SYMBOLS &&
                ir_symbols_to_timings((const uint32_t *)s_rx_buf, symbols, &s_work.timings);
    if (!fits) {
        count(&s_stats.overflows);
        STARK_LOGW("ir", "rx overflow (%u symbols)", (unsigned)symbols);
        return;
    }
    ir_protocol_t p = ir_decode(&s_work.timings, &s_work.frame);
    ir_timings_t scratch;
    s_work.carrier_hz = 38000u;
    (void)ir_encode(&s_work.frame, &scratch, &s_work.carrier_hz); /* the protocol's carrier */
    if (p == IR_PROTO_UNKNOWN) {
        count(&s_stats.unknown);
        STARK_LOGI("ir", "rx RAW n=%u", (unsigned)s_work.timings.n);
    } else {
        count(&s_stats.decoded);
        if (s_work.frame.repeat) {
            STARK_LOGI("ir", "rx %s repeat", ir_protocol_name(p));
        } else {
            STARK_LOGI("ir", "rx %s addr=0x%02X cmd=0x%02X", ir_protocol_name(p),
                       (unsigned)s_work.frame.address, (unsigned)s_work.frame.command);
        }
    }
    portENTER_CRITICAL(&s_lock);
    ir_capture_push(&s_ring, &s_work);
    s_stats.dropped = s_ring.dropped;
    portEXIT_CRITICAL(&s_lock);
    publish(STARK_IR_EVT_RX);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* One gated transmission: the frame (and its repeats) at the protocol's
 * period, with its carrier. Runs on the IR task. */
static void handle_tx(void)
{
    size_t words = 0;
    stark_err_t err = STARK_OK;
    const rmt_carrier_config_t carrier = {.frequency_hz = s_tx_signal.carrier_hz,
                                          .duty_cycle = IR_TX_DUTY_PCT / 100.0f};
    if (!ir_timings_to_symbols(&s_tx_signal.timings, (uint32_t *)s_tx_buf, IR_TX_SYMBOLS, &words)) {
        err = STARK_ERR_INVALID_ARG;
    } else if (rmt_disable(s_tx) != ESP_OK || rmt_apply_carrier(s_tx, &carrier) != ESP_OK ||
               rmt_enable(s_tx) != ESP_OK) {
        err = STARK_ERR_IO;
    }
    const rmt_transmit_config_t cfg = {.loop_count = 0};
    for (uint8_t k = 0; err == STARK_OK && k <= s_tx_repeats; k++) {
        uint32_t started = now_ms();
        if (rmt_transmit(s_tx, s_copy, s_tx_buf, words * sizeof s_tx_buf[0], &cfg) != ESP_OK) {
            err = STARK_ERR_IO;
        } else if (rmt_tx_wait_all_done(s_tx, (int)(s_tx_plan.signal_us / 1000u + 100u)) !=
                   ESP_OK) {
            err = STARK_ERR_TIMEOUT;
        } else if (k < s_tx_repeats) {
            uint32_t elapsed = now_ms() - started;
            if (elapsed < s_tx_plan.period_ms) {
                vTaskDelay(pdMS_TO_TICKS(s_tx_plan.period_ms - elapsed));
            }
        }
    }
    if (err == STARK_ERR_TIMEOUT) {
        (void)rmt_disable(s_tx); /* abandon the stuck transfer, ready for the next one */
        (void)rmt_enable(s_tx);
    }
    portENTER_CRITICAL(&s_tx_lock);
    s_tx_have_last = true;
    s_tx_last_end_ms = now_ms();
    portEXIT_CRITICAL(&s_tx_lock);
    if (err == STARK_OK) {
        count(&s_stats.tx);
        STARK_LOGI("ir", "tx done");
    } else {
        STARK_LOGW("ir", "tx failed: %s", stark_err_str(err));
    }
    atomic_store(&s_tx_busy, false);
    publish(err == STARK_OK ? STARK_IR_EVT_TX_DONE : STARK_IR_EVT_TX_FAILED);
}

static void ir_task(void *arg)
{
    (void)arg;
    for (;;) {
        ir_msg_t msg;
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (msg.kind == MSG_RX && atomic_load(&s_rx_on)) {
            handle_rx(msg.symbols);
            arm();
        } else if (msg.kind == MSG_TX) {
            handle_tx();
        }
    }
}

stark_err_t stark_ir_init(void)
{
    if (s_ready) {
        return STARK_ERR_STATE;
    }
    s_ready = true;
    const stark_board_pins_t *pins = stark_board_pins();
    if (pins->ir_rx < 0) {
        STARK_LOGI("ir", "not fitted");
        return STARK_OK;
    }
    ir_capture_init(&s_ring);
    s_queue = xQueueCreateStatic(4, sizeof(ir_msg_t), s_queue_storage, &s_queue_buf);
    const rmt_rx_channel_config_t rx_cfg = {
        .gpio_num = pins->ir_rx,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = IR_RX_MEM_BLOCK,
    };
    if (rmt_new_rx_channel(&rx_cfg, &s_rx) != ESP_OK) {
        return STARK_ERR_IO;
    }
    const rmt_rx_event_callbacks_t cbs = {.on_recv_done = on_recv_done};
    if (rmt_rx_register_event_callbacks(s_rx, &cbs, NULL) != ESP_OK) {
        return STARK_ERR_IO;
    }
    const rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = pins->ir_tx,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = IR_TX_MEM_BLOCK,
        .trans_queue_depth = 1,
    };
    const rmt_copy_encoder_config_t copy_cfg = {}; /* an empty struct in ESP-IDF v6.1 */
    if (rmt_new_tx_channel(&tx_cfg, &s_tx) != ESP_OK ||
        rmt_new_copy_encoder(&copy_cfg, &s_copy) != ESP_OK || rmt_enable(s_tx) != ESP_OK) {
        return STARK_ERR_IO;
    }
    if (xTaskCreateStaticPinnedToCore(
            ir_task, "stark_ir", (uint32_t)(sizeof s_stack / sizeof s_stack[0]), NULL,
            CONFIG_STARK_IR_TASK_PRIO, s_stack, &s_tcb, IR_CORE) == NULL) {
        return STARK_ERR_NO_MEM;
    }
    s_fitted = true;
    STARK_LOGI("ir", "init rx=%d tx=%d", pins->ir_rx, pins->ir_tx);
    return STARK_OK;
}

stark_err_t stark_ir_rx_start(void)
{
    if (!s_fitted) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (atomic_exchange(&s_rx_on, true)) {
        return STARK_OK;
    }
    if (rmt_enable(s_rx) != ESP_OK) {
        atomic_store(&s_rx_on, false);
        return STARK_ERR_IO;
    }
    arm();
    STARK_LOGI("ir", "rx on");
    return STARK_OK;
}

stark_err_t stark_ir_rx_stop(void)
{
    if (!s_fitted) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (!atomic_exchange(&s_rx_on, false)) {
        return STARK_OK;
    }
    (void)rmt_disable(s_rx);
    STARK_LOGI("ir", "rx off");
    return STARK_OK;
}

stark_err_t stark_ir_transmit(const ir_signal_t *s, uint8_t repeats)
{
    if (!s_fitted) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    ir_tx_plan_t plan;
    STARK_CHECK_RET(ir_tx_check(s, repeats, &plan));
    portENTER_CRITICAL(&s_tx_lock);
    stark_err_t rate =
        ir_tx_rate(s_tx_have_last, s_tx_last_end_ms, now_ms(), CONFIG_STARK_IR_TX_MIN_INTERVAL_MS);
    portEXIT_CRITICAL(&s_tx_lock);
    STARK_CHECK_RET(rate);
    if (atomic_exchange(&s_tx_busy, true)) {
        return STARK_ERR_BUSY;
    }
    s_tx_signal = *s;
    s_tx_repeats = repeats;
    s_tx_plan = plan;
    /* SECURITY_SCOPE §4.5: every transmission is logged before it starts. */
    if (s->frame.protocol == IR_PROTO_UNKNOWN) {
        STARK_LOGI("ir", "tx RAW n=%u carrier=%u dur=%u repeats=%u", (unsigned)s->timings.n,
                   (unsigned)s->carrier_hz, (unsigned)plan.total_ms, (unsigned)repeats);
    } else {
        STARK_LOGI("ir", "tx %s addr=0x%02X cmd=0x%02X carrier=%u dur=%u repeats=%u",
                   ir_protocol_name(s->frame.protocol), (unsigned)s->frame.address,
                   (unsigned)s->frame.command, (unsigned)s->carrier_hz, (unsigned)plan.total_ms,
                   (unsigned)repeats);
    }
    const ir_msg_t msg = {.kind = MSG_TX};
    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        atomic_store(&s_tx_busy, false);
        return STARK_ERR_BUSY;
    }
    return STARK_OK;
}

stark_err_t stark_ir_take(ir_signal_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!s_fitted) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    portENTER_CRITICAL(&s_lock);
    bool got = ir_capture_take(&s_ring, out);
    portEXIT_CRITICAL(&s_lock);
    return got ? STARK_OK : STARK_ERR_NOT_FOUND;
}

stark_err_t stark_ir_stats(stark_ir_stats_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_stats;
    portEXIT_CRITICAL(&s_lock);
    return STARK_OK;
}
