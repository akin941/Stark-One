# ARCHITECTURE — STARK ONE firmware

Target: ESP-IDF v6.1, C11, FreeRTOS, ESP32-S3.
This document is normative for the implementation agent. Where it sketches a header,
the sketch is the contract; naming and signatures may be refined but not re-invented.

---

## 1. Principles

1. **Layered, one-directional dependencies.** A layer may depend only on layers below
   it. Violations are build failures, not code-review comments (§10).
2. **Pure core, thin port.** Every non-trivial piece of logic is split into a
   *core* (pure C, no ESP-IDF headers, host-testable) and a *port* (ESP-IDF calls,
   thin enough not to need tests). Host tests target cores.
3. **No simulator conditionals.** Wokwi and real hardware run the same code path.
   Differences are expressed as *board variants* (pin maps, feature flags), never as
   `#ifdef WOKWI` (ADR-0010).
4. **Extensible, not speculative.** Extension points are defined now; implementations
   arrive when a milestone needs them. Empty abstractions are deleted, not kept.
5. **One owner per resource.** Each bus, task and buffer has exactly one owning
   component. Shared access goes through that owner's API.
6. **Errors propagate, they do not print.** Components return `stark_err_t`; only the
   top layer decides what the user sees.

---

## 2. Layer model

```
┌────────────────────────────────────────────────────────────────────┐
│ L5  apps/            gpio · i2c · uart · ir · nfc · subghz · diag  │
├────────────────────────────────────────────────────────────────────┤
│ L4  stark_app  (app manager, registry, lifecycle)                  │
│     stark_ui   (screen stack, list menu, widgets, navigation)      │
├────────────────────────────────────────────────────────────────────┤
│ L3  services:  stark_display · stark_input · stark_buzzer          │
│                stark_storage · stark_settings · stark_module       │
│                stark_power · stark_diag                            │
├────────────────────────────────────────────────────────────────────┤
│ L2  cores (pure C, host-tested):                                   │
│     stark_event · stark_gfx · stark_input_core · stark_ui_model    │
├────────────────────────────────────────────────────────────────────┤
│ L1  stark_hal   (gpio/spi/i2c/pwm/time/task port)                  │
│     stark_log · stark_err                                          │
├────────────────────────────────────────────────────────────────────┤
│ L0  stark_board (pin map, clocks, board_init, capability flags)    │
├────────────────────────────────────────────────────────────────────┤
│     ESP-IDF v6.1 / FreeRTOS / ESP32-S3                             │
└────────────────────────────────────────────────────────────────────┘
```

Rule: L(n) may include headers from L(n-1) … L0 only. L2 cores may include **nothing**
from L1 except `stark_err.h` (header-only, freestanding) — they receive their
dependencies by injection.

`stark_err.h` has no dependencies of its own (not even ESP-IDF) and is available to
**every** layer, including L0 — it is the one universal exception to the rule above,
not an L1-specific one. This is why `stark_board`'s contract below returns
`stark_err_t` despite being listed under L0. `stark_log` is not exempted the same way:
it depends on `esp_log`, and L0 components must not depend on it — `stark_board`
returns `stark_err_t` and logs nothing itself; whatever calls it (currently `main.c`)
decides what to log.

---

## 3. Repository layout

```
stark-one/
├── CMakeLists.txt                  # ESP-IDF project root
├── sdkconfig.defaults              # shared, committed
├── sdkconfig.defaults.esp32s3      # target-specific, committed
├── partitions.csv
├── dependencies.lock               # committed (ADR-0003)
├── diagram.json                    # Wokwi V0 circuit (repo root, ADR-0010)
├── wokwi.toml
├── main/
│   ├── CMakeLists.txt
│   └── stark_main.c                # app_main: board → services → UI task
├── components/
│   ├── stark_err/                  # L1  error codes, header-only
│   ├── stark_log/                  # L1  logging facade
│   ├── stark_board/                # L0  pin map + board_init + caps
│   ├── stark_hal/                  # L1  gpio/spi/pwm/time port
│   ├── stark_event/                # L2  event bus core + port
│   ├── stark_gfx/                  # L2  surfaces, primitives, text
│   ├── stark_input/                # L2 core + L3 service
│   ├── stark_display/              # L3  ILI9341 panel + flush pipeline
│   ├── stark_buzzer/               # L3  LEDC tone sequencer
│   ├── stark_ui/                   # L4  screen stack, list menu
│   └── stark_app/                  # L4  app registry + manager
├── apps/                           # L5  one ESP-IDF component per app
│   └── app_about/  app_inputtest/  app_displaytest/ …
├── test/
│   ├── host/                       # standalone CMake + Unity host test project
│   └── scenarios/                  # Wokwi CI automation YAML
├── scripts/                        # build.sh test_host.sh test_wokwi.sh fmt.sh …
├── tools/                          # font converter, asset packer (host Python)
└── docs/
```

