// Embedded mGBA cores running FFCC's own GBA client program, one per port. See ffcc_gba_mgba.h.
//
// Link: the GameCube's GBA SDK functions (GBAGetStatus/GBARead/GBAWrite/GBAReset, HLE'd in
// ffcc_gba.cpp) map 1:1 onto the four JoyBus commands mGBA implements in GBASIOJOYSendCommand
// (src/gba/sio.c): POLL/RESET return the device id and JOYSTAT, TRANS returns the 4 bytes the
// client put in JOY_TRANS plus JOYSTAT, RECV stores 4 bytes into JOY_RECV. The client program
// handles JOYCNT/JOYSTAT itself, so no GBA BIOS is needed: the multiboot image is loaded straight
// into work RAM (GBALoadMB, as mGBA does for .mb files) instead of being uploaded through the
// BIOS's JoyBus boot handshake, and GBAJoyBoot on the GameCube side just reports success.
//
// Timing: the cores run on the guest thread from the draw hook (Tick), never concurrently with
// the SDK calls (cooperative fibers on one OS thread), so no locking is needed.
#include "ffcc_gba_mgba.h"
#include "runtime_log.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(RECOMP_PROJECT_FFCC) && defined(MKW_HAVE_MGBA)

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

extern "C" uint16_t FfccGbaReadKeys(uint32_t chan);   // aurora pad.cpp
extern "C" uint16_t FfccGbaClientKeys(uint32_t chan);
   // ffcc_gba.cpp: keys minus Select (turned into ChgCtrlMode)

namespace {

constexpr uint32_t kPorts = 4;

void SilentLog(struct mLogger*, int, enum mLogLevel, const char*, va_list) {}
mLogger g_logger{SilentLog, nullptr};
bool g_loggerSet = false;

// Minimal SIO driver: mGBA only needs `p` (set by GBASIOSetDriver) for the JoyBus commands; the
// callbacks exist so the core never dereferences a null one when the client changes SIO mode.
bool DrvInit(GBASIODriver*) { return true; }
void DrvDeinit(GBASIODriver*) {}
void DrvReset(GBASIODriver*) {}
uint32_t DrvId(const GBASIODriver*) { return 0x4A4F5942; }   // 'JOYB'
bool DrvLoadState(GBASIODriver*, const void*, size_t) { return true; }
void DrvSaveState(GBASIODriver*, void** state, size_t* size) { *state = nullptr; *size = 0; }
void DrvSetMode(GBASIODriver*, enum GBASIOMode) {}
bool DrvHandlesMode(GBASIODriver*, enum GBASIOMode mode) { return mode == GBA_SIO_JOYBUS; }
int DrvConnected(GBASIODriver*) { return 1; }
int DrvDeviceId(GBASIODriver*) { return 0; }
uint16_t DrvWriteSIOCNT(GBASIODriver*, uint16_t v) { return v; }
uint16_t DrvWriteRCNT(GBASIODriver*, uint16_t v) { return v; }
bool DrvStart(GBASIODriver*) { return false; }
void DrvFinishMulti(GBASIODriver*, uint16_t data[4]) { data[0] = data[1] = data[2] = data[3] = 0xFFFF; }
uint8_t DrvFinish8(GBASIODriver*) { return 0xFF; }
uint32_t DrvFinish32(GBASIODriver*) { return 0xFFFFFFFFu; }

struct Port {
    mCore* core = nullptr;
    GBASIODriver driver{};
    std::vector<uint32_t> frame;       // 240 x 160 XRGB8
    std::vector<uint8_t> image;        // the multiboot program (kept alive for the VFile)
    std::chrono::steady_clock::time_point lastTick{};
    double pendingFrames = 0.0;
    uint32_t framesRun = 0;
    bool active = false;
};
Port g_ports[kPorts];

bool Disabled() {
    static int s = -1;
    if (s < 0) {
        const char* env = std::getenv("WIICOMPILED_GBA");
        s = (env && (std::strcmp(env, "fake") == 0 || std::strcmp(env, "0") == 0)) ? 1 : 0;
    }
    return s == 1;
}

constexpr int32_t kCyclesPerFrame = 280896;          // VIDEO_TOTAL_LENGTH
constexpr int32_t kSliceCycles = kCyclesPerFrame / 16;   // about 1 ms of GBA time

// Run the core for `cycles` of emulated time. core->runLoop only executes until the NEXT scheduled
// event (the next h-blank, ~1232 cycles) and returns, so loop on the timing clock (measured: a
// single runLoop per "slice" gave the client ~70 us instead of 1 ms and the handshake stalled).
void RunCycles(Port& p, int32_t cycles) {
    GBA* gba = static_cast<GBA*>(p.core->board);
    const int32_t start = mTimingCurrentTime(&gba->timing);
    int guard = 0;
    while (int32_t(mTimingCurrentTime(&gba->timing) - start) < cycles && ++guard < 20000) p.core->runLoop(p.core);
    // Charge the time that actually elapsed: when the client idles until its next interrupt a single
    // runLoop covers most of a frame, and charging the requested slice let the link calls run the
    // client at 191 frames/s (measured) - the 'fast inputs' and multiple moves per press.
    p.pendingFrames -= double(int32_t(mTimingCurrentTime(&gba->timing) - start)) / double(kCyclesPerFrame);
}

void Destroy(Port& p) {
    if (p.core) {
        GBASIOSetDriver(&static_cast<GBA*>(p.core->board)->sio, nullptr);
        p.core->deinit(p.core);
        p.core = nullptr;
    }
    p.active = false;
}

} // namespace

