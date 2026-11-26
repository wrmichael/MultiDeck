// MultiDeck touch interface (LVGL 9, 1024 x 600). Same look as RigDeck: its dark palette, its gold
// Audiowide start-up screen ("goldText", carried over from RigDeck's ui.cpp) and Montserrat text.
//
//   start-up screen  gold "MultiDeck", "Firmware launcher by AC9HP", version; fades after a few seconds
//   main screen      a tile for each firmware on the card (gold monogram, name, version, state)
//   details          what the file is, whether it is already on the board, Start / Cancel
//   progress         copying / checking / starting, or what went wrong
//
// Runs on the board (with launcher.cpp) and on a PC (test/ui_sim.cpp) for screenshots.
#include "ui.h"
#include "launcher.h"
#include "lvgl.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef CONFIG_MULTIDECK_SPLASH_MS
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif
#ifndef CONFIG_MULTIDECK_SPLASH_MS
#define CONFIG_MULTIDECK_SPLASH_MS 2500
#endif

static const int W = UI_W, H = UI_H;
// RigDeck's colours
static const uint32_t BG = 0x0d1317, PAN = 0x151e25, PAN2 = 0x1c2831, LINE = 0x2a3944, RED = 0xff5b5b, GREEN = 0x3fcf8e,
                      AMB = 0xf6b93b, TXT = 0xdce5ea, DIM = 0x8498a5, GOLD = 0xD4AF5A, GOLD_DIM = 0x8A7A55;
static inline lv_color_t C(uint32_t rgb) { return lv_color_hex(rgb); }
static const lv_font_t* f16 = &lv_font_montserrat_16;
static const lv_font_t* f20 = &lv_font_montserrat_20;
static const lv_font_t* f28 = &lv_font_montserrat_28;

extern "C" const uint8_t audiowide_call_ttf[];
extern "C" const size_t audiowide_call_ttf_len;

// ---- gold lettering (from RigDeck) -------------------------------------------------------------
// Soft bevel lit from the upper left with a highlight, metal bands across the letter height, a fine
// brushed texture, a dark rim and a warm shadow, all computed from the letter shapes.
static void boxBlur(const float* in, float* out, float* tmp, int w, int h, int r) {
    for (int y = 0; y < h; y++) {
        const float* row = in + y * w;
        float acc = 0;
        for (int x = -r; x <= r; x++) acc += row[x < 0 ? 0 : x >= w ? w - 1 : x];
        for (int x = 0; x < w; x++) {
            tmp[y * w + x] = acc / (2 * r + 1);
            int xa = x + r + 1, xr = x - r;
            acc += row[xa >= w ? w - 1 : xa] - row[xr < 0 ? 0 : xr];
        }
    }
    for (int x = 0; x < w; x++) {
        float acc = 0;
        for (int y = -r; y <= r; y++) acc += tmp[(y < 0 ? 0 : y >= h ? h - 1 : y) * w + x];
        for (int y = 0; y < h; y++) {
            out[y * w + x] = acc / (2 * r + 1);
            int ya = y + r + 1, yr = y - r;
            acc += tmp[(ya >= h ? h - 1 : ya) * w + x] - tmp[(yr < 0 ? 0 : yr) * w + x];
        }
    }
}
static void gauss(const float* in, float* out, float* t1, float* t2, int w, int h, int r) {
    boxBlur(in, t1, t2, w, h, r);
    boxBlur(t1, out, t2, w, h, r);
    boxBlur(out, t1, t2, w, h, r);
    memcpy(out, t1, sizeof(float) * w * h);
}