Every `components/*` and `apps/*` directory is a standard ESP-IDF component with its
own `CMakeLists.txt` declaring `REQUIRES`/`PRIV_REQUIRES` explicitly. `REQUIRES` is the
enforcement mechanism for §2: a component must not list a higher layer.

---

## 4. Naming & style contract

| Item | Convention | Example |
| --- | --- | --- |
| Component | `stark_<domain>` | `stark_input` |
| Public header | `components/X/include/<component>.h` | `stark_input.h` |
| Public symbol | `<component>_<verb>` | `stark_input_start()` |
| Type | `<component>_<noun>_t` | `stark_input_config_t` |
| Enum value | `STARK_<COMPONENT>_<NAME>` | `STARK_KEY_OK` |
| Private symbol | `static`, file-local, no prefix needed | |
| Core (pure) file | `<component>_core.c/.h` | `stark_input_core.c` |
| Port (IDF) file | `<component>_port.c` | `stark_input_port.c` |

C11, 4-space indent, 100-column soft limit, `snake_case`, braces always. Enforced by
`.clang-format`. No dynamic allocation after init in the render/input path (§8).

---

## 5. Error handling

```c
/* stark_err.h — L1, header-only, no dependencies */
typedef enum {
    STARK_OK = 0,
    STARK_ERR_INVALID_ARG,
    STARK_ERR_NO_MEM,
    STARK_ERR_TIMEOUT,
    STARK_ERR_NOT_FOUND,
    STARK_ERR_NOT_SUPPORTED,
    STARK_ERR_BUSY,
    STARK_ERR_IO,
    STARK_ERR_STATE,
} stark_err_t;

const char *stark_err_str(stark_err_t err);
```

* Every fallible function returns `stark_err_t`; outputs go through pointer arguments.
* `esp_err_t` never crosses a public STARK header. Ports translate at the boundary
  (`stark_err_from_esp()` lives in `stark_hal`).
* `STARK_CHECK(cond, err)` / `STARK_CHECK_RET(expr)` macros in `stark_err.h`; they log
  through `stark_log` at the *port* layer only.
* Boot-critical failures (display init, board init) call `stark_panic()` which logs,
  emits a buzzer error pattern if available, and reboots after 5 s. It lives in
  `stark_log` (L1) as `stark_panic(const char *what, stark_err_t err)`, logging
  `panic: <what> failed: <error>` (STARK-0015); the buzzer pattern needs a hook once
  `stark_buzzer` exists.

---

## 6. Component contracts

### 6.1 `stark_board` (L0)

Sole source of truth for hardware wiring. Selected by Kconfig:
`STARK_BOARD_DEVKITC1` (default, also used by Wokwi) | `STARK_BOARD_PCB_R1` (later).

```c
typedef struct {
    int      sclk, mosi, miso;          /* shared SPI2 bus */
    int      tft_cs, tft_dc, tft_rst, tft_bl;
    int      key[STARK_KEY_COUNT];      /* UP DOWN LEFT RIGHT OK BACK */
    int      buzzer, led_status;
    int      sd_cs;                     /* -1 when absent */
    int      i2c_sda, i2c_scl;          /* -1 when absent */
    uint32_t tft_spi_hz;
} stark_board_pins_t;

const stark_board_pins_t *stark_board_pins(void);
const char               *stark_board_name(void);
stark_err_t               stark_board_init(void);   /* clocks, bus, common GPIO */
stark_err_t               stark_board_heartbeat_start(void); /* status LED, 1 Hz 10 % */
```

A board *capability* structure (`has_sdcard`, `has_ir`, `has_nfc`, …) is deliberately
**absent in V0**: nothing would read it. It is introduced by the first milestone that
needs it — V0.2 for the SD card — and generalised into module capability bits at V0.5
(ADR-0009).

A pin value of `-1` means "not present on this board"; services must handle it by
degrading, not by failing the boot.

### 6.2 `stark_hal` (L1)

A deliberately *small* port layer — it exists so L2 cores and host tests can be built
without ESP-IDF, not to abstract all of ESP-IDF.

```c
/* time */
uint64_t stark_hal_now_us(void);
void     stark_hal_delay_ms(uint32_t ms);

/* gpio */
stark_err_t stark_hal_gpio_config_input(int pin, bool pullup);
stark_err_t stark_hal_gpio_config_output(int pin, bool initial);
bool        stark_hal_gpio_read(int pin);
void        stark_hal_gpio_write(int pin, bool level);

/* pwm (LEDC) */
stark_err_t stark_hal_pwm_init(int pin, uint32_t hz, uint8_t channel);
stark_err_t stark_hal_pwm_set_freq(uint8_t channel, uint32_t hz);
stark_err_t stark_hal_pwm_set_duty_pct(uint8_t channel, uint8_t pct);

/* spi bus ownership: stark_board initialises SPI2; devices attach via esp_lcd /
   spi_bus_add_device in their own port files. stark_hal does not wrap SPI. */
```

