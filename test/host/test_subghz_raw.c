/*
 * test_subghz_raw.c — the raw capture core (TASKS.md STARK-0603, ADR-0022):
 * accumulation rules, the `rf` v1 file (round trip of every scene, every malformed
 * case, 10 000 seeded mutations under ASan/UBSan), the histogram against the
 * generator's independent figures, and the file names.
 */
#include <stdio.h>
#include <string.h>
#include "fake_subghz_scenes.h"
#include "subghz_raw.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

#define FILE_BUF 26000

static subghz_sym_t s_syms[SUBGHZ_RAW_MAX_SYMBOLS + 8];
static char s_text[FILE_BUF];

/* The whole file text of r, through chunks of `chunk` bytes. */
static size_t format_all(const subghz_raw_t *r, size_t chunk)
{
    size_t cursor = 0;
    size_t total = 0;
    for (;;) {
        size_t n = 0;
        TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_format(r, &cursor, s_text + total, chunk, &n));
        if (n == 0) {
            return total;
        }
        total += n;
        TEST_ASSERT_TRUE(total + chunk <= FILE_BUF);
    }
}

static subghz_raw_t raw_of(const subghz_sym_t *sym, size_t n)
{
    return (subghz_raw_t){433920000u, SUBGHZ_MOD_OOK, 4800u, (subghz_sym_t *)sym, (uint16_t)n};
}

/* ---- names -------------------------------------------------------------------- */

static void test_names(void)
{
    TEST_ASSERT_EQUAL_UINT32(1, subghz_name_number("rf_0001.rf"));
    TEST_ASSERT_EQUAL_UINT32(9999, subghz_name_number("rf_9999.rf"));
    TEST_ASSERT_EQUAL_UINT32(0, subghz_name_number("rf_0000.rf"));
    TEST_ASSERT_EQUAL_UINT32(0, subghz_name_number("rf_001.rf"));
    TEST_ASSERT_EQUAL_UINT32(0, subghz_name_number("rf_00001.rf"));
    TEST_ASSERT_EQUAL_UINT32(0, subghz_name_number("rx_0001.rf"));
    TEST_ASSERT_EQUAL_UINT32(0, subghz_name_number("rf_0001.ir"));
    TEST_ASSERT_EQUAL_UINT32(0, subghz_name_number("rf_00a1.rf"));
    TEST_ASSERT_EQUAL_UINT32(0, subghz_name_number("ir_0001.ir"));
    TEST_ASSERT_EQUAL_UINT32(0, subghz_name_number(NULL));
    uint32_t highest = 0;
    subghz_name_scan(&highest, "readme.txt");
    TEST_ASSERT_EQUAL_UINT32(0, highest);
    subghz_name_scan(&highest, "rf_0007.rf");
    subghz_name_scan(&highest, "rf_0003.rf");
    TEST_ASSERT_EQUAL_UINT32(7, highest);
    char name[16];
    TEST_ASSERT_EQUAL(STARK_OK, subghz_name_next(highest, name, sizeof name));
    TEST_ASSERT_EQUAL_STRING("rf_0008.rf", name);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_name_next(0, name, sizeof name));
    TEST_ASSERT_EQUAL_STRING("rf_0001.rf", name);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_name_next(9998, name, 11));
    TEST_ASSERT_EQUAL_STRING("rf_9999.rf", name);
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, subghz_name_next(9999, name, sizeof name));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_name_next(1, name, 10));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_name_next(1, NULL, 16));
}

static void test_modulation_names(void)
{
    TEST_ASSERT_EQUAL_STRING("ook", subghz_mod_name(SUBGHZ_MOD_OOK));
    TEST_ASSERT_EQUAL_STRING("2fsk", subghz_mod_name(SUBGHZ_MOD_2FSK));
    TEST_ASSERT_EQUAL_STRING("?", subghz_mod_name(SUBGHZ_MOD_COUNT));
}

/* ---- validity --------------------------------------------------------------------- */

static void test_validity(void)
{
    subghz_sym_t ok[] = {400, -1200, 400};
    subghz_raw_t r = raw_of(ok, 3);
    TEST_ASSERT_TRUE(subghz_raw_valid(&r));
    TEST_ASSERT_EQUAL_UINT32(2000, subghz_raw_duration_us(&r));
    TEST_ASSERT_FALSE(subghz_raw_valid(NULL));

    subghz_raw_t bad = r;
    bad.sym = NULL;
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    bad = r;
    bad.n = 0;
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    bad = r;
    bad.n = SUBGHZ_RAW_MAX_SYMBOLS + 1;
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    bad = r;
    bad.freq_hz = SUBGHZ_BAND_MIN_HZ - 1;
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    bad.freq_hz = SUBGHZ_BAND_MAX_HZ + 1;
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    bad.freq_hz = SUBGHZ_BAND_MIN_HZ;
    TEST_ASSERT_TRUE(subghz_raw_valid(&bad));
    bad.freq_hz = SUBGHZ_BAND_MAX_HZ;
    TEST_ASSERT_TRUE(subghz_raw_valid(&bad));
    bad = r;
    bad.mod = SUBGHZ_MOD_COUNT;
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    bad = r;
    bad.rate = SUBGHZ_RAW_RATE_MIN - 1;
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    bad.rate = SUBGHZ_RAW_RATE_MAX + 1;
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    bad.rate = SUBGHZ_RAW_RATE_MAX;
    TEST_ASSERT_TRUE(subghz_raw_valid(&bad));

    subghz_sym_t low_first[] = {-400, 1200};
    bad = raw_of(low_first, 2);
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    subghz_sym_t same_sign[] = {400, 1200, -400};
    bad = raw_of(same_sign, 3);
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    subghz_sym_t zero[] = {400, 0, 400};
    bad = raw_of(zero, 3);
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    subghz_sym_t too_long[] = {1000001, -400};
    bad = raw_of(too_long, 2);
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    subghz_sym_t min_int[] = {400, INT32_MIN};
    bad = raw_of(min_int, 2);
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad));
    subghz_sym_t edge[] = {1000000, -1000000, 1};
    bad = raw_of(edge, 3);
    TEST_ASSERT_FALSE(subghz_raw_valid(&bad)); /* 2 s and 1 µs */
    bad.n = 2;
    TEST_ASSERT_TRUE(subghz_raw_valid(&bad)); /* exactly 2 s */
}

