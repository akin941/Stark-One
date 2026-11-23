/*
 * subghz_svc.c — the module service's decisions (subghz_svc.h). Pure.
 */
#include "subghz_svc.h"
#include <string.h>

void subghz_svc_init(subghz_svc_t *s, const subghz_svc_io_t *io)
{
    memset(s, 0, sizeof *s);
    s->io = *io;
    const cc1101_io_t drv_io = {io->xfer, io->now_ms, io->delay_ms, io->ctx};
    cc1101_bind(&s->drv, &drv_io);
}

bool subghz_svc_ready(const subghz_svc_t *s)
{
    return s->ready;
}

static void log_what(subghz_svc_t *s, subghz_log_t what, unsigned a, unsigned b)
{
    s->io.log(s->io.ctx, what, a, b);
}

/* The slot of a known module that declares a radio and both wires, or -1. */
static int find_slot(subghz_svc_t *s)
{
    stark_module_entry_t list[STARK_MODULE_ENTRIES];
    size_t n = s->io.list(s->io.ctx, list, STARK_MODULE_ENTRIES);
    if (n > STARK_MODULE_ENTRIES) {
        n = STARK_MODULE_ENTRIES;
    }
    for (size_t i = 0; i < n; i++) {
        if (list[i].kind == STARK_MODULE_SLOT && list[i].state == STARK_MODULE_KNOWN &&
            (list[i].declared & SUBGHZ_WIRING) == SUBGHZ_WIRING) {
            return list[i].slot;
        }
    }
    return -1;
}

static void backoff(subghz_svc_t *s, uint32_t ms)
{
    s->have_retry = true;
    s->retry_at = s->io.now_ms(s->io.ctx) + ms;
}

static void give_back(subghz_svc_t *s)
{
    s->io.close(s->io.ctx);
    s->io.release(s->io.ctx, &s->lease);
    s->claimed = false;
}

/* A rejection: try again later, and neither log a repeat of the last one nor try as often. */
static void note_failure(subghz_svc_t *s, subghz_log_t what, unsigned a, unsigned b)
{
    bool repeat = s->have_fail && s->fail_what == what && s->fail_a == a && s->fail_b == b;
    s->retry_ms = repeat ? s->retry_ms * 2u : SUBGHZ_RETRY_MS;
    if (s->retry_ms > SUBGHZ_RETRY_MAX_MS) {
        s->retry_ms = SUBGHZ_RETRY_MAX_MS;
    }
    backoff(s, s->retry_ms);
    if (!repeat) {
        s->have_fail = true;
        s->fail_what = what;
        s->fail_a = a;
        s->fail_b = b;
        log_what(s, what, a, b);
    }
}

/* The module was claimed but is not usable: let go of it and try again later. */
static void reject(subghz_svc_t *s, subghz_log_t what, unsigned a, unsigned b)
{
    give_back(s);
    note_failure(s, what, a, b);
}

/* Nothing is being measured and nothing is pending. */
static void reset_measure(subghz_svc_t *s)
{
    memset(&s->st, 0, sizeof s->st);
    s->rx_on = false;
    s->idle_log = false;
    s->retune = false;
    s->scan_active = false;
    s->have_logged = false;
    s->op_misses = 0;
    s->cap_setup = false;
    s->cap_active = false;
    s->cap_finish = false;
    s->cap_rx = false;
}

static void leave_ready(subghz_svc_t *s, subghz_log_t what, unsigned a, unsigned b, bool wait)
{
    if (s->cap_rx) {
        s->io.rx_stop(s->io.ctx);
    }
    if (s->rx_on) {
        (void)cc1101_idle(&s->drv); /* best effort: the chip may well be gone */
    }
    reset_measure(s);
    give_back(s);
    s->ready = false;
    s->misses = 0;
    s->have_fail = what == SUBGHZ_LOG_NOT_CC1101;
    s->fail_what = what;
    s->fail_a = a;
    s->fail_b = b;
    s->retry_ms = SUBGHZ_RETRY_MS;
    if (wait) {
        backoff(s, SUBGHZ_RETRY_MS);
    }
    log_what(s, what, a, b);
    s->io.changed(s->io.ctx, false);
}