Host builds link `stark_hal_host.c`: a fake clock the tests advance explicitly, and a
GPIO array the tests read/write. Determinism is the point — host tests never sleep.

### 6.3 `stark_event` (L2 core + L3 port)

The single nervous system of the firmware. One bus, one consumer loop, many producers.

```c
typedef enum {
    STARK_EVT_NONE = 0,
    STARK_EVT_KEY,             /* payload: key */
    STARK_EVT_TICK,            /* periodic, from UI loop */
    STARK_EVT_APP_REQUEST,     /* launch/exit requests */
    STARK_EVT_SYSTEM,          /* low battery, sd inserted, module attached … */
} stark_evt_type_t;

typedef struct {
    stark_evt_type_t type;
    uint64_t         ts_us;
    union {
        struct { uint8_t key; uint8_t action; uint8_t repeat; } key;
        struct { uint32_t id; int32_t arg; }                    app;
        struct { uint32_t id; int32_t arg; }                    system;
    };
} stark_event_t;
```

Core (`stark_event_core.c`, pure): a fixed-capacity SPSC/MPSC ring buffer plus a
subscriber table (`N ≤ 8` handlers, each with a type mask). No allocation, no locking
primitives — locking is injected:

```c
typedef struct { void (*lock)(void *); void (*unlock)(void *); void *ctx; } stark_lock_t;

stark_err_t stark_event_core_init(stark_event_core_t *bus, stark_event_t *storage,
                                  size_t capacity, stark_lock_t lock);
stark_err_t stark_event_core_publish(stark_event_core_t *bus, const stark_event_t *e);
size_t      stark_event_core_dispatch(stark_event_core_t *bus, uint32_t max_events);
stark_err_t stark_event_core_subscribe(stark_event_core_t *bus, uint32_t type_mask,
                                       stark_event_handler_t fn, void *ctx);

typedef void (*stark_event_handler_t)(const stark_event_t *e, void *ctx);
#define STARK_EVT_MASK(type) (UINT32_C(1) << (uint32_t)(type))  /* one bit per type */
typedef struct { uint32_t published, dropped, max_depth; } stark_event_stats_t;
```

Core semantics (STARK-0009; `stark_event_core.h` documents each call in full):

* The lock is never held while a handler runs, so a handler may publish or subscribe
  under a non-recursive mutex.
* `dispatch()` delivers only events queued before it started — a handler's publishes
  wait for the next call, even if they overflow the ring — and returns the number of
  events consumed (an event nobody subscribes to still counts). A nested `dispatch()`
  on the same bus returns 0. A subscriber added during `dispatch()` receives events
  from the next one dequeued. There is no unsubscribe: no V0 consumer needs one.
* `publish()` rejects `STARK_EVT_NONE` and unknown types with `STARK_ERR_INVALID_ARG`;
  a full ring still accepts the new event (`STARK_OK`) and drops the oldest.
* `stark_event_core_t` is caller-allocated; its `stats` field is the only one the owner
  may read (under its lock). `ts_us` is never touched by the core.

Port (`stark_event.c`): one global bus, a FreeRTOS mutex as the injected lock, a
binary semaphore so the UI task can block until something is published, and an ISR-safe
`stark_event_publish_from_isr()` added **only when a producer needs it** (not in V0).
STARK-0010 implements it with static storage and static FreeRTOS objects (no allocation
at all); the core's ring is the only event storage:

```c
stark_err_t stark_event_init(void);                       /* once, from app_main() */
stark_err_t stark_event_publish(const stark_event_t *e);  /* stamps ts_us if 0 */
stark_err_t stark_event_subscribe(uint32_t type_mask, stark_event_handler_t fn, void *ctx);
size_t      stark_event_dispatch(uint32_t max_events);
stark_err_t stark_event_wait(uint32_t timeout_ms);        /* STARK_OK | STARK_ERR_TIMEOUT */
stark_err_t stark_event_stats(stark_event_stats_t *out);  /* snapshot under the mutex */
```

The semaphore only wakes; the ring decides. `wait()` returns `STARK_OK` exactly when the
bus holds an undispatched event (checked before every take, so neither a lost wakeup nor
a stale token after a drained burst is possible) and `STARK_ERR_TIMEOUT` otherwise.
Binary rather than counting because a burst needs one wakeup, not one per event. Any
number of producer tasks; exactly one consumer task (the UI task) calls `wait()` and
`dispatch()`. Task context only — `esp_timer` callbacks qualify only with the default
`ESP_TIMER_TASK` dispatch. Every call returns `STARK_ERR_STATE` before `init()`.

