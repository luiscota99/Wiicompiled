// Replay a recorded GameCube<->client link trace (WIICOMPILED_GBA_TRACE) into a fresh mGBA client and
// compare the client's answers with the recording. Usage:
//   mgba_test3 <trace.bin> <port> [stopIndex] [experiment]
// Replays every 'W' record of that port to the client and, at every 'R' record, reads the client's word
// and compares (pad words [04 ..] are matched loosely). After `stopIndex` records (default: all) the
// replay stops, a frame is saved, and the optional experiment runs: "ctrl1" sends [09 01], "statereq"
// sends [0C 0E 00 00], "mtype0" sends [10][1B 00][0D 00 7D ED].
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cstdarg>
extern "C" {
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/timing.h>
#include <mgba/gba/core.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/sio.h>
#include <mgba-util/vfs.h>
}
static void SilentLog(struct mLogger*, int, enum mLogLevel, const char*, va_list) {}
static mLogger g_logger{SilentLog, nullptr};
static bool DrvInit(GBASIODriver*) { return true; }
static void DrvDeinit(GBASIODriver*) {}
static void DrvReset(GBASIODriver*) {}
static uint32_t DrvId(const GBASIODriver*) { return 1; }
static bool DrvLoadState(GBASIODriver*, const void*, size_t) { return true; }
static void DrvSaveState(GBASIODriver*, void** s, size_t* n) { *s = nullptr; *n = 0; }
static void DrvSetMode(GBASIODriver*, enum GBASIOMode) {}
static bool DrvHandlesMode(GBASIODriver*, enum GBASIOMode m) { return m == GBA_SIO_JOYBUS; }
static int DrvConnected(GBASIODriver*) { return 1; }
static int DrvDeviceId(GBASIODriver*) { return 0; }
static uint16_t DrvW16(GBASIODriver*, uint16_t v) { return v; }
static bool DrvStart(GBASIODriver*) { return false; }
static void DrvFinishMulti(GBASIODriver*, uint16_t d[4]) { d[0] = d[1] = d[2] = d[3] = 0xFFFF; }
static uint8_t DrvF8(GBASIODriver*) { return 0xFF; }
static uint32_t DrvF32(GBASIODriver*) { return 0xFFFFFFFFu; }

static mCore* core; static GBA* gba; static GBASIODriver drv{}; static std::vector<uint32_t> frame(240 * 160, 0);
static constexpr int32_t kFrame = 280896, kSlice = kFrame / 16;
struct Rec { char tag; uint8_t port, state; uint32_t word; };

