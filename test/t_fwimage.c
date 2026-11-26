// Host test of main/fwimage.c:  sh test/build.sh [image.bin ...]
// Synthetic images (app, merged, damaged, wrong chip) and any real .bin files given on the command
// line. For every image with a SHA-256 the test recomputes the digest over the bytes fwimage says
// belong to the app: a match proves the length and offset are right.
#include "../main/fwimage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---- small SHA-256 ------------------------------------------------------------------------------
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256(const uint8_t* m, size_t len, uint8_t out[32]) {
    uint32_t H[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t total = ((len + 9 + 63) / 64) * 64;
    uint8_t* b = calloc(total, 1);
    memcpy(b, m, len);
    b[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) b[total - 1 - i] = (uint8_t)(bits >> (8 * i));
    for (size_t c = 0; c < total; c += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[c + 4 * i] << 24 | b[c + 4 * i + 1] << 16 | b[c + 4 * i + 2] << 8 | b[c + 4 * i + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = H[0], bb = H[1], cc = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & bb) ^ (a & cc) ^ (bb & cc));
            h = g; g = f; f = e; e = d + t1; d = cc; cc = bb; bb = a; a = t1 + t2;
        }
        H[0] += a; H[1] += bb; H[2] += cc; H[3] += d; H[4] += e; H[5] += f; H[6] += g; H[7] += h;
    }
    free(b);
    for (int i = 0; i < 8; i++) for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(H[i] >> (24 - 8 * j));
}

// ---- memory reader ------------------------------------------------------------------------------
typedef struct { const uint8_t* d; size_t n; } Mem;
static size_t memread(void* ctx, uint32_t off, void* buf, size_t n) {
    Mem* m = ctx;
    if (off >= m->n) return 0;
    if (off + n > m->n) n = m->n - off;
    memcpy(buf, m->d + off, n);
    return n;
}

// ---- synthetic app image as esptool writes it ---------------------------------------------------
static size_t make_app(uint8_t* out, uint16_t chip, int nseg, const int* seglen, int hash, uint16_t minrev) {
    memset(out, 0, 24);
    out[0] = 0xE9; out[1] = (uint8_t)nseg; out[12] = chip & 0xFF; out[13] = chip >> 8;
    out[15] = minrev & 0xFF; out[16] = minrev >> 8; out[17] = 0xFF; out[18] = 0xFF; out[23] = (uint8_t)hash;
    size_t p = 24;
    for (int s = 0; s < nseg; s++) {
        uint32_t L = (uint32_t)seglen[s];
        uint32_t addr = 0x40000000u + s * 0x10000;
        for (int i = 0; i < 4; i++) { out[p + i] = (uint8_t)(addr >> 8 * i); out[p + 4 + i] = (uint8_t)(L >> 8 * i); }
        p += 8;
        for (uint32_t i = 0; i < L; i++) out[p + i] = (uint8_t)(i * 7 + s);
        if (s == 0) {
            uint32_t mg = 0xABCD5432;
            for (int i = 0; i < 4; i++) out[p + i] = (uint8_t)(mg >> 8 * i);
            memset(out + p + 4, 0, 252);
            strcpy((char*)out + p + 16, "1.2.3");
            strcpy((char*)out + p + 48, "demoapp");
            strcpy((char*)out + p + 80, "12:00:00");
            strcpy((char*)out + p + 96, "Nov 25 2026");
            strcpy((char*)out + p + 112, "v6.1");
        }
        p += L;
    }
    size_t align = 15 - (p % 16);                        // esptool: align_file_position(f, 16) + checksum byte
    memset(out + p, 0, align);
    p += align;
    out[p++] = 0xEF;
    if (hash) { sha256(out, p, out + p); p += 32; }
    return p;
}

static void put_part(uint8_t* e, uint8_t type, uint8_t sub, uint32_t off, uint32_t size, const char* label) {
    memset(e, 0, 32);
    e[0] = 0xAA; e[1] = 0x50; e[2] = type; e[3] = sub;
    for (int i = 0; i < 4; i++) { e[4 + i] = (uint8_t)(off >> 8 * i); e[8 + i] = (uint8_t)(size >> 8 * i); }
    strncpy((char*)e + 12, label, 16);
}

static void check_sha(const uint8_t* d, const fwi_info_t* fi, const char* what) {
    if (!fi->ok || !fi->has_sha) return;
    uint8_t s[32];
    sha256(d + fi->app_offset, fi->app_len - 32, s);
    int same = memcmp(s, fi->sha256, 32) == 0;
    printf("  %s: SHA-256 over the app bytes %s the digest in the file\n", what, same ? "matches" : "DOES NOT match");
    CHECK(same);
}

