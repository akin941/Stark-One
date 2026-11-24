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