static void runCycles(int32_t cycles) { const int32_t start = mTimingCurrentTime(&gba->timing); int g = 0; while (int32_t(mTimingCurrentTime(&gba->timing) - start) < cycles && ++g < 100000) core->runLoop(core); }
static uint8_t poll() { uint8_t d[8] = {0}; GBASIOJOYSendCommand(&drv, JOY_POLL, d); return d[2]; }
static uint32_t readWord(uint8_t* st = nullptr) { uint8_t t[8] = {0}; GBASIOJOYSendCommand(&drv, JOY_TRANS, t); if (st) *st = t[4]; return (uint32_t(t[0]) << 24) | (uint32_t(t[1]) << 16) | (uint32_t(t[2]) << 8) | t[3]; }
static uint8_t writeWord(uint32_t w) { uint8_t t[8] = {uint8_t(w >> 24), uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w)}; GBASIOJOYSendCommand(&drv, JOY_RECV, t); return t[0]; }
static void savePpm(const char* name) {
    FILE* f = std::fopen(name, "wb"); if (!f) return;
    std::fprintf(f, "P6\n240 160\n255\n");
    for (uint32_t c : frame) { unsigned char px[3] = {uint8_t(c & 0xFF), uint8_t((c >> 8) & 0xFF), uint8_t((c >> 16) & 0xFF)}; std::fwrite(px, 1, 3, f); }
    std::fclose(f); std::printf("saved %s\n", name);
}
static bool isPad(uint32_t w) { return ((w >> 24) & 0x3Fu) == 0x04u; }
// Read the client's words until a non-pad word (or `maxFrames` of emulated time); pad words are dropped.
static bool readCommand(uint32_t& out, int maxFrames) {
    for (int i = 0; i < maxFrames * 16; ++i) {
        while (poll() & 0x08) { const uint32_t w = readWord(); if (!isPad(w)) { out = w; return true; } }
        runCycles(kSlice);
    }
    return false;
}
static void drainPrint(const char* tag, int frames) {
    int pads = 0;
    for (int f = 0; f < frames; ++f) for (int s = 0; s < 16; ++s) { runCycles(kSlice); while (poll() & 0x08) { const uint32_t w = readWord(); if (isPad(w)) ++pads; else std::printf("  %-12s <- %08X\n", tag, w); } }
    std::printf("  %-12s (%d frames, %d pad words)\n", tag, frames, pads);
}
static void xfer(const char* tag, uint32_t w) {
    std::printf("  %-12s -> %08X (joystat %02X)\n", tag, w, writeWord(w));
    for (int i = 0; i < 32; ++i) { runCycles(kSlice); if ((gba->memory.io[GBA_REG(JOYSTAT)] & 0x02u) == 0) break; }
    drainPrint(tag, 2);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: mgba_test3 trace.bin port [stopIndex] [experiment]\n"); return 1; }
    const int port = std::atoi(argv[2]);
    const long stopIndex = argc > 3 ? std::atol(argv[3]) : -1;
    const std::string experiment = argc > 4 ? argv[4] : "";
    std::vector<Rec> recs;
    { FILE* f = std::fopen(argv[1], "rb"); if (!f) { std::printf("no trace\n"); return 1; }
      uint8_t h[3]; uint32_t w; while (std::fread(h, 1, 3, f) == 3 && std::fread(&w, 4, 1, f) == 1) if (h[1] == port) recs.push_back({char(h[0]), h[1], h[2], w}); std::fclose(f); }
    std::printf("trace: %zu records for port %d\n", recs.size(), port);
    const char* path = std::getenv("FFCC_CLIENT_BIN");   // dvd/gba/ffcc_cli.bin from your own extracted disc
    if (!path) { std::printf("set FFCC_CLIENT_BIN to dvd/gba/ffcc_cli.bin of your extracted disc
"); return 1; }
    FILE* f = std::fopen(path, "rb"); if (!f) { std::printf("no image\n"); return 1; }
    std::vector<uint8_t> img; uint8_t buf[4096]; size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) img.insert(img.end(), buf, buf + n);
    std::fclose(f);
    mLogSetDefaultLogger(&g_logger);
    core = GBACoreCreate(); if (!core || !core->init(core)) { std::printf("core failed\n"); return 1; }
    mCoreInitConfig(core, "ffcc-gba");
    mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "remove");
    core->setVideoBuffer(core, reinterpret_cast<mColor*>(frame.data()), 240);
    VFile* vf = VFileMemChunk(img.data(), img.size());
    if (!core->loadROM(core, vf)) { std::printf("loadROM failed\n"); return 1; }
    core->reset(core);
    drv.init = DrvInit; drv.deinit = DrvDeinit; drv.reset = DrvReset; drv.driverId = DrvId;
    drv.loadState = DrvLoadState; drv.saveState = DrvSaveState; drv.setMode = DrvSetMode; drv.handlesMode = DrvHandlesMode;
    drv.connectedDevices = DrvConnected; drv.deviceId = DrvDeviceId; drv.writeSIOCNT = DrvW16; drv.writeRCNT = DrvW16;
    drv.start = DrvStart; drv.finishMultiplayer = DrvFinishMulti; drv.finishNormal8 = DrvF8; drv.finishNormal32 = DrvF32;
    gba = static_cast<GBA*>(core->board);
    GBASIOSetDriver(&gba->sio, &drv);
    core->setKeys(core, 0);
    // boot like the runtime: drain the zero word until the client raises its idle flag (0x20)
    for (int i = 0; i < 60; ++i) { core->runFrame(core); const uint8_t st = poll(); if (st & 0x20) break; if (st & 0x08) readWord(); }
    // INJECT=<index>:<hex words comma-separated>: words written just before that record (lost words test)
    long injectAt = -1; std::vector<uint32_t> injectWords;
    if (const char* inj = std::getenv("INJECT")) {
        injectAt = std::atol(inj); const char* c = std::strchr(inj, ':');
        std::string list = c ? c + 1 : ""; size_t pos = 0;
        while (pos <= list.size()) { size_t e = list.find(',', pos); if (e == std::string::npos) e = list.size(); const std::string h = list.substr(pos, e - pos); pos = e + 1; if (!h.empty()) injectWords.push_back(uint32_t(std::strtoul(h.c_str(), nullptr, 16))); }
    }
    long idx = 0, mismatches = 0, missing = 0, written = 0, matched = 0;
    for (const Rec& r : recs) {
        if (stopIndex >= 0 && idx >= stopIndex) break;
        ++idx;
        if (idx == injectAt) for (uint32_t w : injectWords) { char tag[24]; std::snprintf(tag, sizeof tag, "inject@%ld", idx); xfer(tag, w); }
        if (r.tag == 'W') {
            // wait until the client accepted the previous word
            for (int i = 0; i < 32 && (gba->memory.io[GBA_REG(JOYSTAT)] & 0x02u); ++i) runCycles(kSlice);
            writeWord(r.word); ++written;
            for (int i = 0; i < 32; ++i) { runCycles(kSlice); if ((gba->memory.io[GBA_REG(JOYSTAT)] & 0x02u) == 0) break; }
            const uint32_t op = (r.word >> 24) & 0x3Fu;
            if (op != 0x05u && op != 0x0Bu) std::printf("%6ld W st %02X -> %08X\n", idx, r.state, r.word);
        } else if (r.tag == 'R') {
            if (isPad(r.word)) { while (poll() & 0x08) { const uint32_t w = readWord(); if (!isPad(w)) std::printf("%6ld R st %02X expected pad, client %08X\n", idx, r.state, w); } continue; }
            uint32_t w = 0;
            if (!readCommand(w, 5)) { ++missing; std::printf("%6ld R st %02X expected %08X, client: nothing within 5 frames\n", idx, r.state, r.word); continue; }
            if (w == r.word) { ++matched; if (((r.word >> 24) & 0x3Fu) != 0x06u) std::printf("%6ld R st %02X <- %08X ok\n", idx, r.state, r.word); }
            else { ++mismatches; std::printf("%6ld R st %02X expected %08X, client %08X\n", idx, r.state, r.word, w); }
        }
    }
    std::printf("replayed %ld records: %ld written, %ld replies matched, %ld mismatched, %ld missing\n", idx, written, matched, mismatches, missing);
    drainPrint("settle", 10);
    char name[64]; std::snprintf(name, sizeof name, "replay_p%d_%ld.ppm", port, idx); savePpm(name);
    if (experiment == "ctrl1") { xfer("ctrl 1", 0x09010000u); drainPrint("after", 40); std::snprintf(name, sizeof name, "replay_p%d_%ld_ctrl1.ppm", port, idx); savePpm(name); }
    else if (experiment.rfind("key:", 0) == 0) {
        const unsigned mask = std::strtoul(experiment.c_str() + 4, nullptr, 16);
        std::printf("  press keys 0x%X for 8 frames\n", mask);
        core->setKeys(core, mask); drainPrint("held", 8); core->setKeys(core, 0); drainPrint("released", 40);
        std::snprintf(name, sizeof name, "replay_p%d_%ld_key%X.ppm", port, idx, mask); savePpm(name);
    }
    else if (experiment.rfind("ctrl1key:", 0) == 0) {
        const unsigned mask = std::strtoul(experiment.c_str() + 9, nullptr, 16);
        xfer("ctrl 1", 0x09010000u); drainPrint("after ctrl1", 10);
        std::printf("  press keys 0x%X for 8 frames\n", mask);
        core->setKeys(core, mask); drainPrint("held", 8); core->setKeys(core, 0); drainPrint("released", 40);
        std::snprintf(name, sizeof name, "replay_p%d_%ld_ctrl1key%X.ppm", port, idx, mask); savePpm(name);
    }
    else if (experiment.rfind("words:", 0) == 0) {
        // comma-separated hex words, e.g. words:140F0B00,09010000 ; each is sent, then the client drained
        std::string list = experiment.substr(6); size_t pos = 0; int k = 0;
        while (pos <= list.size()) { size_t c = list.find(',', pos); if (c == std::string::npos) c = list.size(); const std::string h = list.substr(pos, c - pos); pos = c + 1; if (h.empty()) continue;
            char tag[16]; std::snprintf(tag, sizeof tag, "w%d", ++k); xfer(tag, uint32_t(std::strtoul(h.c_str(), nullptr, 16))); drainPrint(tag, 30); }
        std::snprintf(name, sizeof name, "replay_p%d_%ld_words.ppm", port, idx); savePpm(name);
    }
    else if (experiment == "statereq") { xfer("state req", 0x0C0E0000u); drainPrint("after", 20); }
    else if (experiment == "mtype0") { xfer("reset", 0x10000000u); xfer("mtype 0", 0x1B000000u); xfer("chkcrc", 0x0D007DEDu); drainPrint("after", 20); xfer("ctrl 1", 0x09010000u); drainPrint("after", 40); std::snprintf(name, sizeof name, "replay_p%d_%ld_mtype0.ppm", port, idx); savePpm(name); }
    return 0;
}
