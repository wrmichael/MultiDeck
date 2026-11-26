// MultiDeck's bootloader addition: "hold BOOT after a reset to come back to MultiDeck".
//
// The BOOT button (GPIO 35) is also the ESP32-P4's download-mode strapping pin: held while RESET is
// released, the chip stays in the ROM's USB download mode. So the button is looked for a little
// later, here in the second-stage bootloader: tap RESET, then press BOOT within the window.
//
// When it is seen, otadata is erased. With otadata blank the bootloader starts the factory app,
// which is MultiDeck. Nothing else is touched (the slots keep their firmware, NVS keeps everyone's
// settings), so the next firmware picked in MultiDeck can start straight from its slot.
#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "bootloader_common.h"

#ifndef CONFIG_MULTIDECK_RETURN_GPIO
#define CONFIG_MULTIDECK_RETURN_GPIO 35
#endif
#ifndef CONFIG_MULTIDECK_RETURN_WINDOW_MS
#define CONFIG_MULTIDECK_RETURN_WINDOW_MS 500
#endif

static const char *TAG = "multideck";

void bootloader_hooks_include(void) {}

void bootloader_after_init(void)
{
    if (CONFIG_MULTIDECK_RETURN_WINDOW_MS <= 0) return;
    // Pressed = low (internal pull-up). Needs to be seen pressed for 30 ms in a row, against contact bounce.
    int held = 0;
    for (int t = 0; t < CONFIG_MULTIDECK_RETURN_WINDOW_MS; t++) {
        bool down = bootloader_common_check_long_hold_gpio_level(CONFIG_MULTIDECK_RETURN_GPIO, 0, false) == GPIO_LONG_HOLD;
        held = down ? held + 1 : 0;
        if (held >= 30) break;
        esp_rom_delay_us(1000);
    }
    if (held < 30) return;
    ESP_LOGI(TAG, "BOOT held: back to MultiDeck (otadata cleared)");
    if (!bootloader_common_erase_part_type_data("", true)) ESP_LOGE(TAG, "could not clear otadata");
}
