/*
 * test_subghz_decode.c — the generic PWM / PPM / Manchester decoders (TASKS.md STARK-0604,
 * ADR-0022 §7): every vector of scripts/gen_subghz_vectors.py (bits built into captures with
 * ±10 % jitter, tolerance edges, limits, noise, mutations) against what the generator's
 * independent reference decoder reads from it, to the microsecond; the three spec scenes; the
 * burst-splitting rule at its edge; invalid and hostile captures; and 10 000 seeded random
 * captures under ASan and UBSan with exactly-sized buffers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_subghz_decode_vectors.h"
#include "fake_subghz_scenes.h"
#include "subghz_decode.h"
#include "subghz_raw.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static subghz_dec_result_t s_res;

static const char *kind_of(const subghz_dec_burst_t *b)
{
    return subghz_dec_kind_name((subghz_dec_kind_t)b->kind);
}

static void check_result(const char *name, const fake_dec_vector_t *v, const subghz_dec_result_t *r)
{
    char msg[160];
    snprintf(msg, sizeof msg, "%s: decoded", name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->decoded, r->decoded, msg);
    snprintf(msg, sizeof msg, "%s: unknown", name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->unknown, r->unknown, msg);
    snprintf(msg, sizeof msg, "%s: omitted", name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->omitted, r->omitted, msg);
    snprintf(msg, sizeof msg, "%s: distinct bursts", name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v->n_burst, r->n, msg);
    for (unsigned i = 0; i < v->n_burst; i++) {
        const fake_dec_burst_t *want = &v->burst[i];
        const subghz_dec_burst_t *got = &r->burst[i];
        char hex[SUBGHZ_DEC_HEX_SIZE];
        subghz_dec_hex(got, hex, sizeof hex);
        snprintf(msg, sizeof msg, "%s: burst %u kind", name, i);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want->kind, kind_of(got), msg);
        snprintf(msg, sizeof msg, "%s: burst %u bits", name, i);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want->bits, got->bits, msg);
        snprintf(msg, sizeof msg, "%s: burst %u hex", name, i);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want->hex, hex, msg);
        snprintf(msg, sizeof msg, "%s: burst %u repeats", name, i);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(want->repeats, got->repeats, msg);
        snprintf(msg, sizeof msg, "%s: burst %u short", name, i);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(want->short_us, got->short_us, msg);
        snprintf(msg, sizeof msg, "%s: burst %u long", name, i);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(want->long_us, got->long_us, msg);
    }
}

/* The invariants of any result, whatever went in. */
static void check_invariants(const subghz_dec_result_t *r, size_t n)
{
    TEST_ASSERT_TRUE(r->n <= SUBGHZ_DEC_MAX_BURSTS);
    unsigned sum = r->omitted;
    for (unsigned i = 0; i < r->n; i++) {
        const subghz_dec_burst_t *b = &r->burst[i];
        TEST_ASSERT_TRUE(b->kind > SUBGHZ_DEC_UNKNOWN && b->kind < SUBGHZ_DEC_KIND_COUNT);
        TEST_ASSERT_TRUE(b->bits >= SUBGHZ_DEC_MIN_BITS && b->bits <= SUBGHZ_DEC_MAX_BITS);
        TEST_ASSERT_TRUE(b->repeats >= 1);
        TEST_ASSERT_TRUE(b->short_us >= 1 && b->short_us < b->long_us);
        for (unsigned bit = b->bits; bit < 128u; bit++) {
            TEST_ASSERT_EQUAL_UINT8(0, b->data[bit / 8u] & (0x80u >> (bit % 8u)));
        }
        for (unsigned j = 0; j < i; j++) {
            const subghz_dec_burst_t *o = &r->burst[j];
            TEST_ASSERT_FALSE(o->kind == b->kind && o->bits == b->bits &&
                              memcmp(o->data, b->data, sizeof b->data) == 0);
        }
        sum += b->repeats;
    }
    TEST_ASSERT_EQUAL_UINT(r->decoded, sum);
    TEST_ASSERT_TRUE((size_t)r->decoded + r->unknown <= n); /* a burst is at least one symbol */
}

/* ---- names and text ------------------------------------------------------------------- */

