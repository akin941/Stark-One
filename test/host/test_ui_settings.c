/*
 * test_ui_settings.c — the real Settings app on the host UI test port
 * (TASKS.md STARK-0205) over the real settings core and a fake NVS:
 * values in the right-aligned column, LEFT/RIGHT clamping with a buzz at
 * the limits, OK stepping forward with wrap, Restore defaults behind a
 * confirmation, and BACK committing what changed. The first frame is
 * pinned after inspection.
 */
#include <stdio.h>
#include "fake_nvs.h"
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_settings.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "unity.h"

extern const stark_app_t app_settings;
static const stark_app_t *const k_apps[] = {&app_settings};

void setUp(void)
{}
void tearDown(void)
{}

static void frame(void)
{
    stark_ui_tick();
}

static void key(stark_key_t k)
{
    ui_port_key(k, STARK_KEY_PRESS);
    ui_port_key(k, STARK_KEY_RELEASE);
    frame();
}

static void test_settings_app(void)
{
    hal_host_set_now_us(0);
    fake_nvs_reset();
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_settings_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("settings"));
    frame();

    /* The opening frame: four values and Restore defaults (inspected). */
    uint64_t h = ui_port_fb_hash();
    if (h != UINT64_C(0x17cf11b48e687703)) {
        (void)ui_port_dump_ppm("ui_settings.ppm");
        printf("settings frame hash 0x%016llx (see ui_settings.ppm)\n", (unsigned long long)h);
    }
    TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x17cf11b48e687703), h);

    /* Brightness: RIGHT at the maximum is refused (buzz), LEFT steps down. */
    int rejects = ui_port_buzzer_rejects();
    key(STARK_KEY_RIGHT);
    TEST_ASSERT_EQUAL(rejects + 1, ui_port_buzzer_rejects());
    key(STARK_KEY_LEFT);
    key(STARK_KEY_LEFT);
    TEST_ASSERT_EQUAL_UINT8(80, stark_settings_get(STARK_SET_BRIGHTNESS));
    TEST_ASSERT_TRUE(ui_port_log_contains("settings: brightness=80"));
    for (int i = 0; i < 10; i++) {
        key(STARK_KEY_LEFT);
    }
    TEST_ASSERT_EQUAL_UINT8(10, stark_settings_get(STARK_SET_BRIGHTNESS)); /* clamped */

    /* Volume: OK steps forward and wraps. */
    key(STARK_KEY_DOWN);
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_EQUAL_UINT8(0, stark_settings_get(STARK_SET_VOLUME)); /* High -> Off */

    /* Restore defaults: cancel changes nothing, OK restores and commits. */
    key(STARK_KEY_DOWN);
    key(STARK_KEY_DOWN);
    key(STARK_KEY_DOWN);
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=4 \"Restore defaults\""));
    rejects = ui_port_buzzer_rejects();
    key(STARK_KEY_LEFT); /* not adjustable */
    TEST_ASSERT_EQUAL(rejects + 1, ui_port_buzzer_rejects());
    ui_port_short(STARK_KEY_OK);
    frame();
    ui_port_short(STARK_KEY_BACK);
    frame();
    TEST_ASSERT_EQUAL_UINT8(10, stark_settings_get(STARK_SET_BRIGHTNESS));
    ui_port_short(STARK_KEY_OK);
    frame();
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_EQUAL_UINT8(100, stark_settings_get(STARK_SET_BRIGHTNESS));
    TEST_ASSERT_EQUAL_UINT8(3, stark_settings_get(STARK_SET_VOLUME));
    TEST_ASSERT_TRUE(ui_port_log_contains("settings: committed n=2"));

    /* A change, then BACK: committed to the store. */
    key(STARK_KEY_UP);
    key(STARK_KEY_UP);
    key(STARK_KEY_LEFT); /* repeat: Normal -> Slow */
    ui_port_short(STARK_KEY_BACK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop settings"));
    uint8_t v = 9;
    TEST_ASSERT_TRUE(fake_nvs_value("repeat", &v));
    TEST_ASSERT_EQUAL_UINT8(0, v);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_settings_app);
    return UNITY_END();
}
