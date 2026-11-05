/*
 * modules_view.c — the inspector's text (modulesapp.h). What a descriptor says
 * is shown as declared, what a driver confirmed as confirmed: a descriptor is
 * not proof of identity (ADR-0021 §2).
 */
#include <stdio.h>
#include <string.h>
#include "modulesapp.h"
#include "stark_log.h"
#include "stark_module.h"

#define VALUE_MAX 24

static const struct {
    uint32_t bit;
    const char *name;
} k_caps[] = {
    {STARK_CAP_IR_TX, "IR TX"},      {STARK_CAP_IR_RX, "IR RX"},       {STARK_CAP_NFC, "NFC"},
    {STARK_CAP_MOD_SPI, "SPI line"}, {STARK_CAP_MOD_IRQ, "IRQ lines"},
};

static stark_module_entry_t s_list[STARK_MODULE_ENTRIES];
static size_t s_order[STARK_MODULE_ENTRIES]; /* list row -> s_list */
static char s_labels[STARK_MODULE_ENTRIES][16];
static char s_values[MODULES_DETAIL_ROWS][VALUE_MAX];
static ui_menu_item_t s_items[MODULES_DETAIL_ROWS];
static size_t s_count;

/* Providers first: they are what the device has without any module. */
static size_t refresh(void)
{
    size_t total = stark_module_list(s_list, STARK_MODULE_ENTRIES);
    size_t n = total < STARK_MODULE_ENTRIES ? total : STARK_MODULE_ENTRIES;
    size_t row = 0;
    for (size_t i = 0; i < n; i++) {
        if (s_list[i].kind != STARK_MODULE_SLOT) {
            s_order[row++] = i;
        }
    }
    for (size_t i = 0; i < n; i++) {
        if (s_list[i].kind == STARK_MODULE_SLOT) {
            s_order[row++] = i;
        }
    }
    return n;
}

static const char *state_text(const stark_module_entry_t *e)
{
    switch (e->state) {
        case STARK_MODULE_KNOWN:
            return e->kind == STARK_MODULE_ONBOARD ? "Fitted" : "Present";
        case STARK_MODULE_GENERIC:
            return "Generic";
        case STARK_MODULE_CONFLICT:
            return "Conflict";
        case STARK_MODULE_ABSENT:
        default:
            return e->kind == STARK_MODULE_SLOT ? "No ID" : "Absent";
    }
}

static void add(const char *label, const char *value, bool enabled)
{
    if (s_count == MODULES_DETAIL_ROWS) {
        return;
    }
    s_items[s_count] = (ui_menu_item_t){.label = label, .disabled = !enabled};
    (void)snprintf(s_values[s_count], VALUE_MAX, "%s", value);
    s_count++;
}

const ui_menu_item_t *modulesview_list(size_t *count)
{
    size_t n = refresh();
    s_count = 0;
    for (size_t row = 0; row < n; row++) {
        const stark_module_entry_t *e = &s_list[s_order[row]];
        const char *label = e->name;
        if (e->kind == STARK_MODULE_SLOT) {
            (void)snprintf(s_labels[row], sizeof s_labels[row], "Slot %u", (unsigned)e->slot);
            label = s_labels[row];
        }
        /* a slot's name is what its descriptor says, kept to the value column */
        add(label,
            e->kind == STARK_MODULE_SLOT && e->state == STARK_MODULE_KNOWN ? e->name
                                                                           : state_text(e),
            true);
        STARK_LOGI("modules", "%s: %s", label, s_values[s_count - 1]);
    }
    *count = s_count;
    return s_items;
}

const ui_menu_item_t *modulesview_detail(size_t index, size_t *count)
{
    static char hex[VALUE_MAX];
    size_t n = refresh();
    s_count = 0;
    if (index >= n) {
        *count = 0;
        return s_items;
    }
    const stark_module_entry_t *e = &s_list[s_order[index]];
    add("State", state_text(e), false);
    if (e->kind == STARK_MODULE_SLOT && e->state != STARK_MODULE_ABSENT) {
        add("Name", e->name[0] != '\0' ? e->name : "Unknown", false);
        (void)snprintf(hex, sizeof hex, "0x%04X rev %u", (unsigned)e->id, (unsigned)e->rev);
        add("ID", hex, false);
    }
    if (e->state == STARK_MODULE_GENERIC) {
        add("Why", e->unsupported ? "unsupported caps" : module_desc_result_str(e->why), false);
    }
    for (size_t i = 0; i < sizeof k_caps / sizeof k_caps[0]; i++) {
        if ((e->declared & k_caps[i].bit) != 0) {
            add(k_caps[i].name, (e->confirmed & k_caps[i].bit) != 0 ? "confirmed" : "declared",
                false);
        }
    }
    uint32_t unknown = e->declared & ~(uint32_t)STARK_CAPS_KNOWN;
    if (unknown != 0) {
        (void)snprintf(hex, sizeof hex, "0x%08X", (unsigned)unknown);
        add("Unknown", hex, false); /* a future module: listed, ignored */
    }
    if (e->kind == STARK_MODULE_SLOT && e->declared != 0) {
        add("Declared", "not proof of identity", false);
    }
    for (size_t i = 0; i < s_count; i++) {
        STARK_LOGI("modules", "detail %s: %s", s_items[i].label, s_values[i]);
    }
    *count = s_count;
    return s_items;
}

const char *modulesview_value(size_t index)
{
    return index < s_count ? s_values[index] : NULL;
}
