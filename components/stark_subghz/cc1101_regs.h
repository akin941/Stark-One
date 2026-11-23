/*
 * cc1101_regs.h — CC1101 registers, strobes and register profiles
 * (TASKS.md STARK-0601, ADR-0022 §5). Pure: no I/O, no ESP-IDF.
 *
 * NOTE: the band lock and the power cap live here, as constants. Nothing in
 * this file or in cc1101_drv.h takes a frequency outside 433.05–434.79 MHz or
 * a PATABLE value as an argument, and the cap is not a Kconfig option.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"
#include "subghz_rssi.h" /* the band's edges */

/* ---- SPI header byte ------------------------------------------------------ */
#define CC1101_HDR_READ      0x80u
#define CC1101_HDR_BURST     0x40u

/* ---- configuration registers 0x00..0x2E ---------------------------------- */
#define CC1101_IOCFG2        0x00u
#define CC1101_IOCFG1        0x01u
#define CC1101_IOCFG0        0x02u
#define CC1101_FIFOTHR       0x03u
#define CC1101_PKTCTRL1      0x07u
#define CC1101_PKTCTRL0      0x08u
#define CC1101_ADDR          0x09u
#define CC1101_CHANNR        0x0Au
#define CC1101_FSCTRL1       0x0Bu
#define CC1101_FSCTRL0       0x0Cu
#define CC1101_FREQ2         0x0Du
#define CC1101_FREQ1         0x0Eu
#define CC1101_FREQ0         0x0Fu
#define CC1101_MDMCFG4       0x10u
#define CC1101_MDMCFG3       0x11u
#define CC1101_MDMCFG2       0x12u
#define CC1101_MDMCFG1       0x13u
#define CC1101_MDMCFG0       0x14u
#define CC1101_DEVIATN       0x15u
#define CC1101_MCSM2         0x16u
#define CC1101_MCSM1         0x17u
#define CC1101_MCSM0         0x18u
#define CC1101_FOCCFG        0x19u
#define CC1101_BSCFG         0x1Au
#define CC1101_AGCCTRL2      0x1Bu
#define CC1101_AGCCTRL1      0x1Cu
#define CC1101_AGCCTRL0      0x1Du
#define CC1101_WOREVT1       0x1Eu
#define CC1101_WOREVT0       0x1Fu
#define CC1101_WORCTRL       0x20u
#define CC1101_FREND1        0x21u
#define CC1101_FREND0        0x22u
#define CC1101_FSCAL3        0x23u
#define CC1101_FSCAL2        0x24u
#define CC1101_FSCAL1        0x25u
#define CC1101_FSCAL0        0x26u
#define CC1101_FSTEST        0x29u
#define CC1101_TEST2         0x2Cu
#define CC1101_TEST1         0x2Du
#define CC1101_TEST0         0x2Eu
#define CC1101_REG_LAST      0x2Eu

/* ---- command strobes (header byte without R/W and burst) ------------------ */
#define CC1101_SRES          0x30u
#define CC1101_SRX           0x34u
#define CC1101_STX           0x35u
#define CC1101_SIDLE         0x36u
#define CC1101_SFRX          0x3Au
#define CC1101_SFTX          0x3Bu
#define CC1101_SNOP          0x3Du

/* ---- status registers (read with the burst bit) --------------------------- */
#define CC1101_PARTNUM       0x30u
#define CC1101_VERSION       0x31u
#define CC1101_RSSI          0x34u
#define CC1101_MARCSTATE     0x35u

#define CC1101_PATABLE       0x3Eu

#define CC1101_PARTNUM_VALUE 0x00u
#define CC1101_VERSION_A     0x14u
#define CC1101_VERSION_B     0x04u

#define CC1101_MARC_IDLE     0x01u
#define CC1101_MARC_RX       0x0Du
#define CC1101_MARC_TX       0x13u
#define CC1101_MARC_MASK     0x1Fu

/* ---- limits (ADR-0022 §5–6): constants, never Kconfig --------------------- */
#define CC1101_XTAL_HZ       26000000u
#define CC1101_HZ_MIN        SUBGHZ_BAND_MIN_HZ
#define CC1101_HZ_MAX        SUBGHZ_BAND_MAX_HZ
#define CC1101_BAUD_MIN      600u
#define CC1101_BAUD_MAX      50000u
#define CC1101_PA_CAP        0x60u /* about 0 dBm conducted at 433 MHz */
#define CC1101_PATABLE_SIZE  8u

typedef struct {
    uint8_t addr;
    uint8_t value;
} cc1101_reg_t;

typedef enum {
    CC1101_PRESET_OOK_RX,
    CC1101_PRESET_OOK_TX,
    CC1101_PRESET_FSK2_RX,
    CC1101_PRESET_COUNT
} cc1101_preset_t;

/* round(hz * 2^16 / 26 MHz) as FREQ2, FREQ1, FREQ0. STARK_ERR_INVALID_ARG outside the band. */
stark_err_t cc1101_freq_regs(uint32_t hz, uint8_t out[3]);

/*
 * DRATE_E in the low nibble of *mdmcfg4 (the channel-filter bits are the
 * preset's) and DRATE_M in *mdmcfg3, for the nearest data rate.
 * STARK_ERR_INVALID_ARG outside 600..50 000 Bd.
 */
stark_err_t cc1101_rate_regs(uint32_t baud, uint8_t *mdmcfg4, uint8_t *mdmcfg3);

/*
 * A preset's complete register list for 433.92 MHz at 4800 Bd, in write order;
 * NULL and 0 for an unknown preset. cc1101_configure() overrides FREQ and the
 * data rate.
 */
size_t cc1101_preset_regs(cc1101_preset_t preset, const cc1101_reg_t **out);

/*
 * The PATABLE image written with every configuration: index 0 (OOK "off") is
 * 0, index 1 is CC1101_PA_CAP, the rest 0. No byte above the cap exists.
 */
const uint8_t *cc1101_patable(void);