Overflow policy: the ring drops the **oldest** event and increments
`stark_event_stats_t.dropped`, which diagnostics surfaces. Dropping input silently is
a bug we must be able to see.

### 6.4 `stark_gfx` (L2, pure)

No hardware knowledge whatsoever. Draws into a caller-provided RGB565 buffer.

```c
typedef struct { int16_t x, y, w, h; } gfx_rect_t;

typedef struct {
    uint16_t  *pixels;      /* RGB565, native-endian — the panel swap is stark_display's */
    int16_t    w, h;        /* buffer dimensions */
    int16_t    origin_x, origin_y;  /* where this buffer maps on the logical screen */
    gfx_rect_t clip;
} gfx_surface_t;

void gfx_surface_init(gfx_surface_t *s, uint16_t *pixels, int16_t w, int16_t h,
                      int16_t origin_x, int16_t origin_y);   /* clip = its own area */
void gfx_fill(gfx_surface_t *s, gfx_rect_t r, uint16_t colour);
void gfx_rect(gfx_surface_t *s, gfx_rect_t r, uint16_t colour);
void gfx_hline(gfx_surface_t *s, int16_t x, int16_t y, int16_t w, uint16_t colour);
void gfx_vline(gfx_surface_t *s, int16_t x, int16_t y, int16_t h, uint16_t colour);
void gfx_blit_1bpp(gfx_surface_t *s, int16_t x, int16_t y, const uint8_t *bits,
                   int16_t w, int16_t h, uint16_t fg, uint16_t bg, bool transparent);

int16_t gfx_text(gfx_surface_t *s, const gfx_font_t *f, int16_t x, int16_t y,
                 const char *utf8, uint16_t fg, uint16_t bg, bool transparent);
int16_t gfx_text_width(const gfx_font_t *f, const char *utf8);
```

Refinements fixed by STARK-0013: `clip` is in logical coordinates and drawing is limited to
`clip ∩ the buffer's logical area`, computed by one shared helper (`gfx_clip()`,
component-private) in 32-bit arithmetic; `w`/`h` ≤ 0 draws nothing; `gfx_rect` is a 1 px
outline; `gfx_blit_1bpp` bits are row-major, rows padded to a byte, MSB = leftmost pixel.

`origin_x/origin_y` is what makes band rendering transparent to callers: a screen draws
in *logical screen coordinates* and the surface translates. Drawing entirely outside the
band is a cheap clip-reject, not a special case in UI code.

Fonts: 1bpp bitmap fonts generated offline by `tools/fontconv.py` into `.c` arrays
(STARK-0014: `gfx_font.h` holds `gfx_font_t`, `gfx_text()` and `gfx_text_width()`;
`gfx_text()` returns the string's width; each malformed UTF-8 byte draws the fallback;
`gfx_font_mono16` is public-domain X.Org misc-fixed 8x13 in 8x16 cells —
`components/stark_gfx/fonts/README.md`).
V0 ships one 8×16 ASCII font (`gfx_font_mono16`) plus optionally a 6×10. Latin-1 /
Turkish glyph coverage is a V0.1 follow-up; `gfx_text` decodes UTF-8 and substitutes
`?` for unmapped code points from the start so the API never changes.

Colour: `GFX_RGB565(r,g,b)` macro. The panel byte order swap belongs to
`stark_display`, not here — `stark_gfx` always stores native-endian RGB565.

### 6.5 `stark_display` (L3)

Owns the panel, the band buffers and the flush pipeline.

```c
stark_err_t stark_display_init(void);
int16_t     stark_display_width(void);
int16_t     stark_display_height(void);

/* Render callback contract: called once per band, with a surface whose origin is set
   to the band's top-left. The callee draws in logical screen coordinates. */
typedef void (*stark_render_fn)(gfx_surface_t *surface, void *ctx);

stark_err_t stark_display_render(gfx_rect_t area, stark_render_fn fn, void *ctx);
stark_err_t stark_display_set_backlight(uint8_t pct);
```

Implementation: `esp_lcd_panel_io_spi` + `esp_lcd_ili9341` (registry component, pinned
`==2.1.0`, fallback `==2.0.2` — ADR-0004),
SPI2_HOST, `esp_lcd_panel_draw_bitmap` per band. Two DMA-capable band buffers of
`320 × STARK_DISPLAY_BAND_H` (default 40 → 25.6 kB each, 51.2 kB total) in internal
RAM, ping-ponged so rendering overlaps the DMA transfer. `stark_display_render()` walks
the requested area band by band; the UI never sees this.

