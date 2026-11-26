// Desktop preview of the MultiDeck touch interface: the real ui.cpp on LVGL, with a pretend card and
// pretend flash, rendered to PNG-ready PPM screenshots in test/out/ui/. Taps the screen like a finger.
//   LVGL=/path/to/lvgl-9.5.0 sh test/ui_sim.sh
#include "../main/launcher.h"
#include "../main/ui.h"
#include "lvgl.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int64_t simMs = 0;

// ---- pretend board ------------------------------------------------------------------------------
static int scenario = 0;                               // 0 = card with firmware, 1 = no card, 2 = empty card
static int64_t launchAt = -1;
static md::Entry launched;
static bool failCopy = false;

namespace md {
void scan(Catalog& c) {
    c = Catalog();
    c.dir = "/sdcard/firmware";
    if (scenario == 1) { c.sdOk = false; c.sdInfo = "No microSD card"; return; }
    c.sdOk = true;
    c.sdInfo = "SD32G, 31.9 GB";
    if (scenario == 2) return;
    auto add = [&](const char* title, const char* file, const char* ver, uint32_t size, int slot, bool last, const char* desc,
                   const char* problem = "", const char* warn = "", bool merged = false) {
        Entry e;
        e.title = title; e.file = file; e.path = std::string("/sdcard/firmware/") + file; e.version = ver;
        e.project = title; e.idf = "v6.1"; e.built = "Nov 25 2026 20:51:02"; e.fileSize = size; e.appLen = size - 60000;
        e.slot = slot; e.last = last; e.desc = desc; e.problem = problem; e.warning = warn; e.merged = merged;
        c.entries.push_back(e);
    };
    add("RigDeck", "RigDeck-1.1.6-beta.bin", "1.1.6-beta", 2957376, 0, true,
        "FT8 / FT4 station, logger and control head.\nPlug the radio's USB cable into the board.", "", "", true);
    add("RigDeck stable", "rigdeck-1.0.0.bin", "1.0.0", 2204080, -1, false, "The first stable RigDeck release.", "", "", true);
    add("Meshtastic", "meshtastic-p4.bin", "2.7.15", 3100000, 1, false, "LoRa mesh messenger (needs a LoRa module on the header).");
    add("Weather Station", "weather.bin", "0.9.2", 1450000, -1, false, "");
    add("Wi-Fi chip", "rigdeck-wifi-chip.bin", "esp-hosted-3.1.0", 1151088, -1, false, "",
        "Built for ESP32-C6, not for this board's ESP32-P4.");
    add("lvgl_demo", "lvgl_demo.bin", "v1.0", 1800000, -1, false, "LVGL widgets demo.", "",
        "Built for a different flash layout (its \"storage\" partition is elsewhere): its settings may not be kept.", true);
}
bool launch(const Entry& e) { launched = e; launchAt = simMs; return true; }
Progress progress() {
    Progress p;
    if (launchAt < 0) return p;
    int64_t t = simMs - launchAt;
    if (launched.slot >= 0) { p.phase = t < 600 ? Progress::Checking : Progress::Starting; p.percent = 100;
        p.message = (t < 600 ? "Checking " : "Starting ") + launched.title + " ..."; return p; }
    int pct = (int)(t / 60);
    if (failCopy && pct >= 37) { p.phase = Progress::Failed; p.message = "Reading the card failed part-way. Nothing was started: check the card and try again."; return p; }
    if (pct > 100) { p.phase = Progress::Checking; p.percent = 100; p.message = "Checking the copy ..."; return p; }
    char m[96];
    snprintf(m, sizeof m, "Copying %s: %.1f of %.1f MB", launched.title.c_str(), launched.appLen * pct / 100.0 / 1048576.0,
             launched.appLen / 1048576.0);
    p.phase = Progress::Copying; p.percent = pct; p.message = m;
    return p;
}
const char* returnHint() {
    return "To come back here from any firmware: tap RESET, then straight away press and hold BOOT until MultiDeck appears.";
}
std::string fwVersion() { return "1.0.0"; }
}  // namespace md

