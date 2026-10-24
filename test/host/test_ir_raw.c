/*
 * test_ir_raw.c — the `.ir` text format (TASKS.md STARK-0302): format →
 * parse round trips for every vector, a full 256-duration capture, every
 * malformed-input class, truncated buffers, and 10 000 seeded random inputs
 * that must never crash (ASan/UBSan).
 */
#include <stdio.h>
#include <string.h>
#include "ir_codec.h"
#include "ir_nec_vectors.h"
#include "ir_raw.h"
#include "ir_rc6_vectors.h"
#include "ir_sirc20_vectors.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static char s_text[IR_RAW_TEXT_MAX];

static stark_err_t parse(const char *text, ir_signal_t *out)
{
    return ir_raw_parse(text, strlen(text), out);
}

static void round_trip(const ir_vector_t *v)
{
    ir_signal_t s = {.carrier_hz = 38000};
    s.timings.n = v->n;
    memcpy(s.timings.us, v->timings, v->n * sizeof v->timings[0]);
    (void)ir_decode(&s.timings, &s.frame);
    TEST_ASSERT_TRUE_MESSAGE(ir_raw_format(&s, s_text, sizeof s_text) > 0, v->label);
    ir_signal_t r;
    TEST_ASSERT_EQUAL_MESSAGE(STARK_OK, parse(s_text, &r), v->label);
    TEST_ASSERT_EQUAL_UINT16(s.timings.n, r.timings.n);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(s.timings.us, r.timings.us, s.timings.n);
    TEST_ASSERT_EQUAL_INT(s.frame.protocol, r.frame.protocol);
    TEST_ASSERT_EQUAL_HEX16(s.frame.address, r.frame.address);
    TEST_ASSERT_EQUAL_UINT32(38000, r.carrier_hz);
}

static void test_round_trip_every_vector(void)
{
    for (size_t i = 0; i < sizeof k_nec_vectors / sizeof k_nec_vectors[0]; i++) {
        round_trip(&k_nec_vectors[i]);
    }
    for (size_t i = 0; i < sizeof k_rc6_vectors / sizeof k_rc6_vectors[0]; i++) {
        round_trip(&k_rc6_vectors[i]);
    }
    for (size_t i = 0; i < sizeof k_sirc20_vectors / sizeof k_sirc20_vectors[0]; i++) {
        round_trip(&k_sirc20_vectors[i]);
    }
}

static void test_exact_text(void)
{
    ir_signal_t s = {.carrier_hz = 36000};
    s.frame = (ir_frame_t){.protocol = IR_PROTO_RC6, .address = 0x10, .command = 0x20, .toggle = 1};
    s.timings = (ir_timings_t){.n = 3, .us = {2666, 889, 444}};
    TEST_ASSERT_TRUE(ir_raw_format(&s, s_text, sizeof s_text) > 0);
    TEST_ASSERT_EQUAL_STRING("stark-ir 1\nprotocol RC6\naddress 0x0010\ncommand 0x0020\ntoggle 1\n"
                             "carrier 36000\ntimings 2666 889 444\n",
                             s_text);
    s.frame = (ir_frame_t){.protocol = IR_PROTO_NEC, .repeat = true};
    TEST_ASSERT_TRUE(ir_raw_format(&s, s_text, sizeof s_text) > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_text, "repeat 1\n"));
}

static void test_full_capture_and_small_buffers(void)
{
    ir_signal_t s = {.carrier_hz = 38000};
    s.timings.n = IR_MAX_DURATIONS;
    for (int i = 0; i < IR_MAX_DURATIONS; i++) {
        s.timings.us[i] = 65535;
    }
    size_t n = ir_raw_format(&s, s_text, sizeof s_text);
    TEST_ASSERT_TRUE(n > 0);
    ir_signal_t r;
    TEST_ASSERT_EQUAL(STARK_OK, ir_raw_parse(s_text, n, &r));
    TEST_ASSERT_EQUAL_UINT16(IR_MAX_DURATIONS, r.timings.n);
    TEST_ASSERT_EQUAL_INT(IR_PROTO_UNKNOWN, r.frame.protocol);
    char small[40];
    TEST_ASSERT_EQUAL_size_t(0, ir_raw_format(&s, small, sizeof small)); /* header too long */
    char mid[120];
    TEST_ASSERT_EQUAL_size_t(0, ir_raw_format(&s, mid, sizeof mid)); /* timings too long */
    s.timings.n = 1;
    char tight[96];
    size_t need = ir_raw_format(&s, tight, sizeof tight);
    TEST_ASSERT_TRUE(need > 0);
    TEST_ASSERT_EQUAL_size_t(0, ir_raw_format(&s, tight, need)); /* no room for "\n\0" */
    TEST_ASSERT_EQUAL_size_t(0, ir_raw_format(NULL, s_text, sizeof s_text));
    TEST_ASSERT_EQUAL_size_t(0, ir_raw_format(&s, NULL, 10));
    TEST_ASSERT_EQUAL_size_t(0, ir_raw_format(&s, s_text, 0));
    s.timings.n = IR_MAX_DURATIONS + 1;
    TEST_ASSERT_EQUAL_size_t(0, ir_raw_format(&s, s_text, sizeof s_text));
}

