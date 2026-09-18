// FFCC port: the runtime host-side VI model (hle/vi.cpp) registered at FFCC (GCCP01) addresses.
// FFCC's VI.c has no retrace callbacks, VIGetNextField or VIConfigurePan; VIGetTvFormat stays
// translated and reads CurrTvMode, which vi.cpp mirrors through GuestAddr::ViTvFormat.
#include "hle_stubs.h"
#include "abi_bridge.h"

extern "C" void VIInit_HLE_801b94a4(CpuContext* ctx);
extern "C" void __VIInit_HLE_801b9294(CpuContext* ctx);
extern "C" void VIWaitForRetrace_HLE_801b99ec(CpuContext* ctx);
extern "C" void VIConfigure_HLE_801b9f6c(CpuContext* ctx);
extern "C" void VIFlush_HLE_801ba9a4(CpuContext* ctx);
extern "C" void VISetNextFrameBuffer_HLE_801baab8(CpuContext* ctx);
extern "C" void VISetBlack_HLE_801bab2c(CpuContext* ctx);
extern "C" void VIGetRetraceCount_HLE_801baba4(CpuContext* ctx);
extern "C" void VIGetCurrentLine_HLE_801bac48(CpuContext* ctx);
extern "C" void VIGetDTVStatus_HLE_801bad38(CpuContext* ctx);

PPC_NATIVE_OVERRIDE_VOID(8018bd34, VIInit_HLE_801b94a4, (CpuContext* ctx), (ctx));               // VIInit
PPC_NATIVE_OVERRIDE_VOID(8018bb34, __VIInit_HLE_801b9294, (CpuContext* ctx), (ctx));             // __VIInit
PPC_NATIVE_OVERRIDE_VOID(8018c1e4, VIWaitForRetrace_HLE_801b99ec, (CpuContext* ctx), (ctx));     // VIWaitForRetrace
PPC_NATIVE_OVERRIDE_VOID(8018c6ac, VIConfigure_HLE_801b9f6c, (CpuContext* ctx), (ctx));          // VIConfigure
PPC_NATIVE_OVERRIDE_VOID(8018ced4, VIFlush_HLE_801ba9a4, (CpuContext* ctx), (ctx));              // VIFlush
PPC_NATIVE_OVERRIDE_VOID(8018d004, VISetNextFrameBuffer_HLE_801baab8, (CpuContext* ctx), (ctx)); // VISetNextFrameBuffer
PPC_NATIVE_OVERRIDE_VOID(8018d070, VISetBlack_HLE_801bab2c, (CpuContext* ctx), (ctx));           // VISetBlack
PPC_NATIVE_OVERRIDE_VOID(8018d0ec, VIGetRetraceCount_HLE_801baba4, (CpuContext* ctx), (ctx));    // VIGetRetraceCount
PPC_NATIVE_OVERRIDE_VOID(8018d15c, VIGetCurrentLine_HLE_801bac48, (CpuContext* ctx), (ctx));     // VIGetCurrentLine
PPC_NATIVE_OVERRIDE_VOID(8018d25c, VIGetDTVStatus_HLE_801bad38, (CpuContext* ctx), (ctx));       // VIGetDTVStatus
