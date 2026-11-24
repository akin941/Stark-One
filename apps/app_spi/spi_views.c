/*
 * spi_views.c — the SPI app's setup rows and texts (spiapp.h, TASKS.md STARK-0706): which rows a
 * view shows, what each says and what LEFT/RIGHT does to it. Pure: no service call, no UI call, no
 * allocation; every string is written into the caller's buffer or is a constant.
 */
#include <stdio.h>
#include <string.h>
#include "spiapp.h"

static const char *const k_hex_labels[SPIAPP_RESULT_ROWS] = {"MISO 0-7", "MISO 8-15", "MISO 16-23",
                                                             "MISO 24-31"};
static const char *const k_text_labels[SPIAPP_RESULT_ROWS] = {"Text 0-7", "Text 8-15", "Text 16-23",
                                                              "Text 24-31"};
static const char *const k_preset_labels[SPIAPP_PRESETS] = {"JEDEC ID", "Read status",
                                                            "Dummy read"};
static const char *const k_preset_values[SPIAPP_PRESETS] = {"9f + 3", "05 + 1", "4 x 00"};

void spiview_cfg_reset(spiapp_cfg_t *c)
{
    *c = (spiapp_cfg_t){.clock = 3u, .low_nibble = true}; /* 1 MHz */
    (void)spiview_preset(c, 0);
}

uint32_t spiview_hz(const spiapp_cfg_t *c)
{
    return stark_lab_spi_clock(c->clock);
}

bool spiview_preset(spiapp_cfg_t *c, size_t preset)
{
    if (preset >= SPIAPP_PRESETS) {
        return false;
    }
    memset(c->data, 0, sizeof c->data);
    if (preset == 0u) {
        c->data[0] = 0x9Fu; /* the JEDEC ID command, then three bytes clocked in */
        c->count = 4u;
    } else if (preset == 1u) {
        c->data[0] = 0x05u; /* read status register, then one byte */
        c->count = 2u;
    } else {
        c->count = 4u; /* four zero bytes: what an idle line gives back */
    }
    return true;
}

size_t spiview_rows(const spiapp_cfg_t *c, size_t result_len, spiview_row_t *out)
{
    size_t n = 0;
    out[n++] = (spiview_row_t){SPIROW_CLOCK, 0};
    out[n++] = (spiview_row_t){SPIROW_MODE, 0};
    for (size_t i = 0; i < SPIAPP_PRESETS; i++) {
        out[n++] = (spiview_row_t){SPIROW_PRESET, (uint8_t)i};
    }
    out[n++] = (spiview_row_t){SPIROW_COUNT, 0};
    out[n++] = (spiview_row_t){SPIROW_NIBBLE, 0};
    for (size_t i = 0; i < c->count && i < SPIAPP_BYTES_MAX; i++) {
        out[n++] = (spiview_row_t){SPIROW_BYTE, (uint8_t)i};
    }
    out[n++] = (spiview_row_t){SPIROW_SEND, 0};
    out[n++] = (spiview_row_t){SPIROW_NOTE, 0};
    size_t rows = (result_len + SPIAPP_RESULT_PER - 1u) / SPIAPP_RESULT_PER;
    for (size_t i = 0; i < rows && i < SPIAPP_RESULT_ROWS; i++) {
        out[n++] = (spiview_row_t){SPIROW_HEX, (uint8_t)i};
        out[n++] = (spiview_row_t){SPIROW_TEXT, (uint8_t)i};
    }
    return n;
}

const char *spiview_label(const spiview_row_t *row)
{
    switch (row->kind) {
        case SPIROW_CLOCK:
            return "Clock";
        case SPIROW_MODE:
            return "Mode";
        case SPIROW_PRESET:
            return k_preset_labels[row->idx % SPIAPP_PRESETS];
        case SPIROW_COUNT:
            return "Bytes";
        case SPIROW_NIBBLE:
            return "Nibble";
        case SPIROW_BYTE:
            return "Byte"; /* NOTE: its number is in the value column: one string, not 32 */
        case SPIROW_SEND:
            return "Send";
        case SPIROW_HEX:
            return k_hex_labels[row->idx % SPIAPP_RESULT_ROWS];
        case SPIROW_TEXT:
            return k_text_labels[row->idx % SPIAPP_RESULT_ROWS];
        case SPIROW_NOTE:
        default:
            return "";
    }
}

const char *spiview_note(void)
{
    return "Own hardware only";
}

const char *spiview_error_text(stark_err_t err)
{
    if (err == STARK_ERR_BUSY) {
        return "slot busy";
    }
    if (err == STARK_ERR_NOT_SUPPORTED) {
        return "refused";
    }
    if (err == STARK_ERR_NOT_FOUND) {
        return "no module";
    }
    if (err == STARK_ERR_INVALID_ARG) {
        return "bad value";
    }
    return err == STARK_ERR_STATE ? "not confirmed" : "bus error";
}

