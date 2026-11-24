/*
 * test_lab_uart.c — the UART tool's data rules (components/stark_lab/lab_uart.c, TASKS.md
 * STARK-0705, ADR-0023 §7) against the vectors of scripts/gen_lab_vectors.py, an independent Python
 * implementation: wire times and the loopback window, the byte ring and its loss count, the
 * judgement of a loopback, hex and ASCII rows, the send file's grammar, the log names and CRC-32,
 * and the fake peer's scenes. Proves the rules and their bounds, never a byte on a wire
 * (ROADMAP V1 exit 13).
 */
#include <stdio.h>
#include <string.h>
#include "fake_lab.h"
#include "fake_lab_uart_vectors.h"
#include "lab_uart.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void test_the_lists(void)
{
    static const uint32_t k_want[] = {1200,  2400,   4800,   9600,   19200, 38400,
                                      57600, 115200, 230400, 460800, 921600};
    TEST_ASSERT_EQUAL_UINT(11, STARK_LAB_UART_BAUDS);
    for (size_t i = 0; i < 11; i++) {
        TEST_ASSERT_EQUAL_UINT32(k_want[i], stark_lab_uart_baud(i));
        TEST_ASSERT_EQUAL_UINT(i, lab_uart_baud_index(k_want[i]));
    }
    TEST_ASSERT_EQUAL_UINT32(0, stark_lab_uart_baud(11));
    TEST_ASSERT_EQUAL_UINT(SIZE_MAX, lab_uart_baud_index(14400));
    static const char *const k_fmt[] = {"8N1", "8E1", "8O1", "8N2", "7E1"};
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_STRING(k_fmt[i], stark_lab_uart_fmt_name(i));
        TEST_ASSERT_EQUAL_STRING(k_fmt[i], lab_uart_fmt(i)->name);
        /* the fake peer's own table gives the same bits per character */
        TEST_ASSERT_EQUAL_UINT32(fake_lab_uart_bits((uint8_t)i), lab_uart_bits_per_char(i));
    }
    TEST_ASSERT_NULL(stark_lab_uart_fmt_name(5));
    TEST_ASSERT_NULL(lab_uart_fmt(5));
    TEST_ASSERT_EQUAL_UINT32(10, lab_uart_bits_per_char(0)); /* 8N1 */
    TEST_ASSERT_EQUAL_UINT32(11, lab_uart_bits_per_char(1)); /* 8E1 */
    TEST_ASSERT_EQUAL_UINT32(11, lab_uart_bits_per_char(3)); /* 8N2 */
    TEST_ASSERT_EQUAL_UINT32(10, lab_uart_bits_per_char(4)); /* 7E1 */
    TEST_ASSERT_EQUAL_UINT32(0, lab_uart_bits_per_char(5));
    TEST_ASSERT_EQUAL_UINT32(0, fake_lab_uart_bits(5));
}

