// What the screen shows and what it can ask for. launcher.cpp is the board side; the PC preview
// (test/ui_sim.cpp) has its own versions of these functions.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace md {

struct Entry {
    std::string path;          // file on the card ("" = only on the board)
    std::string file;          // its name, for the screen
    std::string title;         // from <name>.txt, else the project name, else the file name
    std::string desc;          // from <name>.txt (lines after the first)
    std::string project, version, idf, built;
    std::string sha;           // the image's SHA-256, hex ("" if the image carries none)
    uint32_t fileSize = 0, appLen = 0;
    uint32_t appOffset = 0;
    bool merged = false;
    int slot = -1;             // 0 / 1: this exact image is already in app slot ota_0 / ota_1
    bool last = false;         // the one started last time
    std::string problem;       // not empty: it cannot be started here, and why
    std::string warning;       // can be started, but note this
};

struct Catalog {
    bool sdOk = false;
    std::string sdInfo;        // "SD32G, 31.9 GB" or why there is no card
    std::string dir;           // the folder that was searched
    std::vector<Entry> entries;
};

// Reads the card and the two app slots. Takes a moment (headers of every file, a hash of each slot).
void scan(Catalog& out);

// Starts the chosen firmware in the background: copies it into a slot unless it is already there,
// selects it and restarts. Watch progress().
bool launch(const Entry& e);

struct Progress {
    enum Phase { Idle, Erasing, Copying, Checking, Starting, Failed } phase = Idle;
    int percent = 0;
    std::string message;
};
Progress progress();

// The words on the screen for "how to get back here".
const char* returnHint();
std::string fwVersion();

}  // namespace md
