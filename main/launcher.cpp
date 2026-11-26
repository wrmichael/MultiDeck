// Board side of the launcher: lists the firmware on the microSD card, knows what is already in the
// two app slots, and copies / selects / starts the one picked on the screen.
//
// Slots: ota_0 and ota_1 (4 MB each) keep the last two firmware started. An image is matched to a
// slot by its SHA-256 (the digest esptool appends to every image, which esp_partition_get_sha256
// reports for a flashed app), so starting one of those again needs no copying, and an image that a
// firmware replaced in a slot by itself (e.g. RigDeck's own Wi-Fi update) is simply no longer matched.
#include "launcher.h"
#include "fwimage.h"
#include "sdcard.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <sys/stat.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/efuse_hal.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char* TAG = "launcher";

#ifndef CONFIG_MULTIDECK_FW_DIR
#define CONFIG_MULTIDECK_FW_DIR "/sdcard/firmware"
#endif

namespace md {

static std::mutex s_mx;
static Progress s_prog;
static bool s_busy = false;

static void setProgress(Progress::Phase ph, int pct, const std::string& msg) {
    std::lock_guard<std::mutex> l(s_mx);
    s_prog.phase = ph;
    s_prog.percent = pct;
    s_prog.message = msg;
}
Progress progress() {
    std::lock_guard<std::mutex> l(s_mx);
    return s_prog;
}

const char* returnHint() {
    return "To come back here from any firmware: tap RESET, then straight away press and hold BOOT until MultiDeck appears.";
}
std::string fwVersion() { return esp_app_get_description()->version; }

// ---- helpers --------------------------------------------------------------------------------------
static std::string hex(const uint8_t* d, size_t n) {
    static const char* x = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += x[d[i] >> 4]; s += x[d[i] & 15]; }
    return s;
}
static const char* chipName(uint16_t id) {
    switch (id) {
        case 0x0000: return "ESP32";
        case 0x0002: return "ESP32-S2";
        case 0x0005: return "ESP32-C3";
        case 0x0009: return "ESP32-S3";
        case 0x000C: return "ESP32-C2";
        case 0x000D: return "ESP32-C6";
        case 0x0010: return "ESP32-H2";
        case 0x0012: return "ESP32-P4";
        case 0x0014: return "ESP32-C61";
        case 0x0017: return "ESP32-C5";
        default: return "another chip";
    }
}
static bool endsWithBin(const char* n) {
    size_t l = strlen(n);
    return l > 4 && n[l - 4] == '.' && tolower((unsigned char)n[l - 3]) == 'b' && tolower((unsigned char)n[l - 2]) == 'i' &&
           tolower((unsigned char)n[l - 1]) == 'n';
}
static size_t fileRead(void* ctx, uint32_t off, void* buf, size_t n) {
    FILE* f = (FILE*)ctx;
    if (fseek(f, (long)off, SEEK_SET) != 0) return 0;
    return fread(buf, 1, n, f);
}
static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || (uint8_t)s[i] == 0xEF || (uint8_t)s[i] == 0xBB || (uint8_t)s[i] == 0xBF)) i++;
    return s.substr(i);
}
// <name>.txt next to <name>.bin: first line = title, the rest = description (a few lines).
static void readSidecar(const std::string& binPath, Entry& e) {
    std::string p = binPath.substr(0, binPath.size() - 4) + ".txt";
    FILE* f = fopen(p.c_str(), "r");
    if (!f) { p = binPath.substr(0, binPath.size() - 4) + ".TXT"; f = fopen(p.c_str(), "r"); }
    if (!f) return;
    char line[160];
    int n = 0;
    while (fgets(line, sizeof line, f) && n < 8) {
        std::string s = trim(line);
        if (n == 0) { if (!s.empty()) e.title = s.substr(0, 40); }
        else { if (!e.desc.empty()) e.desc += "\n"; e.desc += s; }
        n++;
    }
    fclose(f);
    e.desc = trim(e.desc);
}