/* A ready module is checked by its own answer every round. */
static void check_ready(subghz_svc_t *s)
{
    if (!s->io.lease_valid(s->io.ctx, &s->lease)) {
        leave_ready(s, SUBGHZ_LOG_LOST, 0, 0, false);
        return;
    }
    uint8_t part;
    uint8_t ver;
    stark_err_t err = cc1101_detect(&s->drv, &part, &ver);
    if (err == STARK_OK) {
        s->misses = 0;
    } else if (++s->misses >= SUBGHZ_MISS_LIMIT) {
        if (err == STARK_ERR_NOT_FOUND) {
            leave_ready(s, SUBGHZ_LOG_NOT_CC1101, part, ver, true);
        } else {
            leave_ready(s, SUBGHZ_LOG_LOST, 0, 0, true);
        }
    }
}

static void identify(subghz_svc_t *s, uint8_t slot)
{
    stark_err_t err = s->io.claim(s->io.ctx, slot, &s->lease);
    if (err != STARK_OK) {
        return; /* held by someone else, or the slot changed under us: try again next round */
    }
    s->claimed = true;
    s->none_logged = true; /* "no module" describes the start, not what happens later */
    err = s->io.open(s->io.ctx, &s->lease);
    if (err != STARK_OK) {
        s->io.release(s->io.ctx, &s->lease);
        s->claimed = false;
        note_failure(s, SUBGHZ_LOG_ERROR, (unsigned)err, 0);
        return;
    }
    stark_err_t reset_err = cc1101_reset(&s->drv);
    uint8_t part;
    uint8_t ver;
    err = cc1101_detect(&s->drv, &part, &ver); /* also when the reset failed: say what answered */
    if (err == STARK_ERR_NOT_FOUND) {
        reject(s, SUBGHZ_LOG_NOT_CC1101, part, ver);
        return;
    }
    if (err == STARK_OK) {
        err = reset_err;
    }
    if (err == STARK_OK) {
        err =
            cc1101_configure(&s->drv, CC1101_PRESET_OOK_RX, SUBGHZ_DEFAULT_HZ, SUBGHZ_DEFAULT_BAUD);
    }
    if (err != STARK_OK) {
        reject(s, SUBGHZ_LOG_ERROR, (unsigned)err, 0);
        return;
    }
    s->have_fail = false;
    s->misses = 0;
    s->ready = true;
    log_what(s, SUBGHZ_LOG_VERSION, ver, 0);
    s->io.changed(s->io.ctx, true);
}

/*
 * A failed measurement is tried again next round; the second in a row drops the module
 * (fail closed: it is identified from scratch, with a reset, when it answers again).
 */
static void op_failed(subghz_svc_t *s, stark_err_t err)
{
    if (++s->op_misses >= SUBGHZ_OP_MISS_LIMIT) {
        leave_ready(s, SUBGHZ_LOG_ERROR, (unsigned)err, 0, true);
    }
}

static stark_err_t probe_tune(void *ctx, uint32_t hz)
{
    return cc1101_tune_rx(&((subghz_svc_t *)ctx)->drv, hz);
}

static stark_err_t probe_read(void *ctx, uint8_t *raw)
{
    return cc1101_rssi_raw(&((subghz_svc_t *)ctx)->drv, raw);
}

static void probe_delay(void *ctx, uint32_t ms)
{
    subghz_svc_t *s = ctx;
    s->io.delay_ms(s->io.ctx, ms);
}

static void start_probe(subghz_svc_t *s)
{
    const subghz_probe_t probe = {probe_tune, probe_read, probe_delay, s};
    subghz_scan_start(&s->scan, &probe);
}

/* No capture is running or pending (the receiver itself is stopped by the next round). */
static void end_capture(subghz_svc_t *s)
{
    s->cap_setup = false;
    s->cap_active = false;
    s->cap_finish = false;
}

