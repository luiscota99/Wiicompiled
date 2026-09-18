// Serial interface device probing for the FFCC port.
//
// Our runtime skips the SI MMIO register setup entirely (see the boot log line from SIInit), so the
// retail SIProbe reads hardware that is not there and never reports a controller. FFCC's JoyBus
// thread calls SIProbe per port and only accepts four device identifiers:
//
//     if (padType == 0x00040000 || padType == 0x09000000 ||
//         padType == 0x8B100000 || padType == 0x88000000)
//         threadParam->m_padType = padType;
//     else if (padType != 0x80)
//         threadParam->m_padType = 0x40;          // nothing plugged in
//
// Anything else leaves the port marked empty, so the game believes no controller exists. Menus that
// assign a player to a slot then have nobody to assign, which is how the character select ends up
// drawing no cursor and ignoring every button.
//
// Report a standard GameCube controller on port 0 and nothing on the rest. The value is
// SI_GC_CONTROLLER = SI_TYPE_DOLPHIN | SI_GC_STANDARD = 0x08000000 | 0x01000000, from
// FFCC-Decomp/include/dolphin/si.h.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "runtime_log.h"

#include <cstdint>

#if defined(RECOMP_PROJECT_FFCC)

namespace {
constexpr uint32_t kSiGcController = 0x09000000u;  // SI_TYPE_DOLPHIN | SI_GC_STANDARD
constexpr uint32_t kSiNoDevice = 0u;
constexpr uint32_t kJoyBusPortEmpty = 0x40u;  // what JoyBus itself stores for an empty port               // not 0x80, so JoyBus marks the port empty
} // namespace

extern "C" bool FfccGbaPortHasController(uint32_t chan);   // aurora pad.cpp
constexpr uint32_t kSiGba = 0x00040000u;      // SI_GBA: what joybus.cpp:640 accepts for a GBA

extern "C" uint32_t SIProbe_ffcc(uint32_t chan)
{
    // Port 0 is the GameCube pad; ports 1-3 report a GBA when a controller (or the
    // WIICOMPILED_FAKE_GBA test mirror) is there, so the JoyBus thread boots our fake GBA.
    // Multiplayer needs every player on a GBA and no pad: WIICOMPILED_FAKE_GBA=0,1 turns port 0
    // into a GBA as well (driven by the same PC pad).
    const uint32_t type = FfccGbaPortHasController(chan) ? kSiGba
                        : (chan == 0u) ? kSiGcController : kSiNoDevice;
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        RT_LOG(RT_TAG_OS) << "SIProbe: reporting a standard GameCube controller on port 0" << std::endl;
    }
    return type;
}

// SIProbe, 0x8018588C, SIBios.o
PPC_NATIVE_OVERRIDE(8018588c, SIProbe_ffcc, uint32_t, (uint32_t a0), (a0));


// JoyBus::GetPadType(int player), 0x800A6AC4.
//
// The accessor just returns m_threadParams[player].m_padType, which the JoyBus thread fills from
// SIProbe. Overriding SIProbe alone only helps if that thread actually runs its probe state machine
// on our runtime, and the character select screen discards ALL input unless this returns a
// recognised controller id:
//
//     entry.m_padType = Joybus.GetPadType(0);
//     if (entry.m_padType == 0x09000000 || ...) entry.m_connected = 1;
//     if (entry.m_connected == 1 && ...) { read buttons } else { down = repeat = 0; }
//
// (CMenuPcs::CalcGoOutCharaSelect, src/wm_menu.cpp). Answer it directly so the port-0 controller is
// visible regardless of the probe thread's state. Member function: r3 is this, r4 is the player.
extern "C" uint32_t GetPadType_ffcc(uint32_t thisPtr, uint32_t player)
{
    (void)thisPtr;
    if (player < 4u && FfccGbaPortHasController(player)) return kSiGba;
    return (player == 0u) ? kSiGcController : kJoyBusPortEmpty;
}

PPC_NATIVE_OVERRIDE(800a6ac4, GetPadType_ffcc, uint32_t, (uint32_t a0, uint32_t a1), (a0, a1));

#endif // RECOMP_PROJECT_FFCC
