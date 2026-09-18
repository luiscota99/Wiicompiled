// Draw the GBA personal screens with the game's own menu renderer: the host-to-guest call bridge.
//
// CMenuPcs::draw (p_menu.cpp, 0x8009631C) runs every frame after the 3D scene: it sets the 2D
// orthographic state (640x448, blend, no Z) and draws the HUD (drawBattle), the village menu, the
// world map or the bonus screen, then the PAUSE overlay. Hooking it and calling the game's own
// primitives AFTER the original body leaves the 2D state intact, so anything drawn here composes
// with the frame exactly like the game's menus (same font, same textures, same copy-out).
//
// Bridge: the hook runs on the guest thread with its CpuContext; a translated function is called
// by loading its argument registers and using InvokeIndirectCpu (abi_bridge.h), the same path the
// runtime uses for guest callbacks (ffcc_aram.cpp InvokeGuest). Pointer arguments must be guest
// addresses: strings and structs are staged in MEM2 (0x90000000+), which a GameCube game never
// touches (the runtime maps 128 MB there: "MEM2 defaults active"). Immediate-mode vertices go
// through the runtime's own FIFO writer (memory_access.h), as gx_egg.cpp does.
//
// Game primitives used (FFCC-Decomp, PAL; EABI: ints r3.., floats f1.., structs by value are
// passed as pointers to a copy, verified in build/GCCP01/asm/MenuUtil.s at 0x80177034):
//   CMenuPcs::DrawFont(int x, int y, _GXColor c, int tlut, char* text, float scale, float margin)
//     0x8017AD50 (MenuUtil.cpp:1508): r3 this, r4 x, r5 y, r6 &colour, r7 tlut, r8 text, f1, f2.
//   CMenuPcs::DrawSingleIcon(int iconNo, int x, int y, float alpha, int rawIcon, float uvScale)
//     0x80147728 (singmenu.cpp): r4 item id, r5 x, r6 y, r7 rawIcon (0 = look up the item's icon
//     in gSingMenuItemIconByType), f1 alpha, f2 quad scale (32 px * scale).
//   CMenuPcs::SetAttrFmt(FMT) 0x80095F58 (2 = position only), CMenuPcs::SetTexture(TEX)
//     0x80095BD0 (-1 = no texture: TEV passes the material colour), GXSetChanMatColor 0x801A3974
//     (GX_COLOR0A0 = 4, pointer to colour), _GXSetBlendMode 0x8017277C, GXBegin 0x8016F0F0 as the
//     host override GX__Begin_8016f0f0; this is CMenuPcs::DrawFilter's technique (wm_menu.cpp)
//     with a sized quad instead of the full screen.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "runtime_log.h"
#include "memory.h"
#include "memory_access.h"
#include "ffcc_gba_screens.h"
#include "ffcc_gba_mgba.h"
#include "gx_guest_write.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(RECOMP_PROJECT_FFCC)

extern "C" void func_8009631C(CpuContext* ctx);   // CMenuPcs::draw, translated body
extern "C" void GX__Begin_8016f0f0(uint32_t t, uint32_t vf, uint32_t nv);
extern "C" uint16_t FfccGbaReadKeys(uint32_t chan);
extern "C" uint16_t FfccGbaClientKeys(uint32_t chan);

namespace FfccGbaScreens {
bool MenuOpen(uint32_t chan);
uint32_t ModeType(uint32_t chan);
bool TakeToggleRequest(uint32_t chan);
}
namespace {
// The handheld's screen is shown while the player is in its menu (control mode 1 or the pause menu) or
// creating a character; in the field it stays hidden like a handheld resting in the player's lap.
bool GbaVisible(uint32_t port) {
    return FfccMgba::Active(port) && (FfccGbaScreens::MenuOpen(port) || FfccGbaScreens::ModeType(port) == 1);
}
} // namespace

