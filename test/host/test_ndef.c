/*
 * test_ndef.c — NDEF parse/encode and the URI/Text helpers (TASKS.md
 * STARK-0401) against spec-derived vectors (scripts/gen_nfc_vectors.py,
 * ADR-0020 §4), plus truncation at every byte and seeded fuzzing.
 */
#include <stdio.h>
#include <string.h>
#include "ndef.h"
#include "nfc_ndef_vectors.h"
#include "nfc_rtd_vectors.h"
#include "unity.h"

#define N_NDEF (sizeof k_ndef_vectors / sizeof k_ndef_vectors[0])
#define N_RTD  (sizeof k_rtd_vectors / sizeof k_rtd_vectors[0])

void setUp(void)
{}
void tearDown(void)
{}

static void assert_record(const char *label, const ndef_vec_record_t *want,
                          const ndef_record_t *got)
{
    TEST_ASSERT_EQUAL_MESSAGE(want->tnf, got->tnf, label);
    TEST_ASSERT_EQUAL_MESSAGE(want->type_len, got->type_len, label);
    TEST_ASSERT_EQUAL_MESSAGE(want->id_len, got->id_len, label);
    TEST_ASSERT_EQUAL_MESSAGE(want->payload_len, got->payload_len, label);
    /* Unity rejects a zero-length comparison; empty fields are covered by the lengths */
    if (want->type_len > 0) {
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want->type, got->type, want->type_len, label);
    }
    if (want->id_len > 0) {
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want->id, got->id, want->id_len, label);
    }
    if (want->payload_len > 0) {
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want->payload, got->payload, want->payload_len, label);
    }
}

