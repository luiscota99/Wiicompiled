#include "hle_stubs.h"
#include "abi_bridge.h"
#include "memory.h"
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <algorithm>

namespace fs = std::filesystem;

namespace {
    constexpr int32_t CARD_RESULT_READY = 0;
    constexpr int32_t CARD_RESULT_NOCARD = -3;
    constexpr int32_t CARD_RESULT_NOFILE = -4;
    constexpr int32_t CARD_RESULT_EXIST = -7;
    constexpr int32_t CARD_RESULT_INSSPACE = -9;
    constexpr int32_t CARD_RESULT_LIMIT = -11;

    bool g_mounted = false;
    int32_t g_lastResult[2] = { CARD_RESULT_NOCARD, CARD_RESULT_NOCARD };

    void SetResult(uint32_t chan, int32_t res) {
        if (chan < 2) g_lastResult[chan] = res;
    }

    void InvokeGuest(uint32_t callback, uint32_t chan, int32_t result) {
        if (callback == 0 || !TranslatedFunctionRegistry::FindByAddressPtr(callback)) return;
        auto& cpu = GetPersistentCpuContext();
        cpu.gpr[3] = chan;
        cpu.gpr[4] = result;
        InvokeIndirectCpu(callback, &cpu);
    }

    fs::path GetMemcardDir() {
        fs::path p = fs::current_path() / "UserData" / "memcard_a";
        if (!fs::exists(p)) {
            fs::create_directories(p);
        }
        return p;
    }

    std::string MakeSafeName(const char* name32) {
        std::string safe;
        for (int i = 0; i < 32 && name32[i] != '\0'; ++i) {
            char c = name32[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
                safe += c;
            } else {
                safe += '_';
            }
        }
        return safe;
    }

    struct FileEntry {
        std::string safeName;
        uint32_t length;
    };

