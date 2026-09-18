// FFCC port, Phase 1 spike: guest helpers that Mario Kart-specific HLE code calls by Mario Kart
// address. Where FFCC has the same SDK function it is forwarded to the FFCC translation (same
// register contract); the rest report and abort so any path that reaches them is found at runtime.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void MissingHelper(const char* mkwName) {
    std::fprintf(stderr, "[ffcc] Mario Kart guest helper %s called; not part of FFCC\n", mkwName);
    std::abort();
}
void Forward(uint32_t ffccAddress, const char* name, CpuContext* ctx) {
    if (!TranslatedFunctionRegistry::FindByAddressPtr(ffccAddress)) {
        std::fprintf(stderr, "[ffcc] %s (0x%08X) is not in the translation; cannot forward\n", name, ffccAddress);
        std::abort();
    }
    InvokeIndirectCpu(ffccAddress, ctx);
}
}  // namespace

// os_scheduler.cpp SelectThread: r3 = context, returns nonzero when resumed -> OSSaveContext.
extern "C" void func_801A1ED8(CpuContext* ctx) { Forward(0x8017D7B4u, "OSSaveContext", ctx); }
// os_alarm.cpp ProcessAlarmQueue: r3 = alarm, r5:r6 = fire time, r7 = handler -> InsertAlarm (static).
extern "C" void func_801A0620(CpuContext* ctx) { Forward(0x8017C1DCu, "InsertAlarm", ctx); }
// os_alarm.cpp OSSetPeriodicAlarm: r3:r4 = start -> fire time. The Wii SDK converts through
// __OSTimeToSystemTime; the GameCube SDK in FFCC stores the start time as is.
extern "C" void func_801AADE0(CpuContext* ctx) { (void)ctx; }
// os_init.cpp OSInitAlarm late-init hook: forward to FFCC OSInitAlarm.
extern "C" void func_801A961C(CpuContext* ctx) { Forward(0x8017C180u, "OSInitAlarm", ctx); }
// Mario Kart only: AXFX reverb callback, network, StaticR prolog.
extern "C" void func_8012B830(CpuContext*) { MissingHelper("func_8012B830"); }
extern "C" void func_801D8D30(CpuContext*) { MissingHelper("func_801D8D30"); }
extern "C" void func_801D9E94(CpuContext*) { MissingHelper("func_801D9E94"); }
extern "C" void func_8055531C(CpuContext*) { MissingHelper("func_8055531C"); }