static void test_kind_names(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown", subghz_dec_kind_name(SUBGHZ_DEC_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("pwm", subghz_dec_kind_name(SUBGHZ_DEC_PWM));
    TEST_ASSERT_EQUAL_STRING("ppm", subghz_dec_kind_name(SUBGHZ_DEC_PPM));
    TEST_ASSERT_EQUAL_STRING("manchester", subghz_dec_kind_name(SUBGHZ_DEC_MANCHESTER));
    TEST_ASSERT_EQUAL_STRING("?", subghz_dec_kind_name(SUBGHZ_DEC_KIND_COUNT));
    TEST_ASSERT_EQUAL_STRING("?", subghz_dec_kind_name((subghz_dec_kind_t)99));
}

static void test_hex_text(void)
{
    subghz_dec_burst_t b;
    char out[SUBGHZ_DEC_HEX_SIZE];
    memset(&b, 0, sizeof b);
    b.bits = 24;
    b.data[0] = 0x5A;
    b.data[1] = 0x3C;
    b.data[2] = 0xC3;
    subghz_dec_hex(&b, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("5a3cc3", out);

    b.bits = 12; /* a started byte is a whole byte: the bits first, then zeros */
    b.data[0] = 0xA5;
    b.data[1] = 0xC0;
    subghz_dec_hex(&b, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("a5c0", out);

    b.bits = 128;
    memset(b.data, 0xFF, sizeof b.data);
    subghz_dec_hex(&b, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(32, strlen(out));
    TEST_ASSERT_EQUAL_STRING("ffffffffffffffffffffffffffffffff", out);

    memset(out, 'x', sizeof out);
    subghz_dec_hex(&b, out, 5); /* truncated to whole digit pairs, always terminated */
    TEST_ASSERT_EQUAL_STRING("ffff", out);
    subghz_dec_hex(&b, out, 3);
    TEST_ASSERT_EQUAL_STRING("ff", out);
    subghz_dec_hex(&b, out, 2);
    TEST_ASSERT_EQUAL_STRING("", out);
    subghz_dec_hex(&b, out, 1);
    TEST_ASSERT_EQUAL_STRING("", out);
    out[0] = 'x';
    subghz_dec_hex(&b, out, 0); /* nothing may be written */
    TEST_ASSERT_EQUAL_CHAR('x', out[0]);
}

/* ---- the generator's vectors -------------------------------------------------------------- */

static void test_every_vector_decodes_as_the_reference_says(void)
{
    for (size_t i = 0; i < FAKE_DEC_VECTOR_COUNT; i++) {
        const fake_dec_vector_t *v = &k_fake_dec_vectors[i];
        TEST_ASSERT_EQUAL_MESSAGE(STARK_OK, subghz_decode(v->sym, v->n, &s_res), v->name);
        check_result(v->name, v, &s_res);
        check_invariants(&s_res, v->n);
    }
}

static void test_twenty_jittered_vectors_per_kind_decode_to_their_bits(void)
{
    unsigned per_kind[SUBGHZ_DEC_KIND_COUNT] = {0};
    for (size_t i = 0; i < FAKE_DEC_VECTOR_COUNT; i++) {
        const fake_dec_vector_t *v = &k_fake_dec_vectors[i];
        if (v->jitter_pct != 10) {
            continue;
        }
        TEST_ASSERT_EQUAL_MESSAGE(STARK_OK, subghz_decode(v->sym, v->n, &s_res), v->name);
        TEST_ASSERT_EQUAL_MESSAGE(0, s_res.unknown, v->name); /* every burst, repeats included */
        TEST_ASSERT_EQUAL_MESSAGE(1, s_res.n, v->name);       /* all of them the same bits */
        TEST_ASSERT_EQUAL_STRING_MESSAGE(v->burst[0].kind, kind_of(&s_res.burst[0]), v->name);
        per_kind[s_res.burst[0].kind]++;
    }
    TEST_ASSERT_TRUE(per_kind[SUBGHZ_DEC_PWM] >= 20);
    TEST_ASSERT_TRUE(per_kind[SUBGHZ_DEC_PPM] >= 20);
    TEST_ASSERT_TRUE(per_kind[SUBGHZ_DEC_MANCHESTER] >= 20);
}

static void test_the_vector_set_covers_the_cases_that_matter(void)
{
    unsigned edge_in = 0;
    unsigned edge_out = 0;
    unsigned noise = 0;
    unsigned mutated = 0;
    bool omitted = false;
    bool ambiguous = false;
    bool mixed = false;
    for (size_t i = 0; i < FAKE_DEC_VECTOR_COUNT; i++) {
        const fake_dec_vector_t *v = &k_fake_dec_vectors[i];
        edge_in += strncmp(v->name, "edge_", 5) == 0 && strstr(v->name, "_in") != NULL;
        edge_out += strncmp(v->name, "edge_", 5) == 0 && strstr(v->name, "_out") != NULL;
        noise += strncmp(v->name, "noise_", 6) == 0;
        mutated += strncmp(v->name, "mutated_", 8) == 0;
        omitted = omitted || v->omitted == 2u;
        ambiguous = ambiguous || strcmp(v->name, "ambiguous_ppm_manchester") == 0;
        mixed = mixed || strcmp(v->name, "mixed_in_a_burst") == 0;
        if (strncmp(v->name, "edge_", 5) == 0 && strstr(v->name, "_out") != NULL) {
            TEST_ASSERT_EQUAL_MESSAGE(1, v->unknown, v->name); /* one refused, the repeats read */
            TEST_ASSERT_EQUAL_MESSAGE(2, v->decoded, v->name);
        }
        if (strncmp(v->name, "noise_", 6) == 0) {
            TEST_ASSERT_EQUAL_MESSAGE(0, v->decoded, v->name); /* no false decode of noise */
        }
    }
    TEST_ASSERT_EQUAL_UINT(14, edge_in);
    TEST_ASSERT_EQUAL_UINT(14, edge_out);
    TEST_ASSERT_TRUE(noise >= 16);
    TEST_ASSERT_TRUE(mutated >= 36);
    TEST_ASSERT_TRUE(omitted && ambiguous && mixed);
}

/* ---- the scenes ---------------------------------------------------------------------------- */

static size_t capture_scene(int scene, subghz_sym_t *out)
{
    subghz_cap_t cap;
    subghz_cap_init(&cap, out, SUBGHZ_RAW_MAX_SYMBOLS);
    subghz_cap_feed(&cap, k_fake_scenes[scene - 1].sym, k_fake_scenes[scene - 1].n);
    return cap.n;
}

static subghz_sym_t s_scene[SUBGHZ_RAW_MAX_SYMBOLS];

static void test_the_spec_scenes_decode_to_their_bits(void)
{
    /* the constants are the scenes' definitions in gen_subghz_vectors.py */
    size_t n = capture_scene(FAKE_SUBGHZ_SCENE_PWM24, s_scene);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(s_scene, n, &s_res));
    TEST_ASSERT_EQUAL(1, s_res.n);
    TEST_ASSERT_EQUAL(4, s_res.decoded);
    TEST_ASSERT_EQUAL(0, s_res.unknown);
    TEST_ASSERT_EQUAL_STRING("pwm", kind_of(&s_res.burst[0]));
    TEST_ASSERT_EQUAL(24, s_res.burst[0].bits);
    TEST_ASSERT_EQUAL(4, s_res.burst[0].repeats);
    TEST_ASSERT_EQUAL_UINT32(400, s_res.burst[0].short_us);
    TEST_ASSERT_EQUAL_UINT32(1200, s_res.burst[0].long_us);
    char hex[SUBGHZ_DEC_HEX_SIZE];
    subghz_dec_hex(&s_res.burst[0], hex, sizeof hex);
    TEST_ASSERT_EQUAL_STRING("a5c3e1", hex);

    n = capture_scene(FAKE_SUBGHZ_SCENE_PPM24, s_scene);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(s_scene, n, &s_res));
    TEST_ASSERT_EQUAL(1, s_res.n);
    TEST_ASSERT_EQUAL_STRING("ppm", kind_of(&s_res.burst[0]));
    TEST_ASSERT_EQUAL(24, s_res.burst[0].bits);
    TEST_ASSERT_EQUAL(4, s_res.burst[0].repeats);
    TEST_ASSERT_EQUAL_UINT32(400, s_res.burst[0].short_us);
    TEST_ASSERT_EQUAL_UINT32(1200, s_res.burst[0].long_us);
    subghz_dec_hex(&s_res.burst[0], hex, sizeof hex);
    TEST_ASSERT_EQUAL_STRING("5a3cc3", hex);

    n = capture_scene(FAKE_SUBGHZ_SCENE_MANCHESTER32, s_scene);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(s_scene, n, &s_res));
    TEST_ASSERT_EQUAL(1, s_res.n);
    TEST_ASSERT_EQUAL(3, s_res.decoded);
    TEST_ASSERT_EQUAL_STRING("manchester", kind_of(&s_res.burst[0]));
    TEST_ASSERT_EQUAL(32, s_res.burst[0].bits);
    TEST_ASSERT_EQUAL(3, s_res.burst[0].repeats);
    TEST_ASSERT_EQUAL_UINT32(500, s_res.burst[0].short_us);
    TEST_ASSERT_EQUAL_UINT32(1000, s_res.burst[0].long_us);
    subghz_dec_hex(&s_res.burst[0], hex, sizeof hex);
    TEST_ASSERT_EQUAL_STRING("c3a5f00f", hex);

    /* the two limit scenes are no frame at all */
    for (int scene = FAKE_SUBGHZ_SCENE_DENSE; scene <= FAKE_SUBGHZ_SCENE_SLOW; scene++) {
        n = capture_scene(scene, s_scene);
        TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(s_scene, n, &s_res));
        TEST_ASSERT_EQUAL(0, s_res.decoded);
        TEST_ASSERT_EQUAL(1, s_res.unknown);
    }
}

