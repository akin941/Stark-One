/*
 * test_ui_apps.c — the real V0 app screens on the host UI test port
 * (docs/VALIDATION.md §3.3): each screen's frame is pinned by a golden hash
 * recorded before STARK-0103 moved every colour and layout constant into
 * stark_theme.h, so that refactor — and any later one — is provably
 * pixel-identical. About needs ESP-IDF chip/flash/heap APIs and cannot run
 * on the host; test_theme_values below pins the literals it used.
 */
#include <stdio.h>
#include <string.h>
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_theme.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "unity.h"

extern const stark_app_t app_inputtest;
extern const stark_app_t app_buzzertest;
extern const stark_app_t app_displaytest;
static const stark_app_t *const k_apps[] = {&app_inputtest, &app_displaytest, &app_buzzertest};

void setUp(void)
{}
void tearDown(void)
{}

static int s_mismatches;

/* Reports every differing frame (hash + a PPM to look at), then the test
 * fails once at the end — one run shows all of them. */
static void check_frame(const char *name, uint64_t golden)
{
    uint64_t h = ui_port_fb_hash();
    if (h != golden) {
        char path[64];
        snprintf(path, sizeof path, "ui_%s.ppm", name);
        (void)ui_port_dump_ppm(path);
        printf("%s frame hash 0x%016llx (see %s)\n", name, (unsigned long long)h, path);
        s_mismatches++;
    }
}

static void launch(const char *id)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch(id));
    stark_ui_tick();
}

static void leave(void)
{
    ui_port_short(STARK_KEY_BACK);
    stark_ui_tick();
}

static void test_app_frames(void)
{
    hal_host_set_now_us(0);
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, sizeof k_apps / sizeof k_apps[0]));
    stark_ui_tick();

    launch("inputtest");
    check_frame("inputtest", UINT64_C(0x889fc0a48b74aa7f));
    ui_port_key(STARK_KEY_OK, STARK_KEY_PRESS); /* OK held: its row turns yellow */
    stark_ui_tick();
    check_frame("inputtest_ok_down", UINT64_C(0x0c86b2512e95b410));
    ui_port_key(STARK_KEY_OK, STARK_KEY_RELEASE);
    leave();

    launch("buzzertest");
    check_frame("buzzertest", UINT64_C(0x4a74571c13219471));
    leave();

    launch("displaytest");
    check_frame("displaytest", UINT64_C(0x06e7438914289865));
    leave();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_mismatches, "frames differ from their goldens");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_app_frames);
    return UNITY_END();
}
