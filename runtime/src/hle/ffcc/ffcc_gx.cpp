// FFCC port: GX entry points whose Mario Kart wrappers bake the Wii GXData layout, Wii signatures
// or Mario Kart static addresses. Everything else in the GX HLE is shared (see ffcc_natives.cpp and
// GxOff in project_guest_addresses.h). Addresses: config/GCCP01/symbols.txt.
#include "../gx/gx_internal.h"
// GXCpu2Efb.h has no extern "C" guards of its own (the other dolphin headers do); aurora
// compiles GXPeekZ with C linkage, so declare it that way here or the link fails.
extern "C" {
#include <dolphin/gx/GXCpu2Efb.h>
}
#include <cstring>
#include <vector>

extern "C" GXFifoObj* GXInit(void* base, u32 size);
extern "C" void GX__InitFifoBase_8016c7c8(uint32_t fa, uint32_t ba, uint32_t s);
extern "C" void GX__SetCPUFifo_8016c94c(uint32_t fa);
extern "C" void GX__SetGPFifo_8016cb2c(uint32_t fa);
extern "C" void GX__GetCPUFifo_8016cf10(uint32_t fa);
extern "C" void GX__SetDirtyState_8016ee78();
extern "C" void GX__GetViewportv_801733e0(uint32_t oa);
extern "C" void GX__LoadTexObj_80170f2c(uint32_t oa, uint32_t tid);
extern "C" void OSWakeupThread_HLE_801aaaa4(CpuContext* ctx);
extern "C" uint32_t OS__GetCurrentThread_801a98b0_hle();

namespace {
// GXInit.c / GXFifo.c statics
constexpr uint32_t kFifoObj = 0x80327C78u;          // FifoObj (returned by GXInit)
constexpr uint32_t kCpuFifoPtr = 0x8032F310u;       // CPUFifo
constexpr uint32_t kGpFifoPtr = 0x8032F314u;        // GPFifo
constexpr uint32_t kCurrentThread = 0x8032F318u;    // __GXCurrentThread
constexpr uint32_t kCpGpLinked = 0x8032F31Cu;       // CPGPLinked
constexpr uint32_t kOverflowSuspend = 0x8032F320u;  // GXOverflowSuspendInProgress
constexpr uint32_t kOverflowCount = 0x8032F328u;    // __GXOverflowCount
constexpr uint32_t kDrawDoneCb = 0x8032F334u;       // DrawDoneCB
constexpr uint32_t kDrawDone = 0x8032F338u;         // DrawDone
constexpr uint32_t kFinishQueue = 0x8032F33Cu;      // FinishQueue (OSThreadQueue)
constexpr uint32_t kOldCpuFifoPtr = 0x8032F348u;    // OldCPUFifo (GXFifoObj*)
constexpr uint32_t kPiReg = 0x8032F2E8u, kCpReg = 0x8032F2ECu, kPeReg = 0x8032F2F0u, kMemReg = 0x8032F2F4u;
constexpr uint32_t kDefaultTexRegionCb = 0x8019EF44u, kDefaultTlutRegionCb = 0x8019EFC0u;
constexpr uint32_t kCpInterruptHandler = 0x801A02C8u, kTokenHandler = 0x801A2254u, kFinishHandler = 0x801A2320u;
constexpr uint32_t kInitGX = 0x8019FA04u;           // __GXInitGX (translated; GX API calls only)
constexpr uint32_t kTexRegionCallbackOff = 0x410u, kTlutRegionCallbackOff = 0x414u;
std::vector<uint8_t> g_gxShadow;                    // GXData copy for dlSaveContext (host side)

uint32_t GxData() { return Memory::Read32(kGXDataPtrAddr); }
}  // namespace

extern "C" void GX__SetCPUFifo_ffcc(uint32_t fa) {
    Memory::Write32(kCpuFifoPtr, fa);   // read back by the translated GXGetCPUFifo
    GX__SetCPUFifo_8016c94c(fa);
}
extern "C" void GX__SetGPFifo_ffcc(uint32_t fa) {
    Memory::Write32(kGpFifoPtr, fa);
    GX__SetGPFifo_8016cb2c(fa);
}

