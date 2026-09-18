// FFCC port: health counters the test harness reads (scripts/play.py status, scripts/probe.py).
// Off unless WIICOMPILED_STATS is set, so a normal run stays silent without a rebuild.
#pragma once
#include <cstdlib>

namespace FfccStats {
inline bool Enabled() {
    static const bool on = std::getenv("WIICOMPILED_STATS") != nullptr;
    return on;
}
}  // namespace FfccStats
