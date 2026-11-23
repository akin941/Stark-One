/*
 * fake_cc1101.h — a test-only CC1101 as seen over SPI (TASKS.md STARK-0601,
 * ADR-0022 §4). Pure C, linked by the host tests and by the emulator test
 * image's substitute stark_subghz_transport; never by the production firmware.
 *
 * It answers transfers, not API calls: the status byte on every transfer,
 * single and burst register access (0x00..0x2E), PATABLE, the status registers
 * 0x30..0x3D (burst bit), and the strobes SRES, SRX, STX, SIDLE, SFRX, SFTX and
 * SNOP driving MARCSTATE. The RSSI is derived from an injected "air" (one carrier
 * and its level) at the tuned frequency. Its data lines replay injected symbol
 * scenes on GDO2 while it is in RX and record what GDO0 carries while it is in
 * TX; an "own receiver" switch feeds the recording back into GDO2. Electrical
 * and RF behaviour are not modelled, and nothing here is a recording of a real
 * device.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_subghz_transport.h" /* subghz_sym_t */

#define FAKE_CC1101_SCENE_MAX         2100 /* symbols: a transmission is at most 2048 */
#define FAKE_CC1101_FREQ_LOG          64
#define FAKE_CC1101_AIR_HALF_WIDTH_HZ 12500u /* STARK-0602; it was 50 kHz in STARK-0601 */

typedef enum {
    FAKE_CC1101_NONE = 0,
    FAKE_CC1101_NO_ANSWER, /* MISO reads all 0xFF, nothing changes */
    FAKE_CC1101_STUCK,     /* MARCSTATE never changes (it keeps what it was) */
    FAKE_CC1101_BIT_FLIP,  /* the first data byte of a read has bit 0 flipped */
    FAKE_CC1101_SPI_ERROR, /* the transfer fails */
    FAKE_CC1101_FAULT_COUNT
} fake_cc1101_fault_t;

void fake_cc1101_reset(void); /* power-on: IDLE, datasheet defaults, a CC1101 0x00/0x14, no air */

/* Which chip answers: PARTNUM and VERSION. fake_cc1101_present(false): nothing answers. */
void fake_cc1101_chip(uint8_t partnum, uint8_t version);
void fake_cc1101_present(bool present);

/* The next count transfers fail this way; -1 until cleared, 0 (or NONE) clears. */
void fake_cc1101_fault(fake_cc1101_fault_t kind, int count);

/*
 * The air: one carrier and its level in dBm. The RSSI reads that level while the
 * tuned frequency is within FAKE_CC1101_AIR_HALF_WIDTH_HZ of carrier_hz (half a scan
 * channel: a carrier occupies exactly one 25 kHz channel), and -110 dBm elsewhere.
 * carrier_hz 0: silence.
 */
void fake_cc1101_air(uint32_t carrier_hz, int level_dbm);

/* One chip-select-framed transfer. */
stark_err_t fake_cc1101_xfer(const uint8_t *tx, uint8_t *rx, size_t len);

/* ---- GDO lines ------------------------------------------------------------ */

/*
 * A scene GDO2 plays once each time the chip enters RX. The fake keeps the pointer, not a
 * copy (STARK-0603: the catalogue lives in flash), so the caller keeps the symbols alive.
 * fake_cc1101_gdo2_take() hands the scene out in pieces of at most `max` (0 when not in RX
 * or nothing is left), as a receiver's frames arrive.
 */
void fake_cc1101_scene(const subghz_sym_t *s, size_t n);
size_t fake_cc1101_gdo2_take(subghz_sym_t *out, size_t max);
/* GDO0 while in TX is recorded (up to FAKE_CC1101_SCENE_MAX); false unless in TX. */
bool fake_cc1101_gdo0_put(const subghz_sym_t *s, size_t n);
size_t fake_cc1101_gdo0_recorded(const subghz_sym_t **out);
void fake_cc1101_own_rx(bool on); /* a recording is also played on GDO2 */

/* ---- observation ---------------------------------------------------------- */

typedef struct {
    uint8_t marcstate;       /* what MARCSTATE reads */
    uint32_t transfers;      /* since reset */
    uint32_t tuned_hz;       /* from FREQ2..0 */
    uint32_t freq_writes;    /* transfers that wrote FREQ2..0 */
    uint32_t out_of_band;    /* of those, how many left the tuned word outside 433.05–434.79 */
    uint32_t patable_writes; /* bytes written to PATABLE */
    uint8_t patable_max;     /* the largest byte ever written to PATABLE */
    uint32_t patable_other;  /* bytes written that were neither 0x00 nor 0x60 */
    uint32_t tx_strobes;     /* STX seen */
    uint32_t gdo0_symbols;   /* symbols GDO0 carried while in TX (STARK-0605), since reset */
    uint32_t gdo0_refused;   /* symbols offered on GDO0 while not in TX (they went nowhere) */
    uint8_t reg[0x2F];       /* the configuration registers */
    uint8_t patable[8];
} fake_cc1101_stats_t;

const fake_cc1101_stats_t *fake_cc1101_stats(void);
/* The tuned frequency each FREQ write left behind, oldest first (the last FAKE_CC1101_FREQ_LOG). */
size_t fake_cc1101_freq_log(const uint32_t **out);