// GXInit(base, size): host GX init, GameCube GXData defaults, the __GXFifoInit / __GXPEInit
// bookkeeping the SDK does through CP/PE registers, then the translated __GXInitGX for the API
// defaults. Raw context because __GXInitGX is a guest call.
extern "C" void GX__Init_ffcc(CpuContext* ctx) {
    const uint32_t base = ctx->gpr[3];
    const uint32_t size = ctx->gpr[4];
    GXInit(GuestToHostPtr(base, size), size);
    const uint32_t gd = GxData();
    for (uint32_t off = 0; off < GxOff::Size; off += 4) Memory::Write32(gd + off, 0);
    Memory::Write8(gd + GxOff::InDispList, 0);
    Memory::Write8(gd + GxOff::DlSaveContext, 1);
    Memory::Write8(gd + GxOff::AbtWaitPECopy, 1);
    Memory::Write32(gd + kTexRegionCallbackOff, kDefaultTexRegionCb);
    Memory::Write32(gd + kTlutRegionCallbackOff, kDefaultTlutRegionCb);
    Memory::Write32(kPiReg, 0xCC003000u);
    Memory::Write32(kCpReg, 0xCC000000u);
    Memory::Write32(kPeReg, 0xCC001000u);
    Memory::Write32(kMemReg, 0xCC004000u);
    // __GXFifoInit
    __OSSetInterruptHandler_801a65f8_hle(0x11u, kCpInterruptHandler);
    __OSUnmaskInterrupts_801a69bc_hle(0x4000u);
    Memory::Write32(kCurrentThread, OS__GetCurrentThread_801a98b0_hle());
    Memory::Write32(kCpuFifoPtr, 0);
    Memory::Write32(kGpFifoPtr, 0);
    Memory::Write8(kCpGpLinked, 0);
    Memory::Write32(kOverflowSuspend, 0);
    Memory::Write32(kOverflowCount, 0);
    GX__InitFifoBase_8016c7c8(kFifoObj, base, size);
    GX__SetCPUFifo_ffcc(kFifoObj);
    GX__SetGPFifo_ffcc(kFifoObj);
    // __GXPEInit
    __OSSetInterruptHandler_801a65f8_hle(0x12u, kTokenHandler);
    __OSSetInterruptHandler_801a65f8_hle(0x13u, kFinishHandler);
    __OSUnmaskInterrupts_801a69bc_hle(0x3000u);
    Memory::Write32(kFinishQueue, 0);
    Memory::Write32(kFinishQueue + 4, 0);
    // register defaults from GXInit (GameCube SDK)
    Memory::Write32(gd + GxOff::GenMode, 0);
    Memory::Write32(gd + GxOff::BpMask, 0x0F0000FFu);
    Memory::Write32(gd + GxOff::LpSize, 0x22000000u);
    Memory::Write32(gd + GxOff::Iref, 0x27000000u);
    for (uint32_t i = 0; i < 8; ++i) {
        Memory::Write32(gd + GxOff::SuTs0 + i * 4, (0x30u + 2 * i) << 24);
        Memory::Write32(gd + GxOff::SuTs1 + i * 4, (0x31u + 2 * i) << 24);
    }
    Memory::Write32(gd + GxOff::SuScis0, 0x20u << 24);
    Memory::Write32(gd + GxOff::SuScis1, 0x21u << 24);
    BeginDisplayListRecording(0, 0);
    RT_LOGF(RT_TAG_GX, "GX initialized (FFCC), FifoObj 0x%08X base=0x%08X size=0x%08X gxData=0x%08X" "\n", kFifoObj, base, size, gd);
    InvokeIndirectCpu(kInitGX, ctx);
    ctx->gpr[3] = kFifoObj;
}