namespace {
constexpr uint32_t kMenuPcs      = 0x802EA1C0u;   // MenuPcs, p_menu.o
constexpr uint32_t kJoybus       = 0x802EAAC0u;   // Joybus, joybus.o
constexpr uint32_t kChgCtrlMode  = 0x800A78ACu;   // ChgCtrlMode__6JoyBusFi: r3 this, r4 port; queues [09 mode^1]
constexpr uint32_t kDrawFont     = 0x8017AD50u;
constexpr uint32_t kDrawIcon     = 0x80147728u;
constexpr uint32_t kSetAttrFmt   = 0x80095F58u;
constexpr uint32_t kSetTexture   = 0x80095BD0u;
constexpr uint32_t kSetMatColor  = 0x801A3974u;
constexpr uint32_t kSetBlendMode = 0x8017277Cu;
constexpr uint32_t kGxSetZMode   = 0x801A5D84u;   // GXSetZMode(enable, func, update)
constexpr uint32_t kFontSetZMode = 0x80092A68u;   // SetZMode__5CFontFii(compareEnable, updateEnable): CFont::DrawInit re-applies it
constexpr uint32_t kMenuFonts    = 0xF8u;         // offsetof(CMenuPcs, m_fonts)
constexpr uint32_t kMenuMode     = 0x740u;        // offsetof(CMenuPcs, m_mode): 0 field/village, 1 world map (roster), 2 bonus (p_menu.h:885)
// The menu texture slots hold a different set per scene (live: slot 2 is the message window in the
// field but a map tile on the roster) and the font palettes differ too (field text uses TLUT 6,
// the roster's own text TLUT 7, wm_menu.cpp DrawFont2 calls), so both follow m_mode.
uint32_t MenuMode() { uint32_t m = 0; return ::Memory::TryRead32(kMenuPcs + kMenuMode, m) ? m : 0; }
uint32_t TextTlut() { return MenuMode() == 0 ? 6u : 7u; }
// MEM2 scratch for staged arguments and the GBA frames. The runtime parks the DVD file table at the
// top of MEM2 (system_bridge.cpp: g_dvdFstReservedBase, below the IPC and IOS reservations); the
// fixed 0x97DF0000 landed inside it (measured: file-table text where the texture object should be).
// A GameCube game never touches MEM2, so 4 MB below that reservation is free.
extern "C" uint32_t g_dvdFstReservedBase;
uint32_t g_scratch = 0, g_scratchText = 0, g_scratchMtx = 0, g_gbaTexObj = 0, g_gbaImage = 0;
void InitScratch() {
    if (g_scratch != 0) return;
    const uint32_t base = (g_dvdFstReservedBase >= 0x90800000u) ? (g_dvdFstReservedBase - 0x400000u) : 0x97000000u;
    g_scratch = base; g_scratchText = base + 0x100u; g_scratchMtx = base + 0x800u; g_gbaTexObj = base + 0x1000u; g_gbaImage = base + 0x10000u;
    RT_LOG(RT_TAG_OS) << "gba native draw: MEM2 scratch at 0x" << std::hex << base << " (file table at 0x" << g_dvdFstReservedBase << ")" << std::dec << std::endl;
}
constexpr uint32_t kTextMax = 120;
constexpr uint32_t kGxQuads = 0x80, kGxVtxFmt0 = 0, kGxColor0A0 = 4;

bool g_bridgeOk = false;
bool g_bridgeChecked = false;
bool g_attrFmtSet = false;     // SetAttrFmt(2)/SetTexture(-1) are set once per frame before quads
bool g_useNative = true;

void StageBytes(uint32_t addr, const uint8_t* bytes, size_t n) {
    // Memory::Write32 stores a value in guest byte order, so pack four bytes per word in order.
    for (size_t i = 0; i < n; i += 4) {
        uint32_t w = 0;
        for (size_t k = 0; k < 4; ++k) w = (w << 8) | (i + k < n ? bytes[i + k] : 0u);
        ::Memory::Write32(addr + uint32_t(i), w);
    }
}

void StageColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const uint8_t c[4] = {r, g, b, a};
    StageBytes(g_scratch, c, 4);
}

void Call(CpuContext* ctx, uint32_t target) { InvokeIndirectCpu(target, ctx); }

// 2D overlay on top of everything drawn later in the frame: the game's HUD text is drawn with depth
// test ALWAYS and depth WRITE on (CFont::DrawInit, fontman.cpp:522-531: zCompare flag -> enable,
// func 7, update 1), which stamps the near plane so 3D objects rendered afterwards fail their depth
// test behind it. Disabling depth instead let hats and hair paint over the panels (measured).
constexpr uint32_t kGxSetFog = 0x801A59BCu;   // GXSetFog(type, startz, endz, nearz, farz, &color)
// The roster and the field scenes leave linear fog with a black colour enabled (host draw-state dump:
// the only state that differed between the title, where the blit showed, and the roster, where it
// did not). Fog applies to every draw in the frame, so text and the handheld frame were mixed to
// black while the dark plate merely looked dark. The scene sets its fog again every frame.
void FogOff(CpuContext* ctx) {
    StageColor(0, 0, 0, 0);
    ctx->gpr[3] = 0; ctx->gpr[4] = g_scratch;
    ctx->fpr[1].d = 0; ctx->fpr[2].d = 0; ctx->fpr[3].d = 0; ctx->fpr[4].d = 0;
    Call(ctx, kGxSetFog);
}
void ZOff(CpuContext* ctx) { ctx->gpr[3] = 1; ctx->gpr[4] = 7; ctx->gpr[5] = 1; Call(ctx, kGxSetZMode); }
void FontZOff(CpuContext* ctx) {
    uint32_t font = 0;
    if (::Memory::TryRead32(kMenuPcs + kMenuFonts, font) && font >= 0x80000000u && font < 0x81800000u) {
        ctx->gpr[3] = font; ctx->gpr[4] = 1; ctx->gpr[5] = 0; Call(ctx, kFontSetZMode);   // compare flag: ALWAYS + write
    }
}

