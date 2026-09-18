// In-game replacement for the GBA "personal screens" of FFCC multiplayer: the screen logic.
//
// FFCC multiplayer needs one Game Boy Advance per player; the GameCube streams each player's data
// to it and the player edits their command list, items and equipment, or creates their character,
// on the handheld. The runtime's fake GBA (ffcc_gba.cpp) answers the link; this file runs the
// screens the GBA would show, reading the player's state straight from guest memory and sending
// the same words a real client sends. Each frame it emits a list of draw operations in a 360x240
// screen space (ffcc_gba_screens.h); ffcc_gba_native_draw.cpp draws them with the game's own
// font, icons and quads through the host-to-guest bridge, and an ImGui fallback here draws the
// same list when that bridge is not available (WIICOMPILED_GBA_IMGUI=1 forces it).
//
// Guest data (FFCC-Decomp, PAL):
//   Game.m_scriptFoodBase[4]  game.h:164  (CGame 0x8021EEC0 + 0xC5C0): CCaravanWork* per port.
//   CCaravanWork (gobjwork.h): m_maxHp +0x1A, m_hp +0x1C, m_strength +0x1E, m_magic +0x20,
//     m_defense +0x22, m_equipment s16[4] +0xAC (weapon, armor, tribal, accessory: inventory index
//     or -1), m_inventoryItemCount +0xB4, m_inventoryItems s16[164] +0xB6 (64 inventory, 96
//     permanent artifacts from index 64, 4 temporary artifacts from index 160), m_gil +0x200,
//     m_commandListInventorySlotRef s16[8] +0x204 (slots 0/1 Attack/Defend, -1 empty),
//     m_currentCmdListIndex +0x224, m_jobType +0x3AC, m_letterMeta u16[8] +0x3B8, m_name +0x3CA,
//     m_tribeId +0x3E0, m_genderFlag +0x3E2, m_letterCount +0x3E8, m_numCmdListSlots +0xBAA.
//   Item rows: SItemFlatRow[] at Game.unkCFlatData0[2] (0x8022B468), 0x48 bytes each, m_equipFlags
//     at +4: race bits 0..3, body bits 0x10/0x20, slot bits 0x100 weapon, 0x400 armor, 0xA00
//     tribal, 0x3000 accessory (singmenu.cpp ChkEquipPossible / GetEquipType 3030-3090).
//   Item types (singmenu.cpp:3504 GetItemType): 1..0x9E equipment, ..0xFF, ..0x124, 0x125,
//     ..0x129, ..0x17C, ..0x188, ..0x190, else. Command candidates (menu_cmd.cpp:1650 GetCmdItem):
//     types 1 (only gSingMenuItemIconByType[id] == tribe), 2, 3, 4, 7.
//   Names: Game.m_cFlatDataArr[1].TableStrings(0)[id * 5 + 4] (menu_cmd.cpp:789), table pointer
//     at 0x8022D03C (verified live). Trade names: PTR_s_Blacksmith[8] at 0x80214140.
//
// Client words (all consumed by GbaQueue::ExecutQueue unless noted; opcode = byte0 & 0x3F):
//   [0E 00 code 00]  screen report -> m_stateCodeArr (joybus.cpp:2778): 1 command list (GameCube
//                    streams it, state 0x4C), 2 items, 3 equipment (stream type 0x06), 5 temporary
//                    artifacts (type 0x09), 7 favourites (type 0x04), 9 letters, 0 field.
//   [1F slot idxLo idxHi]  CCaravanWork::ChgCmdLst(slot, s16 inventory index, -1 clears).
//   [17 action idx]  ChgItemData: 1 FGUseItem, 2 FGPutItem (drop), 3 DeleteItemIdx (gbaque.cpp:598).
//   [1E slot idx]    ChgEquipPos(slot, s8 inventory index or -1) (gbaque.cpp:647; the GameCube's
//                    own menu passes the same, menu_equip.cpp:311).
//   [06 18 00 00]    confirm -> m_evtState1 = 1 (gbaque.cpp:906), polled by the stage script.
//   Character creation (mode type 1, SendMType [1B 01] from GbaQue.InitCmakeInfo, wm_menu.cpp:7443):
//     packets answered with JoyBus::SendResult [06 code] ok / [07 code] rejected. Name: [1C crcHi
//     crcLo n0] + 5 x [5C a b c] (ChkCMakeName 2789; CRC16 over the 16-byte buffer with
//     JoyBusCrcTable 0x801DA0A4, init 0xFFFF, inverted; max 7 chars, cmake.cpp:1006). Then, under
//     OPCODE 0x14 (ExecutQueue's cmd == 0x14 block; GBARecvSend pops 0x0C words with sub 2/3/6-9 as
//     letter/shop requests, so 0x0C never reaches the creation code, measured): [14 02 type]
//     (tribe | look << 2 | body << 7), [14 03 job], [14 06 month day], favourites [1D crcHi crcLo
//     f0] [5D f1 f2 f3] (nibble ratings), end [14 05], cancel [14 04]. SetMakeChara
//     (wm_menu.cpp:7128) then builds the caravan work; the GameCube sends [1B 04].
#include "memory.h"
#include "runtime_log.h"
#include "ffcc_gba_screens.h"
#include "ffcc_gba_mgba.h"

#include <imgui.h>
#include <aurora/imgui.h>
#include <cstdlib>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(RECOMP_PROJECT_FFCC)

extern "C" uint16_t FfccGbaReadKeys(uint32_t chan);   // aurora pad.cpp: GBA KEYINPUT bits, active-high
extern "C" bool FfccGbaClientShown(uint32_t chan);    // ffcc_gba.cpp: that player has the client screen open

