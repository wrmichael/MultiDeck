cmake_minimum_required(VERSION 3.16)
# MultiDeck: firmware launcher for the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (the RigDeck board).
# The version comes from version.txt (ESP-IDF reads it automatically).
# bootloader_components/ adds the "hold BOOT to come back" check to the bootloader.
set(EXTRA_COMPONENT_DIRS components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(multideck)
