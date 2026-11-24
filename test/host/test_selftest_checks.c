/*
 * test_selftest_checks.c — the ten self-test checks (TASKS.md STARK-0707) against a healthy and a
 * broken environment, one broken call at a time: a corrupted RAM word, a heap at its floor, a flash
 * size mismatch, a digest mismatch, a read error, NVS unreadable, a card that fails at each step of
 * the write, a module that declares and does not ACK, a drop count of one, a stack of 128 B. Also
 * the SHA-256 against FIPS vectors, the image-length walk, and the table (order, kinds, and the
 * places the app relies on). The flash model has no write or erase to call, and its contents are
 * compared before and after: the check reads and nothing else.
 */
#include <stdio.h>
#include <string.h>
#include "selftest_checks.h"
#include "selftest_env_fake.h"
#include "selftest_sha256.h"
#include "selftestapp.h"
#include "unity.h"

static char ev[STARK_SELFTEST_EVIDENCE + 1];

void setUp(void)
{
    fenv_reset();
    selftest_checks_bind(&fenv_env);
    memset(ev, 0, sizeof ev);
}
void tearDown(void)
{}

static stark_selftest_result_t run(stark_selftest_result_t (*check)(char *, size_t))
{
    memset(ev, 0, sizeof ev);
    stark_selftest_result_t r = check(ev, sizeof ev);
    TEST_ASSERT_TRUE_MESSAGE(strlen(ev) <= STARK_SELFTEST_EVIDENCE, "evidence too long");
    return r;
}

/* ---- the table -------------------------------------------------------------------------- */

static void test_the_table_is_ten_checks_in_the_documented_order(void)
{
    size_t n = 0;
    const stark_selftest_check_t *t = selftest_checks_table(&n);
    TEST_ASSERT_EQUAL_size_t(10, n);
    TEST_ASSERT_EQUAL_size_t(SELFTEST_COUNT, n);
    static const char *const k_names[10] = {
        "RAM",     "Flash",   "Settings", "Storage",   "Module bus", "Events and stacks",
        "Buttons", "Display", "Buzzer",   "Status LED"};
    for (size_t i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_STRING(k_names[i], t[i].name);
        /* the automated ones run on the worker; the four the app observes have no run */
        TEST_ASSERT_EQUAL(i < SELFTEST_AUTOMATED, t[i].run != NULL);
        /* only what the operator judges is attested: display, buzzer, LED */
        TEST_ASSERT_EQUAL(i >= SELFTEST_DISPLAY, t[i].attested);
        TEST_ASSERT_TRUE(strlen(t[i].name) < 24u); /* a label and a log tag */
    }
    TEST_ASSERT_EQUAL_size_t(STAPP_BUTTONS, SELFTEST_BUTTONS);
    TEST_ASSERT_EQUAL_size_t(STAPP_DISPLAY, SELFTEST_DISPLAY);
    TEST_ASSERT_EQUAL_size_t(STAPP_BUZZER, SELFTEST_BUZZER);
    TEST_ASSERT_EQUAL_size_t(STAPP_LED, SELFTEST_LED);
    TEST_ASSERT_EQUAL_size_t(STAPP_BUTTONS, SELFTEST_AUTOMATED);
    TEST_ASSERT_NOT_NULL(selftest_checks_table(NULL));
}

static void test_every_check_says_so_when_no_environment_is_bound(void)
{
    selftest_checks_bind(NULL);
    stark_selftest_result_t (*checks[])(char *, size_t) = {
        selftest_check_ram,     selftest_check_flash,   selftest_check_settings,
        selftest_check_storage, selftest_check_modules, selftest_check_events};
    for (size_t i = 0; i < sizeof checks / sizeof checks[0]; i++) {
        TEST_ASSERT_EQUAL(ST_FAIL, run(checks[i]));
        TEST_ASSERT_EQUAL_STRING("no environment", ev);
    }
}

/* ---- 1. RAM ----------------------------------------------------------------------------- */

