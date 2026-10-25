/*
 * test_ui_nfc.c — the real NFC app on the host UI test port (TASKS.md
 * STARK-0404) with a fake stark_nfc: no module (nothing polled), waiting, a
 * tag with its NDEF records, identification-only and read-only tags (Write
 * disabled), the exact confirmation text, cancel writes nothing, write done /
 * failed / refused, BACK from the payloads to the view, Save dump refused
 * without a card and saved as nfc_NNNN.nfc with one (STARK-0405), and
 * polling stops with the app. Frames pinned after inspection.
 */
#include <stdio.h>
#include <string.h>
#include "ndef.h"
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "ui_port_nfc.h"
#include "ui_port_storage.h"
#include "nfc_dump.h"
#include "unity.h"

extern const stark_app_t app_nfc;
static const stark_app_t *const k_apps[] = {&app_nfc};
static stark_nfc_read_t s_tag;

void setUp(void)
{}
void tearDown(void)
{}

static void frame(void)
{
    stark_ui_tick();
}

static void press(stark_key_t k)
{
    ui_port_short(k);
    frame();
}

static void pin(const char *name, uint64_t want)
{
    uint64_t h = ui_port_fb_hash();
    if (h != want) {
        char path[64];
        snprintf(path, sizeof path, "ui_nfc_%s.ppm", name);
        (void)ui_port_dump_ppm(path);
        printf("nfc %s hash 0x%016llx (see %s)\n", name, (unsigned long long)h, path);
    }
    TEST_ASSERT_EQUAL_HEX64(want, h);
}

/* An NTAG213-like tag: 7-byte UID, an NDEF message of the given records. */
static void make_tag(nfc_tech_t tech, uint8_t sak, bool writable, const ndef_record_t *recs,
                     size_t n)
{
    memset(&s_tag, 0, sizeof s_tag);
    static const uint8_t uid[] = {0x04, 0x30, 0x3B, 0x46, 0x51, 0x5C, 0x67};
    memcpy(s_tag.tag.uid, uid, sizeof uid);
    s_tag.tag.uid_len = sizeof uid;
    s_tag.tag.atqa = 0x0044;
    s_tag.tag.sak = sak;
    s_tag.tag.tech = tech;
    s_tag.tag.pages = sak == 0x00 ? 45 : 0;
    s_tag.writable = writable;
    s_tag.ndef_capacity = sak == 0x00 ? 141 : 0;
    if (recs != NULL) {
        s_tag.ndef_present = true;
        s_tag.ndef_len = (uint16_t)ndef_encode(recs, n, s_tag.ndef, sizeof s_tag.ndef);
    }
}

static void test_no_module(void)
{
    hal_host_set_now_us(0);
    ui_port_nfc_reset();
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("nfc"));
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("nfcapp: no module"));
    TEST_ASSERT_FALSE(ui_port_nfc_polling());
    pin("no_module", UINT64_C(0x510f4add15a0e62b));
    press(STARK_KEY_OK); /* nothing selectable */
    TEST_ASSERT_EQUAL(0, ui_port_nfc_writes());
    press(STARK_KEY_BACK);
}