// Solid quad in the 640x448 menu space (CMenuPcs::DrawFilter technique, sized).
void GameRect(CpuContext* ctx, float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (w <= 0 || h <= 0) return;
    ctx->gpr[3] = kMenuPcs; ctx->gpr[4] = 2; Call(ctx, kSetAttrFmt);               // position only
    ctx->gpr[3] = kMenuPcs; ctx->gpr[4] = 0xFFFFFFFFu; Call(ctx, kSetTexture);      // no texture
    StageColor(r, g, b, a);
    ctx->gpr[3] = kGxColor0A0; ctx->gpr[4] = g_scratch; Call(ctx, kSetMatColor);
    ctx->gpr[3] = 1; ctx->gpr[4] = 4; ctx->gpr[5] = 5; ctx->gpr[6] = 1; Call(ctx, kSetBlendMode);   // blend src alpha
    ZOff(ctx);
    GX__Begin_8016f0f0(kGxQuads, kGxVtxFmt0, 4);
    GX_HLE_FIFO_WriteFloat(x);     GX_HLE_FIFO_WriteFloat(y);     GX_HLE_FIFO_WriteFloat(0.0f);
    GX_HLE_FIFO_WriteFloat(x + w); GX_HLE_FIFO_WriteFloat(y);     GX_HLE_FIFO_WriteFloat(0.0f);
    GX_HLE_FIFO_WriteFloat(x + w); GX_HLE_FIFO_WriteFloat(y + h); GX_HLE_FIFO_WriteFloat(0.0f);
    GX_HLE_FIFO_WriteFloat(x);     GX_HLE_FIFO_WriteFloat(y + h); GX_HLE_FIFO_WriteFloat(0.0f);
    g_attrFmtSet = false;   // DrawFont / DrawSingleIcon reset their own state
}

// A line of text with the game's menu font at (x, y) in the 640x448 menu space.
void GameText(CpuContext* ctx, float x, float y, const std::string& text, uint8_t r, uint8_t g, uint8_t b, float scale) {
    if (text.empty()) return;
    StageColor(r, g, b, 0xFF);
    uint8_t buf[kTextMax + 4] = {0};
    const size_t n = text.size() < kTextMax ? text.size() : kTextMax;
    std::memcpy(buf, text.data(), n);
    StageBytes(g_scratchText, buf, n + 4);
    ctx->gpr[3] = kMenuPcs;
    ctx->gpr[4] = uint32_t(int32_t(x));
    ctx->gpr[5] = uint32_t(int32_t(y));
    ctx->gpr[6] = g_scratch;
    ctx->gpr[7] = TextTlut();   // palette: 6 in the field (HUD names, MenuUtil.s), 7 on the roster
    ctx->gpr[8] = g_scratchText;
    ctx->fpr[1].d = double(scale);
    ctx->fpr[2].d = 0.0;
    Call(ctx, kDrawFont);
}

constexpr uint32_t kIconTexture = 0x25;   // DrawSingleIcon uses m_textures[0x25]; unloaded in multiplayer
constexpr uint32_t g_texturesOff = 0x18C; // offsetof(CMenuPcs, m_textures): m_fonts 0xF8 + 5*4, m_battleMesMenus 12*4,
                                          // m_battleRingMenus 4*4, m_textureSets 16*4 (p_menu.h:878-882; 105*4 ends at 0x330 = m_pad330)
bool IconTextureLoaded() {
    uint32_t p = 0;
    return MenuMode() == 0 && ::Memory::TryRead32(kMenuPcs + g_texturesOff + kIconTexture * 4u, p) && p >= 0x80000000u && p < 0x81800000u;
}
void GameIcon(CpuContext* ctx, float x, float y, int itemId, float scale) {
    if (itemId <= 0 || !IconTextureLoaded()) return;
    ctx->gpr[3] = kMenuPcs;
    ctx->gpr[4] = uint32_t(itemId);
    ctx->gpr[5] = uint32_t(int32_t(x));
    ctx->gpr[6] = uint32_t(int32_t(y));
    ctx->gpr[7] = 0;
    ctx->fpr[1].d = 1.0;
    ctx->fpr[2].d = double(scale);
    Call(ctx, kDrawIcon);
}