static void test_ram_passes_on_a_healthy_machine_and_gives_the_block_back(void)
{
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_ram));
    TEST_ASSERT_EQUAL_STRING("27500 B", ev); /* a quarter of the largest free block */
    TEST_ASSERT_EQUAL_UINT(1, fenv.allocs);
    TEST_ASSERT_EQUAL_UINT(1, fenv.frees);
    TEST_ASSERT_EQUAL_size_t(0, fenv.allocated);
    TEST_ASSERT_TRUE(fenv.min_free_seen >= SELFTEST_HEAP_FLOOR);
    TEST_ASSERT_EQUAL_UINT(13, fenv.dwells); /* 8 walking ones, 4 constants, addresses */
    fenv.largest = 200000;
    fenv.allocs = fenv.frees = 0;
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_ram));
    TEST_ASSERT_EQUAL_STRING("32768 B", ev); /* never more than 32 kB */
}

static void test_ram_never_takes_free_heap_below_128_kb(void)
{
    for (size_t free_total = SELFTEST_HEAP_FLOOR - 4096u;
         free_total <= SELFTEST_HEAP_FLOOR + 70000u; free_total += 997u) {
        fenv_reset();
        selftest_checks_bind(&fenv_env);
        fenv.free_total = free_total;
        fenv.largest = free_total;
        stark_selftest_result_t r = run(selftest_check_ram);
        TEST_ASSERT_EQUAL_size_t(0, fenv.allocated);
        TEST_ASSERT_EQUAL_UINT(fenv.allocs, fenv.frees);
        if (fenv.allocs > 0u) {
            TEST_ASSERT_TRUE(fenv.min_free_seen >= SELFTEST_HEAP_FLOOR);
            TEST_ASSERT_TRUE(fenv.last_alloc <= SELFTEST_RAM_BLOCK_MAX);
            TEST_ASSERT_TRUE(fenv.last_alloc <= fenv.largest / 4u);
            TEST_ASSERT_EQUAL_size_t(0, fenv.last_alloc % 4u);
            TEST_ASSERT_EQUAL(ST_PASS, r);
        } else {
            TEST_ASSERT_EQUAL(ST_FAIL, r);
            TEST_ASSERT_NOT_NULL(strstr(ev, "heap too low"));
        }
    }
}

static void test_ram_shrinks_to_what_the_floor_allows(void)
{
    fenv.free_total = SELFTEST_HEAP_FLOOR + 4096u;
    fenv.largest = 200000;
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_ram));
    TEST_ASSERT_EQUAL_STRING("4096 B", ev);
    TEST_ASSERT_EQUAL_size_t(SELFTEST_HEAP_FLOOR, fenv.min_free_seen); /* to the floor, no lower */
}

static void test_ram_fails_when_there_is_no_room_for_a_useful_test(void)
{
    fenv.free_total = SELFTEST_HEAP_FLOOR;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_ram));
    TEST_ASSERT_EQUAL_STRING("heap too low: 131072 B free", ev);
    TEST_ASSERT_EQUAL_UINT(0, fenv.allocs);
    fenv.free_total = SELFTEST_HEAP_FLOOR + 1000u; /* under 1 kB of room */
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_ram));
    TEST_ASSERT_EQUAL_UINT(0, fenv.allocs);
    fenv.free_total = 226356;
    fenv.largest = 3000; /* fragmented: a quarter of it is 750 B */
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_ram));
    TEST_ASSERT_EQUAL_UINT(0, fenv.allocs);
}

static void test_ram_reports_an_allocation_failure(void)
{
    fenv.alloc_fails = true;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_ram));
    TEST_ASSERT_EQUAL_STRING("no block of 27500 B", ev);
    TEST_ASSERT_EQUAL_UINT(0, fenv.frees);
}

static void test_ram_notices_another_task_taking_memory_meanwhile(void)
{
    fenv.free_total = SELFTEST_HEAP_FLOOR + 40000u;
    fenv.largest = 200000;
    fenv.steal_on_alloc = 20000; /* the block fits, but then the heap is under the floor */
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_ram));
    TEST_ASSERT_EQUAL_STRING("heap fell below 128 kB", ev);
    TEST_ASSERT_EQUAL_UINT(1, fenv.frees);
    TEST_ASSERT_EQUAL_UINT(0, fenv.dwells); /* no pattern was written */
}

