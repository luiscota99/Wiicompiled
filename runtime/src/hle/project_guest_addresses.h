// Per-project guest addresses used by the HLE. The runtime was written against Mario Kart Wii
// (RMCP01); every constant here names an SDK global or callback that exists under the same
// name in other games at a different address. Build with -DRECOMP_PROJECT_FFCC=1 for
// Final Fantasy Crystal Chronicles (GCCP01; addresses from the decomp symbols.txt and the
// linker map). Default: Mario Kart Wii.
#pragma once
#include <cstdint>

#if defined(RECOMP_PROJECT_FFCC) && !defined(FFCC_DEBUG_LOGS)
#define FFCC_DEBUG_LOGS 0
#endif

namespace GuestAddr {
#if defined(RECOMP_PROJECT_FFCC)
// --- AI (ai.c) ---
inline constexpr uint32_t AIInitialized         = 0x8032F170u;  // __AI_init_flag
inline constexpr uint32_t AICallbackBusy        = 0x8032F174u;  // __AID_Active
inline constexpr uint32_t AICallbackStackSwitch = 0x8032F168u;  // __CallbackStack
inline constexpr uint32_t AIDmaCallback         = 0x8032F164u;  // __AID_Callback
// --- DSP (dsp.c) ---
inline constexpr uint32_t DspInitialized        = 0x8032F2C0u;  // __DSP_init_flag
inline constexpr uint32_t DspAssertPending      = 0x8032F2C8u;  // __DSP_rude_task_pending
inline constexpr uint32_t DspAssertTask         = 0x8032F2CCu;  // __DSP_rude_task
inline constexpr uint32_t DspTmpTask            = 0x8032F2D0u;  // __DSP_tmp_task
inline constexpr uint32_t DspCurrentTask        = 0x8032F2DCu;  // __DSP_curr_task
inline constexpr uint32_t DspFirstTask          = 0x8032F2D8u;  // __DSP_first_task
inline constexpr uint32_t DspRunningTask        = 0x8032F2D4u;  // __DSP_last_task
// --- AX (AXOut.c / DSPCode.c) ---
inline constexpr uint32_t AxDspTask             = 0x80310860u;  // __AXDSPTask (DSPTaskInfo)
inline constexpr uint32_t AxIramMmem            = 0x80219840u;  // axDspSlave (IRAM ucode image)
inline constexpr uint32_t AxDramMmem            = 0x803108B0u;  // __AXDramImage
inline constexpr uint32_t AxInitCallback        = 0x801928A4u;  // __AXDSPInitCallback
inline constexpr uint32_t AxResumeCallback      = 0x801928B0u;  // __AXDSPResumeCallback
inline constexpr uint32_t AxDoneCallback        = 0x80192908u;  // __AXDSPDoneCallback
inline constexpr uint32_t AxRequestCallback     = 0u;           // this SDK sets req_cb = NULL
inline constexpr uint32_t OneConstant           = 0u;           // FFCC build uses host literals (ax_effects.cpp)
inline constexpr uint32_t ScaleConstant         = 0u;
// --- OS threads / scheduler / interrupts (OSThread.c, OSInterrupt.c) ---
inline constexpr uint32_t InterruptHandlerTablePtr = 0x8032EF80u; // InterruptHandlerTable
inline constexpr uint32_t DefaultThreadContext  = 0x8030C0B0u;  // DefaultThread (OSThread, 0x318 bytes)
inline constexpr uint32_t IdleThreadContext     = 0x8030BD98u;  // IdleThread
inline constexpr uint32_t ThreadQueueArray      = 0x8030BC98u;  // RunQueue[32]
inline constexpr uint32_t SwitchThreadCallbackPtr = 0x8032EAB0u; // SwitchThreadCallback
inline constexpr uint32_t SchedulerReschedCounter = 0x8032EFD4u; // RunQueueHint (used as a 0/1 flag)
inline constexpr uint32_t SchedulerPendingFlag  = 0x8032EFD0u;  // RunQueueBits
inline constexpr uint32_t SchedulerIdleFlag     = 0x8032EFD8u;  // Reschedule (used as a disable count)
// --- VI (vi.c) ---
inline constexpr uint32_t ViInitializedFlag     = 0x8032F0E0u;  // IsInitialized
inline constexpr uint32_t ViTvFormat            = 0x8032F124u;  // CurrTvMode (read by the translated VIGetTvFormat)
inline constexpr uint32_t ViRenderWidth         = 0x8030CC3Cu;  // HorVer.DispSizeX
inline constexpr uint32_t ViRenderHeight        = 0x8030CC3Eu;  // HorVer.DispSizeY
inline constexpr uint32_t ViXfbWidth            = 0x8030CC4Au;  // HorVer.FBSizeX
inline constexpr uint32_t ViXfbHeight           = 0x8030CC4Cu;  // HorVer.FBSizeY
inline constexpr uint32_t ViRetraceCount        = 0x8032F0E4u;  // retraceCount
inline constexpr uint32_t ViTimingGuard         = 0u;           // no counterpart in this SDK
inline constexpr uint32_t ViPreRetraceCallback  = 0x8032F0F4u;  // PreCB
inline constexpr uint32_t ViPostRetraceCallback = 0x8032F0F8u;  // PostCB
inline constexpr uint32_t ViNextFrameBuffer     = 0x8032F128u;  // NextBufAddr
inline constexpr uint32_t ViNextFrameBufferHw   = 0x8030CC64u;  // HorVer.bufAddr
inline constexpr uint32_t ViRetraceQueue        = 0x8032F0ECu;  // retraceQueue (OSThreadQueue)
inline constexpr uint32_t EggSSystem            = 0u;           // no EGG in FFCC: post-retrace gate disabled
inline constexpr uint32_t ViDefaultTvFormat     = 1u;           // PAL console: VIInit reads VI_PAL from the IPL setup
// --- GX (gx.c, GXDisplayList.c, GXFifo.c) ---
inline constexpr uint32_t GxDataPtr             = 0x80333828u;  // gx (GXData* const, .sdata2) -> gxData 0x80327780
inline constexpr uint32_t GxDlFifo              = 0x80327CF8u;  // DisplayListFifo (GXFifoObj)
inline constexpr uint32_t GxDlWritePtr          = 0x80327D0Cu;  // DisplayListFifo + 0x14
inline constexpr uint32_t GxDlCount             = 0x80327D14u;  // DisplayListFifo + 0x1C
inline constexpr uint32_t GxDrawDoneFlag        = 0x8032F338u;  // DrawDone
// --- OS functions the thread HLE calls or plants as return addresses (OSThread.c, OSContext.c) ---
inline constexpr uint32_t OSInitContext         = 0x8017D938u;
inline constexpr uint32_t OSExitThread          = 0x801812C4u;
inline constexpr uint32_t OSUnlockAllMutex      = 0x8017F0ACu;  // __OSUnlockAllMutex
inline constexpr uint32_t SchedulerInitFlag     = 0u;           // Mario Kart-only slow-path init (0 = skip)
inline constexpr uint32_t ThreadAttrSource      = 0u;
inline constexpr uint32_t AlarmQueue            = 0x8032EF60u;  // AlarmQueue (OSAlarm.c static {head, tail})
inline constexpr uint32_t AlarmQueueOffsetFromR13 = 0x803363C0u - 0x8032EF60u;  // r13 (SDA1) - AlarmQueue = 0x7460
// --- DVD (dvd.c) ---
inline constexpr uint32_t DvdWaitingQueue       = 0x8030CAB8u;  // WaitingQueue[4]
inline constexpr uint32_t DvdCanceling          = 0x8032F0A0u;  // Canceling
inline constexpr uint32_t DvdResumeFromHere     = 0x8032F0A8u;  // ResumeFromHere
inline constexpr uint32_t DvdPausingFlag        = 0x8032F090u;  // PausingFlag
inline constexpr uint32_t DvdExecuting          = 0x8032F080u;  // executing
inline constexpr uint32_t DvdInitialized        = 0x8032F0C0u;  // DVDInitialized
inline constexpr uint32_t DvdFsInit             = 0x801882D0u;  // __DVDFSInit
// --- GX display list save/context (GXDispList.c) ---
inline constexpr uint32_t GxSavedData           = 0x80327D1Cu;  // __savedGXdata (GXData, 0x4F8 bytes)
inline constexpr uint32_t GxOldCpuFifo          = 0x8032F348u;  // OldCPUFifo (GXFifoObj*)
#else
inline constexpr uint32_t AIInitialized         = 0x80386448u;
inline constexpr uint32_t AICallbackBusy        = 0x8038644Cu;
inline constexpr uint32_t AICallbackStackSwitch = 0x8038647Cu;
inline constexpr uint32_t AIDmaCallback         = 0x80386480u;
inline constexpr uint32_t DspInitialized        = 0x80386608u;
inline constexpr uint32_t DspAssertPending      = 0x80386610u;
inline constexpr uint32_t DspAssertTask         = 0x80386614u;
inline constexpr uint32_t DspTmpTask            = 0x80386618u;
inline constexpr uint32_t DspCurrentTask        = 0x8038661Cu;
inline constexpr uint32_t DspFirstTask          = 0x80386620u;
inline constexpr uint32_t DspRunningTask        = 0x80386624u;
inline constexpr uint32_t AxDspTask             = 0x802F81A0u;
inline constexpr uint32_t AxIramMmem            = 0x8027F820u;
inline constexpr uint32_t AxDramMmem            = 0x802F8200u;
inline constexpr uint32_t AxInitCallback        = 0x80126948u;
inline constexpr uint32_t AxResumeCallback      = 0x80126954u;
inline constexpr uint32_t AxDoneCallback        = 0x801269A8u;
inline constexpr uint32_t AxRequestCallback     = 0x801269B8u;
inline constexpr uint32_t OneConstant           = 0x80388588u;
inline constexpr uint32_t ScaleConstant         = 0x8038858Cu;
inline constexpr uint32_t InterruptHandlerTablePtr = 0x803868f8u;
inline constexpr uint32_t DefaultThreadContext  = 0x80347498u;
inline constexpr uint32_t IdleThreadContext     = 0x803478b0u;
inline constexpr uint32_t ThreadQueueArray      = 0x803477b0u;
inline constexpr uint32_t SwitchThreadCallbackPtr = 0x80385ae0u;
inline constexpr uint32_t SchedulerReschedCounter = 0x8038691cu;
inline constexpr uint32_t SchedulerPendingFlag  = 0x80386920u;
inline constexpr uint32_t SchedulerIdleFlag     = 0x80386918u;
inline constexpr uint32_t ViInitializedFlag     = 0x80386b38u;
inline constexpr uint32_t ViTvFormat            = 0x80386ba8u;
inline constexpr uint32_t ViRenderWidth         = 0x80350864u;
inline constexpr uint32_t ViRenderHeight        = 0x80350866u;
inline constexpr uint32_t ViXfbWidth            = 0x80350872u;
inline constexpr uint32_t ViXfbHeight           = 0x8035087cu;
inline constexpr uint32_t ViRetraceCount        = 0x80386be4u;
inline constexpr uint32_t ViTimingGuard         = 0x80386b44u;
inline constexpr uint32_t ViPreRetraceCallback  = 0x80386bb8u;
inline constexpr uint32_t ViPostRetraceCallback = 0x80386bb4u;
inline constexpr uint32_t ViNextFrameBuffer     = 0x80386ba0u;
inline constexpr uint32_t ViNextFrameBufferHw   = 0x80350890u;
inline constexpr uint32_t ViRetraceQueue        = 0x80386bc0u;
inline constexpr uint32_t EggSSystem            = 0x80386F60u;
inline constexpr uint32_t ViDefaultTvFormat     = 0u;
inline constexpr uint32_t GxDataPtr             = 0x803886C8u;
inline constexpr uint32_t GxDlFifo              = 0x80344090u;
inline constexpr uint32_t GxDlWritePtr          = 0x803440A4u;
inline constexpr uint32_t GxDlCount             = 0x803440ACu;
inline constexpr uint32_t GxDrawDoneFlag        = 0x803867d8u;
inline constexpr uint32_t OSInitContext         = 0x801A20BCu;
inline constexpr uint32_t OSExitThread          = 0x801AA0F0u;
inline constexpr uint32_t OSUnlockAllMutex      = 0x801A8088u;
inline constexpr uint32_t SchedulerInitFlag     = 0x80347130u;
inline constexpr uint32_t ThreadAttrSource      = 0x80385AA8u;
inline constexpr uint32_t AlarmQueue            = 0u;
inline constexpr uint32_t AlarmQueueOffsetFromR13 = 0x6360u;
// --- DVD (dvd.c) ---
inline constexpr uint32_t DvdWaitingQueue       = 0x80343230u;
inline constexpr uint32_t DvdCanceling          = 0x80386664u;
inline constexpr uint32_t DvdResumeFromHere     = 0x80386668u;
inline constexpr uint32_t DvdPausingFlag        = 0x80386670u;
inline constexpr uint32_t DvdExecuting          = 0x803866F0u;
inline constexpr uint32_t DvdInitialized        = 0x803866a0u;
inline constexpr uint32_t DvdFsInit             = 0x8015DF1Cu;
// --- GX display list save/context (GXDispList.c) ---
inline constexpr uint32_t GxSavedData           = 0x80344110u;
inline constexpr uint32_t GxOldCpuFifo          = 0x80344710u;
#endif
}  // namespace GuestAddr