/* ---- accumulation ----------------------------------------------------------------- */

static void test_cap_rules(void)
{
    subghz_cap_t c;
    subghz_cap_init(&c, s_syms, 16);
    const subghz_sym_t a[] = {-500, -300, 0, 400, 100, -200, -300, 50};
    subghz_cap_feed(&c, a, sizeof a / sizeof a[0]);
    TEST_ASSERT_EQUAL(3, c.n); /* silence dropped, 0 ignored, same signs merged */
    TEST_ASSERT_EQUAL(500, s_syms[0]);
    TEST_ASSERT_EQUAL(-500, s_syms[1]);
    TEST_ASSERT_EQUAL(50, s_syms[2]);
    TEST_ASSERT_EQUAL_UINT32(1050, c.total_us);
    TEST_ASSERT_FALSE(c.full);
    const subghz_sym_t b[] = {-70, 90}; /* a symbol may arrive in pieces */
    subghz_cap_feed(&c, b, 2);
    TEST_ASSERT_EQUAL(5, c.n);
    TEST_ASSERT_EQUAL(-70, s_syms[3]);
    TEST_ASSERT_EQUAL(90, s_syms[4]);
}

static void test_cap_symbols_are_limited_to_one_second(void)
{
    subghz_cap_t c;
    subghz_cap_init(&c, s_syms, 16);
    const subghz_sym_t a[] = {INT32_MAX, INT32_MIN, 700000, 700000};
    subghz_cap_feed(&c, a, 4);
    TEST_ASSERT_EQUAL(1000000, s_syms[0]);
    TEST_ASSERT_EQUAL(-1000000, s_syms[1]);
    TEST_ASSERT_EQUAL(2, c.n); /* 2 s reached with the second: nothing more is kept */
    TEST_ASSERT_TRUE(c.full);
    TEST_ASSERT_EQUAL_UINT32(2000000u, c.total_us);

    subghz_cap_init(&c, s_syms, 16);
    const subghz_sym_t m[] = {700000,
                              700000}; /* merged: saturates at a second, the rest is dropped */
    subghz_cap_feed(&c, m, 2);
    TEST_ASSERT_EQUAL(1, c.n);
    TEST_ASSERT_EQUAL(1000000, s_syms[0]);
    TEST_ASSERT_EQUAL_UINT32(1000000u, c.total_us);
    TEST_ASSERT_FALSE(c.full);
    const subghz_sym_t more[] = {5}; /* a symbol already at a second takes nothing more */
    subghz_cap_feed(&c, more, 1);
    TEST_ASSERT_EQUAL(1, c.n);
    TEST_ASSERT_EQUAL_UINT32(1000000u, c.total_us);
}

static void test_cap_two_second_limit_cuts_the_last_symbol(void)
{
    subghz_cap_t c;
    subghz_cap_init(&c, s_syms, 64);
    const subghz_sym_t a[] = {900000, -900000, 900000, -900000, 5};
    subghz_cap_feed(&c, a, 5);
    TEST_ASSERT_EQUAL(3, c.n);
    TEST_ASSERT_EQUAL(200000, s_syms[2]);
    TEST_ASSERT_EQUAL_UINT32(2000000u, c.total_us);
    TEST_ASSERT_TRUE(c.full);
    subghz_cap_feed(&c, a, 5); /* once full everything is ignored */
    TEST_ASSERT_EQUAL(3, c.n);
}

static void test_cap_symbol_limit(void)
{
    subghz_cap_t c;
    subghz_cap_init(&c, s_syms, 100000); /* limited to 2048 */
    TEST_ASSERT_EQUAL(SUBGHZ_RAW_MAX_SYMBOLS, c.cap);
    subghz_cap_init(&c, s_syms, 0); /* and at least 1 */
    TEST_ASSERT_EQUAL(1, c.cap);
    const subghz_sym_t one[] = {10, -10};
    subghz_cap_feed(&c, one, 2);
    TEST_ASSERT_EQUAL(1, c.n);
    TEST_ASSERT_TRUE(c.full);

    subghz_cap_init(&c, s_syms, 2048);
    for (int i = 0; i < 3000; i++) {
        subghz_sym_t v = i % 2 == 0 ? 10 : -10;
        subghz_cap_feed(&c, &v, 1);
    }
    TEST_ASSERT_EQUAL(2048, c.n);
    TEST_ASSERT_TRUE(c.full);
    TEST_ASSERT_EQUAL_UINT32(20480u, c.total_us);
}