static void test_wire_time_and_the_loopback_window(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_UART_WIRE_VECS >= 50u);
    for (size_t i = 0; i < FAKE_LAB_UART_WIRE_VECS; i++) {
        const fake_lab_uart_wire_vec_t *v = &fake_lab_uart_wire_vecs[i];
        TEST_ASSERT_EQUAL_UINT32(v->ms, lab_uart_wire_ms(v->n, v->baud, v->fmt));
        TEST_ASSERT_EQUAL_UINT32(v->window, lab_uart_loop_window_ms(v->baud, v->fmt));
    }
    TEST_ASSERT_EQUAL_UINT32(0, lab_uart_wire_ms(10, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(0, lab_uart_wire_ms(10, 9600, 9));
    TEST_ASSERT_EQUAL_UINT32(0, lab_uart_wire_ms(0, 9600, 0));
    /* the slowest listen of all: 64 characters of 8N2 at 1 200 baud, twice, and 20 ms */
    TEST_ASSERT_EQUAL_UINT32(587, lab_uart_wire_ms(64, 1200, 3));
    TEST_ASSERT_TRUE(lab_uart_loop_window_ms(1200, 3) < STARK_LAB_UART_TIMEOUT_MS_MAX);
}

static void test_the_ring_follows_the_vectors(void)
{
    static uint8_t s_store[4096];
    for (size_t i = 0; i < FAKE_LAB_UART_RING_VECS; i++) {
        const fake_lab_uart_ring_vec_t *v = &fake_lab_uart_ring_vecs[i];
        char msg[48];
        snprintf(msg, sizeof msg, "ring vector %u", (unsigned)i);
        lab_uart_ring_t r;
        lab_uart_ring_init(&r, s_store, v->cap);
        uint8_t piece[256];
        uint32_t k = 0;
        while (k < v->total) {
            size_t n = v->total - k < v->chunk ? v->total - k : v->chunk;
            for (size_t j = 0; j < n; j++) {
                piece[j] = (uint8_t)(k + j);
            }
            lab_uart_ring_push(&r, piece, n);
            k += (uint32_t)n;
        }
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->count, r.count, msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->lost, r.lost, msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->total, r.total, msg);
        static uint8_t all[4096];
        size_t got = lab_uart_ring_peek(&r, 0, all, sizeof all);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->count, got, msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->crc, stark_lab_uart_crc32(0, all, got), msg);
        size_t pn = lab_uart_ring_peek(&r, v->skip, all, v->take);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->peek_n, pn, msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->peek_crc, stark_lab_uart_crc32(0, all, pn), msg);
    }
}

static void test_the_ring_never_loses_a_byte_without_counting_it(void)
{
    uint8_t store[8];
    lab_uart_ring_t r;
    lab_uart_ring_init(&r, store, sizeof store);
    uint8_t d[3] = {1, 2, 3};
    uint32_t pushed = 0;
    for (int round = 0; round < 50; round++) {
        lab_uart_ring_push(&r, d, sizeof d);
        pushed += 3;
        TEST_ASSERT_EQUAL_UINT32(pushed, r.total);
        TEST_ASSERT_EQUAL_UINT32(pushed, (uint32_t)r.count + r.lost);
        TEST_ASSERT_TRUE(r.count <= sizeof store);
    }
    lab_uart_ring_clear(&r);
    TEST_ASSERT_EQUAL_UINT(0, r.count);
    TEST_ASSERT_EQUAL_UINT32(0, r.lost);
    TEST_ASSERT_EQUAL_UINT32(0, r.total);
    uint8_t out[4];
    TEST_ASSERT_EQUAL_UINT(0, lab_uart_ring_peek(&r, 0, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT(0, lab_uart_ring_peek(&r, 5, out, sizeof out));
    lab_uart_ring_push(&r, d, 0); /* nothing */
    TEST_ASSERT_EQUAL_UINT(0, r.count);
}

static void test_the_loopback_judgement_follows_the_vectors(void)
{
    uint8_t want[STARK_LAB_UART_LOOP_LEN];
    lab_uart_loop_pattern(want);
    for (size_t i = 0; i < STARK_LAB_UART_LOOP_LEN; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, want[i]);
    }
    for (size_t i = 0; i < FAKE_LAB_UART_LOOP_VECS; i++) {
        const fake_lab_uart_loop_vec_t *v = &fake_lab_uart_loop_vecs[i];
        stark_lab_loop_t r;
        lab_uart_loop_check(v->rx, v->n_rx, v->sent, v->framing, v->parity, v->overrun, &r);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->status, (unsigned)r.status, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->received, r.received, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->sent, r.sent, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->errors, r.errors, v->name);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(v->first, r.first_mismatch, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->framing, r.framing, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->parity, r.parity, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->overrun, r.overrun, v->name);
        /* AC 1: a mismatch is never reported as success */
        if (r.status == STARK_LAB_LOOP_OK) {
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.errors, v->name);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(r.sent, r.received, v->name);
            TEST_ASSERT_EQUAL_INT32_MESSAGE(-1, r.first_mismatch, v->name);
        }
    }
}

