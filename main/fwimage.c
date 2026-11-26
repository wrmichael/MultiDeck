// Firmware image inspection (see fwimage.h). Layout as written by esptool / ESP-IDF:
//
//   image header (24 bytes): magic 0xE9, segment count, flash mode/speed/size, entry address,
//       wp pin, drive settings, chip id (u16 @12), min chip rev (u8 @14), min rev full (u16 @15),
//       max rev full (u16 @17), reserved, hash_appended (u8 @23)
//   segments: 8-byte header (load address, length) + data, one after the other
//   padding to 16 bytes with the checksum in the last byte, then the SHA-256 if hash_appended
//   The first segment of an application starts with esp_app_desc_t (magic 0xABCD5432).
#include "fwimage.h"
#include <stdio.h>
#include <string.h>

#define IMG_MAGIC 0xE9
#define APP_DESC_MAGIC 0xABCD5432u
#define PT_OFFSET 0x8000
#define PT_MAGIC 0x50AA
#define PT_MD5_MAGIC 0xEBEB

static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void copystr(char* dst, size_t n, const uint8_t* src, size_t srcmax) {
    size_t i = 0;
    for (; i + 1 < n && i < srcmax && src[i]; i++) dst[i] = (src[i] >= 32 && src[i] < 127) ? (char)src[i] : '?';
    dst[i] = 0;
}

static void fail(fwi_info_t* o, const char* msg) {
    o->ok = false;
    snprintf(o->error, sizeof o->error, "%s", msg);
}

// The partition table of a merged image; returns the offset of the app to use (factory, else the
// first app slot that lies inside the file), or 0 when there is none.
static uint32_t read_table(fwi_read_fn rd, void* ctx, uint32_t size, fwi_info_t* o) {
    uint32_t pick = 0, first = 0;
    for (int i = 0; i < FWI_MAX_PARTS; i++) {
        uint8_t e[32];
        if (rd(ctx, PT_OFFSET + i * 32, e, 32) != 32) break;
        uint16_t m = u16(e);
        if (m != PT_MAGIC) break;                        // 0xFFFF (end) or the MD5 entry (0xEBEB)
        fwi_part_t* p = &o->parts[o->nparts++];
        p->type = e[2];
        p->subtype = e[3];
        p->offset = u32(e + 4);
        p->size = u32(e + 8);
        copystr(p->label, sizeof p->label, e + 12, 16);
        if (p->type == 0 && p->offset < size) {
            if (p->subtype == 0x00 && !pick) pick = p->offset;   // factory
            if (!first) first = p->offset;
        }
    }
    return pick ? pick : first;
}

void fwi_inspect(fwi_read_fn rd, void* ctx, uint32_t size, fwi_info_t* o) {
    memset(o, 0, sizeof *o);
    uint8_t h[24];
    if (size < 24 + 8 + 256 || rd(ctx, 0, h, 24) != 24) return fail(o, "The file is too small to be firmware.");

    // A merged image is recognised by its partition table at 0x8000 (on the ESP32-P4 the bootloader
    // sits at 0x2000, so such a file starts with 0xFF padding, not with an image header).
    uint32_t base = 0;
    uint8_t pt[2];
    bool table = size > PT_OFFSET + 32 && rd(ctx, PT_OFFSET, pt, 2) == 2 && u16(pt) == PT_MAGIC;
    if (!table && h[0] != IMG_MAGIC) return fail(o, "Not an ESP firmware image (no 0xE9 at the start).");
    if (table) {
        o->merged = true;
        base = read_table(rd, ctx, size, o);
        if (!base) return fail(o, "A full-flash image, but its partition table has no app inside the file.");
        if (rd(ctx, base, h, 24) != 24 || h[0] != IMG_MAGIC)
            return fail(o, "A full-flash image, but there is no app where its partition table says.");
    }
    o->app_offset = base;
    o->chip_id = u16(h + 12);
    o->min_rev = u16(h + 15);
    o->max_rev = u16(h + 17);
    if (o->min_rev == 0 && h[14]) o->min_rev = (uint16_t)(h[14] * 100);   // old images: major only
    int nseg = h[1];
    if (nseg < 1 || nseg > 16) return fail(o, "The image header is damaged (segment count).");

    // the application descriptor at the start of the first segment
    uint8_t d[256];
    if (rd(ctx, base + 24 + 8, d, sizeof d) != sizeof d) return fail(o, "The file ends inside the image header.");
    if (u32(d) != APP_DESC_MAGIC) return fail(o, "This is not an application (a bootloader or data image?).");
    copystr(o->version, sizeof o->version, d + 16, 32);
    copystr(o->project, sizeof o->project, d + 48, 32);
    copystr(o->time, sizeof o->time, d + 80, 16);
    copystr(o->date, sizeof o->date, d + 96, 16);
    copystr(o->idf, sizeof o->idf, d + 112, 32);

    // walk the segments to find the end of the image
    uint32_t pos = base + 24;
    for (int i = 0; i < nseg; i++) {
        uint8_t s[8];
        if (rd(ctx, pos, s, 8) != 8) return fail(o, "The file is cut short (a segment header is missing).");
        uint32_t len = u32(s + 4);
        if (len > 16u * 1024 * 1024 || pos + 8 + len > size) return fail(o, "The file is cut short (a segment is incomplete).");
        pos += 8 + len;
    }
    uint32_t rel = pos - base;
    rel = (rel + 16) & ~15u;                              // padding with the checksum as its last byte
    o->has_sha = h[23] == 1;
    if (o->has_sha) {
        if (base + rel + 32 > size) return fail(o, "The file is cut short (the SHA-256 at the end is missing).");
        if (rd(ctx, base + rel, o->sha256, 32) != 32) return fail(o, "Could not read the image's SHA-256.");
        rel += 32;
    } else if (base + rel > size) return fail(o, "The file is cut short (no checksum).");
    o->app_len = rel;
    o->ok = true;
}
