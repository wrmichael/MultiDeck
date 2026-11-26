// microSD card, mounted the way RigDeck does it on this board: SDMMC slot 0, 4-bit, IOMUX pins,
// powered from the ESP32-P4's on-chip LDO channel 4. The slot has no card-detect line, so the card is
// looked for when MultiDeck starts and again on "Rescan".
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdcard.h"

static const char *TAG = "sdcard";
static sdmmc_card_t *s_card = NULL;
static sd_pwr_ctrl_handle_t s_pwr = NULL;
static bool s_mounted = false;

static esp_err_t mount(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false,             // never: the card holds the user's firmware files
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    if (!s_pwr) {
        sd_pwr_ctrl_ldo_config_t ldo = {.ldo_chan_id = 4};
        esp_err_t e = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &s_pwr);
        if (e != ESP_OK) return e;
    }
    host.pwr_ctrl_handle = s_pwr;
    const sdmmc_slot_config_t slot = {
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 4,
        .flags = 0,
    };
    return esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT, &host, &slot, &mc, &s_card);
}

bool sdcard_mount(char *info, size_t n)
{
    if (s_mounted) {                                  // look again: the card may have been changed
        esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT, s_card);
        s_mounted = false;
        s_card = NULL;
    }
    esp_err_t err = mount();
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no microSD card (%s)", esp_err_to_name(err));
        snprintf(info, n, "%s", err == ESP_FAIL ? "Card not readable (format it FAT32)" : "No microSD card");
        return false;
    }
    s_mounted = true;
    uint64_t total = 0, freeb = 0;
    esp_vfs_fat_info(SDCARD_MOUNT, &total, &freeb);
    char name[8] = "SD card";
    if (s_card->cid.name[0]) { memcpy(name, s_card->cid.name, 7); name[7] = 0; }
    snprintf(info, n, "%s, %.1f GB", name, total / 1e9);
    ESP_LOGI(TAG, "microSD card: %s", info);
    return true;
}
