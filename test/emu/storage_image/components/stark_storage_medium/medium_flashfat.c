/*
 * medium_flashfat.c — the emulator storage test image's medium
 * (ADR-0018 §3): stark_storage_medium.h over a wear-levelled FAT volume on
 * the `emufat` internal-flash partition. Test image only — the production
 * firmware links components/stark_storage_medium/medium_sdspi.c.
 *
 * The harness drives the "card" through two globals over GDB
 * (scripts/test_emu.py `pokes`): emu_medium_present = 0 is a pulled card,
 * emu_medium_formatted = 0 a card without a FAT volume. While "pulled",
 * every sector read and write fails, as a removed SD card's would: the
 * mounted volume's diskio is replaced by one that forwards to the
 * wear-levelling layer only while the card is present (STARK-0207).
 */
#include "stark_storage_medium.h"
#include <stdbool.h>
#include "diskio_impl.h"
#include "diskio_wl.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

#define EMU_PARTITION "emufat"

volatile uint8_t emu_medium_present = 1;
volatile uint8_t emu_medium_formatted = 1;

static wl_handle_t s_wl = WL_INVALID_HANDLE;
static const char *s_mount_point;

/* ---- the "pullable" diskio: IDF's diskio_wl.c, gated on presence ---- */

static DSTATUS emu_init(BYTE pdrv)
{
    (void)pdrv;
    return 0;
}

static DSTATUS emu_status(BYTE pdrv)
{
    (void)pdrv;
    return emu_medium_present ? 0 : STA_NODISK;
}

static DRESULT emu_read(BYTE pdrv, BYTE *buf, LBA_t sector, UINT count)
{
    (void)pdrv;
    size_t ss = wl_sector_size(s_wl);
    if (!emu_medium_present) {
        return RES_ERROR;
    }
    return wl_read(s_wl, sector * ss, buf, count * ss) == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT emu_write(BYTE pdrv, const BYTE *buf, LBA_t sector, UINT count)
{
    (void)pdrv;
    size_t ss = wl_sector_size(s_wl);
    if (!emu_medium_present || wl_erase_range(s_wl, sector * ss, count * ss) != ESP_OK) {
        return RES_ERROR;
    }
    return wl_write(s_wl, sector * ss, buf, count * ss) == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT emu_ioctl(BYTE pdrv, BYTE cmd, void *buf)
{
    (void)pdrv;
    switch (cmd) {
        case CTRL_SYNC:
            return emu_medium_present ? RES_OK : RES_ERROR;
        case GET_SECTOR_COUNT:
            *(LBA_t *)buf = (LBA_t)(wl_size(s_wl) / wl_sector_size(s_wl));
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buf = (WORD)wl_sector_size(s_wl);
            return RES_OK;
        default:
            return RES_ERROR;
    }
}

static const ff_diskio_impl_t k_emu_diskio = {emu_init, emu_status, emu_read, emu_write, emu_ioctl};

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
        /* The test partition's own flash, never user media: it is seeded at
         * build time (STARK-0306); formatting only rescues a damaged one. */
        .format_if_mount_failed = true,
        .max_files = (int)max_files,
    };
    if (esp_vfs_fat_spiflash_mount_rw_wl(mount_point, EMU_PARTITION, &cfg, &s_wl) != ESP_OK) {
        s_wl = WL_INVALID_HANDLE;
        return STARK_ERR_IO;
    }
    s_mount_point = mount_point;
    /* Same drive, same handle bookkeeping (unmount clears it); only the
     * sector functions change. */
    ff_diskio_register(ff_diskio_get_pdrv_wl(s_wl), &k_emu_diskio);
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

stark_err_t stark_storage_medium_present(void)
{
    if (s_wl != WL_INVALID_HANDLE) {
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