STARK-0016 specifics: the band grid is fixed (rows `[k·BAND_H, (k+1)·BAND_H)`); for each
intersected band the callback's surface covers exactly *area ∩ band* (its origin is
that rectangle's corner) and only those pixels are transferred, so a partial render
touches nothing else. The walk is a pure, host-tested function (`display_bands.c`). The
call returns only after every transfer has completed; a full-screen render logs
`display: full refresh <N> ms` (informational — `docs/measurements.md`).

Constraints this satisfies: no PSRAM (ADR-0014), no 150 kB framebuffer, partial updates
are naturally cheap, and the same code works on any panel size.

Wokwi note: the ILI9341 part ignores RST and backlight. The driver still drives them —
the sim simply does not model them, which is exactly the "no simulator conditionals"
outcome we want.

### 6.6 `stark_input` (L2 core + L3 service)

```c
typedef enum { STARK_KEY_UP, STARK_KEY_DOWN, STARK_KEY_LEFT, STARK_KEY_RIGHT,
               STARK_KEY_OK, STARK_KEY_BACK } stark_key_t;
/* The key count is stark_board.h's STARK_KEY_COUNT (a board fact), not an enum
   member: stark_input.h asserts STARK_KEY_COUNT == STARK_KEY_BACK + 1. */

typedef enum { STARK_KEY_PRESS, STARK_KEY_RELEASE, STARK_KEY_REPEAT,
               STARK_KEY_LONG,  STARK_KEY_SHORT } stark_key_action_t;

/* input_core.h (STARK-0011) */
typedef struct { stark_key_t key; stark_key_action_t action; uint8_t repeat; } input_action_t;
void   input_core_init(input_core_t *c);
size_t input_core_update(input_core_t *c, uint8_t raw_bitmap, uint32_t now_ms,
                         input_action_t *out, size_t max_out);   /* INPUT_CORE_MAX_ACTIONS */
```

Core: a per-key state machine fed by `(raw_bitmap, now_ms)`, emitting actions into a
caller-supplied output array. Fully deterministic, fully host-tested. Timings are
measured from the debounced edge (when `PRESS` is emitted); a gap between updates never
bursts missed repeats, and a release seen after 500 ms without `LONG` yields `LONG` then
`RELEASE`, never `SHORT`:

* debounce: 20 ms stable-state filter
* `SHORT` on release before 500 ms; `LONG` at 500 ms held (emitted once)
* `REPEAT` every 120 ms after a 400 ms hold, for UP/DOWN/LEFT/RIGHT only
* simultaneous keys are independent; no chords in V0

Service: a 5 ms `esp_timer` samples the six GPIOs (active-low, internal pull-up),
runs the core, publishes `STARK_EVT_KEY` events. No ISR, no per-pin interrupts —
polling at 5 ms is cheaper and immune to contact bounce storms.

### 6.6a `stark_buzzer` (L3)

```c
stark_err_t stark_buzzer_init(void);                     /* LEDC channel 0, silent */
stark_err_t stark_buzzer_tone(uint16_t hz, uint16_t ms);
stark_err_t stark_buzzer_play(const stark_buzzer_note_t *notes, size_t count); /* <= 16 */
void        stark_buzzer_stop(void);
void        stark_buzzer_click(void);   /* 20 ms, 2 kHz: menu selection change */
void        stark_buzzer_reject(void);  /* 60 ms, 300 Hz: BACK at root, nothing to select */
```

Non-blocking (STARK-0020): a request starts the first note and returns; a one-shot
`esp_timer` steps through the rest; a new request replaces the old. One mutex covers
the sequence, the PWM and the timer; a callback that fired for an older arming is
recognised by its deadline and ignored. Before `init()` every call is a no-op, so
`stark_ui` calls the feedback sounds unconditionally.

### 6.7 `stark_ui` (L4)

A screen stack plus a small widget set. Retained-mode-lite: screens keep their own
state and declare *damage*; the UI loop coalesces damage and issues one
`stark_display_render()` per frame.

```c
typedef struct stark_screen {
    const char *name;
    void (*on_enter)(struct stark_screen *self);
    void (*on_exit) (struct stark_screen *self);
    bool (*on_event)(struct stark_screen *self, const stark_event_t *e); /* true = consumed */
    void (*on_render)(struct stark_screen *self, gfx_surface_t *s);
    void       *state;
    gfx_rect_t  damage;      /* union of dirty areas; empty = nothing to redraw */
} stark_screen_t;

stark_err_t stark_ui_init(void);
stark_err_t stark_ui_push(stark_screen_t *screen);
stark_err_t stark_ui_pop(void);
void        stark_ui_invalidate(stark_screen_t *s, gfx_rect_t area);
void        stark_ui_tick(void);   /* called by the UI loop: dispatch + render */
```