static const esp_partition_t* slotPart(int i) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, i == 0 ? ESP_PARTITION_SUBTYPE_APP_OTA_0 : ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
}

struct SlotInfo { bool valid = false; std::string sha; esp_app_desc_t desc{}; };

static std::string nvsGetStr(const char* key) {
    nvs_handle_t h;
    std::string out;
    if (nvs_open("multideck", NVS_READONLY, &h) != ESP_OK) return out;
    char buf[80];
    size_t n = sizeof buf;
    if (nvs_get_str(h, key, buf, &n) == ESP_OK) out = buf;
    nvs_close(h);
    return out;
}
static void nvsSet(const char* last, int slot) {
    nvs_handle_t h;
    if (nvs_open("multideck", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "last", last);
    nvs_set_u8(h, "lastslot", (uint8_t)slot);
    nvs_commit(h);
    nvs_close(h);
}
static int nvsLastSlot() {
    nvs_handle_t h;
    uint8_t v = 0xFF;
    if (nvs_open("multideck", NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, "lastslot", &v); nvs_close(h); }
    return v <= 1 ? v : -1;
}

// ---- scan -----------------------------------------------------------------------------------------
static void checkEntry(Entry& e, const fwi_info_t& fi) {
    uint32_t rev = efuse_hal_chip_revision();          // major * 100 + minor
    const esp_partition_t* s0 = slotPart(0);
    if (fi.chip_id != FWI_CHIP_ESP32P4) {
        e.problem = std::string("Built for ") + chipName(fi.chip_id) + ", not for this board's ESP32-P4.";
    } else if (rev < fi.min_rev) {
        char b[120];
        snprintf(b, sizeof b, "Needs chip revision v%u.%u or newer; this board's ESP32-P4 is v%u.%u. Rebuild it for this chip.",
                 fi.min_rev / 100, fi.min_rev % 100, (unsigned)(rev / 100), (unsigned)(rev % 100));
        e.problem = b;
    } else if (fi.max_rev != 0xFFFF && rev > fi.max_rev) {
        char b[120];
        snprintf(b, sizeof b, "Built for chip revisions up to v%u.%u; this board is v%u.%u.", fi.max_rev / 100, fi.max_rev % 100,
                 (unsigned)(rev / 100), (unsigned)(rev % 100));
        e.problem = b;
    } else if (s0 && fi.app_len > s0->size) {
        char b[100];
        snprintf(b, sizeof b, "Too big: %.2f MB, the app slots hold %.0f MB.", fi.app_len / 1048576.0, s0->size / 1048576.0);
        e.problem = b;
    } else if (strcmp(fi.project, "multideck") == 0) {
        e.problem = "This is MultiDeck itself. Update MultiDeck over USB instead.";
    }
    // A full-flash image carries its own partition table: if its data partitions are not where this
    // board has them, the firmware will look for its settings in the wrong place.
    if (fi.merged && e.problem.empty()) {
        for (int i = 0; i < fi.nparts; i++) {
            const fwi_part_t& p = fi.parts[i];
            if (p.type != 1 || p.subtype == 0x00) continue;   // data partitions; otadata is MultiDeck's
            const esp_partition_t* q = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, p.label);
            if (!q || q->address != p.offset || q->size != p.size) {
                e.warning = std::string("Built for a different flash layout (its \"") + p.label +
                            "\" partition is elsewhere): its settings may not be kept.";
                break;
            }
        }
    }
}