// GXLoadTexObjPreLoaded(obj, region, id): the region is host-managed.
extern "C" void GX__LoadTexObjPreLoaded_ffcc(uint32_t oa, uint32_t region, uint32_t tid) {
    (void)region;
    GX__LoadTexObj_80170f2c(oa, tid);
}

// GXFinishInterruptHandler: DrawDone = TRUE; OSWakeupThread(&FinishQueue); DrawDoneCB().
extern "C" void GX__FinishInterruptHandler_ffcc(CpuContext* ctx) {
    Memory::Write8(kDrawDone, 1);
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    cpu->gpr[3] = kFinishQueue;
    OSWakeupThread_HLE_801aaaa4(cpu);
    const uint32_t cb = Memory::Read32(kDrawDoneCb);
    if (cb != 0) InvokeIndirectCpu(cb, cpu);
}

// GXSetDrawDone(): the FIFO decoder does not raise the PE finish interrupt, so the finish completes
// here synchronously (host GXDrawDone flushes and waits), then the FFCC finish handler runs. The
// translated GXWaitDrawDone then sees DrawDone set and returns without sleeping on FinishQueue.
extern "C" void GX__SetDrawDone_ffcc(CpuContext* ctx) {
    Memory::Write8(kDrawDone, 0);
    GXDrawDone();
    GX__FinishInterruptHandler_ffcc(ctx);
}

// GXBeginDisplayList(list, size) / GXEndDisplayList(): GameCube semantics (OldCPUFifo is a pointer,
// GXData shadow kept host side), same recording model as the Mario Kart wrappers.
extern "C" void GX__BeginDisplayList_ffcc(uint32_t la, uint32_t s) {
    const uint32_t gd = GxData();
    if (!gd) return;
    if (Memory::Read32(gd + GxOff::DirtyState)) GX__SetDirtyState_8016ee78();
    if (Memory::Read8(gd + GxOff::DlSaveContext)) {
        const uint8_t* src = static_cast<const uint8_t*>(Memory::GetPointer(gd, GxOff::Size));
        g_gxShadow.assign(src, src + GxOff::Size);
    }
    Memory::Write32(kOldCpuFifoPtr, Memory::Read32(kCpuFifoPtr));
    Memory::Write32(kDlFifoAddr + 0x00, la);          // base
    Memory::Write32(kDlFifoAddr + 0x04, la + s - 4u); // end
    Memory::Write32(kDlFifoAddr + 0x08, s);           // size
    Memory::Write32(kDlFifoAddr + 0x14, la);          // rdPtr
    Memory::Write32(kDlFifoAddr + 0x18, la);          // wrPtr
    Memory::Write32(kDlFifoAddr + 0x1C, 0);           // count
    Memory::Write8(kDlFifoAddr + kDlWrapFlagOffset, 0);
    Memory::Write8(gd + GxOff::InDispList, 1u);
    BeginDisplayListRecording(la, s);
    GXFlush();
    GX__SetCPUFifo_ffcc(kDlFifoAddr);
}

extern "C" uint32_t GX__EndDisplayList_ffcc() {
    GXFlush();
    GX__GetCPUFifo_8016cf10(kDlFifoAddr);   // host cursor/count back into DisplayListFifo
    const uint8_t wrapped = Memory::Read8(kDlFifoAddr + kDlWrapFlagOffset);
    GX__SetCPUFifo_ffcc(Memory::Read32(kOldCpuFifoPtr));
    const uint32_t gd = GxData();
    if (gd) {
        if (Memory::Read8(gd + GxOff::DlSaveContext) != 0 && g_gxShadow.size() == GxOff::Size) {
            const int32_t level = OS__DisableInterrupts_801a65ac();
            const uint32_t savedCpEnable = Memory::Read32(gd + GxOff::CpEnable);
            std::memcpy(Memory::GetPointer(gd, GxOff::Size), g_gxShadow.data(), GxOff::Size);
            Memory::Write32(gd + GxOff::CpEnable, savedCpEnable);
            OS__RestoreInterrupts_801a65d4(level);
        }
        Memory::Write8(gd + GxOff::InDispList, 0);
    }
    EndDisplayListRecording();
    return wrapped == 0 ? Memory::Read32(kDlCountAddr) : 0;
}