static void test_ram_finds_a_stuck_bit_in_every_pass(void)
{
    for (unsigned call = 1; call <= 13; call++) {
        fenv_reset();
        selftest_checks_bind(&fenv_env);
        fenv.stuck_offset = 100;
        fenv.stuck_mask = 0x10;
        fenv.stuck_on_call = call;
        TEST_ASSERT_EQUAL_MESSAGE(ST_FAIL, run(selftest_check_ram), "a corrupted word goes unseen");
        if (call < 13u) {
            TEST_ASSERT_NOT_NULL(strstr(ev, "bad byte +100 read "));
        } else {
            TEST_ASSERT_NOT_NULL(strstr(ev, "bad word +100 read "));
        }
        TEST_ASSERT_EQUAL_UINT(call, fenv.dwells); /* the run stops at the first bad pass */
        TEST_ASSERT_EQUAL_UINT(fenv.allocs, fenv.frees);
        TEST_ASSERT_EQUAL_size_t(0, fenv.allocated);
    }
}

static void test_ram_finds_a_corruption_at_the_first_and_the_last_byte(void)
{
    fenv.stuck_mask = 0x80;
    fenv.stuck_offset = 0;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_ram));
    TEST_ASSERT_NOT_NULL(strstr(ev, "+0 "));
    fenv.stuck_offset = 27499;
    fenv.dwells = 0;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_ram));
    TEST_ASSERT_NOT_NULL(strstr(ev, "+27499 "));
}

/* ---- SHA-256 ---------------------------------------------------------------------------- */

static void hex(const uint8_t d[32], char out[65])
{
    for (size_t i = 0; i < 32; i++) {
        snprintf(out + 2 * i, 3, "%02x", d[i]);
    }
}

static void sha(const void *data, size_t n, char out[65])
{
    selftest_sha256_t c;
    uint8_t d[32];
    selftest_sha256_init(&c);
    selftest_sha256_update(&c, data, n);
    selftest_sha256_final(&c, d);
    hex(d, out);
}

static void test_sha256_matches_the_fips_vectors(void)
{
    char h[65];
    sha("abc", 3, h);
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", h);
    sha("", 0, h);
    TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", h);
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha(two, strlen(two), h);
    TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", h);
    static char million[1000000];
    memset(million, 'a', sizeof million);
    sha(million, sizeof million, h);
    TEST_ASSERT_EQUAL_STRING("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", h);
}

static void test_sha256_pads_correctly_around_the_block_boundaries(void)
{
    static const struct {
        size_t n;
        const char *digest;
    } k[] = {
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
        {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
        {121, "e9615320128cc7a3d6078e9af05603188e5ccbf0d07d8b735d3df5e8e0c1281f"},
    };
    uint8_t msg[200];
    memset(msg, 'a', sizeof msg);
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        char h[65];
        sha(msg, k[i].n, h);
        TEST_ASSERT_EQUAL_STRING(k[i].digest, h);
        /* the same message fed in pieces of every size is the same digest */
        for (size_t piece = 1; piece < 131; piece += 13) {
            selftest_sha256_t c;
            uint8_t d[32];
            selftest_sha256_init(&c);
            for (size_t off = 0; off < k[i].n; off += piece) {
                size_t take = k[i].n - off < piece ? k[i].n - off : piece;
                selftest_sha256_update(&c, msg + off, take);
            }
            selftest_sha256_final(&c, d);
            hex(d, h);
            TEST_ASSERT_EQUAL_STRING(k[i].digest, h);
        }
    }
}

/* ---- 2. Flash --------------------------------------------------------------------------- */

static void test_flash_passes_with_two_read_only_passes_over_the_image(void)
{
    static uint8_t before[FENV_FLASH_BYTES];
    memcpy(before, fenv.flash, sizeof before);
    uint32_t len = fenv_put_image(3, 4000, true);
    memcpy(before, fenv.flash, sizeof before);
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_flash));
    TEST_ASSERT_EQUAL_MEMORY(before, fenv.flash, sizeof before); /* nothing was written */
    char want[STARK_SELFTEST_EVIDENCE + 1];
    char h[65];
    sha(fenv.flash + FENV_APP_OFFSET, len, h);
    snprintf(want, sizeof want, "4 MB, image %lu B, sha %.8s", (unsigned long)len, h);
    TEST_ASSERT_EQUAL_STRING(want, ev);
    TEST_ASSERT_TRUE(fenv.bytes_read >= 2u * len);
    TEST_ASSERT_TRUE(fenv.bytes_read < 2u * len + 200u);
    TEST_ASSERT_EQUAL_UINT32(FENV_APP_OFFSET + len, fenv.max_offset_read);
    TEST_ASSERT_TRUE(fenv.max_offset_read <= FENV_APP_OFFSET + FENV_APP_SIZE);
}

