/*
 * test_ui_fault.c — fault containment on the host UI test port (TASKS.md
 * STARK-0108), through the real App Test: a reported fault unwinds to the
 * launcher with an "App stopped" alert, never a panic; stale reports are
 * ignored. (The worker itself needs FreeRTOS: see the emulator scenario.)
 */
#include <string.h>
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "unity.h"

extern const stark_app_t app_apptest;
static const stark_app_t *const k_apps[] = {&app_apptest};

void setUp(void)
{}
void tearDown(void)
{}

static int log_find(const char *needle, size_t from)
{
    for (size_t i = from; i < ui_port_log_count(); i++) {
        if (strstr(ui_port_log_line(i), needle) != NULL) {
            return (int)i;
        }
    }
    return -1;
}

static void assert_log_order(const char *const *lines, size_t n)
{
    size_t from = 0;
    for (size_t i = 0; i < n; i++) {
        int at = log_find(lines[i], from);
        TEST_ASSERT_TRUE_MESSAGE(at >= 0, lines[i]);
        from = (size_t)at + 1;
    }
}

static void tick2(void)
{
    stark_ui_tick(); /* the key's handler publishes the report ... */
    stark_ui_tick(); /* ... which the next dispatch delivers */
}

static void test_fault_containment(void)
{
    hal_host_set_now_us(0);
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
    stark_ui_tick();

    /* Outside an app, a stray report does nothing. */
    stark_app_fail(STARK_ERR_IO);
    tick2();
    TEST_ASSERT_EQUAL(-1, log_find("app: fault", 0));

    /* App Test -> "Fail" (row 3): fault, unwind, alert. */
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("apptest"));
    stark_ui_tick();
    for (int i = 0; i < 3; i++) {
        ui_port_key(STARK_KEY_DOWN, STARK_KEY_PRESS);
    }
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=3 \"Fail\""));
    ui_port_short(STARK_KEY_OK);
    tick2();
    static const char *const fault[] = {"app: fault apptest: I/O error", "app: stop apptest",
                                        "ui: home depth=1", "dialog: open \"App stopped\""};
    assert_log_order(fault, 4);
    ui_port_short(STARK_KEY_OK); /* close the alert */
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: ok"));

    /* A report stamped with an earlier launch is stale once App Test runs
     * again: ignored. */
    ui_port_reset_records();
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("apptest"));
    stark_ui_tick();
    stark_event_t stale = {.type = STARK_EVT_APP_REQUEST};
    stale.app.id = STARK_APP_REQ_FAIL;
    stale.app.arg = 0; /* no launch has generation 0 */
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_publish(&stale));
    stark_ui_tick();
    TEST_ASSERT_EQUAL(-1, log_find("app: fault", 0));
    TEST_ASSERT_EQUAL(-1, log_find("app: stop", 0));

    /* The worker items degrade cleanly on the host (no FreeRTOS). */
    ui_port_key(STARK_KEY_DOWN, STARK_KEY_PRESS);
    ui_port_key(STARK_KEY_DOWN, STARK_KEY_PRESS);
    stark_ui_tick();
    ui_port_short(STARK_KEY_OK); /* "Worker demo" */
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("apptest: worker demo not started: Not supported"));
    ui_port_short(STARK_KEY_BACK);
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop apptest"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fault_containment);
    return UNITY_END();
}
