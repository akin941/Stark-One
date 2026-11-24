/*
 * stark_hal_host.c — fake HAL for host tests.
 * Implements components/stark_hal/include/stark_hal.h exactly (the same
 * header stark_hal_esp.c implements — STARK-0007 AC #1); test-only
 * control/inspection lives in stark_hal_host_control.h instead.
 */
#include "stark_hal.h"
#include "stark_hal_host_control.h"
#include <string.h>

/* ---- Time --------------------------------------------------------------- */

static uint64_t g_fake_now_us = 0;

uint64_t stark_hal_now_us(void)
{
    return g_fake_now_us;
}

static unsigned g_fail;
static uint64_t g_delayed_us;
static unsigned g_delay_calls;
static void (*g_delay_hook)(unsigned call);

void stark_hal_delay_ms(uint32_t ms)
{
    /* no waiting in host tests — tests never sleep; the time is only counted */
    g_delayed_us += (uint64_t)ms * 1000u;
    g_delay_calls++;
    if (g_delay_hook != NULL) {
        g_delay_hook(g_delay_calls);
    }
}

void stark_hal_delay_us(uint32_t us)
{
    g_delayed_us += us;
}

uint64_t hal_host_delayed_us(void)
{
    return g_delayed_us;
}

void hal_host_fail(unsigned mask)
{
    g_fail = mask;
}

void hal_host_set_delay_hook(void (*hook)(unsigned call))
{
    g_delay_hook = hook;
    g_delay_calls = 0;
}

void hal_host_set_now_us(uint64_t now_us)
{
    g_fake_now_us = now_us;
}

/* ---- GPIO --------------------------------------------------------------- */

#define MAX_GPIO 49 /* ESP32-S3: GPIO0-48 */

typedef struct {
    bool configured;
    bool is_output;
    bool level;
    bool pullup;
} gpio_state_t;

static gpio_state_t g_gpio_state[MAX_GPIO];

stark_err_t stark_hal_gpio_config_input(int pin, bool pullup)
{
    if (pin < 0 || pin >= MAX_GPIO) {
        return STARK_ERR_INVALID_ARG;
    }
    g_gpio_state[pin].configured = true;
    g_gpio_state[pin].is_output = false;
    g_gpio_state[pin].pullup = pullup;
    g_gpio_state[pin].level = pullup; /* pulled high when floating */
    return STARK_OK;
}

stark_err_t stark_hal_gpio_config_output(int pin, bool initial)
{
    if (pin < 0 || pin >= MAX_GPIO) {
        return STARK_ERR_INVALID_ARG;
    }
    g_gpio_state[pin].configured = true;
    g_gpio_state[pin].is_output = true;
    g_gpio_state[pin].pullup = false;
    g_gpio_state[pin].level = initial;
    return STARK_OK;
}

bool stark_hal_gpio_read(int pin)
{
    if (pin < 0 || pin >= MAX_GPIO) {
        return false;
    }
    return g_gpio_state[pin].level;
}

void stark_hal_gpio_write(int pin, bool level)
{
    if (pin < 0 || pin >= MAX_GPIO) {
        return;
    }
    g_gpio_state[pin].level = level;
}

/* ---- Lab pads (STARK-0701) -------------------------------------------------- */

static hal_host_pad_t g_pads[MAX_GPIO];
static unsigned g_pad_calls;

stark_err_t stark_hal_gpio_config_in(int pin, stark_hal_pull_t pull)
{
    if (pin < 0 || pin >= MAX_GPIO) {
        return STARK_ERR_INVALID_ARG;
    }
    g_pad_calls++;
    g_pads[pin] = (hal_host_pad_t){.mode = HAL_HOST_PAD_IN, .pull = pull};
    g_gpio_state[pin].configured = true;
    g_gpio_state[pin].is_output = false;
    g_gpio_state[pin].pullup = pull == STARK_HAL_PULL_UP;
    g_gpio_state[pin].level = pull == STARK_HAL_PULL_UP; /* floating reads low */
    return STARK_OK;
}

stark_err_t stark_hal_gpio_hiz(int pin)
{
    return stark_hal_gpio_config_in(pin, STARK_HAL_PULL_NONE);
}

