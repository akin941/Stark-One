/*
 * test_lab_autobaud.c — auto-baud detection (components/stark_lab/lab_autobaud.c, TASKS.md
 * STARK-0705, ADR-0023 §7) against the edge stamps of scripts/gen_lab_vectors.py: streams at every
 * baud and format with jitter, runs of identical bits, idle gaps and spikes in the idle line, the
 * 3 % snap boundary from both sides, baud rates that are not on the list, too few edges, glitches,
 * a clock that is missing and steps that go backwards, each with the result an independent Python
 * implementation computes; and the fake peer's own stream, which must read back as the baud it
 * was made at. Proves the arithmetic and its bounds, never a signal on a pad (ROADMAP V1 exit 13).
 */
#include <stdio.h>
#include <string.h>
#include "fake_lab.h"
#include "fake_lab_uart_vectors.h"
#include "lab_autobaud.h"
#include "lab_uart.h"
#include "unity.h"

void setUp(void)
{
    fake_lab_reset();
}
void tearDown(void)
{}

/* AC 2: every vector reproduces the reference exactly, and the stream made at a listed baud is
 * named with that baud and no other; the rest are a status and never a number. */
static void test_every_vector_matches_the_independent_reference(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_UART_AUTOBAUD_VECS >= 60u);
    size_t named = 0;
    size_t refused = 0;
    for (size_t i = 0; i < FAKE_LAB_UART_AUTOBAUD_VECS; i++) {
        const fake_lab_uart_autobaud_vec_t *v = &fake_lab_uart_autobaud_vecs[i];
        stark_lab_autobaud_t r;
        lab_autobaud_compute(v->ts, v->n, v->tick, &r);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->status, (unsigned)r.status, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->baud, r.baud, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->raw, r.raw_baud, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->bit, r.bit_ticks, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->n, r.edges, v->name);
        if (v->nominal != 0u) {
            named++;
            TEST_ASSERT_EQUAL_UINT_MESSAGE(STARK_LAB_BAUD_OK, (unsigned)r.status, v->name);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->nominal, r.baud, v->name);
            TEST_ASSERT_NOT_EQUAL_MESSAGE(SIZE_MAX, lab_uart_baud_index(r.baud), v->name);
            /* within 3 % of what the refined bit time says */
            uint32_t diff = r.raw_baud > r.baud ? r.raw_baud - r.baud : r.baud - r.raw_baud;
            TEST_ASSERT_TRUE_MESSAGE((uint64_t)diff * 100u <= 3u * r.baud, v->name);
        } else if (r.status != STARK_LAB_BAUD_OK) {
            refused++;
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.baud, v->name);
        }
    }
    TEST_ASSERT_TRUE(named >= 45u);
    TEST_ASSERT_TRUE(refused >= 15u);
}

static void test_every_listed_baud_has_a_vector(void)
{
    for (size_t b = 0; b < STARK_LAB_UART_BAUDS; b++) {
        size_t found = 0;
        for (size_t i = 0; i < FAKE_LAB_UART_AUTOBAUD_VECS; i++) {
            found += fake_lab_uart_autobaud_vecs[i].nominal == stark_lab_uart_baud(b) ? 1u : 0u;
        }
        TEST_ASSERT_TRUE_MESSAGE(found >= 2u, "a listed baud without two vectors");
    }
}

static void test_the_texts(void)
{
    char buf[16];
    stark_lab_autobaud_t r = {.status = STARK_LAB_BAUD_OK, .baud = 38400};
    TEST_ASSERT_EQUAL_STRING("38400", stark_lab_autobaud_text(&r, buf, sizeof buf));
    r.status = STARK_LAB_BAUD_UNKNOWN;
    TEST_ASSERT_EQUAL_STRING("unknown", stark_lab_autobaud_text(&r, buf, sizeof buf));
    r.status = STARK_LAB_BAUD_TOO_FEW;
    TEST_ASSERT_EQUAL_STRING("too few edges", stark_lab_autobaud_text(&r, buf, sizeof buf));
    r.status = STARK_LAB_BAUD_NO_PULSE;
    TEST_ASSERT_EQUAL_STRING("no pulse", stark_lab_autobaud_text(&r, buf, sizeof buf));
    r.status = STARK_LAB_BAUD_INVALID;
    TEST_ASSERT_EQUAL_STRING("invalid", stark_lab_autobaud_text(&r, buf, sizeof buf));
}