namespace FfccGbaScreens {
bool MenuOpen(uint32_t chan);                 // ffcc_gba.cpp
bool Confirmed(uint32_t chan);                // ffcc_gba.cpp: ready sent, waiting for the game's release
uint32_t ModeType(uint32_t chan);             // ffcc_gba.cpp: last [1B mode]
bool TakeResult(uint32_t chan, bool& ok, uint8_t& code);
void SendWord(uint32_t chan, uint32_t word);  // ffcc_gba.cpp
void Confirm(uint32_t chan);                  // ffcc_gba.cpp: screen closed + confirm word
void ReportScreen(uint32_t chan, uint8_t code);
const std::vector<uint8_t>& Payload(uint32_t chan, uint32_t type);
}

namespace {

using FfccGbaScreens::Op;
using FfccGbaScreens::Screen;
using FfccGbaScreens::kScreenW;
using FfccGbaScreens::kScreenH;

constexpr uint32_t kScriptFoodBase = 0x8022B480u;   // CGame + 0xC5C0
constexpr uint32_t kTable0Strings  = 0x8022D03Cu;   // m_cFlatDataArr[1].m_tabl[0].m_strings
constexpr uint32_t kItemRows       = 0x8022B468u;   // Game.unkCFlatData0[2] -> SItemFlatRow[]
constexpr uint32_t kIconByItem     = 0x801DE6ACu;   // gSingMenuItemIconByType, u8[]
constexpr uint32_t kCrcTable       = 0x801DA0A4u;   // JoyBusCrcTable, u16[256]
constexpr uint32_t kJobNames       = 0x80214140u;   // PTR_s_Blacksmith, char*[8]
constexpr uint32_t kItemRowSize = 0x48;
constexpr uint32_t kOffMaxHp = 0x1A, kOffHp = 0x1C, kOffStr = 0x1E, kOffMag = 0x20, kOffDef = 0x22;
constexpr uint32_t kOffEquip = 0xAC, kOffItemCount = 0xB4, kOffInventory = 0xB6, kOffGil = 0x200;
constexpr uint32_t kOffSlots = 0x204, kOffCurrent = 0x224, kOffJob = 0x3AC, kOffLetterMeta = 0x3B8;
constexpr uint32_t kOffName = 0x3CA, kOffTribe = 0x3E0, kOffGender = 0x3E2, kOffLetterCount = 0x3E8, kOffNumSlots = 0xBAA;
constexpr int kInventoryCapacity = 64, kPermArtStart = 64, kPermArtCount = 96, kTempArtStart = 160, kTempArtCount = 4;

constexpr uint16_t kKeyA = 0x001, kKeyB = 0x002;
constexpr uint16_t kKeyRight = 0x010, kKeyLeft = 0x020, kKeyUp = 0x040, kKeyDown = 0x080, kKeyR = 0x100, kKeyL = 0x200;

// Screen layout (360x240 space). The game font at scale 1 is about 20 px tall in menu space.
constexpr float kFont = 0.4f, kSmall = 0.34f, kLine = 11.0f, kCharW = 5.0f;
constexpr float kLeft = 5.0f, kTop = 17.0f, kStatusY = 118.0f;

struct Rgb { uint8_t r, g, b; };
// FFCC's own in-game text style: white outlined glyphs on a dark translucent panel (the field
// message strip), so the game's shadowed font stays legible over any scene.
constexpr Rgb kInk{0xF4, 0xF4, 0xF6}, kDim{0xB4, 0xB8, 0xC4}, kSel{0xFF, 0xE0, 0x60}, kGood{0x90, 0xF0, 0x90}, kBad{0xFF, 0x90, 0x80};
constexpr Rgb kHeadInk{0xF4, 0xF4, 0xF6}, kHeadDim{0x9C, 0xA0, 0xB0};

Screen g_screens[4];
bool g_native = false;

void Rect(Screen& s, float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    Op op; op.kind = Op::Rect; op.x = x; op.y = y; op.w = w; op.h = h; op.r = r; op.g = g; op.b = b; op.a = a; s.ops.push_back(op);
}
void Text(Screen& s, float x, float y, const std::string& t, Rgb c, float scale = kFont) {
    Op op; op.kind = Op::Text; op.x = x; op.y = y; op.text = t; op.r = c.r; op.g = c.g; op.b = c.b; op.scale = scale; s.ops.push_back(op);
}
void Icon(Screen& s, float x, float y, int itemId, float scale = 0.5f) {
    Op op; op.kind = Op::Icon; op.x = x; op.y = y; op.itemId = itemId; op.scale = scale; s.ops.push_back(op);
}
uint32_t g_port = 0;   // port whose screen is being built (for player-coloured accents)
void Highlight(Screen& s, float y) {
    const FfccGbaScreens::PlayerRgb c = FfccGbaScreens::PlayerColor(g_port);
    Rect(s, kLeft - 3, y - 1, kScreenW - 2 * kLeft + 6, kLine - 1, uint8_t(c.r * 3 / 4), uint8_t(c.g * 3 / 4), uint8_t(c.b * 3 / 4), 0xC8);
}

bool ValidPtr(uint32_t p) { return p >= 0x80000000u && p < 0x81800000u; }

std::string ReadCString(uint32_t addr, size_t max = 48) {
    std::string s;
    if (!ValidPtr(addr)) return s;
    for (size_t i = 0; i < max; ++i) {
        const uint8_t c = ::Memory::Read8(addr + uint32_t(i));
        if (c == 0) break;
        s.push_back((c >= 0x20 && c < 0x7F) ? char(c) : '?');
    }
    return s;
}

int16_t R16s(uint32_t a) { return int16_t(::Memory::Read16(a)); }

int ItemType(int id) {
    if (id <= 0) return 0;
    if (id <= 0x9E) return 1;
    if (id <= 0xFF) return 2;
    if (id <= 0x124) return 3;
    if (id == 0x125) return 4;
    if (id <= 0x129) return 5;
    if (id <= 0x17C) return 6;
    if (id <= 0x188) return 7;
    if (id <= 0x190) return 8;
    return 9;
}

std::string ItemName(int id) {
    uint32_t table = 0, ptr = 0;
    if (id <= 0) return "---";
    if (!::Memory::TryRead32(kTable0Strings, table) || !ValidPtr(table)) return "?";
    if (!::Memory::TryRead32(table + uint32_t(id * 5 + 4) * 4u, ptr)) return "?";
    std::string n = ReadCString(ptr);
    return n.empty() ? "?" : n;
}

std::string JobName(int job) {
    uint32_t ptr = 0;
    if (job < 0 || job > 7 || !::Memory::TryRead32(kJobNames + uint32_t(job) * 4u, ptr)) return "Trade " + std::to_string(job + 1);
    std::string n = ReadCString(ptr);
    return n.empty() ? "Trade " + std::to_string(job + 1) : n;
}

uint16_t EquipFlags(int id) {
    uint32_t base = 0;
    if (id <= 0 || !::Memory::TryRead32(kItemRows, base) || !ValidPtr(base)) return 0;
    return ::Memory::Read16(base + uint32_t(id) * kItemRowSize + 4u);
}

int EquipSlotOf(int id) {   // CMenuPcs::GetEquipType (singmenu.cpp:3072)
    const uint16_t f = EquipFlags(id);
    if (f & 0x100) return 0;
    if (f & 0x400) return 1;
    if (f & 0xA00) return 2;
    if (f & 0x3000) return 3;
    return -1;
}

bool EquipPossible(int id, int tribe, int gender) {   // CMenuPcs::ChkEquipPossible (singmenu.cpp:3036)
    const uint16_t f = EquipFlags(id);
    const unsigned race = f & 0xF, body = f & 0x30;
    const unsigned raceMask = 1u << (tribe & 3), bodyMask = gender ? 0x20u : 0x10u;
    if (race && body) return (race & raceMask) && (body & bodyMask);
    if (race) return (race & raceMask) != 0;
    return (body & bodyMask) != 0;
}

uint16_t Crc16(const uint8_t* data, size_t len) {   // JoyBus::Crc16 (joybus.cpp:2893) with the game's table
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        const uint16_t entry = ::Memory::Read16(kCrcTable + uint32_t((uint8_t(crc >> 8) ^ data[i])) * 2u);
        crc = uint16_t((crc << 8) ^ entry);
    }
    return uint16_t(~crc);
}

