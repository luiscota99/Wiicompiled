// Standalone client-behaviour probe: boot ffcc_cli.bin in mGBA, run the link handshake, then replay
// the GameCube's command words of the roster -> field transition and watch what the client answers
// and draws. Usage: mgba_test2 game|roster  (frames saved as PPM next to the exe).
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

static void runCycles(int32_t cycles) { const int32_t start = mTimingCurrentTime(&gba->timing); int g = 0; while (int32_t(mTimingCurrentTime(&gba->timing) - start) < cycles && ++g < 100000) core->runLoop(core); }
static uint8_t poll() { uint8_t d[8] = {0}; GBASIOJOYSendCommand(&drv, JOY_POLL, d); return d[2]; }
static void readw(const char* tag) { uint8_t t[8] = {0}; GBASIOJOYSendCommand(&drv, JOY_TRANS, t); std::printf("  %-14s <- client %02X %02X %02X %02X (joystat %02X)\n", tag, t[0], t[1], t[2], t[3], t[4]); }
static uint8_t writew(const char* tag, uint8_t a, uint8_t b, uint8_t c, uint8_t d4) { uint8_t t[8] = {a, b, c, d4}; GBASIOJOYSendCommand(&drv, JOY_RECV, t); std::printf("  %-14s -> client %02X %02X %02X %02X (joystat %02X)\n", tag, a, b, c, d4, t[0]); return t[0]; }
// Drain: run up to `frames` frames, reading every word the client queues (pad words [04 ..] are counted, not printed).
static void drain(const char* tag, int frames) {
    int pads = 0;
    for (int f = 0; f < frames; ++f) {
        for (int s = 0; s < 16; ++s) {
            runCycles(kSlice);
            while (poll() & 0x08) { uint8_t t[8] = {0}; GBASIOJOYSendCommand(&drv, JOY_TRANS, t); if ((t[0] & 0x3F) == 0x04) { ++pads; } else std::printf("  %-14s <- client %02X %02X %02X %02X\n", tag, t[0], t[1], t[2], t[3]); }
        }
    }
    std::printf("  %-14s (%d frames, %d pad words)\n", tag, frames, pads);
}
// Write a word like the runtime does: wait until consumed (RECV clear), then let the client answer.
static void xfer(const char* tag, uint8_t a, uint8_t b, uint8_t c, uint8_t d4) {
    writew(tag, a, b, c, d4);
    for (int i = 0; i < 32; ++i) { runCycles(kSlice); if ((gba->memory.io[GBA_REG(JOYSTAT)] & 0x02u) == 0) break; }
    drain(tag, 2);
}
static void savePpm(const char* name) {
    FILE* f = std::fopen(name, "wb"); if (!f) return;
    std::fprintf(f, "P6\n240 160\n255\n");
    for (uint32_t c : frame) { unsigned char px[3] = {uint8_t(c & 0xFF), uint8_t((c >> 8) & 0xFF), uint8_t((c >> 16) & 0xFF)}; std::fwrite(px, 1, 3, f); }
    std::fclose(f); std::printf("  saved %s\n", name);
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "game";
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
    // boot: drain the zero word, wait for the initial code
    for (int i = 0; i < 60; ++i) { core->runFrame(core); const uint8_t st = poll(); if (st & 0x20) break; if (st & 0x08) readw("boot"); }
    std::printf("handshake\n");
    readw("initial code");
    drain("", 1);
    writew("disk id", 0x22, 0, 0, 0);
    for (int i = 0; i < 4; ++i) { drain("", 1); if (poll() & 0x08) { readw("context"); break; } }
    for (int i = 0; i < 6; ++i) { drain("", 1); if (poll() & 0x08) { readw("host id"); break; } }
    writew("host id", 0x22, 0x69, 0x47, 0x16);
    for (int i = 0; i < 6; ++i) { drain("", 1); if (poll() & 0x08) readw("reply"); }
    writew("context", 0x01, 0x00, 0, 0);
    drain("after ctx", 6);
    std::printf("phase: %s\n", mode.c_str());
    if (mode == "game") {
        xfer("reset [10]", 0x10, 0, 0, 0);
        xfer("mtype 0", 0x1B, 0, 0, 0);
        xfer("mapno?", 0x0C, 0x00, 0, 0);
        xfer("chkcrc", 0x0D, 0x00, 0x7D, 0xED);
        drain("after crc", 5);
        xfer("gbastart", 0x0A, 0x01, 0, 0);
        xfer("spmode", 0x14, 0x11, 0, 0);
        drain("settle", 20); savePpm("frame_game_field.ppm");
        xfer("ctrl 1", 0x09, 0x01, 0, 0);
        drain("after ctrl1", 40); savePpm("frame_game_ctrl1.ppm");
        xfer("state req", 0x0C, 0x0E, 0, 0);
        drain("after statereq", 20);
        xfer("ctrl 0", 0x09, 0x00, 0, 0);
        drain("after ctrl0", 20); savePpm("frame_game_ctrl0.ppm");
    } else {
        xfer("reset [10]", 0x10, 0, 0, 0);
        xfer("mtype 4", 0x1B, 0x04, 0, 0);
        xfer("chkcrc", 0x0D, 0x00, 0x7D, 0xED);
        drain("after crc", 5);
        xfer("gbastart", 0x0A, 0x01, 0, 0);
        drain("roster settle", 20); savePpm("frame_roster.ppm");
        xfer("ctrl 1 (open)", 0x09, 0x01, 0, 0);
        drain("after ctrl1", 5);
        xfer("gbastop", 0x0A, 0x00, 0, 0);
        xfer("chkcrc", 0x0D, 0x00, 0x7D, 0xED);
        drain("after stop", 20); savePpm("frame_after_stop.ppm");
        xfer("gbastart", 0x0A, 0x01, 0, 0);
        xfer("spmode", 0x14, 0x11, 0, 0);
        drain("field settle", 20); savePpm("frame_roster_field.ppm");
        xfer("ctrl 1", 0x09, 0x01, 0, 0);
        drain("after ctrl1 b", 40); savePpm("frame_roster_ctrl1.ppm");
    }
    unsigned nonblack = 0; for (uint32_t c : frame) if ((c & 0xFFFFFF) != 0) ++nonblack;
    std::printf("frame non-black pixels: %u / %zu\n", nonblack, frame.size());
    return 0;
}
