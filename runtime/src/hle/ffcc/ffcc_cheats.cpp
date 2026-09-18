// FFCC port: test cheats, off unless WIICOMPILED_CHEATS names them.
//
//   god    - players take no damage. The game's own MUTEKI: CGCharaObj::addHp
//            (FFCC-Decomp src/charaobj.cpp:1964) returns before touching HP for a party
//            target when DbgMenuPcs.GetDbgFlagsRaw() & 4. The retail debug menu that toggles
//            it (p_dbgmenu.cpp) needs a debug pad, so the bit is held set from here instead.
//   dmgN   - non-party targets take N times the damage: a pre-hook on addHp multiplies a
//            negative delta (r4) and then runs the translated body, so flinch, death and
//            item logic stay the game's own.
//
// Facts used: DbgMenuPcs at 0x80306708, m_dbgFlags at +0x04 (include/ffcc/p_dbgmenu.h:87);
// addHp at 0x8010F5BC with r3 = this, r4 = delta, r5 = source; CGPartyObj vtable 0x802119E4
// (a party object's first word). Monster subclasses have their own vtables, so "not party" is
// the test, which also covers breakables; fine for testing.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "runtime_log.h"
#include "memory.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#if defined(RECOMP_PROJECT_FFCC)

extern "C" void func_8010F5BC(CpuContext* ctx);   // CGCharaObj::addHp, translated body

namespace {
constexpr uint32_t kDbgMenuPcs = 0x80306708u;
constexpr uint32_t kDbgFlagsOffset = 0x04u;
constexpr uint32_t kDbgFlagMuteki = 0x4u;
constexpr uint32_t kPartyObjVtable = 0x802119E4u;

struct Cheats {
    bool parsed = false;
    bool god = false;
    int damageMul = 1;
};
Cheats g_cheats;

void Parse() {
    if (g_cheats.parsed) return;
    g_cheats.parsed = true;
    const char* env = std::getenv("WIICOMPILED_CHEATS");
    if (env == nullptr || *env == 0) return;
    const char* p = env;
    while (*p) {
        while (*p == ',' || *p == ' ') ++p;
        const char* start = p;
        while (*p && *p != ',' && *p != ' ') ++p;
        const size_t n = size_t(p - start);
        if (n == 3 && std::strncmp(start, "god", 3) == 0) g_cheats.god = true;
        else if (n > 3 && std::strncmp(start, "dmg", 3) == 0) g_cheats.damageMul = std::atoi(start + 3);
    }
    if (g_cheats.damageMul < 1) g_cheats.damageMul = 1;
    RT_LOG(RT_TAG_OS) << "cheats: god=" << g_cheats.god << " damage x" << g_cheats.damageMul << std::endl;
}
} // namespace

namespace FfccCheats {
// Called once per presented frame from the VI loop (next to the stereo hold).
void Tick() {
    Parse();
    if (!g_cheats.god) return;
    uint32_t flags = 0;
    if (::Memory::TryRead32(kDbgMenuPcs + kDbgFlagsOffset, flags) && (flags & kDbgFlagMuteki) == 0) {
        ::Memory::TryWrite32(kDbgMenuPcs + kDbgFlagsOffset, flags | kDbgFlagMuteki);
    }
}
} // namespace FfccCheats

extern "C" void addHp_cheat_8010f5bc(CpuContext* ctx) {
    Parse();
    if (g_cheats.damageMul > 1) {
        const uint32_t self = ctx->gpr[3];
        const int32_t delta = int32_t(ctx->gpr[4]);
        uint32_t vt = 0;
        if (delta < 0 && self != 0 && ::Memory::TryRead32(self, vt) && vt != kPartyObjVtable) {
            ctx->gpr[4] = uint32_t(delta * g_cheats.damageMul);
        }
    }
    func_8010F5BC(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x8010F5BC, addHp_cheat_8010f5bc, "addHp_cheat_8010f5bc");

#endif // RECOMP_PROJECT_FFCC
