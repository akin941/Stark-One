/*
 * fake_lab.c — the test-only model of the lab's wires (fake_lab.h). Test host and emulator
 * test image only; production never links it.
 */
#include "fake_lab.h"
#include <string.h>

#define MAX_WIRES 4

typedef struct {
    fake_lab_mode_t mode;
    uint8_t pull;
    bool out;
    fake_lab_ext_t ext;
    uint32_t hz; /* the external square wave */
    uint8_t duty;
    uint32_t pwm_hz; /* the PWM output */
    uint8_t pwm_duty;
} pad_t;

typedef struct {
    fake_lab_fault_t kind;
    int pin;
    int count; /* -1 forever */
} fault_t;

static pad_t g_pad[FAKE_LAB_PINS];
static int g_wire_from[MAX_WIRES];
static int g_wire_to[MAX_WIRES];
static int g_wires;
static fault_t g_fault;
static uint64_t g_now_us;
static unsigned g_calls;
static unsigned g_pulses;
static uint32_t g_pulse_us;
static bool g_pulse_level;
static uint32_t g_jitter_ticks;
static uint32_t g_jitter_seed;
static uint32_t g_glitch_every;
static uint32_t g_epoch;

/* the I2C bus */
static const uint8_t k_i2c_addr[FAKE_LAB_I2C_DEVICES] = {0x23, 0x3c, 0x48, 0x68, 0x76};
static uint8_t g_i2c_mask;
static uint8_t g_i2c_file[2][256]; /* 0x68 and 0x76 */
static uint16_t g_i2c_word[4];     /* 0x48 */
static uint8_t g_i2c_ptr[FAKE_LAB_I2C_DEVICES];
static fake_lab_i2c_fault_t g_i2c_fault;
static int g_i2c_fault_addr;
static int g_i2c_fault_count;
static bool g_i2c_drop_seen;
static unsigned g_i2c_calls;
static unsigned g_i2c_writes;
static fake_lab_i2c_probe_fn g_i2c_ext_probe;
static fake_lab_i2c_xfer_fn g_i2c_ext_xfer;

static void uart_reset(void); /* the UART peer's state, below */

static void i2c_reset(void)
{
    g_i2c_mask = FAKE_LAB_I2C_ALL;
    memset(g_i2c_file, 0, sizeof(g_i2c_file));
    g_i2c_file[0][0x75] = 0x68;
    g_i2c_file[0][0x6b] = 0x40;
    g_i2c_file[1][0xd0] = 0x60;
    g_i2c_word[0] = 0x1900;
    g_i2c_word[1] = 0x60a0;
    g_i2c_word[2] = 0x4b00;
    g_i2c_word[3] = 0x5000;
    memset(g_i2c_ptr, 0, sizeof(g_i2c_ptr));
    g_i2c_fault = FAKE_LAB_I2C_FAULT_NONE;
    g_i2c_fault_addr = -1;
    g_i2c_fault_count = 0;
    g_i2c_drop_seen = false;
    g_i2c_calls = 0;
    g_i2c_writes = 0;
    g_i2c_ext_probe = NULL;
    g_i2c_ext_xfer = NULL;
}

static bool valid(int pin)
{
    return pin >= 0 && pin < FAKE_LAB_PINS;
}

void fake_lab_reset(void)
{
    memset(g_pad, 0, sizeof(g_pad));
    memset(g_wire_from, 0, sizeof(g_wire_from));
    memset(g_wire_to, 0, sizeof(g_wire_to));
    g_wires = 0;
    g_fault = (fault_t){0};
    g_now_us = 0;
    g_calls = 0;
    g_pulses = 0;
    g_pulse_us = 0;
    g_pulse_level = false;
    g_jitter_ticks = 0;
    g_jitter_seed = 0;
    g_glitch_every = 0;
    g_epoch = 0;
    i2c_reset();
    uart_reset();
}

void fake_lab_time_us(uint64_t now_us)
{
    g_now_us = now_us;
}

void fake_lab_ext(int pin, fake_lab_ext_t mode, uint32_t hz, uint8_t duty_pct)
{
    if (valid(pin)) {
        g_pad[pin].ext = mode;
        g_pad[pin].hz = hz;
        g_pad[pin].duty = duty_pct;
    }
}

void fake_lab_wire(int from, int to)
{
    if (from < 0) {
        g_wires = 0;
    } else if (valid(from) && valid(to) && g_wires < MAX_WIRES) {
        g_wire_from[g_wires] = from;
        g_wire_to[g_wires] = to;
        g_wires++;
    }
}