static void test_hostile_input_is_a_status_and_never_a_read_past_the_array(void)
{
    stark_lab_autobaud_t r;
    lab_autobaud_compute(NULL, 40, 240000000u, &r);
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_BAUD_INVALID, (unsigned)r.status);
    lab_autobaud_compute(NULL, 0, 240000000u, &r);
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_BAUD_TOO_FEW, (unsigned)r.status);
    uint32_t ts[LAB_AUTOBAUD_MAX_EDGES];
    for (size_t i = 0; i < LAB_AUTOBAUD_MAX_EDGES; i++) {
        ts[i] = (uint32_t)(i * 2083u);
    }
    lab_autobaud_compute(ts, LAB_AUTOBAUD_MAX_EDGES, 240000000u, &r); /* the most it takes */
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_BAUD_OK, (unsigned)r.status);
    TEST_ASSERT_EQUAL_UINT32(115200, r.baud);
    lab_autobaud_compute(ts, LAB_AUTOBAUD_MAX_EDGES + 1u, 240000000u, &r);
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_BAUD_INVALID, (unsigned)r.status);
    /* a stamp that went backwards anywhere is refused whole */
    ts[1000] = ts[999] - 1u;
    lab_autobaud_compute(ts, 1100, 240000000u, &r);
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_BAUD_INVALID, (unsigned)r.status);
    TEST_ASSERT_EQUAL_UINT32(0, r.baud);
}

static void test_the_same_input_gives_the_same_result(void)
{
    const fake_lab_uart_autobaud_vec_t *v = &fake_lab_uart_autobaud_vecs[3];
    stark_lab_autobaud_t a;
    stark_lab_autobaud_t b;
    lab_autobaud_compute(v->ts, v->n, v->tick, &a);
    lab_autobaud_compute(v->ts, v->n, v->tick, &b);
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

/* The fake peer's stream, captured the way the service does (512 stamps from the RX pad), reads
 * back as the baud and format it was made at: the model and the rule agree, at every baud and
 * format, with the jitter the edge capture offers. */
static void test_the_fake_peer_reads_back_as_its_own_baud_and_format(void)
{
    for (uint8_t fmt = 0; fmt < STARK_LAB_UART_FMTS; fmt++) {
        for (size_t b = 0; b < STARK_LAB_UART_BAUDS; b++) {
            uint32_t baud = stark_lab_uart_baud(b);
            fake_lab_reset();
            fake_lab_uart_pins(38, 39);
            fake_lab_uart_peer(FAKE_LAB_UART_TEXT, baud, fmt);
            fake_lab_edge_noise(240000000u / baud / 10u < 240u ? 240000000u / baud / 10u : 240u,
                                7u + fmt);
            fake_lab_time_us(123456);
            uint32_t ts[512];
            size_t n = 0;
            uint32_t seen = 0;
            bool first = false;
            TEST_ASSERT_EQUAL(STARK_OK,
                              fake_lab_pad_edges(39, 2000000u, ts, 512, &n, &seen, &first));
            TEST_ASSERT_TRUE(first); /* the line idles high */
            TEST_ASSERT_EQUAL_UINT(512, n);
            TEST_ASSERT_TRUE(seen > 512u);
            stark_lab_autobaud_t r;
            lab_autobaud_compute(ts, n, FAKE_LAB_TICK_HZ, &r);
            char msg[40];
            snprintf(msg, sizeof msg, "%u %s", (unsigned)baud, stark_lab_uart_fmt_name(fmt));
            TEST_ASSERT_EQUAL_UINT_MESSAGE(STARK_LAB_BAUD_OK, (unsigned)r.status, msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(baud, r.baud, msg);
        }
    }
}

static void test_a_peer_that_does_not_talk_gives_no_edges(void)
{
    static const fake_lab_uart_mode_t k_quiet[] = {FAKE_LAB_UART_SILENT, FAKE_LAB_UART_LOOP,
                                                   FAKE_LAB_UART_ECHO};
    for (size_t i = 0; i < 3; i++) {
        fake_lab_reset();
        fake_lab_uart_pins(38, 39);
        fake_lab_uart_peer(k_quiet[i], 38400, 0);
        uint32_t ts[64];
        size_t n = 9;
        uint32_t seen = 9;
        bool first = false;
        TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_edges(39, 100000u, ts, 64, &n, &seen, &first));
        TEST_ASSERT_EQUAL_UINT(0, n);
        stark_lab_autobaud_t r;
        lab_autobaud_compute(ts, n, FAKE_LAB_TICK_HZ, &r);
        TEST_ASSERT_EQUAL_UINT(STARK_LAB_BAUD_TOO_FEW, (unsigned)r.status);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_vector_matches_the_independent_reference);
    RUN_TEST(test_every_listed_baud_has_a_vector);
    RUN_TEST(test_the_texts);
    RUN_TEST(test_hostile_input_is_a_status_and_never_a_read_past_the_array);
    RUN_TEST(test_the_same_input_gives_the_same_result);
    RUN_TEST(test_the_fake_peer_reads_back_as_its_own_baud_and_format);
    RUN_TEST(test_a_peer_that_does_not_talk_gives_no_edges);
    return UNITY_END();
}
