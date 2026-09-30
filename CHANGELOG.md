# Changelog — Touhou 10 Switch port

## Touhou 10: Mountain of Faith — Switch Port r5

## What's new in r5

* **No more lag when skipping dialogue:** every dialogue line is drawn at
  twice its size and filtered down with D3DX's triangle filter. The game
  module this port runs emulated every multiply-add of that filter in 80-bit
  software floating point: about 27 ms per line on a desktop CPU and far more
  on the Switch, so skipping several lines in a row stalled the game. The
  port now runs that filter natively (`src/host_resample.cpp`), the same fast
  path upstream YomotsuHisami/th10 adopted later: in the game's x87 mode
  (single precision, round to nearest) plain float math rounds identically.
  About 0.7 ms per line now. Host test `tests/test_resample.cpp` runs the
  module's own filter and the native one on the same inputs: byte-identical
  output. Any other arithmetic mode still uses the original code.
* **Float math matches PC:** GCC fused 209 multiply-adds in the game module
  on ARM64, which wasm semantics (and upstream's build) forbid. It is now
  compiled with `-ffp-contract=off`, so every float operation rounds as it
  does in the browser/PC build.
* Key Config: the D-pad and stick clicks can no longer be picked as
  buttons (they only move). Left-stick click no longer doubles as focus.
* Display version `1.00a-r5`.

---

## Touhou 10: Mountain of Faith — Switch Port r4

Native Nintendo Switch homebrew port of TH10 v1.00a (`touhou10.nro`).

r4 is a frame-pacing and controls release on top of r3. Gameplay, scoring,
replay format and pixels are unchanged.

## What r4 fixes

* **59.8 FPS / judder after a continue (and after long play):** the game's
  60 Hz frame limiter ran on the wall clock while the display ran on vsync.
  A Switch panel that is not exactly 60.000 Hz, plus the sleep the loop did
  after every swap, periodically landed a vsync just before the game's
  deadline, so that vsync showed the previous frame again. The host now
  feeds the game a vsync-driven clock (exactly one game frame per presented
  frame, never behind real time), the same "scheduled tick" upstream
  YomotsuHisami/th10 adopted. Host test `tests/test_pacing.cpp` replays the
  game's deadline logic: 0 stutters per 30 minutes at 60.00 / 60.05 /
  59.94 Hz, versus 16 / 80 / 1 with the old clock.
* **Performance in busy scenes and heavy spell cards (Kanako's last card is the easiest place to see it):** the old loop slept until
  the game's deadline *after* each vsync, so a frame only had whatever was
  left of the 16.7 ms period - sometimes a few milliseconds - before the
  next vsync. Frames now start right after the vsync with the full budget.
* **Controls are remappable:** B/A/L/+/R no longer also inject fixed
  keyboard keys; they reach the game only as gamepad buttons numbered to
  match TH10's default pad config, so a fresh `th10.cfg` gives the same
  layout as before and the in-game Key Config can change it.
  Display version is `1.00a-r4`.

## r3 (previous)

r3 is a host-side performance release on top of r2. Gameplay, scoring, replay
format and pixels are unchanged; remaining spell-card hitch and dialogue
dips are cut by removing per-sprite heap traffic and tightening GL / font
work.

## What r3 fixes

* **Spell-card CPU:** the D3D device now keeps a live `CompactPipeline`.
  `SetTransform` is a 64-byte memcpy (no `std::vector` / `std::map` per
  sprite). Draw*() copies that struct; it no longer walks state maps.
  Graphics objects are a handle vector (O(1) lookup).
* **GL apply:** matching pipeline keys skip uniform and enable/blend
  traffic. Viewport, bind and sampler caches from r2 stay. World-UV
  instance batches flush at 2048 sprites.
* **Dialogue / font:** glyph "over" composite clips to the bitmap, takes
  an opaque `src_a==255` packed write, and uses 32-bit math. The FreeType
  cache is never wiped. UnlockSurface dirty-rects so only the locked
  pixels are converted and `glTexSubImage2D`'d.
* **CPU clock:** Horizon CPU bus is set to 1785 MHz (same boost other
  native ports use). GPU clocks stay with the applet.
* **hbmenu title:** NACP matches saekaze/th07 — Japanese slot is
  `東方風神録　～ Mountain of Faith` / `上海アリス幻樂団`; the other 15
  language slots (including Korean/Chinese/pt-BR that nacptool leaves
  blank) get the romanised title. Display version is `1.00a-r3`.

r2 notes still apply: instanced danmaku, triangle coalescing, vsync as
the frame fence, `TH10_FAST_MEMCHECK`, streaming VBOs, early-Z program.

## Install

Copy to `sd:/switch/th10/` next to your legally owned game data:

```text
sd:/switch/th10/
    ├── touhou10.nro
    ├── th10.dat
    ├── thbgm.dat     # optional (BGM)
    └── msgothic.ttc  # recommended (system fonts are used as fallback)
```

Rebuild with `./scripts/build_switch.sh` (devkitA64 + switch portlibs).

## Notes

* Full game: menus, options, replays, Music Room, gameplay, pause, snapshots.
* Fixed Switch controls (see README); − (Minus) takes screenshots.
* Saves, replays and snapshots are written next to the game data.

Host tests (no game data / no SDK): `./tests/run.sh`

Game data is **not** included — you must own a copy of TH10 v1.00a.
