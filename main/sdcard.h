#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SDCARD_MOUNT "/sdcard"
// Mounts (or re-mounts) the card. true = mounted; info: what the card is, or why there is none.
bool sdcard_mount(char *info, size_t n);
#ifdef __cplusplus
}
#endif