static void test_cap_only_silence_keeps_nothing(void)
{
    subghz_cap_t c;
    subghz_cap_init(&c, s_syms, 16);
    const subghz_sym_t a[] = {-100, -200, -300};
    subghz_cap_feed(&c, a, 3);
    TEST_ASSERT_EQUAL(0, c.n);
    subghz_cap_feed(&c, NULL, 0);
    TEST_ASSERT_EQUAL(0, c.n);
}

/* ---- every scene: capture, histogram, file -------------------------------------- */

static void test_every_scene_is_captured_as_the_generator_says(void)
{
    for (size_t i = 0; i < FAKE_SUBGHZ_SCENE_COUNT; i++) {
        const fake_subghz_scene_t *sc = &k_fake_scenes[i];
        for (size_t piece = 1; piece <= 129; piece += 64) { /* however the symbols arrive */
            subghz_cap_t c;
            subghz_cap_init(&c, s_syms, SUBGHZ_RAW_MAX_SYMBOLS);
            for (size_t at = 0; at < sc->n; at += piece) {
                subghz_cap_feed(&c, sc->sym + at, sc->n - at < piece ? sc->n - at : piece);
            }
            TEST_ASSERT_EQUAL_MESSAGE(sc->kept_n, c.n, sc->name);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(sc->kept_hash, subghz_raw_hash(s_syms, c.n), sc->name);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(sc->kept_us, c.total_us, sc->name);
        }
        subghz_cap_t c;
        subghz_cap_init(&c, s_syms, SUBGHZ_RAW_MAX_SYMBOLS);
        subghz_cap_feed(&c, sc->sym, sc->n);
        subghz_hist_t h;
        subghz_hist(s_syms, c.n, &h);
        TEST_ASSERT_EQUAL_MESSAGE(sc->bins, h.bins, sc->name);
        TEST_ASSERT_EQUAL(0, h.other);
        for (unsigned b = 0; b < sc->bins; b++) {
            TEST_ASSERT_EQUAL_MESSAGE(sc->bin_count[b], h.bin[b].count, sc->name);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(sc->bin_mean[b], h.bin[b].mean_us, sc->name);
        }
    }
}

static void test_every_scene_round_trips_exactly(void)
{
    static subghz_sym_t back[SUBGHZ_RAW_MAX_SYMBOLS];
    static const size_t chunks[] = {SUBGHZ_RAW_CHUNK_MIN, 256, 1000, 4096};
    for (size_t i = 0; i < FAKE_SUBGHZ_SCENE_COUNT; i++) {
        const fake_subghz_scene_t *sc = &k_fake_scenes[i];
        subghz_cap_t c;
        subghz_cap_init(&c, s_syms, SUBGHZ_RAW_MAX_SYMBOLS);
        subghz_cap_feed(&c, sc->sym, sc->n);
        for (size_t k = 0; k < sizeof chunks / sizeof chunks[0]; k++) {
            subghz_raw_t r = {433920000u, i % 2 == 0 ? SUBGHZ_MOD_OOK : SUBGHZ_MOD_2FSK,
                              4800u + (uint32_t)i, s_syms, c.n};
            size_t len = format_all(&r, chunks[k]);
            TEST_ASSERT_TRUE(len > 0 && len <= SUBGHZ_RAW_FILE_MAX);
            subghz_raw_t out;
            TEST_ASSERT_EQUAL_MESSAGE(
                STARK_OK, subghz_raw_parse(s_text, len, back, SUBGHZ_RAW_MAX_SYMBOLS, &out),
                sc->name);
            TEST_ASSERT_EQUAL_UINT32(r.freq_hz, out.freq_hz);
            TEST_ASSERT_EQUAL(r.mod, out.mod);
            TEST_ASSERT_EQUAL_UINT32(r.rate, out.rate);
            TEST_ASSERT_EQUAL(c.n, out.n);
            TEST_ASSERT_EQUAL_INT32_ARRAY(s_syms, out.sym, c.n); /* exactly, not within a tick */
            TEST_ASSERT_TRUE(subghz_raw_valid(&out));
        }
    }
}

static void test_the_file_text_is_what_the_format_says(void)
{
    const subghz_sym_t s[] = {400,   -1200, 400,   -1200, 1200,  -400, 400,   -1200, 400,
                              -1200, 400,   -1200, 400,   -1200, 400,  -1200, 400,   -99999};
    subghz_raw_t r = raw_of(s, 18);
    size_t len = format_all(&r, 256);
    s_text[len] = '\0';
    TEST_ASSERT_EQUAL_STRING(
        "stark-subghz 1\n"
        "freq 433920000\n"
        "mod ook\n"
        "rate 4800\n"
        "symbols 18\n"
        "400 -1200 400 -1200 1200 -400 400 -1200 400 -1200 400 -1200 400 -1200 400 -1200\n"
        "400 -99999\n",
        s_text);
}

