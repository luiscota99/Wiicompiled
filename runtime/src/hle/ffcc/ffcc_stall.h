// FFCC port diagnostics: stall watchdog.
//
// When the guest stops making progress the whole process goes quiet -- no frames, no logging, one
// core pinned -- and there is nothing in the log to say where it stopped. This watchdog notices that
// no frame has been presented for a while and dumps, once, the state needed to find the culprit:
// the guest scheduler variables, the alarm queue, and a native stack for every thread that is
// executing inside our own module.
//
// Enable with WIICOMPILED_STALL_SECONDS=<seconds>. Unset or 0 disables it entirely.
#pragma once

namespace FfccStall {

// Called once per presented frame. Cheap: one relaxed atomic increment.
void NoteFrame();

} // namespace FfccStall