static void test_vectors(void)
{
    static uint8_t out[1024];
    unsigned valid = 0;
    for (size_t i = 0; i < N_NDEF; i++) {
        const ndef_vector_t *v = &k_ndef_vectors[i];
        ndef_message_t m;
        TEST_ASSERT_EQUAL_MESSAGE(v->expect, ndef_parse(v->msg, v->len, &m), v->label);
        if (v->expect != STARK_OK) {
            continue;
        }
        valid++;
        TEST_ASSERT_EQUAL_MESSAGE(v->count, m.count, v->label);
        for (uint8_t r = 0; r < m.count; r++) {
            assert_record(v->label, &v->recs[r], &m.rec[r]);
        }
        size_t n = ndef_encode(m.rec, m.count, out, sizeof out);
        if (v->canonical) {
            TEST_ASSERT_EQUAL_MESSAGE(v->len, n, v->label);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(v->msg, out, n, v->label);
        } else {
            ndef_message_t again;
            TEST_ASSERT_EQUAL_MESSAGE(STARK_OK, ndef_parse(out, n, &again), v->label);
            TEST_ASSERT_EQUAL_MESSAGE(m.count, again.count, v->label);
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL(40, valid);
}

static void test_rtd_vectors(void)
{
    for (size_t i = 0; i < N_RTD; i++) {
        const ndef_rtd_vector_t *v = &k_rtd_vectors[i];
        ndef_message_t m;
        TEST_ASSERT_EQUAL_MESSAGE(STARK_OK, ndef_parse(v->msg, v->len, &m), v->label);
        char value[128];
        char lang[64];
        char line[96];
        stark_err_t err = v->kind == 'U'
                              ? ndef_uri_get(&m.rec[0], value, sizeof value)
                              : ndef_text_get(&m.rec[0], lang, sizeof lang, value, sizeof value);
        TEST_ASSERT_EQUAL_MESSAGE(v->expect, err, v->label);
        if (err == STARK_OK) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(v->value, value, v->label);
            if (v->kind == 'T') {
                TEST_ASSERT_EQUAL_STRING_MESSAGE(v->lang, lang, v->label);
            }
        }
        ndef_describe(&m.rec[0], line, sizeof line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(v->line, line, v->label);
    }
}

/* Every prefix of a valid multi-record message is malformed, never a crash. */
static void test_truncation_at_every_byte(void)
{
    for (size_t i = 0; i < N_NDEF; i++) {
        const ndef_vector_t *v = &k_ndef_vectors[i];
        if (v->expect != STARK_OK || v->count < 3) {
            continue;
        }
        for (uint16_t cut = 0; cut < v->len; cut++) {
            ndef_message_t m;
            TEST_ASSERT_NOT_EQUAL_MESSAGE(STARK_OK, ndef_parse(v->msg, cut, &m), v->label);
        }
    }
}

static uint32_t s_rng = 0x5EED0401u;
static uint32_t rnd(void)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return s_rng >> 8;
}

/* Seeded mutations of every valid vector: each parses or fails cleanly, and
 * whatever parses re-encodes and describes within its buffers (ASan/UBSan). */
static void test_fuzz(void)
{
    static uint8_t buf[1024];
    static uint8_t out[1100];
    unsigned parsed = 0;
    for (unsigned round = 0; round < 10000; round++) {
        const ndef_vector_t *v = &k_ndef_vectors[rnd() % N_NDEF];
        if (v->len == 0 || v->len > sizeof buf) {
            continue;
        }
        memcpy(buf, v->msg, v->len);
        size_t len = v->len;
        unsigned flips = 1 + rnd() % 4;
        for (unsigned f = 0; f < flips; f++) {
            buf[rnd() % len] = (uint8_t)rnd();
        }
        if (rnd() % 4 == 0) {
            len = rnd() % (len + 1);
        }
        ndef_message_t m;
        if (ndef_parse(buf, len, &m) != STARK_OK) {
            continue;
        }
        parsed++;
        char line[40];
        for (uint8_t r = 0; r < m.count; r++) {
            ndef_describe(&m.rec[r], line, sizeof line);
            TEST_ASSERT_TRUE(strlen(line) < sizeof line);
        }
        (void)ndef_encode(m.rec, m.count, out, sizeof out);
    }
    TEST_ASSERT_GREATER_THAN(0, parsed);
}

static void test_make_and_get(void)
{
    uint8_t payload[64];
    ndef_record_t r;
    char out[64];
    char lang[8];
    TEST_ASSERT_EQUAL(STARK_OK,
                      ndef_uri_make("https://example.com/stark", payload, sizeof payload, &r));
    TEST_ASSERT_EQUAL_HEX8(0x04, payload[0]); /* the longest prefix, not "http" */
    TEST_ASSERT_EQUAL(STARK_OK, ndef_uri_get(&r, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("https://example.com/stark", out);
    TEST_ASSERT_EQUAL(STARK_OK,
                      ndef_uri_make("https://www.example.com", payload, sizeof payload, &r));
    TEST_ASSERT_EQUAL_HEX8(0x02, payload[0]);
    TEST_ASSERT_EQUAL(STARK_OK, ndef_uri_make("stark:lab", payload, sizeof payload, &r));
    TEST_ASSERT_EQUAL_HEX8(0x00, payload[0]);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ndef_uri_make("", payload, sizeof payload, &r));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ndef_uri_make(NULL, payload, sizeof payload, &r));
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, ndef_uri_make("https://example.com", payload, 4, &r));
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, ndef_uri_get(&r, out, 5));

    TEST_ASSERT_EQUAL(STARK_OK, ndef_text_make("en", "lab", payload, sizeof payload, &r));
    TEST_ASSERT_EQUAL(STARK_OK, ndef_text_get(&r, lang, sizeof lang, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("en", lang);
    TEST_ASSERT_EQUAL_STRING("lab", out);
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, ndef_text_get(&r, lang, 2, out, sizeof out));
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, ndef_text_get(&r, lang, sizeof lang, out, 3));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ndef_text_make("", "x", payload, sizeof payload, &r));
    char long_lang[70];
    memset(long_lang, 'a', 64);
    long_lang[64] = '\0';
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      ndef_text_make(long_lang, "x", payload, sizeof payload, &r));
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, ndef_text_make("en", "too long", payload, 5, &r));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      ndef_text_make(NULL, "x", payload, sizeof payload, &r));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      ndef_text_get(NULL, lang, sizeof lang, out, sizeof out));

    /* a URI record and a Text record encode into one message that parses back */
    uint8_t p1[32];
    uint8_t p2[32];
    ndef_record_t recs[2];
    TEST_ASSERT_EQUAL(STARK_OK, ndef_uri_make("https://example.org/lab", p1, sizeof p1, &recs[0]));
    TEST_ASSERT_EQUAL(STARK_OK, ndef_text_make("en", "STARK ONE lab", p2, sizeof p2, &recs[1]));
    uint8_t msg[96];
    size_t n = ndef_encode(recs, 2, msg, sizeof msg);
    TEST_ASSERT_GREATER_THAN(0, n);
    ndef_message_t m;
    TEST_ASSERT_EQUAL(STARK_OK, ndef_parse(msg, n, &m));
    TEST_ASSERT_EQUAL(2, m.count);
    TEST_ASSERT_EQUAL(STARK_OK, ndef_uri_get(&m.rec[0], out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("https://example.org/lab", out);
}

static void test_encode_rejects(void)
{
    uint8_t out[16];
    static const uint8_t type[] = {'a'};
    ndef_record_t r = {.tnf = NDEF_TNF_MIME, .type = type, .type_len = 1};
    TEST_ASSERT_EQUAL(0, ndef_encode(NULL, 1, out, sizeof out));
    TEST_ASSERT_EQUAL(0, ndef_encode(&r, 0, out, sizeof out));
    TEST_ASSERT_EQUAL(0, ndef_encode(&r, NDEF_MAX_RECORDS + 1, out, sizeof out));
    TEST_ASSERT_EQUAL(4, ndef_encode(&r, 1, out, sizeof out));
    TEST_ASSERT_EQUAL(0, ndef_encode(&r, 1, out, 3)); /* too small */
    r.tnf = NDEF_TNF_UNCHANGED;
    TEST_ASSERT_EQUAL(0, ndef_encode(&r, 1, out, sizeof out));
    r.tnf = NDEF_TNF_EMPTY; /* with a type */
    TEST_ASSERT_EQUAL(0, ndef_encode(&r, 1, out, sizeof out));
    r = (ndef_record_t){
        .tnf = NDEF_TNF_MIME, .type = type, .type_len = 1, .payload_len = NFC_NDEF_MAX + 1};
    TEST_ASSERT_EQUAL(0, ndef_encode(&r, 1, out, sizeof out));
    ndef_message_t m;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ndef_parse(NULL, 1, &m));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ndef_parse(out, 1, NULL));
}