stark_err_t subghz_svc_meter(subghz_svc_t *s, uint32_t hz)
{
    uint8_t words[3];
    if (!s->ready) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = cc1101_freq_regs(hz, words);
    if (err != STARK_OK) {
        return err;
    }
    memset(&s->st, 0, sizeof s->st);
    s->st.mode = STARK_SUBGHZ_METER;
    s->st.hz = hz;
    s->meter_hz = hz;
    s->retune = true;
    s->scan_active = false;
    end_capture(s);
    s->rx_on = true; /* from here the chip may be receiving, until it is told otherwise */
    s->idle_log = false;
    s->have_logged = false;
    s->op_misses = 0;
    return STARK_OK;
}

stark_err_t subghz_svc_scan(subghz_svc_t *s)
{
    if (!s->ready) {
        return STARK_ERR_STATE;
    }
    memset(&s->st, 0, sizeof s->st);
    s->st.mode = STARK_SUBGHZ_SCAN;
    s->retune = false;
    s->scan_active = true;
    end_capture(s);
    s->rx_on = true;
    s->idle_log = false;
    s->op_misses = 0;
    start_probe(s);
    return STARK_OK;
}

stark_err_t subghz_svc_capture(subghz_svc_t *s, subghz_mod_t mod, uint32_t hz, subghz_sym_t *buf,
                               size_t cap)
{
    uint8_t words[3];
    if (!s->ready) {
        return STARK_ERR_STATE;
    }
    if ((unsigned)mod >= (unsigned)SUBGHZ_MOD_COUNT || buf == NULL || cap == 0 ||
        cc1101_freq_regs(hz, words) != STARK_OK) {
        return STARK_ERR_INVALID_ARG;
    }
    memset(&s->st, 0, sizeof s->st);
    s->st.mode = STARK_SUBGHZ_CAPTURE;
    s->st.hz = hz;
    s->retune = false;
    s->scan_active = false;
    s->cap_mod = mod;
    s->cap_hz = hz;
    s->cap_setup = true;
    s->cap_active = false;
    s->cap_finish = false;
    s->cap_noted = 0;
    subghz_cap_init(&s->cap, buf, cap);
    s->rx_on = true; /* from here the chip may be receiving, until it is told otherwise */
    s->idle_log = false;
    s->op_misses = 0;
    return STARK_OK;
}

void subghz_svc_capture_finish(subghz_svc_t *s)
{
    if (s->st.mode == STARK_SUBGHZ_CAPTURE && !s->st.capture_done) {
        s->cap_finish = true;
    }
}

void subghz_svc_stop(subghz_svc_t *s)
{
    if (s->st.mode != STARK_SUBGHZ_IDLE) {
        s->idle_log = true;
    }
    memset(&s->st, 0, sizeof s->st);
    s->retune = false;
    s->scan_active = false;
    end_capture(s);
}

void subghz_svc_status(const subghz_svc_t *s, stark_subghz_status_t *out)
{
    *out = s->st;
}

static void run_idle(subghz_svc_t *s)
{
    if (s->rx_on) {
        stark_err_t err = cc1101_idle(&s->drv);
        if (err != STARK_OK) {
            op_failed(s, err);
            return;
        }
        s->rx_on = false;
        s->op_misses = 0;
    }
    if (s->idle_log) {
        s->idle_log = false;
        log_what(s, SUBGHZ_LOG_IDLE, 0, 0);
    }
}