uint32_t Word(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    return (uint32_t(b0) << 24) | (uint32_t(b1) << 16) | (uint32_t(b2) << 8) | uint32_t(b3);
}

int Wrap(int v, int n) { return n <= 0 ? 0 : ((v % n) + n) % n; }

std::string Fit(const std::string& t, int maxChars) { return int(t.size()) > maxChars ? t.substr(0, size_t(maxChars - 1)) + "." : t; }

// Scrolling list with a cursor, `show` rows from y; icons optional.
struct ListRow { std::string text; int itemId; };
void DrawRows(Screen& s, const std::vector<ListRow>& rows, int cursor, float y, int show, bool icons) {
    const int n = int(rows.size());
    int first = cursor - show / 2;
    if (first > n - show) first = n - show;
    if (first < 0) first = 0;
    for (int i = first; i < n && i < first + show; ++i, y += kLine) {
        const bool sel = (i == cursor);
        if (sel) Highlight(s, y);
        const float tx = icons ? kLeft + 14 : kLeft + 6;
        if (icons && rows[size_t(i)].itemId > 0) Icon(s, kLeft + 1, y - 1, rows[size_t(i)].itemId, 0.3f);
        Text(s, tx, y, Fit(rows[size_t(i)].text, 32), sel ? kSel : kInk);
    }
    if (n > show) Text(s, kScreenW - 40, y, "(" + std::to_string(cursor + 1) + "/" + std::to_string(n) + ")", kDim, kSmall);
}

// ---------------------------------------------------------------- personal menu (tabs)

enum Tab { kTabCommands = 0, kTabItems, kTabEquip, kTabArtifacts, kTabStatus, kTabFavourites, kTabCount };
const char* const kTabNames[kTabCount] = {"Cmd", "Items", "Equip", "Arti", "Status", "Fav"};
const uint8_t kTabScreenCode[kTabCount] = {1, 2, 3, 5, 0, 7};
const char* const kEquipSlotNames[4] = {"Weapon", "Armor", "Tribal", "Accessory"};
const char* const kTribeNames[4] = {"Clavat", "Lilty", "Yuke", "Selkie"};

struct Entry { int index; int itemId; std::string name; };

struct MenuUi {
    bool wasOpen = false;
    uint16_t prevKeys = 0;
    int tab = kTabCommands;
    int cursor = 0;          // row in the tab
    int sub = -1;            // -1 = top level of the tab; else a picker/action list is open
    int subCursor = 0;
    std::vector<Entry> subEntries;
    std::string status;
    bool statusBad = false;
    std::chrono::steady_clock::time_point statusAt{};
};
MenuUi g_menu[4];

void SetStatus(MenuUi& ui, const std::string& s, bool bad = false) {
    ui.status = s; ui.statusBad = bad; ui.statusAt = std::chrono::steady_clock::now();
}

std::vector<Entry> CommandCandidates(uint32_t work) {
    std::vector<Entry> out;
    const int tribe = ::Memory::Read16(work + kOffTribe) & 3;
    for (int i = 0; i < kInventoryCapacity; ++i) {
        const int id = R16s(work + kOffInventory + uint32_t(i) * 2u);
        const int t = ItemType(id);
        if (t == 0 || t == 5 || t == 6 || t == 8 || t == 9) continue;
        if (t == 1 && ::Memory::Read8(kIconByItem + uint32_t(id)) != tribe) continue;
        out.push_back({i, id, ItemName(id)});
    }
    return out;
}

std::vector<Entry> InventoryEntries(uint32_t work) {
    std::vector<Entry> out;
    for (int i = 0; i < kInventoryCapacity; ++i) {
        const int id = R16s(work + kOffInventory + uint32_t(i) * 2u);
        if (id > 0) out.push_back({i, id, ItemName(id)});
    }
    return out;
}