static void test_describe_truncates(void)
{
    uint8_t payload[200];
    ndef_record_t r;
    char line[16];
    TEST_ASSERT_EQUAL(STARK_OK, ndef_uri_make("https://example.com/a/very/long/path", payload,
                                              sizeof payload, &r));
    ndef_describe(&r, line, sizeof line);
    TEST_ASSERT_EQUAL_STRING("URI https://...", line);
    TEST_ASSERT_EQUAL(15, strlen(line));
    /* a multi-byte character is never cut in half */
    TEST_ASSERT_EQUAL(STARK_OK, ndef_text_make("tr", "aaççç", payload, sizeof payload, &r));
    ndef_describe(&r, line, sizeof line);
    TEST_ASSERT_EQUAL_STRING("Text tr: aa...", line);
    char tiny[3];
    ndef_describe(&r, tiny, sizeof tiny);
    TEST_ASSERT_EQUAL_STRING("Te", tiny);
    ndef_describe(NULL, line, sizeof line);
    TEST_ASSERT_EQUAL_STRING("", line);
    ndef_describe(&r, NULL, 0); /* nothing to write into */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_vectors);
    RUN_TEST(test_rtd_vectors);
    RUN_TEST(test_truncation_at_every_byte);
    RUN_TEST(test_fuzz);
    RUN_TEST(test_make_and_get);
    RUN_TEST(test_encode_rejects);
    RUN_TEST(test_describe_truncates);
    return UNITY_END();
}
