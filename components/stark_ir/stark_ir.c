/*
 * stark_ir.c — the RMT port of stark_ir (stark_ir.h). Target-only; every
 * decision about timings, protocols and the capture ring is the pure core
 * (ir_symbols.c, ir_codec.c, ir_capture.c — host-tested).
 *
 * The RX done callback runs in the ISR: it only hands the symbol count to
 * the IR task through a queue. The task converts, decodes, stores, logs one
 * line and re-arms the receiver.
 */
#include "stark_ir.h"
#include <stdatomic.h>
#include <stdbool.h>
#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ir_capture.h"
#include "ir_codec.h"
#include "ir_symbols.h"
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

typedef enum { MSG_RX = 1 } ir_msg_kind_t;

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
