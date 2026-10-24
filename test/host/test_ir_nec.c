/*
 * test_ir_nec.c — NEC and NEC-extended (TASKS.md STARK-0301): every
 * spec-derived vector (ADR-0019 §3), exact nominal encodings, exhaustive
 * (NEC) and sampled (NECX) round trips, malformed input.
 */
#include <stdio.h>
#include <string.h>
#include "ir_codec.h"
#include "ir_necx_vectors.h"
#include "ir_nec_vectors.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void load(ir_timings_t *t, const ir_vector_t *v)
{
    t->n = v->n;
    memcpy(t->us, v->timings, v->n * sizeof v->timings[0]);
}

static void run_vectors(const ir_vector_t *vs, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        const ir_vector_t *v = &vs[i];
        ir_timings_t t;
        ir_frame_t f;
        load(&t, v);
        ir_protocol_t p = ir_decode(&t, &f);
        TEST_ASSERT_EQUAL_INT_MESSAGE(v->expect, p, v->label);
        if (v->expect != IR_PROTO_UNKNOWN) {
            TEST_ASSERT_EQUAL_HEX16_MESSAGE(v->address, f.address, v->label);
            TEST_ASSERT_EQUAL_HEX16_MESSAGE(v->command, f.command, v->label);
            TEST_ASSERT_EQUAL_MESSAGE(v->repeat, f.repeat, v->label);
        } else {
            TEST_ASSERT_EQUAL_INT_MESSAGE(IR_PROTO_UNKNOWN, f.protocol, v->label);
        }
    }
}

static void test_nec_vectors(void)
{
    TEST_ASSERT_TRUE(sizeof k_nec_vectors / sizeof k_nec_vectors[0] >= 20);
    run_vectors(k_nec_vectors, sizeof k_nec_vectors / sizeof k_nec_vectors[0]);
}

static void test_necx_vectors(void)
{
    TEST_ASSERT_TRUE(sizeof k_necx_vectors / sizeof k_necx_vectors[0] >= 20);
    run_vectors(k_necx_vectors, sizeof k_necx_vectors / sizeof k_necx_vectors[0]);
}

/* The firmware's encoder reproduces the generator's nominal timings exactly. */
static void test_nominal_encodings_match_the_generator(void)
{
    ir_timings_t t;
    uint32_t carrier = 0;
    ir_frame_t f = {.protocol = IR_PROTO_NEC, .address = 0x10, .command = 0x12};
    TEST_ASSERT_EQUAL(STARK_OK, ir_encode(&f, &t, &carrier));
    TEST_ASSERT_EQUAL_UINT32(38000, carrier);
    const ir_vector_t *v = &k_nec_vectors[4]; /* nominal 10/12 */
    TEST_ASSERT_EQUAL_STRING("nominal 10/12", v->label);
    TEST_ASSERT_EQUAL_UINT16(v->n, t.n);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(v->timings, t.us, v->n);
    f = (ir_frame_t){.protocol = IR_PROTO_NECX, .address = 0x1234, .command = 0x56};
    TEST_ASSERT_EQUAL(STARK_OK, ir_encode(&f, &t, &carrier));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(k_necx_vectors[0].timings, t.us, k_necx_vectors[0].n);
    f = (ir_frame_t){.protocol = IR_PROTO_NEC, .repeat = true};
    TEST_ASSERT_EQUAL(STARK_OK, ir_encode(&f, &t, &carrier));
    TEST_ASSERT_EQUAL_UINT16(3, t.n);
}

static void test_nec_round_trip_exhaustive(void)
{
    for (uint32_t a = 0; a <= 0xFF; a++) {
        for (uint32_t c = 0; c <= 0xFF; c++) {
            ir_frame_t f = {
                .protocol = IR_PROTO_NEC, .address = (uint16_t)a, .command = (uint16_t)c};
            ir_frame_t g;
            ir_timings_t t;
            uint32_t carrier;
            TEST_ASSERT_EQUAL(STARK_OK, ir_encode(&f, &t, &carrier));
            TEST_ASSERT_EQUAL_INT(IR_PROTO_NEC, ir_decode(&t, &g));
            TEST_ASSERT_TRUE(g.address == a && g.command == c && !g.repeat);
        }
    }
}