static void test_flash_reads_only_the_image_even_when_the_partition_is_larger(void)
{
    (void)fenv_put_image(1, 100, false);
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_flash));
    TEST_ASSERT_TRUE(fenv.max_offset_read < FENV_APP_OFFSET + 400u);
}

static void test_flash_accepts_the_other_chip_sizes_the_header_can_name(void)
{
    for (unsigned code = 0; code <= 5; code++) {
        fenv_reset();
        selftest_checks_bind(&fenv_env);
        fenv.flash[3] = (uint8_t)(code << 4);
        fenv.chip_size = 1024u * 1024u << code;
        TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_flash));
        char want[16];
        snprintf(want, sizeof want, "%u MB, image", 1u << code);
        TEST_ASSERT_EQUAL_MEMORY(want, ev, strlen(want));
    }
}

static void test_flash_fails_on_a_chip_that_is_not_the_size_the_image_was_built_for(void)
{
    fenv.chip_size = 2u * 1024u * 1024u;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_flash));
    TEST_ASSERT_EQUAL_STRING("chip 2 MB, header 4 MB", ev);
    TEST_ASSERT_TRUE(fenv.reads <= 1u); /* no hashing after the size check failed */
}

static void test_flash_fails_when_the_size_or_the_header_cannot_be_read(void)
{
    fenv.chip_size_fails = true;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_flash));
    TEST_ASSERT_EQUAL_STRING("chip size unreadable", ev);
    fenv.chip_size_fails = false;
    fenv.read_fail_at = 0;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_flash));
    TEST_ASSERT_EQUAL_STRING("header unreadable", ev);
    fenv.read_fail_at = UINT32_MAX;
    fenv.flash[0] = 0x00;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_flash));
    TEST_ASSERT_EQUAL_STRING("no image header at 0", ev);
    fenv.flash[0] = 0xE9;
    fenv.flash[3] = 0x90;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_flash));
    TEST_ASSERT_EQUAL_STRING("unknown size code 9", ev);
}

static void test_flash_fails_without_a_running_partition_or_a_valid_image(void)
{
    fenv.app_fails = true;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_flash));
    TEST_ASSERT_EQUAL_STRING("no running partition", ev);
    fenv.app_fails = false;
    fenv.flash[FENV_APP_OFFSET] = 0x00;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_flash));
    TEST_ASSERT_EQUAL_STRING("image header invalid", ev);
}

static void test_flash_fails_on_a_read_error_in_either_pass(void)
{
    uint32_t len = fenv_put_image(3, 4000, true);
    fenv.read_fail_at = FENV_APP_OFFSET + len / 2u;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_flash));
    TEST_ASSERT_EQUAL_STRING("flash read failed", ev);
}

static void test_flash_fails_when_the_two_passes_disagree(void)
{
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_flash));
    uint32_t pass_reads = (fenv.reads - 5u) / 2u;
    fenv_reset();
    selftest_checks_bind(&fenv_env);
    fenv.corrupt_after_reads = 5u + pass_reads + 3u; /* the second pass reads a flipped bit */
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_flash));
    TEST_ASSERT_EQUAL_STRING("two passes differ", ev);
}

static void test_image_length_walks_header_segments_padding_and_digest(void)
{
    for (unsigned segs = 1; segs <= 16; segs++) {
        for (uint32_t bytes = 0; bytes < 70; bytes += 7) {
            for (int digest = 0; digest < 2; digest++) {
                uint32_t want = fenv_put_image(segs, bytes, digest != 0);
                uint32_t got = 0;
                TEST_ASSERT_EQUAL(STARK_OK, selftest_image_length(&fenv_env, FENV_APP_OFFSET,
                                                                  FENV_APP_SIZE, &got));
                TEST_ASSERT_EQUAL_UINT32(want, got);
                TEST_ASSERT_EQUAL_UINT32(0, (want - (digest ? 32u : 0u)) % 16u);
            }
        }
    }
}