// Opaque rounded plate: horizontal strips whose inset follows a circle in the corner bands, so the
// corners are round at any resolution without a texture (radius r, one strip per unit).
void RoundedPlate(CpuContext* ctx, float x, float y, float w, float h, float r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    const int steps = int(r);
    for (int i = 0; i < steps; ++i) {
        // row i (from the top edge) is at distance (r - i - 0.5) from the corner centre line
        const float dy = r - float(i) - 0.5f;
        const float inset = r - std::sqrt(std::max(0.0f, r * r - dy * dy));
        GameRect(ctx, x + inset, y + float(i), w - 2 * inset, 1.0f, cr, cg, cb, ca);                 // top band
        GameRect(ctx, x + inset, y + h - float(i) - 1.0f, w - 2 * inset, 1.0f, cr, cg, cb, ca);      // bottom band
    }
    GameRect(ctx, x, y + r, w, h - 2 * r, cr, cg, cb, ca);                                           // middle
}

// The game's own message window (CMenuPcs::DrawWindow with texture set 2, the nine-slice box the
// field messages and town menus use, mesmenu.cpp:523) around the screen; falls back to a dark
// translucent panel with a gold rim when that texture set is not resident.
constexpr uint32_t kSetColor   = 0x80094BBCu;   // SetColor__8CMenuPcsFR6CColor: r4 = &CColor{r,g,b,a}
constexpr uint32_t kDrawWindow = 0x80094BECu;   // DrawWindow__8CMenuPcsFffffQ28CMenuPcs3TEXf: f1..f4 box, r4 tex, f5 corner
constexpr uint32_t kWindowTex  = 2;
bool WindowTextureLoaded() {
    uint32_t p = 0;
    return MenuMode() == 0 && ::Memory::TryRead32(kMenuPcs + g_texturesOff + kWindowTex * 4u, p) && p >= 0x80000000u && p < 0x81800000u;
}
void DrawFrame(CpuContext* ctx, float ox, float oy, uint32_t port) {
    using namespace FfccGbaScreens;
    const float pad = 9.0f, corner = 16.0f, radius = 11.0f;
    const PlayerRgb c = PlayerColor(port);
    // Opaque rounded plate first (the window texture is translucent and its corner tiles are nearly
    // square, measured), dark and leaning to the player's colour so the scene is fully covered.
    RoundedPlate(ctx, ox - pad + 1, oy - pad + 1, kScreenW + 2 * pad - 2, kScreenH + 2 * pad - 2, radius,
                 uint8_t(0x14 + c.r / 6), uint8_t(0x12 + c.g / 6), uint8_t(0x18 + c.b / 6), 0xFA);
    if (WindowTextureLoaded()) {
        // The game's own window on top, tinted lightly with the player's colour (SetColor multiplies
        // the texture). SetAttrFmt(0) first: DrawWindow writes pos + texcoord vertices and a stale
        // descriptor desyncs the GX stream (measured).
        ctx->gpr[3] = kMenuPcs; ctx->gpr[4] = 0; Call(ctx, kSetAttrFmt);
        ctx->gpr[3] = 1; ctx->gpr[4] = 4; ctx->gpr[5] = 5; ctx->gpr[6] = 1; Call(ctx, kSetBlendMode);
        ZOff(ctx);
        StageColor(uint8_t(0xB0 + c.r * 5 / 16), uint8_t(0xB0 + c.g * 5 / 16), uint8_t(0xB0 + c.b * 5 / 16), 0xFF);
        ctx->gpr[3] = kMenuPcs; ctx->gpr[4] = g_scratch; Call(ctx, kSetColor);
        ctx->gpr[3] = kMenuPcs; ctx->gpr[4] = kWindowTex;
        ctx->fpr[1].d = double(ox - pad); ctx->fpr[2].d = double(oy - pad);
        ctx->fpr[3].d = double(kScreenW + 2 * pad); ctx->fpr[4].d = double(kScreenH + 2 * pad);
        ctx->fpr[5].d = double(corner);
        Call(ctx, kDrawWindow);
    } else {
        RoundedPlate(ctx, ox - pad - 1, oy - pad - 1, kScreenW + 2 * pad + 2, kScreenH + 2 * pad + 2, radius + 1, c.r, c.g, c.b, 0xE0);
        RoundedPlate(ctx, ox - pad, oy - pad, kScreenW + 2 * pad, kScreenH + 2 * pad, radius, 0x0C, 0x10, 0x20, 0xF8);
    }
}

