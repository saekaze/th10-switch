#include "host_input.hpp"

#include <cmath>
#include <cstring>

#include "wasm_host.hpp"

namespace {

// DirectInput scan codes by browser code (keyboard-state.mjs scanCodes).
struct CodeMap {
    const char* code;
    uint8_t value;
};
const CodeMap kScan[] = {
    {"Escape", 1}, {"Digit1", 2}, {"Digit2", 3}, {"Digit3", 4}, {"Digit4", 5},
    {"Digit5", 6}, {"Digit6", 7}, {"Digit7", 8}, {"Digit8", 9}, {"Digit9", 10},
    {"Digit0", 11}, {"Minus", 12}, {"Equal", 13}, {"Backspace", 14}, {"Tab", 15},
    {"KeyQ", 16}, {"KeyW", 17}, {"KeyE", 18}, {"KeyR", 19}, {"KeyT", 20},
    {"KeyY", 21}, {"KeyU", 22}, {"KeyI", 23}, {"KeyO", 24}, {"KeyP", 25},
    {"BracketLeft", 26}, {"BracketRight", 27}, {"Enter", 28},
    {"ControlLeft", 29}, {"KeyA", 30}, {"KeyS", 31}, {"KeyD", 32},
    {"KeyF", 33}, {"KeyG", 34}, {"KeyH", 35}, {"KeyJ", 36}, {"KeyK", 37},
    {"KeyL", 38}, {"Semicolon", 39}, {"Quote", 40}, {"Backquote", 41},
    {"ShiftLeft", 42}, {"Backslash", 43}, {"KeyZ", 44}, {"KeyX", 45},
    {"KeyC", 46}, {"KeyV", 47}, {"KeyB", 48}, {"KeyN", 49}, {"KeyM", 50},
    {"Comma", 51}, {"Period", 52}, {"Slash", 53}, {"ShiftRight", 54},
    {"NumpadMultiply", 55}, {"AltLeft", 56}, {"Space", 57}, {"CapsLock", 58},
    {"F1", 59}, {"F2", 60}, {"F3", 61}, {"F4", 62}, {"F5", 63}, {"F6", 64},
    {"F7", 65}, {"F8", 66}, {"F9", 67}, {"F10", 68}, {"NumLock", 69},
    {"ScrollLock", 70}, {"Numpad7", 71}, {"Numpad8", 72}, {"Numpad9", 73},
    {"NumpadSubtract", 74}, {"Numpad4", 75}, {"Numpad5", 76}, {"Numpad6", 77},
    {"NumpadAdd", 78}, {"Numpad1", 79}, {"Numpad2", 80}, {"Numpad3", 81},
    {"Numpad0", 82}, {"NumpadDecimal", 83}, {"F11", 87}, {"F12", 88},
    {"NumpadEnter", 156}, {"ControlRight", 157}, {"NumpadDivide", 181},
    {"PrintScreen", 183}, {"AltRight", 184}, {"Pause", 197}, {"Home", 199},
    {"ArrowUp", 200}, {"PageUp", 201}, {"ArrowLeft", 203}, {"ArrowRight", 205},
    {"End", 207}, {"ArrowDown", 208}, {"PageDown", 209}, {"Insert", 210},
    {"Delete", 211}, {"MetaLeft", 219}, {"MetaRight", 220},
    {"ContextMenu", 221},
};

// Explicit virtual-key overrides (keyboard-state.mjs virtualKeyboard map).
const CodeMap kVirt[] = {
    {"ArrowLeft", 37}, {"ArrowUp", 38}, {"ArrowRight", 39}, {"ArrowDown", 40},
    {"ShiftLeft", 160}, {"ShiftRight", 161}, {"ControlLeft", 162},
    {"ControlRight", 163}, {"AltLeft", 164}, {"AltRight", 165}, {"Escape", 27},
    {"Enter", 13}, {"NumpadEnter", 13}, {"Space", 32}, {"Backspace", 8},
    {"Tab", 9}, {"CapsLock", 20}, {"PageUp", 33}, {"PageDown", 34},
    {"Home", 36}, {"End", 35}, {"Insert", 45}, {"Delete", 46},
    {"PrintScreen", 44}, {"Pause", 19}, {"Minus", 189}, {"Equal", 187},
    {"BracketLeft", 219}, {"BracketRight", 221}, {"Backslash", 220},
    {"Semicolon", 186}, {"Quote", 222}, {"Backquote", 192}, {"Comma", 188},
    {"Period", 190}, {"Slash", 191}, {"NumpadMultiply", 106},
    {"NumpadAdd", 107}, {"NumpadSubtract", 109}, {"NumpadDecimal", 110},
    {"NumpadDivide", 111},
};

int lookup(const CodeMap* table, size_t n, const std::string& code) {
    for (size_t i = 0; i < n; i++)
        if (code == table[i].code) return table[i].value;
    return 0;
}

int virtual_code(const std::string& code) {
    int v = lookup(kVirt, sizeof(kVirt) / sizeof(kVirt[0]), code);
    if (v) return v;
    if (code.size() == 4 && code.compare(0, 3, "Key") == 0)
        return (unsigned char)code[3];
    if (code.size() == 6 && code.compare(0, 5, "Digit") == 0 &&
        code[5] >= '0' && code[5] <= '9')
        return (unsigned char)code[5];
    if (code.size() == 7 && code.compare(0, 6, "Numpad") == 0 &&
        code[6] >= '0' && code[6] <= '9')
        return 96 + (code[6] - '0');
    if (code.size() >= 2 && code[0] == 'F') {
        bool digits = true;
        for (size_t i = 1; i < code.size(); i++)
            if (code[i] < '0' || code[i] > '9') digits = false;
        if (digits) return 111 + atoi(code.c_str() + 1);
    }
    return 0;
}

}  // namespace

