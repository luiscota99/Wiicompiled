// Report a stereo console to the game.
//
// OSGetSoundMode reads bit 2 of the SRAM flags, which on real hardware holds the Mono/Stereo choice
// from the console's system menu. This runtime does not emulate SRAM or the RTC device behind it, so
// the read returns zeros and the console looks like it is set to MONO.
//
// The game believes it, and the consequence is not subtle. FFCC's sound driver branches on the mode
// in SetVoiceVolumeMix (FFCC-Decomp/src/RedSound/RedExecute.cpp):
//
//     switch (RedSoundPlayModeGet()) {
//     case REDSOUND_SOUND_MODE_MONO:
//         leftMix = (volume * RedPanningDataGet(REDSOUND_PAN_BYTE_CENTER)) >> REDSOUND_PAN_MIX_SHIFT;
//         axMix->vL = (u16)leftMix;
//         axMix->vR = (u16)leftMix;      // pan discarded, both channels identical
//
// so every voice is written with equal left and right volumes and the pan argument is thrown away.
// For a stereo music stream, whose two halves are deliberately panned hard apart in
// RedStream.cpp, that collapses the whole thing to mono.
//
// Measured before this fix, on the opening cutscene: our output had an L/R correlation of exactly
// 1.0000 and side/mid energy of 0.0000 -- bit-identical channels -- against Dolphin's 0.8749 and
// 0.2586 on the same scene. It also ran about 3 dB hot and clipped, because summing two perfectly
// correlated channels adds level that a properly panned mix does not. Quiet menu passages measured
// 0.21 side energy and were fine, which is what narrowed this to the mode rather than the mixer.
//
// Returning STEREO is the honest answer for a PC port: there is no console menu to read, and every
// other part of the audio path has been verified against the hardware structures.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "runtime_log.h"
#include "memory.h"

#include <cstdint>

#if defined(RECOMP_PROJECT_FFCC)

namespace {
constexpr uint32_t kOsSoundModeStereo = 1u;  // OS_SOUND_MODE_STEREO; MONO is 0
} // namespace

extern "C" uint32_t OSGetSoundMode_ffcc()
{
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        RT_LOG(RT_TAG_OS) << "OSGetSoundMode: reporting STEREO (no SRAM to read)" << std::endl;
    }
    return kOsSoundModeStereo;
}

// OSGetSoundMode, 0x8018033C, OSRtc.o
PPC_NATIVE_OVERRIDE(8018033c, OSGetSoundMode_ffcc, uint32_t, (), ());


// The sound driver keeps its own mode, and it is NOT derived from OSGetSoundMode. Measured live with
// a debugger breakpoint on SetVoiceVolumeMix: pan arrives as 0x40, which is REDSOUND_PAN_BYTE_CENTER,
// and that value is produced by exactly one line (RedExecute.cpp):
//
//     if (RedSoundPlayModeGet() == REDSOUND_SOUND_MODE_MONO) {
//         panPosition = REDSOUND_PAN_BYTE_CENTER;
//     } else if (voice->m_voiceSwitch & REDSOUND_VOICE_SWITCH_PAIRED_PAN) {
//         panPosition = PAIRED_LEFT ? 0 : REDSOUND_PAN_BYTE_MASK;   // the stereo image lives here
//
// so the driver is in mono and the hard-left/hard-right pairing that gives a stereo music stream its
// width is skipped entirely. In this driver STEREO is 0 and MONO is 1, the inverse of the OS
// convention. Hold both globals at STEREO; with no console menu to read, stereo is the right default.
namespace {
constexpr uint32_t kRedSoundMode     = 0x8032F3C8u;  // m_SoundMode,     RedDriver.o
constexpr uint32_t kRedSoundPlayMode = 0x8032F400u;  // m_SoundPlayMode, RedDriver.o
constexpr uint32_t kRedStereo = 0u;                  // REDSOUND_SOUND_MODE_STEREO
} // namespace

namespace FfccSoundMode {
void ForceStereoPlayMode()
{
    uint32_t mode = 0;
    if (!::Memory::TryRead32(kRedSoundPlayMode, mode)) {
        return;
    }
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        uint32_t raw = 0;
        ::Memory::TryRead32(kRedSoundMode, raw);
        RT_LOG(RT_TAG_AUDIO) << "RedSound mode observed: m_SoundMode=" << raw
                             << " m_SoundPlayMode=" << mode
                             << " (0=stereo 1=mono); forcing stereo" << std::endl;
    }
    if (mode != kRedStereo) {
        ::Memory::TryWrite32(kRedSoundPlayMode, kRedStereo);
    }
    uint32_t sm = 0;
    if (::Memory::TryRead32(kRedSoundMode, sm) && sm != kRedStereo) {
        ::Memory::TryWrite32(kRedSoundMode, kRedStereo);
    }
}
} // namespace FfccSoundMode

#endif // RECOMP_PROJECT_FFCC
