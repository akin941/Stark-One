/*
 * app_gpio.c — GPIO (TASKS.md STARK-0702, STARK-0703, ADR-0023): reads, drives, pulses, PWMs and
 * measures the pins the lab offers (stark_lab), and leaves every pin in hi-Z with no pull when it
 * stops. The home list shows the offered pins; OK opens one, and the pin's view has a Mode (Hi-Z,
 * Input, Output, PWM, Pulse), the settings that mode needs and an action row. Nothing is driven by
 * a setting: only an action does, and the first drive of a pin in a session asks a confirmation
 * first (the pin, its name, what the pad reads, "own hardware only"); OK arms the pin for the
 * session, BACK touches nothing. BACK from a pin releases it, BACK at home and leaving the app
 * release everything. A pulse waits on the app worker (up to 1 s) and so does a measurement of an
 * input's frequency and duty (up to 4.1 s, the Measure row), and BACK cuts either short; a
 * contention ends in hi-Z with an alert; a pin that stops being offered (a module in its slot) is
 * released by the service and the view returns to the list. The row logic is gpio_views.c (pure);
 * this file binds it to the service and the screen.
 */
#include "gpioapp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stark_app.h"
#include "stark_caps.h"
#include "stark_event.h"
#include "stark_input.h"
#include "stark_lab.h"
#include "stark_log.h"
#include "stark_module.h"
#include "ui_dialog.h"
#include "ui_menu.h"

#define NOTE_PULSE_DONE   300 /* STARK_APP_REQ_NOTIFY arg: the worker's pulse has ended */
#define NOTE_MEASURE_DONE 301 /* ... the worker's measurement has ended */

typedef enum { VIEW_HOME, VIEW_PIN } view_t;
typedef enum { ACT_NONE, ACT_DRIVE, ACT_TOGGLE, ACT_PWM, ACT_PULSE } act_t;
typedef enum { BUSY_NONE, BUSY_PULSE, BUSY_MEASURE } busy_t;

static view_t s_view;
static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);

/* home */
static stark_lab_pin_t s_pins[STARK_BOARD_LAB_PINS_MAX]; /* the offered pins, in table order */
static size_t s_pin_count;
static char s_home_label[STARK_BOARD_LAB_PINS_MAX][24];
static ui_menu_item_t s_home_items[STARK_BOARD_LAB_PINS_MAX];
static const ui_menu_item_t s_none_item = {.label = "No pins offered", .disabled = true};

/* the open pin */
static int s_pin;
static char s_name[16];
static char s_pad_label[24]; /* "<pin> <name>": the pad row says which pin this is */
static gpioapp_cfg_t s_cfg;
static gpioview_row_t s_kinds[GPIOVIEW_ROWS_MAX];
static size_t s_kind_count;
static ui_menu_item_t s_items[GPIOVIEW_ROWS_MAX];
static stark_lab_info_t s_info;
static bool s_level_read;
static char s_probe[12];
static char s_last_state[12];
static char s_meas[40]; /* the Measure row's text */
static act_t s_pending; /* the action the confirmation is for */

/* the pulse or the measurement on the worker: the UI task alone starts it, the worker alone
 * writes `err` (and `meas`) */
static struct {
    int pin;
    bool level;
    uint32_t us;
    stark_err_t err;
    stark_lab_meas_t meas;
} s_job;
static busy_t s_busy;

static void show(view_t view);
static bool gpio_event(stark_screen_t *self, const stark_event_t *e);

/* ---- the pin as the service sees it ---------------------------------------------------- */

/* Takes the pin's info (and the pad's level while it is an input); false once the pin is gone. */
static bool take_info(void)
{
    if (stark_lab_info(s_pin, &s_info) != STARK_OK) {
        return false;
    }
    if (s_info.mode == STARK_LAB_MODE_IN) {
        (void)stark_lab_read(s_pin, &s_level_read);
    }
    return true;
}

static void state_text(char *buf, size_t n)
{
    gpioview_state_text(&s_info, s_level_read, buf, n);
}