static void synthetic(void) {
    static uint8_t buf[1 << 20];
    fwi_info_t fi;
    for (int L0 = 256; L0 < 300; L0++) {                 // every padding case
        int segs[3] = {L0, 1000 + L0 % 7, 33};
        size_t n = make_app(buf, FWI_CHIP_ESP32P4, 3, segs, 1, 100);
        Mem m = {buf, n};
        fwi_inspect(memread, &m, (uint32_t)n, &fi);
        CHECK(fi.ok);
        CHECK(fi.app_len == n);
        CHECK(fi.app_offset == 0);
        CHECK(!fi.merged);
        CHECK(fi.chip_id == FWI_CHIP_ESP32P4);
        CHECK(fi.min_rev == 100);
        CHECK(strcmp(fi.project, "demoapp") == 0 && strcmp(fi.version, "1.2.3") == 0 && strcmp(fi.idf, "v6.1") == 0);
        uint8_t s[32];
        sha256(buf, n - 32, s);
        CHECK(memcmp(s, fi.sha256, 32) == 0);
        Mem cut = {buf, n - 5};                              // cut short
        fwi_inspect(memread, &cut, (uint32_t)n - 5, &fi);
        CHECK(!fi.ok);
    }
    {   // no hash appended
        int segs[2] = {300, 77};
        size_t n = make_app(buf, FWI_CHIP_ESP32P4, 2, segs, 0, 0);
        Mem m = {buf, n};
        fwi_inspect(memread, &m, (uint32_t)n, &fi);
        CHECK(fi.ok && !fi.has_sha && fi.app_len == n);
    }
    {   // merged image: bootloader-ish blob at 0, table at 0x8000, app at 0x10000, data after it
        static uint8_t mg[0x10000 + 0x20000];
        memset(mg, 0xFF, sizeof mg);
        // 0x0-0x2000: 0xFF, as in a real ESP32-P4 merged image (bootloader at 0x2000)
        put_part(mg + 0x8000, 1, 2, 0x9000, 0x6000, "nvs");
        put_part(mg + 0x8020, 0, 0, 0x10000, 0x300000, "factory");
        put_part(mg + 0x8040, 0, 0x10, 0x800000, 0x400000, "ota_0");
        mg[0x8060] = 0xEB; mg[0x8061] = 0xEB;
        int segs[2] = {4000, 999};
        size_t n = make_app(mg + 0x10000, FWI_CHIP_ESP32P4, 2, segs, 1, 0);
        Mem m = {mg, 0x10000 + n + 4096};                     // trailing bytes past the app are ignored
        fwi_inspect(memread, &m, (uint32_t)m.n, &fi);
        CHECK(fi.ok && fi.merged && fi.app_offset == 0x10000 && fi.app_len == n);
        CHECK(fi.nparts == 3 && strcmp(fi.parts[1].label, "factory") == 0);
        check_sha(mg, &fi, "synthetic merged");
    }
    {   // damaged
        int segs[1] = {300};
        size_t n = make_app(buf, FWI_CHIP_ESP32P4, 1, segs, 1, 0);
        Mem m = {buf, n};
        buf[0] = 0x00;
        fwi_inspect(memread, &m, (uint32_t)n, &fi);
        CHECK(!fi.ok && strstr(fi.error, "0xE9"));
        buf[0] = 0xE9; buf[32] ^= 0xFF;                       // app descriptor magic
        fwi_inspect(memread, &m, (uint32_t)n, &fi);
        CHECK(!fi.ok && strstr(fi.error, "not an application"));
        buf[32] ^= 0xFF; buf[1] = 0;
        fwi_inspect(memread, &m, (uint32_t)n, &fi);
        CHECK(!fi.ok);
    }
}

static void real_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("  (cannot open %s)\n", path); return; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* d = malloc(n);
    if (fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return; }
    fclose(f);
    Mem m = {d, (size_t)n};
    fwi_info_t fi;
    fwi_inspect(memread, &m, (uint32_t)n, &fi);
    printf("%s (%ld bytes): %s\n", path, n, fi.ok ? "app found" : fi.error);
    if (fi.ok) {
        printf("  project %s, version %s, built %s %s, IDF %s, chip 0x%04x, rev %u..%u\n", fi.project, fi.version, fi.date,
               fi.time, fi.idf, fi.chip_id, fi.min_rev, fi.max_rev);
        printf("  %s, app at 0x%x, %u bytes%s\n", fi.merged ? "merged image" : "app image", fi.app_offset, fi.app_len,
               fi.has_sha ? " incl. SHA-256" : "");
        for (int i = 0; i < fi.nparts; i++)
            printf("    part %-9s type %u sub 0x%02x at 0x%06x size 0x%06x\n", fi.parts[i].label, fi.parts[i].type,
                   fi.parts[i].subtype, fi.parts[i].offset, fi.parts[i].size);
        check_sha(d, &fi, path);
    }
    CHECK(fi.ok);
    free(d);
}

int main(int argc, char** argv) {
    synthetic();
    for (int i = 1; i < argc; i++) real_file(argv[i]);
    printf("t_fwimage: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