bool InUse(uint32_t work, int invIndex) {   // CMenuPcs::EquipChk: in a command slot or equipped
    const int numSlots = R16s(work + kOffNumSlots);
    for (int s = 2; s < numSlots && s < 8; ++s) if (R16s(work + kOffSlots + uint32_t(s) * 2u) == invIndex) return true;
    for (int e = 0; e < 4; ++e) if (R16s(work + kOffEquip + uint32_t(e) * 2u) == invIndex) return true;
    return false;
}

std::vector<Entry> EquipCandidates(uint32_t work, int slot) {
    std::vector<Entry> out;
    const int tribe = ::Memory::Read16(work + kOffTribe) & 3;
    const int gender = ::Memory::Read16(work + kOffGender) != 0;
    for (int i = 0; i < kInventoryCapacity; ++i) {
        const int id = R16s(work + kOffInventory + uint32_t(i) * 2u);
        if (ItemType(id) != 1 || EquipSlotOf(id) != slot || !EquipPossible(id, tribe, gender)) continue;
        if (InUse(work, i)) continue;
        out.push_back({i, id, ItemName(id)});
    }
    return out;
}

void DrawHeader(Screen& s, int active) {
    const FfccGbaScreens::PlayerRgb c = FfccGbaScreens::PlayerColor(g_port);
    Rect(s, 0, kTop - 6, kScreenW, 1.0f, c.r, c.g, c.b, 0xD0);   // rule under the tabs in the player's colour
    float x = kLeft;
    for (int t = 0; t < kTabCount; ++t) {
        const std::string name = kTabNames[t];
        const float w = kCharW * kSmall / kFont * float(name.size()) * 1.15f + 6.0f;
        if (t == active) Rect(s, x - 2, 2, w, kTop - 8, uint8_t(c.r * 3 / 4), uint8_t(c.g * 3 / 4), uint8_t(c.b * 3 / 4), 0xE0);
        Text(s, x, 3, name, t == active ? kInk : kHeadDim, kSmall);
        x += w + 2;
    }
}