static void test_every_scene_decodes_as_the_generator_says(void)
{
    for (int scene = 1; scene <= FAKE_SUBGHZ_SCENE_COUNT; scene++) {
        size_t n = capture_scene(scene, s_scene);
        TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(s_scene, n, &s_res));
        check_result(k_fake_dec_scenes[scene - 1].name, &k_fake_dec_scenes[scene - 1], &s_res);
    }
}

/* ---- rules at their edges ----------------------------------------------------------------- */

#define PULSE_US 400

/* A pulse-distance frame (a closing pulse) of `bits` from the top of `value`, long gap
 * `long_us`, then `gap_us`, written to out; returns the symbol count. */
static size_t ppm_frame(subghz_sym_t *out, uint32_t value, unsigned bits, int long_us, int gap_us)
{
    size_t n = 0;
    for (unsigned i = 0; i < bits; i++) {
        out[n++] = PULSE_US;
        out[n++] = (value >> (bits - 1u - i)) & 1u ? -long_us : -PULSE_US;
    }
    out[n++] = PULSE_US;
    out[n++] = (subghz_sym_t)-gap_us;
    return n;
}

static void test_a_low_of_exactly_four_shortest_is_not_a_gap(void)
{
    subghz_sym_t sym[64];
    size_t n = ppm_frame(sym, 0xA5, 8, 1600, 12000);
    n += ppm_frame(sym + n, 0xA5, 8, 1600, 12000);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(sym, n, &s_res));
    TEST_ASSERT_EQUAL(2, s_res.decoded); /* 4 x 400 us: a bit, not a gap */
    TEST_ASSERT_EQUAL(0, s_res.unknown);
    TEST_ASSERT_EQUAL_UINT32(1600, s_res.burst[0].long_us);

    n = ppm_frame(sym, 0xA5, 8, 1601, 12000);
    n += ppm_frame(sym + n, 0xA5, 8, 1601, 12000);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(sym, n, &s_res));
    TEST_ASSERT_EQUAL(0, s_res.decoded); /* one µs more cuts every frame into pieces */
    TEST_ASSERT_TRUE(s_res.unknown > 2);
}