namespace FfccMgba {

bool Available() { return !Disabled(); }
bool Active(uint32_t chan) { return chan < kPorts && g_ports[chan].active; }

bool Boot(uint32_t chan, const uint8_t* image, size_t len) {
    if (chan >= kPorts || Disabled() || image == nullptr || len < 0xC0) return false;
    Port& p = g_ports[chan];
    Destroy(p);
    if (!g_loggerSet) { mLogSetDefaultLogger(&g_logger); g_loggerSet = true; }

    p.image.assign(image, image + len);
    p.core = GBACoreCreate();
    if (!p.core) return false;
    if (!p.core->init(p.core)) { p.core = nullptr; return false; }
    mCoreInitConfig(p.core, "ffcc-gba");
    mCoreConfigSetDefaultValue(&p.core->config, "idleOptimization", "remove");
    p.frame.assign(size_t(kWidth) * kHeight, 0);
    p.core->setVideoBuffer(p.core, reinterpret_cast<mColor*>(p.frame.data()), kWidth);

    VFile* vf = VFileMemChunk(p.image.data(), p.image.size());
    if (!vf || !GBAIsMB(vf)) {
        RT_LOG(RT_TAG_OS) << "mgba: port " << chan << ": image is not a multiboot program (" << len << " bytes)" << std::endl;
        if (vf) vf->close(vf);
        Destroy(p);
        return false;
    }
    if (!p.core->loadROM(p.core, vf)) {   // GBAIsMB -> GBALoadMB: straight into work RAM
        RT_LOG(RT_TAG_OS) << "mgba: port " << chan << ": loadROM failed" << std::endl;
        Destroy(p);
        return false;
    }
    p.core->reset(p.core);

    p.driver = GBASIODriver{};
    p.driver.init = DrvInit; p.driver.deinit = DrvDeinit; p.driver.reset = DrvReset; p.driver.driverId = DrvId;
    p.driver.loadState = DrvLoadState; p.driver.saveState = DrvSaveState; p.driver.setMode = DrvSetMode;
    p.driver.handlesMode = DrvHandlesMode; p.driver.connectedDevices = DrvConnected; p.driver.deviceId = DrvDeviceId;
    p.driver.writeSIOCNT = DrvWriteSIOCNT; p.driver.writeRCNT = DrvWriteRCNT; p.driver.start = DrvStart;
    p.driver.finishMultiplayer = DrvFinishMulti; p.driver.finishNormal8 = DrvFinish8; p.driver.finishNormal32 = DrvFinish32;
    GBASIOSetDriver(&static_cast<GBA*>(p.core->board)->sio, &p.driver);

    // Boot handshake. Measured with the standalone harness: the client puts the link in JoyBus mode on
    // its first frame and sends one zero word (the acknowledgment the SDK's JoyBoot consumes on
    // hardware), then its initial code word (56 00 57 00) with the idle flag: JOYSTAT 0x28, which is
    // exactly what JoyBus::InitialCode case 0 waits for. Drain the zero word here, stop at 0x28/0x20.
    for (int i = 0; i < 240; ++i) {
        p.core->runFrame(p.core);
        uint8_t d[8] = {0};
        GBASIOJOYSendCommand(&p.driver, JOY_POLL, d);
        if (d[2] & 0x20u) break;                       // idle flag up: the initial code is the GameCube's to read
        if (d[2] & 0x08u) {
            uint8_t t[8] = {0};
            GBASIOJOYSendCommand(&p.driver, JOY_TRANS, t);   // the boot acknowledgment
            RT_LOG(RT_TAG_OS) << "mgba: port " << chan << " drained boot word " << std::hex << int(t[0]) << ' ' << int(t[1]) << ' ' << int(t[2]) << ' ' << int(t[3]) << std::dec << " at frame " << i << std::endl;
        }
    }
    p.lastTick = std::chrono::steady_clock::now();
    p.pendingFrames = 0.0;
    p.active = true;
    RT_LOG(RT_TAG_OS) << "mgba: port " << chan << " booted the client program (" << len << " bytes)" << std::endl;
    return true;
}

int Command(uint32_t chan, uint8_t cmd, uint8_t* data) {
    if (!Active(chan)) return 0;
    Port& p = g_ports[chan];
    return GBASIOJOYSendCommand(&p.driver, static_cast<GBASIOJOYCommand>(cmd), data);
}

void Tick(uint32_t chan, uint16_t gbaKeys) {
    if (!Active(chan)) return;
    Port& p = g_ports[chan];
    p.core->setKeys(p.core, gbaKeys & 0x3FFu);   // same bit order: A B Select Start Right Left Up Down R L
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - p.lastTick).count();
    p.lastTick = now;
    // 59.7275 GBA frames per second; cap the catch-up so a stall never runs the core for seconds.
    p.pendingFrames += (dt > 0.25 ? 0.25 : dt) * 59.7275;
    int n = 0;
    while (p.pendingFrames >= 1.0 && n < 4) { p.core->runFrame(p.core); p.pendingFrames -= 1.0; ++n; ++p.framesRun; }
}