// Gold text into an ARGB8888 draw buffer (background: `bg`). scale < 1 for small sizes: the bevel,
// rim and shadow shrink with the letters. False if out of memory.
static bool goldText(lv_draw_buf_t* buf, const char* text, uint32_t bg = 0x000000, float scale = 1.0f) {
    const int w = buf->header.w, h = buf->header.h;
    int size = h * 88 / 100;
    lv_font_t* f = nullptr;
    for (int tries = 0; tries < 4; tries++) {
        f = lv_tiny_ttf_create_data(audiowide_call_ttf, audiowide_call_ttf_len, size);
        if (!f) return false;
        lv_point_t sz;
        lv_text_get_size(&sz, text, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= w * 92 / 100 || tries == 3) break;
        int ns = size * (w * 92 / 100) / sz.x;
        lv_tiny_ttf_destroy(f);
        size = ns;
    }
    lv_obj_t* cv = lv_canvas_create(lv_layer_top());
    lv_canvas_set_draw_buf(cv, buf);
    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_TRANSP);
    lv_layer_t layer;
    lv_canvas_init_layer(cv, &layer);
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = f;
    d.color = lv_color_white();
    d.align = LV_TEXT_ALIGN_CENTER;
    d.text = text;
    int32_t lh = lv_font_get_line_height(f);
    lv_area_t a = {0, (h - lh) / 2, w - 1, (h - lh) / 2 + lh - 1};
    lv_draw_label(&layer, &d, &a);
    lv_canvas_finish_layer(cv, &layer);
    lv_obj_delete(cv);
    lv_tiny_ttf_destroy(f);

    const int n = w * h;
    float* M = (float*)malloc(sizeof(float) * n * 5);
    if (!M) return false;
    float *hg = M + n, *t1 = M + 2 * n, *t2 = M + 3 * n, *sh = M + 4 * n;
    uint8_t* px = (uint8_t*)buf->data;
    const uint32_t stride = buf->header.stride;
    int top = h, bot = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float v = px[y * stride + x * 4 + 3] / 255.0f;
            M[y * w + x] = v;
            if (v > 0.5f) { if (y < top) top = y; if (y > bot) bot = y; }
        }
    if (bot <= top) { free(M); return false; }

    int rb = (int)lroundf(3 * scale), rs = (int)lroundf(4 * scale);
    gauss(M, hg, t1, t2, w, h, rb < 1 ? 1 : rb);
    gauss(M, sh, t1, t2, w, h, rs < 1 ? 1 : rs);
    const float Lx = -0.55f, Ly = -0.75f, Lz = 0.36f, Ln = sqrtf(Lx * Lx + Ly * Ly + Lz * Lz);
    const float lx = Lx / Ln, ly = Ly / Ln, lz = Lz / Ln;
    float hx = lx, hy = ly, hz = lz + 1, hn = sqrtf(hx * hx + hy * hy + hz * hz);
    hx /= hn; hy /= hn; hz /= hn;
    struct Stop { float t, r, g, b; };
    static const Stop st[] = {{0.00f, 255, 236, 160}, {0.18f, 250, 210, 92}, {0.42f, 184, 128, 26}, {0.50f, 122, 82, 10},
                              {0.58f, 196, 142, 34},  {0.78f, 255, 214, 100}, {1.00f, 150, 100, 18}};
    const float bgR = (bg >> 16) & 255, bgG = (bg >> 8) & 255, bgB = bg & 255;
    const int ox = (int)lroundf(5 * scale), oy = (int)lroundf(6 * scale);
    uint32_t seed = 12345;
    for (int y = 0; y < h; y++) {
        float t = (float)(y - top) / (float)(bot - top);
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        int k = 0;
        while (k < 5 && t > st[k + 1].t) k++;
        float u = (t - st[k].t) / (st[k + 1].t - st[k].t);
        float br = st[k].r + (st[k + 1].r - st[k].r) * u, bgc = st[k].g + (st[k + 1].g - st[k].g) * u,
              bb = st[k].b + (st[k + 1].b - st[k].b) * u;
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            float m = M[i];
            float gx = (hg[y * w + (x < w - 1 ? x + 1 : x)] - hg[y * w + (x > 0 ? x - 1 : x)]) * 0.5f;
            float gy = (hg[(y < h - 1 ? y + 1 : y) * w + x] - hg[(y > 0 ? y - 1 : y) * w + x]) * 0.5f;
            float nx = -gx * 22 * scale, ny = -gy * 22 * scale, nz = 1, nn = sqrtf(nx * nx + ny * ny + nz * nz);
            nx /= nn; ny /= nn; nz /= nn;
            float dif = nx * lx + ny * ly + nz * lz; dif = dif < 0 ? 0 : dif;
            float sp = nx * hx + ny * hy + nz * hz; sp = sp < 0 ? 0 : sp;
            sp = powf(sp, 40);
            seed = seed * 1664525u + 1013904223u;
            float tex = 0.96f + 0.08f * ((seed >> 8) & 0xFFFF) / 65535.0f;
            float shade = (0.55f + 0.65f * dif) * tex;
            float r = br * shade + sp * 230, g = bgc * shade + sp * 225, b = bb * shade + sp * 200;
            int sx = x - ox, sy = y - oy;
            float s = (sx >= 0 && sy >= 0) ? sh[sy * w + sx] : 0;
            float rim = hg[i] > m ? (hg[i] - m) * 2.2f : 0;
            rim = rim > 1 ? 1 : rim;
            // outside the letters: warm shadow and dark rim over the background colour
            float shadeOut = 1 - 0.55f * s;
            float or_ = bgR * shadeOut + 48 * s + 70 * rim, og = bgG * shadeOut + 32 * s + 48 * rim, ob = bgB * shadeOut + 5 * s + 8 * rim;
            r = r * m + or_ * (1 - m); g = g * m + og * (1 - m); b = b * m + ob * (1 - m);
            uint8_t* p = px + y * stride + x * 4;
            p[0] = (uint8_t)(b > 255 ? 255 : b); p[1] = (uint8_t)(g > 255 ? 255 : g);
            p[2] = (uint8_t)(r > 255 ? 255 : r); p[3] = 255;
        }
    }
    free(M);
    return true;
}

