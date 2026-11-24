/*
 * spiapp.h — private to the SPI app (TASKS.md STARK-0706, ADR-0023 §6): the rows of the setup
 * editor and the text they show, built from the editor's configuration and what stark_lab returned.
 * spi_views.c holds no service call and no UI call (pure, host-tested to every line); app_spi.c
 * drives the service and the screens.
 *
 * The editor is a list of rows (a menu cannot take UP and DOWN for a value), like the I2C app's:
 * each byte is two nibbles, the Nibble row says which one LEFT and RIGHT change, and a nibble wraps
 * inside itself, so every byte is at most 15 steps from any other. The three presets load the bytes
 * of a common first look at a SPI flash and move the selection to Send.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_lab.h"

#define SPIAPP_BYTES_MAX   STARK_LAB_SPI_MAX /* bytes of one transaction: the service's limit */
#define SPIAPP_PRESETS     3u
#define SPIAPP_RESULT_PER  8u                                     /* bytes per result row */
#define SPIAPP_RESULT_ROWS (SPIAPP_BYTES_MAX / SPIAPP_RESULT_PER) /* per view: hex, then text */
/* clock, mode, presets, bytes, nibble, the bytes, send, note, and a hex and a text row per eight */
#define SPIAPP_ROWS_MAX    (2u + SPIAPP_PRESETS + 2u + SPIAPP_BYTES_MAX + 2u + 2u * SPIAPP_RESULT_ROWS)

typedef struct {
    uint8_t clock; /* an index into stark_lab_spi_clock(), 0..STARK_LAB_SPI_CLOCKS - 1 */
    uint8_t mode;  /* SPI mode 0..3 */
    uint8_t count; /* bytes to send, 1..32 */
    bool low_nibble;
    uint8_t data[SPIAPP_BYTES_MAX];
} spiapp_cfg_t;

typedef enum {
    SPIROW_CLOCK,
    SPIROW_MODE,
    SPIROW_PRESET, /* action: load preset `idx` (JEDEC ID, Read status, Dummy read) */
    SPIROW_COUNT,  /* the byte count */
    SPIROW_NIBBLE, /* which nibble LEFT/RIGHT change */
    SPIROW_BYTE,   /* byte `idx` */
    SPIROW_SEND,   /* action: send (asks first) */
    SPIROW_NOTE,   /* a dimmed reminder, never selected */
    SPIROW_HEX,    /* result bytes `idx * 8` as hex (dimmed, never selected) */
    SPIROW_TEXT    /* ... and as ASCII */
} spiview_kind_t;

typedef struct {
    spiview_kind_t kind;
    uint8_t idx;
} spiview_row_t;

/* 1 MHz, mode 0, the JEDEC ID preset: 9f 00 00 00. */
void spiview_cfg_reset(spiapp_cfg_t *c);

/* The clock the configuration names, in hertz. */
uint32_t spiview_hz(const spiapp_cfg_t *c);

/* Loads preset 0 JEDEC ID (9f + 3 bytes), 1 Read status (05 + 1) or 2 Dummy read (4 x 00); the
 * clock and mode stay. False for a preset past the list. */
bool spiview_preset(spiapp_cfg_t *c, size_t preset);

/* The rows, top to bottom; `result_len` is how many bytes the last transaction returned (0: none
 * yet), shown below the note as a hex row and a text row per eight. Returns how many (at most
 * SPIAPP_ROWS_MAX). */
size_t spiview_rows(const spiapp_cfg_t *c, size_t result_len, spiview_row_t *out);

/* The label of a row: a constant, never NULL. The note's text is spiview_note(). */
const char *spiview_label(const spiview_row_t *row);
const char *spiview_note(void);

/*
 * The value column: written into `buf` (or a constant), NULL for a row with none. `result` holds
 * `result_len` bytes; `status` is the text beside the Send row ("" for none, "ok", "slot busy",
 * ...).
 */
const char *spiview_value(const spiview_row_t *row, const spiapp_cfg_t *c, const uint8_t *result,
                          size_t result_len, const char *status, char *buf, size_t n);

/* LEFT (-1) / RIGHT (+1) on a row: true when the configuration changed. The clock, mode and count
 * clamp at their ends; a nibble wraps. Actions, notes and results do not adjust. */
bool spiview_adjust(spiapp_cfg_t *c, const spiview_row_t *row, int dir);

/* A short text for what a service call returned: "slot busy", "refused", "bus error", ... */
const char *spiview_error_text(stark_err_t err);

/* The home list's value column for a slot: the module's name when it can be probed, else the
 * reason it cannot. */
const char *spiview_slot_value(const stark_lab_spi_slot_t *s);

/* The confirmation text (at most 159 bytes): the slot, the clock, the mode and the bytes. */
void spiview_message(uint8_t slot, const spiapp_cfg_t *c, char *buf, size_t n);