static uint32_t run_meter(subghz_svc_t *s)
{
    uint32_t now = s->io.now_ms(s->io.ctx);
    if (s->retune) {
        stark_err_t err = cc1101_tune_rx(&s->drv, s->meter_hz);
        if (err != STARK_OK) {
            op_failed(s, err);
            return 1;
        }
        s->retune = false;
        s->op_misses = 0;
        s->next_ms = now + SUBGHZ_SCAN_SETTLE_MS;
        log_what(s, SUBGHZ_LOG_METER, s->meter_hz, 0);
        return 1;
    }
    if ((int32_t)(now - s->next_ms) < 0) {
        return s->next_ms - now; /* woken early: 1..SUBGHZ_METER_PERIOD_MS left */
    }
    uint8_t raw;
    stark_err_t err = cc1101_rssi_raw(&s->drv, &raw);
    if (err != STARK_OK) {
        op_failed(s, err);
        return 1;
    }
    s->op_misses = 0;
    s->next_ms = now + SUBGHZ_METER_PERIOD_MS;
    int16_t level = subghz_rssi_dbm_x2(raw);
    bool changed = !s->st.valid || level != s->st.dbm_x2;
    s->st.valid = true;
    s->st.dbm_x2 = level;
    if (!s->have_logged || level - s->logged_x2 >= SUBGHZ_LOG_DELTA_X2 ||
        s->logged_x2 - level >= SUBGHZ_LOG_DELTA_X2) {
        s->have_logged = true;
        s->logged_x2 = level;
        log_what(s, SUBGHZ_LOG_RSSI, (unsigned)(int)level, 0);
    }
    if (changed) {
        s->io.notify(s->io.ctx, STARK_SUBGHZ_EVT_RSSI);
    }
    return SUBGHZ_METER_PERIOD_MS;
}

/* The sweep is complete: the radio goes back to IDLE, then the peak is announced. */
static void finish_scan(subghz_svc_t *s)
{
    stark_err_t err = cc1101_idle(&s->drv);
    if (err != STARK_OK) {
        op_failed(s, err);
        return;
    }
    s->rx_on = false;
    s->scan_active = false;
    s->op_misses = 0;
    s->st.scan_done = true;
    s->st.valid = true;
    s->st.percent = 100;
    s->st.hz = s->scan.peak_hz;
    s->st.dbm_x2 = s->scan.peak_dbm_x2;
    log_what(s, SUBGHZ_LOG_PEAK, s->scan.peak_hz, (unsigned)(int)s->scan.peak_dbm_x2);
    s->io.notify(s->io.ctx, STARK_SUBGHZ_EVT_SCAN_DONE);
}

static void run_scan(subghz_svc_t *s)
{
    if (s->scan.state == SUBGHZ_SCAN_RUNNING) {
        (void)subghz_scan_step(&s->scan);
    }
    switch (s->scan.state) {
        case SUBGHZ_SCAN_RUNNING:
            s->op_misses = 0;
            s->st.percent = subghz_scan_percent(&s->scan);
            break;
        case SUBGHZ_SCAN_FAILED:
            op_failed(s, s->scan.err);
            if (s->ready) {
                start_probe(s); /* the sweep starts again from the first channel */
                s->st.percent = 0;
            }
            break;
        case SUBGHZ_SCAN_DONE:
        case SUBGHZ_SCAN_IDLE:
        default:
            finish_scan(s);
            break;
    }
}

/* The profile of the chosen modulation, the receiver on GDO2, and the first round of symbols. */
static void start_capture(subghz_svc_t *s)
{
    if (s->cap_rx) {
        s->io.rx_stop(s->io.ctx); /* a capture started again while the last one ran */
        s->cap_rx = false;
    }
    cc1101_preset_t preset =
        s->cap_mod == SUBGHZ_MOD_2FSK ? CC1101_PRESET_FSK2_RX : CC1101_PRESET_OOK_RX;
    stark_err_t err = cc1101_configure(&s->drv, preset, s->cap_hz, SUBGHZ_DEFAULT_BAUD);
    if (err == STARK_OK) {
        err = cc1101_tune_rx(&s->drv, s->cap_hz);
    }
    if (err == STARK_OK) {
        err = s->io.rx_start(s->io.ctx);
    }
    if (err != STARK_OK) {
        op_failed(s, err);
        return;
    }
    s->cap_rx = true;
    s->cap_setup = false;
    s->cap_active = true;
    s->cap_note_ms = s->io.now_ms(s->io.ctx);
    s->op_misses = 0;
    log_what(s, SUBGHZ_LOG_CAPTURE_START, s->cap_hz, (unsigned)s->cap_mod);
}