void fake_lab_fault(fake_lab_fault_t fault, int pin, int count)
{
    g_fault =
        (fault_t){.kind = count == 0 ? FAKE_LAB_FAULT_NONE : fault, .pin = pin, .count = count};
}

/* True (and one use consumed) when `kind` is armed for this pad. */
static bool fault_hits(fake_lab_fault_t kind, int pin)
{
    if (g_fault.kind != kind || (g_fault.pin >= 0 && g_fault.pin != pin)) {
        return false;
    }
    if (g_fault.count > 0 && --g_fault.count == 0) {
        g_fault.kind = FAKE_LAB_FAULT_NONE;
    }
    return true;
}

bool fake_lab_square(uint32_t hz, uint8_t duty_pct, uint64_t now_us)
{
    uint64_t period = hz > 0 ? 1000000u / hz : 1u;
    if (period == 0) {
        period = 1;
    }
    return (now_us % period) * 100u < period * duty_pct;
}

/* What drives a pad from outside: a steady level or a square wave. */
typedef struct {
    bool square;
    bool level; /* steady */
    uint32_t hz;
    uint8_t duty;
} src_t;

static bool src_level(const src_t *s, uint64_t now_us)
{
    return s->square ? fake_lab_square(s->hz, s->duty, now_us) : s->level;
}

/* The external source of the pad, from the harness or from a wire; false when nothing drives it. */
static bool external_src(int pin, src_t *s)
{
    const pad_t *p = &g_pad[pin];
    switch (p->ext) {
        case FAKE_LAB_EXT_HIGH:
            *s = (src_t){.level = true};
            return true;
        case FAKE_LAB_EXT_LOW:
            *s = (src_t){.level = false};
            return true;
        case FAKE_LAB_EXT_SQUARE:
            *s = (src_t){.square = true, .hz = p->hz, .duty = p->duty};
            return true;
        case FAKE_LAB_EXT_NONE:
        default:
            break;
    }
    for (int i = 0; i < g_wires; i++) {
        if (g_wire_to[i] == pin && g_pad[g_wire_from[i]].mode == FAKE_LAB_PAD_OUT) {
            *s = (src_t){.level = g_pad[g_wire_from[i]].out};
            return true;
        }
        if (g_wire_to[i] == pin && g_pad[g_wire_from[i]].mode == FAKE_LAB_PAD_PWM) {
            *s = (src_t){.square = true,
                         .hz = g_pad[g_wire_from[i]].pwm_hz,
                         .duty = g_pad[g_wire_from[i]].pwm_duty};
            return true;
        }
    }
    return false;
}

/* The level an external source drives onto the pad now; false when nothing drives it. */
static bool external(int pin, bool *level)
{
    src_t s;
    if (!external_src(pin, &s)) {
        return false;
    }
    *level = src_level(&s, g_now_us);
    return true;
}

stark_err_t fake_lab_pad_in(int pin, uint8_t pull)
{
    if (!valid(pin) || pull > 2u) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    if (fault_hits(FAKE_LAB_FAULT_CONFIG_ERROR, pin)) {
        return STARK_ERR_IO;
    }
    g_pad[pin].mode = FAKE_LAB_PAD_IN;
    g_pad[pin].pull = pull;
    return STARK_OK;
}

stark_err_t fake_lab_pad_out(int pin, bool level)
{
    if (!valid(pin)) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    if (fault_hits(FAKE_LAB_FAULT_CONFIG_ERROR, pin)) {
        return STARK_ERR_IO;
    }
    g_pad[pin].mode = FAKE_LAB_PAD_OUT;
    g_pad[pin].pull = 0;
    g_pad[pin].out = level;
    return STARK_OK;
}

stark_err_t fake_lab_pwm_start(int pin, uint32_t hz, uint8_t duty_pct)
{
    if (!valid(pin) || duty_pct > 100u) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    if (fault_hits(FAKE_LAB_FAULT_CONFIG_ERROR, pin)) {
        return STARK_ERR_IO;
    }
    g_pad[pin].mode = FAKE_LAB_PAD_PWM;
    g_pad[pin].pull = 0;
    g_pad[pin].pwm_hz = hz;
    g_pad[pin].pwm_duty = duty_pct;
    return STARK_OK;
}

stark_err_t fake_lab_pulse(int pin, bool level, uint32_t us)
{
    if (!valid(pin)) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    if (fault_hits(FAKE_LAB_FAULT_CONFIG_ERROR, pin)) {
        return STARK_ERR_IO;
    }
    g_pad[pin].mode = FAKE_LAB_PAD_OUT;
    g_pad[pin].pull = 0;
    g_pad[pin].out = !level; /* the pulse is over: the pad rests at the opposite level */
    g_pulses++;
    g_pulse_us = us;
    g_pulse_level = level;
    return STARK_OK;
}

