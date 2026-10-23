/*
 * medium_sdspi.c — the production storage medium: microSD over the shared
 * SPI2 bus with FATFS (stark_storage_medium.h, ADR-0007/0018).
 *
 * stark_board_init() has already initialised SPI2_HOST. The card attaches
 * once, as a second device with its own CS and clock, and stays attached:
 * its CS is then driven high whenever the card is not addressed, and ESP-IDF's
 * spi_master bus lock keeps its transactions from interleaving with the
 * panel's (ADR-0018 §4). Each mount attempt only re-initialises the card and
 * mounts its FAT volume — the pieces esp_vfs_fat_sdspi_mount() would also
 * do, without its attach/detach of the device on every retry (ADR-0018 §5
 * retries every few seconds while no card is present).
 */
#include "stark_storage_medium.h"
#include <stdbool.h>
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"
#include "stark_board.h"

static bool s_attached;
static sdspi_dev_handle_t s_dev;
static sdmmc_card_t s_card;
static bool s_mounted;
static BYTE s_pdrv = FF_DRV_NOT_USED;
static char s_drive[3]; /* "<pdrv>:" */
static const char *s_mount_point;

/* NOTE: with no card in the slot every retry would make the SD and FAT
 * drivers log an ERROR — one burst every few seconds, forever, on the
 * console and later in the log file. stark_storage reports each state change
 * once instead, so their own lines are switched off. */
static void quiet_vendor_logs(void)
{
    static const char *const tags[] = {"sdmmc_cmd",     "sdmmc_common", "sdmmc_init",
                                       "sdmmc_sd",      "sdspi_host",   "sdspi_transaction",
                                       "vfs_fat_sdmmc", "vfs_fat"};
    for (size_t i = 0; i < sizeof tags / sizeof tags[0]; i++) {
        esp_log_level_set(tags[i], ESP_LOG_NONE);
    }
}

static esp_err_t attach(void)
{
    if (s_attached) {
        return ESP_OK;
    }
    quiet_vendor_logs();
    esp_err_t err = sdspi_host_init();
    if (err != ESP_OK) {
        return err;
    }
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.host_id = SPI2_HOST;
    dev.gpio_cs = stark_board_pins()->sd_cs;
    err = sdspi_host_init_device(&dev, &s_dev);
    s_attached = err == ESP_OK;
    return err;
}

/* Undoes a partial or complete mount; the device stays attached. */
static void release(void)
{
    if (s_pdrv != FF_DRV_NOT_USED) {
        (void)f_mount(NULL, s_drive, 0);
        if (s_mount_point != NULL) {
            (void)esp_vfs_fat_unregister_path(s_mount_point);
        }
        ff_diskio_unregister(s_pdrv);
        s_pdrv = FF_DRV_NOT_USED;
    }
    s_mounted = false;
    s_mount_point = NULL;
}

const char *stark_storage_medium_name(void)
{
    return "sdspi";
}

stark_err_t stark_storage_medium_mount(const char *mount_point, size_t max_files)
{
    if (mount_point == NULL || max_files == 0) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_mounted) {
        return STARK_ERR_STATE;
    }
    if (attach() != ESP_OK) {
        return STARK_ERR_IO;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = s_dev;
    host.max_freq_khz = CONFIG_STARK_STORAGE_SD_KHZ;
    if (sdmmc_card_init(&host, &s_card) != ESP_OK) {
        return STARK_ERR_NOT_FOUND; /* nothing usable answered */
    }
    if (ff_diskio_get_drive(&s_pdrv) != ESP_OK) {
        s_pdrv = FF_DRV_NOT_USED;
        return STARK_ERR_IO;
    }
    ff_diskio_register_sdmmc(s_pdrv, &s_card);
    s_drive[0] = (char)('0' + s_pdrv);
    s_drive[1] = ':';
    s_drive[2] = '\0';
    s_mount_point = mount_point;
    esp_vfs_fat_conf_t conf = {
        .base_path = mount_point,
        .fat_drive = s_drive,
        .max_files = max_files,
    };
    FATFS *fs = NULL;
    if (esp_vfs_fat_register(&conf, &fs) != ESP_OK) {
        s_mount_point = NULL; /* not registered: nothing to unregister */
        release();
        return STARK_ERR_IO;
    }
    /* Never format (ADR-0018 §6): a card without a FAT volume is reported. */
    FRESULT res = f_mount(fs, s_drive, 1);
    if (res != FR_OK) {
        release();
        return res == FR_NO_FILESYSTEM ? STARK_ERR_NOT_SUPPORTED : STARK_ERR_IO;
    }
    s_mounted = true;
    return STARK_OK;
}

void stark_storage_medium_unmount(void)
{
    if (s_mounted) {
        release();
    }
}

stark_err_t stark_storage_medium_probe(void)
{
    if (!s_mounted) {
        return STARK_ERR_STATE;
    }
    return sdmmc_get_status(&s_card) == ESP_OK ? STARK_OK : STARK_ERR_NOT_FOUND;
}

stark_err_t stark_storage_medium_space(stark_medium_space_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
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