PPC_NATIVE_OVERRIDE_VOID(8019f174, GX__Init_ffcc, (CpuContext* ctx), (ctx));                                   // GXInit
PPC_NATIVE_OVERRIDE_VOID(801a04e4, GX__SetCPUFifo_ffcc, (uint32_t fa), (fa));                                  // GXSetCPUFifo
PPC_NATIVE_OVERRIDE_VOID(801a05f4, GX__SetGPFifo_ffcc, (uint32_t fa), (fa));                                   // GXSetGPFifo
PPC_NATIVE_OVERRIDE_VOID(801a4228, GX__LoadTexObjPreLoaded_ffcc, (uint32_t oa, uint32_t region, uint32_t tid), (oa, region, tid));  // GXLoadTexObjPreLoaded
PPC_NATIVE_OVERRIDE_VOID(801a2320, GX__FinishInterruptHandler_ffcc, (CpuContext* ctx), (ctx));                 // GXFinishInterruptHandler
PPC_NATIVE_OVERRIDE_VOID(801a1fa0, GX__SetDrawDone_ffcc, (CpuContext* ctx), (ctx));                             // GXSetDrawDone
PPC_NATIVE_OVERRIDE_VOID(801a5ff8, GX__BeginDisplayList_ffcc, (uint32_t la, uint32_t s), (la, s));             // GXBeginDisplayList
PPC_NATIVE_OVERRIDE(801a60c0, GX__EndDisplayList_ffcc, uint32_t, (), ());                                     // GXEndDisplayList
PPC_NATIVE_OVERRIDE_VOID(801a6800, GX__GetViewportv_801733e0, (uint32_t oa), (oa));                            // GXGetViewportv

// GXPeekZ: the SDK body reads the depth buffer through the GameCube EFB aperture at 0xC8000000,
// which this runtime guards as "EFB Logical"; the read faulted, was zero-filled and logged as an
// unmapped guest touch (four per frame from pppFrameLensFlare+0x9c, the lens-flare occlusion
// test: it samples Z under the sun and counts how many samples are farther than the flare).
// With zeros every sample reads "occluded" and the flare never draws. Aurora keeps a readback of
// the depth attachment (gfx/depth_peek.cpp); forward to it and write the 24-bit Z back to the
// guest pointer. Only GXPeekZ is linked into FFCC (GXPeekARGB/GXPoke* are UNUSED in the MAP).
extern "C" void GX__PeekZ_ffcc(uint32_t x, uint32_t y, uint32_t za)
{
    u32 z = 0;
    GXPeekZ(static_cast<u16>(x), static_cast<u16>(y), &z);
    if (za != 0) {
        Memory::Write32(za, z);
    }
}
PPC_NATIVE_OVERRIDE_VOID(801a21e8, GX__PeekZ_ffcc, (uint32_t x, uint32_t y, uint32_t za), (x, y, za));  // GXPeekZ
// __GXSetDirtyState: the HLE owns VAT/VCD state; the translated body re-sent the guest copies through
// the FIFO. Own wrapper name: the translator native index keys registrations by symbol.
extern "C" void GX__SetDirtyState_ffcc() { GX__SetDirtyState_8016ee78(); }
PPC_NATIVE_OVERRIDE_VOID(801a2424, GX__SetDirtyState_ffcc, (), ());
// __GXSetVAT / __GXSetVCD: leaf senders of the guest-side VAT/VCD copies (called by __GXSetDirtyState);
// the HLE owns that state, so they are no-ops.
extern "C" void GX__SetVAT_ffcc() {}
extern "C" void GX__SetVCD_ffcc() {}
PPC_NATIVE_OVERRIDE_VOID(801a1838, GX__SetVAT_ffcc, (), ());
PPC_NATIVE_OVERRIDE_VOID(801a0fb8, GX__SetVCD_ffcc, (), ());
