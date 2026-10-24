/*
 * ir_view.c — the IR app's receive view (TASKS.md STARK-0305): the last
 * IRAPP_RECENT captures, newest first, each with its protocol, address and
 * command (or RAW and its length), carrier and duration count. `Save last`
 * or OK on a capture saves it; `Browse saved` opens Browse (STARK-0306).
 */
#include <stdio.h>
#include "ir_codec.h"
#include "irapp.h"
#include "stark_ir.h"
#include "stark_log.h"

enum { ROW_SAVE, ROW_BROWSE, ROW_FIRST_CAPTURE };

static ir_signal_t s_caps[IRAPP_RECENT]; /* [0] is the newest */
static size_t s_count;
static char s_labels[IRAPP_RECENT][24];
static char s_values[IRAPP_RECENT][16];
static ui_menu_item_t s_items[ROW_FIRST_CAPTURE + IRAPP_RECENT];

void irapp_describe(const ir_signal_t *s, char *label, size_t len)
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

void irview_reset(void)
{
    s_count = 0;
}

const ui_menu_item_t *irview_items(size_t *count)
{
    s_items[ROW_SAVE] = (ui_menu_item_t){.label = "Save last"};
    s_items[ROW_BROWSE] = (ui_menu_item_t){.label = "Browse saved"};
    /* Fixed rows (the menu model counts them once): unused ones blank and
     * never selectable. */
    for (size_t i = 0; i < IRAPP_RECENT; i++) {
        s_labels[i][0] = '\0';
        if (i < s_count) {
            irapp_describe(&s_caps[i], s_labels[i], sizeof s_labels[i]);
            snprintf(s_values[i], sizeof s_values[i], "%uk n%u",
                     (unsigned)(s_caps[i].carrier_hz / 1000u), (unsigned)s_caps[i].timings.n);
        }
        s_items[ROW_FIRST_CAPTURE + i] =
            (ui_menu_item_t){.label = s_labels[i], .disabled = i >= s_count};
    }
    *count = sizeof s_items / sizeof s_items[0];
    return s_items;
}

const char *irview_value(size_t index)
{
    return index >= ROW_FIRST_CAPTURE && index - ROW_FIRST_CAPTURE < s_count
               ? s_values[index - ROW_FIRST_CAPTURE]
               : NULL;
}

void irview_activate(size_t index)
{
    if (index == ROW_SAVE) {
        irapp_save(s_count > 0 ? &s_caps[0] : NULL);
    } else if (index == ROW_BROWSE) {
        irbrowse_open();
    } else {
        irapp_save(&s_caps[index - ROW_FIRST_CAPTURE]);
    }
}

void irview_drain(void)
{
    ir_signal_t s;
    while (stark_ir_take(&s) == STARK_OK) {
        size_t keep = s_count < IRAPP_RECENT ? s_count : IRAPP_RECENT - 1;
        for (size_t i = keep; i > 0; i--) {
            s_caps[i] = s_caps[i - 1];
        }
        s_caps[0] = s;
        s_count = keep + 1;
        char label[24];
        irapp_describe(&s, label, sizeof label);
        STARK_LOGI("irapp", "view %s", label);
    }
}
