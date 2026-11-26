// MultiDeck: firmware launcher for the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B.
// Lives in the factory partition. Shows the firmware on the microSD card; the one picked is copied
// into an app slot (unless it is already there) and started. MultiDeck comes back when BOOT is held
// after a reset (bootloader_components/multideck_boot_hook) or when a firmware asks for it
// (components/multideck_return).
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "launcher.h"
#include "ui.h"

static const char* TAG = "multideck";

extern "C" void app_main(void) {
    // The NVS partition is shared with every firmware started from here. MultiDeck never erases it:
    // if it cannot be opened, MultiDeck just does not remember the last firmware.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "settings not available (%s); continuing without them", esp_err_to_name(err));
    ESP_LOGI(TAG, "MultiDeck %s", md::fwVersion().c_str());
    if (!display_start()) ESP_LOGE(TAG, "no display: nothing to show");
}