std::string sdl_scancode_to_code(int sc) {
    static const char* letters[] = {
        "KeyA", "KeyB", "KeyC", "KeyD", "KeyE", "KeyF", "KeyG", "KeyH",
        "KeyI", "KeyJ", "KeyK", "KeyL", "KeyM", "KeyN", "KeyO", "KeyP",
        "KeyQ", "KeyR", "KeyS", "KeyT", "KeyU", "KeyV", "KeyW", "KeyX",
        "KeyY", "KeyZ"};
    if (sc >= 4 && sc <= 29) return letters[sc - 4];
    static const char* digits[] = {"Digit1", "Digit2", "Digit3", "Digit4",
                                   "Digit5", "Digit6", "Digit7", "Digit8",
                                   "Digit9", "Digit0"};
    if (sc >= 30 && sc <= 39) return digits[sc - 30];
    switch (sc) {
        case 40: return "Enter";
        case 41: return "Escape";
        case 42: return "Backspace";
        case 43: return "Tab";
        case 44: return "Space";
        case 45: return "Minus";
        case 46: return "Equal";
        case 47: return "BracketLeft";
        case 48: return "BracketRight";
        case 49: return "Backslash";
        case 51: return "Semicolon";
        case 52: return "Quote";
        case 53: return "Backquote";
        case 54: return "Comma";
        case 55: return "Period";
        case 56: return "Slash";
        case 57: return "CapsLock";
        case 58: return "F1";
        case 59: return "F2";
        case 60: return "F3";
        case 61: return "F4";
        case 62: return "F5";
        case 63: return "F6";
        case 64: return "F7";
        case 65: return "F8";
        case 66: return "F9";
        case 67: return "F10";
        case 68: return "F11";
        case 69: return "F12";
        case 70: return "PrintScreen";
        case 71: return "ScrollLock";
        case 72: return "Pause";
        case 73: return "Insert";
        case 74: return "Home";
        case 75: return "PageUp";
        case 76: return "Delete";
        case 77: return "End";
        case 78: return "PageDown";
        case 79: return "ArrowRight";
        case 80: return "ArrowLeft";
        case 81: return "ArrowDown";
        case 82: return "ArrowUp";
        case 83: return "NumLock";
        case 84: return "NumpadDivide";
        case 85: return "NumpadMultiply";
        case 86: return "NumpadSubtract";
        case 87: return "NumpadAdd";
        case 88: return "NumpadEnter";
        case 89: return "Numpad1";
        case 90: return "Numpad2";
        case 91: return "Numpad3";
        case 92: return "Numpad4";
        case 93: return "Numpad5";
        case 94: return "Numpad6";
        case 95: return "Numpad7";
        case 96: return "Numpad8";
        case 97: return "Numpad9";
        case 98: return "Numpad0";
        case 99: return "NumpadDecimal";
        case 101: return "ContextMenu";
        case 224: return "ControlLeft";
        case 225: return "ShiftLeft";
        case 226: return "AltLeft";
        case 227: return "MetaLeft";
        case 228: return "ControlRight";
        case 229: return "ShiftRight";
        case 230: return "AltRight";
        case 231: return "MetaRight";
        default: return "";
    }
}

void switch_pad_to_codes(const SwitchPadState& pad,
                         std::set<std::string>& out) {
    bool left = pad.dleft || pad.lx < -0.5f;
    bool right = pad.dright || pad.lx > 0.5f;
    bool up = pad.dup || pad.ly < -0.5f;
    bool down = pad.ddown || pad.ly > 0.5f;
    if (left) out.insert("ArrowLeft");
    if (right) out.insert("ArrowRight");
    if (up) out.insert("ArrowUp");
    if (down) out.insert("ArrowDown");
    // r4: shot / bomb / focus / pause / skip are no longer mirrored as fixed
    // keyboard keys. They reach the game only as gamepad buttons (see
    // switch_pad_to_inputpad), so TH10's own Key Config can remap them; the
    // default config gives the same layout as before.
    // pad.x / pad.y are SDL's positional X / Y: the Switch's Y / X buttons.
    if (pad.y) out.insert("Enter");  // Switch X
    if (pad.x) out.insert("KeyR");   // Switch Y: retry
    if (pad.minus) out.insert("KeyP");  // screenshot
}

