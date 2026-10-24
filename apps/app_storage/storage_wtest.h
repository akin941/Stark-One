/*
 * storage_wtest.h — the Storage app's write test (TASKS.md STARK-0203).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define WTEST_BYTES 102400u /* ROADMAP V0.2 exit 2: 100 kB */
#define WTEST_CHUNK 4096u

/* Starts the test on the app worker. Progress (0-100) reaches the app as
 * STARK_APP_REQ_NOTIFY. Logs (scenario contract): `storageapp: wtest start`,
 * `storageapp: wtest ok bytes=102400 crc=<8 hex> frames=<n>` (UI frames
 * rendered meanwhile), `storageapp: wtest failed: <err> at <offset>`, or
 * `storageapp: wtest refused: <state>` when no card is mounted. */
void storage_wtest_start(void);

/* CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF), continued from crc. */
uint32_t storage_wtest_crc32(uint32_t crc, const uint8_t *p, size_t n);
