/*
 * cc1101_drv.h — the CC1101 driver over an injected SPI transfer (TASKS.md
 * STARK-0601, ADR-0022). Pure: the caller supplies the bus and the clock, and
 * the host tests supply a fake chip.
 *
 * Every call is bounded: a transfer count of its own, and every wait ends at a
 * deadline on the injected clock or after a fixed number of polls, whichever
 * comes first, so a chip that never answers gives STARK_ERR_TIMEOUT, never a
 * hang. STX is never a generic strobe: cc1101_strobe() refuses it, and the only way to
 * transmit is cc1101_tx_start() (STARK-0605), which re-checks the band and the power table on
 * the chip itself. PATABLE is only ever written from cc1101_patable().
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cc1101_regs.h"
#include "stark_err.h"

#define CC1101_RESET_TIMEOUT_MS 5u
#define CC1101_IDLE_TIMEOUT_MS  5u
#define CC1101_MAX_POLLS        16u /* a wait never polls more often, whatever the clock says */
#define CC1101_TX_TIMEOUT_MS    5u  /* STX to MARCSTATE TX (the calibration runs first) */
#define CC1101_XFER_MAX         (1u + CC1101_PATABLE_SIZE)

typedef struct {
    /* One chip-select-framed transfer of len bytes; rx may equal tx's length. */
    stark_err_t (*xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
    uint32_t (*now_ms)(void *ctx);
    void (*delay_ms)(void *ctx, uint32_t ms);
    void *ctx;
} cc1101_io_t;

typedef struct {
    cc1101_io_t io;
    uint8_t status;         /* the last status byte the chip sent */
    bool configured;        /* a profile has been written since the last reset */
    cc1101_preset_t preset; /* the profile that was written (valid while configured) */
    uint32_t freq_hz;       /* ... and its frequency */
} cc1101_t;

void cc1101_bind(cc1101_t *d, const cc1101_io_t *io);

/* SRES, then MARCSTATE IDLE within 5 ms; STARK_ERR_TIMEOUT otherwise. Forgets the profile. */
stark_err_t cc1101_reset(cc1101_t *d);

/*
 * Reads PARTNUM and VERSION. STARK_OK for 0x00 and 0x14 or 0x04, else
 * STARK_ERR_NOT_FOUND; both bytes are returned either way (what answered). A
 * transfer error is returned as it is, with the outputs left at 0xFF.
 */
stark_err_t cc1101_detect(cc1101_t *d, uint8_t *partnum, uint8_t *version);

/*
 * IDLE, then the preset's registers with the frequency and the data rate
 * overriding its defaults, the PATABLE image, and a read-back of the frequency
 * registers. STARK_ERR_INVALID_ARG, before any transfer, for a frequency
 * outside 433.05–434.79 MHz, a rate outside 600..50 000 Bd or an unknown
 * preset. The chip is left in IDLE. STARK_ERR_IO if the read-back differs.
 */
stark_err_t cc1101_configure(cc1101_t *d, cc1101_preset_t preset, uint32_t hz, uint32_t baud);

/*
 * SRES, SRX, SIDLE, SFRX, SFTX or SNOP. STX is STARK_ERR_NOT_SUPPORTED (see cc1101_tx_start),
 * any other byte STARK_ERR_INVALID_ARG, and SRX before a configuration STARK_ERR_STATE.
 */
stark_err_t cc1101_strobe(cc1101_t *d, uint8_t strobe);

/*
 * Retunes a configured chip and leaves it receiving (STARK-0602): SIDLE, MARCSTATE
 * IDLE, FREQ2..0 in one burst, SRX, MARCSTATE RX, each wait bounded.
 * STARK_ERR_INVALID_ARG, before any transfer, for a frequency outside
 * 433.05–434.79 MHz; STARK_ERR_STATE before a configuration; STARK_ERR_TIMEOUT
 * when the chip does not reach IDLE or RX.
 */
stark_err_t cc1101_tune_rx(cc1101_t *d, uint32_t hz);

/* SIDLE, then MARCSTATE IDLE within 5 ms: the radio is not receiving (STARK-0602). */
stark_err_t cc1101_idle(cc1101_t *d);

/*
 * The one way into TX (STARK-0605). The chip must have been configured with the OOK_TX preset at
 * exactly hz. Before STX it is checked again, on the chip itself: hz is inside 433.05–434.79
 * MHz (STARK_ERR_INVALID_ARG, nothing sent), FREQ2..0 read back as that frequency's words and
 * PATABLE reads back as {0x00, 0x60, 0, 0, 0, 0, 0, 0} (STARK_ERR_IO otherwise: a bit flipped
 * since the configuration, or a chip that is not the one that was written), then STX and a
 * bounded wait for MARCSTATE TX (STARK_ERR_TIMEOUT). STARK_ERR_STATE for any other
 * configuration. On any error the chip may be anywhere from IDLE to TX: the caller idles it.
 */
stark_err_t cc1101_tx_start(cc1101_t *d, uint32_t hz);

stark_err_t cc1101_rssi_raw(cc1101_t *d, uint8_t *raw);
stark_err_t cc1101_marcstate(cc1101_t *d, uint8_t *state);
