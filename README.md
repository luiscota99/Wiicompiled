# Final Fantasy Crystal Chronicles (GameCube, PAL) on WiiCompiled

This fork of [WiiCompiled](https://github.com/patchzyy/Wiicompiled) runs the PAL GameCube release of Final Fantasy Crystal Chronicles (`GCCP01`),
including its multiplayer mode: every player's Game Boy Advance is an emulated handheld running the
game's own client program, drawn in the corners of the screen. Up to four players; each port takes its gamepad, and ports
without one can be driven from the keyboard with per-port bindings (defaults for two extra
players included, all remappable in the F10 settings bar). You need your own dumped disc; nothing from the game ships here. See
[`docs/ffcc.md`](docs/ffcc.md) for the build, configuration and controls.

| Character creation on the clients | Dungeon start: command list and hub menu |
| --- | --- |
| ![creation](docs/media/ffcc_creation.jpg) | ![dungeon](docs/media/ffcc_dungeon_prompt.jpg) |
| ![hub menu](docs/media/ffcc_hub_menu.jpg) | ![combat](docs/media/ffcc_combat.jpg) |

![creation clip](https://github.com/luiscota99/Wiicompiled/releases/download/ffcc-media-1/ffcc_creation_small.gif)

![dungeon clip](https://github.com/luiscota99/Wiicompiled/releases/download/ffcc-media-1/ffcc_dungeon_menus_small.gif)

Full videos: [character creation](https://github.com/luiscota99/Wiicompiled/releases/download/ffcc-media-1/ffcc_creation.mp4) and
[dungeon start, menus and combat](https://github.com/luiscota99/Wiicompiled/releases/download/ffcc-media-1/ffcc_dungeon_menus.mp4).

## Quick start

**You need:** your own dumped PAL disc of Final Fantasy Crystal Chronicles (`GCCP01`), extracted to a
folder (it contains `dvd/`), and its `main.dol`. Tools: .NET 8 SDK (or a newer one with
`DOTNET_ROLL_FORWARD=Major`), CMake, Ninja, LLVM-MinGW, Python 3, and on Windows a Windows SDK for the
C++/WinRT headers. Nothing from the game is in this repository.

**Build** (from the repository root):

```
git submodule update --init third_party/mgba
dotnet build translator/Translator.sln -c Release
copy <your main.dol> projects\ffcc\main.dol
dotnet translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll translate-recursive 0x80003154 --project projects/ffcc/recomp.yml --threads 8 --output-metadata generated/base_translation_output.json --prune-stale
dotnet translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll generate-data-init --project projects/ffcc/recomp.yml
dotnet translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll emit-build-shards --project projects/ffcc/recomp.yml --out generated/build_shards
python scripts/ffcc/build_libmgba.py --toolchain <llvm-mingw>/bin
cmake -G Ninja -S runtime -B build-ffcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=<llvm-mingw>/bin/clang.exe -DCMAKE_CXX_COMPILER=<llvm-mingw>/bin/clang++.exe -DCMAKE_RC_COMPILER=<llvm-mingw>/bin/llvm-windres.exe -DMKW_CPPWINRT_INCLUDE_DIR="C:/Program Files (x86)/Windows Kits/10/Include/<version>/cppwinrt" -DCMAKE_CXX_FLAGS=-DRECOMP_PROJECT_FFCC=1
cmake --build build-ffcc --target WiiCompiled --parallel 8
```

**Run:** create an empty `build-ffcc\portable.txt`, then put in `build-ffcc\UserData\Config.toml`:

```toml
[paths]
dvd_root = "<folder that contains dvd/>"

[gba]
players = 2   # 1-4
```

Start `build-ffcc\WiiCompiled.exe`. Each port takes its gamepad in connection order; ports without
one use the keyboard (defaults: port 1 WASD/X/Z/Space/Enter, port 3 WASD/J/K/U/I, port 4 arrows and
numpad 1/2/3/0). Remap anything with **F10 > Controller settings**. In play, Select gives the handheld
client control and B opens its menu; at a dungeon start every player confirms their command list.

The full guide, environment variables and troubleshooting: [`docs/ffcc.md`](docs/ffcc.md).

## Why this exists

Crystal Chronicles' multiplayer was designed around one Game Boy Advance per player, linked to the
GameCube with a cable: menus, the radar, the command list and the letters all live on the handheld
while the TV shows the shared world. That hardware is the reason the mode is rarely played today, and
emulators need one emulated handheld per player wired to the console. This port makes the whole setup
one program on one PC: the game runs as native code produced by WiiCompiled's static recompiler, and
each player's handheld is the game's own client program running in an embedded emulator, drawn on the
same screen. Nothing is re-implemented by hand: the client is the original, the link protocol is the
original, and the runtime only supplies what the console hardware and SDK used to.

The fork is also a proof that WiiCompiled works for a GameCube title: everything game-specific sits in
a project manifest and a per-game address header, so the same runtime can be pointed at another game.

## Disclosures

- **AI assistance.** This port was developed with Anthropic's Claude (Claude Code). Every change was
  verified against the running game, the FFCC decompilation, or recorded link traces replayed into
  the emulated client; nothing was accepted on the model's word alone. Commits carry a co-author
  trailer and this notice.
- **Game data.** None is included. You need your own dumped PAL disc; the translator reads your
  `main.dol` (its SHA-256 is checked), the runtime reads your extracted disc, and the handheld client
  program is uploaded by the game itself from that disc at run time. Do not ask where to get the game.
- **Not affiliated** with Square Enix, Nintendo, or the authors of the projects credited below.
  Final Fantasy Crystal Chronicles is a trademark of Square Enix; GameCube and Game Boy Advance are
  trademarks of Nintendo.
- **Licences.** This repository is GPL-3.0 like upstream WiiCompiled. mGBA (MPL-2.0) is a pinned
  submodule built as a static library; the FFCC symbol map comes from FFCC-Decomp (CC0). Details in
  [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
- **Region.** PAL (`GCCP01`) only. Other regions have different addresses and are untested.

## Status and roadmap

Working today: title, roster, character creation on the handheld or the console menu, towns, world
map, dungeons, combat, saves; one to four players on emulated handhelds with all of the client's
screens streamed by the game; two extra players on one keyboard by default.

Known limits: the PAL game renders 25 frames per second (as on hardware) and the handheld screens
follow it; pausing shows the client's PAUSE card as on hardware; some rendering issues remain.

Planned, in order:

1. Replace the emulated link layer with a native implementation behind the same game entry points,
   removing the last scheduling differences from hardware (the recorder and replay harness in
   `scripts/ffcc/` exist to verify it word for word).
2. A presentation-time compositor so the handheld screens can refresh at 50 Hz independently of the
   game's frame rate.
3. Optional deviations from hardware: menus usable while paused.
4. The remaining rendering issues, then a pull request to upstream WiiCompiled for the GameCube and
   per-game infrastructure.

Bug reports and pull requests are welcome; include the runtime log and, for link problems, a trace
recorded with `WIICOMPILED_GBA_TRACE`.

## Known issues

- **Rendering.** The screen deformation effect used by some attacks and spells draws as a square;
  some world-map effects draw as blobs; a few scenes are lit black where they should not be. These
  are host renderer gaps, not game logic; a Dolphin capture of the same scene is the reference.
- **Frame rate.** The PAL game renders 25 frames per second by design (one frame per two retraces), and
  the handheld screens refresh at that rate even though the emulated clients run at 60.
- **Rare early exit.** In a small number of launches the process has ended silently within the first
  minute, with no fault text or stall dump. Unexplained; if it happens to you, keep the runtime log
  and open an issue.
- **First start is slow.** The shader pipeline cache is built on first launch (about 15 to 20 seconds
  before the title appears); later starts are quick.
- **Pause.** The handheld client shows its PAUSE card and ignores input while the game is paused, as
  on hardware; the client's menus are used during play.
- **Keyboard port 2.** Port 2 has no default keyboard set (it is expected to have a gamepad); assign
  one in the F10 settings bar if you need it.
- **Emulated client pacing.** The clients run a few frames ahead of the wall clock under load (capped);
  harmless, but it is on the list to trim.
- **PAL only.** Other regions are untested and need their own addresses.

## Help wanted

This is one person's port, built on other people's foundations, and there is plenty left that a
second pair of hands would move faster than the first:

- **Testing on other machines.** Different GPUs and drivers, different controllers, four real
  gamepads, long sessions. A clean clone built from the quick start is the most useful report.
- **Rendering.** A few scenes still draw wrong (lighting, world-map effects, one deformation). The
  fastest way to fix them is a Dolphin reference capture of the same scene next to ours.
- **The native link layer.** The plan above; the game's entry points, the protocol notes and the
  replay harness are in the tree. Someone who enjoys protocol work could take this on.
- **Other games, other regions.** The per-game address header and the project manifest are the
  whole recipe for pointing the runtime at another GameCube title or another FFCC region.
- **Documentation and setup.** Anything that made your first build harder than the quick start says.

Open an issue with what you tried and the runtime log, or a pull request against `ffcc-port`. Small,
verified changes are easier to take than large ones; say how you tested.

Everything below this line is the upstream WiiCompiled README (Mario Kart Wii), which this fork
builds on and keeps intact; its build instructions, FAQ and licence apply here too.

---

<img width="4190" height="1232" alt="wiicomplogofinalfinalfinalev2MADEBY_INKWRECK_plzcredit" src="https://github.com/user-attachments/assets/df7a3f2e-5336-479a-b4c0-968dd578726d" />

## WiiCompiled (upstream)

<p align="center">
  <a href="https://github.com/patchzyy/Wiicompiled/releases"><img alt="Windows 10 / 11, x64" src="https://img.shields.io/badge/Windows-10%20%2F%2011%20%C2%B7%20x64-0078D4"></a>
  <a href="https://github.com/patchzyy/Wiicompiled/releases"><img alt="Linux, x64 / ARM64" src="https://img.shields.io/badge/Linux-x64%20%2F%20ARM64-FCC624?logo=linux&amp;logoColor=white"></a>
  <a href="https://github.com/patchzyy/Wiicompiled/releases"><img alt="macOS 14+, Apple Silicon" src="https://img.shields.io/badge/macOS-14%2B%20%C2%B7%20Apple%20Silicon-0A84FF?logo=apple&amp;logoColor=white"></a>
</p>
<p align="center">
  <a href="#building-from-source"><img alt="PowerPC static recompilation" src="https://img.shields.io/badge/PowerPC-static%20recompilation-FF9F0A"></a>
  <a href="#retro-rewind"><img alt="Retro Rewind supported" src="https://img.shields.io/badge/Retro%20Rewind-supported-FF375F"></a>
  <a href="https://github.com/TeamWheelWizard/WheelWizard/releases"><img alt="Install with Wheel Wizard" src="https://img.shields.io/badge/install%20with-Wheel%20Wizard-8B5CF6"></a>
  <a href="LICENSE"><img alt="License: GPLv3" src="https://img.shields.io/badge/license-GPLv3-2EA44F?logo=gnu&amp;logoColor=white"></a>
</p>

A native PC port of Mario Kart Wii, made with static recompilation.

There's no emulator in the loop, no interpreter, no JIT, no PowerPC
anywhere at runtime.

> [!IMPORTANT]
> There is no Nintendo code, no assets and no game data anywhere in this project or its releases.
> You need your own legally dumped copy of the PAL version of the game. Setup only ships the
> toolchain, the translation runs on your machine against your disc image, and nothing ever gets
> uploaded.

[What is a github, I just want to play](https://github.com/TeamWheelWizard/WheelWizard/releases/latest)

---

## What it does

**Unlocked framerate with interpolation.** 
The original game is hard-locked to 60 fps. The runtime can generate interpolated frames in between, so on a
120/144 Hz monitor things genuinely look smoother.

> [!WARNING]
> Interpolation is experimental right now and will show artifacts in specific scenarios.

**Any aspect ratio you want.** 
Drag the window bigger, wider, whatever, the camera adjusts
live.

**Native rendering via aurora.** 
The graphics layer is built on
[aurora](https://github.com/encounter/aurora). Aurora is a source-level GameCube & Wii compatibility layer.

**High internal resolution.** 
Play at several times the console's resolution.

**Music ducking.** 
Start playing something else, Spotify, a YouTube video, and
the game automatically mutes its own music until the other audio stops. Optional, if you'd
rather it didn't. All audio that shows in your display media controls on your windows pc fall under this.

**An in-game settings bar.** 
Press **F10** while the game window has focus:
- Internal resolution
- FPS counter
- Controller assignment for all four ports
- Full per-controller button mapping, including the bumpers
- Dolphin-syntax input expressions and GCPadNew.ini import
- Controller vibration on/off
- Volume, instant mute, and the music ducking toggle

Everything you change is saved to `Config.toml` on the spot and restored next launch.

**Dolphin-compatible input expressions.** 
Each GameCube control can carry an expression in Dolphin's input syntax, with the same operators
and the same functions.
A Dolphin `GCPadNew.ini` can be imported directly from the F10 bar.

**Vibration toggle.** 
Force feedback can be turned off for every port at once.
The official Wii U / Switch GameCube adapter (WUP-028) works too; as with Dolphin, on Windows the
adapter must be switched to the WinUSB driver once (Zadig).

**Real Wii Remotes over Bluetooth.**
Pair a Wii Remote with Windows (Settings > Bluetooth > Add device, press 1+2 or SYNC, leave the
PIN empty)

Known limitations of the Wii Remote path:
- No IR pointer yet: menus are navigated with the D-pad and A (the game treats the remote as
  pointing away from the screen).
- Battery level is not reported to the game and the remote's speaker is not implemented.
- Only the Wii Remote's own accelerometer is calibrated; the Nunchuk's uses SDL's fixed zero point.
- The Classic Controller's L/R triggers reach the game as digital (full pull on click): SDL does not
  expose their analog travel.
- Turn the Wii Remote support off in that menu if you use a Mayflash DolphinBar, which already
  presents the remote as a regular gamepad.

## Requirements

- Windows 10 or 11, 64-bit
- GPU: GTX 1650 / RX 6400 / Arc A310 or higher
- CPU: Intel Core i5-8400 / AMD Ryzen 5 2600 (4c/6c, ~3.5GHz+) or higher
- About 20 GB of free disk space during installation (Final game size ~5 GB)
- macOS 14 (Sonoma) or later on Apple Silicon
- On macOS, Apple Xcode Command Line Tools (Setup opens Apple's installer when they are missing)
- A clean, unmodified **PAL `RMCP01`** disc image of Mario Kart Wii, dumped by you. ISO, GCM,
  GCZ, CISO, WBFS, WIA and RVZ are accepted.

> [!NOTE]
> GPU/CPU minimums are set by driver support and D3D12/Vulkan feature requirements, not by the game's actual demands.

Only the clean PAL revision will work. Anything else (other
regions, patched executables) is rejected outright.

> [!NOTE]
> Nobody here will tell you where to get the game. Dumping your own disc is on you, and links to
> game files won't be provided or tolerated.

## Installing

For an easy experience, use [Wheel Wizard](https://github.com/TeamWheelWizard/WheelWizard). Pick your clean PAL `RMCP01`
image under Settings, turn on **WiiCompiled (beta)**, and hit install from the Home page.
Wheel Wizard downloads the setup tool from this repo and walks you through install, updates and
launching. The backend itself is deliberately command-line only, Wheel Wizard is a wrapper around it.


> [!CAUTION]
> Only take builds from this repository's
> [Releases](https://github.com/patchzyy/Wiicompiled/releases) page. If someone's sharing an
> installer through Discord or some random download site, don't touch it!!

## A note on related projects

WiiCompiled, Wheel Wizard, Retro rewind and other related projects are developed
**independently** and each has its **own** contribution rules and all have their own
rules. What applies here does not automatically apply there,
and vice versa. Check each project's own CONTRIBUTING and README files.

## Retro Rewind

[Retro Rewind](https://wiki.tockdom.com/wiki/Retro_Rewind), ZPL's Mario Kart Wii mod distribution,
can be built as its **own static profile**: instead of applying `Code.pul` as runtime patches,
the Kamek/Pulsar code is statically translated together with the base game into a separate native
executable.

Wheel Wizard drives this too.

## Building from source

Owning the game is still required even if you compile everything yourself.

You'll need: .NET 8 SDK, CMake, Ninja, and LLVM/Clang (the shipped build uses LLVM-MinGW targeting
`x86-64-v3`).

Build the translator:

```powershell
dotnet build translator/Translator.sln -c Release
```

The default test suite needs no binaries and no host C++ compiler, so you can hack on the
translator without any game data around.

For everything beyond that, feeding in your own `main.dol`/`StaticR.rel`, running the
translation, generating the manifest and build graph, and compiling, see [`translator/README.md`](translator/README.md).

For a step-by-step guide on compiling both WiiCompiled and Retro Rewind from source on macOS (Apple Silicon), see the [macOS Build Guide](docs/building-macos.md).

## FAQ

**Is this an emulator?**
No. Everything is compiled to native code before you ever press play. At runtime there's nothing
emulating a Wii CPU or GPU.

**Do you provide the game?**
No. Don't ask. Nothing in this repo or any release contains Nintendo code or assets.

**Why does setup take so long?**
Because we **don't** ship the translated binary, most other recomp projects do, but we
don't want to risk it right now, setup has to run a static recompiler over the whole game
and then throw a C++ compiler at the result. It's a **one-time cost** on your machine.

**Which game version works?**
Clean PAL `RMCP01`. Other regions and modified executables are **rejected**. Translating
them against the wrong manifest would give you a subtly broken game that's miserable to debug for us.

**Can I recompile other GameCube/Wii games with it?**
The translator itself handles DOLs and RELs generically, see
`projects/examples/generic-dol.yml`. The catch is that a *playable* port also needs a runtime:
audio, input, GX, everything the game touches.

**The game crashed / stopped with an error.**
Errors are deliberately loud instead of quietly swallowed. Send a report along with the run log
from `%LOCALAPPDATA%\WiiCompiled\Logs`.

**Will you fix original bugs?**
Not in the base game, behavior identical to real hardware is the goal. Only report things where this port differs
from the original game. As for Retro Rewind, some base-game behavior **is** patched, so if it differs from the
base game, that's normal. If Retro Rewind behavior differs between Dolphin/Wii and WiiCompiled, open an issue on GitHub.

**How accurate are the physics?**
100% - this is proven by in-game ghosts. Since ghosts are replay files based on inputs rather
than tracked positions, matching ghosts prove the physics match across Dolphin/Wii/WiiCompiled.

**Is it done?**
Not fully. The game is in a state where everything should be playable and the physics do match
100% with the original game, but compatibility, rendering, networking and performance are all
actively being worked on. If you do find an issue, we strongly encourage you to open one on
GitHub so we can take a look at it.

## AI usage
AI coding tools were used during development of this project. 
All translated output is verified against real hardware behavior and most importantly, physics accuracy is proven synced across Wii, Dolphin, and WiiCompiled (see FAQ). 

## Credits
- **inkwreck** - making the logo
- **[aurora](https://github.com/encounter/aurora)** - the GX rendering/windowing backend this
  project's whole graphics layer sits on. MIT licensed.
- **[Dawn](https://dawn.googlesource.com/dawn)** - Google's WebGPU implementation, powering
  aurora's Direct3D, Vulkan and OpenGL backends.
- **[Dolphin Emulator](https://github.com/dolphin-emu/dolphin)** - an invaluable reference for Wii
  hardware behavior during development, plus the source of the free DSP coefficient ROM and the
  unmodified default WiiConnect24 bootstrap tree bundled with the runtime.
- **[Retro Rewind](https://wiki.tockdom.com/wiki/Retro_Rewind)** by ZPL and team - the mod
  distribution this project supports.
- **[Wheel Wizard](https://github.com/TeamWheelWizard/WheelWizard)** - the mod manager this
  project integrates with as a launch backend.
- **[mGBA](https://mgba.io)** by endrift and contributors - emulates each player's Game Boy Advance
  for the Final Fantasy Crystal Chronicles port and provides the JoyBus link the game's SDK calls
  are mapped onto. MPL-2.0.
- **[FFCC-Decomp](https://github.com/zcanann/FFCC-Decomp)** by zcanann and contributors - the
  decompilation of Final Fantasy Crystal Chronicles (CC0), the knowledge base for that game's
  handheld link protocol, menus and symbol map.
- Everyone in the static recompilation community.

Bundled third-party components and their licenses live in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).


## License

WiiCompiled is free software: you can redistribute it and/or modify it under the terms of the
[GNU General Public License, version 3](LICENSE) as published by the Free Software Foundation.

WiiCompiled is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

Any mkwii distribution making use of WiiCompiled must be licensed under GPL v3.0.

Not affiliated with, endorsed by, or associated with Nintendo. Mario Kart Wii is a trademark of
Nintendo. No Nintendo intellectual property is contained in, distributed with, or obtainable
through this project.