static void test_the_longest_file_fits_the_limit(void)
{
    for (size_t i = 0; i < SUBGHZ_RAW_MAX_SYMBOLS; i++) {
        s_syms[i] = i % 2 == 0 ? 976 : -976; /* 2048 x 976 = 1.999 s */
    }
    subghz_raw_t r = raw_of(s_syms, SUBGHZ_RAW_MAX_SYMBOLS);
    TEST_ASSERT_TRUE(subghz_raw_valid(&r));
    size_t len = format_all(&r, 4096);
    TEST_ASSERT_TRUE(len < 20000); /* "a file is < 19 kB" */
    static subghz_sym_t back[SUBGHZ_RAW_MAX_SYMBOLS];
    subghz_raw_t out;
    TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parse(s_text, len, back, SUBGHZ_RAW_MAX_SYMBOLS, &out));
    TEST_ASSERT_EQUAL(SUBGHZ_RAW_MAX_SYMBOLS, out.n);
    TEST_ASSERT_EQUAL_INT32_ARRAY(s_syms, back, SUBGHZ_RAW_MAX_SYMBOLS);
}

static void test_format_refuses_bad_arguments(void)
{
    const subghz_sym_t ok[] = {400, -400};
    subghz_raw_t r = raw_of(ok, 2);
    size_t cursor = 0;
    size_t n = 99;
    char out[SUBGHZ_RAW_CHUNK_MIN];
    TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_format(&r, &cursor, out, sizeof out, &n));
    TEST_ASSERT_TRUE(n > 0);
    cursor = 0;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      subghz_raw_format(&r, &cursor, out, SUBGHZ_RAW_CHUNK_MIN - 1, &n));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_format(&r, NULL, out, sizeof out, &n));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_format(&r, &cursor, NULL, sizeof out, &n));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_format(&r, &cursor, out, sizeof out, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_format(NULL, &cursor, out, sizeof out, &n));
    subghz_raw_t bad = r;
    bad.rate = 10; /* a file that would not read back is never written */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_format(&bad, &cursor, out, sizeof out, &n));
    TEST_ASSERT_EQUAL(0, cursor);
}

/* ---- the parser: strict ------------------------------------------------------------ */

#define HDR "stark-subghz 1\nfreq 433920000\nmod ook\nrate 4800\n"

static stark_err_t parse(const char *text, subghz_raw_t *out)
{
    static subghz_sym_t buf[SUBGHZ_RAW_MAX_SYMBOLS];
    return subghz_raw_parse(text, strlen(text), buf, SUBGHZ_RAW_MAX_SYMBOLS, out);
}

static void rejects(const char *text)
{
    subghz_raw_t out = {1, SUBGHZ_MOD_2FSK, 1, NULL, 1};
    TEST_ASSERT_EQUAL_MESSAGE(STARK_ERR_INVALID_ARG, parse(text, &out), text);
    TEST_ASSERT_EQUAL(0, out.n); /* a file never loads partly */
    TEST_ASSERT_EQUAL_UINT32(0, out.freq_hz);
    TEST_ASSERT_NULL(out.sym);
}

static void accepts(const char *text, unsigned n)
{
    subghz_raw_t out;
    TEST_ASSERT_EQUAL_MESSAGE(STARK_OK, parse(text, &out), text);
    TEST_ASSERT_EQUAL(n, out.n);
}

static void test_the_smallest_files_and_the_syntax_around_them(void)
{
    accepts(HDR "symbols 1\n5\n", 1);
    accepts(HDR "symbols 2\n5 -6\n", 2);
    accepts(HDR "symbols 2\n5\n-6\n", 2); /* a line may hold fewer than 16 */
    accepts(HDR "symbols 2\n5 -6", 2);    /* no newline at the end */
    accepts("# a comment first\n\n" HDR "symbols 1\n5\n", 1);
    accepts(HDR "# between\nsymbols 1 # trailing\n5 # more\n\n# after the last\n\n", 1);
    accepts("stark-subghz\t1\r\nfreq  433920000\r\nmod\t2fsk\r\nrate 600\r\nsymbols 2\r\n "
            "1\t-1000000 \r\n",
            2);
    accepts("stark-subghz 1\nfreq 433050000\nmod ook\nrate 50000\nsymbols 1\n1\n", 1);
    accepts("stark-subghz 1\nfreq 434790000\nmod ook\nrate 4800\nsymbols 1\n1000000\n", 1);
    accepts(HDR "symbols 1\n5\n#\xff\xfe binary bytes are fine in a comment\n", 1);

    subghz_raw_t out;
    TEST_ASSERT_EQUAL(
        STARK_OK,
        parse("stark-subghz 1\nfreq 434790000\nmod 2fsk\nrate 600\nsymbols 3\n7 -8 9\n", &out));
    TEST_ASSERT_EQUAL_UINT32(434790000u, out.freq_hz);
    TEST_ASSERT_EQUAL(SUBGHZ_MOD_2FSK, out.mod);
    TEST_ASSERT_EQUAL_UINT32(600u, out.rate);
    TEST_ASSERT_EQUAL(3, out.n);
    TEST_ASSERT_EQUAL(-8, out.sym[1]);
}

