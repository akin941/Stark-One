/*
 * test_ui_app_caps.c — caps_required on the host UI port (TASKS.md STARK-0502,
 * STARK-0505): all, one and none of an app's capabilities present; a refused
 * launch leaves the launcher; an app with no requirement is unaffected; losing a
 * capability does not close a running app; the launcher dims and restores rows,
 * and moves a selection off a row that is dimmed, but only once no app covers it.
 * Frames pinned after inspection.
 */
#include <stdio.h>
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "unity.h"

#define CAP_A (1u << 0)
#define CAP_B (1u << 2)

static void render(stark_screen_t *self, gfx_surface_t *s)
{
    (void)self;
    (void)s;
}

static stark_screen_t s_screen = {.name = "t", .on_render = render};
static int s_starts;

static stark_err_t on_start(void **state)
{
    (void)state;
    s_starts++;
    return STARK_OK;
}

static stark_screen_t *screen(void *state)
{
    (void)state;
    return &s_screen;
}

static const stark_app_t k_free = {
    .id = "free", .title = "Free", .category = "T", .on_start = on_start, .screen = screen};
static const stark_app_t k_need = {.id = "need",
                                   .title = "Need",
                                   .category = "T",
                                   .on_start = on_start,
                                   .screen = screen,
                                   .caps_required = CAP_A | CAP_B};
static const stark_app_t *const k_apps[] = {&k_free, &k_need};

void setUp(void)
{}
void tearDown(void)
{}

static void test_gating(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 2));
    TEST_ASSERT_TRUE(stark_app_available(&k_free));

    /* none */
    TEST_ASSERT_FALSE(stark_app_available(&k_need));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, stark_app_launch("need"));
    TEST_ASSERT_TRUE(ui_port_log_contains("app: need needs caps 0x00000005"));
    TEST_ASSERT_EQUAL(0, s_starts);

    /* one of two */
    stark_app_set_caps(CAP_A);
    TEST_ASSERT_TRUE(ui_port_log_contains("app: caps 0x00000001"));
    TEST_ASSERT_FALSE(stark_app_available(&k_need));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, stark_app_launch("need"));

    /* no requirement: unaffected */
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("free"));
    TEST_ASSERT_EQUAL(1, s_starts);
    stark_app_stop_current();
    stark_ui_tick();

    /* all, plus unknown bits */
    stark_app_set_caps(CAP_A | CAP_B | 0x80000000u);
    TEST_ASSERT_TRUE(stark_app_available(&k_need));
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("need"));
    TEST_ASSERT_EQUAL(2, s_starts);

    /* lost while running: the app stays until it leaves, then is refused */
    stark_app_set_caps(0);
    stark_ui_tick();
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, stark_app_launch("free"));
    stark_app_stop_current();
    stark_ui_tick();
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, stark_app_launch("need"));
    TEST_ASSERT_EQUAL(2, s_starts);

    /* restored */
    stark_app_set_caps(CAP_A | CAP_B);
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("need"));
    stark_app_stop_current();
    stark_ui_tick();
}

static void pin(const char *name, uint64_t want)
{
    stark_ui_tick();
    uint64_t h = ui_port_fb_hash();
    if (h != want) {
        char path[64];
        snprintf(path, sizeof path, "ui_caps_%s.ppm", name);
        (void)ui_port_dump_ppm(path);
        printf("caps %s hash 0x%016llx (see %s)\n", name, (unsigned long long)h, path);
    }
    TEST_ASSERT_EQUAL_HEX64(want, h);
}

/* After test_gating: rows are T header 0, Free 1, Need 2. */
static void test_launcher_rows(void)
{
    stark_app_set_caps(0);
    stark_ui_tick();
    pin("need_dimmed", UINT64_C(0xa0a274ed5bcc50eb));
    ui_port_short(STARK_KEY_DOWN); /* one enabled row: nothing to move to */
    stark_ui_tick();
    ui_port_short(STARK_KEY_OK);
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start free"));
    stark_app_stop_current();
    stark_ui_tick();

    stark_app_set_caps(CAP_A | CAP_B); /* restored: selectable */
    stark_ui_tick();
    pin("need_enabled", UINT64_C(0x0fe747cad958dde8));
    ui_port_reset_records();
    ui_port_short(STARK_KEY_DOWN);
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=2 \"Need\""));

    ui_port_reset_records();
    stark_app_set_caps(CAP_A); /* dimmed under the selection: it moves, silently */
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=1 \"Free\""));
    pin("selection_moved", UINT64_C(0xa0a274ed5bcc50eb));
    ui_port_short(STARK_KEY_OK);
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start free"));
    stark_app_stop_current();
    stark_ui_tick();
}

/* After test_launcher_rows: selection on Free 1, caps CAP_A. */
static void test_selection_behind_an_open_app(void)
{
    stark_app_set_caps(CAP_A | CAP_B);
    stark_ui_tick();
    ui_port_short(STARK_KEY_DOWN);
    stark_ui_tick();
    ui_port_short(STARK_KEY_OK);
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start need"));

    /* lost and back while the app is open: the selection stays on its row */
    ui_port_reset_records();
    stark_app_set_caps(CAP_A);
    stark_ui_tick();
    TEST_ASSERT_FALSE(ui_port_log_contains("menu: sel="));
    stark_app_set_caps(CAP_A | CAP_B);
    stark_app_stop_current();
    stark_ui_tick();
    TEST_ASSERT_FALSE(ui_port_log_contains("menu: sel="));
    ui_port_short(STARK_KEY_OK);
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start need"));

    /* still lost when the app leaves: only then does it move */
    ui_port_reset_records();
    stark_app_set_caps(CAP_A);
    stark_ui_tick();
    TEST_ASSERT_FALSE(ui_port_log_contains("menu: sel="));
    stark_app_stop_current();
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=1 \"Free\""));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_gating);
    RUN_TEST(test_launcher_rows);
    RUN_TEST(test_selection_behind_an_open_app);
    return UNITY_END();
}
