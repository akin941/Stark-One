/*
 * test_ui_navigation.c — global navigation on the host UI test port
 * (TASKS.md STARK-0106): a BACK held long goes home from anywhere, a
 * dialog it unwinds reports CANCEL, the OK+BACK chord is reserved, and
 * paging that cannot move gives the reject buzz.
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

/* What the input core emits for a BACK held past 500 ms, then released. */
static void back_held_long(void)
{
    ui_port_key(STARK_KEY_BACK, STARK_KEY_PRESS);
    ui_port_key(STARK_KEY_BACK, STARK_KEY_LONG);
    ui_port_key(STARK_KEY_BACK, STARK_KEY_RELEASE);
}

static void test_navigation(void)
{
    hal_host_set_now_us(0);
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
    stark_ui_tick();

    /* Long-BACK from a dialog inside an app: both unwind, the dialog
     * reports CANCEL, the app stops, one home line with depth 2. */
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("apptest"));
    ui_port_short(STARK_KEY_OK); /* "Confirm dialog" */
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Confirm\""));
    ui_port_reset_records();
    back_held_long();
    stark_ui_tick();
    static const char *const home[] = {"dialog: cancel", "apptest: cancelled", "app: stop apptest",
                                       "ui: home depth=2"};
    assert_log_order(home, 4);
    TEST_ASSERT_EQUAL_INT(0, ui_port_buzzer_rejects());
    TEST_ASSERT_EQUAL(-1, log_find("app: start", 0)); /* the release relaunched nothing */

    /* At the root: a reject buzz, `ui: home at root`, nothing else. */
    ui_port_reset_records();
    back_held_long();
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("ui: home at root"));
    TEST_ASSERT_EQUAL_INT(1, ui_port_buzzer_rejects());

    /* OK+BACK chord in the launcher: reserved — logged, activates nothing.
     * (The core suppresses both keys' SHORT/LONG while it is latched.) */
    ui_port_reset_records();
    ui_port_key(STARK_KEY_OK, STARK_KEY_PRESS);
    ui_port_key(STARK_KEY_BACK, STARK_KEY_PRESS);
    ui_port_key(STARK_KEY_OK, STARK_KEY_CHORD);
    ui_port_key(STARK_KEY_OK, STARK_KEY_RELEASE);
    ui_port_key(STARK_KEY_BACK, STARK_KEY_RELEASE);
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("ui: chord reserved"));
    TEST_ASSERT_EQUAL(-1, log_find("app: start", 0));
    TEST_ASSERT_EQUAL(-1, log_find("ui: home", 0));

    /* stark_ui_pop_to_root() is public and never pops the root. */
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("apptest"));
    ui_port_reset_records();
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_pop_to_root());
    TEST_ASSERT_TRUE(ui_port_log_contains("ui: home depth=1"));
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop apptest"));

    /* Paging that cannot move rejects: LEFT on the first item of a list
     * that fits its window. */
    ui_port_reset_records();
    ui_port_key(STARK_KEY_LEFT, STARK_KEY_PRESS);
    stark_ui_tick();
    TEST_ASSERT_EQUAL_INT(1, ui_port_buzzer_rejects());
    TEST_ASSERT_EQUAL(-1, log_find("menu: sel=", 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_navigation);
    return UNITY_END();
}