void RunMenu(uint32_t port, Screen& out) {
    g_port = port;
    MenuUi& ui = g_menu[port];
    const bool open = FfccGbaScreens::MenuOpen(port);
    if (!open) { ui.wasOpen = false; ui.sub = -1; return; }
    if (!ui.wasOpen) { ui.wasOpen = true; ui.prevKeys = FfccGbaReadKeys(port); ui.sub = -1; ui.status.clear(); }

    uint32_t work = 0;
    if (!::Memory::TryRead32(kScriptFoodBase + port * 4u, work) || !ValidPtr(work)) return;
    const int numSlots = R16s(work + kOffNumSlots);
    if (numSlots < 2 || numSlots > 8) return;

    const uint16_t keys = FfccGbaReadKeys(port);
    const uint16_t pressed = keys & ~ui.prevKeys;
    ui.prevKeys = keys;

    if (ui.sub < 0 && (pressed & (kKeyL | kKeyR))) {
        ui.tab = Wrap(ui.tab + ((pressed & kKeyR) ? 1 : -1), kTabCount);
        ui.cursor = 0;
        FfccGbaScreens::ReportScreen(port, kTabScreenCode[ui.tab]);
    }

    // ---- input
    if (ui.sub >= 0) {
        const int n = int(ui.subEntries.size());
        if (pressed & kKeyUp)   ui.subCursor = Wrap(ui.subCursor - 1, n);
        if (pressed & kKeyDown) ui.subCursor = Wrap(ui.subCursor + 1, n);
        if (pressed & kKeyB) ui.sub = -1;
        if ((pressed & kKeyA) && n > 0) {
            const Entry& e = ui.subEntries[size_t(ui.subCursor)];
            if (ui.tab == kTabCommands) {
                FfccGbaScreens::SendWord(port, Word(0x1F, uint8_t(ui.cursor), uint8_t(e.index & 0xFF), uint8_t((e.index >> 8) & 0xFF)));
                SetStatus(ui, "Slot " + std::to_string(ui.cursor + 1) + ": " + e.name);
            } else if (ui.tab == kTabEquip) {
                FfccGbaScreens::SendWord(port, Word(0x1E, uint8_t(ui.cursor), uint8_t(e.index & 0xFF), 0));
                SetStatus(ui, std::string(kEquipSlotNames[ui.cursor]) + ": " + e.name);
            } else if (ui.tab == kTabItems) {
                FfccGbaScreens::SendWord(port, Word(0x17, uint8_t(e.index), uint8_t(e.itemId), 0));
                SetStatus(ui, e.name + ": " + ItemName(R16s(work + kOffInventory + uint32_t(e.itemId) * 2u)));
            }
            RT_LOG(RT_TAG_OS) << "gba screen: port " << port << " tab " << kTabNames[ui.tab] << " row " << ui.cursor
                              << " -> entry " << e.index << " (" << e.name << ")" << std::endl;
            ui.sub = -1;
        }
    } else {
        int rows = 1;
        switch (ui.tab) {
        case kTabCommands: rows = numSlots; if (ui.cursor < 2) ui.cursor = 2; break;
        case kTabItems: rows = int(InventoryEntries(work).size()); break;
        case kTabEquip: rows = 4; break;
        default: rows = 1; break;
        }
        if (rows < 1) rows = 1;
        if (ui.cursor >= rows) ui.cursor = rows - 1;
        if (pressed & kKeyUp)   ui.cursor = (ui.tab == kTabCommands) ? (ui.cursor > 2 ? ui.cursor - 1 : rows - 1) : Wrap(ui.cursor - 1, rows);
        if (pressed & kKeyDown) ui.cursor = (ui.tab == kTabCommands) ? (ui.cursor < rows - 1 ? ui.cursor + 1 : 2) : Wrap(ui.cursor + 1, rows);
        if (pressed & kKeyA) {
            if (ui.tab == kTabCommands) {
                ui.subEntries = CommandCandidates(work);
                ui.subEntries.insert(ui.subEntries.begin(), Entry{-1, 0, "(clear slot)"});
                ui.sub = 0; ui.subCursor = 0;
            } else if (ui.tab == kTabEquip) {
                ui.subEntries = EquipCandidates(work, ui.cursor);
                ui.subEntries.insert(ui.subEntries.begin(), Entry{-1, 0, "(remove)"});
                ui.sub = 0; ui.subCursor = 0;
            } else if (ui.tab == kTabItems) {
                const std::vector<Entry> inv = InventoryEntries(work);
                if (!inv.empty() && ui.cursor < int(inv.size())) {
                    const Entry& it = inv[size_t(ui.cursor)];
                    ui.subEntries = {Entry{1, it.index, "Use"}, Entry{2, it.index, "Drop"}, Entry{3, it.index, "Delete"}};
                    ui.sub = 0; ui.subCursor = 0;
                }
            }
        }
    }

    // ---- draw
    out.visible = true;
    out.title = ReadCString(work + kOffName, 17);
    DrawHeader(out, ui.tab);
    Text(out, kScreenW - kCharW * float(out.title.size()) * kSmall / kFont - 6, 3, out.title, kHeadDim, kSmall);
    float y = kTop;

    const std::vector<uint8_t>& bonus = FfccGbaScreens::Payload(port, 7);
    if (bonus.size() > 2) {
        std::string text;
        for (size_t i = 1; i < bonus.size() && bonus[i] != 0 && i < 60; ++i) text.push_back((bonus[i] >= 0x20 && bonus[i] < 0x7F) ? char(bonus[i]) : ' ');
        if (!text.empty() && text != "Bonus") { Text(out, kLeft, y, "Bonus: " + text, kGood, kSmall); y += kLine - 2; }
    }

    switch (ui.tab) {
    case kTabCommands: {
        const int current = R16s(work + kOffCurrent);
        for (int s = 0; s < numSlots; ++s, y += kLine) {
            std::string label = (s == 0) ? "Attack" : (s == 1) ? "Defend" : "---";
            int id = 0;
            if (s >= 2) {
                const int ref = R16s(work + kOffSlots + uint32_t(s) * 2u);
                if (ref >= 0 && ref < kInventoryCapacity) { id = R16s(work + kOffInventory + uint32_t(ref) * 2u); label = ItemName(id); }
            }
            const bool sel = (ui.sub < 0 && s == ui.cursor);
            if (sel) Highlight(out, y);
            Text(out, kLeft, y, std::to_string(s + 1), sel ? kSel : kDim);
            if (id > 0) Icon(out, kLeft + 11, y - 1, id, 0.3f);
            Text(out, kLeft + 26, y, label + ((s == current) ? " *" : ""), sel ? kSel : kInk);
        }
        break;
    }
    case kTabItems: {
        const std::vector<Entry> inv = InventoryEntries(work);
        std::vector<ListRow> rows;
        for (const Entry& e : inv) rows.push_back({e.name, e.itemId});
        if (rows.empty()) Text(out, kLeft, y, "(no items)", kDim);
        else DrawRows(out, rows, ui.cursor, y, ui.sub >= 0 ? 3 : 8, true);
        Text(out, kLeft, kStatusY - kLine, std::to_string(int(::Memory::Read16(work + kOffItemCount))) + " items    " +
             std::to_string(int(::Memory::Read32(work + kOffGil))) + " gil", kDim, kSmall);
        break;
    }
    case kTabEquip: {
        for (int e = 0; e < 4; ++e, y += kLine) {
            const int ref = R16s(work + kOffEquip + uint32_t(e) * 2u);
            std::string label = "---";
            int id = 0;
            if (ref >= 0 && ref < kInventoryCapacity) { id = R16s(work + kOffInventory + uint32_t(ref) * 2u); label = ItemName(id); }
            const bool sel = (ui.sub < 0 && e == ui.cursor);
            if (sel) Highlight(out, y);
            Text(out, kLeft, y, kEquipSlotNames[e], sel ? kSel : kDim);
            if (id > 0) Icon(out, kLeft + 54, y - 1, id, 0.3f);
            Text(out, kLeft + 68, y, label, sel ? kSel : kInk);
        }
        break;
    }
    case kTabArtifacts: {
        int shown = 0;
        for (int i = 0; i < kPermArtCount && shown < 4; ++i) {
            const int id = R16s(work + kOffInventory + uint32_t(kPermArtStart + i) * 2u);
            if (id > 0) { Icon(out, kLeft + 2, y - 1, id, 0.5f); Text(out, kLeft + 22, y, ItemName(id), kInk); y += kLine; ++shown; }
        }
        if (shown == 0) { Text(out, kLeft, y, "(no artifacts yet)", kDim); y += kLine; }
        Text(out, kLeft, y, "This dungeon:", kDim, kSmall); y += kLine - 3;
        for (int i = 0; i < kTempArtCount; ++i) {
            const int id = R16s(work + kOffInventory + uint32_t(kTempArtStart + i) * 2u);
            if (id > 0) { Icon(out, kLeft + 2, y - 1, id, 0.5f); Text(out, kLeft + 22, y, ItemName(id), kInk); y += kLine; }
        }
        break;
    }
    case kTabStatus: {
        const std::string name = ReadCString(work + kOffName, 17);
        Text(out, kLeft, y, name, kInk, 0.52f); y += kLine + 3;
        Text(out, kLeft, y, std::string(kTribeNames[::Memory::Read16(work + kOffTribe) & 3]) + "   " + JobName(int32_t(::Memory::Read32(work + kOffJob))), kInk); y += kLine + 4;
        Text(out, kLeft, y, "HP " + std::to_string(::Memory::Read16(work + kOffHp)) + " / " + std::to_string(::Memory::Read16(work + kOffMaxHp)), kInk); y += kLine;
        Text(out, kLeft, y, "Strength " + std::to_string(::Memory::Read16(work + kOffStr)) + "   Defense " + std::to_string(::Memory::Read16(work + kOffDef)) +
             "   Magic " + std::to_string(::Memory::Read16(work + kOffMag)), kInk); y += kLine;
        Text(out, kLeft, y, "Gil " + std::to_string(::Memory::Read32(work + kOffGil)) + "   Items " + std::to_string(::Memory::Read16(work + kOffItemCount)) +
             "   Letters " + std::to_string(::Memory::Read32(work + kOffLetterCount)), kInk);
        break;
    }
    case kTabFavourites: {
        std::string s;
        for (int i = 0; i < 8; ++i) s += std::to_string(int(::Memory::Read16(work + kOffLetterMeta + uint32_t(i) * 2u))) + "  ";
        Text(out, kLeft, y, "Ratings: " + s, kInk); y += kLine;
        Text(out, kLeft, y, "(set at creation; editing not implemented)", kDim, kSmall);
        break;
    }
    default: break;
    }

    if (ui.sub >= 0) {
        const float py = kTop + (ui.tab == kTabItems ? 3 * kLine + 2 : (ui.tab == kTabEquip ? 4 * kLine + 3 : float(numSlots) * kLine + 3));
        Rect(out, kLeft - 3, py - 4, kScreenW - 2 * kLeft + 6, 1.5f, 0xC0, 0xA8, 0x70, 0xE0);
        std::vector<ListRow> rows;
        for (const Entry& e : ui.subEntries) rows.push_back({e.name, e.itemId});
        if (rows.empty()) Text(out, kLeft, py, "(nothing fits)", kDim);
        else DrawRows(out, rows, ui.subCursor, py, int((kStatusY - py) / kLine) - 1, ui.tab != kTabItems);
        Text(out, kLeft, kStatusY, "Up/Down   A choose   B back", kDim, kSmall);
    } else {
        Text(out, kLeft, kStatusY, "L/R tab   A select   Select close", kDim, kSmall);
    }
    if (!ui.status.empty() && std::chrono::steady_clock::now() - ui.statusAt < std::chrono::seconds(3))
        Text(out, kLeft, kStatusY - kLine + 2, ui.status, ui.statusBad ? kBad : kGood, kSmall);
}