    std::vector<FileEntry> GetFiles() {
        std::vector<FileEntry> files;
        fs::path dir = GetMemcardDir();
        if (!fs::exists(dir)) return files;
        for (auto& p : fs::directory_iterator(dir)) {
            if (p.path().extension() == ".stat") {
                FileEntry e;
                e.safeName = p.path().stem().string();
                std::ifstream f(p.path(), std::ios::binary);
                if (f) {
                    uint8_t statData[0x6C];
                    f.read(reinterpret_cast<char*>(statData), 0x6C);
                    if (f.gcount() == 0x6C) {
                        e.length = (statData[0x20] << 24) | (statData[0x21] << 16) | (statData[0x22] << 8) | statData[0x23];
                        files.push_back(e);
                    }
                }
            }
        }
        std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
            return a.safeName < b.safeName;
        });
        return files;
    }

    bool LogOnce(const char* func) {
        static std::vector<std::string> logged;
        if (std::find(logged.begin(), logged.end(), func) == logged.end()) {
            logged.push_back(func);
            return true;
        }
        return false;
    }

    // Temporary tracing. LogOnce printed one line per entry point for the whole session, which
    // hid every repeat and made a loop through the card path look like no card activity at all.
    // Capped globally so this cannot flood the log the way the alarm sentinel did.
    inline unsigned& CardTraceCount() { static unsigned n = 0; return n; }
    #define LOG_CARD_FUNC() do { \
        unsigned& cardTraceN = CardTraceCount(); \
        if (cardTraceN < 400u) { \
            ++cardTraceN; \
            std::fprintf(stderr, "[card] %s%c", __func__, 10); \
        } \
    } while (0)

    void FillFileInfo(uint32_t fileInfoPtr, uint32_t chan, int32_t fileNo, uint32_t length) {
        Memory::Write32(fileInfoPtr + 0x00, chan);
        Memory::Write32(fileInfoPtr + 0x04, fileNo);
        Memory::Write32(fileInfoPtr + 0x08, 0);
        Memory::Write32(fileInfoPtr + 0x0C, length);
        Memory::Write8(fileInfoPtr + 0x10, 0);
        Memory::Write8(fileInfoPtr + 0x11, 0);
    }

    int32_t OpenByName(uint32_t chan, const std::string& safeName, uint32_t fileInfoPtr) {
        if (chan != 0) return CARD_RESULT_NOCARD;
        auto files = GetFiles();
        for (int i = 0; i < (int)files.size(); ++i) {
            if (files[i].safeName == safeName) {
                FillFileInfo(fileInfoPtr, chan, i, files[i].length);
                SetResult(chan, CARD_RESULT_READY);
                return CARD_RESULT_READY;
            }
        }
        SetResult(chan, CARD_RESULT_NOFILE);
        return CARD_RESULT_NOFILE;
    }

    int32_t OpenByFileNo(uint32_t chan, int32_t fileNo, uint32_t fileInfoPtr) {
        if (chan != 0) return CARD_RESULT_NOCARD;
        auto files = GetFiles();
        if (fileNo >= 0 && fileNo < (int)files.size()) {
            FillFileInfo(fileInfoPtr, chan, fileNo, files[fileNo].length);
            SetResult(chan, CARD_RESULT_READY);
            return CARD_RESULT_READY;
        }
        SetResult(chan, CARD_RESULT_NOFILE);
        return CARD_RESULT_NOFILE;
    }

    int32_t DoCreate(uint32_t chan, uint32_t fileNamePtr, uint32_t size, uint32_t fileInfoPtr) {
        if (chan != 0) return CARD_RESULT_NOCARD;
        
        char nameBuf[33];
        for (int i = 0; i < 32; ++i) nameBuf[i] = Memory::Read8(fileNamePtr + i);
        nameBuf[32] = '\0';
        std::string safeName = MakeSafeName(nameBuf);
        
        auto files = GetFiles();
        for (const auto& f : files) {
            if (f.safeName == safeName) {
                SetResult(chan, CARD_RESULT_EXIST);
                return CARD_RESULT_EXIST;
            }
        }
        
        uint32_t usedBlocks = 0;
        for (const auto& f : files) {
            usedBlocks += (f.length + 8191) / 8192;
        }
        uint32_t neededBlocks = (size + 8191) / 8192;
        if (usedBlocks + neededBlocks > 123 || files.size() >= 127) {
            SetResult(chan, CARD_RESULT_INSSPACE);
            return CARD_RESULT_INSSPACE;
        }
        
        fs::path dir = GetMemcardDir();
        fs::path binPath = dir / (safeName + ".bin");
        fs::path statPath = dir / (safeName + ".stat");
        
        uint32_t allocSize = neededBlocks * 8192;
        std::vector<uint8_t> zeros(allocSize, 0);
        
        std::ofstream binF(binPath, std::ios::binary);
        binF.write(reinterpret_cast<const char*>(zeros.data()), allocSize);
        binF.close();
        
        uint8_t statData[0x6C];
        std::memset(statData, 0, sizeof(statData));
        std::memcpy(statData, nameBuf, 32);
        statData[0x20] = (size >> 24) & 0xFF;
        statData[0x21] = (size >> 16) & 0xFF;
        statData[0x22] = (size >> 8) & 0xFF;
        statData[0x23] = size & 0xFF;
        
        for(int i=0; i<4; ++i) statData[0x28 + i] = Memory::Read8(0x80000000 + i);
        for(int i=0; i<2; ++i) statData[0x2C + i] = Memory::Read8(0x80000004 + i);
        
        std::ofstream statF(statPath, std::ios::binary);
        statF.write(reinterpret_cast<const char*>(statData), 0x6C);
        statF.close();
        
        return OpenByName(chan, safeName, fileInfoPtr);
    }

    int32_t DoRead(uint32_t fileInfoPtr, uint32_t bufPtr, uint32_t length, uint32_t offset) {
        uint32_t chan = Memory::Read32(fileInfoPtr + 0);
        int32_t fileNo = Memory::Read32(fileInfoPtr + 4);
        if (chan != 0) return CARD_RESULT_NOCARD;
        
        auto files = GetFiles();
        if (fileNo < 0 || fileNo >= (int)files.size()) {
            SetResult(chan, CARD_RESULT_NOFILE);
            return CARD_RESULT_NOFILE;
        }
        
        fs::path binPath = GetMemcardDir() / (files[fileNo].safeName + ".bin");
        std::ifstream binF(binPath, std::ios::binary);
        if (!binF) {
            SetResult(chan, CARD_RESULT_NOFILE);
            return CARD_RESULT_NOFILE;
        }
        
        binF.seekg(0, std::ios::end);
        uint32_t actualSize = binF.tellg();
        if (offset + length > actualSize) {
            SetResult(chan, CARD_RESULT_LIMIT);
            return CARD_RESULT_LIMIT;
        }
        
        binF.seekg(offset, std::ios::beg);
        uint8_t* guestPtr = Memory::GetPointer(bufPtr, length);
        if (guestPtr) {
            binF.read(reinterpret_cast<char*>(guestPtr), length);
        } else {
            std::vector<uint8_t> tmp(length);
            binF.read(reinterpret_cast<char*>(tmp.data()), length);
            for(uint32_t i=0; i<length; ++i) Memory::Write8(bufPtr + i, tmp[i]);
        }
        
        SetResult(chan, CARD_RESULT_READY);
        return CARD_RESULT_READY;
    }

    int32_t DoWrite(uint32_t fileInfoPtr, uint32_t bufPtr, uint32_t length, uint32_t offset) {
        uint32_t chan = Memory::Read32(fileInfoPtr + 0);
        int32_t fileNo = Memory::Read32(fileInfoPtr + 4);
        if (chan != 0) return CARD_RESULT_NOCARD;
        
        auto files = GetFiles();
        if (fileNo < 0 || fileNo >= (int)files.size()) {
            SetResult(chan, CARD_RESULT_NOFILE);
            return CARD_RESULT_NOFILE;
        }
        
        fs::path binPath = GetMemcardDir() / (files[fileNo].safeName + ".bin");
        std::fstream binF(binPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!binF) {
            SetResult(chan, CARD_RESULT_NOFILE);
            return CARD_RESULT_NOFILE;
        }
        
        binF.seekp(0, std::ios::end);
        uint32_t actualSize = binF.tellp();
        if (offset + length > actualSize) {
            SetResult(chan, CARD_RESULT_LIMIT);
            return CARD_RESULT_LIMIT;
        }
        
        binF.seekp(offset, std::ios::beg);
        uint8_t* guestPtr = Memory::GetPointer(bufPtr, length);
        if (guestPtr) {
            binF.write(reinterpret_cast<const char*>(guestPtr), length);
        } else {
            std::vector<uint8_t> tmp(length);
            for(uint32_t i=0; i<length; ++i) tmp[i] = Memory::Read8(bufPtr + i);
            binF.write(reinterpret_cast<const char*>(tmp.data()), length);
        }
        binF.flush();
        
        SetResult(chan, CARD_RESULT_READY);
        return CARD_RESULT_READY;
    }

    int32_t DoDelete(uint32_t chan, uint32_t fileNamePtr) {
        if (chan != 0) return CARD_RESULT_NOCARD;
        char nameBuf[33];
        for (int i = 0; i < 32; ++i) nameBuf[i] = Memory::Read8(fileNamePtr + i);
        nameBuf[32] = '\0';
        std::string safeName = MakeSafeName(nameBuf);
        
        fs::path dir = GetMemcardDir();
        fs::path binPath = dir / (safeName + ".bin");
        fs::path statPath = dir / (safeName + ".stat");
        
        if (fs::exists(binPath) || fs::exists(statPath)) {
            if (fs::exists(binPath)) fs::remove(binPath);
            if (fs::exists(statPath)) fs::remove(statPath);
            SetResult(chan, CARD_RESULT_READY);
            return CARD_RESULT_READY;
        }
        SetResult(chan, CARD_RESULT_NOFILE);
        return CARD_RESULT_NOFILE;
    }

    int32_t DoFormat(uint32_t chan) {
        if (chan != 0) return CARD_RESULT_NOCARD;
        fs::path dir = GetMemcardDir();
        if (fs::exists(dir)) {
            for (auto& p : fs::directory_iterator(dir)) {
                fs::remove(p.path());
            }
        }
        SetResult(chan, CARD_RESULT_READY);
        return CARD_RESULT_READY;
    }

    int32_t DoSetStatus(uint32_t chan, int32_t fileNo, uint32_t statPtr) {
        if (chan != 0) return CARD_RESULT_NOCARD;
        auto files = GetFiles();
        if (fileNo < 0 || fileNo >= (int)files.size()) {
            SetResult(chan, CARD_RESULT_NOFILE);
            return CARD_RESULT_NOFILE;
        }
        fs::path statPath = GetMemcardDir() / (files[fileNo].safeName + ".stat");
        uint8_t statData[0x6C];
        for (int i = 0; i < 0x6C; ++i) {
            statData[i] = Memory::Read8(statPtr + i);
        }
        std::ofstream statF(statPath, std::ios::binary);
        statF.write(reinterpret_cast<const char*>(statData), 0x6C);
        
        SetResult(chan, CARD_RESULT_READY);
        return CARD_RESULT_READY;
    }
} // namespace

