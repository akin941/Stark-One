/*
 * test_gpio_views.c — the GPIO app's pin view (apps/app_gpio/gpio_views.c, TASKS.md STARK-0702):
 * the rows of each mode, the text of each row and what LEFT/RIGHT does to the configuration. Pure:
 * no service and no screen, so every line and every enum value is reachable from here.
 */
#include <string.h>
#include "gpioapp.h"
#include "unity.h"

static gpioapp_cfg_t s_cfg;
static const char *s_meas; /* the Measure row's text (NULL: none yet) */

void setUp(void)
{
    gpioview_cfg_reset(&s_cfg);
    s_meas = NULL;
}
void tearDown(void)
{}

static gpioview_row_t s_kinds[GPIOVIEW_ROWS_MAX];

static size_t rows(gpioapp_mode_t mode)
{
    s_cfg.mode = mode;
    return gpioview_rows(&s_cfg, s_kinds);
}

static const stark_lab_info_t k_hiz = {.mode = STARK_LAB_MODE_HIZ};

static const char *value(gpioview_row_t k, const stark_lab_info_t *info, bool level_read,
                         const char *probe, char *buf)
{
    const gpioview_t v = {
        .cfg = &s_cfg, .info = info, .level_read = level_read, .probe = probe, .meas = s_meas};
    return gpioview_value(k, &v, buf, 24);
}

static void test_defaults(void)
{
    TEST_ASSERT_EQUAL(GPIOAPP_MODE_HIZ, s_cfg.mode);
    TEST_ASSERT_EQUAL(STARK_LAB_PULL_NONE, s_cfg.pull);
    TEST_ASSERT_FALSE(s_cfg.level);
    TEST_ASSERT_EQUAL_UINT32(1000, s_cfg.hz);
    TEST_ASSERT_EQUAL_UINT32(50, s_cfg.duty_pct);
    TEST_ASSERT_EQUAL_UINT32(100, s_cfg.width_us);
}

/* Every mode shows Mode first, its own rows, and the pad row last. */
static void test_the_rows_of_every_mode(void)
{
    TEST_ASSERT_EQUAL_UINT(3, rows(GPIOAPP_MODE_HIZ));
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_MODE, s_kinds[0]);
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_PROBE, s_kinds[1]);
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_PAD, s_kinds[2]);

    TEST_ASSERT_EQUAL_UINT(5, rows(GPIOAPP_MODE_IN));
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_PULL, s_kinds[1]);
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_READ, s_kinds[2]);
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_MEASURE, s_kinds[3]); /* STARK-0703 */
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_PAD, s_kinds[4]);

    TEST_ASSERT_EQUAL_UINT(5, rows(GPIOAPP_MODE_OUT));
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_LEVEL, s_kinds[1]);
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_DRIVE, s_kinds[2]);
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_TOGGLE, s_kinds[3]);
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_PAD, s_kinds[4]);

    TEST_ASSERT_EQUAL_UINT(5, rows(GPIOAPP_MODE_PWM));
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_FREQ, s_kinds[1]);
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_DUTY, s_kinds[2]);
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_PWM, s_kinds[3]);

    TEST_ASSERT_EQUAL_UINT(5, rows(GPIOAPP_MODE_PULSE));
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_LEVEL, s_kinds[1]);
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_WIDTH, s_kinds[2]);
    TEST_ASSERT_EQUAL(GPIOVIEW_ROW_PULSE, s_kinds[3]);

    TEST_ASSERT_EQUAL_UINT(3, rows(GPIOAPP_MODES)); /* an unknown mode shows the Hi-Z rows */
    TEST_ASSERT_TRUE(GPIOVIEW_ROWS_MAX >= 5);
}

static void test_labels(void)
{
    const char *want[GPIOVIEW_ROW_COUNT] = {"Mode",   "Pull",  "Level", "Freq",    "Duty",
                                            "Width",  "Probe", "Read",  "Measure", "Drive",
                                            "Toggle", "Start", "Pulse", "Pad"};
    for (int k = 0; k < GPIOVIEW_ROW_COUNT; k++) {
        TEST_ASSERT_EQUAL_STRING(want[k], gpioview_label((gpioview_row_t)k, false));
    }
    TEST_ASSERT_EQUAL_STRING("Stop", gpioview_label(GPIOVIEW_ROW_PWM, true));
    TEST_ASSERT_EQUAL_STRING("Pad", gpioview_label(GPIOVIEW_ROW_COUNT, false));
    TEST_ASSERT_EQUAL_STRING("Mode", gpioview_label(GPIOVIEW_ROW_MODE, true));
}