stark_err_t fake_lab_pad_hiz(int pin)
{
    if (!valid(pin)) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    g_pad[pin].mode = FAKE_LAB_PAD_HIZ;
    g_pad[pin].pull = 0;
    g_pad[pin].out = false;
    g_pad[pin].pwm_hz = 0;
    g_pad[pin].pwm_duty = 0;
    return STARK_OK;
}

stark_err_t fake_lab_pad_read(int pin, bool *level)
{
    if (!valid(pin) || level == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    if (fault_hits(FAKE_LAB_FAULT_PAD_STUCK_LOW, pin)) {
        *level = false;
        return STARK_OK;
    }
    if (fault_hits(FAKE_LAB_FAULT_PAD_STUCK_HIGH, pin)) {
        *level = true;
        return STARK_OK;
    }
    const pad_t *p = &g_pad[pin];
    bool ext;
    bool driven = external(pin, &ext);
    if (p->mode == FAKE_LAB_PAD_OUT) {
        *level = driven ? ext : p->out; /* an external source against the output: contention */
    } else if (p->mode == FAKE_LAB_PAD_PWM) {
        *level = driven ? ext : fake_lab_square(p->pwm_hz, p->pwm_duty, g_now_us);
    } else if (driven) {
        *level = ext;
    } else {
        *level = p->pull == 1u; /* nothing drives it: the pull, else floating reads low */
    }
    return STARK_OK;
}

unsigned fake_lab_calls(void)
{
    return g_calls;
}

fake_lab_mode_t fake_lab_mode(int pin)
{
    return valid(pin) ? g_pad[pin].mode : FAKE_LAB_PAD_HIZ;
}

uint8_t fake_lab_pull(int pin)
{
    return valid(pin) ? g_pad[pin].pull : 0;
}

bool fake_lab_out_level(int pin)
{
    return valid(pin) && g_pad[pin].out;
}

uint32_t fake_lab_pwm_hz(int pin)
{
    return valid(pin) ? g_pad[pin].pwm_hz : 0u;
}

uint8_t fake_lab_pwm_duty(int pin)
{
    return valid(pin) ? g_pad[pin].pwm_duty : 0u;
}

unsigned fake_lab_pulses(void)
{
    return g_pulses;
}

uint32_t fake_lab_last_pulse_us(void)
{
    return g_pulse_us;
}

bool fake_lab_last_pulse_level(void)
{
    return g_pulse_level;
}

/* ---- edge capture (STARK-0703) --------------------------------------------------------------- */

void fake_lab_edge_noise(uint32_t jitter_ticks, uint32_t seed)
{
    g_jitter_ticks = jitter_ticks;
    g_jitter_seed = seed;
}

void fake_lab_edge_glitches(uint32_t every_edges)
{
    g_glitch_every = every_edges;
}

void fake_lab_edge_epoch(uint32_t ticks)
{
    g_epoch = ticks;
}

/* A deterministic offset in -jitter..+jitter for edge number `k` (xorshift of seed and k). */
static int64_t jitter_of(uint64_t k)
{
    if (g_jitter_ticks == 0u) {
        return 0;
    }
    uint32_t x = g_jitter_seed ^ (uint32_t)(k * 2654435761u) ^ 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return (int64_t)(x % (2u * g_jitter_ticks + 1u)) - (int64_t)g_jitter_ticks;
}

typedef struct {
    uint32_t *ts;
    size_t cap;
    uint32_t limit;
    uint32_t seen;
    uint64_t prev; /* the last stamp handed out (strictly increasing) */
    bool any;
} sink_t;

/* One edge at `t` ticks; false once it was the last the limit allows (callers stop there). */
static bool sink_edge(sink_t *k, uint64_t t)
{
    if (k->any && t <= k->prev) {
        t = k->prev + 1u;
    }
    if (k->seen < k->cap) {
        k->ts[k->seen] = (uint32_t)(t + g_epoch);
    }
    k->seen++;
    k->prev = t;
    k->any = true;
    return k->seen < k->limit;
}

/* The edges of a square wave in [start, end), the wave anchored at tick 0. */
static void square_edges(sink_t *k, uint32_t hz, uint8_t duty, uint64_t start, uint64_t end,
                         bool *first_level)
{
    uint64_t period = hz > 0u ? FAKE_LAB_TICK_HZ / hz : 0u;
    uint64_t high = period * duty / 100u;
    if (period < 2u || high == 0u || high >= period) {
        *first_level = period >= 2u && high >= period; /* steady */
        return;
    }
    uint64_t n0 = start / period;
    *first_level = (start - n0 * period) < high;
    bool first = true;
    for (uint64_t n = n0;; n++) {
        uint64_t rise = n * period;
        if (rise >= end) {
            break;
        }
        for (int edge = 0; edge < 2; edge++) {
            uint64_t t = edge == 0 ? rise : rise + high;
            if (t < start || t >= end) {
                continue;
            }
            if (first) {
                *first_level = edge != 0; /* the level before the first edge: opposite of its own */
                first = false;
            }
            int64_t moved = (int64_t)t + jitter_of(n * 2u + (uint64_t)edge);
            if (!sink_edge(k, moved < 0 ? 0u : (uint64_t)moved)) {
                return;
            }
            if (g_glitch_every > 0u && k->seen % g_glitch_every == 0u) {
                uint64_t gap = edge == 0 ? high : period - high; /* to the next real edge */
                if (gap > 480u) { /* a pair of 0.5 us edges that leaves the level as it was */
                    uint64_t base = (uint64_t)moved;
                    if (!sink_edge(k, base + 120u) || !sink_edge(k, base + 240u)) {
                        return;
                    }
                }
            }
        }
    }
}

/* ---- the UART peer (STARK-0705): state and the edges of a talking peer ------------------------
 */

#define UART_Q       256u /* characters in flight between the peer and the device */
#define UART_FMTS    5u
#define UART_LINES   50u
#define UART_LINE    26u /* "stark uart scene line NN\r\n" */
#define UART_BIN_LEN 1024u

typedef struct {
    uint64_t t_ns;
    uint8_t b;
    uint8_t fe, pe;
} uart_char_t;

static struct {
    int tx_pin, rx_pin;
    fake_lab_uart_mode_t mode;
    uint32_t peer_baud;
    uint8_t peer_fmt;
    bool open, tx;
    uint32_t baud;
    uint8_t fmt;
    size_t rx_buf;
    uint64_t t_open_ns, tx_free_ns, peer_free_ns;
    uart_char_t q[UART_Q];
    size_t qh, qn;
    size_t stream_next; /* the next scene byte to arrive */
    unsigned dropped;
} g_uart = {.tx_pin = -1, .rx_pin = -1};

/* bits per character: start, data, parity, stop; 0 for a format that does not exist */
static const struct {
    uint8_t data;
    char parity;
    uint8_t stop;
} k_uart_fmt[UART_FMTS] = {{8, 'N', 1}, {8, 'E', 1}, {8, 'O', 1}, {8, 'N', 2}, {7, 'E', 1}};

uint32_t fake_lab_uart_bits(uint8_t fmt)
{
    if (fmt >= UART_FMTS) {
        return 0;
    }
    return 1u + k_uart_fmt[fmt].data + (k_uart_fmt[fmt].parity != 'N' ? 1u : 0u) +
           k_uart_fmt[fmt].stop;
}

static uint64_t char_ns(uint32_t baud, uint8_t fmt)
{
    return baud == 0u ? 0u : (uint64_t)fake_lab_uart_bits(fmt) * 1000000000u / baud;
}

size_t fake_lab_uart_scene_len(fake_lab_uart_mode_t mode)
{
    if (mode == FAKE_LAB_UART_TEXT) {
        return UART_LINES * UART_LINE;
    }
    return mode == FAKE_LAB_UART_BINARY ? UART_BIN_LEN : 0u;
}

uint8_t fake_lab_uart_scene_byte(fake_lab_uart_mode_t mode, size_t j)
{
    if (mode == FAKE_LAB_UART_BINARY) {
        return (uint8_t)(j * 7u + 3u);
    }
    static const char k_text[] = "stark uart scene line ";
    size_t line = j / UART_LINE;
    size_t col = j % UART_LINE;
    if (col < 22u) {
        return (uint8_t)k_text[col];
    }
    if (col == 22u) {
        return (uint8_t)('0' + (line / 10u) % 10u);
    }
    if (col == 23u) {
        return (uint8_t)('0' + line % 10u);
    }
    return col == 24u ? (uint8_t)'\r' : (uint8_t)'\n';
}

/* What a receiver set to (rx_baud, rx_fmt) makes of a byte sent at (tx_baud, tx_fmt): the byte
 * itself when the baud is within 3 % and the format the same, else garbage and a framing error
 * (a parity error when the receiver checks parity). The garbage is a fixed bijection that never
 * returns to the original after two legs, so an echo through a mismatched peer differs too. */
static uint8_t deliver(uint8_t b, uint32_t tx_baud, uint8_t tx_fmt, uint32_t rx_baud,
                       uint8_t rx_fmt, uint8_t *fe, uint8_t *pe)
{
    uint32_t diff = tx_baud > rx_baud ? tx_baud - rx_baud : rx_baud - tx_baud;
    *fe = 0;
    *pe = 0;
    if ((uint64_t)diff * 100u > (uint64_t)rx_baud * 3u) {
        *fe = 1;
        return (uint8_t)(b * 5u + 1u);
    }
    if (tx_fmt != rx_fmt) {
        if (rx_fmt < UART_FMTS && k_uart_fmt[rx_fmt].parity != 'N') {
            *pe = 1;
        } else {
            *fe = 1;
        }
        return (uint8_t)(b * 5u + 1u);
    }
    return b;
}

static bool uart_stream_mode(void)
{
    return g_uart.mode == FAKE_LAB_UART_TEXT || g_uart.mode == FAKE_LAB_UART_BINARY;
}

/*
 * The edges of the peer talking on the RX pad over [start, end) ticks: the scene, again and again,
 * back to back from `start`, each character a start bit, the data bits least significant first, the
 * parity bit if the format has one and the stop bits, at the peer's baud. The line idles high.
 * Bit boundaries are computed from the start, never accumulated, so the baud has no drift.
 */
static void uart_rx_edges(sink_t *k, uint64_t start, uint64_t end, bool *first_level)
{
    *first_level = true;
    size_t len = fake_lab_uart_scene_len(g_uart.mode);
    uint32_t bits = fake_lab_uart_bits(g_uart.peer_fmt);
    if (len == 0u || bits == 0u || g_uart.peer_baud == 0u) {
        return;
    }
    uint64_t m = 0; /* the bit number since `start` */
    bool level = true;
    uint64_t edge_no = 0;
    for (size_t j = 0;; j = (j + 1u) % len) {
        uint8_t c = fake_lab_uart_scene_byte(g_uart.mode, j);
        bool seq[16];
        size_t n = 0;
        uint8_t ones = 0;
        seq[n++] = false; /* the start bit */
        for (uint8_t d = 0; d < k_uart_fmt[g_uart.peer_fmt].data; d++) {
            bool bit = ((c >> d) & 1u) != 0u;
            ones = (uint8_t)(ones + (bit ? 1u : 0u));
            seq[n++] = bit;
        }
        char par = k_uart_fmt[g_uart.peer_fmt].parity;
        if (par != 'N') {
            seq[n++] = par == 'E' ? (ones & 1u) != 0u : (ones & 1u) == 0u;
        }
        for (uint8_t s = 0; s < k_uart_fmt[g_uart.peer_fmt].stop; s++) {
            seq[n++] = true;
        }
        for (size_t i = 0; i < n; i++, m++) {
            uint64_t t = start + m * FAKE_LAB_TICK_HZ / g_uart.peer_baud;
            if (t >= end) {
                return;
            }
            if (seq[i] != level) {
                level = seq[i];
                int64_t moved = (int64_t)t + jitter_of(edge_no++);
                if (!sink_edge(k, moved < 0 ? 0u : (uint64_t)moved)) {
                    return;
                }
            }
        }
    }
}

stark_err_t fake_lab_pad_edges(int pin, uint32_t window_us, uint32_t *ts, size_t cap, size_t *n,
                               uint32_t *seen, bool *first_level)
{
    if (!valid(pin) || n == NULL || seen == NULL || first_level == NULL ||
        (cap > 0u && ts == NULL) || window_us == 0u) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    if (fault_hits(FAKE_LAB_FAULT_CONFIG_ERROR, pin)) {
        return STARK_ERR_IO;
    }
    sink_t k = {.ts = ts, .cap = cap, .limit = cap > 0u ? (uint32_t)cap + 1u : FAKE_LAB_GATE_LIMIT};
    *first_level = false;
    bool stuck_low = fault_hits(FAKE_LAB_FAULT_PAD_STUCK_LOW, pin);
    bool stuck_high = !stuck_low && fault_hits(FAKE_LAB_FAULT_PAD_STUCK_HIGH, pin);
    if (stuck_low || stuck_high) {
        *first_level = stuck_high;
    } else {
        const pad_t *p = &g_pad[pin];
        src_t s;
        if (pin == g_uart.rx_pin && uart_stream_mode()) {
            uint64_t from = g_now_us * FAKE_LAB_TICKS_PER_US; /* a peer that is talking */
            uart_rx_edges(&k, from, from + (uint64_t)window_us * FAKE_LAB_TICKS_PER_US,
                          first_level);
            *seen = k.seen;
            *n = k.seen < k.cap ? k.seen : k.cap;
            return STARK_OK;
        }
        if (!external_src(pin, &s)) {
            if (p->mode == FAKE_LAB_PAD_PWM) {
                s = (src_t){.square = true, .hz = p->pwm_hz, .duty = p->pwm_duty};
            } else if (p->mode == FAKE_LAB_PAD_OUT) {
                s = (src_t){.level = p->out};
            } else {
                s = (src_t){.level = p->pull == 1u}; /* nothing drives it: the pull */
            }
        }
        uint64_t start = g_now_us * FAKE_LAB_TICKS_PER_US;
        if (s.square) {
            square_edges(&k, s.hz, s.duty, start,
                         start + (uint64_t)window_us * FAKE_LAB_TICKS_PER_US, first_level);
        } else {
            *first_level = s.level;
        }
    }
    *seen = k.seen;
    *n = k.seen < k.cap ? k.seen : k.cap;
    return STARK_OK;
}

/* ---- the I2C bus (STARK-0704) -----------------------------------------------------------------
 */

void fake_lab_i2c_ext(fake_lab_i2c_probe_fn probe, fake_lab_i2c_xfer_fn xfer)
{
    g_i2c_ext_probe = probe;
    g_i2c_ext_xfer = xfer;
}

void fake_lab_i2c_devices(uint8_t mask)
{
    g_i2c_mask = mask & FAKE_LAB_I2C_ALL;
}

void fake_lab_i2c_fault(fake_lab_i2c_fault_t kind, int addr, int count)
{
    g_i2c_fault = count == 0 ? FAKE_LAB_I2C_FAULT_NONE : kind;
    g_i2c_fault_addr = addr;
    g_i2c_fault_count = count;
    g_i2c_drop_seen = false;
}

/* The device of the five at this address and on the bus, or -1. */
static int i2c_device(uint8_t addr)
{
    for (int i = 0; i < FAKE_LAB_I2C_DEVICES; i++) {
        if (k_i2c_addr[i] == addr && (g_i2c_mask & (1u << i)) != 0u) {
            return i;
        }
    }
    return -1;
}

/* What an armed fault does to this call: STARK_OK lets it through. */
static stark_err_t i2c_fault_for(uint8_t addr)
{
    switch (g_i2c_fault) {
        case FAKE_LAB_I2C_STUCK:
            return STARK_ERR_TIMEOUT;
        case FAKE_LAB_I2C_NACK_ALL:
            return STARK_ERR_NOT_FOUND;
        case FAKE_LAB_I2C_TIMEOUT:
            if (g_i2c_fault_addr >= 0 && g_i2c_fault_addr != (int)addr) {
                return STARK_OK;
            }
            if (g_i2c_fault_count > 0 && --g_i2c_fault_count == 0) {
                g_i2c_fault = FAKE_LAB_I2C_FAULT_NONE;
            }
            return STARK_ERR_TIMEOUT;
        case FAKE_LAB_I2C_DROP_DEVICE:
            if (g_i2c_fault_addr != (int)addr) {
                return STARK_OK;
            }
            if (!g_i2c_drop_seen) {
                g_i2c_drop_seen = true;
                return STARK_OK;
            }
            return STARK_ERR_NOT_FOUND;
        case FAKE_LAB_I2C_FAULT_NONE:
        case FAKE_LAB_I2C_FAULT_COUNT:
        default:
            return STARK_OK;
    }
}

stark_err_t fake_lab_i2c_probe(uint8_t addr)
{
    g_i2c_calls++;
    stark_err_t err = i2c_fault_for(addr);
    if (err != STARK_OK) {
        return err;
    }
    if (i2c_device(addr) >= 0) {
        return STARK_OK;
    }
    return g_i2c_ext_probe != NULL ? g_i2c_ext_probe(addr) : STARK_ERR_NOT_FOUND;
}

/* The bytes of a write: the pointer, then the data from it. True when a register changed or the
 * device took a command. */
static bool i2c_apply_write(int dev, const uint8_t *w, size_t wn)
{
    bool stored = false;
    if (dev == 2) {
        g_i2c_ptr[dev] = (uint8_t)(w[0] & 3u);
        for (size_t i = 1; i + 1u < wn; i += 2u) {
            if (g_i2c_ptr[dev] != 0u) { /* register 0 is the read-only reading */
                g_i2c_word[g_i2c_ptr[dev]] = (uint16_t)((w[i] << 8) | w[i + 1u]);
                stored = true;
            }
        }
    } else if (dev == 3 || dev == 4) {
        uint8_t *file = g_i2c_file[dev - 3];
        uint8_t id_reg = dev == 3 ? 0x75u : 0xd0u; /* the chip id: read-only */
        uint8_t p = w[0];
        for (size_t i = 1; i < wn; i++) {
            if (p != id_reg) {
                file[p] = w[i];
                stored = true;
            }
            p++; /* wraps at 256 */
        }
        g_i2c_ptr[dev] = p;
    }
    return stored || (dev < 2 && wn > 1u);
}

static void i2c_fill_read(int dev, uint8_t *r, size_t rn)
{
    for (size_t i = 0; i < rn; i++) {
        switch (dev) {
            case 0:
                r[i] = i == 0u ? 0x01u : (i == 1u ? 0x23u : 0u);
                break;
            case 2: {
                uint16_t word = g_i2c_word[g_i2c_ptr[dev] & 3u];
                r[i] = (uint8_t)((i & 1u) == 0u ? word >> 8 : word & 0xFFu);
                break;
            }
            case 3:
            case 4:
                r[i] = g_i2c_file[dev - 3][g_i2c_ptr[dev]++];
                break;
            case 1:
            default:
                r[i] = 0;
                break;
        }
    }
}

stark_err_t fake_lab_i2c_xfer(uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn)
{
    g_i2c_calls++;
    stark_err_t err = i2c_fault_for(addr);
    if (err != STARK_OK) {
        return err;
    }
    int dev = i2c_device(addr);
    if (dev < 0) {
        if (g_i2c_ext_xfer != NULL) {
            return g_i2c_ext_xfer(addr, w, wn, r, rn);
        }
        return STARK_ERR_NOT_FOUND;
    }
    if (wn > 0u && i2c_apply_write(dev, w, wn)) {
        g_i2c_writes++;
    }
    if (rn > 0u) {
        i2c_fill_read(dev, r, rn);
    }
    return STARK_OK;
}

unsigned fake_lab_i2c_calls(void)
{
    return g_i2c_calls;
}

unsigned fake_lab_i2c_writes(void)
{
    return g_i2c_writes;
}

uint8_t fake_lab_i2c_reg(uint8_t addr, uint8_t reg)
{
    if (addr == 0x68u) {
        return g_i2c_file[0][reg];
    }
    return addr == 0x76u ? g_i2c_file[1][reg] : 0u;
}

uint16_t fake_lab_i2c_word(uint8_t reg)
{
    return g_i2c_word[reg & 3u];
}

/* ---- the UART peer (STARK-0705, ADR-0023 §7) ------------------------------------------------- */

static void uart_reset(void)
{
    memset(&g_uart, 0, sizeof g_uart);
    g_uart.tx_pin = -1;
    g_uart.rx_pin = -1;
}

void fake_lab_uart_pins(int tx, int rx)
{
    g_uart.tx_pin = tx;
    g_uart.rx_pin = rx;
}

void fake_lab_uart_peer(fake_lab_uart_mode_t mode, uint32_t baud, uint8_t fmt)
{
    if ((unsigned)mode < (unsigned)FAKE_LAB_UART_MODES && fmt < UART_FMTS) {
        g_uart.mode = mode;
        g_uart.peer_baud = baud;
        g_uart.peer_fmt = fmt;
    }
}

stark_err_t fake_lab_uart_open(uint32_t baud, uint8_t fmt, size_t rx_buf, bool tx)
{
    if (baud == 0u || fmt >= UART_FMTS || rx_buf == 0u) {
        return STARK_ERR_INVALID_ARG;
    }
    if (g_uart.open) {
        return STARK_ERR_STATE;
    }
    uint64_t now_ns = g_now_us * 1000u;
    g_uart.open = true;
    g_uart.tx = tx;
    g_uart.baud = baud;
    g_uart.fmt = fmt;
    g_uart.rx_buf = rx_buf;
    g_uart.qh = 0;
    g_uart.qn = 0;
    g_uart.stream_next = 0;
    g_uart.t_open_ns = now_ns;
    g_uart.tx_free_ns = now_ns;
    g_uart.peer_free_ns = now_ns;
    return STARK_OK;
}

void fake_lab_uart_close(void)
{
    g_uart.open = false;
    g_uart.tx = false;
    g_uart.qn = 0;
}

stark_err_t fake_lab_uart_set_baud(uint32_t baud)
{
    if (!g_uart.open) {
        return STARK_ERR_STATE;
    }
    if (baud == 0u) {
        return STARK_ERR_INVALID_ARG;
    }
    g_uart.baud = baud;
    return STARK_OK;
}

bool fake_lab_uart_is_open(void)
{
    return g_uart.open;
}

bool fake_lab_uart_tx(void)
{
    return g_uart.open && g_uart.tx;
}

static void uart_enqueue(uint64_t t_ns, uint8_t b, uint8_t fe, uint8_t pe)
{
    if (g_uart.qn == UART_Q) {
        g_uart.dropped++;
        return;
    }
    g_uart.q[(g_uart.qh + g_uart.qn) % UART_Q] = (uart_char_t){t_ns, b, fe, pe};
    g_uart.qn++;
}

uint32_t fake_lab_uart_wire_us(size_t n)
{
    return (uint32_t)(((uint64_t)n * char_ns(g_uart.baud, g_uart.fmt) + 999u) / 1000u);
}

size_t fake_lab_uart_write(const uint8_t *buf, size_t n)
{
    if (!fake_lab_uart_tx() || buf == NULL) {
        return 0;
    }
    uint64_t now_ns = g_now_us * 1000u;
    uint64_t dev_ct = char_ns(g_uart.baud, g_uart.fmt);
    uint64_t peer_ct = char_ns(g_uart.peer_baud, g_uart.peer_fmt);
    for (size_t i = 0; i < n; i++) {
        uint64_t start = g_uart.tx_free_ns > now_ns ? g_uart.tx_free_ns : now_ns;
        uint64_t end = start + dev_ct;
        g_uart.tx_free_ns = end;
        if (g_uart.mode == FAKE_LAB_UART_LOOP) {
            uart_enqueue(end, buf[i], 0, 0);
        } else if (g_uart.mode == FAKE_LAB_UART_ECHO) {
            uint8_t fe, pe;
            uint8_t at_peer = deliver(buf[i], g_uart.baud, g_uart.fmt, g_uart.peer_baud,
                                      g_uart.peer_fmt, &fe, &pe);
            uint64_t echo_start = g_uart.peer_free_ns > end ? g_uart.peer_free_ns : end;
            g_uart.peer_free_ns = echo_start + peer_ct;
            uint8_t back = deliver(at_peer, g_uart.peer_baud, g_uart.peer_fmt, g_uart.baud,
                                   g_uart.fmt, &fe, &pe);
            uart_enqueue(g_uart.peer_free_ns, back, fe, pe);
        }
    }
    return n;
}

size_t fake_lab_uart_read(uint8_t *buf, size_t max, uint32_t *framing, uint32_t *parity,
                          uint32_t *overrun)
{
    if (!g_uart.open || buf == NULL) {
        return 0;
    }
    uint64_t now_ns = g_now_us * 1000u;
    uint64_t peer_ct = char_ns(g_uart.peer_baud, g_uart.peer_fmt);
    size_t len = uart_stream_mode() ? fake_lab_uart_scene_len(g_uart.mode) : 0u;
    /* the stream bytes that have arrived by now: byte j ends at t_open + (j + 1) * peer_ct */
    size_t arrived = 0;
    if (len > 0u && peer_ct > 0u && now_ns > g_uart.t_open_ns) {
        uint64_t whole = (now_ns - g_uart.t_open_ns) / peer_ct;
        arrived = whole < len ? (size_t)whole : len;
    }
    if (arrived > g_uart.stream_next && arrived - g_uart.stream_next > g_uart.rx_buf) {
        g_uart.stream_next = arrived - g_uart.rx_buf; /* the driver's buffer overflowed */
        if (overrun != NULL) {
            (*overrun)++;
        }
    }
    size_t got = 0;
    while (got < max) {
        if (g_uart.qn > 0u && g_uart.q[g_uart.qh].t_ns <= now_ns) {
            uart_char_t c = g_uart.q[g_uart.qh];
            g_uart.qh = (g_uart.qh + 1u) % UART_Q;
            g_uart.qn--;
            buf[got++] = c.b;
            if (framing != NULL) {
                *framing += c.fe;
            }
            if (parity != NULL) {
                *parity += c.pe;
            }
        } else if (g_uart.stream_next < arrived) {
            uint8_t fe, pe;
            buf[got++] =
                deliver(fake_lab_uart_scene_byte(g_uart.mode, g_uart.stream_next), g_uart.peer_baud,
                        g_uart.peer_fmt, g_uart.baud, g_uart.fmt, &fe, &pe);
            g_uart.stream_next++;
            if (framing != NULL) {
                *framing += fe;
            }
            if (parity != NULL) {
                *parity += pe;
            }
        } else {
            break;
        }
    }
    return got;
}

unsigned fake_lab_uart_dropped(void)
{
    return g_uart.dropped;
}