static void test_image_length_rejects_what_is_not_an_image(void)
{
    uint32_t got = 99;
    uint32_t want = fenv_put_image(3, 4000, true);
    /* the limit is the partition: an image longer than it, or a limit that cuts a segment */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      selftest_image_length(&fenv_env, FENV_APP_OFFSET, want - 1u, &got));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      selftest_image_length(&fenv_env, FENV_APP_OFFSET, 24u + 8u + 100u, &got));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      selftest_image_length(&fenv_env, FENV_APP_OFFSET, 30u, &got));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_image_length(&fenv_env, FENV_APP_OFFSET, want, &got));
    TEST_ASSERT_EQUAL_UINT32(want, got);
    got = 99;
    /* a segment header that claims four gigabytes */
    fenv.flash[FENV_APP_OFFSET + 24 + 7] = 0xFF;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      selftest_image_length(&fenv_env, FENV_APP_OFFSET, FENV_APP_SIZE, &got));
    (void)fenv_put_image(3, 4000, true);
    fenv.flash[FENV_APP_OFFSET + 1] = 0; /* no segments */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      selftest_image_length(&fenv_env, FENV_APP_OFFSET, FENV_APP_SIZE, &got));
    fenv.flash[FENV_APP_OFFSET + 1] = 17; /* too many */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      selftest_image_length(&fenv_env, FENV_APP_OFFSET, FENV_APP_SIZE, &got));
    fenv.flash[FENV_APP_OFFSET + 1] = 3;
    fenv.flash[FENV_APP_OFFSET] = 0xE8; /* not the magic */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      selftest_image_length(&fenv_env, FENV_APP_OFFSET, FENV_APP_SIZE, &got));
    fenv.flash[FENV_APP_OFFSET] = 0xE9;
    /* reads that fail: the header, then a segment header */
    fenv.read_fail_at = FENV_APP_OFFSET;
    TEST_ASSERT_EQUAL(STARK_ERR_IO,
                      selftest_image_length(&fenv_env, FENV_APP_OFFSET, FENV_APP_SIZE, &got));
    fenv.read_fail_at = FENV_APP_OFFSET + 24u + 3u;
    TEST_ASSERT_EQUAL(STARK_ERR_IO,
                      selftest_image_length(&fenv_env, FENV_APP_OFFSET, FENV_APP_SIZE, &got));
    TEST_ASSERT_EQUAL_UINT32(99, got); /* *length is untouched on every failure */
}

/* ---- 3. Settings ------------------------------------------------------------------------ */

static void test_settings_pass_with_valid_values_whatever_the_store_says(void)
{
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_settings));
    TEST_ASSERT_EQUAL_STRING("4 settings valid, from nvs", ev);
    fenv.source = STARK_SETTINGS_FROM_DEFAULTS;
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_settings));
    TEST_ASSERT_EQUAL_STRING("4 settings valid, from defaults", ev);
}

static void test_settings_fail_when_the_store_is_unusable_or_a_value_is_wrong(void)
{
    fenv.source = STARK_SETTINGS_FROM_RAM; /* NVS could not be opened */
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_settings));
    TEST_ASSERT_EQUAL_STRING("store unavailable, RAM only", ev);
    fenv.source = STARK_SETTINGS_FROM_NVS;
    fenv.no_desc = true;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_settings));
    TEST_ASSERT_EQUAL_STRING("setting 0 has no descriptor", ev);
    fenv.no_desc = false;
    fenv.unstable = true;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_settings));
    TEST_ASSERT_EQUAL_STRING("brightness reads back differently", ev);
    fenv.unstable = false;
    static const struct {
        int key;
        uint8_t value;
        const char *text;
    } k_bad[] = {{STARK_SET_BRIGHTNESS, 105, "brightness=105 is invalid"},
                 {STARK_SET_BRIGHTNESS, 0, "brightness=0 is invalid"},
                 {STARK_SET_BRIGHTNESS, 15, "brightness=15 is invalid"},
                 {STARK_SET_VOLUME, 4, "volume=4 is invalid"},
                 {STARK_SET_REPEAT, 3, "repeat=3 is invalid"},
                 {STARK_SET_LOG_LEVEL, 200, "loglvl=200 is invalid"}};
    for (size_t i = 0; i < sizeof k_bad / sizeof k_bad[0]; i++) {
        fenv_reset();
        selftest_checks_bind(&fenv_env);
        fenv.setting[k_bad[i].key] = k_bad[i].value;
        TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_settings));
        TEST_ASSERT_EQUAL_STRING(k_bad[i].text, ev);
    }
}

