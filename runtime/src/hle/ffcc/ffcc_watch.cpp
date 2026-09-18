#include "ffcc_watch.h"
#include "memory.h"
#include "recomp_mod_loader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace FfccWatch {
namespace {
struct Entry { uint32_t addr; uint32_t last; bool init; };
std::vector<Entry>& Entries() {
    static std::vector<Entry> entries;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        if (const char* env = std::getenv("WIICOMPILED_WATCH")) {
            std::string list(env);
            size_t pos = 0;
            while (pos < list.size()) {
                size_t end = list.find(',', pos);
                if (end == std::string::npos) end = list.size();
                std::string tok = list.substr(pos, end - pos);
                if (!tok.empty()) entries.push_back(Entry{static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 16)), 0, false});
                pos = end + 1;
            }
            std::fprintf(stderr, "[watch] %zu guest word(s) watched" "%c", entries.size(), 10);
        }
    }
    return entries;
}
}  // namespace

bool Enabled() { return !Entries().empty(); }

void Poll(const char* site) {
    auto& entries = Entries();
    for (auto& e : entries) {
        uint32_t v = 0;
        if (!Memory::TryRead32(e.addr, v)) continue;
        if (!e.init) { e.init = true; e.last = v; continue; }
        if (v != e.last) {
            std::fprintf(stderr, "[watch] %s: 0x%08X %08X -> %08X active=0x%08X" "%c", site, e.addr, e.last, v,
                         RecompMod::CurrentTranslatedExecutionAddress(), 10);
            std::fflush(stderr);
            e.last = v;
        }
    }
}
}  // namespace FfccWatch