// ---------------------------------------------------------------- character creation

const char* const kBodies[2] = {"Body A", "Body B"};
constexpr int kMaxName = 7;
const char* const kGridRows[4] = {
    "ABCDEFGHIJKLM",
    "NOPQRSTUVWXYZ",
    "abcdefghijklm",
    "nopqrstuvwxyz",
};
const char* const kGridRow4 = "0123456789 -'";   // then DEL, OK

enum CreateRow { kRowName = 0, kRowTribe, kRowBody, kRowLook, kRowTrade, kRowMonth, kRowDay, kRowFinish, kRowCancel, kRowCount };
enum Step { kIdle = -1, kStepName = 0, kStepType, kStepTrade, kStepBirthday, kStepFavourite, kStepEnd, kStepDone };

struct CreateUi {
    bool active = false;
    int row = 0;
    std::string name;
    bool grid = false;
    int gx = 0, gy = 0;
    int tribe = 0, body = 0, look = 0, trade = 0, month = 1, day = 1;
    int step = kIdle;
    std::chrono::steady_clock::time_point sentAt{};
    std::string status;
    bool statusBad = false;
    uint16_t prevKeys = 0;
};
CreateUi g_create[4];

int GridRowLen(int row) { return row < 4 ? 13 : 15; }   // row 4 = 13 chars + DEL + OK

void SendNamePackets(uint32_t port, const std::string& name) {
    uint8_t buf[16] = {0};
    for (size_t i = 0; i < name.size() && i < 15; ++i) buf[i] = uint8_t(name[i]);
    const uint16_t crc = Crc16(buf, 16);
    FfccGbaScreens::SendWord(port, Word(0x1C, uint8_t(crc >> 8), uint8_t(crc & 0xFF), buf[0]));
    for (int k = 1; k <= 5; ++k) {
        const int i = k * 3 - 2;
        FfccGbaScreens::SendWord(port, Word(0x5C, buf[i], buf[i + 1], buf[i + 2]));
    }
    RT_LOG(RT_TAG_OS) << "gba screen: port " << port << " name '" << name << "' crc 0x" << std::hex << crc << std::dec << std::endl;
}

void StartStep(uint32_t port, CreateUi& ui, int step) {
    ui.step = step;
    ui.sentAt = std::chrono::steady_clock::now();
    ui.statusBad = false;
    switch (step) {
    case kStepName: SendNamePackets(port, ui.name); ui.status = "Checking name..."; break;
    case kStepType: {
        const uint8_t type = uint8_t((ui.tribe & 3) | ((ui.look & 3) << 2) | ((ui.body & 1) << 7));
        FfccGbaScreens::SendWord(port, Word(0x14, 0x02, type, 0x00));
        ui.status = "Checking look...";
        break;
    }
    case kStepTrade: FfccGbaScreens::SendWord(port, Word(0x14, 0x03, uint8_t(ui.trade), 0x00)); ui.status = "Checking trade..."; break;
    case kStepBirthday: FfccGbaScreens::SendWord(port, Word(0x14, 0x06, uint8_t(ui.month), uint8_t(ui.day))); ui.status = "Sending birthday..."; break;
    case kStepFavourite: {
        const uint8_t fav[4] = {0x55, 0x55, 0x55, 0x55};   // eight neutral ratings (5)
        const uint16_t crc = Crc16(fav, 4);
        FfccGbaScreens::SendWord(port, Word(0x1D, uint8_t(crc >> 8), uint8_t(crc & 0xFF), fav[0]));
        FfccGbaScreens::SendWord(port, Word(0x5D, fav[1], fav[2], fav[3]));
        ui.status = "Sending favourites...";
        break;
    }
    case kStepEnd:
        FfccGbaScreens::SendWord(port, Word(0x14, 0x05, 0x00, 0x00));
        ui.step = kStepDone;
        ui.status = "Character sent to the GameCube";
        break;
    default: break;
    }
}