// A gold image of `text`, w x h, or nullptr. The buffer is kept in `keep` until the next rebuild.
static std::vector<lv_draw_buf_t*> gBufs;
static lv_obj_t* goldImage(lv_obj_t* parent, const char* text, int w, int h, uint32_t bg, float scale) {
    lv_draw_buf_t* b = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_ARGB8888, 0);
    if (!b) return nullptr;
    if (!goldText(b, text, bg, scale)) { lv_draw_buf_destroy(b); return nullptr; }
    gBufs.push_back(b);
    lv_obj_t* im = lv_image_create(parent);
    lv_image_set_src(im, b);
    return im;
}

// Two letters for a tile: the capitals of "RigDeck" (RD), the first letters of "Mesh Core" (MC),
// else the first two letters (Te). Only letters and digits the Audiowide subset carries.
static std::string monogram(const std::string& t) {
    std::string caps, words, alnum;
    bool start = true;
    for (char c : t) {
        unsigned char u = (unsigned char)c;
        bool an = isalnum(u) && u < 128;
        if (an) {
            if (alnum.size() < 2) alnum += c;
            if (isupper(u)) caps += c;
            if (start) words += (char)toupper(u);
        }
        start = !an;
    }
    if (caps.size() >= 2) return caps.substr(0, 2);
    if (words.size() >= 2) return words.substr(0, 2);
    if (alnum.empty()) return "?";
    std::string m(1, (char)toupper((unsigned char)alnum[0]));
    if (alnum.size() > 1) m += (char)tolower((unsigned char)alnum[1]);
    return m;
}

