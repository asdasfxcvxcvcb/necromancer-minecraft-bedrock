# Necromancer Client

**Necromancer Client** is an open source DLL mod client for **Minecraft: Bedrock Edition (Windows 10/11)**. It hooks into the game at runtime and adds a polished in-game menu with **43 modules** covering combat, movement, visuals and quality-of-life tweaks, plus chat commands, per-module keybinds, config presets, and full HUD customization.
- Open source under the [LICENSE](LICENSE)

> **Disclaimer:** This software modifies the game client. Using cheat-style modules (Aimbot, Triggerbot, Backtrack, etc.) on multiplayer servers can get you banned. Use at your own risk.

---

## Running the DLL

### Requirements (players)

| Requirement | Notes |
|---|---|
| Windows 10/11, 64-bit | The client only runs on Windows. |
| Minecraft: Bedrock Edition | The Windows Store build.
| A DLL injector | Necromancer ships as `Necromancer.dll`, which has to be injected into `Minecraft.Windows.exe`. Use any Bedrock-compatible injector  |

No other runtime dependencies — the VC++ runtime is statically linked into the DLL, and all fonts, images and translations are embedded.

### Usage

1. Start Minecraft.
2. Inject `Necromancer.dll` into `Minecraft.Windows.exe`.
3. Press **Insert** (or **F10**) to open the module menu.
4. Left-click a module to toggle it, right-click to expand its settings.
5. Use the search box to find any module by name (including hidden/debug ones).

