# Touhou 10: Mountain of Faith — Nintendo Switch Port
*(東方風神録　〜 Mountain of Faith)*

![Platform](https://img.shields.io/badge/Platform-Nintendo%20Switch-e60012?style=for-the-badge&logo=nintendoswitch&logoColor=white)
![Status](https://img.shields.io/badge/Status-Fully%20Playable-brightgreen?style=for-the-badge)
![License](https://img.shields.io/badge/Port%20Code-CC0%201.0-blue?style=for-the-badge)

A native homebrew port of ZUN's 2007 bullet hell danmaku classic **Touhou 10: Mountain of Faith** for the **Nintendo Switch** (Horizon OS).

Mountain of Faith is my favourite Touhou game, so it gets the port it deserves. This build runs the clean C++ reimplementation from [YomotsuHisami/th10](https://github.com/YomotsuHisami/th10), compiled to native ARM64 through the wasm2c runtime and driven by an SDL2 host on an OpenGL ES 3 context — no Linux, Box64 or Wine involved.

Because the game logic is a clean reimplementation rather than a decompiled EXE, this port has **none of the bad-decompile bugs**

Companion to the [Touhou 6](https://github.com/saekaze/th06-switch), [Touhou 7](https://github.com/saekaze/th07-switch), [Touhou 8](https://github.com/saekaze/th08-switch), [Touhou 9](https://github.com/saekaze/th09-switch) and [Touhou 11](https://github.com/saekaze/th11-switch) Switch ports, with the same `touhou10.nro` + `sd:/switch/touhou/touhou10/` layout.

---

## 🆕 What's New in 1.00a-r5

* 🚀 **Performance improvements:** busy scenes and heavy spell cards run smoother (Kanako's last spell card is the easiest place to see it), and the game no longer drops to 59.8 FPS or judders after a continue.
* 💬 **No more lag when skipping dialogue** — each line of text is now drawn about 40× faster, with identical text.
* 🛠️ **Stability improvements:** float math now rounds exactly like the PC build, and if `th10.dat` isn't found the port shows where it looked instead of closing.
* 🎮 **Controls like the other Touhou ports:** the in-game **Key Config** works with the Switch buttons, ZL/ZR act as L/R, and the D-Pad and sticks only move. The default layout is the same as every other port (B shoot, A bomb, L/ZL focus, R/ZR skip, + pause).
* 📁 **One folder for all Touhou ports:** `sd:/switch/touhou/touhou10/` is recommended; the NRO's own folder is always checked first, and the old locations still work.

---

## ✨ Key Features

* ⬛ **OLED-Friendly Pillarboxing:** the original 640×480 playfield is centred with pure black (`#000000`) bars and an aspect-correct upscale.
* 🔊 **Full Audio:** sound effects plus BGM streamed from `thbgm.dat` through SDL's audio backend at 48 kHz stereo.
* 🎮 **Sane, Remappable Controls:** Joy-Con (handheld, grip, detached) and Pro Controller with the same default layout as every other Touhou Switch port (B shoot, A bomb, L/ZL focus, R/ZR skip, + pause); the in-game **Key Config** can rebind them.
* 🌏 **Language-Aware Title:** hbmenu shows the original Japanese title (`東方風神録　～ Mountain of Faith`) on consoles set to 日本語 and the romanised one everywhere else, filled across all 16 NACP language slots.
* 💾 **Saves Next to the Data:** `th10.cfg`, `score.dat`, replays (`th10_01.rpy` …) and snapshots are written into the same SD folder the game loaded from. Without `msgothic.ttc`, the port falls back to the Switch shared system fonts.

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

Host-side unit tests (no game data, no SDK):

```bash
./tests/run.sh
```

A desktop Linux test build (same sources, for validation without a console) is also supported via plain CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

---

## 📂 How It Works

The game logic is the architecture-independent C++ from upstream, shipped here as `wasm2c/th10_wasm.c` (generated from `th10-game.wasm` with wabt's wasm2c) and linked against the vendored wasm2c runtime in 32-bit-memory mode. Everything platform-specific lives in `src/host_*` or behind `#ifdef __SWITCH__`:

| File | Purpose |
| :--- | :--- |
| `src/main.cpp` | startup, frame loop, SDL window/input/audio wiring |
| `src/draw_batch.hpp` | sprite instancing + triangle batching (no GL) |
| `src/host_files.cpp` | DAT archive + SD save-folder filesystem |
| `src/host_graphics.cpp` / `src/gl_renderer.cpp` | D3D9-style device on OpenGL ES 3 |
| `src/host_audio.cpp` / `src/audio_mixer.cpp` | SFX + BGM mixer |
| `src/host_fonts.cpp` | CP932/GBK text via FreeType (MS Gothic or system fonts) |
| `src/host_resample.cpp` | native, bit-exact copy of the game's triangle-filter resample (dialogue text); the generated `f731` calls it first |
| `src/host_input.cpp` | keyboard + gamepad mapping |
| `src/host_time.cpp` | monotonic clock / RNG seed |
| `platform/switch/icon.jpg` | 256×256 NRO icon (the original game cover) |
| `scripts/build_switch.sh` | one-shot Switch build |
| `scripts/nacp_lang.py` | Japanese NACP title slot |
| `tests/` | CP932/GBK decoder + batching unit tests |

On Horizon the wasm linear memory uses a malloc backend (`WASM_RT_USE_MMAP=0`); per-access bounds/stack traps are elided in Release because Horizon has no guard pages.

---

## 🤝 Credits & Acknowledgments

* **ZUN / Team Shanghai Alice** — original creator and developer of the Touhou Project series.
* **[YomotsuHisami](https://github.com/YomotsuHisami/th10)** — the clean-room TH10 C++ reimplementation this port is built on.
* **wabt project** — the wasm2c compiler and runtime.
* **Switchbrew & devkitPro Team** — the open-source `libnx` SDK and Switch toolchain.
* Port developed with AI assistance.

**Licensing:** the Switch host code (`src/`, `platform/`, `scripts/`, `tests/`, `switch_include/`, build files) is CC0 1.0 (see `LICENSE`). `wasm2c/th10_wasm.c` / `th10_wasm.h` are generated from the TH10 game module by YomotsuHisami, which does not state a licence — that code stays theirs and is not covered by `LICENSE` (the only Switch change in it is the hook that calls `src/host_resample.cpp`). The wasm2c runtime (`wasm2c/wasm-rt*`) is Apache-2.0, see the file headers.
