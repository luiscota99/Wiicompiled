# Final Fantasy Crystal Chronicles (GameCube, PAL) on WiiCompiled

This branch teaches WiiCompiled's translator and runtime to run the PAL GameCube release of
Final Fantasy Crystal Chronicles (`GCCP01`) natively on PC, including the game's multiplayer mode,
which on real hardware needs one Game Boy Advance per player. Here every player's handheld is an
emulated GBA running the game's own client program, drawn in the corners of the screen.

Nothing from the game ships with this repository. You need your own dumped PAL disc; the
translator reads your `main.dol`, the runtime reads your extracted disc files, and the handheld
client program is uploaded by the game itself from your disc at run time, exactly as it would be
to a real GBA.

## Status

Working:

- Title, roster, character creation (on the GBA client or the GameCube menu), towns, world map,
  dungeons, combat, saves.
- One to four players, each on an emulated GBA client: the client's own screens (radar, command
  list, items, equipment, artefacts, treasures, money, favourites, family, letters) with the
  game streaming their data over the emulated link, exactly as on hardware.
- Two extra players on one keyboard when only two gamepads are present.

Known limits:

- The PAL game renders one frame per two retraces (25 frames per second, as on hardware); the
  handheld screens follow that rate.
- Pausing shows the client's PAUSE card, as on hardware; the client menus are used during play.
- Some rendering issues remain (see the journal in the workspace repository, not part of this tree).

## Requirements

- Everything in the main [README](../README.md): .NET 8 SDK, CMake, Ninja, LLVM-MinGW.
- Your own PAL disc image of Final Fantasy Crystal Chronicles (`GCCP01`), extracted to a folder
  (a `dvd/` tree with `gba/ffcc_cli.bin` inside it) and its `main.dol`.
- The mGBA submodule: `git submodule update --init third_party/mgba`.

## Build

1. Build the translator (see the main README).
2. Put your `main.dol` at `projects/ffcc/main.dol`. Its SHA-256 is checked against
   `projects/ffcc/recomp.yml`.
3. Translate and emit the build graph:

   ```
   dotnet translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll translate-recursive 0x80003154 --project projects/ffcc/recomp.yml --threads 8 --output-metadata generated_ffcc/base_translation_output.json --prune-stale
   dotnet translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll emit-build-shards --project projects/ffcc/recomp.yml --out generated_ffcc/build_shards
   ```

4. Build mGBA as a static library (once):

   ```
   python scripts/ffcc/build_libmgba.py --toolchain <path to llvm-mingw/bin>
   ```

5. Configure and build the runtime with the FFCC project flag and the shard manifest:

   ```
   cmake -G Ninja -S . -B build-ffcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-DRECOMP_PROJECT_FFCC=1 -DMKW_TRANSLATED_SHARD_MANIFEST=<repo>/generated_ffcc/build_shards/shards.cmake
   cmake --build build-ffcc --target WiiCompiled --parallel 8
   ```

   Re-run the translate step whenever a native override is added or removed (a new hooked
   address changes the translation).

## Configuration

`build-ffcc/UserData/Config.toml`:

```toml
[paths]
dvd_root = "<folder holding your extracted disc, the one that contains dvd/>"

[gba]
# Players on emulated handheld clients (1-4): ports 1..n. Default 2.
players = 4
```

Environment variables for testing: `WIICOMPILED_FAKE_GBA=0,1,2,3` overrides the port list,
`WIICOMPILED_GBA=fake` uses the hand-made screens instead of the emulated client,
`WIICOMPILED_GBA_DIRECT=0` disables the hybrid input, `WIICOMPILED_GBA_TRACE=<file>` records every
word exchanged with the clients (see below).

## Playing multiplayer

- Every player is on a GBA client. A player's gamepad drives both the game (in the field) and the
  client's screen (when open). Select gives the client control; on the client, B opens its menu.
  The command list must be confirmed by every player when a dungeon starts.
- Ports 3 and 4 without a gamepad use the keyboard. Port 3: WASD to move, J = A, K = B,
  U = Select, I = Start, Q/E = L/R. Port 4: arrow keys, numpad 1 = A, 2 = B, 3 = Select,
  0 = Start, 7/9 = L/R.

## How the handheld link works here

The runtime implements the GameCube SDK's GBA link calls (`GBAJoyBoot`, `GBAGetStatus`,
`GBAWrite`, `GBARead`, `GBAReset`) against one embedded mGBA core per port, using mGBA's JoyBus
command interface, the same one Dolphin uses for its integrated GBA. The game uploads its client
program from the disc, runs the handshake and talks to the client exactly as on hardware.

Three departures from a plain emulator were needed because the game's link thread is a
cooperative fiber here, not a preemptive OS thread:

- The link thread yields to the game every few polls, and never inside the mode-switch
  handshake, where the game's roster logic treats the intermediate states as a disconnect.
- Words the game queues and then clears in the same frame (its party-assignment step clears the
  queue) are transmitted to the client first; on hardware the thread would have sent them already.
- While a client's screen is hidden, its pad words carry the controller keys sampled at the moment
  the game reads them, so the field controls do not depend on the client's frame timing.

`scripts/ffcc/mgba_replay_harness.cpp` replays a recorded link trace into a fresh client and
compares its answers with the recording; `scripts/ffcc/mgba_client_probe.cpp` boots the client
standalone. Both need `FFCC_CLIENT_BIN` pointing at `dvd/gba/ffcc_cli.bin` from your own disc and
build against `third_party/mgba-standalone/libmgba.a` with the definitions listed in
`runtime/cmake/PublicProducts.cmake`.

## Credits for this port

- **[WiiCompiled](https://github.com/patchzyy/Wiicompiled)** is the foundation: the static
  recompiler, the fiber scheduler, and the GameCube/Wii SDK reimplementation that everything here
  runs on.
- **[aurora](https://github.com/encounter/aurora)** by Luke Street renders the game's GX command
  stream; the handheld screens are drawn through it as ordinary GX textures.
- **[mGBA](https://mgba.io)** by endrift and contributors emulates each player's handheld and
  exposes the JoyBus link that the game's SDK calls are mapped onto. MPL-2.0; see
  `THIRD-PARTY-NOTICES.md`.
- **[Dolphin Emulator](https://dolphin-emu.org)**: its integrated GBA and GameCube-side link code
  were the reference for how the JoyBus protocol behaves and for what a correct link looks like.
- **[FFCC-Decomp](https://github.com/zcanann/FFCC-Decomp)** by zcanann and contributors (CC0): the
  decompilation of the PAL game was the knowledge base for the link protocol (`joybus.cpp`,
  `gbaque.cpp`), the menu and roster logic, and the symbol map in `projects/ffcc/`. Without it the
  handheld protocol and its timing assumptions could not have been understood.
- SDL, Dear ImGui, Dawn and LLVM-MinGW, through WiiCompiled and aurora.

AI assistance: the port was developed with Anthropic's Claude (Claude Code); every change was
verified against the game's behaviour, the decompilation, or recorded link traces.