const char *spiview_slot_value(const stark_lab_spi_slot_t *s)
{
    return s->eligible ? s->name : s->why;
}

/* The bytes of one result row as text: printable ASCII as it is, anything else as a dot. */
static void ascii_row(const uint8_t *b, size_t n, char *buf, size_t sz)
{
    size_t len = 0;
    for (size_t i = 0; i < n && len + 1u < sz; i++) {
        buf[len++] = (b[i] >= 0x20u && b[i] <= 0x7Eu) ? (char)b[i] : '.';
    }
    if (sz > 0u) {
        buf[len] = '\0';
    }
}

const char *spiview_value(const spiview_row_t *row, const spiapp_cfg_t *c, const uint8_t *result,
                          size_t result_len, const char *status, char *buf, size_t n)
{
    switch (row->kind) {
        case SPIROW_CLOCK:
            return stark_lab_spi_clock_text(spiview_hz(c));
        case SPIROW_MODE:
            (void)snprintf(buf, n, "%u", (unsigned)c->mode);
            return buf;
        case SPIROW_PRESET:
            return k_preset_values[row->idx % SPIAPP_PRESETS];
        case SPIROW_COUNT:
            (void)snprintf(buf, n, "%u", (unsigned)c->count);
            return buf;
        case SPIROW_NIBBLE:
            return c->low_nibble ? "low" : "high";
        case SPIROW_BYTE:
            (void)snprintf(buf, n, "%u: 0x%02x", (unsigned)(row->idx % SPIAPP_BYTES_MAX),
                           (unsigned)c->data[row->idx % SPIAPP_BYTES_MAX]);
            return buf;
        case SPIROW_SEND:
            return status[0] != '\0' ? status : NULL;
        case SPIROW_HEX:
        case SPIROW_TEXT: {
            size_t from = (size_t)row->idx * SPIAPP_RESULT_PER;
            size_t left = from < result_len ? result_len - from : 0u;
            size_t take = left < SPIAPP_RESULT_PER ? left : SPIAPP_RESULT_PER;
            if (row->kind == SPIROW_HEX) {
                (void)stark_lab_i2c_hex(result + from, take, buf, n);
            } else {
                ascii_row(result + from, take, buf, n);
            }
            return buf;
        }
        case SPIROW_NOTE:
        default:
            return NULL;
    }
}

/* One nibble of a byte moves by one, wrapping inside itself: the other nibble is not touched. */
static uint8_t nibble_step(uint8_t b, bool low, int dir)
{
    unsigned shift = low ? 0u : 4u;
    unsigned nib = (b >> shift) & 0xFu;
    nib = (nib + (dir > 0 ? 1u : 15u)) & 0xFu;
    return (uint8_t)((b & ~(0xFu << shift)) | (nib << shift));
}

/* A value moves by one inside [lo, hi] and stays at the end it reached. */
static uint8_t clamp_step(uint8_t v, int dir, uint8_t lo, uint8_t hi)
{
    if (dir > 0) {
        return v < hi ? (uint8_t)(v + 1u) : v;
    }
    return v > lo ? (uint8_t)(v - 1u) : v;
}

bool spiview_adjust(spiapp_cfg_t *c, const spiview_row_t *row, int dir)
{
    if (dir == 0) {
        return false;
    }
    uint8_t before;
    switch (row->kind) {
        case SPIROW_CLOCK:
            before = c->clock;
            c->clock = clamp_step(c->clock, dir, 0u, (uint8_t)(STARK_LAB_SPI_CLOCKS - 1u));
            return c->clock != before;
        case SPIROW_MODE:
            before = c->mode;
            c->mode = clamp_step(c->mode, dir, 0u, 3u);
            return c->mode != before;
        case SPIROW_COUNT:
            before = c->count;
            c->count = clamp_step(c->count, dir, 1u, (uint8_t)SPIAPP_BYTES_MAX);
            return c->count != before;
        case SPIROW_NIBBLE:
            c->low_nibble = !c->low_nibble;
            return true;
        case SPIROW_BYTE: {
            uint8_t *b = &c->data[row->idx % SPIAPP_BYTES_MAX];
            *b = nibble_step(*b, c->low_nibble, dir);
            return true;
        }
        case SPIROW_PRESET:
        case SPIROW_SEND:
        case SPIROW_NOTE:
        case SPIROW_HEX:
        case SPIROW_TEXT:
        default:
            return false;
    }
}

void spiview_message(uint8_t slot, const spiapp_cfg_t *c, char *buf, size_t n)
{
    char data[3u * SPIAPP_BYTES_MAX];
    (void)stark_lab_i2c_hex(c->data, c->count, data, sizeof data);
    (void)snprintf(buf, n, "Slot %u, %s, mode %u: %s. Own hardware only.", (unsigned)slot,
                   stark_lab_spi_clock_text(spiview_hz(c)), (unsigned)c->mode, data);
}