static void test_the_loopback_text(void)
{
    char buf[48];
    stark_lab_loop_t r = {
        .status = STARK_LAB_LOOP_OK, .sent = 64, .received = 64, .first_mismatch = -1};
    stark_lab_loop_text(&r, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("64/64 ok", buf);
    stark_lab_loop_log_text(&r, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("64/64 ok", buf);
    r = (stark_lab_loop_t){.status = STARK_LAB_LOOP_MISMATCH,
                           .sent = 64,
                           .received = 60,
                           .errors = 5,
                           .first_mismatch = 12};
    stark_lab_loop_text(&r, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("60/64 differs at 12", buf);
    stark_lab_loop_log_text(&r, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("60/64 mismatch first 12", buf);
    r = (stark_lab_loop_t){.status = STARK_LAB_LOOP_LINE_ERRORS,
                           .sent = 64,
                           .received = 64,
                           .errors = 2,
                           .first_mismatch = -1};
    stark_lab_loop_text(&r, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("64/64 line errors 2", buf);
    stark_lab_loop_log_text(&r, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("64/64 errors 2", buf);
    r = (stark_lab_loop_t){.status = STARK_LAB_LOOP_NO_DATA, .sent = 64};
    stark_lab_loop_text(&r, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("No data back", buf);
    stark_lab_loop_log_text(&r, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("0/64 no data", buf);
}

static void test_hex_and_ascii_rows_follow_the_vectors(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_UART_ROW_VECS >= 16u);
    for (size_t i = 0; i < FAKE_LAB_UART_ROW_VECS; i++) {
        const fake_lab_uart_row_vec_t *v = &fake_lab_uart_row_vecs[i];
        char hex[80];
        char asc[80];
        char msg[32];
        snprintf(msg, sizeof msg, "row vector %u", (unsigned)i);
        TEST_ASSERT_TRUE(v->sz <= sizeof hex);
        size_t hn = stark_lab_uart_hex_row(v->data, v->n, hex, v->sz);
        size_t an = stark_lab_uart_ascii_row(v->data, v->n, asc, v->sz);
        if (v->sz > 0) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(v->hex, hex, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(v->ascii, asc, msg);
        }
        TEST_ASSERT_EQUAL_UINT_MESSAGE(strlen(v->hex), hn, msg);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(strlen(v->ascii), an, msg);
    }
}

static void test_the_send_file_follows_the_vectors(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_UART_FILE_VECS >= 40u);
    for (size_t i = 0; i < FAKE_LAB_UART_FILE_VECS; i++) {
        const fake_lab_uart_file_vec_t *v = &fake_lab_uart_file_vecs[i];
        uint8_t out[STARK_LAB_UART_SEND_MAX + 1];
        size_t count = 99;
        stark_lab_file_t st = stark_lab_uart_parse_send(v->text, v->n, out, &count);
        char msg[32];
        snprintf(msg, sizeof msg, "file vector %u", (unsigned)i);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->status, (unsigned)st, msg);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->n_out, count, msg);
        if (v->n_out > 0) {
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(v->out, out, v->n_out, msg);
        }
        TEST_ASSERT_TRUE_MESSAGE(count <= STARK_LAB_UART_SEND_MAX, msg);
    }
    TEST_ASSERT_EQUAL_STRING("ok", stark_lab_uart_file_text(STARK_LAB_FILE_OK));
    TEST_ASSERT_EQUAL_STRING("empty", stark_lab_uart_file_text(STARK_LAB_FILE_EMPTY));
    TEST_ASSERT_EQUAL_STRING("too long", stark_lab_uart_file_text(STARK_LAB_FILE_TOO_BIG));
    TEST_ASSERT_EQUAL_STRING("bad bytes", stark_lab_uart_file_text(STARK_LAB_FILE_BAD));
}

static void test_log_names_follow_the_vectors(void)
{
    for (size_t i = 0; i < FAKE_LAB_UART_PARSE_VECS; i++) {
        const fake_lab_uart_parse_vec_t *v = &fake_lab_uart_parse_vecs[i];
        uint32_t number = 77;
        TEST_ASSERT_EQUAL_UINT_MESSAGE(
            v->ok, stark_lab_uart_log_name_parse(v->name, &number) ? 1u : 0u, v->name);
        if (v->ok) {
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->number, number, v->name);
        } else {
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(77, number, v->name);
        }
    }
    TEST_ASSERT_FALSE(stark_lab_uart_log_name_parse(NULL, NULL));
    TEST_ASSERT_TRUE(stark_lab_uart_log_name_parse("uart_0042.log", NULL));
    for (size_t i = 0; i < FAKE_LAB_UART_NEXT_VECS; i++) {
        const fake_lab_uart_next_vec_t *v = &fake_lab_uart_next_vecs[i];
        char name[14] = "";
        stark_err_t err = stark_lab_uart_log_name_next(v->highest, name, sizeof name);
        if (v->no_space) {
            TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, err);
        } else {
            TEST_ASSERT_EQUAL(STARK_OK, err);
            TEST_ASSERT_EQUAL_STRING(v->name, name);
        }
    }
    char tiny[13];
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_uart_log_name_next(0, tiny, sizeof tiny));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_uart_log_name_next(0, NULL, 14));
}