static void test_necx_round_trip_sampled(void)
{
    uint32_t x = 0x1234567u;
    for (int i = 0; i < 4096; i++) {
        x = x * 1103515245u + 12345u;
        uint16_t a = (uint16_t)(x >> 8);
        uint16_t c = (uint16_t)((x >> 24) & 0xFF);
        ir_frame_t f = {.protocol = IR_PROTO_NECX, .address = a, .command = c};
        ir_frame_t g;
        ir_timings_t t;
        uint32_t carrier;
        TEST_ASSERT_EQUAL(STARK_OK, ir_encode(&f, &t, &carrier));
        ir_protocol_t want = ((a & 0xFF) ^ (a >> 8)) == 0xFF ? IR_PROTO_NEC : IR_PROTO_NECX;
        TEST_ASSERT_EQUAL_INT(want, ir_decode(&t, &g));
        TEST_ASSERT_EQUAL_HEX16(want == IR_PROTO_NEC ? (a & 0xFF) : a, g.address);
        TEST_ASSERT_EQUAL_HEX16(c, g.command);
    }
}

static void test_encode_rejections(void)
{
    ir_timings_t t;
    uint32_t carrier;
    ir_frame_t f = {.protocol = IR_PROTO_NEC, .address = 0x100, .command = 1};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_encode(&f, &t, &carrier));
    f = (ir_frame_t){.protocol = IR_PROTO_NECX, .address = 1, .command = 0x100};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_encode(&f, &t, &carrier));
    f = (ir_frame_t){.protocol = IR_PROTO_UNKNOWN};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_encode(&f, &t, &carrier));
    f = (ir_frame_t){.protocol = IR_PROTO_COUNT};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_encode(&f, &t, &carrier));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_encode(NULL, &t, &carrier));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_encode(&f, NULL, &carrier));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_encode(&f, &t, NULL));
}

static void test_decode_degenerate_input(void)
{
    ir_timings_t t = {0};
    ir_frame_t f;
    TEST_ASSERT_EQUAL_INT(IR_PROTO_UNKNOWN, ir_decode(&t, &f)); /* n = 0 */
    t.n = IR_MAX_DURATIONS;
    for (int i = 0; i < IR_MAX_DURATIONS; i++) {
        t.us[i] = (uint16_t)(i % 2 ? 65535 : 0);
    }
    TEST_ASSERT_EQUAL_INT(IR_PROTO_UNKNOWN, ir_decode(&t, &f));
    TEST_ASSERT_EQUAL_INT(IR_PROTO_UNKNOWN, ir_decode(NULL, &f));
    TEST_ASSERT_EQUAL_INT(IR_PROTO_UNKNOWN, ir_decode(&t, NULL));
    /* a repeat-length frame that is not a repeat code */
    t.n = 3;
    t.us[0] = 9000;
    t.us[1] = 4500;
    t.us[2] = 560;
    TEST_ASSERT_EQUAL_INT(IR_PROTO_UNKNOWN, ir_decode(&t, &f));
}

static void test_protocol_names(void)
{
    TEST_ASSERT_EQUAL_STRING("RAW", ir_protocol_name(IR_PROTO_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("NEC", ir_protocol_name(IR_PROTO_NEC));
    TEST_ASSERT_EQUAL_STRING("NECX", ir_protocol_name(IR_PROTO_NECX));
    TEST_ASSERT_EQUAL_STRING("SIRC20", ir_protocol_name(IR_PROTO_SIRC20));
    TEST_ASSERT_EQUAL_STRING("?", ir_protocol_name(IR_PROTO_COUNT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nec_vectors);
    RUN_TEST(test_necx_vectors);
    RUN_TEST(test_nominal_encodings_match_the_generator);
    RUN_TEST(test_nec_round_trip_exhaustive);
    RUN_TEST(test_necx_round_trip_sampled);
    RUN_TEST(test_encode_rejections);
    RUN_TEST(test_decode_degenerate_input);
    RUN_TEST(test_protocol_names);
    return UNITY_END();
}