static void test_a_bad_header_is_rejected(void)
{
    rejects("");
    rejects("\n\n# nothing\n");
    rejects("stark-subghz 2\nfreq 433920000\nmod ook\nrate 4800\nsymbols 1\n5\n");
    rejects("stark-subghz\nfreq 433920000\nmod ook\nrate 4800\nsymbols 1\n5\n");
    rejects("stark-subghz 1 x\nfreq 433920000\nmod ook\nrate 4800\nsymbols 1\n5\n");
    rejects("stark-ir 1\nfreq 433920000\nmod ook\nrate 4800\nsymbols 1\n5\n");
    rejects("freq 433920000\nstark-subghz 1\nmod ook\nrate 4800\nsymbols 1\n5\n");
    rejects("stark-subghz 1\nmod ook\nfreq 433920000\nrate 4800\nsymbols 1\n5\n"); /* order */
    rejects("stark-subghz 1\nfreq 433920000\nfreq 433920000\nmod ook\nrate 4800\nsymbols 1\n5\n");
    rejects("stark-subghz 1\nfreq 433920000\nmod ook\nrate 4800\nsymbols 1\nkey 5\n5\n");
    rejects("stark-subghz 1\nfreq 433920000 1\nmod ook\nrate 4800\nsymbols 1\n5\n");
    rejects("stark-subghz 1\nfreq\nmod ook\nrate 4800\nsymbols 1\n5\n");
    rejects("stark-subghz 1\nfrequency 433920000\nmod ook\nrate 4800\nsymbols 1\n5\n");
    rejects(HDR "symbols 1\n"); /* no symbols */
    rejects(HDR);
    rejects("stark-subghz 1\nfreq 433920000\nmod ook\nrate 4800\n5 -5\n"); /* no `symbols` */
}

static void test_header_values_are_checked(void)
{
    const char *freq[] = {"433049999",  "434790001",  "0",          "00433920000", "0433920000",
                          "+433920000", "433920000x", "4339200000", "99999999999", "4294967296",
                          "-1",         "433.92",     "0x19D2E5C0"};
    for (size_t i = 0; i < sizeof freq / sizeof freq[0]; i++) {
        char text[128];
        snprintf(text, sizeof text, "stark-subghz 1\nfreq %s\nmod ook\nrate 4800\nsymbols 1\n5\n",
                 freq[i]);
        rejects(text);
    }
    const char *mod[] = {"OOK", "fsk", "2FSK", "ook2", "", "gfsk", "ask"};
    for (size_t i = 0; i < sizeof mod / sizeof mod[0]; i++) {
        char text[128];
        snprintf(text, sizeof text,
                 "stark-subghz 1\nfreq 433920000\nmod %s\nrate 4800\nsymbols 1\n5\n", mod[i]);
        rejects(text);
    }
    const char *rate[] = {"599", "50001", "0", "04800", "4800.0", "-4800", "abc"};
    for (size_t i = 0; i < sizeof rate / sizeof rate[0]; i++) {
        char text[128];
        snprintf(text, sizeof text,
                 "stark-subghz 1\nfreq 433920000\nmod ook\nrate %s\nsymbols 1\n5\n", rate[i]);
        rejects(text);
    }
    const char *n[] = {"0", "2049", "01", "-1", "x", "1e3", "4294967297"};
    for (size_t i = 0; i < sizeof n / sizeof n[0]; i++) {
        rejects((snprintf(s_text, sizeof s_text, HDR "symbols %s\n5\n", n[i]), s_text));
    }
}

static void test_symbols_are_checked(void)
{
    rejects(HDR "symbols 1\n-5\n");       /* starts high */
    rejects(HDR "symbols 2\n5 6\n");      /* alternate */
    rejects(HDR "symbols 2\n5 -6 \n7\n"); /* more than declared */
    rejects(HDR "symbols 3\n5 -6\n");     /* fewer than declared */
    rejects(HDR "symbols 2\n0 -6\n");     /* zero */
    rejects(HDR "symbols 2\n5 0\n");
    rejects(HDR "symbols 2\n5 -0\n");
    rejects(HDR "symbols 1\n1000001\n"); /* too long */
    rejects(HDR "symbols 2\n5 -1000001\n");
    rejects(HDR "symbols 2\n5 -99999999999\n");
    rejects(HDR "symbols 1\n+5\n");
    rejects(HDR "symbols 1\n05\n");
    rejects(HDR "symbols 2\n5 -\n");
    rejects(HDR "symbols 2\n5 --6\n");
    rejects(HDR "symbols 1\n5.0\n");
    rejects(HDR "symbols 1\n0x10\n");
    rejects(HDR "symbols 1\n1,000\n");
    rejects(HDR "symbols 1\n5abc\n");
    rejects(HDR "symbols 3\n5 -6 7 8\n"); /* the fourth is beyond the count */
    rejects(HDR "symbols 1\n5\n6\n");     /* nothing after the last symbol */
    rejects(HDR "symbols 1\n5\nsymbols 1\n");
    rejects(HDR "symbols 1\n5\n-6\n");
    /* 1.000001 s + 1 s + 1 s: the 2 s total is exceeded */
    rejects(HDR "symbols 3\n1000000 -1000000 1\n");
    accepts(HDR "symbols 2\n1000000 -1000000\n", 2);
    /* 17 symbols on a line */
    rejects(HDR "symbols 17\n1 -1 1 -1 1 -1 1 -1 1 -1 1 -1 1 -1 1 -1 1\n");
    accepts(HDR "symbols 17\n1 -1 1 -1 1 -1 1 -1 1 -1 1 -1 1 -1 1 -1\n1\n", 17);
}