void DrawScreen(CpuContext* ctx, uint32_t port, const FfccGbaScreens::Screen& s) {
    using namespace FfccGbaScreens;
    // Ports 1/2 sit over their own HUD corner (top-left / top-right); ports 3/4 use the bottom corners.
    // The HUD (hearts, name ribbons, the Ready pill down to y ~125) is drawn by the game AFTER this
    // hook and would paint over the panel, so ports 1/2 sit just below their HUD; ports 3/4 bottom.
    // Top corners over the player's own HUD (ports 3/4: bottom corners); the opaque window covers it.
    const float ox = (port % 2 == 0) ? 26.0f : 640.0f - 26.0f - kScreenW;
    const float oy = (port < 2) ? 26.0f : 448.0f - 26.0f - kScreenH;
    DrawFrame(ctx, ox, oy, port);
    for (const Op& op : s.ops) {
        switch (op.kind) {
        case Op::Rect: GameRect(ctx, ox + op.x, oy + op.y, op.w, op.h, op.r, op.g, op.b, op.a); break;
        case Op::Text: GameText(ctx, ox + op.x, oy + op.y, op.text, op.r, op.g, op.b, op.scale); break;
        case Op::Icon: GameIcon(ctx, ox + op.x, oy + op.y, op.itemId, op.scale); break;
        }
    }
}

// ---------------------------------------------------------------- emulated GBA frames
// The mGBA frame (240x160 XRGB8) becomes a GX RGB565 texture in MEM2 (4x4 tiles, big-endian
// texels) and is drawn with the SDK's own texture and TEV setup through the bridge:
//   GXInitTexObj 0x801A3D9C (obj, image, w, h, fmt, wrapS, wrapT, mipmap), GXLoadTexObj 0x801A43C0,
//   GXSetTevOp 0x801A51D8 (stage, GX_REPLACE = 3), GXSetTevOrder 0x801A57B4, GXSetNumTevStages
//   0x801A598C, GXSetNumTexGens 0x801A1C40, GXSetTexCoordGen2 0x801A1970, GXInvalidateTexAll
//   0x801A4660 (the runtime's texture cache is keyed on guest memory generations: NotifyWrite).
constexpr uint32_t kGxInitTexObj = 0x801A3D9Cu, kGxLoadTexObj = 0x801A43C0u, kGxSetTevOp = 0x801A51D8u;
constexpr uint32_t kGxSetTevOrder = 0x801A57B4u, kGxSetNumTevStages = 0x801A598Cu, kGxSetNumTexGens = 0x801A1C40u;
constexpr uint32_t kGxSetTexCoordGen2 = 0x801A1970u, kGxInvalidateTexAll = 0x801A4660u;
constexpr uint32_t kGbaImageStride = 0x20000u;
constexpr uint32_t kGxTfRgb565 = 4, kGxClamp = 0, kGxReplace = 3, kGxTgMtx2x4 = 1, kGxTgTex0 = 4, kGxIdentity = 60, kGxPtIdentity = 125;

bool UploadGbaFrame(uint32_t port) {
    const uint32_t* px = FfccMgba::Frame(port);
    if (!px) return false;
    const uint32_t addr = g_gbaImage + port * kGbaImageStride;
    const uint32_t bytes = FfccMgba::kWidth * FfccMgba::kHeight * 2;
    // Build the tiled image on the host, then store it with ordinary guest writes: GetPointer does
    // not cover this MEM2 range (measured: no frame drawn), and Write32 keeps the texture cache's
    // generation tracking honest.
    static std::vector<uint8_t> tiled;
    tiled.assign(bytes, 0);
    uint8_t* dst = tiled.data();
    const uint32_t tilesPerRow = FfccMgba::kWidth / 4;
    for (uint32_t y = 0; y < FfccMgba::kHeight; ++y) {
        const uint32_t* row = px + y * FfccMgba::kWidth;
        for (uint32_t x = 0; x < FfccMgba::kWidth; ++x) {
            const uint32_t c = row[x];
            // mGBA's 32-bit output is XBGR8 (mCOLOR_NATIVE, mgba-util/image.h): red in the low byte.
            const uint16_t v = uint16_t(((c >> 3) & 0x1F) << 11) | uint16_t(((c >> 10) & 0x3F) << 5) | uint16_t((c >> 19) & 0x1F);
            const uint32_t off = (((y >> 2) * tilesPerRow + (x >> 2)) * 16u + (y & 3u) * 4u + (x & 3u)) * 2u;
            dst[off] = uint8_t(v >> 8);
            dst[off + 1] = uint8_t(v);
        }
    }
    for (uint32_t i = 0; i < bytes; i += 4) {
        const uint32_t w = (uint32_t(dst[i]) << 24) | (uint32_t(dst[i + 1]) << 16) | (uint32_t(dst[i + 2]) << 8) | uint32_t(dst[i + 3]);
        if (!::Memory::TryWrite32(addr + i, w)) { static bool s_once = false; if (!s_once) { s_once = true; RT_LOG(RT_TAG_OS) << "gba native draw: MEM2 frame write failed at 0x" << std::hex << (addr + i) << std::dec << std::endl; } return false; }
    }
    GxGuestWrite::NotifyWrite(addr, bytes);
    { static unsigned s_n = 0; if (s_n < 3) { ++s_n; RT_LOG(RT_TAG_OS) << "gba native draw: uploaded GBA frame for port " << port << " to 0x" << std::hex << addr << std::dec << std::endl; } }
    return true;
}