static void test_value_text(void)
{
    char b[24];
    const char *modes[GPIOAPP_MODES] = {"Hi-Z", "Input", "Output", "PWM", "Pulse"};
    for (int m = 0; m < GPIOAPP_MODES; m++) {
        s_cfg.mode = (gpioapp_mode_t)m;
        TEST_ASSERT_EQUAL_STRING(modes[m], value(GPIOVIEW_ROW_MODE, &k_hiz, false, NULL, b));
    }
    s_cfg.mode = GPIOAPP_MODES; /* out of range reads as Hi-Z */
    TEST_ASSERT_EQUAL_STRING("Hi-Z", value(GPIOVIEW_ROW_MODE, &k_hiz, false, NULL, b));
    const char *pulls[] = {"None", "Up", "Down"};
    for (int p = 0; p < 3; p++) {
        s_cfg.pull = (stark_lab_pull_t)p;
        TEST_ASSERT_EQUAL_STRING(pulls[p], value(GPIOVIEW_ROW_PULL, &k_hiz, false, NULL, b));
    }
    s_cfg.pull = (stark_lab_pull_t)9;
    TEST_ASSERT_EQUAL_STRING("None", value(GPIOVIEW_ROW_PULL, &k_hiz, false, NULL, b));
    s_cfg.level = true;
    TEST_ASSERT_EQUAL_STRING("High", value(GPIOVIEW_ROW_LEVEL, &k_hiz, false, NULL, b));
    s_cfg.level = false;
    TEST_ASSERT_EQUAL_STRING("Low", value(GPIOVIEW_ROW_LEVEL, &k_hiz, false, NULL, b));
    TEST_ASSERT_EQUAL_STRING("1 kHz", value(GPIOVIEW_ROW_FREQ, &k_hiz, false, NULL, b));
    s_cfg.hz = 100;
    TEST_ASSERT_EQUAL_STRING("100 Hz", value(GPIOVIEW_ROW_FREQ, &k_hiz, false, NULL, b));
    s_cfg.hz = 2500; /* off the ladder: not a whole number of kHz */
    TEST_ASSERT_EQUAL_STRING("2500 Hz", value(GPIOVIEW_ROW_FREQ, &k_hiz, false, NULL, b));
    s_cfg.hz = 40000;
    TEST_ASSERT_EQUAL_STRING("40 kHz", value(GPIOVIEW_ROW_FREQ, &k_hiz, false, NULL, b));
    TEST_ASSERT_EQUAL_STRING("50 %", value(GPIOVIEW_ROW_DUTY, &k_hiz, false, NULL, b));
    const struct {
        uint32_t us;
        const char *text;
    } w[] = {{10, "10 us"},      {100, "100 us"},  {1000, "1 ms"},    {10000, "10 ms"},
             {100000, "100 ms"}, {1000000, "1 s"}, {1500, "1500 us"}, {1500000, "1500 ms"}};
    for (size_t i = 0; i < sizeof w / sizeof w[0]; i++) {
        s_cfg.width_us = w[i].us;
        TEST_ASSERT_EQUAL_STRING(w[i].text, value(GPIOVIEW_ROW_WIDTH, &k_hiz, false, NULL, b));
    }
    TEST_ASSERT_NULL(value(GPIOVIEW_ROW_PROBE, &k_hiz, false, NULL, b));
    TEST_ASSERT_NULL(value(GPIOVIEW_ROW_PROBE, &k_hiz, false, "", b));
    TEST_ASSERT_EQUAL_STRING("floating", value(GPIOVIEW_ROW_PROBE, &k_hiz, false, "floating", b));
    TEST_ASSERT_EQUAL_STRING("H", value(GPIOVIEW_ROW_READ, &k_hiz, true, NULL, b));
    TEST_ASSERT_EQUAL_STRING("L", value(GPIOVIEW_ROW_READ, &k_hiz, false, NULL, b));
    TEST_ASSERT_EQUAL_STRING("Hi-Z", value(GPIOVIEW_ROW_PAD, &k_hiz, false, NULL, b));
    TEST_ASSERT_EQUAL_STRING("50 kHz max", value(GPIOVIEW_ROW_MEASURE, &k_hiz, false, NULL, b));
    s_meas = "";
    TEST_ASSERT_EQUAL_STRING("50 kHz max", value(GPIOVIEW_ROW_MEASURE, &k_hiz, false, NULL, b));
    s_meas = "1.000 kHz duty 25.0 %";
    TEST_ASSERT_EQUAL_STRING("1.000 kHz duty 25.0 %",
                             value(GPIOVIEW_ROW_MEASURE, &k_hiz, false, NULL, b));
    TEST_ASSERT_NULL(value(GPIOVIEW_ROW_DRIVE, &k_hiz, false, NULL, b));
    TEST_ASSERT_NULL(value(GPIOVIEW_ROW_TOGGLE, &k_hiz, false, NULL, b));
    TEST_ASSERT_NULL(value(GPIOVIEW_ROW_PWM, &k_hiz, false, NULL, b));
    TEST_ASSERT_NULL(value(GPIOVIEW_ROW_PULSE, &k_hiz, false, NULL, b));
    TEST_ASSERT_NULL(value(GPIOVIEW_ROW_COUNT, &k_hiz, false, NULL, b));
}

