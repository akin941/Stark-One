/*
 * medium_flashfat.c — the emulator storage test image's medium
 * (ADR-0018 §3): stark_storage_medium.h over a wear-levelled FAT volume on
 * the `emufat` internal-flash partition. Test image only — the production
 * firmware links components/stark_storage_medium/medium_sdspi.c.
 *
 * The harness drives the "card" through two globals over GDB
 * (scripts/test_emu.py `pokes`): emu_medium_present = 0 is a pulled card,
 * emu_medium_formatted = 0 a card without a FAT volume.
 */
#include "stark_storage_medium.h"
#include <stdbool.h>
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

#define EMU_PARTITION "emufat"

volatile uint8_t emu_medium_present = 1;
volatile uint8_t emu_medium_formatted = 1;

static wl_handle_t s_wl = WL_INVALID_HANDLE;
static const char *s_mount_point;

const char *stark_storage_medium_name(void)
{
    return "emu-flashfat";
}

stark_err_t stark_storage_medium_mount(const char *mount_point, size_t max_files)
{
    if (mount_point == NULL || max_files == 0) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_wl != WL_INVALID_HANDLE) {
        return STARK_ERR_STATE;
    }
    if (!emu_medium_present) {
        return STARK_ERR_NOT_FOUND;
    }
    if (!emu_medium_formatted) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    esp_vfs_fat_mount_config_t cfg = {
        /* The test partition's own blank flash, never user media: formatted
         * once, on the first boot of a fresh image. */
        .format_if_mount_failed = true,
        .max_files = (int)max_files,
    };
    if (esp_vfs_fat_spiflash_mount_rw_wl(mount_point, EMU_PARTITION, &cfg, &s_wl) != ESP_OK) {
        s_wl = WL_INVALID_HANDLE;
        return STARK_ERR_IO;
    }
    s_mount_point = mount_point;
    return STARK_OK;
}

void stark_storage_medium_unmount(void)
{
    if (s_wl == WL_INVALID_HANDLE) {
        return;
    }
    (void)esp_vfs_fat_spiflash_unmount_rw_wl(s_mount_point, s_wl);
    s_wl = WL_INVALID_HANDLE;
    s_mount_point = NULL;
}

stark_err_t stark_storage_medium_probe(void)
{
    if (s_wl == WL_INVALID_HANDLE) {
        return STARK_ERR_STATE;
    }
    return emu_medium_present ? STARK_OK : STARK_ERR_NOT_FOUND;
}

stark_err_t stark_storage_medium_space(stark_medium_space_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_wl == WL_INVALID_HANDLE) {
        return STARK_ERR_STATE;
    }
    uint64_t total = 0;
    uint64_t free_bytes = 0;
    if (esp_vfs_fat_info(s_mount_point, &total, &free_bytes) != ESP_OK) {
        return STARK_ERR_IO;
    }
    out->total_bytes = total;
    out->free_bytes = free_bytes;
    return STARK_OK;
}
