// Real GBA clients for FFCC multiplayer: the game's own handheld program (dvd/gba/ffcc_cli.bin, the
// multiboot image the GameCube uploads with GBAJoyBoot) running in an embedded mGBA core per port,
// connected to the game's JoyBus SDK calls exactly like a handheld on a link cable. Dolphin's
// integrated GBA uses the same library and link (GBASIOJOYSendCommand).
#pragma once
#include <cstddef>
#include <cstdint>

namespace FfccMgba {

// True when the emulator library is linked and not disabled (WIICOMPILED_GBA=fake disables it).
bool Available();
// True once a core runs the client program on that port (after Boot).
bool Active(uint32_t chan);
// Load the multiboot image the game passed to GBAJoyBoot and start the client. Returns false when
// the core could not be created or the image is not a multiboot program.
bool Boot(uint32_t chan, const uint8_t* image, size_t len);
// JoyBus command from the GameCube side: cmd = 0xFF reset, 0x00 poll, 0x14 trans (GBA -> GC),
// 0x15 recv (GC -> GBA). `data` in/out as the SDK sees the bytes; returns the byte count mGBA
// produced (3 for reset/poll: id lo, id hi, status; 5 for trans: 4 data + status; 1 for recv).
int Command(uint32_t chan, uint8_t cmd, uint8_t* data);
// Called once per presented GameCube frame: feeds the controller keys and runs the core for the
// wall-clock time elapsed (about 1.2 GBA frames per PAL frame).
void Tick(uint32_t chan, uint16_t gbaKeys);
// Give the client a little emulated time between the GameCube's link calls (a real handheld
// answers within microseconds; the core otherwise only advances once per presented frame).
// Runs one GBA frame, charged against Tick's wall-clock budget with a small overdraft.
void Nudge(uint32_t chan, bool urgent = false);   // urgent: a larger overdraft (mode-switch handshake)
// After a GameCube -> GBA word: run the client until it has consumed it (JOYSTAT RECV clear) or
// two frames of emulated time, in ~1 ms slices. A real handheld reacts within microseconds and
// the game's link code checks the status right after writing (measured: the idle flag it clears on
// the context word was still up at the next poll and the link restarted).
void AfterWrite(uint32_t chan, bool expectReply);
// Screen visibility bookkeeping for the draw hook (pop the window out complete after each hide).
void NoteHidden(uint32_t chan);
bool TakeHiddenSinceUpload(uint32_t chan);
// After a GBA -> GameCube word was read: run the client until it has queued its next word or one
// frame passed. The handshake reads the client's host id right after its context word; a real
// SDK read takes over a millisecond, which is when the client queues it (harness: within a frame).
void AfterRead(uint32_t chan);
// Last rendered frame, 240x160 XRGB8 (row stride 240), or null.
const uint32_t* Frame(uint32_t chan);
uint32_t FrameCounter(uint32_t chan);   // emulated frames rendered so far
// Run one more frame right before the screen is uploaded, so the frame shown already contains the
// client's response to the latest keys (the game draws at 25 Hz on PAL; without this a press could
// wait a whole game frame before its effect was visible). Charged to the budget.
void RunAhead(uint32_t chan);
void Shutdown();

constexpr uint32_t kWidth = 240;
constexpr uint32_t kHeight = 160;

} // namespace FfccMgba