InputPad switch_pad_to_inputpad(const SwitchPadState& pad) {
    // Button numbers as TH10 sees them (Key Config shows these numbers).
    // TH10's default pad config is shot=0, bomb=1, slow=2, pause=3, skip=4,
    // so a fresh th10.cfg gives the classic layout: B shot, A bomb,
    // L focus, + pause, R skip. Remapping in Key Config then just works.
    // (pad.a/pad.b are SDL's positional A/B: the Switch's B/A buttons.)
    InputPad p;
    p.buttons.assign(16, 0);
    p.buttons[0] = pad.a;                 // Switch B
    p.buttons[1] = pad.b;                 // Switch A
    p.buttons[2] = pad.l || pad.zl;       // L / ZL
    p.buttons[3] = pad.plus;              // +
    p.buttons[4] = pad.r || pad.zr;       // R / ZR
    p.buttons[5] = pad.y;                 // Switch X
    p.buttons[6] = pad.x;                 // Switch Y
    // ZL / ZR act as L / R (like the other Touhou ports); − is the
    // screenshot key and not assignable.
    // r5: the d-pad and sticks (clicks included) are never buttons, so Key
    // Config cannot pick them. TH10 reads direction from the stick axes, and
    // the host mirrors the d-pad and stick as arrow keys.
    p.axes = {pad.lx, pad.ly, 0.0f, 0.0f};
    return p;
}

void write_input_snapshot(Host* host, uint32_t snapshot,
                          const std::set<std::string>& keys,
                          const InputPad* pads, int pad_count, bool focused) {
    w2c_th100x2Dgame* w = host->wasm;
    uint8_t* bytes = mem_bytes(w, snapshot, 1172);
    memset(bytes, 0, 1172);
    // Virtual keys [0,256) + scan codes [256,512).
    for (const auto& code : keys) {
        int v = virtual_code(code);
        if (v) {
            bytes[v] = 128;
            if (v >= 160 && v <= 165) bytes[16 + (v - 160) / 2] = 128;
        }
        int s = lookup(kScan, sizeof(kScan) / sizeof(kScan[0]), code);
        if (s) bytes[256 + s] = 128;
    }
    auto wu32 = [&](uint32_t off, uint32_t v) {
        memcpy(bytes + off, &v, 4);
    };
    auto wi32 = [&](uint32_t off, int32_t v) {
        memcpy(bytes + off, &v, 4);
    };
    wi32(1168, focused ? 1 : 0);
    for (int dev = 0; dev < 2; dev++) {
        if (dev >= pad_count || !pads) continue;
        const InputPad& pad = pads[dev];
        wu32(1160 + dev * 4, 1);
        uint32_t direct = 512 + dev * 272, legacy = 1056 + dev * 52;
        auto button = [&](size_t i) -> int {
            return i < pad.buttons.size() && pad.buttons[i] ? 1 : 0;
        };
        auto axis = [&](size_t i) -> float {
            return i < pad.axes.size() ? pad.axes[i] : 0.0f;
        };
        int x = button(15) - button(14), y = button(13) - button(12);
        uint32_t pov;
        if (x || y) {
            long deg = lround(atan2((double)x, (double)-y) * 180.0 / M_PI);
            pov = (uint32_t)(((deg + 360) % 360) * 100);
        } else {
            pov = 0xffffffffu;
        }
        for (int i = 0; i < 2; i++) {
            float a = axis(i);
            if (a < -1) a = -1;
            if (a > 1) a = 1;
            wi32(direct + i * 4, (int32_t)lround(-1000 + (a + 1) / 2 * 2000));
        }
        for (int i = 0; i < 4; i++) wu32(direct + 32 + i * 4, i ? 0xffffffffu : pov);
        size_t nb = pad.buttons.size() < 128 ? pad.buttons.size() : 128;
        for (size_t i = 0; i < nb; i++)
            bytes[direct + 48 + i] = pad.buttons[i] ? 128 : 0;
        wu32(legacy, 52);
        wu32(legacy + 4, 255);
        for (int i = 0; i < 6; i++)
            wu32(legacy + 8 + i * 4,
                 (uint32_t)lround((axis(i) + 1) * 32767.5));
        uint32_t pressed = 0;
        size_t nb32 = pad.buttons.size() < 32 ? pad.buttons.size() : 32;
        for (size_t i = 0; i < nb32; i++)
            if (pad.buttons[i]) pressed |= 1u << i;
        wu32(legacy + 32, pressed);
        uint32_t lowbit = 0;
        if (pressed) {
            uint32_t iso = pressed & (0u - pressed);
            lowbit = 32u - (uint32_t)__builtin_clz(iso);
        }
        wu32(legacy + 36, lowbit);
        wu32(legacy + 40, pov);
    }
}
