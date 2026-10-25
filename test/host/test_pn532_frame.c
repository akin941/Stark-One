/*
 * test_pn532_frame.c — PN532 frame build/parse (TASKS.md STARK-0402)
 * against spec-derived vectors (UM0701-02 §6.2, scripts/gen_nfc_vectors.py).
 */
#include <string.h>
#include "nfc_pn532_vectors.h"
#include "pn532_frame.h"
#include "unity.h"

#define N_BUILD (sizeof k_pn532_build / sizeof k_pn532_build[0])
#define N_PARSE (sizeof k_pn532_parse / sizeof k_pn532_parse[0])

void setUp(void)
{}
void tearDown(void)
{}

static void test_build_vectors(void)
{
    uint8_t out[300];
    for (size_t i = 0; i < N_BUILD; i++) {
        const pn532_build_vector_t *v = &k_pn532_build[i];
        size_t n = pn532_frame_build(v->cmd, v->params, v->n, out, sizeof out);
        TEST_ASSERT_EQUAL_MESSAGE(v->len, n, v->label);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(v->frame, out, n, v->label);
    }
}

static void test_parse_vectors(void)
{
    for (size_t i = 0; i < N_PARSE; i++) {
        const pn532_parse_vector_t *v = &k_pn532_parse[i];
        pn532_rx_t kind;
        const uint8_t *data;
        size_t dlen;
        TEST_ASSERT_EQUAL_MESSAGE(
            v->expect, pn532_frame_parse(v->frame, v->len, &kind, &data, &dlen), v->label);
        if (v->expect != STARK_OK) {
            continue;
        }
        TEST_ASSERT_EQUAL_MESSAGE(v->kind, kind, v->label);
        TEST_ASSERT_EQUAL_MESSAGE(v->dlen, dlen, v->label);
        if (v->dlen > 0) {
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(v->data, data, dlen, v->label);
        }
    }
}

/* Any single corrupted byte of a valid response is rejected. */
static void test_every_byte_corrupted(void)
{
    static const uint8_t fw[] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03,
                                 0x32, 0x01, 0x06, 0x07, 0xE8, 0x00};
    for (size_t i = 0; i < sizeof fw; i++) {
        for (unsigned bit = 0; bit < 8; bit++) {
            uint8_t f[sizeof fw];
            memcpy(f, fw, sizeof fw);
            f[i] = (uint8_t)(f[i] ^ (1u << bit));
            pn532_rx_t kind;
            const uint8_t *data;
            size_t dlen;
            stark_err_t err = pn532_frame_parse(f, sizeof f, &kind, &data, &dlen);
            TEST_ASSERT_FALSE(err == STARK_OK && kind == PN532_RX_DATA && dlen == 6 &&
                              memcmp(data, fw + 5, 6) == 0);
        }
    }
}

static void test_round_trip_every_length(void)
{
    uint8_t params[252];
    uint8_t out[300];
    for (size_t i = 0; i < sizeof params; i++) {
        params[i] = (uint8_t)(i * 37u);
    }
    for (size_t n = 0; n <= sizeof params; n++) {
        size_t len = pn532_frame_build(0x40, params, n, out, sizeof out);
        TEST_ASSERT_EQUAL(n + 9, len);
        pn532_rx_t kind;
        const uint8_t *data;
        size_t dlen;
        TEST_ASSERT_EQUAL(STARK_OK, pn532_frame_parse(out, len, &kind, &data, &dlen));
        TEST_ASSERT_EQUAL(PN532_RX_DATA, kind);
        TEST_ASSERT_EQUAL(n + 2, dlen);
        TEST_ASSERT_EQUAL_HEX8(PN532_TFI_HOST, data[0]);
        TEST_ASSERT_EQUAL_HEX8(0x40, data[1]);
        if (n > 0) {
            TEST_ASSERT_EQUAL_MEMORY(params, data + 2, n);
        }
    }
}

static void test_build_rejects(void)
{
    uint8_t out[300];
    uint8_t params[253] = {0};
    TEST_ASSERT_EQUAL(0, pn532_frame_build(0x40, params, 253, out, sizeof out)); /* too long */
    TEST_ASSERT_EQUAL(0, pn532_frame_build(0x02, NULL, 0, out, 8));              /* cap */
    TEST_ASSERT_EQUAL(9, pn532_frame_build(0x02, NULL, 0, out, 9));
    TEST_ASSERT_EQUAL(0, pn532_frame_build(0x40, NULL, 1, out, sizeof out));
    TEST_ASSERT_EQUAL(0, pn532_frame_build(0x02, NULL, 0, NULL, 9));
    pn532_rx_t kind;
    const uint8_t *data;
    size_t dlen;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, pn532_frame_parse(NULL, 6, &kind, &data, &dlen));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, pn532_frame_parse(out, 9, NULL, &data, &dlen));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_vectors);
    RUN_TEST(test_parse_vectors);
    RUN_TEST(test_every_byte_corrupted);
    RUN_TEST(test_round_trip_every_length);
    RUN_TEST(test_build_rejects);
    return UNITY_END();
}