constexpr uint32_t kGxLoadTexMtxImm = 0x801A6624u, kDrawRect = 0x800958FCu;   // DrawRect__8CMenuPcsFUlfffffffff
constexpr uint32_t kGxSetNumIndStages = 0x801A4FD0u, kGxSetTevDirect = 0x801A4FF8u, kGxSetTevSwapMode = 0x801A55F0u;
constexpr uint32_t kGxSetTevSwapModeTable = 0x801A5644u, kGxSetAlphaCompare = 0x801A56DCu, kGxSetCullMode = 0x801A2728u;
constexpr uint32_t kGxSetZCompLoc = 0x801A5DB8u, kGxSetNumChans = 0x801A3A68u;
constexpr uint32_t kGxTexMtx0 = 30, kGxModulate = 0;

void StageFloat(uint32_t addr, float v) { uint32_t u; std::memcpy(&u, &v, 4); ::Memory::Write32(addr, u); }

// The game's own CMenuPcs::DrawRect (pixel UVs, position + texcoord vertices) with our texture bound
// the way CMenuPcs::SetTexture binds one: texture matrix 1/w, 1/h on GX_TEXMTX0, one texgen, one
// modulate TEV stage. Raw FIFO vertices with the pos+tex format never rendered (measured: a plain
// coloured quad through that path stayed invisible while DrawRect drew fine in the same spot).
void DrawGbaFrame(CpuContext* ctx, uint32_t port, float x, float y, float w, float h) {
    const uint32_t obj = g_gbaTexObj + port * 0x40u;
    const uint32_t img = g_gbaImage + port * kGbaImageStride;
    ctx->gpr[3] = obj; ctx->gpr[4] = img; ctx->gpr[5] = FfccMgba::kWidth; ctx->gpr[6] = FfccMgba::kHeight;
    ctx->gpr[7] = kGxTfRgb565; ctx->gpr[8] = kGxClamp; ctx->gpr[9] = kGxClamp; ctx->gpr[10] = 0;
    Call(ctx, kGxInitTexObj);
    Call(ctx, kGxInvalidateTexAll);
    ctx->gpr[3] = obj; ctx->gpr[4] = 0; Call(ctx, kGxLoadTexObj);                       // GX_TEXMAP0
    // 3x4 texture matrix, row-major floats: scale texel UVs to 0..1
    const float m[12] = {1.0f / FfccMgba::kWidth, 0, 0, 0,  0, 1.0f / FfccMgba::kHeight, 0, 0,  0, 0, 1, 0};
    for (uint32_t i = 0; i < 12; ++i) StageFloat(g_scratchMtx + i * 4u, m[i]);
    ctx->gpr[3] = g_scratchMtx; ctx->gpr[4] = kGxTexMtx0; ctx->gpr[5] = kGxTgMtx2x4; Call(ctx, kGxLoadTexMtxImm);
    ctx->gpr[3] = 1; Call(ctx, kGxSetNumTexGens);
    ctx->gpr[3] = 0; ctx->gpr[4] = kGxTgMtx2x4; ctx->gpr[5] = kGxTgTex0; ctx->gpr[6] = kGxTexMtx0; ctx->gpr[7] = 0; ctx->gpr[8] = kGxPtIdentity;
    Call(ctx, kGxSetTexCoordGen2);
    // Everything CMenuPcs::draw sets at frame start plus what TextureMan::SetTextureTev sets per bind:
    // the roster's 3D models run after that setup and leave indirect stages, swap tables and alpha
    // compare behind (measured: the same blit rendered at the title and not on the roster).
    ctx->gpr[3] = 1; Call(ctx, kGxSetNumChans);
    ctx->gpr[3] = 0; Call(ctx, kGxSetZCompLoc);
    ctx->gpr[3] = 0; Call(ctx, kGxSetCullMode);
    ctx->gpr[3] = 6; ctx->gpr[4] = 1; ctx->gpr[5] = 0; ctx->gpr[6] = 7; ctx->gpr[7] = 0; Call(ctx, kGxSetAlphaCompare);   // GEQUAL 1 AND ALWAYS 0
    ctx->gpr[3] = 0; Call(ctx, kGxSetNumIndStages);
    ctx->gpr[3] = 1; Call(ctx, kGxSetNumTevStages);
    ctx->gpr[3] = 0; Call(ctx, kGxSetTevDirect);
    ctx->gpr[3] = 0; ctx->gpr[4] = 0; ctx->gpr[5] = 1; ctx->gpr[6] = 2; ctx->gpr[7] = 3; Call(ctx, kGxSetTevSwapModeTable);   // SWAP0 = R G B A
    ctx->gpr[3] = 0; ctx->gpr[4] = 0; ctx->gpr[5] = 0; Call(ctx, kGxSetTevSwapMode);
    ctx->gpr[3] = 0; ctx->gpr[4] = 0; ctx->gpr[5] = 0; ctx->gpr[6] = kGxColor0A0; Call(ctx, kGxSetTevOrder);
    ctx->gpr[3] = 0; ctx->gpr[4] = kGxModulate; Call(ctx, kGxSetTevOp);
    ctx->gpr[3] = kMenuPcs; ctx->gpr[4] = 0; Call(ctx, kSetAttrFmt);                     // pos + texcoord, F32
    ctx->gpr[3] = 1; ctx->gpr[4] = 4; ctx->gpr[5] = 5; ctx->gpr[6] = 1; Call(ctx, kSetBlendMode);
    ZOff(ctx);
    StageColor(0xFF, 0xFF, 0xFF, 0xFF); ctx->gpr[3] = kMenuPcs; ctx->gpr[4] = g_scratch; Call(ctx, kSetColor);
    ctx->gpr[3] = kMenuPcs; ctx->gpr[4] = 0;
    ctx->fpr[1].d = x; ctx->fpr[2].d = y; ctx->fpr[3].d = w; ctx->fpr[4].d = h;
    ctx->fpr[5].d = 0; ctx->fpr[6].d = 0; ctx->fpr[7].d = 1; ctx->fpr[8].d = 1; ctx->fpr[9].d = 0;
    Call(ctx, kDrawRect);
}

} // namespace

