// FFCC port: a fake Game Boy Advance on serial ports 1-3, so multiplayer works with plain
// PC controllers and no GBA hardware. Providers FfccGbaPortHasController/FfccGbaReadKeys live in
// aurora pad.cpp (SDL access); WIICOMPILED_FAKE_GBA=1,2,3 forces ports on, mirroring port 0's
// controller when no pad of their own is plugged in (single-pad testing).
//
// The game talks to a GBA only through the SDK's synchronous GBA API (FFCC-Decomp
// src/joybus.cpp: GBAReset, GBAGetStatus, GBARead, GBAWrite, GBAJoyBoot), and the GBA itself
// is a dumb terminal: the GameCube uploads dvd/gba/ffcc_cli.bin with JoyBoot, then exchanges
// 4-byte words. The GBA returns buttons (opcode 0x04, GBA KEYINPUT layout in bytes 2:1) and
// acknowledgements; everything the GameCube sends is display data the fake can drop.
//
// Faking at this API level means no JoyBoot cryptography: "boot succeeded" is a return code.
//
// Status byte (JOYSTAT as the game reads it, joybus.cpp):
//   0x30  error flags: the thread bails when either is set
//   0x08  receive-complete: a reply word is available to GBARead
//   0x02  send-buffer full: the previous GBAWrite has not been consumed
//   0x20  set by the client program while it runs the post-boot handshake (the thread waits
//         for exactly 0x20 before writing and 0x28 before reading in that phase)
// Return codes: GBA_READY 0, GBA_NOT_READY 1, GBA_BUSY 2, GBA_JOYBOOT_UNKNOWN_STATE 3.
#include <cstdio>
#include <cstdlib>
#include "hle_stubs.h"
#include "runtime_config.h"
#include "abi_bridge.h"
#include "runtime_log.h"
#include "memory.h"
#include "ffcc_gba_mgba.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>
#include <array>
#include <chrono>

#if defined(RECOMP_PROJECT_FFCC)

extern "C" bool FfccGbaPortHasController(uint32_t chan);   // aurora pad.cpp
extern "C" void VIWaitForRetrace_HLE_801b99ec(CpuContext* ctx);   // vi.cpp: sleeps the calling guest thread
extern "C" uint16_t FfccGbaReadKeys(uint32_t chan);        // ffcc_si.cpp: GBA KEYINPUT bits, active-high

namespace {

constexpr int32_t kGbaReady = 0;
constexpr int32_t kGbaNotReady = 1;
constexpr uint8_t kStatRecv = 0x08;
constexpr uint8_t kStatSendFull = 0x02;
constexpr uint8_t kStatClientIdle = 0x20;

constexpr uint8_t kOpPad = 0x04;
constexpr uint8_t kOpAck = 0x0E;      // [0E 00 code]: client state report (RecvGBA returns 1, no receive bit)
constexpr uint8_t kOpDataAck = 0x0C;  // [0C 00 ..]: popped by GBARecvSend's 0x0C branch with no effect,
                                      // RecvGBA returns 2 -> the receive bit the wait states need

// JoyBus::m_threadParams[chan] (see scripts/joybus_state.py): 0x28 m_state, 0x29 m_subState.
constexpr uint32_t kJoyBusThreadParams = 0x802f07d0u;
constexpr uint32_t kThreadParamSize = 0x3Cu;
uint8_t ThreadState(uint32_t chan)    { return ::Memory::Read8(kJoyBusThreadParams + chan * kThreadParamSize + 0x28); }
uint8_t ThreadSubState(uint32_t chan) { return ::Memory::Read8(kJoyBusThreadParams + chan * kThreadParamSize + 0x29); }
constexpr uint8_t kThreadInitialCode = 0x02;   // joybus.cpp ThreadMain case 0x02 -> InitialCode()

constexpr uint32_t kStreamTypes = 16;

struct FakeGba {
    std::array<std::vector<uint8_t>, kStreamTypes> payloads;   // last complete payload per type
    bool booted = false;
    bool handshake = false;          // client program in its post-boot handshake phase
    uint32_t polls = 0;              // status polls since the last yield
    uint32_t padWords = 0;           // pad words delivered (rate logged every 5 s)
    bool lastWasReply = false;       // alternation between queued replies and pad words
    bool eventPending = false;       // a client-screen request arrived; auto-confirm after 2 s
    bool confirmHeld = false;
    std::chrono::steady_clock::time_point lastConfirm{};
    std::chrono::steady_clock::time_point readyUntil{};   // resend the ready word until then
    uint32_t eventMask = 0;
    std::chrono::steady_clock::time_point eventAt{};
    uint32_t hostId = 0;             // timestamp the GameCube stored in us (echoed on reboot)
    uint32_t streamLeft = 0;         // words still expected in a [05 n ..] JoyData stream
    uint32_t streamType = 0;         // payload type from the [45 lenLo lenHi type] word
    uint32_t streamLen = 0;          // payload byte length announced by the [45 ..] word
    std::vector<uint8_t> streamBuf;  // payload bytes of the stream being received
    bool menuOpen = false;           // control mode 1 ([09 01]) or the overlay reported a screen: personal menu
    bool pauseMenu = false;          // the pause menu opened it ([14 0F 0B]); closes with [14 0F 0C]
    bool selectHeld = false;         // edge detection for the client-side menu toggle
    uint8_t ctrlMode = 0;            // last [09 mode] from the GameCube (1 = it opened our screen)
    bool confirmed = false;          // (unused) confirm sent while the GameCube holds control mode 1
    bool toggleRequest = false;      // Select pressed: the draw hook calls JoyBus::ChgCtrlMode(port)
    uint8_t modeType = 0;            // last SendMType [1B mode]: 1 = character creation, 4 = roster, 0 = game
    uint8_t keyGateMode = 0xFF;      // mode type the release gate was armed for
    bool keyGate = false;            // hold the client's keys at zero until every key is released
    bool synthPad = false;           // hybrid input: the next read hands out a live-key pad word of our own
    bool resultPending = false;      // a SendResult word [06 code] (ok) / [07 code] (rejected) arrived
    bool resultOk = false;
    uint8_t resultCode = 0;
    std::deque<uint32_t> replies;    // words the GameCube will GBARead, big-endian as in memory
    uint32_t lastWrite = 0;
    uint32_t reads = 0, writes = 0;
    std::chrono::steady_clock::time_point lastPad{};   // a real GBA sends one pad word per frame

