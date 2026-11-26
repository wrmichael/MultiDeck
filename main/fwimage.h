// Looks inside a firmware .bin before anything is written to flash. Plain C with no ESP-IDF headers,
// so the same code is tested on a PC (test/t_fwimage.c).
//
// Two kinds of file are accepted:
//   - an application image (what "idf.py build" leaves in build/<project>.bin), and
//   - a merged image for address 0x0 (esptool merge-bin, e.g. a release file for a browser flasher):
//     bootloader, partition table at 0x8000 and the application. The application is taken from it.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define FWI_CHIP_ESP32P4 0x0012
#define FWI_MAX_PARTS 24

// Reads n bytes at off into buf; returns the number of bytes read.
typedef size_t (*fwi_read_fn)(void* ctx, uint32_t off, void* buf, size_t n);

typedef struct {
    char label[17];
    uint8_t type, subtype;
    uint32_t offset, size;
} fwi_part_t;

typedef struct {
    bool ok;                  // an application image was found and its layout is sound
    char error[96];           // why not, in words for the screen
    bool merged;              // the file is a merged image (bootloader + table + app)
    uint32_t app_offset;      // where the application starts in the file
    uint32_t app_len;         // bytes to copy, the SHA-256 included
    uint16_t chip_id;
    uint16_t min_rev, max_rev;  // chip revision range, major*100+minor (max 0xFFFF = any)
    bool has_sha;
    uint8_t sha256[32];       // the image's own digest (what esp_partition_get_sha256 reports once flashed)
    char project[33], version[33], date[17], time[17], idf[33];
    int nparts;               // merged images: the partition table inside
    fwi_part_t parts[FWI_MAX_PARTS];
} fwi_info_t;

void fwi_inspect(fwi_read_fn rd, void* ctx, uint32_t file_size, fwi_info_t* out);

#ifdef __cplusplus
}
#endif