extern "C" void draw_gba_native_8009631c(CpuContext* ctx) {
    func_8009631C(ctx);
    InitScratch();
    if (!g_bridgeChecked) {
        g_bridgeChecked = true;
        const bool fontOk = TranslatedFunctionRegistry::FindByAddressPtr(kDrawFont) != nullptr;
        const bool iconOk = TranslatedFunctionRegistry::FindByAddressPtr(kDrawIcon) != nullptr;
        uint32_t probe = 0;
        const bool mem2 = ::Memory::TryWrite32(g_scratch, 0x12345678u) && ::Memory::TryRead32(g_scratch, probe) && probe == 0x12345678u;
        if (const char* env = std::getenv("WIICOMPILED_GBA_IMGUI")) g_useNative = (env[0] == '0' || env[0] == 0);
        g_bridgeOk = fontOk && iconOk && mem2 && g_useNative;
        FfccGbaScreens::SetNativeRenderer(g_bridgeOk);
        RT_LOG(RT_TAG_OS) << "gba native draw: bridge " << (g_bridgeOk ? "ready" : "unavailable")
                          << " (DrawFont=" << fontOk << " DrawSingleIcon=" << iconOk << " MEM2=" << mem2
                          << " native=" << g_useNative << ")" << std::endl;
    }
    if (!g_bridgeOk) return;
    // Prime the 2D state once per frame before ANY of our quads: the first quads drawn straight after
    // the game's menu code are invisible until a font draw has set the state (measured twice).
    bool primed = false;
    for (uint32_t port = 0; port < 4; ++port) if (GbaVisible(port) || FfccGbaScreens::ScreenFor(port).visible) { FogOff(ctx); FontZOff(ctx); GameText(ctx, -200.0f, -200.0f, " ", 0, 0, 0, 0.1f); primed = true; break; }
    // Real GBA clients: run each core for the elapsed time with the pad keys, then blit its frame in
    // the top corner over that player HUD (ports 3/4 at the bottom), framed by the plate.
    for (uint32_t port = 0; port < 4; ++port) {
        if (!FfccMgba::Active(port)) continue;
        FfccMgba::Tick(port, FfccGbaClientKeys(port));
        if (!GbaVisible(port) || FfccGbaScreens::HostBlit()) { FfccMgba::NoteHidden(port); continue; }
        FfccMgba::RunAhead(port);
        // The client builds a screen while the game streams its data (command list, items, letters, radar:
        // link thread states other than 0x05/0x06/0x3B), and at 25 uploads a second that build-up showed as a
        // cascade. Keep the last complete frame while a stream runs and for a moment after it ends; draw
        // only the plate until the first complete frame exists.
        static std::chrono::steady_clock::time_point s_streamEnd[4]{};
        static bool s_haveFrame[4] = {false, false, false, false};
        if (FfccMgba::TakeHiddenSinceUpload(port)) s_haveFrame[port] = false;
        const uint8_t st = ::Memory::Read8(0x802f07d0u + port * 0x3Cu + 0x28u);
        const bool streaming = !(st == 0x05 || st == 0x06 || st == 0x3B);
        const auto now = std::chrono::steady_clock::now();
        if (streaming) s_streamEnd[port] = now;
        const bool settled = !streaming && (now - s_streamEnd[port]) >= std::chrono::milliseconds(120);
        if (settled && UploadGbaFrame(port)) s_haveFrame[port] = true;
        const float w = 240.0f, h = 160.0f;
        const float x = (port % 2 == 0) ? 20.0f : 640.0f - 20.0f - w;
        const float y = (port < 2) ? 20.0f : 448.0f - 20.0f - h;
        // Frame in the player's colour: a rim in the full colour, a darker plate inside it.
        const FfccGbaScreens::PlayerRgb c = FfccGbaScreens::PlayerColor(port);
        if (!s_haveFrame[port]) continue;   // the whole window pops out with its first complete frame
        RoundedPlate(ctx, x - 8, y - 8, w + 16, h + 16, 9.0f, c.r, c.g, c.b, 0xFF);
        RoundedPlate(ctx, x - 4, y - 4, w + 8, h + 8, 6.0f, uint8_t(c.r * 2 / 5), uint8_t(c.g * 2 / 5), uint8_t(c.b * 2 / 5), 0xFF);
        DrawGbaFrame(ctx, port, x, y, w, h);
    }
    // Select requests from the fake GBA: flip that port's control mode exactly as the game's own
    // handler would for pad 0 (partyobj.cpp menu() -> Joybus.ChgCtrlMode(portIndex)).
    for (uint32_t port = 0; port < 4; ++port) {
        if (!FfccGbaScreens::TakeToggleRequest(port)) continue;
        ctx->gpr[3] = kJoybus; ctx->gpr[4] = port; Call(ctx, kChgCtrlMode);
        RT_LOG(RT_TAG_OS) << "gba native draw: ChgCtrlMode(" << port << ") -> " << ctx->gpr[3] << std::endl;
    }
    for (uint32_t port = 0; port < 4; ++port) {
        const FfccGbaScreens::Screen& s = FfccGbaScreens::ScreenFor(port);
        if (!s.visible) continue;
        if (!primed) {
            FogOff(ctx);
            FontZOff(ctx);
            // The first solid quad after the game's own menu code came out invisible while the second
            // handheld (drawn after the first one's text) was fine: CFont::DrawInit inside DrawFont sets
            // the TEV/vertex state the quads rely on. Prime it once per frame with an off-screen glyph.
            GameText(ctx, -200.0f, -200.0f, " ", 0, 0, 0, 0.1f);
            primed = true;
        }
        DrawScreen(ctx, port, s);
    }
}
REGISTER_NATIVE_FUNCTION_AS(0x8009631C, draw_gba_native_8009631c, "draw_gba_native_8009631c");