    bool PadReady() const {
        // Guest threads are cooperative: if data is always available the JoyBus thread never
        // sleeps and the main thread starves (measured: stall watchdog right after the
        // handshake). Offer a pad word at most every 15 ms, the thread's own poll period.
        return (std::chrono::steady_clock::now() - lastPad) >= std::chrono::milliseconds(12);
    }

    uint8_t Status(uint32_t chan) const {
        // The client program shows its idle flag exactly while the GameCube runs InitialCode;
        // key it to the thread's own state instead of counting writes (a count desynced on the
        // in-town reboot: stray writes ended the "handshake" before the game started it).
        const bool inHandshake = booted && ThreadState(chan) == kThreadInitialCode;
        uint8_t s = 0;
        if (!replies.empty() || (booted && !inHandshake && PadReady())) s |= kStatRecv;
        if (inHandshake) s |= kStatClientIdle;
        return s;
    }
};

std::array<FakeGba, 4> g_gba{};

uint32_t PadWord(uint16_t keys) {
    // joybus.cpp SetPadData: combined = (data[2] << 8) | data[1]; opcode in data[0] & 0x3F.
    return (uint32_t(kOpPad) << 24) | (uint32_t(keys & 0xFF) << 16) | (uint32_t(keys >> 8) << 8);
}

void WriteStatus(uint32_t statusPtr, uint8_t v) {
    if (statusPtr != 0) Memory::Write8(statusPtr, v);
}

bool ChanOk(uint32_t chan) { return chan < 4; }

// JoyBus::m_threadParams[chan] (found by signature, see scripts/joybus_state.py): 0x28 m_state,
// 0x29 m_subState. Logging the thread's own state at every poll gives a per-port trace of the
// protocol without a debugger, which is how the reboot loop on entering town gets diagnosed.

void TraceThreadState(uint32_t chan, const char* where) {
    static uint8_t s_last[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    static unsigned s_n = 0;
    const uint32_t tp = kJoyBusThreadParams + chan * kThreadParamSize;
    const uint8_t st = ::Memory::Read8(tp + 0x28);
    if (st != s_last[chan] && s_n < 2000) {
        ++s_n;
        RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " thread state 0x" << std::hex << int(s_last[chan])
                          << " -> 0x" << int(st) << " sub " << int(::Memory::Read8(tp + 0x29)) << std::dec << " (" << where << ")" << " next " << int(::Memory::Read8(kJoyBusThreadParams + 0x94Cu + chan)) << " ctrl " << int(::Memory::Read8(kJoyBusThreadParams + 0x948u + chan)) << " modeX " << int(::Memory::Read8(kJoyBusThreadParams + 0x950u + chan)) << " code " << int(::Memory::Read8(kJoyBusThreadParams + 0x954u + chan)) << " err " << int(::Memory::Read8(kJoyBusThreadParams + chan * kThreadParamSize + 0x2E)) << " sec " << ::Memory::Read32(kJoyBusThreadParams + 0x938u + chan * 4u) << std::endl;
        s_last[chan] = st;
    }
}

// On hardware every GBA API call is an SI transfer: the SDK's sync wrapper sleeps the calling
// thread until the transfer interrupt. That sleep is what lets the main thread run, because the
// JoyBus thread's in-game loop (joybus.cpp ThreadMain: loop_body/for(;;)/switch/break) never
// yields on its own. Measured without it: the fake answered instantly, the JoyBus thread spun in
// GBAGetStatus/GBARead and the game stalled right after the handshake. Block the caller until the
// next VI retrace whenever it polls with nothing ready; calls that hand over data stay immediate.
void WaitTransfer() {
    if (CpuContext* cpu = TryGetCpuContext()) {
        VIWaitForRetrace_HLE_801b99ec(cpu);
    }
}

} // namespace

// GBAInit, 0x801a7460
namespace FfccGbaScreens {
// Last complete stream payload of a type for a port (byte 0 = type), empty if none yet.
const std::vector<uint8_t>& Payload(uint32_t chan, uint32_t type) {
    static const std::vector<uint8_t> kEmpty;
    if (chan >= 4 || type >= kStreamTypes) return kEmpty;
    return g_gba[chan].payloads[type];
}
bool MenuOpen(uint32_t chan) { return chan < 4 && (g_gba[chan].menuOpen || g_gba[chan].pauseMenu); }
} // namespace FfccGbaScreens
// Keys for the emulated client: the controller's GBA buttons minus Select, which the client would
// forward in its pad word to the GameCube's Select handler (partyobj.cpp CGPartyObj::menu reads pad 0
// for every player, see GBARead_ffcc below). Select is turned into this port's ChgCtrlMode request
// instead, exactly as the fake path does.
// Hybrid input (default; WIICOMPILED_GBA_DIRECT=0 disables): while the client's screen is hidden the controller is a plain GameCube pad
// (PADRead shows it) and the client gets no keys; while shown, the pad is hidden and the client has
// the keys. The client's frame rate then never touches the field controls.
static bool DirectInput() { static int v = -1; if (v < 0) { const char* e = std::getenv("WIICOMPILED_GBA_DIRECT"); v = (e && *e == '0') ? 0 : 1; } return v == 1; }
extern "C" bool FfccGbaClientShown(uint32_t chan) {
    return chan < 4 && FfccMgba::Active(chan) && (g_gba[chan].menuOpen || g_gba[chan].pauseMenu || g_gba[chan].modeType == 1);
}
extern "C" bool FfccGbaDirectInput() { return DirectInput(); }
// Config.toml [gba] players (default 2): ports 0..n-1 run the emulated client. WIICOMPILED_FAKE_GBA overrides.
extern "C" uint32_t FfccConfigGbaPlayers() { return RuntimeConfigFile::GbaPlayers(2); }
extern "C" uint16_t FfccGbaClientKeys(uint32_t chan) {
    if (chan >= 4) return 0;
    FakeGba& g = g_gba[chan];
    uint16_t keys = FfccGbaReadKeys(chan);
    // Release gate: a real player lets go of the button that chose the roster slot before the handheld
    // shows the creation screen; here the same controller feeds both, and the still-held A reached the
    // client's new screen (measured: right after [1B 01] the client sent the empty name and [14 04]
    // cancel, the game answered mode 4 and the roster restarted the creation, forever). Hold the keys at
    // zero after every mode switch until all are released.
    if (g.modeType != g.keyGateMode) { g.keyGateMode = g.modeType; g.keyGate = true; }
    if (g.keyGate) { if ((keys & 0x03FFu) == 0) g.keyGate = false; else keys = 0; }
    if (DirectInput() && !FfccGbaClientShown(chan)) keys &= 0x0004u;   // only Select (for the toggle below)
    const bool selectDown = (keys & 0x0004u) != 0;
    if (selectDown && !g.selectHeld && g.modeType != 1 && !g.pauseMenu) {
        g.toggleRequest = true;
        RT_LOG(RT_TAG_OS) << "mgba link: port " << chan << " select -> request ChgCtrlMode" << std::endl;
    }
    g.selectHeld = selectDown;
    return uint16_t(keys & ~0x0004u);
}
namespace FfccGbaScreens {
bool Confirmed(uint32_t chan) { return chan < 4 && g_gba[chan].confirmed; }
bool TakeToggleRequest(uint32_t chan) {
    if (chan >= 4 || !g_gba[chan].toggleRequest) return false;
    g_gba[chan].toggleRequest = false;
    return true;
}
uint32_t ModeType(uint32_t chan) { return chan < 4 ? g_gba[chan].modeType : 0; }
// Consume the GameCube's answer to a creation packet (JoyBus::SendResult: [06 code] accepted,
// [07 code] rejected; code echoes the packet's sub-command byte).
bool TakeResult(uint32_t chan, bool& ok, uint8_t& code) {
    if (chan >= 4 || !g_gba[chan].resultPending) return false;
    g_gba[chan].resultPending = false;
    ok = g_gba[chan].resultOk;
    code = g_gba[chan].resultCode;
    return true;
}
// Queue a client word for the GameCube (e.g. [1F slot idxLo idxHi] to assign a command slot).
void SendWord(uint32_t chan, uint32_t word) { if (chan < 4) g_gba[chan].replies.push_back(word); }
// The player finished the screen: report it closed ([0E 00 00 00], m_stateCodeArr = 0) and send the
// confirm word ([06 18 00 00] -> m_evtState1 = 1, gbaque.cpp:906), then keep resending the confirm
// for a while in case the script starts its wait afterwards (-0x75 zeroes m_evtState1).
// The overlay switched screens: report the code a real client would (joybus.cpp:2778 stores byte 2
// in m_stateCodeArr; the per-frame block then streams that screen's data).
void ReportScreen(uint32_t chan, uint8_t code) {
    if (chan >= 4) return;
    g_gba[chan].replies.push_back(0x0E000000u | (uint32_t(code) << 8));
    g_gba[chan].menuOpen = true;
    RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " screen report code " << int(code) << std::endl;
}
void Confirm(uint32_t chan) {
    if (chan >= 4) return;
    FakeGba& g = g_gba[chan];
    g.replies.push_back(0x0E000000u);
    g.replies.push_back(0x06180000u);
    g.lastConfirm = std::chrono::steady_clock::now();
    g.readyUntil = g.lastConfirm + std::chrono::seconds(6);
    RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " screen done -> code 0 + confirm" << std::endl;
}
} // namespace FfccGbaScreens

extern "C" void GBAInit_ffcc() {
    for (auto& g : g_gba) g = FakeGba{};
    RT_LOG(RT_TAG_OS) << "fake GBA: initialised (ports 1-3 follow PC controllers)" << std::endl;
}
PPC_NATIVE_OVERRIDE_VOID(801a7460, GBAInit_ffcc, (), ());

// GBAReset, 0x801a765c
extern "C" uint32_t GBAReset_ffcc(uint32_t chan, uint32_t statusPtr) {
    if (!ChanOk(chan) || !FfccGbaPortHasController(chan)) return kGbaNotReady;
    if (FfccMgba::Active(chan)) {
        uint8_t d[8] = {0};
        FfccMgba::Command(chan, 0xFF, d);   // JOY_RESET: id + JOYSTAT
        WaitTransfer();
        WriteStatus(statusPtr, d[2]);
        return kGbaReady;
    }
    RT_LOG(RT_TAG_OS) << "fake GBA: reset on port " << chan << " (was booted=" << g_gba[chan].booted
                      << " handshake=" << g_gba[chan].handshake << " writes=" << g_gba[chan].writes << ")" << std::endl;
    g_gba[chan] = FakeGba{};
    WaitTransfer();
    WriteStatus(statusPtr, 0);
    return kGbaReady;
}
PPC_NATIVE_OVERRIDE(801a765c, GBAReset_ffcc, uint32_t, (uint32_t a0, uint32_t a1), (a0, a1));

// GBAGetStatus, 0x801a7568
extern "C" uint32_t GBAGetStatus_ffcc(uint32_t chan, uint32_t statusPtr) {
    if (!ChanOk(chan) || !FfccGbaPortHasController(chan)) return kGbaNotReady;
    TraceThreadState(chan, "poll");
    if (FfccMgba::Active(chan)) {
        uint8_t d[8] = {0};
        // Every other poll runs the client one frame so it can answer between the game's checks
        // (InitialCode expects the exact status on each step and restarts from sub-state 0 on a miss).
        // Mode-switch handshake (0x14 SendChkCrc .. 0x16 data file): the roster's CalcCharaSelect treats
        // these states as 'GBA not connected' (joybus.cpp GetGBAConnect) and clears the pending creation
        // if it samples one, then asks for mode 4 - the loop that ended every creation (measured). On
        // hardware the handshake takes microseconds; here a retrace yield every 16 polls stretched it
        // over frames. Do not yield inside it (bounded), and let the client run ahead a little more.
        const uint8_t st = ThreadState(chan);
        // 0x16 (SendDataFile: the boot data file, hundreds of words) must keep yielding or the game
        // thread starves during the upload (user: input delay at the title until the client settled).
        const bool critical = st == 0x14 || st == 0x15;
        // Nudge only while the link needs quick answers (boot handshake, mode switch): in play the
        // client advances in Tick and AfterRead; a slice per poll cost host time on the game's thread.
        if (critical || st < 5) FfccMgba::Nudge(chan, critical);
        // Feed keys and run the client's due frames here, at poll rate: the draw hook that used to do it
        // runs 25 times a second in menus, so a press could wait 40 ms before the client saw it
        // (measured latency probe: hidden-screen input equals the fake's, the screen path lagged).
        if (!critical && st >= 5) FfccMgba::Tick(chan, FfccGbaClientKeys(chan));
        FfccMgba::Command(chan, 0x00, d);   // JOY_POLL: JOYSTAT in d[2]
        // Hybrid input: the fake client offered a fresh pad word every 12 ms at whatever poll came, so
        // every game frame had one; the real client queues one per emulated frame and the link
        // thread reads them in bursts, so some game frames got none and GetPadData (which clears
        // the flags) froze the character for a frame. Offer our own live-key pad word in between.
        if (FfccGbaDirectInput() && !FfccGbaClientShown(chan) && (d[2] & 0x08u) == 0 && !critical && st >= 5 && st < 0x80 && g_gba[chan].PadReady()) {
            g_gba[chan].synthPad = true; d[2] |= 0x08u;
        }
        // The JoyBus thread polls in a loop; a real client nearly always has a word queued (JOYSTAT
        // 0x08), so the yield must not depend on it or the main thread starves (measured: 2 fps).
        // While the game streams a screen's data (states other than play/roster/idle) yield every 64 polls
        // instead of 16: the words move about four times faster and the screens load sooner.
        const bool streaming = !critical && st >= 5 && !(st == 0x05 || st == 0x06 || st == 0x3B);
        const uint32_t yieldEvery = critical ? 1024u : (streaming ? 64u : 16u);
        if ((++g_gba[chan].polls % yieldEvery) == 0u) WaitTransfer();
        WriteStatus(statusPtr, d[2]);
        return kGbaReady;
    }
    // Yield regularly, never per poll. Per poll (one retrace each) starved the LINK instead of
    // the main thread: in town the game queues per-frame words faster than ~50/s, the send queue
    // filled (state 0x05 -> 0x85) and the thread rebooted the GBA. A real SI transfer is ~100 us,
    // so sixteen polls between retrace yields is still far slower than hardware and still keeps
    // the cooperative scheduler fed (measured freezes came from never yielding at all).
    if (g_gba[chan].replies.empty() && (++g_gba[chan].polls % 16u) == 0u) {
        WaitTransfer();
    }
    WriteStatus(statusPtr, g_gba[chan].Status(chan));
    return kGbaReady;
}
PPC_NATIVE_OVERRIDE(801a7568, GBAGetStatus_ffcc, uint32_t, (uint32_t a0, uint32_t a1), (a0, a1));

// GBAGetProcessStatus, 0x801a76fc: boot progress percent; we boot instantly.
extern "C" uint32_t GBAGetProcessStatus_ffcc(uint32_t chan, uint32_t percentPtr) {
    if (!ChanOk(chan)) return kGbaNotReady;
    if (percentPtr != 0) Memory::Write8(percentPtr, 100);
    return kGbaReady;
}
PPC_NATIVE_OVERRIDE(801a76fc, GBAGetProcessStatus_ffcc, uint32_t, (uint32_t a0, uint32_t a1), (a0, a1));

// GBAJoyBoot, 0x801a8734 (sync). The program image is ignored; success is reported at once
// and the client enters its handshake phase (status 0x20).
extern "C" uint32_t GBAJoyBoot_ffcc(uint32_t chan, uint32_t paletteColor, uint32_t paletteSpeed,
                                    uint32_t programPtr, uint32_t length, uint32_t statusPtr) {
    (void)paletteColor; (void)paletteSpeed;
    if (!ChanOk(chan) || !FfccGbaPortHasController(chan)) return kGbaNotReady;
    FakeGba& g = g_gba[chan];
    if (FfccMgba::Available() && length >= 0xC0 && length <= 0x40000) {
        // Real client: the image the game hands us is dvd/gba/ffcc_cli.bin; boot it in mGBA.
        const uint8_t* img = Memory::GetPointer(programPtr, length);
        if (img && FfccMgba::Boot(chan, img, length)) {
            for (int i = 0; i < 5; ++i) WaitTransfer();
            uint8_t d[8] = {0};
            FfccMgba::Command(chan, 0x00, d);
            WriteStatus(statusPtr, d[2]);
            RT_LOG(RT_TAG_OS) << "mgba: JoyBoot on port " << chan << " -> client running, JOYSTAT 0x" << std::hex << int(d[2]) << std::dec << std::endl;
            return kGbaReady;
        }
    }
    for (int i = 0; i < 5; ++i) WaitTransfer();   // the real upload takes seconds; keep it a few frames
    g.booted = true;
    g.replies.clear();
    WriteStatus(statusPtr, g.Status(chan));
    RT_LOG(RT_TAG_OS) << "fake GBA: JoyBoot accepted on port " << chan << " (" << length << " bytes ignored)" << std::endl;
    return kGbaReady;
}
PPC_NATIVE_OVERRIDE(801a8734, GBAJoyBoot_ffcc, uint32_t,
                    (uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5),
                    (a0, a1, a2, a3, a4, a5));

// GBAWrite, 0x801a89a0: a 4-byte word from the GameCube.
//
// Post-boot handshake (joybus.cpp InitialCode, sub-states 1..5), as the game checks it:
//   write 1  disk id      status must be exactly 0x20 before; afterwards (status & 0x30) == 0x20
//   read     context      status must be 0x28; byte0 == 1, byte1 = bootFlag<<6 | unk<<4 | retries
//   read     host id      status must be 0x28; the timestamp we stored last time (0 when fresh)
//   write 2  host id      status must be 0x20; we store it
//   write 3  context      status must be 0x20; byte0 = 1, byte1 = port | flags
// then SendMType/SendMapNo/SendLanguage go through the normal queued-write path (status bit 0x02
// clear) and the thread enters state 0x14, where any 0x30 bit is an error: the idle flag drops.
// Link trace (WIICOMPILED_GBA_TRACE=path): every word between the game and the emulated client, for
// offline replay in the standalone harness. Record: tag (W/R/S), port, thread state, word (host order).
static void TraceLink(char tag, uint32_t chan, uint32_t word) {
    static FILE* f = nullptr; static bool tried = false;
    if (!tried) { tried = true; if (const char* e = std::getenv("WIICOMPILED_GBA_TRACE")) f = std::fopen(e, "wb"); }
    if (!f) return;
    const uint8_t hdr[3] = {uint8_t(tag), uint8_t(chan), ThreadState(chan)};
    std::fwrite(hdr, 1, 3, f); std::fwrite(&word, 4, 1, f); std::fflush(f);
}
extern "C" uint32_t GBAWrite_ffcc(uint32_t chan, uint32_t srcPtr, uint32_t statusPtr) {
    if (!ChanOk(chan) || !FfccGbaPortHasController(chan)) return kGbaNotReady;
    FakeGba& g = g_gba[chan];
    if (FfccMgba::Active(chan)) {
        uint8_t d[8] = {0};
        for (uint32_t i = 0; i < 4; ++i) d[i] = Memory::Read8(srcPtr + i);
        TraceLink('W', chan, Memory::Read32(srcPtr));
        FfccMgba::Command(chan, 0x15, d);   // JOY_RECV: GameCube -> GBA, JOYSTAT back in d[0]
        // Track the words the overlay state depends on, as the fake path does below: [09 mode] control
        // mode (personal menu open), [1B mode] mode type (1 = creation), [14 0F 0B/0C] pause open/close.
        {
            const uint32_t w = Memory::Read32(srcPtr);
            const uint32_t op = (w >> 24) & 0xFFu, b1 = (w >> 16) & 0xFFu, b2 = (w >> 8) & 0xFFu;
            if (op == 0x09u) { g.menuOpen = (b1 != 0); g.ctrlMode = uint8_t(b1); }
            else if (op == 0x1Bu) g.modeType = uint8_t(b1);
            else if (op == 0x14u && b1 == 0x0Fu) { if (b2 == 0x0Bu) g.pauseMenu = true; else if (b2 == 0x0Cu) g.pauseMenu = false; }
        }
        { static unsigned s_n = 0; const uint32_t w = Memory::Read32(srcPtr); const uint32_t wop = (w >> 24) & 0x3Fu; const uint32_t wb1 = (w >> 16) & 0xFFu; const bool interesting = wop == 0x09u || wop == 0x1Bu || wop == 0x0Au || wop == 0x0Du || wop == 0x10u || wop == 0x06u || wop == 0x07u || wop == 0x0Cu || (wop == 0x14u && wb1 == 0x0Fu) || (wop == 0x14u && wb1 == 0x11u);
          if (interesting && s_n < 400) { ++s_n; RT_LOG(RT_TAG_OS) << "mgba link: port " << chan << " write " << std::hex << ((w >> 24) & 0xFFu) << ' ' << ((w >> 16) & 0xFFu) << ' ' << ((w >> 8) & 0xFFu) << ' ' << (w & 0xFFu) << " joystat " << int(d[0]) << " state 0x" << int(ThreadState(chan)) << " sub " << int(ThreadSubState(chan)) << std::dec << std::endl; } }
        {   // commands and the mode-switch handshake get an answer; stream words do not
            const uint32_t w = Memory::Read32(srcPtr); const uint32_t op = (w >> 24) & 0x3Fu; const bool cont = ((w >> 24) & 0xC0u) != 0;
            const uint8_t st = ThreadState(chan);
            const bool expectReply = !cont && (st == 0x14 || st == 0x15 || st < 5 || op == 0x09u || op == 0x0Au || op == 0x0Cu || op == 0x0Du || op == 0x10u || op == 0x1Bu);
            FfccMgba::AfterWrite(chan, expectReply);   // let the client consume it, as hardware would before the next poll
        }
        FfccMgba::Command(chan, 0x00, d);   // report the post-consumption status
        d[0] = d[2];
        ++g.writes;
        WriteStatus(statusPtr, d[0]);
        return kGbaReady;
    }
    g.lastWrite = Memory::Read32(srcPtr);
    ++g.writes;
    TraceThreadState(chan, "write");
    if (ThreadState(chan) == kThreadInitialCode) {
        // InitialCode (joybus.cpp:3599): sub 1 WriteInitialCode (disk id) -> we must then show
        // 0x28 twice: context [01 00 00 00] and the stored host id; sub 4 WriteHostId (store);
        // sub 5 WriteContext (ignored). Anything else written in this state is a leftover queued
        // command: acknowledged with the harmless data word so the queue drains.
        const uint8_t sub = ThreadSubState(chan);
        if (sub == 1) {
            g.replies.clear();
            g.replies.push_back(0x01000000u);
            g.replies.push_back(g.hostId);
        } else if (sub == 4) {
            g.hostId = g.lastWrite;
        } else if (sub == 5) {
            RT_LOG(RT_TAG_OS) << "fake GBA: handshake complete on port " << chan
                              << " context=0x" << std::hex << g.lastWrite << std::dec << std::endl;
        } else {
            g.replies.push_back(uint32_t(kOpDataAck) << 24);
        }
    } else {
        const uint32_t op = (g.lastWrite >> 24) & 0x3Fu;
        const uint32_t b1 = (g.lastWrite >> 16) & 0xFFu;
        const uint32_t raw0 = (g.lastWrite >> 24) & 0xFFu;
        // Wait states advance only on a DATA word (GBARecvSend: recvBit = (RecvGBA == 2)); pad
        // words and [0E 00 code] reports never count, and unconsumed data words poison the port
        // (RecvGBA stops at 64 queued). [0C 00 00 00] is popped by the 0x0C branch with no effect
        // and still counts. Streams (MakeJoyData): [05 n crc] then n-1 words [45 ..]/[85 ..].
        if (raw0 == 0x1Bu) {
            // SendMType [1B mode] (joybus.cpp SendMType): 1 = character creation on this GBA
            // (GbaQue.InitCmakeInfo, wm_menu.cpp:7443), 4 = roster/menu, 0 = in game.
            g.modeType = uint8_t(b1);
            g.resultPending = false;
            RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " mode type " << b1 << std::endl;
        } else if (raw0 == 0x06u || raw0 == 0x07u) {
            g.resultPending = true;
            g.resultOk = (raw0 == 0x06u);
            g.resultCode = uint8_t(b1);
            RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << (g.resultOk ? " result OK code 0x" : " result REJECTED code 0x")
                              << std::hex << b1 << std::dec << std::endl;
        }
        if (op == 0x14 && b1 == 0x0F) {
            // SendOpenMenu [14 0F id] (joybus.cpp:977-985): id 0x0B = the player paused (personal
            // menu opens on the GBA), 0x0C = unpaused (menu closes). The client reports state
            // code 1 while open (GC then streams the command list, state 0x4C) and 0 when closed.
            const uint32_t arg = (g.lastWrite >> 8) & 0xFFu;
            if (arg == 0x0Cu) {
                g.pauseMenu = false;
                g.replies.push_back(0x0E000000u);   // [0E 00 00 00]: back to field
            } else {
                g.pauseMenu = true;
                g.replies.push_back(0x0E000100u);   // [0E 00 01 00]: personal menu open
            }
            g.readyUntil = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " menu 0x" << std::hex << arg << std::dec
                              << (arg == 0x0Cu ? " close -> code 0" : " open -> code 1") << std::endl;
        } else if (op == 0x14 && (b1 == 0x12 || b1 == 0x13)) {
            g.readyUntil = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        } else if (op == 0x09) {
            // SetCtrlMode / ChgCtrlMode [09 mode] (joybus.cpp:5996-6110): 1 = this player's GBA
            // holds the personal menu (Select on the GBA toggles it: partyobj.cpp menu(),
            // buttonDown[1] & 0x10; the script's OpenMenu op -0x97 also sets it), 0 = back to
            // the field. While in the menu the client reports screen code 1, which makes the
            // GameCube stream the command list (state 0x4C); code 0 on leaving. The report is
            // an [0E 00 code] word: it does not count as a data word for the wait states.
            g.menuOpen = (b1 != 0);
            g.ctrlMode = uint8_t(b1);
            g.confirmed = false;
            g.replies.push_back(g.menuOpen ? 0x0E000100u : 0x0E000000u);
            if (!g.menuOpen) {
                g.replies.push_back(0x06180000u);   // leaving the screen also confirms (m_evtState1 = 1)
                g.lastConfirm = std::chrono::steady_clock::now();
                g.readyUntil = g.lastConfirm + std::chrono::seconds(6);
            }
            RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " ctrl mode " << b1
                              << (g.menuOpen ? " (menu) -> code 1" : " (field) -> code 0") << std::endl;
        }
        if (raw0 == 0x05u) {
            g.streamLeft = (b1 > 0) ? b1 - 1 : 0;
            g.streamType = 0;
            g.streamLen = 0;
            g.streamBuf.clear();
            if (g.streamLeft == 0) g.replies.push_back(uint32_t(kOpDataAck) << 24);
        } else if (g.streamLeft > 0 && (raw0 == 0x45u || raw0 == 0x85u)) {
            if (raw0 == 0x45u) {
                // [45 lenLo lenHi type]: MakeJoyData puts the payload's first byte (the type) here
                // and the remaining bytes three per [85 b b b] word. Reassemble the payload for
                // the personal-screen overlay (FfccGbaScreens::Payload).
                g.streamType = g.lastWrite & 0xFFu;
                g.streamLen = ((g.lastWrite >> 16) & 0xFFu) | (((g.lastWrite >> 8) & 0xFFu) << 8);
                g.streamBuf.clear();
                g.streamBuf.push_back(uint8_t(g.streamType));
            } else {
                g.streamBuf.push_back(uint8_t((g.lastWrite >> 16) & 0xFFu));
                g.streamBuf.push_back(uint8_t((g.lastWrite >> 8) & 0xFFu));
                g.streamBuf.push_back(uint8_t(g.lastWrite & 0xFFu));
            }
            if (--g.streamLeft == 0) {
                if (g.streamType < kStreamTypes) {
                    if (g.streamBuf.size() > g.streamLen && g.streamLen > 0) g.streamBuf.resize(g.streamLen);
                    g.payloads[g.streamType] = g.streamBuf;
                    static unsigned s_logged = 0;
                    if (s_logged < 200) { ++s_logged; RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " stream type 0x" << std::hex << g.streamType << std::dec << " payload " << g.streamBuf.size() << " bytes (announced " << g.streamLen << ")" << std::endl; }
                }
                g.replies.push_back(uint32_t(kOpDataAck) << 24);
                if (g.menuOpen && g.streamType == 0x0Cu) {
                    // Command list received: this is where the overlay will show and edit it.
                    g.readyUntil = std::chrono::steady_clock::now() + std::chrono::seconds(6);
                    RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " command list received (type 0x0C)" << std::endl;
                }
            }
        } else if ((op == 0x0C && b1 == 0x0E) || op == 0x1B) {
            g.replies.push_back(uint32_t(kOpAck) << 24);        // state code 0: normal play
        } else {
            g.replies.push_back(uint32_t(kOpDataAck) << 24);    // generic data reply
        }
        static unsigned s_logged = 0;
        // Stream words ([45 ..]/[85 ..]) are hundreds per stage and drowned the log; single
        // commands are the interesting ones.
        if (raw0 != 0x45u && raw0 != 0x85u && s_logged < 600) {
            ++s_logged;
            RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " cmd 0x" << std::hex << g.lastWrite
                              << std::dec << std::endl;
        }
    }
    WriteStatus(statusPtr, g.Status(chan));
    return kGbaReady;
}
PPC_NATIVE_OVERRIDE(801a89a0, GBAWrite_ffcc, uint32_t, (uint32_t a0, uint32_t a1, uint32_t a2), (a0, a1, a2));

// GBARead, 0x801a8848: hand back the next queued word, else the current buttons.
extern "C" uint32_t GBARead_ffcc(uint32_t chan, uint32_t dstPtr, uint32_t statusPtr) {
    if (!ChanOk(chan) || !FfccGbaPortHasController(chan)) return kGbaNotReady;
    FakeGba& g = g_gba[chan];
    if (FfccMgba::Active(chan)) {
        uint8_t d[8] = {0};
        if (g.synthPad) {
            g.synthPad = false;
            const uint16_t skeys = uint16_t(FfccGbaReadKeys(chan) & ~0x0004u);
            const uint32_t word = PadWord(skeys);
            Memory::Write32(dstPtr, word);
            g.lastPad = std::chrono::steady_clock::now(); ++g.padWords; ++g.reads;
            uint8_t s2[8] = {0}; FfccMgba::Command(chan, 0x00, s2);
            WriteStatus(statusPtr, s2[2]);
            return kGbaReady;
        }
        FfccMgba::Command(chan, 0x14, d);   // JOY_TRANS: 4 bytes from the GBA + JOYSTAT in d[4]
        TraceLink('R', chan, (uint32_t(d[0]) << 24) | (uint32_t(d[1]) << 16) | (uint32_t(d[2]) << 8) | d[3]);
        { static unsigned s_n = 0; if ((d[0] & 0x3Fu) != 0x04u && (d[0] & 0x3Fu) != 0x05u && !((d[0] & 0x3Fu) == 0x06u && d[1] == 0 && d[2] == 0) && s_n < 400) { ++s_n; RT_LOG(RT_TAG_OS) << "mgba link: port " << chan << " read " << std::hex << int(d[0]) << ' ' << int(d[1]) << ' ' << int(d[2]) << ' ' << int(d[3]) << " status 0x" << int(d[4]) << " state 0x" << int(ThreadState(chan)) << " sub " << int(ThreadSubState(chan)) << std::dec << std::endl; } }
        // Hybrid input (WIICOMPILED_GBA_DIRECT=1): while the client's screen is hidden its pad words carry
        // the live controller keys, sampled at the moment the GameCube reads them, as the fake client did
        // (the game takes the GBA word for a GBA port and ignores the raw pad, pad.cpp:296-299; SetPadData
        // reads KEYINPUT bits from bytes 1-2). Select stays out: it is this port's menu toggle.
        if (FfccGbaDirectInput() && !FfccGbaClientShown(chan) && (d[0] & 0x3Fu) == 0x04u) {
            const uint16_t keys = uint16_t(FfccGbaReadKeys(chan) & ~0x0004u);
            d[1] = uint8_t(keys & 0xFFu); d[2] = uint8_t((keys >> 8) & 0x03u);
            g.lastPad = std::chrono::steady_clock::now();
        }
        for (uint32_t i = 0; i < 4; ++i) Memory::Write8(dstPtr + i, d[i]);
        FfccMgba::AfterRead(chan);          // the client queues its next word during a real read's transfer time
        { uint8_t s2[8] = {0}; FfccMgba::Command(chan, 0x00, s2); d[4] = s2[2]; }
        ++g.reads;
        WriteStatus(statusPtr, d[4]);
        return kGbaReady;
    }
    uint32_t word;
    const bool inHandshake = ThreadState(chan) == kThreadInitialCode;
    // Strict alternation between queued replies and pad words. Replies first starved the pad
    // (a frame without a fresh pad word freezes the character: GetPadData clears the flags);
    // pad first starved the replies (states 0x19/0x2B wait for the client's data word with a
    // 500-2000 ms timeout, then reboot the link: measured 14 JoyBoots entering town).
    const bool padReady = !inHandshake && g.PadReady();
    if (!g.replies.empty() && !(padReady && g.lastWasReply)) {
        word = g.replies.front(); g.replies.pop_front(); g.lastWasReply = true;
    } else if (inHandshake) {
        word = 0; g.lastWasReply = false;
    } else {
        uint16_t keys = FfccGbaReadKeys(chan);
        // While the personal screen is open the overlay consumes the keys; only Select (the
        // GameCube-side menu toggle, partyobj.cpp menu()) still reaches the game, so A/B edit the
        // list instead of confirming, and Start cannot pause from inside the screen.
        // Select opens and closes this port's own screen, like a real GBA whose menu is local and
        // only reports its screen code. It is never passed to the game: the GameCube's Select handler
        // (partyobj.cpp CGPartyObj::menu, GBA buttonDown[1] & 0x10) indexes the pad array with
        // `slot & ~((~(debugPadPort - slot | slot - debugPadPort)) >> 31)`, which is pad 0 for every
        // player unless a debug pad is on that slot, so controller 1's Select toggled every player's
        // control mode and controller 2's did nothing (measured: paired ctrl-mode flips in the log).
        const bool selectDown = (keys & 0x0004u) != 0;
        if (selectDown && !g.selectHeld && g.modeType != 1 && !g.pauseMenu) {
            // Ask the GameCube to flip THIS port's control mode (JoyBus::ChgCtrlMode, called through
            // the bridge from the draw hook): it answers [09 mode], which opens/closes the screen,
            // and control mode 0 on every port is what releases "Prepare your command lists"
            // (measured: the release only ever followed the game's own [09 00]).
            g.toggleRequest = true;
            RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " select -> request ChgCtrlMode" << std::endl;
        }
        g.selectHeld = selectDown;
        keys &= ~0x0004u;
        // Start always passes: it is the pause toggle (system.cpp:355-380) and the pause itself opens
        // this menu, so masking it left the game stuck on PAUSE with no way back (measured).
        if (g.menuOpen || g.pauseMenu || g.modeType == 1) keys &= 0x0008u;
        // The "Prepare your command lists" screen is announced to the client only through the
        // script's event state (no command reaches the link when it appears; measured), and the
        // GameCube then waits for the client's confirm word [06 18 00 00] -> ExecutQueue sets
        // m_evtState1 = 1 (gbaque.cpp:~906), polled by the script (cflat_r2class.cpp:1739), which
        // resets it whenever it starts a new event, so a confirm with nothing pending is harmless.
        // Send it on every fresh A/Start press (edge-triggered, 500 ms debounce), plus the 2 s
        // auto-confirm after an explicit screen request.
        const bool confirmDown = (keys & 0x0009u) != 0;   // A or Start
        const auto now = std::chrono::steady_clock::now();
        if (confirmDown && !g.confirmHeld) g.readyUntil = now + std::chrono::seconds(6);
        g.confirmHeld = confirmDown;
        // The script's event setup (-0x75, cflat_r2class.cpp:1723-1728) zeroes m_evtState1, so a
        // single early confirm is lost. Resend [06 18 00 00] every 400 ms for six seconds after
        // a press or a menu event; the script's wait (-0x76) then sees it whenever it starts.
        if (now < g.readyUntil && now - g.lastConfirm >= std::chrono::milliseconds(400)) {
            g.lastConfirm = now;
            word = 0x06180000u;
            static unsigned s_n = 0; if (s_n < 60) { ++s_n; RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " ready word" << std::endl; }
        } else {
            word = PadWord(keys);
        }
        g.lastPad = std::chrono::steady_clock::now(); ++g.padWords; g.lastWasReply = false;
    }
    {
        static std::chrono::steady_clock::time_point s_rateAt[4]{};
        static uint32_t s_rateBase[4]{};
        const auto now = std::chrono::steady_clock::now();
        if (now - s_rateAt[chan] >= std::chrono::seconds(5)) {
            if (s_rateAt[chan].time_since_epoch().count() != 0) {
                RT_LOG(RT_TAG_OS) << "fake GBA: port " << chan << " pad words/5s=" << (g.padWords - s_rateBase[chan])
                                  << " queued=" << g.replies.size() << " reads=" << g.reads << std::endl;
            }
            s_rateAt[chan] = now; s_rateBase[chan] = g.padWords;
        }
    }
    Memory::Write32(dstPtr, word);
    ++g.reads;
    WriteStatus(statusPtr, g.Status(chan));
    return kGbaReady;
}
PPC_NATIVE_OVERRIDE(801a8848, GBARead_ffcc, uint32_t, (uint32_t a0, uint32_t a1, uint32_t a2), (a0, a1, a2));

#endif // RECOMP_PROJECT_FFCC