/* Logs `gpioapp: pin 38 IN H` when the pad's state differs from the last one logged. */
static void log_state(void)
{
    char now[12];
    state_text(now, sizeof now);
    if (strcmp(now, s_last_state) != 0) {
        (void)snprintf(s_last_state, sizeof s_last_state, "%s", now);
        STARK_LOGI("gpioapp", "pin %d %s", s_pin, now);
    }
}

/* The pin view's rows from the configuration and the latest info (the count follows the mode). */
static void fill_rows(void)
{
    s_kind_count = gpioview_rows(&s_cfg, s_kinds);
    for (size_t i = 0; i < s_kind_count; i++) {
        bool pad = s_kinds[i] == GPIOVIEW_ROW_PAD;
        s_items[i] = (ui_menu_item_t){
            .label =
                pad ? s_pad_label : gpioview_label(s_kinds[i], s_info.mode == STARK_LAB_MODE_PWM),
            .disabled = pad};
    }
}

static void withdrawn(void)
{
    STARK_LOGW("gpioapp", "pin %d withdrawn", s_pin);
    char msg[48];
    (void)snprintf(msg, sizeof msg, "Pin %d is not offered any more. It is released.", s_pin);
    show(VIEW_HOME);
    (void)stark_ui_dialog_alert("Pin withdrawn", msg, NULL, NULL);
}