static void test_bytes_that_are_not_text_are_rejected(void)
{
    subghz_raw_t out;
    static subghz_sym_t buf[16];
    const char nul[] = HDR "symbols 1\n5\0\n";
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_parse(nul, sizeof nul - 1, buf, 16, &out));
    const char nul_comment[] = HDR "symbols 1\n5\n# a\0b\n";
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      subghz_raw_parse(nul_comment, sizeof nul_comment - 1, buf, 16, &out));
    rejects(HDR "symbols 1\n5\x01\n");
    rejects(HDR "symbols 1\n5\x7f\n");
    rejects(HDR "symbols 1\n5\x80\n");
    rejects(HDR "symbols 1\n\xff\n");
    rejects("stark-subghz\v1\n");
}

static void test_a_long_line_is_rejected_but_a_long_comment_is_not(void)
{
    char text[600];
    size_t n = (size_t)snprintf(text, sizeof text, "%s", HDR "symbols 1\n5");
    memset(text + n, ' ', 159 - 1); /* the line is "5" and spaces: 159 characters in all */
    text[n + 158] = '\0';
    accepts(text, 1);
    memset(text + n, ' ', 170);
    text[n + 170] = '\0';
    rejects(text);
    snprintf(text, sizeof text, "%s", HDR "symbols 1\n5 #");
    n = strlen(text);
    memset(text + n, 'x', 400);
    text[n + 400] = '\0';
    accepts(text, 1); /* what a comment holds is nobody's business */
}

static void test_the_size_of_a_file_is_bounded(void)
{
    static char big[SUBGHZ_RAW_FILE_MAX + 100];
    int n = snprintf(big, sizeof big, "%s", HDR "symbols 1\n5\n");
    memset(big + n, '\n', SUBGHZ_RAW_FILE_MAX - (size_t)n);
    subghz_raw_t out;
    static subghz_sym_t buf[16];
    TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parse(big, SUBGHZ_RAW_FILE_MAX, buf, 16,
                                                 &out)); /* exactly the limit */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      subghz_raw_parse(big, SUBGHZ_RAW_FILE_MAX + 1, buf, 16, &out));
    /* the same across feeds */
    subghz_raw_parser_t p;
    subghz_raw_parser_init(&p, buf, 16);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parser_feed(&p, big, SUBGHZ_RAW_FILE_MAX));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_parser_feed(&p, big, 1));
}

static void test_the_buffer_limits_what_a_file_may_declare(void)
{
    subghz_raw_t out;
    static subghz_sym_t buf[5];
    const char *text = HDR "symbols 5\n1 -1 1 -1 1\n";
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_parse(text, strlen(text), buf, 4, &out));
    TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parse(text, strlen(text), buf, 5, &out));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_parse(text, strlen(text), NULL, 5, &out));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_parse(text, strlen(text), buf, 0, &out));
}

static void test_a_parser_stays_failed(void)
{
    static subghz_sym_t buf[8];
    subghz_raw_parser_t p;
    subghz_raw_t out;
    subghz_raw_parser_init(&p, buf, 8);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_parser_feed(&p, "bogus\n", 6));
    const char *good = HDR "symbols 1\n5\n";
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_parser_feed(&p, good, strlen(good)));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_parser_finish(&p, &out));
    TEST_ASSERT_EQUAL(0, out.n);

    subghz_raw_parser_init(&p, buf, 8);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_parser_feed(&p, NULL, 3));
    subghz_raw_parser_init(&p, buf, 8);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parser_feed(&p, NULL, 0));
    TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parser_feed(&p, good, strlen(good)));
    TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parser_finish(&p, &out));
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_raw_parser_finish(&p, &out)); /* finishing again changes nothing */
    TEST_ASSERT_EQUAL(1, out.n);

    /* a last line without its newline that is wrong fails at the end */
    subghz_raw_parser_init(&p, buf, 8);
    const char *bad = HDR "symbols 1\n-5";
    TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parser_feed(&p, bad, strlen(bad)));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_raw_parser_finish(&p, &out));
    TEST_ASSERT_EQUAL(0, out.n);
}

static void test_a_file_read_in_any_pieces_reads_the_same(void)
{
    static subghz_sym_t back[SUBGHZ_RAW_MAX_SYMBOLS];
    const fake_subghz_scene_t *sc = &k_fake_scenes[FAKE_SUBGHZ_SCENE_MANCHESTER32 - 1];
    subghz_raw_t r = raw_of(sc->sym, sc->n);
    size_t len = format_all(&r, 256);
    for (size_t piece = 1; piece <= 300; piece += (piece < 20 ? 1 : 37)) {
        subghz_raw_parser_t p;
        subghz_raw_parser_init(&p, back, SUBGHZ_RAW_MAX_SYMBOLS);
        for (size_t at = 0; at < len; at += piece) {
            TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parser_feed(
                                            &p, s_text + at, len - at < piece ? len - at : piece));
        }
        subghz_raw_t out;
        TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parser_finish(&p, &out));
        TEST_ASSERT_EQUAL(sc->n, out.n);
        TEST_ASSERT_EQUAL_INT32_ARRAY(sc->sym, back, sc->n);
    }
}