// GXData field offsets the HLE touches. The GameCube SDK struct (FFCC, 0x4F8 bytes, see the decomp's
// dolphin/gx/__gx.h) and the Wii SDK struct (Mario Kart, 0x600 bytes) share the head and differ from
// the texture-scale registers on.
namespace GxOff {
#if defined(RECOMP_PROJECT_FFCC)
inline constexpr uint32_t CpEnable = 0x08u, CpEnableLo = 0x0Au, LpSize = 0x7Cu;
inline constexpr uint32_t SuTs0 = 0xB8u, SuTs1 = 0xD8u, SuScis0 = 0xF8u, SuScis1 = 0xFCu;
inline constexpr uint32_t Iref = 0x120u, BpMask = 0x124u, CpDisp = 0x1ECu, CpTex = 0x1FCu, GenMode = 0x204u;
inline constexpr uint32_t ZOffset = 0x454u, ZScale = 0x458u, TcsManEnab = 0x4DCu;
inline constexpr uint32_t InDispList = 0x4F0u, DlSaveContext = 0x4F1u, AbtWaitPECopy = 0x4F2u, DirtyState = 0x4F4u;
inline constexpr uint32_t Size = 0x4F8u;
#else
inline constexpr uint32_t CpEnable = 0x08u, CpEnableLo = 0x0Au, LpSize = 0x7Cu;
inline constexpr uint32_t SuTs0 = 0x108u, SuTs1 = 0x128u, SuScis0 = 0x148u, SuScis1 = 0x14Cu;
inline constexpr uint32_t Iref = 0x170u, BpMask = 0x174u, CpDisp = 0x23Cu, CpTex = 0x24Cu, GenMode = 0x254u;
inline constexpr uint32_t ZOffset = 0x55Cu, ZScale = 0x560u, TcsManEnab = 0x5E4u;
inline constexpr uint32_t InDispList = 0x5F8u, DlSaveContext = 0x5F9u, AbtWaitPECopy = 0x5FAu, DirtyState = 0x5FCu;
inline constexpr uint32_t Size = 0x600u;
#endif
}  // namespace GxOff