// ---- LVGL display + scripted pointer ------------------------------------------------------------
static const int W = UI_W, H = UI_H;
static uint16_t fb[W * H];
static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    int w = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; y++) memcpy(&fb[y * W + a->x1], px + (size_t)(y - a->y1) * w * 2, (size_t)w * 2);
    lv_display_flush_ready(d);
}
static bool touchDown = false;
static int touchX = 0, touchY = 0;
static void touchRead(lv_indev_t*, lv_indev_data_t* data) {
    data->point.x = touchX;
    data->point.y = touchY;
    data->state = touchDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static uint32_t tickCb() { return (uint32_t)simMs; }
static void shot(const char* name) {
    char path[128];
    snprintf(path, sizeof path, "test/out/ui/%s.ppm", name);
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t p = fb[i];
        uint8_t rgb[3] = {(uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s\n", path);
}
static uint16_t px(int x, int y) { return fb[y * W + x]; }
static void run(double s) { for (int i = 0; i < s * 100; i++) { simMs += 10; lv_timer_handler(); } }
static void tap(int x, int y) {
    touchX = x; touchY = y; touchDown = true;
    run(0.12);
    touchDown = false;
    run(0.25);
}
// tile i on the main screen (4 per row, 232 x 212, gap 16, from x 24 / y 76 + 18)
static void tapTile(int i) { tap(24 + (i % 4) * 248 + 116, 76 + 18 + (i / 4) * 228 + 100); }

int main() {
    mkdir("test/out", 0755);
    mkdir("test/out/ui", 0755);
    lv_init();
    lv_tick_set_cb(tickCb);
    lv_display_t* disp = lv_display_create(W, H);
    static uint8_t drawBuf[W * 120 * 2];
    lv_display_set_buffers(disp, drawBuf, nullptr, sizeof drawBuf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush);
    lv_indev_t* in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, touchRead);

    ui_create();
    run(1.0);
    shot("1_splash");
    CHECK(px(5, 5) == 0);                                // black start-up screen
    run(3.0);
    shot("2_main");
    CHECK(px(5, 300) != 0);                              // splash gone: the dark-blue background

    tapTile(0);                                          // RigDeck: on the board, started last
    shot("3_detail_on_board");
    tap(1024 / 2 - 400 + 800 - 24 - 90, 300 - 235 + 470 - 24 - 30);   // Start
    run(0.3);
    shot("4_starting_from_slot");
    CHECK(launchAt >= 0 && launched.title == "RigDeck");

    // a fresh copy from the card (needs a new screen: the board would restart; here we rebuild)
    lv_obj_clean(lv_layer_top());
    launchAt = -1;
    ui_create();
    run(4.0);
    tapTile(1);
    shot("5_detail_copy");
    tap(1024 / 2 - 400 + 800 - 24 - 90, 300 - 235 + 470 - 24 - 30);
    run(2.4);
    shot("6_copying");
    CHECK(launched.title == "RigDeck stable");

    lv_obj_clean(lv_layer_top());
    launchAt = -1;
    ui_create();
    run(4.0);
    tapTile(4);                                          // the C6 image
    shot("7_detail_wrong_chip");
    tap(1024 / 2 - 400 + 800 - 24 - 90, 300 - 235 + 470 - 24 - 30);   // Start is off
    CHECK(launchAt < 0);
    tap(1024 / 2 - 400 + 800 - 24 - 180 - 16 - 90, 300 - 235 + 470 - 24 - 30);   // Cancel
    tapTile(5);
    shot("8_detail_warning");
    tap(20, 20);                                         // tap outside the card: closes
    failCopy = true;
    tapTile(3);
    tap(1024 / 2 - 400 + 800 - 24 - 90, 300 - 235 + 470 - 24 - 30);
    run(3.0);
    shot("9_copy_failed");
    tap(512, 480 + 28);                                  // Back
    failCopy = false;
    launchAt = -1;

    scenario = 1;
    tap(1024 - 24 - 75, 38);                             // Rescan
    run(0.5);
    shot("10_no_card");
    scenario = 2;
    tap(1024 - 24 - 75, 38);
    run(0.5);
    shot("11_empty_card");

    printf("ui_sim: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