static void test_the_low_after_the_last_high_is_not_examined(void)
{
    /* a nine-bit frame without its closing pulse ends on a low: the last bit is lost */
    subghz_sym_t sym[64];
    size_t n = ppm_frame(sym, 0x1A5, 9, 1200, 400);
    n -= 2; /* the closing pulse and the gap */
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(sym, n, &s_res));
    TEST_ASSERT_EQUAL(1, s_res.decoded);
    TEST_ASSERT_EQUAL(8, s_res.burst[0].bits);

    /* with it, all nine, and a capture that ends on the closing pulse is the same */
    n = ppm_frame(sym, 0x1A5, 9, 1200, 400);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(sym, n - 1, &s_res));
    TEST_ASSERT_EQUAL(9, s_res.burst[0].bits);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(sym, n, &s_res));
    TEST_ASSERT_EQUAL(9, s_res.burst[0].bits);
}

static void test_the_same_bits_are_one_entry_with_a_count(void)
{
    subghz_sym_t sym[256];
    size_t n = 0;
    for (int i = 0; i < 5; i++) {
        n += ppm_frame(sym + n, i == 3 ? 0xA6u : 0xA5u, 8, 1200, 12000);
    }
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(sym, n, &s_res));
    TEST_ASSERT_EQUAL(5, s_res.decoded);
    TEST_ASSERT_EQUAL(2, s_res.n);
    TEST_ASSERT_EQUAL(4, s_res.burst[0].repeats); /* first seen first, the odd one after it */
    TEST_ASSERT_EQUAL(1, s_res.burst[1].repeats);
    TEST_ASSERT_EQUAL_HEX8(0xA5, s_res.burst[0].data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA6, s_res.burst[1].data[0]);
}

