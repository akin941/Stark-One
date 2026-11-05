/*
 * test_ui_modules.c — the real inspector on the host UI port (TASKS.md
 * STARK-0505) with a fake stark_module: known, generic, conflict and no-ID slots,
 * providers, unknown bits, an 18-character name, a refresh, and a module removed
 * while its details are open. Frames pinned after inspection.
 */
#include <stdio.h>
#include <string.h>
#include "modulesapp.h"
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_input.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "ui_port_module.h"
#include "unity.h"

extern const stark_app_t app_modules;
static const stark_app_t *const k_apps[] = {&app_modules};

void setUp(void)
{}
void tearDown(void)
{}

static void frame(void)
{
    stark_ui_tick();
}

static void press(stark_key_t k)
{
    ui_port_short(k);
    frame();
}

static void pin(const char *name, uint64_t want)
{
    uint64_t h = ui_port_fb_hash();
    if (h != want) {
        char path[64];
        snprintf(path, sizeof path, "ui_modules_%s.ppm", name);
        (void)ui_port_dump_ppm(path);
        printf("modules %s hash 0x%016llx (see %s)\n", name, (unsigned long long)h, path);
    }
    TEST_ASSERT_EQUAL_HEX64(want, h);
}

static void notify(void)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_MODULE;
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_publish(&e));
    frame();
}

static stark_module_entry_t slot(uint8_t n, stark_module_state_t st, const char *name,
                                 uint32_t declared, uint32_t confirmed)
{
    stark_module_entry_t e;
    memset(&e, 0, sizeof e);
    e.kind = STARK_MODULE_SLOT;
    e.state = st;
    e.slot = n;
    e.id = 0x0101;
    e.rev = 2;
    e.declared = declared;
    e.confirmed = confirmed;
    (void)snprintf(e.name, sizeof e.name, "%s", name);
    return e;
}

static stark_module_entry_t provider(stark_module_kind_t kind, const char *name, uint32_t caps,
                                     bool on)
{
    stark_module_entry_t e;
    memset(&e, 0, sizeof e);
    e.kind = kind;
    e.state = on ? STARK_MODULE_KNOWN : STARK_MODULE_ABSENT;
    e.declared = caps;
    e.confirmed = on ? caps : 0;
    (void)snprintf(e.name, sizeof e.name, "%s", name);
    return e;
}

static void set_table(stark_module_entry_t s0, stark_module_entry_t s1, bool nfc)
{
    stark_module_entry_t list[4] = {
        s0, s1, provider(STARK_MODULE_ONBOARD, "IR", STARK_CAP_IR_TX | STARK_CAP_IR_RX, true),
        provider(STARK_MODULE_BUS, "NFC reader", STARK_CAP_NFC, nfc)};
    ui_port_module_set(list, 4);
}

static void test_inspector(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
    stark_module_entry_t empty0 = slot(0, STARK_MODULE_ABSENT, "", 0, 0);
    stark_module_entry_t empty1 = slot(1, STARK_MODULE_ABSENT, "", 0, 0);
    set_table(empty0, empty1, false);
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("modules"));
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: IR: Fitted"));
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: NFC reader: Absent"));
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: Slot 0: No ID"));
    pin("empty", UINT64_C(0xa4651dff55ae566d));

    /* a module arrives: the list follows, and the selection stays where it was */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN); /* Slot 0 */
    set_table(slot(0, STARK_MODULE_KNOWN, "ABCDEFGHIJKLMNOPQR", STARK_CAP_NFC | 0x80000000u,
                   STARK_CAP_NFC),
              empty1, true);
    notify();
    pin("known_long_name", UINT64_C(0x9c92ee1b20179343));
    press(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail NFC: confirmed"));
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail Unknown: 0x80000000"));
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail Declared: not proof of identity"));
    pin("detail_known", UINT64_C(0x58ba10c58c74b7ca));

    /* removed while its details are open: no stale data */
    ui_port_reset_records();
    set_table(empty0, empty1, false);
    notify();
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail State: No ID"));
    TEST_ASSERT_FALSE(ui_port_log_contains("modules: detail NFC"));
    pin("detail_removed", UINT64_C(0x906811b46c37b862));

    press(STARK_KEY_BACK); /* details -> list, not the launcher */
    frame();
    TEST_ASSERT_FALSE(ui_port_log_contains("app: stop modules"));

    /* declared, not confirmed */
    ui_port_reset_records();
    set_table(slot(0, STARK_MODULE_KNOWN, "NFC carrier", STARK_CAP_NFC, 0), empty1, false);
    notify();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail NFC: declared"));
    press(STARK_KEY_BACK);

    /* generic, with its reason */
    ui_port_reset_records();
    stark_module_entry_t g = slot(0, STARK_MODULE_GENERIC, "", 0, 0);
    g.id = 0;
    g.why = MODULE_DESC_BAD_CRC;
    set_table(g, empty1, false);
    notify();
    pin("generic_list", UINT64_C(0x5ac2aaeb1ccd2441));
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail Why: bad crc"));
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail Name: Unknown"));
    press(STARK_KEY_BACK);

    /* impossible combination */
    ui_port_reset_records();
    stark_module_entry_t u = slot(0, STARK_MODULE_GENERIC, "Claims IR TX", STARK_CAP_IR_TX, 0);
    u.unsupported = true;
    set_table(u, empty1, false);
    notify();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail Why: unsupported caps"));
    press(STARK_KEY_BACK);

    /* a conflict */
    ui_port_reset_records();
    set_table(slot(0, STARK_MODULE_CONFLICT, "NFC carrier", STARK_CAP_NFC, STARK_CAP_NFC),
              slot(1, STARK_MODULE_CONFLICT, "NFC rival", STARK_CAP_NFC, STARK_CAP_NFC), true);
    notify();
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: Slot 0: Conflict"));
    pin("conflict", UINT64_C(0x43591505585d2c21));

    /* the wiring bits and a provider's details */
    ui_port_reset_records();
    set_table(
        slot(0, STARK_MODULE_KNOWN, "SPI lab module", STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ, 0),
        empty1, true);
    notify();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail SPI line: declared"));
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail IRQ lines: declared"));
    press(STARK_KEY_BACK);
    press(STARK_KEY_UP);
    press(STARK_KEY_UP); /* IR */
    ui_port_reset_records();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail State: Fitted"));
    TEST_ASSERT_TRUE(ui_port_log_contains("modules: detail IR TX: confirmed"));
    TEST_ASSERT_FALSE(ui_port_log_contains("modules: detail Declared"));
    press(STARK_KEY_BACK);
    press(STARK_KEY_BACK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop modules"));
}

static void test_view_edges(void)
{
    size_t n = 99;
    ui_port_module_reset();
    (void)modulesview_detail(9, &n);
    TEST_ASSERT_EQUAL(0, n); /* the row no longer exists */
    TEST_ASSERT_NULL(modulesview_value(0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_inspector);
    RUN_TEST(test_view_edges);
    return UNITY_END();
}
