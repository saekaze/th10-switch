// TH10 Switch/Linux entry: SDL2 + GLES3 + wasm2c runtime.
// Mirrors site/runtime/native-game.mjs createNativeGame + the worker loop.
#include <SDL.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <string>
#include <vector>

#include "../wasm2c/th10_wasm.h"
#include "audio_mixer.hpp"
#include "gl_renderer.hpp"
#include "host_audio.hpp"
#include "host_files.hpp"
#include "host_fonts.hpp"
#include "host_graphics.hpp"
#include "host_input.hpp"
#include "host_time.hpp"
#include "draw_batch.hpp"
#include "wasm_host.hpp"

void host_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    fflush(stderr);
}

namespace {

struct Args {
#ifdef __SWITCH__
    std::string data_dir = "sdmc:/switch/th10";
    std::string save_dir = "sdmc:/switch/th10";
#else
    std::string data_dir = "testdata";
    std::string save_dir = "th10_save";
#endif
    bool chinese = false;
    int frames = 0;  // auto-stop after N frames (0 = run forever)
    bool headless = false;
    bool no_audio = false;
    bool fast = false;       // no delay sleep
    bool clip_control = false;  // EXT_clip_control fast path (off = Switch path)
    std::string wav;         // virtual audio -> WAV file (implies no device)
    int seed = -1;           // -1 = time-based like the browser
    std::string press;       // injected keys: "Z@60-90,Down@150-155"
    int log_every = 300;     // status log cadence (frames, 0 = off)
};

struct PressRange {
    std::string code;
    int start, end;
};

std::string short_to_code(const std::string& s) {
    if (s == "Up") return "ArrowUp";
    if (s == "Down") return "ArrowDown";
    if (s == "Left") return "ArrowLeft";
    if (s == "Right") return "ArrowRight";
    if (s == "Shift") return "ShiftLeft";
    if (s == "Ctrl") return "ControlLeft";
    if (s == "Alt") return "AltLeft";
    if (s == "Esc") return "Escape";
    if (s == "Enter") return "Enter";
    if (s == "Space") return "Space";
    if (s.size() == 1 && s[0] >= 'A' && s[0] <= 'Z')
        return std::string("Key") + s;
    if (s.size() == 1 && s[0] >= '0' && s[0] <= '9')
        return std::string("Digit") + s;
    return s;
}

std::vector<PressRange> parse_press(const std::string& s) {
    std::vector<PressRange> out;
    size_t i = 0;
    while (i < s.size()) {
        size_t comma = s.find(',', i);
        std::string tok = s.substr(i, comma == std::string::npos
                                          ? std::string::npos
                                          : comma - i);
        size_t at = tok.find('@');
        if (at == std::string::npos) break;
        std::string name = tok.substr(0, at);
        std::string range = tok.substr(at + 1);
        size_t dash = range.find('-');
        int start = atoi(range.c_str());
        int end = dash == std::string::npos ? start : atoi(range.c_str() + dash + 1);
        out.push_back({short_to_code(name), start, end});
        if (comma == std::string::npos) break;
        i = comma + 1;
    }
    return out;
}

uint64_t wall_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void write_wav16(const char* path, const std::vector<float>& samples,
                 int rate) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        host_log("cannot write wav %s", path);
        return;
    }
    uint32_t data_bytes = (uint32_t)samples.size() * 2;
    uint8_t hdr[44] = {0};
    memcpy(hdr, "RIFF", 4);
    uint32_t chunk = 36 + data_bytes;
    memcpy(hdr + 4, &chunk, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    hdr[16] = 16;
    hdr[20] = 1;
    hdr[22] = 2;
    memcpy(hdr + 24, &rate, 4);
    uint32_t br = rate * 4;
    memcpy(hdr + 28, &br, 4);
    hdr[32] = 4;
    hdr[34] = 16;
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &data_bytes, 4);
    fwrite(hdr, 1, 44, f);
    for (float s : samples) {
        if (s < -1) s = -1;
        if (s > 1) s = 1;
        int16_t v = (int16_t)lround(s * 32767);
        fwrite(&v, 1, 2, f);
    }
    fclose(f);
    host_log("wrote %s (%u samples)", path, (unsigned)samples.size());
}