// ---- small helpers --------------------------------------------------------------------------------
static lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t bg) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, C(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t* label(lv_obj_t* parent, int x, int y, const lv_font_t* f, uint32_t col, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, C(col), 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    return l;
}
static lv_obj_t* button(lv_obj_t* parent, int x, int y, int w, int h, uint32_t bg, uint32_t fg, const char* text, lv_event_cb_t cb,
                        void* ud = nullptr) {
    lv_obj_t* b = box(parent, x, y, w, h, bg);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, C(bg == AMB ? AMB : LINE), 0);
    lv_obj_set_style_bg_color(b, C(bg == AMB ? 0xffd27a : 0x26343f), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, f20, 0);
    lv_obj_set_style_text_color(l, C(fg), 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}
static std::string mb(uint32_t bytes) {
    char b[24];
    snprintf(b, sizeof b, "%.1f MB", bytes / 1048576.0);
    return b;
}

// ---- state ----------------------------------------------------------------------------------------
static md::Catalog gCat;
static lv_obj_t *gScr, *gGrid, *gEmpty, *gCardInfo, *gRescanBtn;
static lv_obj_t *gModal, *gProg, *gProgBar, *gProgPct, *gProgMsg, *gProgBack, *gProgTitle, *gProgHint;
static std::string gProgName;
static lv_obj_t* gSplash;
static lv_draw_buf_t* gSplashBuf;
static lv_timer_t* gProgTimer;
static bool gSplashDone = false;
static std::vector<lv_draw_buf_t*> gModalBufs;

static void rebuild();
static void onRescan(lv_event_t*);

// ---- progress screen ------------------------------------------------------------------------------
static void closeProgress(lv_event_t*) {
    if (gProgTimer) { lv_timer_delete(gProgTimer); gProgTimer = nullptr; }
    if (gProg) { lv_obj_delete(gProg); gProg = nullptr; }
    onRescan(nullptr);                                   // a half-written slot no longer holds what the tiles said
}
static void progTick(lv_timer_t*) {
    if (!gProg) return;
    md::Progress p = md::progress();
    lv_bar_set_value(gProgBar, p.percent, LV_ANIM_OFF);
    char pc[16];
    snprintf(pc, sizeof pc, "%d %%", p.percent);
    lv_label_set_text(gProgPct, p.phase == md::Progress::Copying ? pc : "");
    if (!p.message.empty()) lv_label_set_text(gProgMsg, p.message.c_str());
    if (p.phase == md::Progress::Failed) {
        lv_obj_set_style_text_color(gProgMsg, C(RED), 0);
        lv_obj_set_style_bg_color(gProgBar, C(RED), LV_PART_INDICATOR);
        lv_obj_remove_flag(gProgBack, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gProgHint, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(gProgTitle, ("Could not start " + gProgName).c_str());
        lv_timer_delete(gProgTimer);
        gProgTimer = nullptr;
    }
}
static void openProgress(const md::Entry& e) {
    gProg = box(lv_layer_top(), 0, 0, W, H, BG);
    lv_obj_add_flag(gProg, LV_OBJ_FLAG_CLICKABLE);            // nothing underneath can be tapped meanwhile
    if (lv_obj_t* im = goldImage(gProg, monogram(e.title).c_str(), 300, 170, BG, 0.8f)) lv_obj_align(im, LV_ALIGN_CENTER, 0, -150);
    gProgName = e.title;
    gProgTitle = label(gProg, 0, 0, f28, TXT, ("Starting " + e.title).c_str());
    lv_obj_align(gProgTitle, LV_ALIGN_CENTER, 0, -20);
    gProgBar = lv_bar_create(gProg);
    lv_obj_set_size(gProgBar, 640, 22);
    lv_obj_align(gProgBar, LV_ALIGN_CENTER, 0, 40);
    lv_bar_set_range(gProgBar, 0, 100);
    lv_obj_set_style_bg_color(gProgBar, C(PAN2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gProgBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(gProgBar, C(AMB), LV_PART_INDICATOR);
    lv_obj_set_style_radius(gProgBar, 11, LV_PART_MAIN);
    lv_obj_set_style_radius(gProgBar, 11, LV_PART_INDICATOR);
    gProgPct = label(gProg, 0, 0, f20, AMB, "");
    lv_obj_align(gProgPct, LV_ALIGN_CENTER, 0, 80);
    gProgMsg = label(gProg, 0, 0, f16, DIM, "Getting ready ...");
    lv_obj_set_width(gProgMsg, 760);
    lv_obj_set_style_text_align(gProgMsg, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(gProgMsg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(gProgMsg, LV_ALIGN_CENTER, 0, 125);
    gProgHint = label(gProg, 0, 0, f16, GOLD_DIM, "Leave the power on until it has started.");
    lv_obj_align(gProgHint, LV_ALIGN_BOTTOM_MID, 0, -24);
    gProgBack = button(gProg, (W - 200) / 2, 480, 200, 56, PAN2, TXT, "Back", closeProgress);
    lv_obj_add_flag(gProgBack, LV_OBJ_FLAG_HIDDEN);
    gProgTimer = lv_timer_create(progTick, 100, nullptr);
}

// ---- details --------------------------------------------------------------------------------------
static int gSel = -1;
static void closeModal(lv_event_t* = nullptr) {
    if (gModal) { lv_obj_delete(gModal); gModal = nullptr; }
    for (auto* b : gModalBufs) lv_draw_buf_destroy(b);
    gModalBufs.clear();
}
static void onStart(lv_event_t*) {
    if (gSel < 0 || gSel >= (int)gCat.entries.size()) return;
    md::Entry e = gCat.entries[gSel];
    closeModal();
    openProgress(e);
    if (!md::launch(e)) {
        lv_label_set_text(gProgMsg, "Could not start the copy.");
        progTick(nullptr);
    }
}
static void openDetail(int i) {
    const md::Entry& e = gCat.entries[i];
    gSel = i;
    gModal = box(lv_layer_top(), 0, 0, W, H, 0x000000);
    lv_obj_set_style_bg_opa(gModal, LV_OPA_70, 0);
    lv_obj_add_flag(gModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gModal, [](lv_event_t* ev) { if (lv_event_get_target(ev) == gModal) closeModal(); }, LV_EVENT_CLICKED, nullptr);
    const int CW = 800, CH = 470;
    lv_obj_t* card = box(gModal, (W - CW) / 2, (H - CH) / 2, CW, CH, PAN);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, C(LINE), 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);              // taps on the card do not close it

    size_t before = gBufs.size();
    if (lv_obj_t* im = goldImage(card, monogram(e.title).c_str(), 200, 120, PAN, 0.6f)) lv_obj_set_pos(im, 24, 28);
    gModalBufs.assign(gBufs.begin() + before, gBufs.end());   // freed with the card, not with the grid
    gBufs.resize(before);

    lv_obj_t* t = label(card, 244, 30, f28, TXT, e.title.c_str());
    lv_obj_set_width(t, CW - 268);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    std::string l1;
    if (!e.version.empty()) l1 += "Version " + e.version;
    if (!e.idf.empty()) l1 += (l1.empty() ? "" : "  -  ") + std::string("ESP-IDF ") + e.idf;
    label(card, 244, 74, f16, DIM, l1.c_str());
    if (!e.built.empty()) label(card, 244, 98, f16, DIM, ("Built " + e.built).c_str());
    std::string l2 = e.file;
    if (e.fileSize) l2 += "  (" + mb(e.fileSize) + (e.merged ? ", full-flash image" : "") + ")";
    lv_obj_t* fl = label(card, 244, 122, f16, DIM, l2.c_str());
    lv_obj_set_width(fl, CW - 268);
    lv_label_set_long_mode(fl, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t* d = label(card, 28, 172, f16, TXT, e.desc.empty() ? "" : e.desc.c_str());
    lv_obj_set_width(d, CW - 56);
    lv_obj_set_height(d, 108);
    lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);

    std::string status;
    uint32_t col = GREEN;
    if (!e.problem.empty()) { status = e.problem; col = RED; }
    else if (e.slot >= 0) status = "Already on the board: starts at once.";
    else {
        int s = (int)(e.appLen / 330000) + 3;                // card read + flash write, roughly
        char b[96];
        snprintf(b, sizeof b, "Copied to the board first (%s, about %d seconds).", mb(e.appLen).c_str(), s);
        status = b;
        col = AMB;
    }
    lv_obj_t* stl = label(card, 28, 292, f16, col, status.c_str());
    lv_obj_set_width(stl, CW - 56);
    lv_label_set_long_mode(stl, LV_LABEL_LONG_MODE_WRAP);
    if (!e.warning.empty()) {
        lv_obj_t* w = label(card, 28, 322, f16, AMB, e.warning.c_str());
        lv_obj_set_width(w, CW - 56);
        lv_label_set_long_mode(w, LV_LABEL_LONG_MODE_WRAP);
    }
    button(card, CW - 24 - 180 - 16 - 180, CH - 24 - 60, 180, 60, PAN2, TXT, "Cancel", closeModal);
    lv_obj_t* go = button(card, CW - 24 - 180, CH - 24 - 60, 180, 60, AMB, BG, LV_SYMBOL_PLAY "  Start", onStart);
    if (!e.problem.empty()) {
        lv_obj_set_style_bg_color(go, C(PAN2), 0);
        lv_obj_set_style_border_color(go, C(LINE), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(go, 0), C(DIM), 0);
        lv_obj_remove_flag(go, LV_OBJ_FLAG_CLICKABLE);
    }
}

// ---- main screen ----------------------------------------------------------------------------------
static const int HEAD_H = 76, FOOT_H = 46, TILE_W = 232, TILE_H = 212, GAP = 16, PAD_X = 24;

static void onTile(lv_event_t* e) { openDetail((int)(intptr_t)lv_event_get_user_data(e)); }

static void addTile(int i) {
    const md::Entry& e = gCat.entries[i];
    lv_obj_t* t = lv_obj_create(gGrid);
    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, TILE_W, TILE_H);
    lv_obj_set_style_bg_color(t, C(PAN), 0);
    lv_obj_set_style_bg_color(t, C(PAN2), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, 14, 0);
    lv_obj_set_style_border_width(t, e.last ? 2 : 1, 0);
    lv_obj_set_style_border_color(t, C(e.last ? GOLD : LINE), 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t, onTile, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    if (lv_obj_t* im = goldImage(t, monogram(e.title).c_str(), 170, 96, PAN, 0.5f)) lv_obj_align(im, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t* n = label(t, 0, 0, f20, TXT, e.title.c_str());
    lv_obj_set_width(n, TILE_W - 24);
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 116);
    lv_obj_t* v = label(t, 0, 0, f16, DIM, e.version.empty() ? e.file.c_str() : e.version.c_str());
    lv_obj_set_width(v, TILE_W - 24);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(v, LV_ALIGN_TOP_MID, 0, 146);
    const char* badge = nullptr;
    uint32_t bc = DIM;
    if (!e.problem.empty()) { badge = LV_SYMBOL_WARNING " Can't start here"; bc = RED; }
    else if (e.last) { badge = LV_SYMBOL_LOOP " Started last"; bc = GOLD; }
    else if (e.slot >= 0) { badge = LV_SYMBOL_OK " On the board"; bc = GREEN; }
    else badge = LV_SYMBOL_SD_CARD " On the card";
    lv_obj_t* b = label(t, 0, 0, f16, bc, badge);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 176);
}

static void freeGridBufs() {
    for (auto* b : gBufs) lv_draw_buf_destroy(b);
    gBufs.clear();
}

static void rebuild() {
    lv_obj_clean(gGrid);
    freeGridBufs();
    std::string ci = gCat.sdInfo;
    if (gCat.sdOk) ci += "   " + std::to_string(gCat.entries.size()) + (gCat.entries.size() == 1 ? " firmware" : " firmware");
    lv_label_set_text(gCardInfo, ci.c_str());
    for (int i = 0; i < (int)gCat.entries.size(); i++) addTile(i);
    if (gCat.entries.empty()) {
        lv_obj_remove_flag(gEmpty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t* h = lv_obj_get_child(gEmpty, 0);
        lv_obj_t* b = lv_obj_get_child(gEmpty, 1);
        if (!gCat.sdOk) {
            lv_label_set_text(h, gCat.sdInfo.c_str());
            lv_label_set_text(b, "Put a FAT32 microSD card in with a folder named \"firmware\" on it,\n"
                                 "copy firmware .bin files into that folder, then tap Rescan.");
        } else {
            lv_label_set_text(h, "No firmware on the card yet");
            lv_label_set_text_fmt(b, "Copy firmware .bin files into the folder \"%s\" on the card, then tap Rescan.\n"
                                     "Both kinds work: build/<project>.bin from idf.py, or a full-flash release image.",
                                  gCat.dir.size() > 8 ? gCat.dir.c_str() + 7 : gCat.dir.c_str());
        }
    } else {
        lv_obj_add_flag(gEmpty, LV_OBJ_FLAG_HIDDEN);
    }
}

static void doScan(lv_timer_t* t) {
    if (t) lv_timer_delete(t);
    md::scan(gCat);
    rebuild();
    lv_obj_remove_state(gRescanBtn, LV_STATE_DISABLED);
}
static void onRescan(lv_event_t*) {
    if (lv_obj_has_state(gRescanBtn, LV_STATE_DISABLED)) return;
    lv_obj_add_state(gRescanBtn, LV_STATE_DISABLED);
    lv_label_set_text(gCardInfo, "Looking at the card ...");
    lv_timer_create(doScan, 60, nullptr);                // let the words show first
}

// ---- start-up screen (as RigDeck's) -------------------------------------------------------------
static void splashGone(lv_anim_t*) {
    if (gSplash) { lv_obj_delete(gSplash); gSplash = nullptr; }
    if (gSplashBuf) { lv_draw_buf_destroy(gSplashBuf); gSplashBuf = nullptr; }
}
static void splashFade(lv_obj_t* o) {
    if (!o || o != gSplash || gSplashDone) return;
    gSplashDone = true;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 600);
    lv_anim_set_exec_cb(&a, [](void* v, int32_t x) { lv_obj_set_style_opa((lv_obj_t*)v, (lv_opa_t)x, 0); });
    lv_anim_set_completed_cb(&a, splashGone);
    lv_anim_start(&a);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
}
static void showSplash() {
    gSplash = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(gSplash);
    lv_obj_set_size(gSplash, W, H);
    lv_obj_set_style_bg_color(gSplash, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(gSplash, LV_OPA_COVER, 0);
    lv_obj_add_flag(gSplash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gSplash, [](lv_event_t* e) { splashFade((lv_obj_t*)lv_event_get_target(e)); }, LV_EVENT_CLICKED, nullptr);
    gSplashBuf = lv_draw_buf_create(900, 200, LV_COLOR_FORMAT_ARGB8888, 0);
    if (gSplashBuf && goldText(gSplashBuf, "MultiDeck")) {
        lv_obj_t* im = lv_image_create(gSplash);
        lv_image_set_src(im, gSplashBuf);
        lv_obj_align(im, LV_ALIGN_CENTER, 0, -50);
    }
    lv_obj_t* t = lv_label_create(gSplash);
    lv_obj_set_style_text_font(t, f28, 0);
    lv_obj_set_style_text_color(t, C(GOLD), 0);
    lv_label_set_text(t, "Firmware launcher by AC9HP");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 85);
    lv_obj_t* v = lv_label_create(gSplash);
    lv_obj_set_style_text_font(v, f16, 0);
    lv_obj_set_style_text_color(v, C(GOLD_DIM), 0);
    lv_label_set_text(v, md::fwVersion().c_str());
    lv_obj_align(v, LV_ALIGN_CENTER, 0, 125);
    lv_timer_create([](lv_timer_t* tt) { splashFade(gSplash); lv_timer_delete(tt); }, CONFIG_MULTIDECK_SPLASH_MS, nullptr);
}

extern "C" void ui_create(void) {
    if (gProgTimer) { lv_timer_delete(gProgTimer); gProgTimer = nullptr; }   // (the PC preview builds it more than once)
    gProg = gModal = gSplash = nullptr;
    gSplashDone = false;
    gScr = lv_screen_active();
    lv_obj_clean(gScr);
    lv_obj_set_style_bg_color(gScr, C(BG), 0);
    lv_obj_set_style_bg_opa(gScr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(gScr, LV_OBJ_FLAG_SCROLLABLE);

    // header: gold name, card, Rescan
    lv_obj_t* head = box(gScr, 0, 0, W, HEAD_H, PAN);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_set_style_border_color(head, C(LINE), 0);
    if (lv_obj_t* im = goldImage(head, "MultiDeck", 300, 64, PAN, 0.45f)) lv_obj_set_pos(im, 14, 6);
    gBufs.clear();                                       // the header logo stays for good
    gCardInfo = label(head, 0, 0, f16, DIM, "");
    lv_obj_align(gCardInfo, LV_ALIGN_RIGHT_MID, -190, 0);
    gRescanBtn = button(head, W - 24 - 150, (HEAD_H - 50) / 2, 150, 50, PAN2, TXT, LV_SYMBOL_REFRESH "  Rescan", onRescan);
    lv_obj_set_style_text_color(lv_obj_get_child(gRescanBtn, 0), C(DIM), LV_STATE_DISABLED);

    // the tiles
    gGrid = lv_obj_create(gScr);
    lv_obj_remove_style_all(gGrid);
    lv_obj_set_pos(gGrid, 0, HEAD_H);
    lv_obj_set_size(gGrid, W, H - HEAD_H - FOOT_H);
    lv_obj_set_style_pad_hor(gGrid, PAD_X, 0);
    lv_obj_set_style_pad_top(gGrid, 18, 0);
    lv_obj_set_style_pad_bottom(gGrid, 18, 0);
    lv_obj_set_style_pad_row(gGrid, GAP, 0);
    lv_obj_set_style_pad_column(gGrid, GAP, 0);
    lv_obj_set_flex_flow(gGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(gGrid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(gGrid, LV_SCROLLBAR_MODE_AUTO);

    // nothing to show
    gEmpty = lv_obj_create(gScr);
    lv_obj_remove_style_all(gEmpty);
    lv_obj_set_size(gEmpty, W - 80, 220);
    lv_obj_align(gEmpty, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(gEmpty, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* eh = label(gEmpty, 0, 0, f28, TXT, "");
    lv_obj_align(eh, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_t* eb = label(gEmpty, 0, 0, f16, DIM, "");
    lv_obj_set_style_text_align(eb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(eb, 6, 0);
    lv_obj_align(eb, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_add_flag(gEmpty, LV_OBJ_FLAG_HIDDEN);

    // how to come back
    lv_obj_t* foot = box(gScr, 0, H - FOOT_H, W, FOOT_H, PAN);
    lv_obj_t* fh = label(foot, 0, 0, f16, GOLD_DIM, md::returnHint());
    lv_obj_set_width(fh, W - 40);
    lv_obj_set_style_text_align(fh, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(fh, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_center(fh);

    lv_label_set_text(gCardInfo, "Looking at the card ...");
    lv_obj_add_state(gRescanBtn, LV_STATE_DISABLED);
    if (CONFIG_MULTIDECK_SPLASH_MS > 0) showSplash();
    lv_timer_create(doScan, 80, nullptr);                // behind the start-up screen
}