void Nudge(uint32_t chan, bool urgent) {
    if (!Active(chan)) return;
    Port& p = g_ports[chan];
    if (p.pendingFrames < (urgent ? -12.0 : -3.0)) return;   // overdraft cap: never race ahead of wall clock by more than a few frames
    RunCycles(p, kSliceCycles);
}

void AfterRead(uint32_t chan) {
    if (!Active(chan)) return;
    Port& p = g_ports[chan];
    GBA* gba = static_cast<GBA*>(p.core->board);
    p.core->setKeys(p.core, FfccGbaClientKeys(chan) & 0x3FFu);   // the client samples KEYINPUT when it refills its pad word
    for (int i = 0; i < 16 && p.pendingFrames > -6.0; ++i) {
        RunCycles(p, kSliceCycles);
        if (gba->memory.io[GBA_REG(JOYSTAT)] & 0x08u) break;   // next word queued
    }
}

static bool g_hidden[4] = {false, false, false, false};
void NoteHidden(uint32_t chan) { if (chan < kPorts) g_hidden[chan] = true; }
bool TakeHiddenSinceUpload(uint32_t chan) { if (chan >= kPorts || !g_hidden[chan]) return false; g_hidden[chan] = false; return true; }
void AfterWrite(uint32_t chan, bool expectReply) {
    if (!Active(chan)) return;
    Port& p = g_ports[chan];
    GBA* gba = static_cast<GBA*>(p.core->board);
    int i = 0;
    for (; i < 32; ++i) {
        RunCycles(p, kSliceCycles);
        if ((gba->memory.io[GBA_REG(JOYSTAT)] & 0x02u) == 0) break;   // consumed
    }
    // Then let it answer: a handheld's reply to a command is queued within microseconds, and the
    // GameCube's roster clears a pending creation if it sees the link in its mode-switch states
    // (GetGBAConnect: 0x14..0x16 = not connected) for even one frame (measured).
    // Stream words (data file, command list, letters, map) get no reply: waiting a client frame for one
    // per word throttled every screen load to the client's frame rate (measured: seconds per list).
    for (; expectReply && i < 48 && p.pendingFrames > -12.0; ++i) {
        if (gba->memory.io[GBA_REG(JOYSTAT)] & 0x08u) break;   // reply queued
        RunCycles(p, kSliceCycles);
    }
}

const uint32_t* Frame(uint32_t chan) { return Active(chan) ? g_ports[chan].frame.data() : nullptr; }
uint32_t FrameCounter(uint32_t chan) { return Active(chan) ? g_ports[chan].core->frameCounter(g_ports[chan].core) : 0; }
void RunAhead(uint32_t chan) {
    if (!Active(chan)) return;
    Port& p = g_ports[chan];
    if (p.pendingFrames < -8.0) return;   // never more than a few frames ahead of wall clock
    p.core->runFrame(p.core); p.pendingFrames -= 1.0; ++p.framesRun;
}

void Shutdown() { for (Port& p : g_ports) Destroy(p); }

} // namespace FfccMgba

#else   // no mGBA linked: the fake GBA in ffcc_gba.cpp stays in charge

namespace FfccMgba {
bool Available() { return false; }
bool Active(uint32_t) { return false; }
void Nudge(uint32_t) {}
void AfterWrite(uint32_t, bool) {}
void NoteHidden(uint32_t) {}
bool TakeHiddenSinceUpload(uint32_t) { return false; }
void AfterRead(uint32_t) {}
bool Boot(uint32_t, const uint8_t*, size_t) { return false; }
int Command(uint32_t, uint8_t, uint8_t*) { return 0; }
void Tick(uint32_t, uint16_t) {}
const uint32_t* Frame(uint32_t) { return nullptr; }
uint32_t FrameCounter(uint32_t) { return 0; }
void RunAhead(uint32_t) {}
void Shutdown() {}
} // namespace FfccMgba

#endif
