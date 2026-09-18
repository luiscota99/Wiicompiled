// AX shutdown for the FFCC port.
//
// The retail __AXOutQuit tears down real DSP and AI hardware:
//
//     __AXUserFrameCallback = NULL;
//     DSPCancelTask(&__AXDSPTask);
//     OSSleepThread(&__AXOutThreadQueue);   // woken by the DSP task-done callback
//     AIStopDMA();
//
// Our AX is fully emulated: there is no DSP task list and nothing will ever post the task-done
// callback, so running the guest body crashes on a null callback pointer inside DSPCancelTask and,
// if it got past that, would sleep on a queue nobody wakes. Observed as pc=0 with
// lr=__AXOutQuit+0x38 while starting a new game, on the path
// CSound::Realloc -> CRedSound::End -> AXQuit -> __AXOutQuit.
//
// Do natively what the teardown is actually for: stop delivering frame callbacks, and leave the
// queue alone because we never slept on it.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "memory.h"
#include "runtime_log.h"

#include <cstdint>

#if defined(RECOMP_PROJECT_FFCC)

namespace {
// AXOut.c statics, from FFCC-Decomp/build/GCCP01/main.elf.MAP.
constexpr uint32_t kAxUserFrameCallback = 0x8032F250u;  // __AXUserFrameCallback
} // namespace

extern "C" void AXOutQuit_ffcc()
{
    try {
        ::Memory::Write32(kAxUserFrameCallback, 0u);
    } catch (const ::Memory::AccessViolation&) {
        // Nothing to unhook if the address is not mapped yet.
    }
    RT_LOG(RT_TAG_AUDIO) << "__AXOutQuit handled natively (no DSP task, no AI DMA)" << std::endl;
}

// __AXOutQuit, 0x80192D40, AXOut.o
PPC_NATIVE_OVERRIDE_VOID(80192d40, AXOutQuit_ffcc, (), ());

#endif // RECOMP_PROJECT_FFCC