static void test_a_truncated_file_never_loads(void)
{
    static subghz_sym_t back[SUBGHZ_RAW_MAX_SYMBOLS];
    const fake_subghz_scene_t *sc = &k_fake_scenes[FAKE_SUBGHZ_SCENE_PWM24 - 1];
    subghz_raw_t r = raw_of(sc->sym, sc->n);
    size_t len = format_all(&r, 256);
    size_t last_line = len - 1;
    while (s_text[last_line - 1] != '\n') {
        last_line--;
    }
    for (size_t cut = 0; cut < last_line; cut++) { /* anywhere before the last line */
        subghz_raw_t out;
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                          subghz_raw_parse(s_text, cut, back, SUBGHZ_RAW_MAX_SYMBOLS, &out));
        TEST_ASSERT_EQUAL(0, out.n);
    }
    for (size_t cut = last_line; cut <= len; cut++) { /* in the last line: whole or refused */
        subghz_raw_t out;
        stark_err_t err = subghz_raw_parse(s_text, cut, back, SUBGHZ_RAW_MAX_SYMBOLS, &out);
        TEST_ASSERT_TRUE(err == STARK_ERR_INVALID_ARG ? out.n == 0 : out.n == sc->n);
    }
}

/* ---- fuzzing -------------------------------------------------------------------------- */

static uint32_t s_rng = 0x9E3779B9u;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static void test_ten_thousand_seeded_mutations(void)
{
    enum { GUARD = 4, CAP = 64 };
    static char seed[3][2048];
    static size_t seed_len[3];
    static char mut[2200];
    static subghz_sym_t guarded[GUARD + CAP + GUARD];
    static subghz_sym_t again[CAP];
    static char text2[2200];

    /* three small valid files: a pulse train, a 2-FSK file, and the shortest one */
    subghz_sym_t a[40];
    for (int i = 0; i < 40; i++) {
        a[i] = i % 2 == 0 ? 300 + i : -(500 + i);
    }
    subghz_raw_t r = raw_of(a, 40);
    seed_len[0] = format_all(&r, 256);
    memcpy(seed[0], s_text, seed_len[0]);
    r.mod = SUBGHZ_MOD_2FSK;
    r.n = 20;
    r.rate = 600;
    seed_len[1] = format_all(&r, 256);
    memcpy(seed[1], s_text, seed_len[1]);
    const char *tiny = "# c\nstark-subghz 1\nfreq 433050000\nmod ook\nrate 50000\nsymbols 1\n1\n";
    seed_len[2] = strlen(tiny);
    memcpy(seed[2], tiny, seed_len[2]);

    unsigned accepted = 0;
    for (int iter = 0; iter < 10000; iter++) {
        size_t which = rnd() % 3u;
        size_t len = seed_len[which];
        memcpy(mut, seed[which], len);
        unsigned rounds = 1u + rnd() % 4u;
        for (unsigned k = 0; k < rounds && len > 0; k++) {
            size_t at = rnd() % len;
            switch (rnd() % 6u) {
                case 0:
                    mut[at] = (char)rnd(); /* any byte */
                    break;
                case 1:
                    mut[at] = "0123456789- \n#\t"[rnd() % 15u]; /* the characters that matter */
                    break;
                case 2: /* delete */
                    memmove(mut + at, mut + at + 1, len - at - 1);
                    len--;
                    break;
                case 3: /* insert */
                    if (len + 1 < sizeof mut) {
                        memmove(mut + at + 1, mut + at, len - at);
                        mut[at] = "0123456789-\n "[rnd() % 13u];
                        len++;
                    }
                    break;
                case 4: /* truncate */
                    len = at;
                    break;
                default: /* repeat a stretch */
                    if (len * 2 < sizeof mut) {
                        size_t n = len - at < 64 ? len - at : 64;
                        memmove(mut + at + n, mut + at, len - at);
                        len += n;
                    }
                    break;
            }
        }
        for (int g = 0; g < GUARD; g++) {
            guarded[g] = 0x5A5A5A5A;
            guarded[GUARD + CAP + g] = 0x5A5A5A5A;
        }
        subghz_raw_t out;
        stark_err_t err = subghz_raw_parse(mut, len, guarded + GUARD, CAP, &out);
        for (int g = 0; g < GUARD; g++) { /* nothing was written past the buffer */
            TEST_ASSERT_EQUAL_HEX32(0x5A5A5A5A, (uint32_t)guarded[g]);
            TEST_ASSERT_EQUAL_HEX32(0x5A5A5A5A, (uint32_t)guarded[GUARD + CAP + g]);
        }
        if (err != STARK_OK) {
            TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, err);
            TEST_ASSERT_EQUAL(0, out.n);
            TEST_ASSERT_NULL(out.sym);
            continue;
        }
        accepted++;
        TEST_ASSERT_TRUE(out.n >= 1 && out.n <= CAP);
        TEST_ASSERT_TRUE(subghz_raw_valid(&out)); /* what loads is what could have been saved */
        size_t n2 = format_all(&out, 256);        /* and saving it reads back identically */
        memcpy(text2, s_text, n2);
        subghz_raw_t out2;
        TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parse(text2, n2, again, CAP, &out2));
        TEST_ASSERT_EQUAL(out.n, out2.n);
        TEST_ASSERT_EQUAL_INT32_ARRAY(out.sym, out2.sym, out.n);
    }
    TEST_ASSERT_TRUE(accepted >
                     100); /* the mutations are not all fatal: both outcomes are exercised */
    TEST_ASSERT_TRUE(accepted < 9900);
}

/* ---- hash and histogram -------------------------------------------------------------- */

static void test_hash_matches_the_independent_implementation(void)
{
    const subghz_sym_t a[] = {1, -2};
    TEST_ASSERT_EQUAL_HEX32(0x73f687f1u, subghz_raw_hash(a, 2));
    TEST_ASSERT_EQUAL_HEX32(0x811c9dc5u, subghz_raw_hash(a, 0));
    const subghz_sym_t b[] = {400, -1200, 400};
    TEST_ASSERT_EQUAL_HEX32(0xfaf6f922u, subghz_raw_hash(b, 3));
}

