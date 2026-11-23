/*
 * subghz_transport_fake.c — test substitute for components/stark_subghz_transport
 * (ADR-0022 §3): the same API, answered by the test-only fake CC1101
 * (test/fake/fake_cc1101.c) instead of the module's SPI bus and GDO pins. Test
 * image only. Like production it refuses a stale lease, so a module that leaves
 * its slot is cut off exactly as stark_module_spi cuts it off.
 *
 * Scenarios steer it through GDB pokes (scripts/test_emu.py POKE_VARS), together
 * with emu_mod_slot0 = 32 (the CC1101 module's descriptor):
 *   emu_sg_chip         0 no chip answers, 1 a CC1101 (VERSION 0x14), 2 another chip
 *   emu_sg_carrier_khz  the air: one carrier in kHz (0: silence) ...
 *   emu_sg_level        ... and its level in dBm
 *   emu_sg_scene        a scene of test/fake/fake_subghz_scenes.h (1-based; 0 none) GDO2 plays once
 *                       each time the chip enters RX: 1 pwm24, 2 ppm24, 3 manchester32, 4 dense,
 *                       5 slow (spec-derived, never a recording: scripts/gen_subghz_vectors.py)
 *   emu_sg_own_rx       1: what GDO0 carries is heard on GDO2
 *   emu_sg_fault        a fake_cc1101_fault_t, applied until it changes again (0 clears it)
 * Each is applied before the next transfer after it changes. Integration
 * evidence only: nothing here shows a signal on an antenna.
 */
#include "fake_cc1101.h"
#include "fake_subghz_scenes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stark_subghz_transport.h"

volatile uint8_t emu_sg_chip;
volatile uint32_t emu_sg_carrier_khz;
volatile int8_t emu_sg_level = -110;
volatile uint8_t emu_sg_scene;
volatile uint8_t emu_sg_own_rx;
volatile uint8_t emu_sg_fault;

static stark_module_lease_t s_lease;
static bool s_open;
static bool s_rx_on;
static bool s_inited;
static uint8_t s_chip_applied = 0xFF;
static uint32_t s_carrier_applied = 0xFFFFFFFFu;
static int8_t s_level_applied = 127;
static uint8_t s_scene_applied;
static uint8_t s_own_rx_applied = 0xFF;
static uint8_t s_fault_applied;

/* Applies changed pokes before each transfer. */
static void follow_pokes(void)
{
    uint8_t chip = emu_sg_chip;
    if (chip != s_chip_applied) {
        fake_cc1101_present(chip != 0);
        fake_cc1101_chip(chip == 2 ? 0x01 : 0x00, chip == 2 ? 0x77 : 0x14);
        s_chip_applied = chip;
    }
    uint32_t khz = emu_sg_carrier_khz;
    int8_t level = emu_sg_level;
    if (khz != s_carrier_applied || level != s_level_applied) {
        fake_cc1101_air(khz * 1000u, level);
        s_carrier_applied = khz;
        s_level_applied = level;
    }
    uint8_t scene = emu_sg_scene;
    if (scene != s_scene_applied) {
        if (scene >= 1 && scene <= FAKE_SUBGHZ_SCENE_COUNT) {
            fake_cc1101_scene(k_fake_scenes[scene - 1].sym, k_fake_scenes[scene - 1].n);
        } else {
            fake_cc1101_scene(NULL, 0);
        }
        s_scene_applied = scene;
    }
    uint8_t own = emu_sg_own_rx;
    if (own != s_own_rx_applied) {
        fake_cc1101_own_rx(own != 0);
        s_own_rx_applied = own;
    }
    uint8_t fault = emu_sg_fault;
    if (fault != s_fault_applied && fault < FAKE_CC1101_FAULT_COUNT) {
        fake_cc1101_fault((fake_cc1101_fault_t)fault, fault == 0 ? 0 : -1);
        s_fault_applied = fault;
    }
}

stark_err_t stark_subghz_transport_open(const stark_module_lease_t *lease)
{
    if (lease == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_open) {
        return STARK_ERR_BUSY;
    }
    int cs;
    int gdo0;
    int gdo2;
    stark_err_t err = stark_module_lease_pins(lease, &cs, &gdo0, &gdo2);
    if (err != STARK_OK) {
        return err;
    }
    if (!s_inited) {
        fake_cc1101_reset();
        s_inited = true;
        s_chip_applied = 0xFF; /* the pokes are applied afresh */
        s_carrier_applied = 0xFFFFFFFFu;
        s_level_applied = 127;
        s_own_rx_applied = 0xFF;
        s_scene_applied = 0;
        fake_cc1101_scene(NULL, 0);
        s_fault_applied = 0;
    }
    s_lease = *lease;
    s_open = true;
    return STARK_OK;
}

void stark_subghz_transport_close(void)
{
    s_rx_on = false;
    s_open = false;
}

stark_err_t stark_subghz_transport_xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!s_open || !stark_module_lease_valid(&s_lease)) {
        return STARK_ERR_STATE;
    }
    follow_pokes();
    return fake_cc1101_xfer(tx, rx, len);
}

stark_err_t stark_subghz_transport_rx_start(void)
{
    if (!s_open || !stark_module_lease_valid(&s_lease)) {
        return STARK_ERR_STATE;
    }
    s_rx_on = true;
    return STARK_OK;
}

size_t stark_subghz_transport_rx_take(subghz_sym_t *out, size_t max, uint32_t timeout_ms)
{
    if (!s_rx_on || out == NULL) {
        return 0;
    }
    follow_pokes();
    size_t n = fake_cc1101_gdo2_take(out, max);
    if (n == 0) {
        vTaskDelay(pdMS_TO_TICKS(timeout_ms));
    }
    return n;
}

void stark_subghz_transport_rx_stop(void)
{
    s_rx_on = false;
}

/*
 * Like production it refuses a stale lease and anything stark_subghz_tx_ok() refuses, so the
 * test image cannot send what the policy would not. The fake chip records the symbols only while
 * it is in TX (a transfer that put it there), so a symbol that reaches GDO0 without STX is
 * refused. The on-air time is waited out, so the service holds its lock as long as hardware would.
 */
stark_err_t stark_subghz_transport_tx(const subghz_sym_t *s, size_t n, uint32_t timeout_ms)
{
    if (!s_open || !stark_module_lease_valid(&s_lease)) {
        return STARK_ERR_STATE;
    }
    if (!stark_subghz_tx_ok(s, n, timeout_ms)) {
        return STARK_ERR_INVALID_ARG;
    }
    follow_pokes();
    if (!fake_cc1101_gdo0_put(s, n)) {
        return STARK_ERR_STATE;
    }
    uint32_t total_us = 0;
    for (size_t i = 0; i < n; i++) {
        total_us += (uint32_t)(s[i] < 0 ? -s[i] : s[i]);
    }
    vTaskDelay(pdMS_TO_TICKS((total_us + 999u) / 1000u));
    return STARK_OK;
}