STARK-0017 specifics: `stark_ui_init()` subscribes the UI to every event type on the
global bus and `stark_ui_tick()` dispatches it, so screens receive events through
`on_event`; an unconsumed BACK **short** press pops (a no-op at the root). A push or pop
redraws the new top screen and the status bar in full; otherwise each tick renders the
union of the top screen's damage and the status bar's, cleared before rendering. Screens
draw with their clip limited to the area below the 16 px status bar. The loop body is
`stark_ui_task()` (created by `main`, §7); colours and layout live in `ui_theme.h`.

Widgets in V0: **status bar** (title; battery/SD/clock arrive with their tasks — V0 draws
nothing there rather than placeholders) and
**list menu** (`ui_menu_t` in `ui_menu.h`: items, scroll window, selection,
wrap-around; icons are not in V0). The menu *model* (selection movement, scroll window
arithmetic, paging) lives in `ui_menu_model.c` as pure code with host tests; the
rendering lives beside it (`ui_menu.c`) and is verified visually in Wokwi.

STARK-0018 specifics: 24 px rows (nine below the status bar), the selection inverted,
disabled items dimmed and skipped, a 4 px scroll indicator at the right while the list
overflows, `(empty)` for a list with no items. UP/DOWN act on press and repeat with
wrap; LEFT/RIGHT page one window, clamped at the ends; OK (short) activates. A move
between adjacent visible rows invalidates exactly those two rows; a move that scrolls
the window, wraps or pages invalidates the list; because the UI renders the *union* of a
frame's damage, a move that skips disabled items also repaints the rows between.
Each change logs `menu: sel=<n> "<label>"` (INFO; the index is 0-based).

Navigation contract, global and non-negotiable:

| Key | Meaning |
| --- | --- |
| UP / DOWN | Move selection / adjust value |
| LEFT / RIGHT | Page, or adjust coarse value; app-defined |
| OK | Activate / confirm / enter |
| BACK | Leave screen; at root, no-op with a short buzz |
| BACK long-press | Force-return to root menu from anywhere (added in V0.1) |

### 6.8 `stark_app` (L4)

```c
typedef struct {
    const char *id;              /* "about", "i2c_scan" — stable, used in settings */
    const char *title;           /* menu label */
    const char *category;        /* "System", "Lab", "Radio" — menu grouping */
    stark_err_t (*on_start)(void **state);
    void        (*on_stop)(void *state);
    stark_screen_t *(*screen)(void *state);
} stark_app_t;

const stark_app_t *stark_app_find(const char *id);
size_t             stark_app_list(const stark_app_t **out, size_t max);
stark_err_t        stark_app_launch(const char *id);
void               stark_app_stop_current(void);
```

**Registration is explicit and static** (ADR-0009). Each app component exposes one
`extern const stark_app_t app_<name>;`, declared in `components/stark_app/app_list.h`,
and `app_registry.c` holds the single array:

```c
static const stark_app_t *const stark_apps[] = {
    &app_about, &app_inputtest, &app_displaytest, &app_buzzertest,
};
```

Adding an app is one new directory plus one line here. No linker sections, no section
walking, nothing that can silently vanish under `--gc-sections`. The array is the only
shared file an app author touches, and the registry is logged at boot so the list is
always observable.

Link-time section registration remains a *documented future option* — the
`stark_app_find/list/launch` API is deliberately unchanged by such a migration — but it
is not V0's problem (ADR-0009 revisit trigger: ~15 apps, or apps living outside this
repository).

STARK-0019 specifics: `stark_app_init()` logs `app: registry n=<N> <id>,…` (registry
order), builds the launcher — the root `ui_menu_t`, "STARK ONE", apps sorted by category
then title (a stable, host-tested pure function) — and pushes it. `stark_app_launch()`
runs `on_start`, pushes the app's screen and logs `app: start <id>`; popping that screen
(BACK) calls `on_stop` and logs `app: stop <id>` — the manager chains onto the screen's
`on_exit` for the duration. A failing `on_start` (or a NULL screen) logs
`app: start <id> failed: <err>` and the launcher stays; it never panics. One app at a
time, one screen per app in V0.

**Capability gating is not part of V0.** `caps_required`, capability bits and
disabled-because-hardware-is-absent menu items arrive at V0.5 together with
`stark_module` and the hardware they describe. V0 lists the apps that exist and
launches them.

Execution model: **cooperative, single task**. Apps run inside the UI task through their
screen callbacks. An app that needs a worker (Sub-GHz receive loop, SD write) creates
its own FreeRTOS task in `on_start` and communicates via the event bus; `on_stop` must
join it. No preemptive app scheduling, no app sandbox — this is a single-user tool, and
the complexity is not earned.