void audio_callback(void* userdata, Uint8* stream, int len) {
    AudioMixer* mixer = (AudioMixer*)userdata;
    mixer->render((float*)stream, len / 8);  // stereo f32
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&](const char* name) -> const char* {
            size_t n = strlen(name);
            if (a.compare(0, n, name) == 0 && a.size() > n && a[n] == '=')
                return a.c_str() + n + 1;
            return nullptr;
        };
        const char* v = nullptr;
        if ((v = val("--data")))
            args.data_dir = v;
        else if ((v = val("--save")))
            args.save_dir = v;
        else if ((v = val("--frames")))
            args.frames = atoi(v);
        else if ((v = val("--wav")))
            args.wav = v;
        else if ((v = val("--seed")))
            args.seed = atoi(v);
        else if ((v = val("--press")))
            args.press = v;
        else if ((v = val("--log-every")))
            args.log_every = atoi(v);
        else if (a == "--chs")
            args.chinese = true;
        else if (a == "--headless")
            args.headless = true;
        else if (a == "--no-audio")
            args.no_audio = true;
        else if (a == "--fast")
            args.fast = true;
        else if (a == "--clip-control")
            args.clip_control = true;
        else {
            host_log("unknown arg %s", a.c_str());
            return 1;
        }
    }
    if (!args.wav.empty()) args.no_audio = true;
    std::vector<PressRange> presses = parse_press(args.press);

    uint32_t sdl_flags = 0;
    if (!args.headless) sdl_flags |= SDL_INIT_VIDEO;
    if (!args.no_audio) sdl_flags |= SDL_INIT_AUDIO;
    sdl_flags |= SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER;
    if (SDL_Init(sdl_flags) != 0) {
        host_log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
#ifdef __SWITCH__
    // 1785 MHz CPU. Handheld/docked GPU clocks stay under Horizon.
    if (R_SUCCEEDED(clkrstInitialize())) {
        ClkrstSession cpu{};
        if (R_SUCCEEDED(clkrstOpenSession(&cpu, PcvModuleId_CpuBus, 3))) {
            clkrstSetClockRate(&cpu, 1785000000);
            clkrstCloseSession(&cpu);
        }
        clkrstExit();
    }
#endif

    SDL_Window* window = nullptr;
    SDL_GLContext glctx = nullptr;
    int drawable_w = 1280, drawable_h = 720;
    if (!args.headless) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
#ifdef __SWITCH__
        win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
        window = SDL_CreateWindow("Touhou 10 - Mountain of Faith",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, 1280, 720,
                                  win_flags);
        if (!window) {
            host_log("SDL_CreateWindow failed: %s", SDL_GetError());
            return 1;
        }
#ifdef __SWITCH__
        SDL_ShowCursor(SDL_DISABLE);
#endif
        glctx = SDL_GL_CreateContext(window);
        if (!glctx) {
            host_log("SDL_GL_CreateContext failed: %s", SDL_GetError());
            return 1;
        }
        // vsync is the frame fence; don't also SDL_Delay(1) on a late frame.
        SDL_GL_SetSwapInterval(1);
        SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
        host_log("drawable %dx%d: %s", drawable_w, drawable_h,
                 (const char*)glGetString(GL_VERSION));
    }

    AudioMixer mixer(48000.0);
    SDL_AudioDeviceID audio_dev = 0;
    if (!args.no_audio) {
        SDL_AudioSpec want, got;
        memset(&want, 0, sizeof(want));
        want.freq = 48000;
        want.format = AUDIO_F32SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = audio_callback;
        want.userdata = &mixer;
        audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
        if (!audio_dev) {
            host_log("SDL_OpenAudioDevice failed: %s", SDL_GetError());
            return 1;
        }
        mixer.set_rate(got.freq);
        host_log("audio %dHz f32 stereo", got.freq);
        SDL_PauseAudioDevice(audio_dev, 0);
    }

    SDL_GameController* controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) {
                host_log("controller: %s", SDL_GameControllerName(controller));
                break;
            }
        }
    }

    // ---- host wiring ----
    static Host host;
    static w2c_th10__files files_import;
    static w2c_th10__graphics graphics_import;
    static w2c_th10__fonts fonts_import;
    static w2c_th10__audio audio_import;
    static w2c_th10__time time_import;
    static FilesHost files_host;
    static GraphicsHost graphics_host;
    static AudioHost audio_host;
    static FontsHost fonts_host;
    host.files = &files_host;
    host.graphics = &graphics_host;
    host.audio = &audio_host;
    host.fonts = &fonts_host;
    host.save_dir = args.save_dir;
    host.data_dir = args.data_dir;
    host.chinese = args.chinese;
    files_import.host = &host;
    graphics_import.host = &host;
    fonts_import.host = &host;
    audio_import.host = &host;
    time_import.host = &host;
    audio_host.mixer = &mixer;

    if (!files_host.init(&host, args.data_dir, args.save_dir, args.chinese))
        return 1;
    if (!fonts_host.init(&host, args.data_dir)) return 1;

    wasm_rt_init();
    static w2c_th100x2Dgame wasm;
    wasm2c_th100x2Dgame_instantiate(&wasm, &audio_import, &files_import,
                                   &fonts_import, &graphics_import,
                                   &time_import);
    host.wasm = &wasm;
    // Pregrow linear memory so DAT/texture loads don't realloc mid-stage.
    {
        uint64_t have = wasm.w2c_memory.pages;
        const uint64_t want = 2048;  // 128 MiB
        if (have < want) {
            wasm_rt_grow_memory(&wasm.w2c_memory, want - have);
            host_log("wasm memory %llu pages (%llu bytes)",
                     (unsigned long long)wasm.w2c_memory.pages,
                     (unsigned long long)wasm.w2c_memory.size);
        }
    }
    graphics_host.host = &host;
    audio_host.host = &host;
    files_host.host = &host;
    fonts_host.host = &host;

    std::unique_ptr<GLRenderer> renderer;
    if (!args.headless) {
        renderer.reset(
            new GLRenderer(&host, drawable_w, drawable_h, args.clip_control));
        if (!renderer->init()) return 1;
        graphics_host.renderer = renderer.get();
    }

    if (args.fast) time_set_virtual(true);

    // ---- startup sequence (native-game.mjs) ----
    uint32_t params = graphics_host.alloc(56);
    uint32_t name = graphics_host.alloc(256);
    uint32_t rng = graphics_host.alloc(16);
    uint32_t rate = graphics_host.alloc(4);
    uint32_t quit = graphics_host.alloc(4);
    uint32_t touch_state = graphics_host.alloc(32);
    (void)touch_state;
    uint32_t file_system = w2c_th100x2Dgame_files_create(&wasm);
    {
        const char* archive = args.chinese ? "th10c.dat" : "th10.dat";
        uint8_t* p = mem_bytes(&wasm, name, 256);
        size_t n = strlen(archive) + 1;
        memcpy(p, archive, n);
    }
    if (!w2c_th100x2Dgame_files_attach(&wasm, file_system, name)) {
        host_log("Cannot open the game archive.");
        return 1;
    }
    uint32_t seed = args.seed >= 0
                        ? (uint32_t)args.seed & 65535
                        : (uint32_t)(time(nullptr) & 0xffff);
    host_log("seed %u", seed);
    mem_wu32(&wasm, rng, seed);
    mem_wu32(&wasm, rng + 4, 0);
    mem_wu32(&wasm, rng + 8, seed);
    mem_wu32(&wasm, rng + 12, 0);
    mem_wf32(&wasm, rate, 1.0f);
    {
        uint32_t p14[14] = {640, 480, 22, 1, 0, 0, 1, 0, 1, 1, 80, 0, 0, 0};
        memcpy(mem_bytes(&wasm, params, 56), p14, 56);
    }
    uint32_t input = w2c_th100x2Dgame_input_create(&wasm);
    uint32_t state =
        w2c_th100x2Dgame_game_state_create(&wasm, input, args.chinese ? 1 : 0);
    uint32_t device = w2c_th100x2Dgame_graphics_create(&wasm, params, 0x40);
    uint32_t engine = w2c_th100x2Dgame_animation_engine_create(
        &wasm, file_system, device, rng, rng + 8, rate);
    uint32_t fonts =
        w2c_th100x2Dgame_fonts_create(&wasm, device, rng, args.chinese ? 1 : 0);
    uint32_t audio = w2c_th100x2Dgame_audio_create(&wasm, file_system);
    uint32_t effects =
        w2c_th100x2Dgame_effects_create(&wasm, engine, quit, 0);
    uint32_t app = w2c_th100x2Dgame_application_create(
        &wasm, file_system, input, state, engine, fonts, audio, effects);
    if (!app) {
        host_log("C++ game initialization failed.");
        return 1;
    }
    uint32_t snapshot = w2c_th100x2Dgame_input_snapshot(&wasm, input);
    uint32_t economy = w2c_th100x2Dgame_game_state_economy(&wasm, state);
    host_log("app=%u snapshot=%u economy=%u", app, snapshot, economy);

    // ---- frame loop ----
    std::set<int> pressed;
    bool quit_requested = false;
    uint64_t prev = wall_ms();
    double audio_rem = 0;
    bool stop_sent = false;
    uint64_t steps = 0;
    std::vector<float> wav_samples;
    std::vector<float> wav_block;
    bool virtual_audio = !args.wav.empty();
    if (virtual_audio) wav_block.resize(800 * 2);
    for (;;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                quit_requested = true;
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                pressed.insert(ev.key.keysym.scancode);
            } else if (ev.type == SDL_KEYUP) {
                pressed.erase(ev.key.keysym.scancode);
            }
        }
        if (quit_requested && !stop_sent) {
            w2c_th100x2Dgame_application_stop(&wasm, app);
            stop_sent = true;
        }
        std::set<std::string> keys;
        for (int sc : pressed) {
            std::string c = sdl_scancode_to_code(sc);
            if (!c.empty()) keys.insert(c);
        }
        int frame = (int)graphics_host.frames;
        for (const auto& pr : presses)
            if (frame >= pr.start && frame <= pr.end) keys.insert(pr.code);
        InputPad pads[2];
        int pad_count = 0;
        if (controller) {
            SwitchPadState sp{};
            sp.a = SDL_GameControllerGetButton(
                controller, SDL_CONTROLLER_BUTTON_A);
            sp.b = SDL_GameControllerGetButton(
                controller, SDL_CONTROLLER_BUTTON_B);
            sp.x = SDL_GameControllerGetButton(
                controller, SDL_CONTROLLER_BUTTON_X);
            sp.y = SDL_GameControllerGetButton(
                controller, SDL_CONTROLLER_BUTTON_Y);
            sp.l = SDL_GameControllerGetButton(
                       controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ||
                   SDL_GameControllerGetButton(
                       controller, SDL_CONTROLLER_BUTTON_LEFTSTICK);
            sp.r = SDL_GameControllerGetButton(
                controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            sp.plus =
                SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START);
            sp.minus =
                SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK);
            sp.dup = SDL_GameControllerGetButton(controller,
                                                 SDL_CONTROLLER_BUTTON_DPAD_UP);
            sp.ddown = SDL_GameControllerGetButton(
                controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
            sp.dleft = SDL_GameControllerGetButton(
                controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            sp.dright = SDL_GameControllerGetButton(
                controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            sp.lx = SDL_GameControllerGetAxis(controller,
                                              SDL_CONTROLLER_AXIS_LEFTX) /
                    32768.0f;
            sp.ly = SDL_GameControllerGetAxis(controller,
                                              SDL_CONTROLLER_AXIS_LEFTY) /
                    32768.0f;
            switch_pad_to_codes(sp, keys);
            pads[0] = switch_pad_to_inputpad(sp);
            pad_count = 1;
        }
        write_input_snapshot(&host, snapshot, keys, pad_count ? pads : nullptr,
                             pad_count, true);
        if (args.fast) {
            audio_rem += 1000.0 / 60.0;
        } else {
            uint64_t now = wall_ms();
            audio_rem += (now - prev);
            prev = now;
        }
        uint32_t elapsed = (uint32_t)floor(audio_rem);
        audio_rem -= elapsed;
        w2c_th100x2Dgame_audio_advance(&wasm, audio, elapsed);
        uint32_t result = w2c_th100x2Dgame_application_step(&wasm, app);
        uint32_t error = w2c_th100x2Dgame_application_error(&wasm, app);
        if (error) {
            host_log("C++ game failed: %u", error);
            return 1;
        }
        steps++;
        if (args.fast) time_advance_virtual(1.0 / 60.0);
        audio_host.sync();
        if (virtual_audio) {
            mixer.render(wav_block.data(), 800);
            wav_samples.insert(wav_samples.end(), wav_block.begin(),
                               wav_block.end());
        }
        if (!args.headless) SDL_GL_SwapWindow(window);
        if (args.log_every > 0 && steps % (uint64_t)args.log_every == 0) {
            host_log("step=%llu frame=%llu draws=%llu batches=%llu peak=%.3f mixframes=%llu "
                     "stage=%d char=%d shot=%d diff=%d lives=%d power=%d "
                     "flags=0x%x", (unsigned long long)steps,
                     (unsigned long long)graphics_host.frames,
                     renderer ? renderer->draws : 0,
                     renderer ? renderer->batches : 0, mixer.peak(),
                     (unsigned long long)mixer.frames(),
                     mem_i32(&wasm, economy + 0x3c),
                     mem_i32(&wasm, economy + 0x28),
                     mem_i32(&wasm, economy + 0x2c),
                     mem_i32(&wasm, economy + 0x34),
                     mem_i32(&wasm, economy + 0x30),
                     mem_i32(&wasm, economy + 8), mem_u32(&wasm, economy + 0x60));
        }
        if (result) break;
        if (args.frames > 0 && steps >= (uint64_t)args.frames * 10 + 300) {
            host_log("watchdog: %llu steps without reaching %d frames",
                     (unsigned long long)steps, args.frames);
            break;
        }
        if (args.frames > 0 && graphics_host.frames >= (uint64_t)args.frames) {
            if (!stop_sent) {
                w2c_th100x2Dgame_application_stop(&wasm, app);
                stop_sent = true;
            }
            // Give the app a few frames to shut down gracefully.
            static int grace = 0;
            if (++grace > 30) break;
        }
        if (!args.fast) {
            double delay = w2c_th100x2Dgame_application_delay(&wasm, app);
            int ms = th10::frame_sleep_ms(delay);
            if (ms > 0) SDL_Delay((Uint32)ms);
        }
    }

    uint32_t result = 1;
    if (virtual_audio) write_wav16(args.wav.c_str(), wav_samples, 48000);
    host_log("exit frames=%llu draws=%llu audio_peak=%.3f mixer_frames=%llu",
             (unsigned long long)graphics_host.frames,
             renderer ? renderer->draws : 0, mixer.peak(),
             (unsigned long long)mixer.frames());
    (void)result;

    w2c_th100x2Dgame_application_save(&wasm, app);
    w2c_th100x2Dgame_application_destroy(&wasm, app);
    w2c_th100x2Dgame_effects_destroy(&wasm, effects);
    w2c_th100x2Dgame_audio_destroy(&wasm, audio);
    w2c_th100x2Dgame_fonts_destroy(&wasm, fonts);
    w2c_th100x2Dgame_animation_engine_destroy(&wasm, engine);
    w2c_th100x2Dgame_graphics_destroy(&wasm, device);
    w2c_th100x2Dgame_game_state_destroy(&wasm, state);
    w2c_th100x2Dgame_input_destroy(&wasm, input);
    w2c_th100x2Dgame_files_destroy(&wasm, file_system);
    graphics_host.free(params);
    graphics_host.free(name);
    graphics_host.free(rng);
    graphics_host.free(rate);
    graphics_host.free(quit);
    graphics_host.free(touch_state);
    wasm2c_th100x2Dgame_free(&wasm);

    fonts_host.shutdown();
    files_host.shutdown();
    renderer.reset();
    if (controller) SDL_GameControllerClose(controller);
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    if (glctx) SDL_GL_DeleteContext(glctx);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}