static void test_state_text(void)
{
    char b[12];
    stark_lab_info_t i = {.mode = STARK_LAB_MODE_IN};
    gpioview_state_text(&i, true, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("IN H", b);
    gpioview_state_text(&i, false, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("IN L", b);
    i = (stark_lab_info_t){.mode = STARK_LAB_MODE_OUT, .level = true};
    gpioview_state_text(&i, false, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("OUT H", b);
    i.level = false;
    gpioview_state_text(&i, true, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("OUT L", b);
    i = (stark_lab_info_t){.mode = STARK_LAB_MODE_PWM, .hz = 1000, .duty_pct = 50};
    gpioview_state_text(&i, false, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("PWM", b);
    i = (stark_lab_info_t){.mode = STARK_LAB_MODE_HIZ};
    gpioview_state_text(&i, true, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("Hi-Z", b);
    i.mode = (stark_lab_mode_t)9; /* anything else is released */
    gpioview_state_text(&i, true, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("Hi-Z", b);
}

static void test_home_label(void)
{
    char b[24];
    gpioview_home_label(38, "LAB_UART_TX", b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("38 LAB_UART_TX", b);
    gpioview_home_label(40, NULL, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("40 ", b);
}

static void test_mode_walks_without_wrapping(void)
{
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_MODE, -1)); /* Hi-Z is the first */
    for (int m = GPIOAPP_MODE_IN; m < GPIOAPP_MODES; m++) {
        TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_MODE, +1));
        TEST_ASSERT_EQUAL(m, s_cfg.mode);
    }
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_MODE, +1)); /* Pulse is the last */
    TEST_ASSERT_EQUAL(GPIOAPP_MODE_PULSE, s_cfg.mode);
    TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_MODE, -1));
    TEST_ASSERT_EQUAL(GPIOAPP_MODE_PWM, s_cfg.mode);
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_MODE, 0));
}

static void test_pull_and_level_steps(void)
{
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_PULL, -1));
    TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_PULL, +1));
    TEST_ASSERT_EQUAL(STARK_LAB_PULL_UP, s_cfg.pull);
    TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_PULL, +1));
    TEST_ASSERT_EQUAL(STARK_LAB_PULL_DOWN, s_cfg.pull);
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_PULL, +1));
    TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_PULL, -1));
    TEST_ASSERT_EQUAL(STARK_LAB_PULL_UP, s_cfg.pull);

    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_LEVEL, -1)); /* low already */
    TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_LEVEL, +1));
    TEST_ASSERT_TRUE(s_cfg.level);
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_LEVEL, +1)); /* high already */
    TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_LEVEL, -1));
    TEST_ASSERT_FALSE(s_cfg.level);
}

static void test_frequency_duty_and_width_steps(void)
{
    TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_FREQ, +1));
    TEST_ASSERT_EQUAL_UINT32(2000, s_cfg.hz);
    TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_FREQ, -1));
    TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_FREQ, -1));
    TEST_ASSERT_EQUAL_UINT32(900, s_cfg.hz);
    s_cfg.hz = 100;
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_FREQ, -1));
    s_cfg.hz = 40000;
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_FREQ, +1));

    TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_DUTY, +1));
    TEST_ASSERT_EQUAL_UINT32(51, s_cfg.duty_pct);
    s_cfg.duty_pct = 100;
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_DUTY, +1));
    s_cfg.duty_pct = 0;
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_DUTY, -1));

    TEST_ASSERT_TRUE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_WIDTH, +1));
    TEST_ASSERT_EQUAL_UINT32(1000, s_cfg.width_us);
    s_cfg.width_us = 10;
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_WIDTH, -1));
    s_cfg.width_us = 1000000;
    TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, GPIOVIEW_ROW_WIDTH, +1));
}

/* Action rows and the pad row do not adjust, and a refusal changes nothing. */
static void test_other_rows_do_not_adjust(void)
{
    const gpioapp_cfg_t before = s_cfg;
    const gpioview_row_t none[] = {GPIOVIEW_ROW_PROBE, GPIOVIEW_ROW_READ,   GPIOVIEW_ROW_MEASURE,
                                   GPIOVIEW_ROW_DRIVE, GPIOVIEW_ROW_TOGGLE, GPIOVIEW_ROW_PWM,
                                   GPIOVIEW_ROW_PULSE, GPIOVIEW_ROW_PAD,    GPIOVIEW_ROW_COUNT};
    for (size_t i = 0; i < sizeof none / sizeof none[0]; i++) {
        TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, none[i], +1));
        TEST_ASSERT_FALSE(gpioview_adjust(&s_cfg, none[i], -1));
    }
    TEST_ASSERT_EQUAL_MEMORY(&before, &s_cfg, sizeof before);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_the_rows_of_every_mode);
    RUN_TEST(test_labels);
    RUN_TEST(test_value_text);
    RUN_TEST(test_state_text);
    RUN_TEST(test_home_label);
    RUN_TEST(test_mode_walks_without_wrapping);
    RUN_TEST(test_pull_and_level_steps);
    RUN_TEST(test_frequency_duty_and_width_steps);
    RUN_TEST(test_other_rows_do_not_adjust);
    return UNITY_END();
}
