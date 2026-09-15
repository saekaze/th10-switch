// C++ port of site/runtime/native-input-state.mjs + keyboard-state.mjs,
// fed by SDL instead of browser events. Writes the 1172-byte input snapshot.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>
struct Host;

// A gamepad in web Gamepad order: buttons[0..], axes[0..].
struct InputPad {
    std::vector<uint8_t> buttons;  // 0/1
    std::vector<float> axes;       // -1..1
};

// Translate one SDL scancode to a browser `code` string ("KeyZ", ...).
// Returns "" for unmapped keys.
std::string sdl_scancode_to_code(int scancode);

// Switch controller button/axis state -> browser codes (mirrored into the
// keyboard snapshot so pad-only play works everywhere) + an InputPad.
struct SwitchPadState {
    bool dleft, dright, dup, ddown;
    float lx, ly;  // -1..1
    bool a, b, x, y, l, r, zl, zr, plus, minus, lstick, rstick;
};
void switch_pad_to_codes(const SwitchPadState& pad, std::set<std::string>& out);
InputPad switch_pad_to_inputpad(const SwitchPadState& pad);

void write_input_snapshot(Host* host, uint32_t snapshot,
                          const std::set<std::string>& keys,
                          const InputPad* pads, int pad_count, bool focused);