static void test_short_lows_that_cannot_be_a_period_remainder(void)
{
    /* eight highs of two sizes whose lows are 1 us: not pulse-width */
    subghz_sym_t sym[64];
    size_t n = 0;
    for (int i = 0; i < 9; i++) {
        sym[n++] = i % 3 == 0 ? 1000 : 100;
        sym[n++] = -1;
    }
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(sym, n, &s_res));
    TEST_ASSERT_EQUAL(0, s_res.decoded);
    TEST_ASSERT_EQUAL(1, s_res.unknown);
}

static void test_a_single_cluster_is_not_two(void)
{
    /* pulse-width highs spread within one cluster (all within a quarter of one size) */
    subghz_sym_t sym[64];
    size_t n = 0;
    for (int i = 0; i < 12; i++) {
        sym[n++] = 400 + (i % 4) * 20;
        sym[n++] = -(1200 - (i % 4) * 20);
    }
    sym[n - 1] = -12000;
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(sym, n, &s_res));
    TEST_ASSERT_EQUAL(0, s_res.decoded);
}

static void test_the_decode_does_not_touch_the_capture(void)
{
    subghz_sym_t sym[64];
    subghz_sym_t copy[64];
    size_t n = ppm_frame(sym, 0xA5, 8, 1200, 12000);
    memcpy(copy, sym, n * sizeof sym[0]);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(sym, n, &s_res));
    TEST_ASSERT_EQUAL_INT32_ARRAY(copy, sym, n);
}

/* ---- invalid and hostile input ------------------------------------------------------------ */

static void test_arguments_and_an_empty_capture(void)
{
    const subghz_sym_t one[] = {500};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_decode(one, 1, NULL));
    memset(&s_res, 0xAA, sizeof s_res);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_decode(NULL, 1, &s_res));
    TEST_ASSERT_EQUAL_UINT16(0, s_res.decoded);
    memset(&s_res, 0xAA, sizeof s_res);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(NULL, 0, &s_res)); /* an empty capture: no bursts */
    TEST_ASSERT_EQUAL_UINT16(0, s_res.decoded);
    TEST_ASSERT_EQUAL_UINT16(0, s_res.unknown);
    TEST_ASSERT_EQUAL_UINT8(0, s_res.n);
    memset(&s_res, 0xAA, sizeof s_res);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(one, 0, &s_res));
    TEST_ASSERT_EQUAL_UINT8(0, s_res.n);
}

