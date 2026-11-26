idf_component_register(
    SRCS "app_main.cpp" "launcher.cpp" "fwimage.c" "sdcard.c" "display.c" "ui.cpp" "logo_font.c" "lv_mem_psram.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES fatfs sdmmc esp_driver_sdmmc esp_partition app_update esp_app_format bootloader_support
                  nvs_flash esp_timer efuse spi_flash)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