/* ---- 4. Storage ------------------------------------------------------------------------- */

static void test_storage_writes_reads_back_and_deletes_a_4_kb_file(void)
{
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_storage));
    TEST_ASSERT_EQUAL_STRING("4096 B written, read back, deleted", ev);
    TEST_ASSERT_EQUAL_UINT(0, fenv_file_count()); /* nothing is left on the card */
    TEST_ASSERT_EQUAL_UINT(16, fenv.writes);
    TEST_ASSERT_EQUAL_UINT(1, fenv.removes);
    TEST_ASSERT_EQUAL_INT(0, fenv.handles_open);
}

static void test_storage_skips_without_a_card_fails_for_a_lost_one_and_says_what_it_found(void)
{
    static const struct {
        stark_storage_state_t state;
        stark_selftest_result_t result;
        const char *text;
    } k[] = {{STARK_STORAGE_NOT_FITTED, ST_SKIP, "not fitted"},
             {STARK_STORAGE_ABSENT, ST_SKIP, "no card"},
             {STARK_STORAGE_EJECTED, ST_SKIP, "ejected"},
             /* a card that was mounted and went away, or answers and holds no volume, is a fault */
             {STARK_STORAGE_UNUSABLE, ST_FAIL, "unusable"},
             {STARK_STORAGE_REMOVED, ST_FAIL, "removed"}};
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        fenv.state = k[i].state;
        TEST_ASSERT_EQUAL(k[i].result, run(selftest_check_storage));
        TEST_ASSERT_EQUAL_STRING(k[i].text, ev);
    }
    TEST_ASSERT_EQUAL_UINT(0, fenv.opens); /* nothing is touched without a mounted card */
}

static void test_storage_fails_at_every_step_and_leaves_no_file(void)
{
    static const struct {
        fenv_card_fault_t fault;
        unsigned nth;
        const char *text;
    } k[] = {{FENV_OPEN_WRITE, 0, "open failed: I/O error"},
             {FENV_WRITE, 0, "write failed: I/O error"},
             {FENV_WRITE, 7, "write failed: I/O error"},
             {FENV_WRITE, 15, "write failed: I/O error"},
             {FENV_CLOSE, 0, "close failed: I/O error"},
             {FENV_OPEN_READ, 0, "reopen failed: I/O error"},
             {FENV_READ, 0, "read failed: I/O error"},
             {FENV_SHORT_READ, 0, "short read at 3996"},
             {FENV_BAD_READ, 0, "read-back differs in block 1"}};
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        fenv_reset();
        selftest_checks_bind(&fenv_env);
        fenv.fault = k[i].fault;
        fenv.fail_nth = k[i].nth;
        TEST_ASSERT_EQUAL_MESSAGE(ST_FAIL, run(selftest_check_storage), k[i].text);
        TEST_ASSERT_EQUAL_STRING(k[i].text, ev);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fenv_file_count(), k[i].text);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, fenv.handles_open, k[i].text); /* no handle leaked */
    }
}

static void test_storage_reports_a_file_it_cannot_delete(void)
{
    fenv.fault = FENV_REMOVE;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_storage));
    TEST_ASSERT_EQUAL_STRING("delete failed: I/O error", ev);
}

static void test_storage_names_a_read_back_that_differs_even_when_delete_also_fails(void)
{
    fenv.fault = FENV_BAD_READ;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_storage));
    TEST_ASSERT_NOT_NULL(strstr(ev, "differs"));
}

static void test_storage_with_a_full_card_names_the_failing_write(void)
{
    for (int i = 0; i < FENV_FILES; i++) {
        fenv.files[i].used = true;
        snprintf(fenv.files[i].name, sizeof fenv.files[i].name, "other%d.bin", i);
    }
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_storage));
    TEST_ASSERT_NOT_NULL(strstr(ev, "close failed"));
}

/* ---- 5. Module bus ---------------------------------------------------------------------- */