extern "C" void CARDInit_ffcc() {
    LOG_CARD_FUNC();
}

extern "C" int32_t CARDProbeEx_ffcc(uint32_t chan, uint32_t memSizePtr, uint32_t sectorSizePtr) {
    LOG_CARD_FUNC();
    if (chan != 0) return CARD_RESULT_NOCARD;
    if (memSizePtr) Memory::Write32(memSizePtr, 16);
    if (sectorSizePtr) Memory::Write32(sectorSizePtr, 8192);
    SetResult(chan, CARD_RESULT_READY);
    return CARD_RESULT_READY;
}

extern "C" int32_t CARDMountAsync_ffcc(uint32_t chan, uint32_t workArea, uint32_t detachCallback, uint32_t attachCallback) {
    LOG_CARD_FUNC();
    if (chan != 0) return CARD_RESULT_NOCARD;
    g_mounted = true;
    SetResult(chan, CARD_RESULT_READY);
    if (attachCallback) InvokeGuest(attachCallback, chan, CARD_RESULT_READY);
    return CARD_RESULT_READY;
}

extern "C" int32_t CARDMount_ffcc(uint32_t chan, uint32_t workArea, uint32_t detachCallback) {
    LOG_CARD_FUNC();
    if (chan != 0) return CARD_RESULT_NOCARD;
    g_mounted = true;
    SetResult(chan, CARD_RESULT_READY);
    return CARD_RESULT_READY;
}