static void test_histogram_grouping(void)
{
    subghz_hist_t h;
    subghz_hist(NULL, 0, &h);
    TEST_ASSERT_EQUAL(0, h.bins);
    TEST_ASSERT_EQUAL(0, h.other);

    const subghz_sym_t a[] = {100, -150, 151, 0, -1};
    subghz_hist(a, 5, &h); /* 0 is no duration; 1 is its own bin; 100..150 one; 151 next */
    TEST_ASSERT_EQUAL(3, h.bins);
    TEST_ASSERT_EQUAL(1, h.bin[0].count);
    TEST_ASSERT_EQUAL_UINT32(1, h.bin[0].mean_us);
    TEST_ASSERT_EQUAL(2, h.bin[1].count);
    TEST_ASSERT_EQUAL_UINT32(125, h.bin[1].mean_us);
    TEST_ASSERT_EQUAL_UINT32(100, h.bin[1].lo_us);
    TEST_ASSERT_EQUAL_UINT32(150, h.bin[1].hi_us);
    TEST_ASSERT_EQUAL(1, h.bin[2].count);
    TEST_ASSERT_EQUAL_UINT32(151, h.bin[2].mean_us);

    const subghz_sym_t round[] = {100, 101}; /* 100.5 rounds to 101 */
    subghz_hist(round, 2, &h);
    TEST_ASSERT_EQUAL_UINT32(101, h.bin[0].mean_us);

    /* the order of the symbols does not matter */
    const subghz_sym_t fwd[] = {400, -1200, 410, -1190, 390, -1210, 5000};
    const subghz_sym_t rev[] = {5000, -1210, 390, -1190, 410, -1200, 400};
    subghz_hist_t h2;
    subghz_hist(fwd, 7, &h);
    subghz_hist(rev, 7, &h2);
    TEST_ASSERT_EQUAL(3, h.bins);
    TEST_ASSERT_EQUAL(h.bins, h2.bins);
    for (unsigned b = 0; b < h.bins; b++) {
        TEST_ASSERT_EQUAL(h.bin[b].count, h2.bin[b].count);
        TEST_ASSERT_EQUAL_UINT32(h.bin[b].mean_us, h2.bin[b].mean_us);
    }
    TEST_ASSERT_EQUAL_UINT32(400, h.bin[0].mean_us);
    TEST_ASSERT_EQUAL_UINT32(1200, h.bin[1].mean_us);

    /* more than eight groups: the longest are counted as `other` */
    subghz_sym_t many[12];
    for (int i = 0; i < 12; i++) {
        many[i] = (subghz_sym_t)(1 << i); /* 1, 2, 4 .. 2048 */
    }
    subghz_hist(many, 12, &h);
    TEST_ASSERT_EQUAL(8, h.bins);
    TEST_ASSERT_EQUAL(4, h.other);
    TEST_ASSERT_EQUAL_UINT32(128, h.bin[7].mean_us);

    /* the extremes do not overflow */
    const subghz_sym_t ext[] = {INT32_MAX, INT32_MIN};
    subghz_hist(ext, 2, &h);
    TEST_ASSERT_EQUAL(1, h.bins);
    TEST_ASSERT_EQUAL(2, h.bin[0].count);
    TEST_ASSERT_EQUAL_UINT32(2147483648u, h.bin[0].mean_us); /* 2147483647.5 rounds up */
    TEST_ASSERT_EQUAL_UINT32(2147483648u, h.bin[0].hi_us);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_names);
    RUN_TEST(test_modulation_names);
    RUN_TEST(test_validity);
    RUN_TEST(test_cap_rules);
    RUN_TEST(test_cap_symbols_are_limited_to_one_second);
    RUN_TEST(test_cap_two_second_limit_cuts_the_last_symbol);
    RUN_TEST(test_cap_symbol_limit);
    RUN_TEST(test_cap_only_silence_keeps_nothing);
    RUN_TEST(test_every_scene_is_captured_as_the_generator_says);
    RUN_TEST(test_every_scene_round_trips_exactly);
    RUN_TEST(test_the_file_text_is_what_the_format_says);
    RUN_TEST(test_the_longest_file_fits_the_limit);
    RUN_TEST(test_format_refuses_bad_arguments);
    RUN_TEST(test_the_smallest_files_and_the_syntax_around_them);
    RUN_TEST(test_a_bad_header_is_rejected);
    RUN_TEST(test_header_values_are_checked);
    RUN_TEST(test_symbols_are_checked);
    RUN_TEST(test_bytes_that_are_not_text_are_rejected);
    RUN_TEST(test_a_long_line_is_rejected_but_a_long_comment_is_not);
    RUN_TEST(test_the_size_of_a_file_is_bounded);
    RUN_TEST(test_the_buffer_limits_what_a_file_may_declare);
    RUN_TEST(test_a_parser_stays_failed);
    RUN_TEST(test_a_file_read_in_any_pieces_reads_the_same);
    RUN_TEST(test_a_truncated_file_never_loads);
    RUN_TEST(test_ten_thousand_seeded_mutations);
    RUN_TEST(test_hash_matches_the_independent_implementation);
    RUN_TEST(test_histogram_grouping);
    return UNITY_END();
}