void AdvanceSteps(uint32_t port, CreateUi& ui) {
    if (ui.step == kIdle || ui.step == kStepDone) return;
    bool ok = false; uint8_t code = 0;
    if (FfccGbaScreens::TakeResult(port, ok, code)) {
        if (!ok) {
            static const char* const kWhy[] = {"Name rejected: already used or reserved", "Look rejected: another player has it",
                                               "Trade rejected: another player has it", "Birthday rejected", "Favourites rejected"};
            ui.status = kWhy[ui.step < 5 ? ui.step : 4];
            ui.statusBad = true;
            ui.step = kIdle;
            return;
        }
        StartStep(port, ui, ui.step + 1);
        return;
    }
    if (std::chrono::steady_clock::now() - ui.sentAt > std::chrono::seconds(5)) {
        ui.status = "No answer from the GameCube";
        ui.statusBad = true;
        ui.step = kIdle;
    }
}

void RunCreate(uint32_t port, Screen& out) {
    g_port = port;
    CreateUi& ui = g_create[port];
    if (!ui.active) { ui = CreateUi{}; ui.active = true; ui.prevKeys = FfccGbaReadKeys(port); }

    const uint16_t keys = FfccGbaReadKeys(port);
    const uint16_t pressed = keys & ~ui.prevKeys;
    ui.prevKeys = keys;

    AdvanceSteps(port, ui);
    const bool busy = (ui.step != kIdle);

    if (!busy && ui.grid) {
        const int len = GridRowLen(ui.gy);
        if (pressed & kKeyLeft)  ui.gx = (ui.gx + len - 1) % len;
        if (pressed & kKeyRight) ui.gx = (ui.gx + 1) % len;
        if (pressed & kKeyUp)    { ui.gy = (ui.gy + 4) % 5; if (ui.gx >= GridRowLen(ui.gy)) ui.gx = GridRowLen(ui.gy) - 1; }
        if (pressed & kKeyDown)  { ui.gy = (ui.gy + 1) % 5; if (ui.gx >= GridRowLen(ui.gy)) ui.gx = GridRowLen(ui.gy) - 1; }
        if (pressed & kKeyB) { if (!ui.name.empty()) ui.name.pop_back(); else ui.grid = false; }
        if (pressed & kKeyA) {
            if (ui.gy == 4 && ui.gx == 13) { if (!ui.name.empty()) ui.name.pop_back(); }
            else if (ui.gy == 4 && ui.gx == 14) ui.grid = false;
            else {
                const char c = (ui.gy < 4) ? kGridRows[ui.gy][ui.gx] : kGridRow4[ui.gx];
                if (int(ui.name.size()) < kMaxName) ui.name.push_back(c);
            }
        }
    } else if (!busy) {
        if (pressed & kKeyUp)   ui.row = (ui.row + kRowCount - 1) % kRowCount;
        if (pressed & kKeyDown) ui.row = (ui.row + 1) % kRowCount;
        const int dir = (pressed & kKeyRight) ? 1 : (pressed & kKeyLeft) ? -1 : 0;
        if (dir != 0) {
            switch (ui.row) {
            case kRowTribe: ui.tribe = (ui.tribe + 4 + dir) % 4; break;
            case kRowBody:  ui.body = (ui.body + 2 + dir) % 2; break;
            case kRowLook:  ui.look = (ui.look + 4 + dir) % 4; break;
            case kRowTrade: ui.trade = (ui.trade + 8 + dir) % 8; break;
            case kRowMonth: ui.month = ((ui.month - 1 + 12 + dir) % 12) + 1; break;
            case kRowDay:   ui.day = ((ui.day - 1 + 31 + dir) % 31) + 1; break;
            default: break;
            }
        }
        if (pressed & kKeyA) {
            if (ui.row == kRowName) { ui.grid = true; }
            else if (ui.row == kRowFinish) {
                if (ui.name.empty()) { ui.status = "Enter a name first"; ui.statusBad = true; }
                else StartStep(port, ui, kStepName);
            } else if (ui.row == kRowCancel) {
                FfccGbaScreens::SendWord(port, Word(0x14, 0x04, 0x00, 0x00));
                ui.status = "Cancelled"; ui.statusBad = false;
            }
        }
    }

    out.visible = true;
    out.title = "PLAYER " + std::to_string(port + 1) + "  NEW CHARACTER";
    { const FfccGbaScreens::PlayerRgb c = FfccGbaScreens::PlayerColor(port); Rect(out, 0, kTop - 6, kScreenW, 1.0f, c.r, c.g, c.b, 0xD0); }
    Text(out, kLeft, 4, "New Character", kHeadInk, kSmall);

    const float labelX = kLeft, valueX = kLeft + 40;
    float y = kTop;
    auto row = [&](int r, const char* label, const std::string& value) {
        const bool sel = (!ui.grid && r == ui.row);
        if (sel) Highlight(out, y);
        Text(out, labelX, y, label, sel ? kSel : kDim);
        Text(out, valueX, y, value, sel ? kSel : kInk);
        y += kLine;
    };
    row(kRowName, "Name", ui.name.empty() ? std::string("(press A)") : ui.name + (ui.grid ? "_" : ""));
    row(kRowTribe, "Tribe", kTribeNames[ui.tribe]);
    row(kRowBody, "Body", kBodies[ui.body]);
    row(kRowLook, "Look", std::to_string(ui.look + 1));
    row(kRowTrade, "Trade", JobName(ui.trade));
    row(kRowMonth, "Month", std::to_string(ui.month));
    row(kRowDay, "Day", std::to_string(ui.day));
    row(kRowFinish, "Finish", "");
    row(kRowCancel, "Cancel", "");

    if (ui.grid) {
        // letter grid on the right half
        const float gx0 = 92.0f, gy0 = kTop, cw = 8.0f, ch = 10.5f;
        Rect(out, gx0 - 4, gy0 - 3, kScreenW - gx0 - 2, 5 * ch + 6, 0x18, 0x1C, 0x30, 0xD0);
        for (int yy = 0; yy < 5; ++yy) {
            const int len = GridRowLen(yy);
            for (int xx = 0; xx < len; ++xx) {
                const bool sel = (yy == ui.gy && xx == ui.gx);
                std::string cell;
                if (yy < 4) cell = std::string(1, kGridRows[yy][xx]);
                else if (xx < 13) cell = std::string(1, kGridRow4[xx]);
                else cell = (xx == 13) ? "DEL" : "OK";
                const float cx = gx0 + float(xx) * cw + (xx >= 13 ? float(xx - 13) * 8.0f : 0.0f);
                if (sel) Rect(out, cx - 1, gy0 + float(yy) * ch - 1, (cell.size() > 1 ? 16.0f : cw), ch - 1, 0x8A, 0x64, 0x1E, 0xE0);
                Text(out, cx, gy0 + float(yy) * ch, cell, sel ? kSel : kInk, kSmall);
            }
        }
        Text(out, kLeft, kStatusY, "D-pad  A add  B delete  OK done", kDim, kSmall);
    } else {
        Text(out, kLeft, kStatusY, "Up/Down row  Left/Right change  A select", kDim, kSmall);
    }
    if (!ui.status.empty()) Text(out, kLeft, kStatusY - kLine + 2, ui.status, ui.statusBad ? kBad : kGood, kSmall);
}