static void test_tag_view_and_write(void)
{
    ui_port_nfc_reset();
    ui_port_nfc_set_module(STARK_NFC_READY);
    ui_port_reset_records();
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("nfc"));
    frame();
    TEST_ASSERT_TRUE(ui_port_nfc_polling()); /* the field is on while the app is open */
    pin("waiting", UINT64_C(0xdcb8c0a88062f85a));

    uint8_t p1[64];
    uint8_t p2[64];
    ndef_record_t recs[2];
    TEST_ASSERT_EQUAL(STARK_OK,
                      ndef_uri_make("https://example.com/stark", p1, sizeof p1, &recs[0]));
    TEST_ASSERT_EQUAL(STARK_OK, ndef_text_make("en", "STARK ONE lab", p2, sizeof p2, &recs[1]));
    make_tag(NFC_TECH_NTAG213, 0x00, true, recs, 2);
    ui_port_nfc_set_tag(&s_tag);
    frame();
    pin("ntag_two_records", UINT64_C(0x85a29a240c605177));

    /* Write URI is the only selectable row */
    press(STARK_KEY_OK);
    press(STARK_KEY_OK); /* https://example.com/stark */
    TEST_ASSERT_TRUE(
        ui_port_log_contains("nfcapp: confirm 04303B46515C67: URI https://example.com/stark (22 B "
                             "of 141). Own tags only."));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Write NFC tag?\""));
    press(STARK_KEY_BACK); /* cancel */
    TEST_ASSERT_TRUE(ui_port_log_contains("nfcapp: write cancelled"));
    TEST_ASSERT_EQUAL(0, ui_port_nfc_writes());

    press(STARK_KEY_OK);
    press(STARK_KEY_OK); /* confirm */
    frame();
    TEST_ASSERT_EQUAL(1, ui_port_nfc_writes());
    size_t len = 0;
    const uint8_t *msg = ui_port_nfc_last_write(&len);
    ndef_message_t m;
    char uri[48];
    TEST_ASSERT_EQUAL(STARK_OK, ndef_parse(msg, len, &m));
    TEST_ASSERT_EQUAL(STARK_OK, ndef_uri_get(&m.rec[0], uri, sizeof uri));
    TEST_ASSERT_EQUAL_STRING("https://example.com/stark", uri);
    TEST_ASSERT_TRUE(ui_port_log_contains("nfcapp: wrote 04303B46515C67"));

    /* back in the view: the Text payload this time, and the write fails */
    ui_port_nfc_write_outcome(STARK_ERR_NO_SPACE, STARK_OK);
    press(STARK_KEY_OK);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN); /* Text: STARK ONE lab */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("Text en: STARK ONE lab (20 B of 141)"));
    press(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("nfcapp: write failed: "));

    /* the service refuses (a write still pending) */
    ui_port_nfc_write_outcome(STARK_OK, STARK_ERR_BUSY);
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("nfcapp: write refused: "));
    TEST_ASSERT_EQUAL(2, ui_port_nfc_writes());
    press(STARK_KEY_BACK); /* the payloads -> the view, not the launcher */
    TEST_ASSERT_TRUE(ui_port_nfc_polling());

    /* Save dump (STARK-0405): no card, then a card and the worker */
    ui_port_storage_set(STARK_STORAGE_ABSENT, 0, 0);
    frame();
    press(STARK_KEY_DOWN); /* Write URI -> Save dump */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("nfcapp: save refused: no card"));
    ui_port_storage_clear_files();
    ui_port_storage_set(STARK_STORAGE_MOUNTED, 1u << 20, 1u << 20);
    ui_port_worker_inline(true);
    frame();
    press(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("nfcapp: saved nfc_0001.nfc"));
    const char *text = ui_port_storage_file("nfc_0001.nfc");
    TEST_ASSERT_NOT_NULL(text);
    static nfc_dump_t back;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_dump_parse(text, strlen(text), &back));
    TEST_ASSERT_EQUAL(45, back.n_pages);
    TEST_ASSERT_EQUAL_HEX8(44, back.mem[44 * 4]);
    press(STARK_KEY_DOWN); /* the view was rebuilt on DUMP_DONE: selection on Write URI */
    press(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("nfcapp: saved nfc_0002.nfc"));
    ui_port_worker_inline(false);
}

static void test_tags_that_cannot_be_written(void)
{
    make_tag(NFC_TECH_CLASSIC_1K, 0x08, false, NULL, 0);
    s_tag.tag.atqa = 0x0004;
    ui_port_nfc_set_tag(&s_tag);
    frame();
    pin("classic", UINT64_C(0xb30c1b59d6ffe276));
    ui_port_reset_records();
    press(STARK_KEY_OK); /* nothing selectable */
    TEST_ASSERT_FALSE(ui_port_log_contains("nfcapp: confirm"));

    uint8_t p[64];
    ndef_record_t r;
    TEST_ASSERT_EQUAL(STARK_OK, ndef_text_make("en", "STARK ONE lab", p, sizeof p, &r));
    make_tag(NFC_TECH_NTAG213, 0x00, false, &r, 1); /* read-only */
    ui_port_nfc_set_tag(&s_tag);
    frame();
    pin("read_only", UINT64_C(0x3fd72cfab4a69f26));
    press(STARK_KEY_OK);
    TEST_ASSERT_FALSE(ui_port_log_contains("nfcapp: confirm"));

    make_tag(NFC_TECH_NTAG213, 0x00, true, NULL, 0); /* no NDEF yet, but formatted */
    s_tag.read_err = STARK_ERR_NOT_FOUND;            /* the read stopped */
    ui_port_nfc_set_tag(&s_tag);
    frame();
    press(STARK_KEY_OK);
    TEST_ASSERT_FALSE(ui_port_log_contains("nfcapp: confirm"));

    ui_port_nfc_set_tag(NULL); /* left */
    frame();
    ui_port_nfc_set_module(STARK_NFC_LOST);
    frame();
    pin("module_lost", UINT64_C(0x1f6ef515d1b10322));
    press(STARK_KEY_BACK);
    TEST_ASSERT_FALSE(ui_port_nfc_polling()); /* polling stops with the app */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_module);
    RUN_TEST(test_tag_view_and_write);
    RUN_TEST(test_tags_that_cannot_be_written);
    return UNITY_END();
}