static void test_crc32_follows_zlib(void)
{
    for (size_t i = 0; i < FAKE_LAB_UART_CRC_VECS; i++) {
        const fake_lab_uart_crc_vec_t *v = &fake_lab_uart_crc_vecs[i];
        TEST_ASSERT_EQUAL_HEX32(v->crc, stark_lab_uart_crc32(0, v->data, v->n));
    }
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, stark_lab_uart_crc32(0, (const uint8_t *)"123456789", 9));
    /* a CRC can be carried on across pieces */
    uint32_t c = stark_lab_uart_crc32(0, (const uint8_t *)"1234", 4);
    c = stark_lab_uart_crc32(c, (const uint8_t *)"56789", 5);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, c);
}

static uint32_t scene_crc(fake_lab_uart_mode_t mode)
{
    uint32_t crc = 0;
    for (size_t j = 0; j < fake_lab_uart_scene_len(mode); j++) {
        uint8_t b = fake_lab_uart_scene_byte(mode, j);
        crc = stark_lab_uart_crc32(crc, &b, 1);
    }
    return crc;
}

/* The fake peer's scenes are what the Python generator says they are (the test image's scenario
 * compares the log file's CRC-32 with the same constant). */
static void test_the_scenes_have_the_reference_crc(void)
{
    TEST_ASSERT_EQUAL_UINT(1300, fake_lab_uart_scene_len(FAKE_LAB_UART_TEXT));
    TEST_ASSERT_EQUAL_UINT(1024, fake_lab_uart_scene_len(FAKE_LAB_UART_BINARY));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_uart_scene_len(FAKE_LAB_UART_ECHO));
    TEST_ASSERT_EQUAL_HEX32(FAKE_LAB_UART_TEXT_SCENE_CRC, scene_crc(FAKE_LAB_UART_TEXT));
    TEST_ASSERT_EQUAL_HEX32(FAKE_LAB_UART_BINARY_SCENE_CRC, scene_crc(FAKE_LAB_UART_BINARY));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_lists);
    RUN_TEST(test_wire_time_and_the_loopback_window);
    RUN_TEST(test_the_ring_follows_the_vectors);
    RUN_TEST(test_the_ring_never_loses_a_byte_without_counting_it);
    RUN_TEST(test_the_loopback_judgement_follows_the_vectors);
    RUN_TEST(test_the_loopback_text);
    RUN_TEST(test_hex_and_ascii_rows_follow_the_vectors);
    RUN_TEST(test_the_send_file_follows_the_vectors);
    RUN_TEST(test_log_names_follow_the_vectors);
    RUN_TEST(test_crc32_follows_zlib);
    RUN_TEST(test_the_scenes_have_the_reference_crc);
    return UNITY_END();
}
