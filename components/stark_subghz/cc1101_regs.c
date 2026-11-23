/*
 * cc1101_regs.c — register profiles (cc1101_regs.h). Pure.
 *
 * NOTE: asynchronous serial mode: PKTCTRL0 = 0x32 (PKT_FORMAT 3, infinite
 * length), GDO2 = serial data out (IOCFG2 0x0D), GDO0 = high impedance in RX
 * (0x2E) and serial data in in TX (0x2D). OOK uses PATABLE index 0 for "off"
 * and index 1 for "on" (FREND0 = 0x11). The analogue values (FSCAL, TEST, AGC,
 * filter bandwidth) are the datasheet's recommended 433 MHz settings; whether
 * they are best on the real board is a V1 measurement
 * (docs/bringup/v1-subghz.md).
 */
#include "cc1101_regs.h"

#define LIST(a) (sizeof(a) / sizeof((a)[0]))

/* NOTE: 433.92 MHz = 0x10B071; 4800 Bd = DRATE_E 7, DRATE_M 0x83; 203 kHz channel filter. */
#define COMMON_HEAD                                                                                \
    {CC1101_FIFOTHR, 0x47}, {CC1101_PKTCTRL1, 0x04}, {CC1101_PKTCTRL0, 0x32}, {CC1101_ADDR, 0x00}, \
        {CC1101_CHANNR, 0x00}, {CC1101_FSCTRL1, 0x06}, {CC1101_FSCTRL0, 0x00},                     \
        {CC1101_FREQ2, 0x10}, {CC1101_FREQ1, 0xB0}, {CC1101_FREQ0, 0x71}, {CC1101_MDMCFG4, 0x87},  \
    {                                                                                              \
        CC1101_MDMCFG3, 0x83                                                                       \
    }

#define COMMON_TAIL                                                                                \
    {CC1101_MDMCFG1, 0x02}, {CC1101_MDMCFG0, 0xF8}, {CC1101_DEVIATN, 0x15}, {CC1101_MCSM2, 0x07},  \
        {CC1101_MCSM1, 0x30}, {CC1101_MCSM0, 0x18}, {CC1101_FOCCFG, 0x14}, {CC1101_BSCFG, 0x6C},   \
        {CC1101_AGCCTRL2, 0x07}, {CC1101_AGCCTRL1, 0x00}, {CC1101_AGCCTRL0, 0x91},                 \
        {CC1101_WOREVT1, 0x87}, {CC1101_WOREVT0, 0x6B}, {CC1101_WORCTRL, 0xFB},                    \
    {                                                                                              \
        CC1101_FREND1, 0xB6                                                                        \
    }

#define COMMON_CAL                                                                                 \
    {CC1101_FSCAL3, 0xE9}, {CC1101_FSCAL2, 0x2A}, {CC1101_FSCAL1, 0x00}, {CC1101_FSCAL0, 0x1F},    \
        {CC1101_FSTEST, 0x59}, {CC1101_TEST2, 0x81}, {CC1101_TEST1, 0x35},                         \
    {                                                                                              \
        CC1101_TEST0, 0x09                                                                         \
    }

/* MDMCFG2 0x30: OOK, DC filter on, no Manchester, no sync word. 0x00: the same for 2-FSK. */
static const cc1101_reg_t k_ook_rx[] = {
    {CC1101_IOCFG2, 0x0D},  {CC1101_IOCFG1, 0x2E}, {CC1101_IOCFG0, 0x2E}, COMMON_HEAD,
    {CC1101_MDMCFG2, 0x30}, COMMON_TAIL,           {CC1101_FREND0, 0x11}, COMMON_CAL,
};

static const cc1101_reg_t k_ook_tx[] = {
    {CC1101_IOCFG2, 0x0D},  {CC1101_IOCFG1, 0x2E}, {CC1101_IOCFG0, 0x2D}, COMMON_HEAD,
    {CC1101_MDMCFG2, 0x30}, COMMON_TAIL,           {CC1101_FREND0, 0x11}, COMMON_CAL,
};

static const cc1101_reg_t k_fsk2_rx[] = {
    {CC1101_IOCFG2, 0x0D},  {CC1101_IOCFG1, 0x2E}, {CC1101_IOCFG0, 0x2E}, COMMON_HEAD,
    {CC1101_MDMCFG2, 0x00}, COMMON_TAIL,           {CC1101_FREND0, 0x10}, COMMON_CAL,
};

static const uint8_t k_patable[CC1101_PATABLE_SIZE] = {0x00, CC1101_PA_CAP, 0, 0, 0, 0, 0, 0};

stark_err_t cc1101_freq_regs(uint32_t hz, uint8_t out[3])
{
    if (out == NULL || hz < CC1101_HZ_MIN || hz > CC1101_HZ_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    uint64_t word = (((uint64_t)hz << 16) + CC1101_XTAL_HZ / 2u) / CC1101_XTAL_HZ;
    out[0] = (uint8_t)(word >> 16);
    out[1] = (uint8_t)(word >> 8);
    out[2] = (uint8_t)word;
    return STARK_OK;
}

stark_err_t cc1101_rate_regs(uint32_t baud, uint8_t *mdmcfg4, uint8_t *mdmcfg3)
{
    if (mdmcfg4 == NULL || mdmcfg3 == NULL || baud < CC1101_BAUD_MIN || baud > CC1101_BAUD_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    /* baud = 26 MHz * (256 + M) * 2^E / 2^28: the first E that puts 256 + M in 256..511. */
    uint32_t e = 0;
    uint64_t m = 0;
    for (; e < 16; e++) {
        uint64_t den = (uint64_t)CC1101_XTAL_HZ << e;
        m = (((uint64_t)baud << 28) + den / 2u) / den;
        if (m <= 511u) {
            break;
        }
    }
    /* 600..50 000 Bd always lands in 256..511 for some E below 16. */
    *mdmcfg4 = (uint8_t)e;
    *mdmcfg3 = (uint8_t)(m - 256u);
    return STARK_OK;
}

size_t cc1101_preset_regs(cc1101_preset_t preset, const cc1101_reg_t **out)
{
    const cc1101_reg_t *list = NULL;
    size_t n = 0;
    switch (preset) {
        case CC1101_PRESET_OOK_RX:
            list = k_ook_rx;
            n = LIST(k_ook_rx);
            break;
        case CC1101_PRESET_OOK_TX:
            list = k_ook_tx;
            n = LIST(k_ook_tx);
            break;
        case CC1101_PRESET_FSK2_RX:
            list = k_fsk2_rx;
            n = LIST(k_fsk2_rx);
            break;
        case CC1101_PRESET_COUNT:
        default:
            break;
    }
    if (out != NULL) {
        *out = list;
    }
    return n;
}

const uint8_t *cc1101_patable(void)
{
    return k_patable;
}
