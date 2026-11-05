/*
 * fake_24c02.c — test-only 24C02 EEPROMs (fake_24c02.h).
 */
#include "fake_24c02.h"
#include <string.h>

typedef struct {
    bool fitted;
    uint8_t ptr;
    uint8_t mem[FAKE_24C02_SIZE];
} chip_t;

static chip_t s_chip[FAKE_24C02_DEVICES];
static fake_24c02_fault_t s_fault;
static int s_fault_left;
static unsigned s_transactions;

static chip_t *chip_at(uint8_t addr)
{
    if (addr < 0x50u || addr >= 0x50u + FAKE_24C02_DEVICES) {
        return NULL;
    }
    chip_t *c = &s_chip[addr - 0x50u];
    return c->fitted ? c : NULL;
}

/* The fault this transaction suffers, counted down. */
static fake_24c02_fault_t take_fault(void)
{
    s_transactions++;
    if (s_fault == FAKE_24C02_NONE || s_fault_left == 0) {
        return FAKE_24C02_NONE;
    }
    if (s_fault_left > 0) {
        s_fault_left--;
    }
    return s_fault;
}

void fake_24c02_reset(void)
{
    memset(s_chip, 0, sizeof s_chip);
    s_fault = FAKE_24C02_NONE;
    s_fault_left = 0;
    s_transactions = 0;
}

void fake_24c02_put(uint8_t addr, const uint8_t *image, size_t len)
{
    if (addr < 0x50u || addr >= 0x50u + FAKE_24C02_DEVICES) {
        return;
    }
    chip_t *c = &s_chip[addr - 0x50u];
    memset(c->mem, 0xFF, sizeof c->mem);
    if (image != NULL) {
        memcpy(c->mem, image, len < sizeof c->mem ? len : sizeof c->mem);
    }
    c->ptr = 0;
    c->fitted = true;
}

void fake_24c02_remove(uint8_t addr)
{
    if (addr >= 0x50u && addr < 0x50u + FAKE_24C02_DEVICES) {
        s_chip[addr - 0x50u].fitted = false;
    }
}

void fake_24c02_fault(fake_24c02_fault_t kind, int count)
{
    s_fault = count == 0 ? FAKE_24C02_NONE : kind;
    s_fault_left = count;
}

stark_err_t fake_24c02_probe(uint8_t addr)
{
    fake_24c02_fault_t f = take_fault();
    if (f == FAKE_24C02_TIMEOUT) {
        return STARK_ERR_TIMEOUT;
    }
    if (f == FAKE_24C02_BUS_ERROR) {
        return STARK_ERR_IO;
    }
    if (f == FAKE_24C02_NACK_ADDR || chip_at(addr) == NULL) {
        return STARK_ERR_NOT_FOUND;
    }
    return STARK_OK;
}

stark_err_t fake_24c02_write(uint8_t addr, const uint8_t *buf, size_t len)
{
    fake_24c02_fault_t f = take_fault();
    chip_t *c = chip_at(addr);
    if (f == FAKE_24C02_NACK_ADDR || c == NULL) {
        return STARK_ERR_NOT_FOUND;
    }
    if (f == FAKE_24C02_TIMEOUT) {
        return STARK_ERR_TIMEOUT;
    }
    if (f == FAKE_24C02_BUS_ERROR || f == FAKE_24C02_NACK_DATA) {
        return STARK_ERR_IO;
    }
    if (len > 0) {
        c->ptr = buf[0];
    }
    for (size_t i = 1; i < len; i++) {
        c->mem[c->ptr++] = buf[i]; /* uint8_t: wraps at 256 */
    }
    return STARK_OK;
}

stark_err_t fake_24c02_read(uint8_t addr, uint8_t *buf, size_t len)
{
    fake_24c02_fault_t f = take_fault();
    chip_t *c = chip_at(addr);
    if (f == FAKE_24C02_NACK_ADDR || c == NULL) {
        return STARK_ERR_NOT_FOUND;
    }
    if (f == FAKE_24C02_TIMEOUT) {
        return STARK_ERR_TIMEOUT;
    }
    if (f == FAKE_24C02_BUS_ERROR || f == FAKE_24C02_NACK_DATA) {
        return STARK_ERR_IO;
    }
    if (f == FAKE_24C02_TRUNCATE) {
        if (len > 0) {
            buf[0] = c->mem[c->ptr++];
        }
        return STARK_ERR_IO;
    }
    for (size_t i = 0; i < len; i++) {
        buf[i] = c->mem[c->ptr++];
    }
    if (f == FAKE_24C02_BIT_FLIP && len > 0) {
        buf[len / 2] ^= 0x10u;
    }
    return STARK_OK;
}

stark_err_t fake_24c02_read_at(uint8_t addr, uint8_t off, uint8_t *buf, size_t len)
{
    stark_err_t err = fake_24c02_write(addr, &off, 1);
    return err != STARK_OK ? err : fake_24c02_read(addr, buf, len);
}

unsigned fake_24c02_transactions(void)
{
    return s_transactions;
}