### 6.9 Deferred components (contracts only, no V0 code)

| Component | Milestone | One-line contract |
| --- | --- | --- |
| `stark_storage` | V0.2 | Mounts SD (FATFS over SPI2, shared bus), `stark_storage_*` file helpers, hot-insert events |
| `stark_settings` | V0.2 | Typed key/value over NVS with defaults and change notifications |
| `stark_module` | V0.5 | Enumerates expansion modules (I²C ID EEPROM), introduces capability bits and `caps_required`, arbitrates bus/CS. **Nothing of this exists before V0.5** — no bits, no fields, no stubs |
| `stark_power` | V1/V2 | Battery voltage/SoC, charge state, backlight dimming, light-sleep policy |
| `stark_diag` | V0.1 / V0.7 | v1 at V0.1 (STARK-0109): heap, task high-water marks, FPS, frame overruns, event drops, surfaced in a Diagnostics app. Self-test routines and log export: V0.7 |

---

## 7. Boot sequence

```
app_main()
 ├─ stark_log_init()                       – early, UART0 console
 ├─ stark_board_init()                     – clocks, GPIO defaults, SPI2 bus
 ├─ stark_event_init()                     – bus + lock + semaphore
 ├─ stark_display_init()                   – panel, band buffers, backlight on
 │    └─ on failure: stark_panic()         – nothing else is meaningful without it
 ├─ stark_buzzer_init()                    – LEDC channel
 ├─ stark_input_start()                    – 5 ms sampling timer
 ├─ stark_ui_init()                        – status bar + root menu screen
 ├─ stark_app_init()                       – walk the registry section
 └─ xTaskCreate(stark_ui_task, prio 5, 6 kB stack, core 1)
        loop: wait-for-event(≤33 ms) → stark_ui_tick() → render damaged bands
```

Since STARK-0017 the UI task (`stark_ui_task`, created by `main`) is the event bus's one
consumer; before that, `app_main` drained the bus itself. `main` also
enables the `key` log tag at DEBUG (with `CONFIG_LOG_MAXIMUM_LEVEL_DEBUG` compiled in),
because `key:` lines are DEBUG by contract yet asserted by scenarios (TESTING.md §4).

Boot budget **target** (a hardware goal, not a simulation gate — ADR-0011): first pixel
within 400 ms of reset, root menu interactive within 600 ms, measured on the physical
prototype at V1. Firmware logs `boot: ui_ready in NNN ms` at every boot; Wokwi CI
asserts on the **presence and ordering** of that line, never on the number in it.

## 8. Concurrency & memory model

**Tasks (V0):**

| Task / context | Prio | Stack | Role |
| --- | --- | --- | --- |
| `stark_ui` | 5 | 6 kB | Event dispatch, app ticks, rendering. Owns SPI2 and the band buffers. |
| `esp_timer` (input) | high | – | 5 ms key sample → publish events. Never blocks. |
| `esp_timer` (led/buzzer) | high | – | Heartbeat blink, tone sequence steps. |
| IDLE/main | – | – | Standard IDF. |

**Rules:**

* Only `stark_ui` touches the display or the SPI2 bus in V0. Any future component that
  needs SPI2 goes through `stark_display`/`stark_module` arbitration, never directly.
* Only the event bus crosses task boundaries. No shared mutable globals between tasks.
* All allocation happens during init. The render and input paths are allocation-free.
* Static memory budget V0: band buffers 51.2 kB + event ring 2 kB + UI/app state < 8 kB.
  Target: ≥ 200 kB internal heap free at the root menu, asserted by a diagnostics log
  line at boot.

## 9. Configuration (Kconfig)

Each component owns its options in `components/<name>/Kconfig.stark`; `main/Kconfig.projbuild`
assembles them into the single top-level `STARK` menu with one explicit `rsource` line per
component (STARK-0100 — ESP-IDF renders every component `Kconfig` as a menu of its own, so
per-component `menu "STARK"` blocks would give one STARK menu each). A component that
gains options adds its line there in the same change:

| Symbol | Default | Purpose |
| --- | --- | --- |
| `STARK_BOARD_*` | `DEVKITC1` | Board variant → pin map |
| `STARK_DISPLAY_BAND_H` | 40 | Band height in lines |
| `STARK_DISPLAY_SPI_HZ` | 40000000 | 20 MHz recommended on breadboard |
| `STARK_DISPLAY_SWAP_XY` / `_MIRROR_X` / `_MIRROR_Y` | y / n / n | Rotation: default = landscape 320×240 (MADCTL MV, "rotation 1"); replaces a single `ROTATION` symbol (STARK-0015) |
| `STARK_DISPLAY_BGR` / `_INVERT` | y / n | Panel colour order / inversion — fix colours here, never in `stark_gfx` |
| `STARK_INPUT_POLL_MS` | 5 | Key sampling period |
| `STARK_EVENT_QUEUE_LEN` | 32 | Ring capacity |
| `STARK_UI_TARGET_FPS` | 30 | Render cap |
| `STARK_LOG_LEVEL` | INFO | Maps to esp_log level |

