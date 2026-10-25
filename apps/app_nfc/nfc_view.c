/*
 * nfc_view.c — the NFC app's tag view (TASKS.md STARK-0404): the module
 * state, or the present tag's technology, UID, ATQA/SAK and size, one row per
 * NDEF record, and `Write URI` — enabled only for a writable Type 2 tag.
 */
#include <stdio.h>
#include "ndef.h"
#include "nfcapp.h"
#include "stark_log.h"

#define ROWS_MAX   (5 + NDEF_MAX_RECORDS + 2)
#define LABEL_SIZE 40

static stark_nfc_read_t s_read;
static bool s_has_tag;
static ui_menu_item_t s_items[ROWS_MAX];
static char s_labels[NDEF_MAX_RECORDS + 1][LABEL_SIZE];
static char s_values[4][24];
static const char *s_value_of[ROWS_MAX];
static size_t s_count;
static size_t s_write_row = (size_t)-1;
static size_t s_save_row = (size_t)-1;

void nfcview_refresh(void)
{
    s_has_tag = stark_nfc_take(&s_read) == STARK_OK;
}

void nfcview_log_records(void)
{
    ndef_message_t m;
    if (!s_has_tag || !s_read.ndef_present ||
        ndef_parse(s_read.ndef, s_read.ndef_len, &m) != STARK_OK) {
        return;
    }
    for (uint8_t i = 0; i < m.count; i++) {
        char line[64];
        ndef_describe(&m.rec[i], line, sizeof line);
        STARK_LOGI("nfcapp", "record %s", line);
    }
}

static void row(const char *label, const char *value, bool enabled)
{
    s_items[s_count] = (ui_menu_item_t){.label = label, .disabled = !enabled};
    s_value_of[s_count++] = value;
}

const stark_nfc_read_t *nfcview_writable(const char **why)
{
    if (!s_has_tag) {
        *why = "no tag";
    } else if (s_read.tag.pages == 0 || s_read.ndef_capacity == 0) {
        *why = "not NDEF";
    } else if (!s_read.writable) {
        *why = "read-only";
    } else if (s_read.read_err != STARK_OK) {
        *why = "read again";
    } else {
        return &s_read;
    }
    return NULL;
}

static void tag_rows(void)
{
    const nfc_tag_t *t = &s_read.tag;
    size_t n = 0;
    for (unsigned i = 0; i < t->uid_len; i++) {
        n += (size_t)snprintf(s_values[0] + n, sizeof s_values[0] - n, "%02X", t->uid[i]);
    }
    snprintf(s_values[1], sizeof s_values[1], "%04X/%02X", (unsigned)t->atqa, (unsigned)t->sak);
    snprintf(s_values[2], sizeof s_values[2], "%u pages", (unsigned)t->pages);
    snprintf(s_values[3], sizeof s_values[3], "%s",
             s_read.read_err == STARK_OK ? "read" : stark_err_str(s_read.read_err));
    row(nfc_tech_name(t->tech), s_values[3], false);
    row("UID", s_values[0], false);
    row("ATQA/SAK", s_values[1], false);
    row("Size", t->pages > 0 ? s_values[2] : "-", false);
    ndef_message_t m;
    if (s_read.ndef_present && s_read.ndef_len == 0) {
        row("NDEF: empty", NULL, false);
    } else if (s_read.ndef_present && ndef_parse(s_read.ndef, s_read.ndef_len, &m) == STARK_OK) {
        for (uint8_t i = 0; i < m.count; i++) {
            ndef_describe(&m.rec[i], s_labels[i], LABEL_SIZE);
            row(s_labels[i], NULL, false);
        }
    } else if (s_read.ndef_present) {
        row("NDEF: malformed", NULL, false);
    } else if (t->pages > 0) {
        row("No NDEF", NULL, false);
    }
}

const ui_menu_item_t *nfcview_items(size_t *count)
{
    s_count = 0;
    s_write_row = (size_t)-1;
    s_save_row = (size_t)-1;
    switch (stark_nfc_module()) {
        case STARK_NFC_READY:
            if (s_has_tag) {
                tag_rows();
            } else {
                row("Hold a tag near", NULL, false);
            }
            break;
        case STARK_NFC_LOST:
            row("NFC module lost", "retrying", false);
            *count = s_count;
            return s_items;
        case STARK_NFC_ABSENT:
        case STARK_NFC_NOT_FITTED:
        default:
            row("No NFC module", stark_nfc_module() == STARK_NFC_ABSENT ? "absent" : "not fitted",
                false);
            *count = s_count;
            return s_items;
    }
    const char *why = NULL;
    bool writable = nfcview_writable(&why) != NULL;
    s_write_row = s_count;
    row("Write URI", writable ? NULL : why, writable);
    s_save_row = s_count;
    row("Save dump", s_has_tag ? NULL : "no tag", s_has_tag);
    *count = s_count;
    return s_items;
}

const char *nfcview_value(size_t index)
{
    return index < s_count ? s_value_of[index] : NULL;
}

bool nfcview_is_write_row(size_t index)
{
    return index == s_write_row;
}

bool nfcview_is_save_row(size_t index)
{
    return index == s_save_row;
}