#endif // RECOMP_PROJECT_FFCC

// JoyBus::SendAllStat (script op -0x7F, party assignment at dungeon start) sets the link thread state
// to 0 and clears its command queue. On hardware the thread has transmitted whatever was queued
// microseconds earlier; here it transmits only in its poll bursts, so the mode words [10][1B 00] queued
// when the party left the roster were still in the queue and got wiped: the client stayed in roster
// mode, showed "Please look at the TV screen" and ignored every control-mode word (link trace replay:
// with the two words re-inserted the client shows its dungeon screen and opens its menu). Transmit the
// pending words to the emulated client before the game clears them.
namespace {
constexpr uint32_t kJoyBusTp = 0x802f07d0u;   // ThreadParams; queue count at +0x928 + port*4, words at +0x128 + port*0x100
uint32_t CmdCount(uint32_t port) { uint32_t n = 0; ::Memory::TryRead32(kJoyBusTp + 0x928u + port * 4u, n); return n; }
uint32_t CmdWord(uint32_t port, uint32_t i) { uint32_t w = 0; ::Memory::TryRead32(kJoyBusTp + 0x128u + port * 0x100u + i * 4u, w); return w; }
}
extern "C" uint32_t GBAWrite_ffcc(uint32_t chan, uint32_t srcPtr, uint32_t statusPtr);
extern "C" void func_800A72C4(CpuContext* ctx);
extern "C" void sendallstat_flush_800a72c4(CpuContext* ctx) {
    const uint32_t port = ctx->gpr[4];
    if (port < 4 && FfccMgba::Active(port)) {
        InitScratch();
        const uint32_t n = CmdCount(port);
        for (uint32_t i = 0; i < n && i < 64; ++i) {
            const uint32_t w = CmdWord(port, i);
            if (w == 0) continue;
            ::Memory::Write32(g_scratch + 0x40u, w);
            const uint32_t r = GBAWrite_ffcc(port, g_scratch + 0x40u, g_scratch + 0x44u);
            RT_LOG(RT_TAG_OS) << "joybus: SendAllStat flush port " << port << " word 0x" << std::hex << w << std::dec << " -> " << r << std::endl;
        }
    }
    func_800A72C4(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x800A72C4, sendallstat_flush_800a72c4, "sendallstat_flush_800a72c4");