extern "C" int32_t CARDUnmount_ffcc(uint32_t chan) {
    LOG_CARD_FUNC();
    if (chan != 0) return CARD_RESULT_NOCARD;
    g_mounted = false;
    SetResult(chan, CARD_RESULT_READY);
    return CARD_RESULT_READY;
}

extern "C" int32_t CARDCheckAsync_ffcc(uint32_t chan, uint32_t callback) {
    LOG_CARD_FUNC();
    if (chan != 0) return CARD_RESULT_NOCARD;
    SetResult(chan, CARD_RESULT_READY);
    if (callback) InvokeGuest(callback, chan, CARD_RESULT_READY);
    return CARD_RESULT_READY;
}

extern "C" int32_t CARDOpen_ffcc(uint32_t chan, uint32_t fileNamePtr, uint32_t fileInfoPtr) {
    LOG_CARD_FUNC();
    char nameBuf[33];
    for (int i = 0; i < 32; ++i) nameBuf[i] = Memory::Read8(fileNamePtr + i);
    nameBuf[32] = '\0';
    return OpenByName(chan, MakeSafeName(nameBuf), fileInfoPtr);
}

extern "C" int32_t CARDFastOpen_ffcc(uint32_t chan, uint32_t fileNo, uint32_t fileInfoPtr) {
    LOG_CARD_FUNC();
    return OpenByFileNo(chan, fileNo, fileInfoPtr);
}

extern "C" int32_t CARDClose_ffcc(uint32_t fileInfoPtr) {
    LOG_CARD_FUNC();
    uint32_t chan = Memory::Read32(fileInfoPtr + 0);
    SetResult(chan, CARD_RESULT_READY);
    return CARD_RESULT_READY;
}

extern "C" int32_t CARDCreateAsync_ffcc(uint32_t chan, uint32_t fileNamePtr, uint32_t size, uint32_t fileInfoPtr, uint32_t callback) {
    LOG_CARD_FUNC();
    int32_t res = DoCreate(chan, fileNamePtr, size, fileInfoPtr);
    if (callback) InvokeGuest(callback, chan, res);
    return res;
}

extern "C" int32_t CARDCreate_ffcc(uint32_t chan, uint32_t fileNamePtr, uint32_t size, uint32_t fileInfoPtr) {
    LOG_CARD_FUNC();
    return DoCreate(chan, fileNamePtr, size, fileInfoPtr);
}

extern "C" int32_t CARDDeleteAsync_ffcc(uint32_t chan, uint32_t fileNamePtr, uint32_t callback) {
    LOG_CARD_FUNC();
    int32_t res = DoDelete(chan, fileNamePtr);
    if (callback) InvokeGuest(callback, chan, res);
    return res;
}

extern "C" int32_t CARDDelete_ffcc(uint32_t chan, uint32_t fileNamePtr) {
    LOG_CARD_FUNC();
    return DoDelete(chan, fileNamePtr);
}