/* The capture ended: the receiver stops, the radio goes to IDLE with the usual profile. */
static void finish_capture(subghz_svc_t *s)
{
    if (s->cap_rx) {
        s->io.rx_stop(s->io.ctx);
        s->cap_rx = false;
    }
    stark_err_t err = cc1101_idle(&s->drv);
    if (err == STARK_OK && s->cap_mod != SUBGHZ_MOD_OOK) {
        err = cc1101_configure(&s->drv, CC1101_PRESET_OOK_RX, SUBGHZ_DEFAULT_HZ,
                               SUBGHZ_DEFAULT_BAUD); /* the meter and the scan expect it */
    }
    if (err != STARK_OK) {
        op_failed(s, err); /* tried again next round; two misses in a row drop the module */
        return;
    }
    s->rx_on = false;
    s->cap_active = false;
    s->op_misses = 0;
    s->st.capture_done = true;
    log_what(s, SUBGHZ_LOG_CAPTURE_DONE, s->cap.n, subghz_raw_hash(s->cap.sym, s->cap.n));
    s->io.notify(s->io.ctx, STARK_SUBGHZ_EVT_CAPTURE_DONE);
}

static uint32_t run_capture(subghz_svc_t *s)
{
    if (s->cap_setup) {
        start_capture(s);
        return 1;
    }
    if (!s->cap_active) {
        return SUBGHZ_IDLE_MS; /* ended: the symbols are the caller's */
    }
    if (!s->cap_finish && !s->cap.full) {
        size_t n = s->io.rx_take(s->io.ctx, s->take, SUBGHZ_TAKE_MAX, SUBGHZ_CAPTURE_POLL_MS);
        subghz_cap_feed(&s->cap, s->take, n > SUBGHZ_TAKE_MAX ? SUBGHZ_TAKE_MAX : n);
        s->st.symbols = s->cap.n;
        s->st.span_us = s->cap.total_us;
    }
    uint32_t now = s->io.now_ms(s->io.ctx);
    if (s->st.symbols != s->cap_noted && now - s->cap_note_ms >= SUBGHZ_CAPTURE_NOTE_MS) {
        s->cap_noted = s->st.symbols;
        s->cap_note_ms = now;
        s->io.notify(s->io.ctx, STARK_SUBGHZ_EVT_CAPTURE);
    }
    if (s->cap_finish || s->cap.full) {
        finish_capture(s);
    }
    return 1; /* a round that took symbols already waited for them */
}

static uint32_t run_mode(subghz_svc_t *s)
{
    if (s->cap_rx && s->st.mode != STARK_SUBGHZ_CAPTURE) {
        s->io.rx_stop(
            s->io.ctx); /* another measurement, a stop or a lost module follows a capture */
        s->cap_rx = false;
    }
    switch (s->st.mode) {
        case STARK_SUBGHZ_CAPTURE:
            return run_capture(s);
        case STARK_SUBGHZ_METER:
            return run_meter(s);
        case STARK_SUBGHZ_SCAN:
            if (s->scan_active) {
                run_scan(s);
                return 1;
            }
            return SUBGHZ_IDLE_MS;
        case STARK_SUBGHZ_IDLE:
        default:
            run_idle(s);
            return SUBGHZ_IDLE_MS;
    }
}

uint32_t subghz_svc_step(subghz_svc_t *s)
{
    if (s->ready) {
        check_ready(s);
        return s->ready ? run_mode(s) : SUBGHZ_IDLE_MS;
    }
    int slot = find_slot(s);
    if (slot < 0) {
        s->have_fail = false;
        if (!s->none_logged) {
            s->none_logged = true;
            log_what(s, SUBGHZ_LOG_NO_MODULE, 0, 0);
        }
        return SUBGHZ_IDLE_MS;
    }
    if (s->have_retry && (int32_t)(s->io.now_ms(s->io.ctx) - s->retry_at) < 0) {
        return SUBGHZ_IDLE_MS;
    }
    s->have_retry = false;
    identify(s, (uint8_t)slot);
    return SUBGHZ_IDLE_MS;
}