/* Takes the info again and repaints; the rows are rebuilt in place (the selection stays). */
static void refresh_pin(void)
{
    if (!take_info()) {
        withdrawn();
        return;
    }
    fill_rows();
    log_state();
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

/* ---- actions ----------------------------------------------------------------------------- */

static void notify_done(int note)
{
    stark_event_t e = {.type = STARK_EVT_APP_REQUEST};
    e.app.id = STARK_APP_REQ_NOTIFY;
    e.app.arg = note;
    (void)stark_event_publish(&e);
}

static void pulse_worker(void *ctx)
{
    (void)ctx;
    s_job.err = stark_lab_pulse(s_job.pin, s_job.level, s_job.us);
    notify_done(NOTE_PULSE_DONE);
}

/*
 * NOTE: the 4 kB edge ring (ADR-0023 §4) is heap held only while the measurement runs: it is
 * allocated here, on the worker, and freed before the result is announced, so nothing stays
 * behind when the user leaves the view or the app.
 */
static void measure_worker(void *ctx)
{
    (void)ctx;
    uint32_t *ring = malloc(STARK_LAB_MEAS_RING * sizeof(uint32_t));
    if (ring == NULL) {
        s_job.err = STARK_ERR_NO_MEM;
    } else {
        s_job.err = stark_lab_measure(s_job.pin, ring, STARK_LAB_MEAS_RING, &s_job.meas);
        free(ring);
    }
    notify_done(NOTE_MEASURE_DONE);
}

/*
 * A drive failed. STARK_ERR_IO is what the service returns for a contention (the pad did not follow
 * the level, the service logged `lab: contention pin`) and for a transport fault alike: both end in
 * hi-Z, and the alert says what the user needs to know. Anything else is a log line.
 */
static void drive_failed(stark_err_t err)
{
    if (err == STARK_ERR_IO) {
        char msg[64];
        STARK_LOGW("gpioapp", "pin %d drive failed: released", s_pin);
        (void)snprintf(msg, sizeof msg, "Pin %d did not follow what it drove. Released.", s_pin);
        (void)stark_ui_dialog_alert("Contention", msg, NULL, NULL);
    } else {
        STARK_LOGW("gpioapp", "pin %d not driven: %s", s_pin, stark_err_str(err));
    }
}

static void perform(act_t act)
{
    stark_err_t err = STARK_OK;
    switch (act) {
        case ACT_DRIVE:
            err = stark_lab_write(s_pin, s_cfg.level);
            break;
        case ACT_TOGGLE: {
            bool level = s_info.mode == STARK_LAB_MODE_OUT ? !s_info.level : !s_cfg.level;
            err = s_info.mode == STARK_LAB_MODE_OUT ? stark_lab_toggle(s_pin)
                                                    : stark_lab_write(s_pin, level);
            s_cfg.level = err == STARK_OK ? level : s_cfg.level;
            break;
        }
        case ACT_PWM:
            err = stark_lab_pwm_start(s_pin, s_cfg.hz, s_cfg.duty_pct);
            break;
        case ACT_PULSE:
            s_job.pin = s_pin;
            s_job.level = s_cfg.level;
            s_job.us = s_cfg.width_us;
            s_busy = BUSY_PULSE;
            err = stark_app_worker_start(pulse_worker, NULL);
            if (err != STARK_OK) {
                s_busy = BUSY_NONE;
                STARK_LOGW("gpioapp", "pin %d pulse refused: %s", s_pin, stark_err_str(err));
            }
            return; /* the result arrives as NOTE_PULSE_DONE */
        case ACT_NONE:
        default:
            return;
    }
    if (err != STARK_OK) {
        drive_failed(err);
    }
    refresh_pin();
}

/* The confirmation was answered: OK arms the pin and goes on, BACK leaves the pad released. */
static void confirmed(stark_dialog_result_t result, void *ctx)
{
    (void)ctx;
    act_t act = s_pending;
    s_pending = ACT_NONE;
    if (result != STARK_DIALOG_OK) {
        STARK_LOGI("gpioapp", "cancelled pin %d", s_pin);
        refresh_pin();
        return;
    }
    if (stark_lab_arm(s_pin) != STARK_OK) {
        refresh_pin();
        return;
    }
    STARK_LOGI("gpioapp", "armed pin %d", s_pin);
    perform(act);
}

/* The first drive of a pin in a session: show what the pad is doing and ask. */
static void request(act_t act)
{
    if (s_info.armed) {
        perform(act);
        return;
    }
    stark_lab_pinstate_t st;
    stark_err_t err = stark_lab_state(s_pin, &st);
    if (err != STARK_OK) {
        STARK_LOGW("gpioapp", "pin %d not read: %s", s_pin, stark_err_str(err));
        refresh_pin();
        return;
    }
    const char *reads = st == STARK_LAB_PIN_FLOATING ? "floating"
                        : st == STARK_LAB_PIN_HIGH   ? "high"
                                                     : "low";
    (void)stark_lab_hiz(
        s_pin); /* the probe left it an input: back to hi-Z until a drive is confirmed */
    char title[24];
    char msg[128];
    (void)snprintf(title, sizeof title, "Drive pin %d?", s_pin);
    (void)snprintf(msg, sizeof msg,
                   "%s reads: %s. Own hardware only. Connected hardware may be damaged.", s_name,
                   reads);
    STARK_LOGI("gpioapp", "confirm pin %d reads %s", s_pin, reads);
    s_pending = act;
    if (stark_ui_dialog_confirm(title, msg, confirmed, NULL) != STARK_OK) {
        s_pending = ACT_NONE;
    }
    refresh_pin();
}

static void probe(void)
{
    stark_lab_pinstate_t st;
    stark_err_t err = stark_lab_state(s_pin, &st);
    if (err == STARK_OK) {
        const char *text = st == STARK_LAB_PIN_FLOATING ? "floating"
                           : st == STARK_LAB_PIN_HIGH   ? "high"
                                                        : "low";
        (void)snprintf(s_probe, sizeof s_probe, "%s", text);
        STARK_LOGI("gpioapp", "pin %d probe %s", s_pin, text);
        (void)stark_lab_hiz(s_pin);
    } else {
        s_probe[0] = '\0';
        STARK_LOGW("gpioapp", "pin %d not read: %s", s_pin, stark_err_str(err));
    }
    refresh_pin();
}

/* Measure: the worker takes the 20 ms gate and the window (up to 4.1 s); BACK cuts it short. */
static void measure(void)
{
    s_job.pin = s_pin;
    s_busy = BUSY_MEASURE;
    stark_err_t err = stark_app_worker_start(measure_worker, NULL);
    if (err != STARK_OK) {
        s_busy = BUSY_NONE;
        STARK_LOGW("gpioapp", "pin %d measure refused: %s", s_pin, stark_err_str(err));
        return;
    }
    (void)snprintf(s_meas, sizeof s_meas, "measuring...");
    STARK_LOGI("gpioapp", "pin %d measuring", s_pin);
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

/* The worker's measurement ended: the result is the row's text, or why there is none. */
static void measured(void)
{
    if (s_job.err == STARK_OK) {
        stark_lab_meas_text(&s_job.meas, s_meas, sizeof s_meas);
        STARK_LOGI("gpioapp", "pin %d measure %s", s_pin, s_meas);
    } else {
        s_meas[0] = '\0';
        STARK_LOGW("gpioapp", "pin %d not measured: %s", s_pin, stark_err_str(s_job.err));
    }
    refresh_pin();
}

/* A mode change: the pad is released first (which stops a PWM), then the mode's own rest state. */
static void apply_mode(void)
{
    s_probe[0] = '\0';
    s_meas[0] = '\0';
    (void)stark_lab_hiz(s_pin);
    if (s_cfg.mode == GPIOAPP_MODE_IN) {
        (void)stark_lab_input(s_pin, s_cfg.pull);
    }
}

/* ---- screens ----------------------------------------------------------------------------- */

static void open_pin(size_t index)
{
    const stark_lab_pin_t *p = &s_pins[index];
    if (stark_lab_acquire(p->pin, "gpio") != STARK_OK) {
        STARK_LOGW("gpioapp", "pin %d not available", p->pin);
        show(VIEW_HOME);
        return;
    }
    s_pin = p->pin;
    (void)snprintf(s_name, sizeof s_name, "%s", p->name);
    gpioview_home_label(s_pin, s_name, s_pad_label, sizeof s_pad_label);
    gpioview_cfg_reset(&s_cfg);
    s_probe[0] = '\0';
    s_meas[0] = '\0';
    s_last_state[0] = '\0';
    s_level_read = false;
    s_pending = ACT_NONE;
    STARK_LOGI("gpioapp", "open pin %d %s", s_pin, s_name);
    (void)take_info();
    fill_rows();
    show(VIEW_PIN);
    log_state();
}

static void close_pin(void)
{
    if (s_busy == BUSY_PULSE) {
        stark_lab_pulse_cancel();
    } else if (s_busy == BUSY_MEASURE) {
        stark_lab_measure_cancel();
    }
    stark_lab_release(s_pin);
    STARK_LOGI("gpioapp", "close pin %d", s_pin);
    show(VIEW_HOME);
}

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (s_view == VIEW_HOME) {
        if (s_pin_count > 0 && index < s_pin_count) {
            open_pin(index);
        }
        return;
    }
    if (index >= s_kind_count || s_busy != BUSY_NONE) {
        return;
    }
    switch (s_kinds[index]) {
        case GPIOVIEW_ROW_PROBE:
            probe();
            break;
        case GPIOVIEW_ROW_READ:
            refresh_pin();
            STARK_LOGI("gpioapp", "pin %d read %c", s_pin, s_level_read ? 'H' : 'L');
            break;
        case GPIOVIEW_ROW_MEASURE:
            measure();
            break;
        case GPIOVIEW_ROW_DRIVE:
            request(ACT_DRIVE);
            break;
        case GPIOVIEW_ROW_TOGGLE:
            request(ACT_TOGGLE);
            break;
        case GPIOVIEW_ROW_PWM:
            if (s_info.mode == STARK_LAB_MODE_PWM) {
                stark_lab_pwm_stop(); /* stopping needs no confirmation */
                refresh_pin();
            } else {
                request(ACT_PWM);
            }
            break;
        case GPIOVIEW_ROW_PULSE:
            request(ACT_PULSE);
            break;
        case GPIOVIEW_ROW_MODE:
        case GPIOVIEW_ROW_PULL:
        case GPIOVIEW_ROW_LEVEL:
        case GPIOVIEW_ROW_FREQ:
        case GPIOVIEW_ROW_DUTY:
        case GPIOVIEW_ROW_WIDTH:
        case GPIOVIEW_ROW_PAD:
        case GPIOVIEW_ROW_COUNT:
        default:
            break;
    }
}

/* LEFT/RIGHT on a setting row. */
static bool adjust(size_t index, int dir, void *ctx)
{
    (void)ctx;
    if (s_view != VIEW_PIN || index >= s_kind_count || s_busy != BUSY_NONE) {
        return false;
    }
    gpioview_row_t kind = s_kinds[index];
    if (!gpioview_adjust(&s_cfg, kind, dir)) {
        return false;
    }
    if (kind == GPIOVIEW_ROW_MODE) {
        apply_mode();
        (void)take_info();
        fill_rows();
        show(VIEW_PIN); /* the rows of another mode; the selection is on Mode again */
        log_state();
        return true;
    }
    if (kind == GPIOVIEW_ROW_PULL) {
        static const char *const k_pull[] = {"none", "up", "down"};
        (void)stark_lab_input(s_pin, s_cfg.pull);
        s_meas[0] = '\0'; /* a result belongs to the pull it was made with */
        STARK_LOGI("gpioapp", "pin %d pull %s", s_pin, k_pull[s_cfg.pull]);
    } else if ((kind == GPIOVIEW_ROW_FREQ || kind == GPIOVIEW_ROW_DUTY) &&
               s_info.mode == STARK_LAB_MODE_PWM) {
        stark_err_t err = stark_lab_pwm_start(s_pin, s_cfg.hz, s_cfg.duty_pct); /* armed already */
        if (err != STARK_OK) {
            drive_failed(err);
        }
    }
    refresh_pin();
    return true;
}

static const char *value_of(size_t index, void *ctx)
{
    static char buf[24];
    (void)ctx;
    if (s_view == VIEW_HOME) {
        if (index >= s_pin_count) {
            return NULL;
        }
        stark_lab_info_t info;
        if (stark_lab_info(s_pins[index].pin, &info) != STARK_OK) {
            return "Hi-Z"; /* the app holds no pin at home */
        }
        gpioview_state_text(&info, false, buf, sizeof buf);
        return buf;
    }
    if (index >= s_kind_count) {
        return NULL;
    }
    const gpioview_t v = {.cfg = &s_cfg,
                          .info = &s_info,
                          .level_read = s_level_read,
                          .probe = s_probe,
                          .meas = s_meas};
    return gpioview_value(s_kinds[index], &v, buf, sizeof buf);
}

/* The home list: the pins offered now; logged as `gpioapp: home 38 39`. */
static void fill_home(void)
{
    stark_lab_pin_t all[STARK_BOARD_LAB_PINS_MAX];
    char text[8 * STARK_BOARD_LAB_PINS_MAX + 1] = "";
    size_t used = 0;
    size_t n = stark_lab_pins(all, STARK_BOARD_LAB_PINS_MAX);
    s_pin_count = 0;
    for (size_t i = 0; i < n && i < STARK_BOARD_LAB_PINS_MAX; i++) {
        if (all[i].offered) {
            gpioview_home_label(all[i].pin, all[i].name, s_home_label[s_pin_count],
                                sizeof s_home_label[0]);
            s_home_items[s_pin_count] = (ui_menu_item_t){.label = s_home_label[s_pin_count]};
            s_pins[s_pin_count++] = all[i];
            used += (size_t)snprintf(text + used, sizeof text - used, " %d", all[i].pin);
        }
    }
    STARK_LOGI("gpioapp", "home%s", text);
}

/*
 * Re-initialises the one menu for a view (the selection starts at the top).
 * NOTE: the app manager hooks the running screen's on_exit to stop the app (app_manager.c);
 * ui_menu_init() rewrites the whole screen, so the hook is carried over, and the first call
 * installs gpio_exit() as the screen's own, which the manager keeps and calls first on a pop.
 */
static void gpio_exit(stark_screen_t *self);

static void show(view_t view)
{
    void (*on_exit)(stark_screen_t *) =
        s_menu.screen.on_exit != NULL ? s_menu.screen.on_exit : gpio_exit;
    s_view = view;
    if (view == VIEW_HOME) {
        fill_home();
        if (s_pin_count == 0) {
            ui_menu_init(&s_menu, "GPIO", &s_none_item, 1, activate, NULL);
        } else {
            ui_menu_init(&s_menu, "GPIO", s_home_items, s_pin_count, activate, NULL);
        }
        s_menu.adjust = NULL;
    } else {
        /* NOTE: the title stays "GPIO": the status bar repaints it only when a screen is pushed. */
        ui_menu_init(&s_menu, "GPIO", s_items, s_kind_count, activate, NULL);
        s_menu.adjust = adjust;
    }
    s_menu.value = value_of;
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = gpio_event;
    s_menu.screen.on_exit = on_exit;
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

/* Before the manager joins the worker: a pulse or a measurement that is waiting ends now. */
static void gpio_exit(stark_screen_t *self)
{
    (void)self;
    stark_lab_pulse_cancel();
    stark_lab_measure_cancel();
}

static bool gpio_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type == STARK_EVT_APP_REQUEST && e->app.id == STARK_APP_REQ_NOTIFY &&
        e->app.arg == NOTE_PULSE_DONE) {
        s_busy = BUSY_NONE;
        if (s_view == VIEW_PIN && s_job.pin == s_pin) {
            if (s_job.err != STARK_OK) {
                drive_failed(s_job.err);
            }
            refresh_pin();
        }
        return true;
    }
    if (e->type == STARK_EVT_APP_REQUEST && e->app.id == STARK_APP_REQ_NOTIFY &&
        e->app.arg == NOTE_MEASURE_DONE) {
        s_busy = BUSY_NONE;
        if (s_view == VIEW_PIN && s_job.pin == s_pin) {
            measured();
        }
        return true;
    }
    if (e->type == STARK_EVT_SYSTEM &&
        ((e->system.id == STARK_SYS_MODULE) ||
         (e->system.id == STARK_SYS_LAB && e->system.arg == STARK_LAB_EVT_PINS))) {
        if (e->system.id == STARK_SYS_MODULE) {
            stark_lab_refresh(); /* idempotent: the root has usually done it already */
        }
        if (s_view == VIEW_PIN) {
            if (!take_info()) {
                withdrawn();
            }
        } else {
            show(VIEW_HOME);
        }
        return s_menu_event(self, e);
    }
    if (e->type == STARK_EVT_KEY && s_busy != BUSY_NONE) {
        if (e->key.key == STARK_KEY_BACK && e->key.action == STARK_KEY_SHORT) {
            close_pin();
        }
        return true; /* a key can neither repeat nor stack a pulse or a measurement */
    }
    if (s_view == VIEW_PIN && e->type == STARK_EVT_KEY && e->key.key == STARK_KEY_BACK &&
        e->key.action == STARK_KEY_SHORT) {
        close_pin(); /* BACK leaves the pin, not the app */
        return true;
    }
    return s_menu_event(self, e);
}

static stark_err_t gpio_start(void **state)
{
    (void)state;
    s_view = VIEW_HOME;
    s_busy = BUSY_NONE;
    s_pending = ACT_NONE;
    s_pin_count = 0;
    return STARK_OK;
}

static void gpio_stop(void *state)
{
    (void)state;
    s_busy = BUSY_NONE;
    stark_lab_release_all(); /* every pin hi-Z with no pull; the worker is joined already */
}

static stark_screen_t *gpio_screen(void *state)
{
    (void)state;
    show(VIEW_HOME);
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): a chip with pins. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x14, 0x28, 0x14, 0x28, 0x0F, 0xF0, 0x4F, 0xF2, 0x0F, 0xF0, 0x4F, 0xF2, 0x0F, 0xF0,
    0x4F, 0xF2, 0x0F, 0xF0, 0x4F, 0xF2, 0x0F, 0xF0, 0x14, 0x28, 0x14, 0x28, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_gpio = {
    .id = "gpio",
    .title = "GPIO",
    .category = "Lab",
    .on_start = gpio_start,
    .on_stop = gpio_stop,
    .screen = gpio_screen,
    .icon = k_icon,
    .caps_required = 0,
};