extern "C" int32_t CARDReadAsync_ffcc(uint32_t fileInfoPtr, uint32_t bufPtr, uint32_t length, uint32_t offset, uint32_t callback) {
    LOG_CARD_FUNC();
    int32_t res = DoRead(fileInfoPtr, bufPtr, length, offset);
    uint32_t chan = Memory::Read32(fileInfoPtr + 0);
    if (callback) InvokeGuest(callback, chan, res);
    return res;
}

extern "C" int32_t CARDRead_ffcc(uint32_t fileInfoPtr, uint32_t bufPtr, uint32_t length, uint32_t offset) {
    LOG_CARD_FUNC();
    return DoRead(fileInfoPtr, bufPtr, length, offset);
}

extern "C" int32_t CARDWriteAsync_ffcc(uint32_t fileInfoPtr, uint32_t bufPtr, uint32_t length, uint32_t offset, uint32_t callback) {
    LOG_CARD_FUNC();
    int32_t res = DoWrite(fileInfoPtr, bufPtr, length, offset);
    uint32_t chan = Memory::Read32(fileInfoPtr + 0);
    if (callback) InvokeGuest(callback, chan, res);
    return res;
}

extern "C" int32_t CARDWrite_ffcc(uint32_t fileInfoPtr, uint32_t bufPtr, uint32_t length, uint32_t offset) {
    LOG_CARD_FUNC();
    return DoWrite(fileInfoPtr, bufPtr, length, offset);
}

extern "C" int32_t CARDFormatAsync_ffcc(uint32_t chan, uint32_t callback) {
    LOG_CARD_FUNC();
    int32_t res = DoFormat(chan);
    if (callback) InvokeGuest(callback, chan, res);
    return res;
}

extern "C" int32_t CARDFormat_ffcc(uint32_t chan) {
    LOG_CARD_FUNC();
    return DoFormat(chan);
}

extern "C" int32_t CARDFreeBlocks_ffcc(uint32_t chan, uint32_t byteNotUsedPtr, uint32_t filesNotUsedPtr) {
    LOG_CARD_FUNC();
    if (chan != 0) return CARD_RESULT_NOCARD;
    auto files = GetFiles();
    uint32_t usedBlocks = 0;
    for (const auto& f : files) {
        usedBlocks += (f.length + 8191) / 8192;
    }
    uint32_t freeBlocks = (123 > usedBlocks) ? (123 - usedBlocks) : 0;
    uint32_t freeFiles = (127 > files.size()) ? (127 - files.size()) : 0;
    if (byteNotUsedPtr) Memory::Write32(byteNotUsedPtr, freeBlocks * 8192);
    if (filesNotUsedPtr) Memory::Write32(filesNotUsedPtr, freeFiles);
    SetResult(chan, CARD_RESULT_READY);
    return CARD_RESULT_READY;
}

extern "C" int32_t CARDGetSectorSize_ffcc(uint32_t chan, uint32_t sizePtr) {
    LOG_CARD_FUNC();
    if (chan != 0) return CARD_RESULT_NOCARD;
    if (sizePtr) Memory::Write32(sizePtr, 8192);
    SetResult(chan, CARD_RESULT_READY);
    return CARD_RESULT_READY;
}

extern "C" int32_t CARDGetSerialNo_ffcc(uint32_t chan, uint32_t serialPtr) {
    LOG_CARD_FUNC();
    if (chan != 0) return CARD_RESULT_NOCARD;
    if (serialPtr) {
        Memory::Write32(serialPtr, 0x00000000);
        Memory::Write32(serialPtr + 4, 0x12345678);
    }
    SetResult(chan, CARD_RESULT_READY);
    return CARD_RESULT_READY;
}

extern "C" int32_t CARDGetStatus_ffcc(uint32_t chan, int32_t fileNo, uint32_t statPtr) {
    LOG_CARD_FUNC();
    if (chan != 0) return CARD_RESULT_NOCARD;
    auto files = GetFiles();
    if (fileNo < 0 || fileNo >= (int)files.size()) {
        SetResult(chan, CARD_RESULT_NOFILE);
        return CARD_RESULT_NOFILE;
    }
    fs::path statPath = GetMemcardDir() / (files[fileNo].safeName + ".stat");
    std::ifstream statF(statPath, std::ios::binary);
    if (!statF) {
        SetResult(chan, CARD_RESULT_NOFILE);
        return CARD_RESULT_NOFILE;
    }
    uint8_t statData[0x6C];
    statF.read(reinterpret_cast<char*>(statData), 0x6C);
    for (int i = 0; i < 0x6C; ++i) {
        Memory::Write8(statPtr + i, statData[i]);
    }
    SetResult(chan, CARD_RESULT_READY);
    return CARD_RESULT_READY;
}