static void test_modules_pass_when_the_table_agrees_with_the_probes(void)
{
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_modules));
    TEST_ASSERT_EQUAL_STRING("table agrees with probes, 2 slots", ev);
    TEST_ASSERT_EQUAL_UINT(2, fenv.probes);     /* the two slots; the providers have no EEPROM */
    fenv.entries[0].state = STARK_MODULE_KNOWN; /* a module that answers */
    fenv.probe_result[0] = STARK_OK;
    fenv.entries[1].state = STARK_MODULE_GENERIC;
    fenv.probe_result[1] = STARK_OK;
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_modules));
    fenv.entries[1].state = STARK_MODULE_CONFLICT;
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_modules));
}

static void test_modules_fail_for_a_module_that_declares_and_does_not_ack(void)
{
    fenv.entries[0].state = STARK_MODULE_KNOWN;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_modules));
    TEST_ASSERT_EQUAL_STRING("slot 0 present, 0x50 no ACK", ev);
    fenv_reset();
    selftest_checks_bind(&fenv_env);
    fenv.entries[1].state = STARK_MODULE_GENERIC;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_modules));
    TEST_ASSERT_EQUAL_STRING("slot 1 present, 0x51 no ACK", ev);
}

static void test_modules_fail_for_an_empty_slot_that_acks(void)
{
    fenv.probe_result[1] = STARK_OK;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_modules));
    TEST_ASSERT_EQUAL_STRING("slot 1 empty, 0x51 ACKs", ev);
}

static void test_modules_look_twice_when_the_table_changes_under_them(void)
{
    fenv.probe_result[0] =
        STARK_OK; /* a module was fitted a moment ago: the table is a second old */
    fenv.change_after_list = 1;
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_modules));
    TEST_ASSERT_EQUAL_UINT(2, fenv.lists);
}

static void test_modules_do_not_retry_a_bus_fault(void)
{
    fenv.probe_result[0] = STARK_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_modules));
    TEST_ASSERT_EQUAL_STRING("probe 0x50 failed: Timeout", ev);
    TEST_ASSERT_EQUAL_UINT(1, fenv.probes);
    fenv.probes = 0;
    fenv.probe_result[0] = STARK_ERR_NOT_FOUND;
    fenv.probe_result[1] = STARK_ERR_IO;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_modules));
    TEST_ASSERT_EQUAL_STRING("probe 0x51 failed: I/O error", ev);
}

static void test_modules_skip_without_a_bus_or_without_a_slot_to_check(void)
{
    fenv.probe_result[0] = STARK_ERR_STATE;
    TEST_ASSERT_EQUAL(ST_SKIP, run(selftest_check_modules));
    TEST_ASSERT_EQUAL_STRING("no I2C bus", ev);
    fenv.probe_result[0] = STARK_ERR_NOT_SUPPORTED;
    TEST_ASSERT_EQUAL(ST_SKIP, run(selftest_check_modules));
    fenv.probe_result[0] = STARK_ERR_NOT_FOUND;
    fenv.no_eeprom_addr = true;
    TEST_ASSERT_EQUAL(ST_SKIP, run(selftest_check_modules));
    TEST_ASSERT_EQUAL_STRING("no slot with an ID EEPROM", ev);
    fenv.no_eeprom_addr = false;
    fenv.n_entries = 0;
    TEST_ASSERT_EQUAL(ST_SKIP, run(selftest_check_modules));
    fenv.n_entries = STARK_MODULE_ENTRIES;
    fenv.list_overreport = 7; /* a service that counts more entries than it was given room for */
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_modules));
    TEST_ASSERT_EQUAL_STRING("table agrees with probes, 2 slots", ev);
}

/* ---- 6. Events and stacks --------------------------------------------------------------- */

static void test_events_pass_with_no_drops_and_roomy_stacks(void)
{
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_events));
    TEST_ASSERT_EQUAL_STRING("drops 0, 3 tasks, min stack 700 B", ev);
    fenv.n_tasks = 16;
    for (size_t i = 3; i < 16; i++) {
        snprintf(fenv.tasks[i].name, sizeof fenv.tasks[i].name, "task%u", (unsigned)i);
        fenv.tasks[i].stack_free = 256; /* exactly the minimum is enough */
    }
    TEST_ASSERT_EQUAL(ST_PASS, run(selftest_check_events));
    TEST_ASSERT_EQUAL_STRING("drops 0, 16 tasks, min stack 256 B", ev);
}