stark_err_t stark_hal_gpio_config_out_weak(int pin, bool initial)
{
    if (pin < 0 || pin >= MAX_GPIO) {
        return STARK_ERR_INVALID_ARG;
    }
    if ((g_fail & HAL_HOST_FAIL_OUT_WEAK) != 0u) {
        return STARK_ERR_IO;
    }
    g_pad_calls++;
    g_pads[pin] = (hal_host_pad_t){.mode = HAL_HOST_PAD_OUT, .weak = true, .level = initial};
    g_gpio_state[pin].configured = true;
    g_gpio_state[pin].is_output = true;
    g_gpio_state[pin].pullup = false;
    g_gpio_state[pin].level = initial;
    return STARK_OK;
}

stark_err_t stark_hal_gpio_drive_weak(int pin)
{
    if (pin < 0 || pin >= MAX_GPIO) {
        return STARK_ERR_INVALID_ARG;
    }
    if ((g_fail & HAL_HOST_FAIL_DRIVE_WEAK) != 0u) {
        return STARK_ERR_IO;
    }
    g_pad_calls++;
    g_pads[pin].weak = true;
    return STARK_OK;
}

/* ---- Edge capture (STARK-0703) -------------------------------------------------------------- */

#define HOST_EDGES_MAX 2048

static uint32_t g_edge_ts[HOST_EDGES_MAX];
static size_t g_edge_ts_n;
static uint32_t g_edge_seen;
static bool g_edge_first;
static volatile bool g_edge_cancel;
static int g_edge_irq_pin = -1;
static hal_host_edges_call_t g_edge_call;
static unsigned g_edge_calls;
static void (*g_edge_hook)(int pin);

void hal_host_edges_set(const uint32_t *ts, size_t n, uint32_t seen, bool first_level)
{
    g_edge_ts_n = n < HOST_EDGES_MAX ? n : HOST_EDGES_MAX;
    for (size_t i = 0; i < g_edge_ts_n; i++) {
        g_edge_ts[i] = ts[i];
    }
    g_edge_seen = seen;
    g_edge_first = first_level;
}

void hal_host_set_edges_hook(void (*hook)(int pin))
{
    g_edge_hook = hook;
}

int hal_host_edge_irq_pin(void)
{
    return g_edge_irq_pin;
}

unsigned hal_host_edges_calls(void)
{
    return g_edge_calls;
}

const hal_host_edges_call_t *hal_host_edges_last(void)
{
    return &g_edge_call;
}

uint32_t stark_hal_tick_hz(void)
{
    return 240000000u;
}

void stark_hal_gpio_edges_cancel(void)
{
    g_edge_cancel = true;
}

stark_err_t stark_hal_gpio_edges(int pin, uint32_t window_us, uint32_t *ts, size_t cap, size_t *n,
                                 uint32_t *seen, bool *first_level)
{
    if (pin < 0 || pin >= MAX_GPIO || n == NULL || seen == NULL || first_level == NULL ||
        (cap > 0u && ts == NULL) || window_us == 0u || window_us > 4100000u) {
        return STARK_ERR_INVALID_ARG;
    }
    g_edge_calls++;
    g_edge_call = (hal_host_edges_call_t){
        .pin = pin, .window_us = window_us, .cap = cap, .level_before = g_gpio_state[pin].level};
    g_edge_cancel = false;
    if ((g_fail & HAL_HOST_FAIL_EDGES) != 0u) {
        return STARK_ERR_IO;
    }
    g_edge_irq_pin = pin; /* the interrupt is on for the window only */
    if (g_edge_hook != NULL) {
        g_edge_hook(pin);
    }
    g_edge_irq_pin = -1;
    g_delayed_us += window_us;
    if (g_edge_cancel) {
        return STARK_ERR_STATE;
    }
    uint32_t limit = cap > 0u ? (uint32_t)cap + 1u : STARK_HAL_EDGES_GATE_MAX;
    uint32_t count = g_edge_seen < limit ? g_edge_seen : limit;
    size_t stored = g_edge_ts_n < cap ? g_edge_ts_n : cap;
    for (size_t i = 0; i < stored; i++) {
        ts[i] = g_edge_ts[i];
    }
    *n = stored;
    *seen = count;
    *first_level = g_edge_first;
    return STARK_OK;
}