No runtime behaviour may depend on a Kconfig symbol that Wokwi and hardware would need
to set differently (ADR-0010).

## 10. Dependency enforcement

* Each component's `CMakeLists.txt` lists `REQUIRES` explicitly; nothing relies on
  transitive visibility.
* `scripts/check_layers.py` (STARK-0100; `lint` job and `scripts/check.sh`) parses every
  `REQUIRES`/`PRIV_REQUIRES` list against the component layer table below and fails on
  an upward dependency, a same-layer dependency not on the allowlist, a platform
  dependency of a pure component, any component but `main` requiring an app, or an
  unclassified `stark_*` component.

  | Layer | Components |
  | --- | --- |
  | universal | `stark_err` (any layer may require it; it requires nothing) |
  | L0 | `stark_board` |
  | L1 | `stark_hal`, `stark_log` |
  | L2 | `stark_gfx` (the only all-pure component) |
  | L3 | `stark_event`, `stark_input` (each an L2 core plus an L3 port), `stark_display`, `stark_buzzer` |
  | L4 | `stark_ui`, `stark_app` |
  | L5 | every `apps/app_*` |
  | root | `main` — the composition root, may require anything |

  Same-layer allowlist: `stark_input → stark_event` (the input service publishes key
  events) and `stark_app → stark_ui` (the launcher is a `stark_ui` menu). Pure
  components (`stark_err`, `stark_gfx`) require no platform component. A new component,
  or a new sideways edge, updates the script and this table in the same change.
* Our own components (everything under `components/`, `apps/`, `main/`) build with
  `-Wshadow -Wconversion -Wdouble-promotion` on top of ESP-IDF's warnings-as-errors,
  applied by the root `CMakeLists.txt` and never to vendor components (TESTING.md §3).
* L2 cores additionally build in the host test project *without* ESP-IDF on the include
  path — so an accidental `#include "esp_log.h"` in a core fails the host build. That
  is the cheapest possible enforcement and it is why cores are host-built at all.

## 11. Extension recipe: adding a new app

1. `apps/app_<name>/` with `CMakeLists.txt` (`REQUIRES stark_app stark_ui stark_gfx …`,
   registered `WHOLE_ARCHIVE` so the registry's reference links without any core
   build edit — `apps/README.md`). The root `CMakeLists.txt` lists `apps` in
   `EXTRA_COMPONENT_DIRS` once.
2. Implement `stark_screen_t` callbacks and a `const stark_app_t app_<name>` descriptor.
3. Declare it in `components/stark_app/app_list.h` and add one line to the
   `stark_apps[]` array in `app_registry.c`.
4. Add host tests for any pure logic (protocol codec, parser, formatter).
5. Add a Wokwi scenario if the app is reachable in simulation.
6. From V0.5 onward: declare `caps_required` if it depends on a hardware module.

Step 3 is the *only* core touch, and it is a declaration, not logic. If a new app
forces any other core edit, the abstraction is wrong — fix the abstraction in a
separate task.

## 12. Extension recipe: adding a hardware module (V0.5+)

1. Add the pins/caps to `stark_board` for the target variants.
2. Implement a driver component at L3 with its own bus ownership rules.
3. Register its capability bit so dependent apps enable themselves automatically.
4. Model it in `diagram.json` if Wokwi has a compatible part; otherwise document the
   gap in [WOKWI.md](WOKWI.md) §Limitations and rely on host tests + hardware tests.

## 13. Known architectural risks

| Risk | Mitigation |
| --- | --- |
| Band rendering complicates apps that want a framebuffer | Provide `stark_display_render()` region API only; if a real need appears, add an opt-in PSRAM framebuffer path behind the same interface |
| Single UI task blocks on a slow app | Apps must not block > 16 ms in a callback; `stark_diag` reports frame overruns; long work goes to a worker task |
| The static app array is a shared file (merge conflicts) | Accepted knowingly (ADR-0009); one line per app, and `stark_app_list()` is dumped at boot at INFO level |
| Event bus becomes a dumping ground | Only 4 event families; new families require an ADR |
| LVGL adoption later invalidates `stark_ui` | Screens are the only UI abstraction apps see; an LVGL backend would implement the same `stark_screen_t` contract (ADR-0005) |
