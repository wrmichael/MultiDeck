// On-device touch interface (ui.cpp). ui_create() runs with the LVGL lock held.
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define UI_W 1024
#define UI_H 600
void ui_create(void);
bool display_start(void);
#ifdef __cplusplus
}
#endif