static void test_events_fail_on_a_drop_a_thin_stack_or_a_missing_snapshot(void)
{
    fenv.snap.ev_dropped = 1;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_events));
    TEST_ASSERT_EQUAL_STRING("1 events dropped", ev);
    fenv.snap.ev_dropped = 0;
    fenv.tasks[1].stack_free = 128;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_events));
    TEST_ASSERT_EQUAL_STRING("app_worker stack 128 B", ev);
    fenv.tasks[1].stack_free = 255;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_events));
    fenv.tasks[1].stack_free = 1800;
    fenv.snap_fails = true;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_events));
    TEST_ASSERT_EQUAL_STRING("no diagnostics snapshot", ev);
    fenv.snap_fails = false;
    fenv.n_tasks = 0;
    TEST_ASSERT_EQUAL(ST_FAIL, run(selftest_check_events));
    TEST_ASSERT_EQUAL_STRING("no task table", ev);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_table_is_ten_checks_in_the_documented_order);
    RUN_TEST(test_every_check_says_so_when_no_environment_is_bound);
    RUN_TEST(test_ram_passes_on_a_healthy_machine_and_gives_the_block_back);
    RUN_TEST(test_ram_never_takes_free_heap_below_128_kb);
    RUN_TEST(test_ram_shrinks_to_what_the_floor_allows);
    RUN_TEST(test_ram_fails_when_there_is_no_room_for_a_useful_test);
    RUN_TEST(test_ram_reports_an_allocation_failure);
    RUN_TEST(test_ram_notices_another_task_taking_memory_meanwhile);
    RUN_TEST(test_ram_finds_a_stuck_bit_in_every_pass);
    RUN_TEST(test_ram_finds_a_corruption_at_the_first_and_the_last_byte);
    RUN_TEST(test_sha256_matches_the_fips_vectors);
    RUN_TEST(test_sha256_pads_correctly_around_the_block_boundaries);
    RUN_TEST(test_flash_passes_with_two_read_only_passes_over_the_image);
    RUN_TEST(test_flash_reads_only_the_image_even_when_the_partition_is_larger);
    RUN_TEST(test_flash_accepts_the_other_chip_sizes_the_header_can_name);
    RUN_TEST(test_flash_fails_on_a_chip_that_is_not_the_size_the_image_was_built_for);
    RUN_TEST(test_flash_fails_when_the_size_or_the_header_cannot_be_read);
    RUN_TEST(test_flash_fails_without_a_running_partition_or_a_valid_image);
    RUN_TEST(test_flash_fails_on_a_read_error_in_either_pass);
    RUN_TEST(test_flash_fails_when_the_two_passes_disagree);
    RUN_TEST(test_image_length_walks_header_segments_padding_and_digest);
    RUN_TEST(test_image_length_rejects_what_is_not_an_image);
    RUN_TEST(test_settings_pass_with_valid_values_whatever_the_store_says);
    RUN_TEST(test_settings_fail_when_the_store_is_unusable_or_a_value_is_wrong);
    RUN_TEST(test_storage_writes_reads_back_and_deletes_a_4_kb_file);
    RUN_TEST(test_storage_skips_without_a_card_fails_for_a_lost_one_and_says_what_it_found);
    RUN_TEST(test_storage_fails_at_every_step_and_leaves_no_file);
    RUN_TEST(test_storage_reports_a_file_it_cannot_delete);
    RUN_TEST(test_storage_names_a_read_back_that_differs_even_when_delete_also_fails);
    RUN_TEST(test_storage_with_a_full_card_names_the_failing_write);
    RUN_TEST(test_modules_pass_when_the_table_agrees_with_the_probes);
    RUN_TEST(test_modules_fail_for_a_module_that_declares_and_does_not_ack);
    RUN_TEST(test_modules_fail_for_an_empty_slot_that_acks);
    RUN_TEST(test_modules_look_twice_when_the_table_changes_under_them);
    RUN_TEST(test_modules_do_not_retry_a_bus_fault);
    RUN_TEST(test_modules_skip_without_a_bus_or_without_a_slot_to_check);
    RUN_TEST(test_events_pass_with_no_drops_and_roomy_stacks);
    RUN_TEST(test_events_fail_on_a_drop_a_thin_stack_or_a_missing_snapshot);
    return UNITY_END();
}
