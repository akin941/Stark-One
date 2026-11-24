/*
 * lab_i2c_hints.c — the known-device hints (lab_i2c.h, TASKS.md STARK-0704, ADR-0023 §5): a lookup
 * by address of a phrase that says what the address is often used for. Const data in flash, at
 * most LAB_I2C_HINTS_MAX entries sorted by address. Generic and public: device classes that any
 * datasheet lists, in "may be" wording, because an address does not identify a device and nothing
 * is probed to find out. No vendor database, no register map, nothing but address and phrase.
 * Every phrase fits a menu row beside its address (30 characters).
 */
#include <stddef.h>
#include "lab_i2c.h"

typedef struct {
    uint8_t addr;
    const char *phrase;
} hint_t;

static const hint_t k_hints[] = {
    {0x0c, "may be compass (AK89xx-class)"},  {0x10, "may be light (VEML-class)"},
    {0x18, "may be accelerometer (LIS3)"},    {0x1d, "may be accelerometer (ADXL345)"},
    {0x1e, "may be compass (HMC5883-class)"}, {0x20, "may be I/O expander (PCF8574)"},
    {0x23, "may be light sensor (BH1750)"},   {0x24, "may be NFC reader (PN532)"},
    {0x27, "may be LCD backpack (PCF8574)"},  {0x29, "may be light/ToF (TSL2561)"},
    {0x38, "may be humidity (AHT10-class)"},  {0x39, "may be light sensor (TSL2561)"},
    {0x3c, "may be small OLED (SSD1306)"},    {0x3d, "may be small OLED (SSD1306)"},
    {0x40, "may be humidity (HTU21-class)"},  {0x44, "may be humidity (SHT3x-class)"},
    {0x48, "may be ADC/temp (ADS1115)"},      {0x49, "may be ADC/temp (ADS1115)"},
    {0x50, "may be EEPROM or a module ID"},   {0x51, "may be EEPROM or a module ID"},
    {0x53, "may be accelerometer (ADXL345)"}, {0x57, "may be EEPROM (24Cxx)"},
    {0x5a, "may be gas/touch (CCS811)"},      {0x5c, "may be light/pressure (BH1750)"},
    {0x60, "may be DAC (MCP4725-class)"},     {0x68, "may be RTC/IMU (DS3231)"},
    {0x69, "may be IMU (MPU6050-class)"},     {0x6b, "may be IMU (LSM6DS3-class)"},
    {0x70, "may be I2C mux (TCA9548)"},       {0x76, "may be pressure (BMP280-class)"},
    {0x77, "may be pressure (BMP180-class)"},
};

#define HINTS (sizeof k_hints / sizeof k_hints[0])

_Static_assert(HINTS <= LAB_I2C_HINTS_MAX, "the hints table has at most 48 entries");

const char *stark_lab_i2c_hint(uint8_t addr)
{
    for (size_t i = 0; i < HINTS; i++) {
        if (k_hints[i].addr == addr) {
            return k_hints[i].phrase;
        }
    }
    return NULL;
}

size_t lab_i2c_hints_count(void)
{
    return HINTS;
}

uint8_t lab_i2c_hints_addr(size_t i)
{
    return i < HINTS ? k_hints[i].addr : 0u;
}