Configs, logs and saved skins live in `C:\necromancer_mcbe\` (`Configs`, `Logs`, `Skins`).

### Chat commands

Commands are sent through normal chat with a `.` prefix (changeable with `.setprefix`):

| Command | What it does |
|---|---|
| `.help` | Lists all commands |
| `.toggle <module>` | Toggles a module from chat |
| `.config <save/load/list/...>` | Manages config presets |
| `.setprefix <char>` | Changes the command prefix |
| `.eject` | Uninjects the client |
| `.waypoint` | Waypoint management |

---

## Modules

### Combat

- **Auto Clicker** — Automatically clicks for you at a set speed (independent left/right CPS, randomization, hold modes).
- **Aimbot** — Automatically aims at nearby entities (smooth aiming, drift, target priority, wall check).
- **Triggerbot** — Automatically hits entities under your crosshair (CPS cap, smart criticals, mace smash support).
- **Shield Breaker** — Swaps to an axe and hits players who are blocking with a shield, then swaps back.
- **Backtrack** — Shows where players were a moment ago so you can aim at their past position on servers with lag compensation.

### Movement

- **Toggle Sprint/Sneak** — Toggle sprinting or sneaking without holding the button.
- **NoFall** — Prevents fall damage, including an auto water bucket clutch mode with fakelag integration.
- **Anti AFK** — Acts only after you go idle (jump, move, look around); your own input always takes priority.
- **Fakelag** — Chokes your outgoing movement, then briefly releases it based on range, timing and combat triggers. Shows the choked position as a hitbox.
- **Velocity** — Reduces or cancels knockback taken from attacks and explosions.
- **Legit Scaffold** — Only allows placing blocks on faces physically reachable from your position, keeping bridging tower-safe.

### Visuals / HUD

#### World rendering

- **ESP** — See entities through walls (health bars, names, equipment, line of sight).
- **Block ESP** — Highlights chosen blocks through walls with per-block colors.
- **Block Overlay** — Changes the overlay shown on the block you're looking at.
- **Legit Hitbox** — Shows entity bounding boxes.
- **Hitboxes** — Customizable hitbox rendering with eye-line and looking-at indicators.
- **Damage Indicator** — Shows damage you deal to entities as floating numbers.
- **Out Of View Arrows** — Arrows around the screen center pointing toward off-screen players and mobs.
- **Fullbright** — Extra world brightness.
- **Environment Changer** — Changes visual features in the environment (time, weather, fog color).
- **Freelook** — Look around freely without changing your movement direction.
- **Zoom** — Zoom in and out bound to a key (cinematic camera, DPI adjust, hide hand).
- **Anti OBS** — Renders ESP/arrows on a separate overlay so screen capture software can't record them (ghost client).

#### HUD displays

- **FPS Counter** — Shows your framerate.
- **CPS Counter** — Shows your clicks per second (left/right/both).
- **Combo Counter** — Shows your current combo count.
- **Keystrokes** — Shows movement keys, mouse buttons and space bar state.
- **Keybind List** — Shows your keybinds and whether each module is active.
- **Ping Display** — Shows the average upstream ping of the connected server.
- **Server Display** — Shows the server you're connected to (with optional port and featured-server name).
- **Speed Display** — Shows your speed in blocks per second.
- **Movable Coordinates** — Makes the vanilla coordinates display movable.
- **Custom Coordinates** — Shows player position and dimension (searchable, not in a menu tab).
- **Bow Indicator** — Shows how charged your bow is.
- **Break Indicator** — Shows the break progress of the block you're breaking.
- **Item Counter** — Counts arrows, totems, potions, crystals and XP bottles in your inventory.
- **WAILA** — "What Am I Looking At" — info panel for the block or entity you're looking at.
- **Player List** — Enhanced in-game player list.
- **Third Person Nametag** — Shows your own nametag while in third person.
- **Nickname** — Makes your username appear as something else in chat.
- **Font** — Choose which font the client HUD and modules render with.

### Misc

- **Kill Notification** — Plays a sound after you kill an entity (preset sounds or any Minecraft sound ID).
- **Skin Stealer** — Save player skins from anyone on the current player list.
- **Text/Command Hotkey** — Binds a chat message or command to a key.
- **AntiBot** — Filters out fake/bot players using player list, name and actor data heuristics.
- **Disable Mouse Wheel** — Stops the mouse wheel from switching hotbar slots.
- **Chat** — Chat automation with a spammer and kill-say messages.
- **Auto Block** — Switches to another stack of blocks when the one you're holding runs out.
- **Chest Stealer** — Automatically takes everything out of chests you open.
- **Item Switcher** — Automatically switches to a selected item when it appears in your hotbar; comes with a searchable item picker.

### Hidden

- **debug_info** — Logs per-module event timings to `Logs\fps_tester.txt` every 10 seconds. Searchable but not in any menu category, for performance profiling.

---

## Building from source

### Build requirements

| Tool | Why | Where to get it |
|---|---|---|
| **Visual Studio 2022** (or Build Tools) with *Desktop development with C++* | MSVC compiler (`cl.exe`), Windows SDK, CMake + Ninja | [visualstudio.microsoft.com/downloads](https://visualstudio.microsoft.com/downloads/) |
| **CMake 3.22+** | Build system generator (bundled with VS) | Included in the VS C++ workload or [cmake.org](https://cmake.org/download/) |
| **Ninja** | Build executor (bundled with VS) | Included in the VS C++ workload |
| **MinGW-w64 binutils (`ld.exe`)** | Embeds the `assets/` files into the DLL at link time | Install [MSYS2](https://www.msys2.org/), then `pacman -S mingw-w64-x86_64-toolchain`, and add `msys64\mingw64\bin` to PATH |
| **Git** | Cloning + dependency fetch (CPM) | [git-scm.com](https://git-scm.com/download/win) |
| Internet access on first configure | CMake/CPM downloads glm, minhook, mnemosyne, nlohmann/json, libhat, EnTT | — |

### Quick build (recommended)

Just double-click one of the batch scripts in the repo root — they auto-detect your Visual Studio installation with `vswhere` and report anything missing:

| Script | Produces |
|---|---|
| `BUILD_RELEASE.bat` | `out/build/x64-release/Necromancer.dll` — optimized, no debug info. **Ship this one.** |
| `BUILD_RELEASE_AVX2.bat` | `out/build/x64-release-avx2/Necromancer.dll` — same but compiled with `/arch:AVX2`; noticeably faster on modern CPUs but refuses to run on pre-2013 hardware. |
| `BUILD_NIGHTLY.bat` | `out/build/x64-nightly/Necromancer.dll` + PDB — optimized *with* symbols, for crash diagnosis. |
| `build_dbg.bat` | `out/build/x64-debug/Necromancer.dll` + PDB — unoptimized development build. |

### Manual build

From an **x64 Native Tools Command Prompt for VS 2022** (or any terminal after running `vcvars64.bat`):

```bat
cmake --preset x64-release
cmake --build out/build/x64-release --parallel
```

Output: `out/build/x64-release/Necromancer.dll`

Other presets: `x64-debug`, `x64-relwithdebinfo`, `x64-nightly`, `x64-release-avx2`. The AVX2 preset sets `NECROMANCER_AVX2=ON` (adds `/arch:AVX2`); everything else about it is identical to release.

> **Note:** The stale `Necromancer.sln`/`Necromancer.vcxproj` in the repo root are **not** the real build. They're leftover IDE artifacts from an old source layout — don't open and build them; always use CMake presets or the batch scripts.

---

## Project layout

```
├── CMakeLists.txt / CMakePresets.json   CMake build definition
├── BUILD_*.bat / build_dbg.bat          One-click build scripts (auto-detect VS)
├── src/
│   ├── client/       Client core: hooks, screens (ClickGUI/HUD editor), modules, configs
│   ├── mc/           Reversed Minecraft Bedrock SDK headers + signature storage
│   └── util/         Drawing, DX context, math, logging helpers
├── deps/             Build dependencies (CPM bootstrap, vhook)
├── assets/           Fonts, images, translations — embedded into the DLL via ld.exe
├── cmake/            CMake helper scripts
└── docs/             Research notes
```

## Contributing

Pull requests welcome. A few hard rules:

- Format your code with the repo's `.clang-format` before committing (CI enforces it).
- Don't push build output, `out/`, or local tooling state.
- New modules must register in `ModuleManager.cpp` **and** be added to the right category whitelist in `ClickGUI::isModuleInTab`, or they'll only be reachable through search.

## License

See [LICENSE](LICENSE).

---

**Necromancer Client is not affiliated with Mojang or Microsoft.** Minecraft is a trademark of Microsoft Corporation.
