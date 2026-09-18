// FFCC port, Phase 1 spike: GameCube ARAM (auxiliary RAM) and ARQ (ARAM DMA request queue) HLE.
// The Wii runtime has no ARAM. FFCC (RedSound) uses ARInit, the AR DMA API and ARQPostRequest.
// Model: a 16 MB host buffer; DMA transfers complete immediately; callbacks run synchronously
// on the persistent CPU context, matching the SDK contract (ar.c / arq.c in the decomp).
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "memory.h"
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
constexpr uint32_t kAramSize = 0x01000000u;   // 16 MB
constexpr uint32_t kAramStackBase = 0x4000u;  // first 16 KB reserved by the SDK
}  // namespace
// Exported for the GameCube AX mixer path (ax_gamecube.cpp).
std::vector<uint8_t>& FfccAramBuffer() { static std::vector<uint8_t> aram(kAramSize, 0); return aram; }
namespace {
std::vector<uint8_t>& Aram() { return FfccAramBuffer(); }
bool g_arInit = false;
bool g_arqInit = false;
uint32_t g_arDmaCallback = 0;
uint32_t g_arqChunkSize = 0x1000;

void InvokeGuest(uint32_t callback, uint32_t arg) {
    if (callback == 0 || !TranslatedFunctionRegistry::FindByAddressPtr(callback)) return;
    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = arg;
    InvokeIndirectCpu(callback, &cpu);
}

// type 0: main memory -> ARAM, type 1: ARAM -> main memory (SDK AR_MRAM_TO_ARAM / AR_ARAM_TO_MRAM)
void AramDma(uint32_t type, uint32_t mainAddr, uint32_t aramAddr, uint32_t length) {
    auto& aram = Aram();
    if (aramAddr >= kAramSize || length > kAramSize - aramAddr) {
        std::fprintf(stderr, "[ffcc][aram] DMA out of range: aram=0x%08X len=0x%X\n", aramAddr, length);
        return;
    }
    if (type == 0) {
        for (uint32_t i = 0; i < length; ++i) aram[aramAddr + i] = Memory::Read8(mainAddr + i);
    } else {
        for (uint32_t i = 0; i < length; ++i) Memory::Write8(mainAddr + i, aram[aramAddr + i]);
    }
}
}  // namespace

// --- ar.c -------------------------------------------------------------------
extern "C" uint32_t ARInit_ffcc(uint32_t stackIndexAddr, uint32_t numEntries) {
    (void)stackIndexAddr; (void)numEntries;
    g_arInit = true;
    return kAramStackBase;
}
extern "C" uint32_t ARCheckInit_ffcc() { return g_arInit ? 1u : 0u; }
extern "C" uint32_t ARRegisterDMACallback_ffcc(uint32_t callback) {
    const uint32_t previous = g_arDmaCallback; g_arDmaCallback = callback; return previous;
}
extern "C" uint32_t ARGetDMAStatus_ffcc() { return 0; }  // never busy: transfers complete immediately
extern "C" void ARStartDMA_ffcc(uint32_t type, uint32_t mainAddr, uint32_t aramAddr, uint32_t length) {
    AramDma(type, mainAddr, aramAddr, length);
    InvokeGuest(g_arDmaCallback, 0);
}
extern "C" void __ARHandler_ffcc(uint32_t exception, uint32_t context) { (void)exception; (void)context; }
extern "C" void __ARClearInterrupt_ffcc() {}
extern "C" uint32_t __ARGetInterruptStatus_ffcc() { return 0; }
extern "C" void __ARChecksize_ffcc() {}

// --- arq.c ------------------------------------------------------------------
// ARQRequest: next +0, owner +4, type +8, priority +C, source +10, dest +14, length +18, callback +1C
extern "C" void ARQInit_ffcc() { g_arqInit = true; }
extern "C" void ARQSetChunkSize_ffcc(uint32_t size) { g_arqChunkSize = size; }
extern "C" void ARQPostRequest_ffcc(uint32_t request, uint32_t owner, uint32_t type, uint32_t priority,
                                    uint32_t source, uint32_t dest, uint32_t length, uint32_t callback) {
    if (request) {
        Memory::Write32(request + 0x00, 0);
        Memory::Write32(request + 0x04, owner);
        Memory::Write32(request + 0x08, type);
        Memory::Write32(request + 0x0C, priority);
        Memory::Write32(request + 0x10, source);
        Memory::Write32(request + 0x14, dest);
        Memory::Write32(request + 0x18, length);
        Memory::Write32(request + 0x1C, callback);
    }
    // ARQ_TYPE_MRAM_TO_ARAM = 0 (source = main memory, dest = ARAM); ARQ_TYPE_ARAM_TO_MRAM = 1
    if (type == 0) AramDma(0, source, dest, length);
    else           AramDma(1, dest, source, length);
    InvokeGuest(callback, request);
}
extern "C" void __ARQServiceQueueLo_ffcc() {}
extern "C" void __ARQCallbackHack_ffcc(uint32_t unused) { (void)unused; }
extern "C" void __ARQInterruptServiceRoutine_ffcc() {}

// Registrations keyed by FFCC (GCCP01) addresses from config/GCCP01/symbols.txt
PPC_NATIVE_OVERRIDE(8018f888, ARInit_ffcc, uint32_t, (uint32_t a, uint32_t b), (a, b));
PPC_NATIVE_OVERRIDE(8018f880, ARCheckInit_ffcc, uint32_t, (), ());
PPC_NATIVE_OVERRIDE(8018f710, ARRegisterDMACallback_ffcc, uint32_t, (uint32_t cb), (cb));
PPC_NATIVE_OVERRIDE(8018f754, ARGetDMAStatus_ffcc, uint32_t, (), ());
PPC_NATIVE_OVERRIDE_VOID(8018f790, ARStartDMA_ffcc, (uint32_t t, uint32_t m, uint32_t a, uint32_t l), (t, m, a, l));
PPC_NATIVE_OVERRIDE_VOID(8018f94c, __ARHandler_ffcc, (uint32_t e, uint32_t c), (e, c));
PPC_NATIVE_OVERRIDE_VOID(8018f9c4, __ARClearInterrupt_ffcc, (), ());
PPC_NATIVE_OVERRIDE(8018f9e4, __ARGetInterruptStatus_ffcc, uint32_t, (), ());
PPC_NATIVE_OVERRIDE_VOID(8018f9f4, __ARChecksize_ffcc, (), ());
PPC_NATIVE_OVERRIDE_VOID(801913b8, ARQInit_ffcc, (), ());
PPC_NATIVE_OVERRIDE_VOID(80191584, ARQSetChunkSize_ffcc, (uint32_t s), (s));
PPC_NATIVE_OVERRIDE_VOID(80191428, ARQPostRequest_ffcc, (uint32_t r, uint32_t o, uint32_t t, uint32_t p, uint32_t s, uint32_t d, uint32_t l, uint32_t c), (r, o, t, p, s, d, l, c));
PPC_NATIVE_OVERRIDE_VOID(801911e8, __ARQServiceQueueLo_ffcc, (), ());
PPC_NATIVE_OVERRIDE_VOID(801912e8, __ARQCallbackHack_ffcc, (uint32_t u), (u));
PPC_NATIVE_OVERRIDE_VOID(801912ec, __ARQInterruptServiceRoutine_ffcc, (), ());
