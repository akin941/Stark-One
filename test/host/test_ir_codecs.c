/*
 * test_ir_codecs.c — RC5, RC6 mode 0 and SIRC 12/15/20 (TASKS.md
 * STARK-0302): every spec-derived vector (ADR-0019 §3), nominal encodings
 * equal to the generator's, exhaustive round trips, decoder order,
 * malformed input.
 */
#include <string.h>
#include "ir_codec.h"
#include "ir_rc5_vectors.h"
#include "ir_rc6_vectors.h"
#include "ir_sirc12_vectors.h"
#include "ir_sirc15_vectors.h"
#include "ir_sirc20_vectors.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void run_vectors(const ir_vector_t *vs, size_t count)
{
    TEST_ASSERT_TRUE(count >= 20);
    for (size_t i = 0; i < count; i++) {
        const ir_vector_t *v = &vs[i];
        ir_timings_t t = {.n = v->n};
        memcpy(t.us, v->timings, v->n * sizeof v->timings[0]);
        ir_frame_t f;
        TEST_ASSERT_EQUAL_INT_MESSAGE(v->expect, ir_decode(&t, &f), v->label);
        if (v->expect != IR_PROTO_UNKNOWN) {
            TEST_ASSERT_EQUAL_HEX16_MESSAGE(v->address, f.address, v->label);
            TEST_ASSERT_EQUAL_HEX16_MESSAGE(v->command, f.command, v->label);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(v->toggle, f.toggle, v->label);
        }
    }
}

#define RUN(table) run_vectors(table, sizeof table / sizeof table[0])

static void test_vectors(void)
{
    RUN(k_rc5_vectors);
    RUN(k_rc6_vectors);
    RUN(k_sirc12_vectors);
    RUN(k_sirc15_vectors);
    RUN(k_sirc20_vectors);
}

/* Nominal vector 0 of each table equals the firmware's own encoding. */
static void assert_encodes_like(const ir_vector_t *v, ir_protocol_t p, uint32_t carrier_want)
{
    ir_frame_t f = {
        .protocol = p, .address = v->address, .command = v->command, .toggle = v->toggle};
    ir_timings_t t;
    uint32_t carrier = 0;
    TEST_ASSERT_EQUAL(STARK_OK, ir_encode(&f, &t, &carrier));
    TEST_ASSERT_EQUAL_UINT32(carrier_want, carrier);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->n, t.n, v->label);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(v->timings, t.us, v->n);
}

static void test_nominal_encodings_match_the_generator(void)
{
    for (int i = 0; i < 6; i++) {
        assert_encodes_like(&k_rc5_vectors[i], IR_PROTO_RC5, 36000);
        assert_encodes_like(&k_rc6_vectors[i], IR_PROTO_RC6, 36000);
        assert_encodes_like(&k_sirc12_vectors[i], IR_PROTO_SIRC12, 40000);
        assert_encodes_like(&k_sirc15_vectors[i], IR_PROTO_SIRC15, 40000);
        assert_encodes_like(&k_sirc20_vectors[i], IR_PROTO_SIRC20, 40000);
    }
}

static void round_trip(ir_protocol_t p, uint16_t a, uint16_t c, uint8_t t)
{
    ir_frame_t f = {.protocol = p, .address = a, .command = c, .toggle = t};
    ir_frame_t g;
    ir_timings_t tm;
    uint32_t carrier;
    TEST_ASSERT_EQUAL(STARK_OK, ir_encode(&f, &tm, &carrier));
    TEST_ASSERT_EQUAL_INT(p, ir_decode(&tm, &g));
    TEST_ASSERT_TRUE(g.address == a && g.command == c && g.toggle == t);
}

static void test_round_trips_exhaustive(void)
{
    for (uint8_t t = 0; t < 2; t++) {
        for (uint16_t a = 0; a < 32; a++) {
            for (uint16_t c = 0; c < 128; c++) {
                round_trip(IR_PROTO_RC5, a, c, t);
            }
        }
        for (uint16_t a = 0; a < 256; a++) {
            for (uint16_t c = 0; c < 256; c++) {
                round_trip(IR_PROTO_RC6, a, c, t);
            }
        }
    }
    for (uint16_t c = 0; c < 128; c++) {
        for (uint16_t a = 0; a < 32; a++) {
            round_trip(IR_PROTO_SIRC12, a, c, 0);
        }
        for (uint16_t a = 0; a < 256; a++) {
            round_trip(IR_PROTO_SIRC15, a, c, 0);
        }
        for (uint32_t a = 0; a < 8192; a++) {
            round_trip(IR_PROTO_SIRC20, (uint16_t)a, c, 0);
        }
    }
}

static void test_encode_rejections(void)
{
    ir_timings_t t;
    uint32_t carrier;
    const ir_frame_t bad[] = {
        {.protocol = IR_PROTO_RC5, .address = 32},
        {.protocol = IR_PROTO_RC5, .command = 128},
        {.protocol = IR_PROTO_RC5, .toggle = 2},
        {.protocol = IR_PROTO_RC6, .address = 256},
        {.protocol = IR_PROTO_RC6, .command = 256},
        {.protocol = IR_PROTO_RC6, .toggle = 2},
        {.protocol = IR_PROTO_SIRC12, .address = 32},
        {.protocol = IR_PROTO_SIRC15, .address = 256},
        {.protocol = IR_PROTO_SIRC20, .address = 8192},
        {.protocol = IR_PROTO_SIRC12, .command = 128},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_encode(&bad[i], &t, &carrier));
    }
}

static void test_degenerate_input(void)
{
    ir_frame_t f;
    ir_timings_t t = {.n = 1, .us = {889}};
    TEST_ASSERT_EQUAL_INT(IR_PROTO_UNKNOWN, ir_decode(&t, &f));
    t = (ir_timings_t){.n = 2, .us = {2666, 889}}; /* an RC6 leader alone */
    TEST_ASSERT_EQUAL_INT(IR_PROTO_UNKNOWN, ir_decode(&t, &f));
    /* RC6 leader with a scaled estimate that is itself out of range */
    t = (ir_timings_t){.n = 4, .us = {5000, 1660, 900, 900}};
    TEST_ASSERT_EQUAL_INT(IR_PROTO_UNKNOWN, ir_decode(&t, &f));
    /* 64 alternating unit pulses overflow the bi-phase buffers */
    t.n = 80;
    for (int i = 0; i < 80; i++) {
        t.us[i] = 889;
    }
    TEST_ASSERT_EQUAL_INT(IR_PROTO_UNKNOWN, ir_decode(&t, &f));
    for (int i = 2; i < 80; i++) {
        t.us[i] = 444;
    }
    t.us[0] = 2666;
    t.us[1] = 889;
    TEST_ASSERT_EQUAL_INT(IR_PROTO_UNKNOWN, ir_decode(&t, &f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_vectors);
    RUN_TEST(test_nominal_encodings_match_the_generator);
    RUN_TEST(test_round_trips_exhaustive);
    RUN_TEST(test_encode_rejections);
    RUN_TEST(test_degenerate_input);
    return UNITY_END();
}