extern "C" int32_t CARDSetStatus_ffcc(uint32_t chan, int32_t fileNo, uint32_t statPtr) {
    LOG_CARD_FUNC();
    return DoSetStatus(chan, fileNo, statPtr);
}

extern "C" int32_t CARDSetStatusAsync_ffcc(uint32_t chan, int32_t fileNo, uint32_t statPtr, uint32_t callback) {
    LOG_CARD_FUNC();
    int32_t res = DoSetStatus(chan, fileNo, statPtr);
    if (callback) InvokeGuest(callback, chan, res);
    return res;
}

extern "C" int32_t CARDGetResultCode_ffcc(uint32_t chan) {
    LOG_CARD_FUNC();
    if (chan >= 2) return CARD_RESULT_NOCARD;
    return g_lastResult[chan];
}

PPC_NATIVE_OVERRIDE_VOID(801993f4, CARDInit_ffcc, (), ());
PPC_NATIVE_OVERRIDE(8019c438, CARDProbeEx_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2), (a0, a1, a2));
PPC_NATIVE_OVERRIDE(8019cafc, CARDMountAsync_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3), (a0, a1, a2, a3));
PPC_NATIVE_OVERRIDE(8019cc9c, CARDMount_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2), (a0, a1, a2));
PPC_NATIVE_OVERRIDE(8019cd80, CARDUnmount_ffcc, int32_t, (uint32_t a0), (a0));
PPC_NATIVE_OVERRIDE(8019c344, CARDCheckAsync_ffcc, int32_t, (uint32_t a0, uint32_t a1), (a0, a1));
PPC_NATIVE_OVERRIDE(8019da44, CARDOpen_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2), (a0, a1, a2));
PPC_NATIVE_OVERRIDE(8019d8e4, CARDFastOpen_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2), (a0, a1, a2));
PPC_NATIVE_OVERRIDE(8019dbbc, CARDClose_ffcc, int32_t, (uint32_t a0), (a0));
PPC_NATIVE_OVERRIDE(8019dd48, CARDCreateAsync_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4), (a0, a1, a2, a3, a4));
PPC_NATIVE_OVERRIDE(8019df68, CARDCreate_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3), (a0, a1, a2, a3));
PPC_NATIVE_OVERRIDE(8019e848, CARDDeleteAsync_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2), (a0, a1, a2));
PPC_NATIVE_OVERRIDE(8019e958, CARDDelete_ffcc, int32_t, (uint32_t a0, uint32_t a1), (a0, a1));
PPC_NATIVE_OVERRIDE(8019e298, CARDReadAsync_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4), (a0, a1, a2, a3, a4));
PPC_NATIVE_OVERRIDE(8019e3e0, CARDRead_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3), (a0, a1, a2, a3));
PPC_NATIVE_OVERRIDE(8019e648, CARDWriteAsync_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4), (a0, a1, a2, a3, a4));
PPC_NATIVE_OVERRIDE(8019e75c, CARDWrite_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3), (a0, a1, a2, a3));
PPC_NATIVE_OVERRIDE(8019d5c8, CARDFormatAsync_ffcc, int32_t, (uint32_t a0, uint32_t a1), (a0, a1));
PPC_NATIVE_OVERRIDE(8019d610, CARDFormat_ffcc, int32_t, (uint32_t a0), (a0));
PPC_NATIVE_OVERRIDE(8019962c, CARDFreeBlocks_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2), (a0, a1, a2));
PPC_NATIVE_OVERRIDE(8019977c, CARDGetSectorSize_ffcc, int32_t, (uint32_t a0, uint32_t a1), (a0, a1));
PPC_NATIVE_OVERRIDE(8019ee80, CARDGetSerialNo_ffcc, int32_t, (uint32_t a0, uint32_t a1), (a0, a1));
PPC_NATIVE_OVERRIDE(8019eb98, CARDGetStatus_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2), (a0, a1, a2));
PPC_NATIVE_OVERRIDE(8019ee38, CARDSetStatus_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2), (a0, a1, a2));
PPC_NATIVE_OVERRIDE(8019ecc4, CARDSetStatusAsync_ffcc, int32_t, (uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3), (a0, a1, a2, a3));
PPC_NATIVE_OVERRIDE(801995fc, CARDGetResultCode_ffcc, int32_t, (uint32_t a0), (a0));
