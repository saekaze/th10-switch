# Touhou 10: Mountain of Faith — Switch Port r3

Native Nintendo Switch homebrew port of TH10 v1.00a (`touhou10.nro`).

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