static void test_a_capture_that_is_not_one_is_refused_and_leaves_nothing(void)
{
    static subghz_sym_t big[SUBGHZ_DEC_MAX_SYMS + 2];
    for (size_t i = 0; i < sizeof big / sizeof big[0]; i++) {
        big[i] = i % 2 == 0 ? 100 : -100;
    }
    memset(&s_res, 0xAA, sizeof s_res);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(big, SUBGHZ_DEC_MAX_SYMS, &s_res)); /* the limit */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_decode(big, SUBGHZ_DEC_MAX_SYMS + 1, &s_res));
    TEST_ASSERT_EQUAL_UINT8(0, s_res.n);
    TEST_ASSERT_EQUAL_UINT16(0, s_res.unknown);

    const subghz_sym_t zero[] = {400, 0, 400};
    const subghz_sym_t starts_low[] = {-400, 400, -400};
    const subghz_sym_t same_sign[] = {400, -400, -400};
    const subghz_sym_t same_high[] = {400, 400};
    const subghz_sym_t too_long_high[] = {1000001, -400};
    const subghz_sym_t too_long_low[] = {400, -1000001};
    const subghz_sym_t min[] = {INT32_MIN};
    const subghz_sym_t min_low[] = {400, INT32_MIN};
    const subghz_sym_t max[] = {INT32_MAX, -400};
    const struct {
        const subghz_sym_t *sym;
        size_t n;
    } bad[] = {
        {zero, 3},         {starts_low, 3}, {same_sign, 3}, {same_high, 2}, {too_long_high, 2},
        {too_long_low, 2}, {min, 1},        {min_low, 2},   {max, 2},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memset(&s_res, 0xAA, sizeof s_res);
        TEST_ASSERT_EQUAL_MESSAGE(STARK_ERR_INVALID_ARG,
                                  subghz_decode(bad[i].sym, bad[i].n, &s_res), "bad capture");
        TEST_ASSERT_EQUAL_UINT8(0, s_res.n);
        TEST_ASSERT_EQUAL_UINT16(0, s_res.decoded);
        TEST_ASSERT_EQUAL_UINT16(0, s_res.omitted);
    }

    /* the largest legal values are fine */
    const subghz_sym_t edge[] = {1000000, -1000000, 1000000, -1000000, 1, -1};
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(edge, 6, &s_res));
}

static void test_no_two_alike_means_nothing_to_split_on(void)
{
    const subghz_sym_t sym[] = {100, -300, 900, -2700};
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(sym, 4, &s_res));
    TEST_ASSERT_EQUAL(0, s_res.decoded);
    TEST_ASSERT_EQUAL(0, s_res.unknown); /* no burst was ever delimited */
}

/* ---- ten thousand seeded random captures -------------------------------------------------- */

static uint32_t s_rng = 0x6040604u;

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static uint32_t rnd_between(uint32_t lo, uint32_t hi)
{
    return lo + rnd() % (hi - lo + 1u);
}

/* A random alternating capture of n symbols whose magnitudes come from `mode`. */
static void random_capture(subghz_sym_t *sym, size_t n, unsigned mode)
{
    static const int32_t scale[] = {10, 100, 1000, 10000, 1000000};
    int32_t top = scale[rnd() % 5u];
    for (size_t i = 0; i < n; i++) {
        int32_t mag;
        switch (mode) {
            case 0: /* any size up to the scale */
                mag = (int32_t)rnd_between(1u, (uint32_t)top);
                break;
            case 1: /* two sizes */
                mag = rnd() % 2u == 0 ? top : top / 3 + 1;
                break;
            case 2: /* mostly one size, sometimes anything */
                mag = rnd() % 8u == 0 ? (int32_t)rnd_between(1u, 1000000u) : top;
                break;
            default: /* the extremes */
                mag = rnd() % 2u == 0 ? 1 : 1000000;
                break;
        }
        sym[i] = i % 2 == 0 ? mag : -mag;
    }
}

static void fuzz_one(subghz_sym_t *sym, size_t n)
{
    subghz_dec_result_t first;
    /* an exactly sized heap copy: any read past n is the sanitizer's to find */
    subghz_sym_t *tight = malloc(n * sizeof *tight);
    TEST_ASSERT_NOT_NULL(tight);
    memcpy(tight, sym, n * sizeof *tight);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(tight, n, &first));
    check_invariants(&first, n);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(tight, n, &s_res));
    TEST_ASSERT_EQUAL_MEMORY(&first, &s_res, sizeof first); /* the same input, the same result */
    TEST_ASSERT_EQUAL_INT32_ARRAY(sym, tight, n);           /* and the input untouched */
    free(tight);
}