void scan(Catalog& out) {
    out = Catalog();
    out.dir = CONFIG_MULTIDECK_FW_DIR;
    char info[64];
    out.sdOk = sdcard_mount(info, sizeof info);
    out.sdInfo = info;

    SlotInfo slots[2];
    for (int i = 0; i < 2; i++) {
        const esp_partition_t* p = slotPart(i);
        uint8_t sha[32];
        if (p && esp_ota_get_partition_description(p, &slots[i].desc) == ESP_OK && esp_partition_get_sha256(p, sha) == ESP_OK) {
            slots[i].valid = true;
            slots[i].sha = hex(sha, 32);
        }
    }
    std::string last = nvsGetStr("last");
    bool inSlot[2] = {false, false};

    if (out.sdOk) {
        DIR* d = opendir(out.dir.c_str());
        if (d) {
            struct dirent* de;
            while ((de = readdir(d)) != nullptr && out.entries.size() < 64) {
                const char* n = de->d_name;
                if (n[0] == '.' || n[0] == '_' || !endsWithBin(n)) continue;   // also macOS "._name.bin" files
                Entry e;
                e.file = n;
                e.path = out.dir + "/" + n;
                struct stat st;
                if (stat(e.path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                e.fileSize = (uint32_t)st.st_size;
                FILE* f = fopen(e.path.c_str(), "rb");
                if (!f) continue;
                setvbuf(f, nullptr, _IOFBF, 1024);
                fwi_info_t* fi = (fwi_info_t*)malloc(sizeof(fwi_info_t));
                if (!fi) { fclose(f); continue; }
                fwi_inspect(fileRead, f, e.fileSize, fi);
                fclose(f);
                e.title = e.file.substr(0, e.file.size() - 4);
                if (!fi->ok) {
                    e.problem = fi->error;
                } else {
                    e.project = fi->project;
                    e.version = fi->version;
                    e.idf = fi->idf;
                    e.built = std::string(fi->date) + " " + fi->time;
                    e.appLen = fi->app_len;
                    e.appOffset = fi->app_offset;
                    e.merged = fi->merged;
                    if (fi->has_sha) e.sha = hex(fi->sha256, 32);
                    checkEntry(e, *fi);
                }
                free(fi);
                readSidecar(e.path, e);
                for (int i = 0; i < 2; i++)
                    if (!e.sha.empty() && slots[i].valid && slots[i].sha == e.sha && e.slot < 0) { e.slot = i; inSlot[i] = true; }
                e.last = !e.sha.empty() && e.sha == last;
                out.entries.push_back(e);
            }
            closedir(d);
        }
    }
    std::sort(out.entries.begin(), out.entries.end(), [](const Entry& a, const Entry& b) {
        std::string x = a.title, y = b.title;
        for (auto& c : x) c = (char)tolower((unsigned char)c);
        for (auto& c : y) c = (char)tolower((unsigned char)c);
        return x < y;
    });
    // firmware that is on the board but not (or no longer) on the card can still be started
    for (int i = 0; i < 2; i++) {
        if (!slots[i].valid || inSlot[i]) continue;
        if (strcmp(slots[i].desc.project_name, "multideck") == 0) continue;
        Entry e;
        e.title = slots[i].desc.project_name;
        e.project = slots[i].desc.project_name;
        e.version = slots[i].desc.version;
        e.idf = slots[i].desc.idf_ver;
        e.built = std::string(slots[i].desc.date) + " " + slots[i].desc.time;
        e.file = i == 0 ? "on the board (slot ota_0)" : "on the board (slot ota_1)";
        e.slot = i;
        e.sha = slots[i].sha;
        e.last = e.sha == last;
        out.entries.push_back(e);
    }
    ESP_LOGI(TAG, "%s: %d firmware listed (card: %s)", out.dir.c_str(), (int)out.entries.size(), out.sdInfo.c_str());
}

// ---- launch ---------------------------------------------------------------------------------------
static void startSlot(int slot, const std::string& sha, const std::string& title) {
    const esp_partition_t* p = slotPart(slot);
    setProgress(Progress::Checking, 100, "Checking " + title + " ...");
    esp_err_t e = esp_ota_set_boot_partition(p);           // checks the whole image before selecting it
    if (e != ESP_OK) {
        setProgress(Progress::Failed, 0, std::string("The firmware in the slot did not pass its check (") + esp_err_to_name(e) + ").");
        return;
    }
    nvsSet(sha.c_str(), slot);
    setProgress(Progress::Starting, 100, "Starting " + title + " ...");
    ESP_LOGI(TAG, "starting %s from %s", title.c_str(), p->label);
    vTaskDelay(pdMS_TO_TICKS(900));
    esp_restart();
}

static bool copyToSlot(const Entry& en, int slot) {
    const esp_partition_t* part = slotPart(slot);
    FILE* f = fopen(en.path.c_str(), "rb");
    if (!f) { setProgress(Progress::Failed, 0, "Could not open " + en.file + " on the card."); return false; }
    const size_t BUF = 32 * 1024;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(BUF, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!buf) buf = (uint8_t*)malloc(BUF);
    if (!buf) { fclose(f); setProgress(Progress::Failed, 0, "Out of memory."); return false; }
    setvbuf(f, nullptr, _IONBF, 0);                        // straight into the DMA buffer
    esp_ota_handle_t h = 0;
    setProgress(Progress::Erasing, 0, "Preparing slot " + std::string(part->label) + " ...");
    // Sequential writes: each flash sector is erased just before it is written, so the bar moves smoothly.
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h);
    bool ok = err == ESP_OK && fseek(f, (long)en.appOffset, SEEK_SET) == 0;
    uint32_t done = 0;
    while (ok && done < en.appLen) {
        size_t want = std::min<size_t>(BUF, en.appLen - done);
        size_t got = fread(buf, 1, want, f);
        if (got != want) { ok = false; setProgress(Progress::Failed, 0, "Reading the card failed part-way. Nothing was started: check the card and try again."); break; }
        err = esp_ota_write(h, buf, got);
        if (err != ESP_OK) { ok = false; break; }
        done += got;
        char m[96];
        snprintf(m, sizeof m, "Copying %s: %.1f of %.1f MB", en.title.c_str(), done / 1048576.0, en.appLen / 1048576.0);
        setProgress(Progress::Copying, (int)((uint64_t)done * 100 / en.appLen), m);
    }
    fclose(f);
    free(buf);
    if (!ok) {
        if (h) esp_ota_abort(h);
        if (progress().phase != Progress::Failed)
            setProgress(Progress::Failed, 0, std::string("Writing to flash failed (") + esp_err_to_name(err) + "). Nothing was started.");
        return false;
    }
    setProgress(Progress::Checking, 100, "Checking the copy ...");
    err = esp_ota_end(h);                                  // verifies segments and the SHA-256
    if (err != ESP_OK) {
        setProgress(Progress::Failed, 0, std::string("The copied firmware failed its check (") + esp_err_to_name(err) +
                                             "). The file on the card may be damaged.");
        return false;
    }
    return true;
}

static void launchTask(void* arg) {
    Entry* en = (Entry*)arg;
    int slot = en->slot;
    if (slot < 0) {
        // Use an empty slot, else the one that does not hold the firmware started last.
        bool valid[2];
        for (int i = 0; i < 2; i++) {
            esp_app_desc_t d;
            valid[i] = esp_ota_get_partition_description(slotPart(i), &d) == ESP_OK;
        }
        int lastSlot = nvsLastSlot();
        slot = !valid[0] ? 0 : !valid[1] ? 1 : (lastSlot == 0 ? 1 : 0);
        if (copyToSlot(*en, slot)) startSlot(slot, en->sha, en->title);
    } else {
        startSlot(slot, en->sha, en->title);
    }
    delete en;
    {
        std::lock_guard<std::mutex> l(s_mx);
        s_busy = false;
    }
    vTaskDelete(nullptr);
}

bool launch(const Entry& e) {
    {
        std::lock_guard<std::mutex> l(s_mx);
        if (s_busy) return false;
        s_busy = true;
        s_prog = Progress();
    }
    Entry* copy = new Entry(e);
    if (xTaskCreatePinnedToCore(launchTask, "launch", 8192, copy, 4, nullptr, 1) != pdPASS) {
        delete copy;
        std::lock_guard<std::mutex> l(s_mx);
        s_busy = false;
        return false;
    }
    return true;
}

}  // namespace md