static void test_defaults_comments_and_unknown_keys(void)
{
    ir_signal_t r;
    TEST_ASSERT_EQUAL(STARK_OK, parse("# a capture\n\nstark-ir 1  # v1\nfoo bar\nprotocol NEC\n"
                                      "timings 2400 600 600\r\n",
                                      &r));
    TEST_ASSERT_EQUAL_UINT16(3, r.timings.n);
    TEST_ASSERT_EQUAL_UINT32(38000, r.carrier_hz); /* RAW default */
    /* a missing carrier takes the decoded protocol's default */
    ir_timings_t t;
    uint32_t c;
    ir_frame_t f = {.protocol = IR_PROTO_SIRC12, .address = 1, .command = 2};
    TEST_ASSERT_EQUAL(STARK_OK, ir_encode(&f, &t, &c));
    char text[400] = "stark-ir 0x1\ntimings";
    for (int i = 0; i < t.n; i++) {
        char num[8];
        snprintf(num, sizeof num, " %u", t.us[i]);
        strcat(text, num);
    }
    TEST_ASSERT_EQUAL(STARK_OK, parse(text, &r));
    TEST_ASSERT_EQUAL_INT(IR_PROTO_SIRC12, r.frame.protocol);
    TEST_ASSERT_EQUAL_UINT32(40000, r.carrier_hz);
    TEST_ASSERT_EQUAL(STARK_OK, parse("stark-ir 1\ncarrier 0x9C40\ntimings 0xFFFF 0Xa\n", &r));
    TEST_ASSERT_EQUAL_UINT32(40000, r.carrier_hz);
    TEST_ASSERT_EQUAL_UINT16(10, r.timings.us[1]);
}

static void test_malformed(void)
{
    static const char *const bad[] = {
        "",
        "timings 1 2 3\n",         /* no header */
        "stark-ir 2\ntimings 1\n", /* wrong version */
        "stark-ir\ntimings 1\n",   /* no version */
        "stark-ir x1\ntimings 1\n",
        "stark-ir 1\n",                /* no timings */
        "stark-ir 1\ntimings\n",       /* empty timings */
        "stark-ir 1\ntimings 0\n",     /* zero duration */
        "stark-ir 1\ntimings 65536\n", /* too long a duration */
        "stark-ir 1\ntimings 12a\n",   /* not a number */
        "stark-ir 1\ntimings 0x\n",    /* hex without digits */
        "stark-ir 1\ntimings 0xG1\n",
        "stark-ir 1\ncarrier 19999\ntimings 1\n",
        "stark-ir 1\ncarrier 60001\ntimings 1\n",
        "stark-ir 1\ncarrier\ntimings 1\n",
        "stark-ir 1\ncarrier fast\ntimings 1\n",
    };
    ir_signal_t r;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        TEST_ASSERT_EQUAL_MESSAGE(STARK_ERR_INVALID_ARG, parse(bad[i], &r), bad[i]);
    }
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_raw_parse(NULL, 4, &r));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_raw_parse("x", 1, NULL));
    /* 257 values */
    char text[IR_RAW_TEXT_MAX] = "stark-ir 1\ntimings";
    for (int i = 0; i <= IR_MAX_DURATIONS; i++) {
        strcat(text, " 5");
    }
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, parse(text, &r));
    /* an over-long line */
    static char longline[2200];
    memset(longline, ' ', sizeof longline - 1);
    memcpy(longline, "stark-ir 1\n", 11);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_raw_parse(longline, sizeof longline - 1, &r));
    /* a truncated buffer is read only as far as len */
    const char *full = "stark-ir 1\ntimings 9000 4500\n";
    TEST_ASSERT_EQUAL(STARK_OK, ir_raw_parse(full, strlen(full) - 4, &r)); /* "...9000 4" */
    TEST_ASSERT_EQUAL_UINT16(2, r.timings.n);
    TEST_ASSERT_EQUAL_UINT16(4, r.timings.us[1]);
}

static void test_fuzz(void)
{
    uint32_t x = 0xC0FFEE;
    static char buf[300];
    for (int i = 0; i < 10000; i++) {
        size_t len = 0;
        if (i % 2) { /* random bytes */
            x = x * 1664525u + 1013904223u;
            len = (x >> 8) % sizeof buf;
            for (size_t k = 0; k < len; k++) {
                x = x * 1664525u + 1013904223u;
                buf[k] = (char)(x >> 24);
            }
        } else { /* a mutated valid file */
            const char *base = "stark-ir 1\ncarrier 38000\ntimings 9000 4500 560 560 560\n";
            len = strlen(base);
            memcpy(buf, base, len);
            x = x * 1664525u + 1013904223u;
            buf[(x >> 8) % len] = (char)(x >> 24);
        }
        ir_signal_t r;
        (void)ir_raw_parse(buf, len, &r);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_every_vector);
    RUN_TEST(test_exact_text);
    RUN_TEST(test_full_capture_and_small_buffers);
    RUN_TEST(test_defaults_comments_and_unknown_keys);
    RUN_TEST(test_malformed);
    RUN_TEST(test_fuzz);
    return UNITY_END();
}