static void test_ten_thousand_seeded_random_captures(void)
{
    static subghz_sym_t sym[SUBGHZ_DEC_MAX_SYMS];
    for (unsigned iter = 0; iter < 10000u; iter++) {
        size_t n;
        if (iter % 4u == 0) {
            /* a vector with a few symbols changed to anything */
            const fake_dec_vector_t *v = &k_fake_dec_vectors[rnd() % FAKE_DEC_VECTOR_COUNT];
            n = v->n;
            memcpy(sym, v->sym, n * sizeof sym[0]);
            for (unsigned k = rnd() % 4u; k > 0; k--) {
                size_t at = rnd() % n;
                int32_t mag = rnd() % 2u == 0 ? (int32_t)rnd_between(1u, 20000u) : 1000000;
                sym[at] = sym[at] > 0 ? mag : -mag;
            }
        } else {
            n = iter % 7u == 0 ? SUBGHZ_DEC_MAX_SYMS : rnd_between(1u, 400u);
            random_capture(sym, n, (iter / 4u) % 4u);
        }
        fuzz_one(sym, n);
    }
}

static void test_hostile_streams_stay_bounded(void)
{
    static subghz_sym_t sym[SUBGHZ_DEC_MAX_SYMS];
    /* all one size: one burst of 2047 equal symbols, then a capture of 2048 alternating
     * extremes, then 2048 symbols of 1 us */
    for (size_t i = 0; i < SUBGHZ_DEC_MAX_SYMS; i++) {
        sym[i] = i % 2 == 0 ? 500 : -500;
    }
    fuzz_one(sym, SUBGHZ_DEC_MAX_SYMS);
    for (size_t i = 0; i < SUBGHZ_DEC_MAX_SYMS; i++) {
        sym[i] = i % 2 == 0 ? 1000000 : -1000000;
    }
    fuzz_one(sym, SUBGHZ_DEC_MAX_SYMS);
    for (size_t i = 0; i < SUBGHZ_DEC_MAX_SYMS; i++) {
        sym[i] = i % 2 == 0 ? 1 : -1;
    }
    fuzz_one(sym, SUBGHZ_DEC_MAX_SYMS);
    /* 1 us and 1 s alternately, in both phases */
    for (size_t i = 0; i < SUBGHZ_DEC_MAX_SYMS; i++) {
        bool high = i % 2 == 0;
        sym[i] = (i / 2) % 2 == 0 ? (high ? 1 : -1) : (high ? 1000000 : -1000000);
    }
    fuzz_one(sym, SUBGHZ_DEC_MAX_SYMS);
    /* a thousand tiny bursts: more bursts than result entries, none decodes */
    size_t n = 0;
    for (int b = 0; b < 400; b++) {
        sym[n++] = 100;
        sym[n++] = -100;
        sym[n++] = 100;
        sym[n++] = -1000;
        if (n > SUBGHZ_DEC_MAX_SYMS - 4) {
            break;
        }
    }
    fuzz_one(sym, n);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_decode(sym, n, &s_res));
    TEST_ASSERT_EQUAL(0, s_res.decoded);
    TEST_ASSERT_TRUE(s_res.unknown > SUBGHZ_DEC_MAX_BURSTS);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_kind_names);
    RUN_TEST(test_hex_text);
    RUN_TEST(test_every_vector_decodes_as_the_reference_says);
    RUN_TEST(test_twenty_jittered_vectors_per_kind_decode_to_their_bits);
    RUN_TEST(test_the_vector_set_covers_the_cases_that_matter);
    RUN_TEST(test_the_spec_scenes_decode_to_their_bits);
    RUN_TEST(test_every_scene_decodes_as_the_generator_says);
    RUN_TEST(test_a_low_of_exactly_four_shortest_is_not_a_gap);
    RUN_TEST(test_the_low_after_the_last_high_is_not_examined);
    RUN_TEST(test_the_same_bits_are_one_entry_with_a_count);
    RUN_TEST(test_short_lows_that_cannot_be_a_period_remainder);
    RUN_TEST(test_a_single_cluster_is_not_two);
    RUN_TEST(test_the_decode_does_not_touch_the_capture);
    RUN_TEST(test_arguments_and_an_empty_capture);
    RUN_TEST(test_a_capture_that_is_not_one_is_refused_and_leaves_nothing);
    RUN_TEST(test_no_two_alike_means_nothing_to_split_on);
    RUN_TEST(test_ten_thousand_seeded_random_captures);
    RUN_TEST(test_hostile_streams_stay_bounded);
    return UNITY_END();
}
