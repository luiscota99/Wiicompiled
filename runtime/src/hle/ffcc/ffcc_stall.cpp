#include "ffcc_stall.h"

#include "memory.h"
#include "hle/project_guest_addresses.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace FfccStall {
namespace {

std::atomic<uint64_t> g_frames{0};
std::atomic<bool> g_started{false};
std::atomic<bool> g_reported{false};

uint32_t ReadGuest(uint32_t addr)
{
    try {
        return ::Memory::Read32(addr);
    } catch (...) {
        return 0xDEADBEEFu;
    }
}

#if defined(_WIN32)
// Walk one thread's native stack without dbghelp.
//
// x86-64 Windows unwinds from tables, so RtlLookupFunctionEntry plus RtlVirtualUnwind gives a real
// stack with no symbol server and no extra link dependency. Symbol names are resolved afterwards by
// llvm-symbolizer, which is what understands this binary's DWARF; a Windows debugger would not.
void DumpThreadStack(DWORD tid, uintptr_t moduleBase, uintptr_t moduleEnd, uintptr_t preferredBase)
{
    HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                          FALSE, tid);
    if (h == nullptr) {
        return;
    }
    if (SuspendThread(h) == static_cast<DWORD>(-1)) {
        CloseHandle(h);
        return;
    }

    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    std::vector<uintptr_t> frames;
    bool insideUs = false;
    if (GetThreadContext(h, &ctx)) {
        for (int depth = 0; depth < 48; ++depth) {
            const uintptr_t pc = static_cast<uintptr_t>(ctx.Rip);
            if (pc == 0) {
                break;
            }
            frames.push_back(pc);
            if (pc >= moduleBase && pc < moduleEnd) {
                insideUs = true;
            }

            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
            if (fn == nullptr) {
                // Leaf function: pop the return address by hand and carry on.
                if (ctx.Rsp == 0) {
                    break;
                }
                ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                ctx.Rsp += 8;
                if (ctx.Rip == 0) {
                    break;
                }
                continue;
            }

            PVOID handlerData = nullptr;
            DWORD64 establisherFrame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData,
                             &establisherFrame, nullptr);
            if (ctx.Rip == 0) {
                break;
            }
        }
    }
    ResumeThread(h);
    CloseHandle(h);

    if (!insideUs || frames.empty()) {
        return;  // OS/driver threads are noise here.
    }

    std::fprintf(stderr, "[stall] thread %lu native stack (%zu frames)%c",
                 static_cast<unsigned long>(tid), frames.size(), 10);
    for (size_t i = 0; i < frames.size(); ++i) {
        const uintptr_t pc = frames[i];
        if (pc >= moduleBase && pc < moduleEnd) {
            // Static address: what llvm-symbolizer wants for this image.
            const uintptr_t stat = pc - moduleBase + preferredBase;
            std::fprintf(stderr, "[stall]   #%02zu 0x%016llx  static 0x%016llx%c", i,
                         static_cast<unsigned long long>(pc),
                         static_cast<unsigned long long>(stat), 10);
        } else {
            std::fprintf(stderr, "[stall]   #%02zu 0x%016llx  (outside module)%c", i,
                         static_cast<unsigned long long>(pc), 10);
        }
    }
}

void DumpAllStacks()
{
    HMODULE self = GetModuleHandleW(nullptr);
    if (self == nullptr) {
        return;
    }
    const auto base = reinterpret_cast<uintptr_t>(self);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(self);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const uintptr_t moduleEnd = base + nt->OptionalHeader.SizeOfImage;
    const uintptr_t preferred = static_cast<uintptr_t>(nt->OptionalHeader.ImageBase);

    std::fprintf(stderr, "[stall] module base 0x%llx preferred 0x%llx (symbolize the static column)%c",
                 static_cast<unsigned long long>(base),
                 static_cast<unsigned long long>(preferred), 10);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }
    const DWORD pid = GetCurrentProcessId();
    const DWORD self_tid = GetCurrentThreadId();
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid && te.th32ThreadID != self_tid) {
                DumpThreadStack(te.th32ThreadID, base, moduleEnd, preferred);
            }
            te.dwSize = sizeof(te);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}
#else
void DumpAllStacks() {}
#endif

void Report()
{
    std::fprintf(stderr, "[stall] no frame presented for the configured timeout%c", 10);

    // Guest scheduler state. These are the variables that decide whether a thread switch can happen
    // at all, so a stall that is really "the scheduler refused to switch" shows up here directly.
    std::fprintf(stderr,
                 "[stall] Reschedule=0x%x RunQueueHint=0x%x RunQueueBits=0x%x%c",
                 ReadGuest(GuestAddr::SchedulerIdleFlag),
                 ReadGuest(GuestAddr::SchedulerIdleFlag - 4u),
                 ReadGuest(GuestAddr::SchedulerPendingFlag), 10);
    std::fprintf(stderr, "[stall] OSCurrentThread=0x%x OSCurrentContext=0x%x%c",
                 ReadGuest(0x800000e4u), ReadGuest(0x800000d4u), 10);

    if constexpr (GuestAddr::AlarmQueue != 0u) {
        const uint32_t head = ReadGuest(GuestAddr::AlarmQueue);
        std::fprintf(stderr, "[stall] AlarmQueue head=0x%x tail=0x%x%c", head,
                     ReadGuest(GuestAddr::AlarmQueue + 4u), 10);
        if (head != 0u) {
            std::fprintf(stderr, "[stall]   head handler=0x%x prev=0x%x next=0x%x%c",
                         ReadGuest(head), ReadGuest(head + 0x10u), ReadGuest(head + 0x14u), 10);
        }
    }

    DumpAllStacks();
    std::fflush(stderr);
}

void WatchdogMain(unsigned timeoutSeconds)
{
    uint64_t lastSeen = g_frames.load(std::memory_order_relaxed);
    auto lastChange = std::chrono::steady_clock::now();

    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const uint64_t now = g_frames.load(std::memory_order_relaxed);
        const auto stamp = std::chrono::steady_clock::now();

        if (now != lastSeen) {
            lastSeen = now;
            lastChange = stamp;
            g_reported.store(false, std::memory_order_relaxed);
            continue;
        }

        const auto stalled =
            std::chrono::duration_cast<std::chrono::seconds>(stamp - lastChange).count();
        if (stalled >= static_cast<long long>(timeoutSeconds) &&
            !g_reported.exchange(true, std::memory_order_relaxed)) {
            Report();
        }
    }
}

} // namespace

void NoteFrame()
{
    g_frames.fetch_add(1, std::memory_order_relaxed);

    if (g_started.load(std::memory_order_acquire)) {
        return;
    }
    if (g_started.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    unsigned seconds = 0;
    if (const char* env = std::getenv("WIICOMPILED_STALL_SECONDS")) {
        seconds = static_cast<unsigned>(std::strtoul(env, nullptr, 10));
    }
    if (seconds == 0) {
        return;  // disabled
    }
    std::fprintf(stderr, "[stall] watchdog armed: %u s without a presented frame%c", seconds, 10);
    std::thread(WatchdogMain, seconds).detach();
}

} // namespace FfccStall
