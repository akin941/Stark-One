/*
 * stark_caps.h — capability bits (TASKS.md V0.5, ADR-0021). A descriptor
 * declares them; only a driver in this firmware confirms them, and only
 * confirmed bits satisfy an app's caps_required. A descriptor is metadata,
 * never a credential.
 */
#pragma once

#define STARK_CAP_IR_TX   (1u << 0) /* onboard IR LED driver */
#define STARK_CAP_IR_RX   (1u << 1) /* onboard IR demodulator */
#define STARK_CAP_NFC     (1u << 2) /* a PN532-class reader answers on the shared I2C bus */
#define STARK_CAP_MOD_SPI (1u << 3) /* slot module uses MOD_CS on shared SPI2 */
#define STARK_CAP_MOD_IRQ (1u << 4) /* slot module uses MOD_IRQ0/1 */

#define STARK_CAPS_KNOWN                                                                           \
    (STARK_CAP_IR_TX | STARK_CAP_IR_RX | STARK_CAP_NFC | STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ)
