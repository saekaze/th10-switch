# Touhou 10: Mountain of Faith — Nintendo Switch Port
*(東方風神録　〜 Mountain of Faith)*

![Platform](https://img.shields.io/badge/Platform-Nintendo%20Switch-e60012?style=for-the-badge&logo=nintendoswitch&logoColor=white)
![Status](https://img.shields.io/badge/Status-Fully%20Playable-brightgreen?style=for-the-badge)
![License](https://img.shields.io/badge/Port%20Code-CC0%201.0-blue?style=for-the-badge)

A native homebrew port of ZUN's 2007 bullet hell danmaku classic **Touhou 10: Mountain of Faith** for the **Nintendo Switch** (Horizon OS).

Mountain of Faith is my favourite Touhou game, so it gets the port it deserves. This build runs the clean C++ reimplementation from [YomotsuHisami/th10](https://github.com/YomotsuHisami/th10), compiled directly to native ARM64 and driven by an SDL2 host on an OpenGL ES 3 context — no WebAssembly layer, no Linux, Box64 or Wine involved.

Because the game logic is a clean reimplementation rather than a decompiled EXE, this port has **none of the bad-decompile bugs**

Companion to the [Touhou Switch ports](https://github.com/saekaze/touhou-switch), with the same `touhou10.nro` + `sd:/switch/touhou/touhou10/` layout.

---

## 🆕 What's New in 1.00a-r6

* 🚀 **Native build, much faster:** the game code is now compiled straight to ARM64 instead of going through a WebAssembly layer. In the heaviest spell cards the game's own work per frame is about **60% lower**, the hardest moments run about **33% faster** when fast-forwarding a replay, and the frame drops in busy boss fights are gone.
* 🔋 **Stock CPU clock:** r6 runs at the Switch's normal CPU speed (r5 asked for 1785 MHz) and is still faster.
* 🕒 **Stays smooth in long sessions:** memory use and frame cost stay flat through long play with many restarts, returns to the title and character changes.
* 💾 **Nothing to migrate:** same folder, same `th10.cfg`, `scoreth10.dat` and replays, same controls. Just replace `touhou10.nro`.

---

## ✨ Key Features

* ⬛ **OLED-Friendly Pillarboxing:** the original 640×480 playfield is centred with pure black (`#000000`) bars and an aspect-correct upscale.
* 🔊 **Full Audio:** sound effects plus BGM streamed from `thbgm.dat`, mixed with miniaudio and played through SDL.
* 🎮 **Sane, Remappable Controls:** Joy-Con (handheld, grip, detached) and Pro Controller with the same default layout as every other Touhou Switch port (B shoot, A bomb, L/ZL focus, R/ZR skip, + pause); the in-game **Key Config** can rebind them.
* 🌏 **Language-Aware Title:** hbmenu shows the original Japanese title (`東方風神録　～ Mountain of Faith`) on consoles set to 日本語 and the romanised one everywhere else, filled across all 16 NACP language slots.
* 💾 **Saves Next to the Data:** `th10.cfg`, `scoreth10.dat`, replays (`replay/th10_01.rpy` …) and snapshots are written into the same SD folder the game loaded from. Without `msgothic.ttc`, the port falls back to the Switch shared system fonts.

---

## 📥 Installation Guide

> ⚠️ **Disclaimer:** In compliance with ZUN's guidelines and copyright law, this repository contains **ONLY the homebrew engine code**. No game assets are distributed. You must legally own a copy of *Touhou 10: Mountain of Faith v1.00a*.

### 1. SD Card File Structure

1. Ensure your Nintendo Switch is running custom firmware (Atmosphère CFW).
2. Download the latest `touhou10.nro` from the [Releases](../../releases) tab (or build from source).
3. Create the folder `sd:/switch/touhou/touhou10/` and copy the following into it:

```text
sd:/switch/touhou/touhou10/
    ├── touhou10.nro          # Nintendo Switch homebrew executable
    ├── th10.dat              # Main game archive (v1.00a)
    ├── thbgm.dat             # Background music archive
    └── msgothic.ttc          # Japanese font (ships with the Windows release)
```

`thbgm.dat` is optional (the game runs silent without it); `msgothic.ttc` is recommended for the most faithful text, with the console system fonts as fallback.

**Recommended place: `sd:/switch/touhou/touhou10/`.** Keeping every Touhou port in one `sd:/switch/touhou/` folder (`touhou6`, `touhou7`, `touhou8` …) is much tidier than a separate folder per game. Other places still work: the port first looks in its own folder (wherever the NRO is), then for a `th10` / `touhou10` folder (any capitalisation) directly on the SD card, in `switch/`, `touhou/`, `switch/touhou/`, `games/` or `roms/`. If `th10.dat` isn't found anywhere, the port shows where it looked instead of closing.

### 2. Launching

Run `touhou10.nro` from the **Homebrew Menu (hbmenu)**, **Sphaira launcher**, or a home screen forwarder.

---

## 🕹 Controls

| Nintendo Switch Button | Action |
| :--- | :--- |
| **Left Stick / D-Pad** | Character Movement |
| **B** | Shoot / Confirm |
| **A** | Bomb / Cancel |
| **L / ZL** | Focus (Precision Slow-Motion Movement) |
| **R / ZR** | Skip Dialogue (hold) |
| **+ (Plus)** | Pause / In-Game Menu |
| **X** | Enter (while not bound in Key Config) |
| **Y** | Retry (while not bound in Key Config) |
| **− (Minus)** | Screenshot (saved next to the game data) |

**The same default layout in every Touhou Switch port:** B shoots, A bombs, L/ZL focuses, R/ZR skips dialogue, + pauses. These are only defaults — the Switch buttons act as the game's own gamepad, so the in-game **Key Config** can rebind them, and **Default** there brings this layout back. The D-Pad and sticks only move — they can never be picked as a button. Key Config numbers: 0 B, 1 A, 2 L/ZL, 3 +, 4 R/ZR, 5 X, 6 Y.

---

## 🛠 Building from Source

### Automated Build (GitHub Actions)

This repository includes a CI pipeline (`.github/workflows/build-switch.yml`; a copy also lives in `scripts/`). Push or fork the repository and the workflow compiles `touhou10.nro` inside the official `devkitpro/devkita64` container, uploading it as a downloadable artifact. Tagging `v*` additionally publishes it as a release asset.

### Local Build (Linux / macOS / WSL)

1. Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with `devkitA64` and `libnx`.
2. Install the required Switch portlibs:

   ```bash
   sudo dkp-pacman -Syu
   sudo dkp-pacman -S switch-dev switch-mesa switch-libdrm_nouveau switch-sdl2 switch-freetype switch-libpng switch-bzip2 switch-zlib switch-harfbuzz
   ```

3. Build:

   ```bash
   export DEVKITPRO=/opt/devkitpro
   ./scripts/build_switch.sh
   ```

   The result is `build-switch/touhou10.nro`.

A desktop Linux build of the same sources (SDL2 + GLES 3) and its host tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
xvfb-run -a build/th10_host_tests
build/touhou10 /path/to/th10-data
```

The Linux build can also play a scripted input file with a fixed seed and save screenshots (`TH10_SCRIPT`, `TH10_SEED`, `TH10_SHOTS`; see the comment in `src/main.cpp`), which is how the port is checked frame by frame.

---

## 📂 How It Works

The game logic is upstream's architecture-independent C++, built natively with `TH_NATIVE_PLATFORM` (typed graphics calls instead of the browser's Direct3D-style device). Upstream targets 32-bit pointers (the original EXE is x86, the web build wasm32); the port keeps the game's file formats and save data byte-identical on 64-bit ARM. A 32-bit build of the same sources renders every frame pixel-identically and writes the same `scoreth10.dat`.

| Path | Purpose |
| :--- | :--- |
| `game/` | YomotsuHisami's reconstruction of the original game code |
| `platform/` | upstream's platform layer (application flow, title, world, audio, fonts) |
| `rebuild/` | x87 80-bit float emulation (Berkeley SoftFloat) for bit-exact replays |
| `portable/` | shared GLES 3 renderer, input mapping and numeric helpers |
| `src/main.cpp` | startup, vsync-paced frame loop, Switch buttons and touch screen |
| `src/FileHost.cpp` | `th10.dat` / `thbgm.dat` access and the SD save folder |
| `src/GraphicsHost.cpp` | textures, render targets and draws on the GLES renderer |
| `src/AudioHost.cpp` | DirectSound-style buffers on miniaudio + SDL audio |
| `src/FontHost.cpp` | CP932/GBK text via FreeType (MS Gothic or the Switch system font) |
| `platform/switch/icon.jpg` | 256×256 NRO icon (the original game cover) |
| `scripts/build_switch.sh` | one-shot Switch build |
| `scripts/nacp_lang.py` | Japanese NACP title slot |
| `tests/test_host.cpp` | host tests (files, fonts, audio, renderer) |

`th10-switch.log` in the data folder gets a timing line every 600 played frames (`step` = the game's CPU work, `present` = GPU and vsync, in ms per frame), useful when reporting slowdowns.

---

## 🤝 Credits & Acknowledgments

* **ZUN / Team Shanghai Alice** — original creator and developer of the Touhou Project series.
* **[YomotsuHisami](https://github.com/YomotsuHisami/th10)** — the clean-room TH10 C++ reimplementation this port is built on.
* **John R. Hauser** — Berkeley SoftFloat. **David Reid** — miniaudio.
* **Switchbrew & devkitPro Team** — the open-source `libnx` SDK and Switch toolchain.
* Port developed with AI assistance.

**Licensing:** the Switch-only files (`scripts/`, `platform/switch/`, `tests/`, build files) are CC0 1.0 (see `LICENSE`). The game and platform code (`game/`, `platform/`, `portable/`, `rebuild/machine.hpp`) and the hosts in `src/`, which are adapted from upstream's SDL host, come from YomotsuHisami and are under the **MIT** licence ([confirmed by the author](https://github.com/YomotsuHisami/th10/issues/3)), not `LICENSE` (see [`LICENSE-YomotsuHisami.txt`](LICENSE-YomotsuHisami.txt)). Berkeley SoftFloat (`rebuild/third_party/softfloat.c`) is BSD-3-Clause and miniaudio / stb (`portable/sdl/third_party/`) are public domain or MIT-0; see their file headers.
