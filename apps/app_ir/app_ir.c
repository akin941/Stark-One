/*
 * app_ir.c — IR (TASKS.md STARK-0305): receive-first. While the app is open
 * the receiver listens; each capture is listed newest first with its
 * protocol, address and command (or RAW and its length) and its carrier and
 * duration count. `Save last` or OK on a capture writes it to the card;
 * `Browse saved` lists the files (STARK-0306). Leaving the app turns the
 * receiver off.
 */
#include <stdio.h>
#include "ir_codec.h"
#include "irapp.h"
#include "stark_app.h"
#include "stark_ir.h"
#include "stark_log.h"
#include "ui_menu.h"

enum { ROW_SAVE, ROW_BROWSE, ROW_FIRST_CAPTURE };

static ir_signal_t s_caps[IRAPP_RECENT]; /* [0] is the newest */
static size_t s_count;
static char s_labels[IRAPP_RECENT][24];
static char s_values[IRAPP_RECENT][16];
static ui_menu_item_t s_items[ROW_FIRST_CAPTURE + IRAPP_RECENT];
static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);

static void describe(const ir_signal_t *s, char *label, size_t len)
{
    const ir_frame_t *f = &s->frame;
    if (f->protocol == IR_PROTO_UNKNOWN) {
        snprintf(label, len, "RAW %u", (unsigned)s->timings.n);
    } else if (f->repeat) {
        snprintf(label, len, "%s repeat", ir_protocol_name(f->protocol));
    } else {
        snprintf(label, len, "%s %02X/%02X", ir_protocol_name(f->protocol), (unsigned)f->address,
                 (unsigned)f->command);
    }
}

static void rebuild(void)
{
    s_items[ROW_SAVE] = (ui_menu_item_t){.label = "Save last"};
    s_items[ROW_BROWSE] = (ui_menu_item_t){.label = "Browse saved"};
    /* Fixed rows (the menu model counts them once): unused ones blank and
     * never selectable. */
    for (size_t i = 0; i < IRAPP_RECENT; i++) {
        s_labels[i][0] = '\0';
        if (i < s_count) {
            describe(&s_caps[i], s_labels[i], sizeof s_labels[i]);
            snprintf(s_values[i], sizeof s_values[i], "%uk n%u",
                     (unsigned)(s_caps[i].carrier_hz / 1000u), (unsigned)s_caps[i].timings.n);
        }
        s_items[ROW_FIRST_CAPTURE + i] =
            (ui_menu_item_t){.label = s_labels[i], .disabled = i >= s_count};
    }
}

static const char *value_of(size_t index, void *ctx)
{
    (void)ctx;
    return index >= ROW_FIRST_CAPTURE && index - ROW_FIRST_CAPTURE < s_count
               ? s_values[index - ROW_FIRST_CAPTURE]
               : NULL;
}

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (index == ROW_SAVE) {
        irapp_save(s_count > 0 ? &s_caps[0] : NULL);
    } else if (index == ROW_BROWSE) {
        STARK_LOGI("irapp", "browse");
    } else {
        irapp_save(&s_caps[index - ROW_FIRST_CAPTURE]);
    }
}

static bool ir_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_IR &&
        e->system.arg == STARK_IR_EVT_RX) {
        ir_signal_t s;
        while (stark_ir_take(&s) == STARK_OK) {
            size_t keep = s_count < IRAPP_RECENT ? s_count : IRAPP_RECENT - 1;
            for (size_t i = keep; i > 0; i--) {
                s_caps[i] = s_caps[i - 1];
            }
            s_caps[0] = s;
            s_count = keep + 1;
            char label[24];
            describe(&s, label, sizeof label);
            STARK_LOGI("irapp", "view %s", label);
        }
        rebuild();
        stark_ui_invalidate(self, stark_ui_content_rect());
        return true;
    }
    return s_menu_event(self, e);
}

static stark_err_t ir_start(void **state)
{
    (void)state;
    s_count = 0;
    return stark_ir_rx_start(); /* receive-first: listening while the app is open */
}

static void ir_stop(void *state)
{
    (void)state;
    (void)stark_ir_rx_stop();
}

static stark_screen_t *ir_screen(void *state)
{
    (void)state;
    rebuild();
    ui_menu_init(&s_menu, "IR", s_items, sizeof s_items / sizeof s_items[0], activate, NULL);
    s_menu.value = value_of;
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = ir_event;
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): an LED with rays. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x24, 0x48, 0x12, 0x90, 0x09, 0x20, 0x00, 0x00, 0x07, 0xC0, 0x0F, 0xE0, 0x0F, 0xE0,
    0x0F, 0xE0, 0x0F, 0xE0, 0x1F, 0xF0, 0x04, 0x40, 0x04, 0x40, 0x04, 0x40, 0x04, 0x40, 0x00, 0x00,
};

const stark_app_t app_ir = {
    .id = "ir",
    .title = "IR",
    .category = "Lab",
    .on_start = ir_start,
    .on_stop = ir_stop,
    .screen = ir_screen,
    .icon = k_icon,
};