const hal_host_pad_t *hal_host_pad(int pin)
{
    return (pin < 0 || pin >= MAX_GPIO) ? NULL : &g_pads[pin];
}

unsigned hal_host_pad_calls(void)
{
    return g_pad_calls;
}

void hal_host_gpio_reset(void)
{
    g_fail = 0;
    g_delayed_us = 0;
    g_delay_calls = 0;
    g_delay_hook = NULL;
    memset(g_pads, 0, sizeof(g_pads));
    g_pad_calls = 0;
    memset(g_gpio_state, 0, sizeof(g_gpio_state));
    g_edge_ts_n = 0;
    g_edge_seen = 0;
    g_edge_first = false;
    g_edge_cancel = false;
    g_edge_irq_pin = -1;
    g_edge_call = (hal_host_edges_call_t){0};
    g_edge_calls = 0;
    g_edge_hook = NULL;
}

/* ---- PWM (LEDC) -----------------------------------------------------------
 * No fake hardware to drive — these just record their arguments for tests
 * to inspect (TASKS.md STARK-0007 host-test requirement). */

static hal_host_pwm_init_call_t g_pwm_init_call;
static hal_host_pwm_set_freq_call_t g_pwm_set_freq_call;
static hal_host_pwm_set_duty_call_t g_pwm_set_duty_call;
static hal_host_pwm_stop_call_t g_pwm_stop_call;
static unsigned g_pwm_stop_calls;

stark_err_t stark_hal_pwm_init(int pin, uint32_t hz, uint8_t channel)
{
    if ((g_fail & HAL_HOST_FAIL_PWM_INIT) != 0u) {
        return STARK_ERR_IO;
    }
    g_pwm_init_call.called = true;
    g_pwm_init_call.pin = pin;
    g_pwm_init_call.hz = hz;
    g_pwm_init_call.channel = channel;
    if (pin >= 0 && pin < MAX_GPIO) {
        g_pads[pin] =
            (hal_host_pad_t){.mode = HAL_HOST_PAD_PWM}; /* routed to LEDC, not a GPIO level */
    }
    return STARK_OK;
}

stark_err_t stark_hal_pwm_set_freq(uint8_t channel, uint32_t hz)
{
    g_pwm_set_freq_call.called = true;
    g_pwm_set_freq_call.channel = channel;
    g_pwm_set_freq_call.hz = hz;
    return STARK_OK;
}

stark_err_t stark_hal_pwm_set_duty_pct(uint8_t channel, uint8_t pct)
{
    if ((g_fail & HAL_HOST_FAIL_PWM_DUTY) != 0u) {
        return STARK_ERR_IO;
    }
    if (pct > 100) {
        pct = 100;
    }
    g_pwm_set_duty_call.called = true;
    g_pwm_set_duty_call.channel = channel;
    g_pwm_set_duty_call.pct = pct;
    return STARK_OK;
}

stark_err_t stark_hal_pwm_stop(uint8_t channel)
{
    g_pwm_stop_call.called = true;
    g_pwm_stop_call.channel = channel;
    g_pwm_stop_calls++;
    return STARK_OK;
}

const hal_host_pwm_stop_call_t *hal_host_pwm_last_stop(void)
{
    return &g_pwm_stop_call;
}

unsigned hal_host_pwm_stops(void)
{
    return g_pwm_stop_calls;
}

const hal_host_pwm_init_call_t *hal_host_pwm_last_init(void)
{
    return &g_pwm_init_call;
}

const hal_host_pwm_set_freq_call_t *hal_host_pwm_last_set_freq(void)
{
    return &g_pwm_set_freq_call;
}

const hal_host_pwm_set_duty_call_t *hal_host_pwm_last_set_duty(void)
{
    return &g_pwm_set_duty_call;
}

void hal_host_pwm_reset(void)
{
    memset(&g_pwm_init_call, 0, sizeof(g_pwm_init_call));
    memset(&g_pwm_set_freq_call, 0, sizeof(g_pwm_set_freq_call));
    memset(&g_pwm_set_duty_call, 0, sizeof(g_pwm_set_duty_call));
    memset(&g_pwm_stop_call, 0, sizeof(g_pwm_stop_call));
    g_pwm_stop_calls = 0;
}
