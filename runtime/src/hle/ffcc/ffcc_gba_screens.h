// GBA personal screens for FFCC multiplayer: the screen logic (ffcc_gba_screens.cpp) emits a list of
// draw operations in a 360x240 "GBA screen" space each frame; a renderer draws them. The native
// renderer (ffcc_gba_native_draw.cpp) uses the game's own font, textures and icons through the
// host-to-guest bridge; an ImGui fallback draws the same list when the bridge is unavailable.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace FfccGbaScreens {

constexpr float kScreenW = 200.0f;
constexpr float kScreenH = 130.0f;

struct Op {
    enum Kind { Rect, Text, Icon } kind;
    float x = 0, y = 0, w = 0, h = 0;   // Rect: box; Text/Icon: position (top-left)
    uint8_t r = 0, g = 0, b = 0, a = 255;
    std::string text;                   // Text
    float scale = 1.0f;                 // Text: font scale; Icon: quad scale
    int itemId = 0;                     // Icon: item id (game icon table)
};

struct Screen {
    bool visible = false;
    std::string title;                  // shown on the bezel
    std::vector<Op> ops;
};

// Player colours as the HUD ribbons use them: 1 blue, 2 red, 3 yellow, 4 green.
struct PlayerRgb { uint8_t r, g, b; };
inline PlayerRgb PlayerColor(uint32_t port) {
    static const PlayerRgb k[4] = {{0x58, 0x48, 0xD8}, {0xD8, 0x38, 0x28}, {0xD8, 0xB0, 0x30}, {0x38, 0xB8, 0x58}};
    return k[port & 3];
}

const Screen& ScreenFor(uint32_t port);
bool HostBlit();   // the overlay draws the emulated client's screen (default)
void SetNativeRenderer(bool active);    // true: the game-side renderer draws, ImGui fallback hides
bool NativeRenderer();

} // namespace FfccGbaScreens