// ---------------------------------------------------------------- ImGui fallback renderer

void DrawFallback(uint32_t port, const Screen& s) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sx = vp->Size.x / 640.0f, sy = vp->Size.y / 448.0f;
    const float ox = vp->Pos.x + ((port % 2 == 0) ? 26.0f : 640.0f - 26.0f - kScreenW) * sx;
    const float oy = vp->Pos.y + ((port < 2) ? 26.0f : 448.0f - 26.0f - kScreenH) * sy;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(ox - 10 * sx, oy - 10 * sy), ImVec2(ox + (kScreenW + 10) * sx, oy + (kScreenH + 10) * sy), IM_COL32(0xC8, 0xA8, 0x60, 0xE0), 4.0f);
    dl->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + kScreenW * sx, oy + kScreenH * sy), IM_COL32(0x10, 0x14, 0x24, 0xD8));
    for (const Op& op : s.ops) {
        const ImVec2 p(ox + op.x * sx, oy + op.y * sy);
        switch (op.kind) {
        case Op::Rect: dl->AddRectFilled(p, ImVec2(p.x + op.w * sx, p.y + op.h * sy), IM_COL32(op.r, op.g, op.b, op.a)); break;
        case Op::Text: dl->AddText(nullptr, 20.0f * op.scale * sy, p, IM_COL32(op.r, op.g, op.b, 0xFF), op.text.c_str()); break;
        case Op::Icon: dl->AddRect(p, ImVec2(p.x + 16 * sx, p.y + 16 * sy), IM_COL32(0x80, 0x80, 0x90, 0xFF)); break;
        }
    }
}

} // namespace

namespace FfccGbaScreens {

const Screen& ScreenFor(uint32_t port) { static const Screen kNone; return port < 4 ? g_screens[port] : kNone; }
void SetNativeRenderer(bool active) { g_native = active; }
bool NativeRenderer() { return g_native; }

// Host-side blit of the emulated client's screen: this overlay runs once per presented frame at the
// display cadence, while the game itself renders one frame per two retraces on PAL (CGraphic::Flip
// waits for two), so a screen drawn inside the game's frame refreshed at 25 Hz and read as slow.
// WIICOMPILED_GBA_HOSTBLIT=1 selects it; the in-game GX blit is the default.
bool HostBlit() { static int v = -1; if (v < 0) { const char* e = std::getenv("WIICOMPILED_GBA_HOSTBLIT"); v = (e && *e == '1') ? 1 : 0; } return v == 1; }   // default off: the overlay is presented one game frame late (measured 25 presents/s, +1 frame)
static void DrawClientFrame(uint32_t port) {
    static ImTextureID s_tex[4] = {};
    static bool s_have[4] = {false, false, false, false};
    static std::vector<uint32_t> s_rgba(FfccMgba::kWidth * FfccMgba::kHeight, 0xFF000000u);
    const uint32_t* frame = FfccMgba::Frame(port);
    if (!frame) return;
    for (uint32_t i = 0; i < FfccMgba::kWidth * FfccMgba::kHeight; ++i) s_rgba[i] = frame[i] | 0xFF000000u;   // XBGR8 in memory is R,G,B,X
    if (!s_have[port]) { s_tex[port] = aurora_imgui_add_texture(FfccMgba::kWidth, FfccMgba::kHeight, s_rgba.data()); s_have[port] = true; }
    else aurora_imgui_update_texture(s_tex[port], s_rgba.data());
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sx = vp->Size.x / 640.0f, sy = vp->Size.y / 448.0f;
    const float w = 240.0f, h = 160.0f, pad = 6.0f;
    const float x = (port % 2 == 0) ? 20.0f : 640.0f - 20.0f - w;
    const float y = (port < 2) ? 20.0f : 448.0f - 20.0f - h;
    const PlayerRgb c = PlayerColor(port);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(vp->Pos.x + (x - pad) * sx, vp->Pos.y + (y - pad) * sy), ImVec2(vp->Pos.x + (x + w + pad) * sx, vp->Pos.y + (y + h + pad) * sy), IM_COL32(c.r / 3, c.g / 3, c.b / 3, 0xF0), 6.0f * sx);
    dl->AddImage(s_tex[port], ImVec2(vp->Pos.x + x * sx, vp->Pos.y + y * sy), ImVec2(vp->Pos.x + (x + w) * sx, vp->Pos.y + (y + h) * sy));
}
void Draw() {
    for (uint32_t port = 0; port < 4; ++port) {
        Screen& s = g_screens[port];
        s.visible = false;
        s.ops.clear();
        if (FfccMgba::Active(port)) { if (HostBlit() && FfccGbaClientShown(port)) DrawClientFrame(port); continue; }   // the real client draws its own screens
        if (ModeType(port) == 1) RunCreate(port, s);
        else {
            g_create[port].active = false;
            RunMenu(port, s);
        }
        if (s.visible && !g_native) DrawFallback(port, s);
    }
}

} // namespace FfccGbaScreens

#endif // RECOMP_PROJECT_FFCC
