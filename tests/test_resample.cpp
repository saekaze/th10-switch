// Native triangle-resample fast path (src/host_resample.cpp) versus the
// module's own TextureResample::triangle: identical bytes and return codes on
// the same inputs, plus the dialogue-line cost (fonts_draw) both ways.
// Links the whole wasm2c module; needs a CJK font for the text timing.
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <random>

#include "audio_mixer.hpp"
#include "host_audio.hpp"
#include "host_files.hpp"
#include "host_fonts.hpp"
#include "host_graphics.hpp"
#include "wasm_host.hpp"

extern "C" int th10_native_triangle_enabled;
extern "C" unsigned th10_native_triangle_calls;
void host_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

namespace {
w2c_th100x2Dgame wasm;
int failures = 0;
bool last_native = false;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

uint32_t allocate(uint32_t n) { return w2c_th100x2Dgame_graphics_allocate(&wasm, n); }
uint32_t surface(uint32_t format, uint32_t width, uint32_t height, uint32_t bpp) {
    const uint32_t pitch = width * bpp + 8, s = allocate(20), pixels = allocate(pitch * height);
    const uint32_t v[5] = {format, width, height, pitch, pixels};
    memcpy(mem_bytes(&wasm, s, 20), v, 20);
    return s;
}
uint32_t rect(int32_t l, int32_t t, int32_t r, int32_t b) {
    const uint32_t at = allocate(16);
    const int32_t v[4] = {l, t, r, b};
    memcpy(mem_bytes(&wasm, at, 16), v, 16);
    return at;
}
std::vector<uint8_t> pixels_of(uint32_t s) {
    const uint32_t h = mem_u32(&wasm, s + 8), pitch = mem_u32(&wasm, s + 12), p = mem_u32(&wasm, s + 16);
    const uint8_t* b = mem_bytes(&wasm, p, pitch * h);
    return std::vector<uint8_t>(b, b + pitch * h);
}
void fill(uint32_t s, std::mt19937& rng, int zero_percent) {
    const uint32_t h = mem_u32(&wasm, s + 8), pitch = mem_u32(&wasm, s + 12), p = mem_u32(&wasm, s + 16);
    uint8_t* b = mem_bytes(&wasm, p, pitch * h);
    for (uint32_t i = 0; i < pitch * h; i += 2) {
        const bool zero = (int)(rng() % 100) < zero_percent;
        b[i] = zero ? 0 : (uint8_t)rng();
        b[i + 1] = zero ? 0 : (uint8_t)rng();
    }
}

struct Case {
    uint32_t in_format, in_bpp, out_format, out_bpp, sw, sh, dw, dh, wrap;
};
void compare(const Case& c, std::mt19937& rng) {
    const uint32_t in = surface(c.in_format, c.sw + 3, c.sh + 2, c.in_bpp);
    const uint32_t out_a = surface(c.out_format, c.dw + 5, c.dh + 1, c.out_bpp);
    const uint32_t out_b = surface(c.out_format, c.dw + 5, c.dh + 1, c.out_bpp);
    fill(in, rng, 60);
    fill(out_a, rng, 0);
    {  // same starting output contents
        const auto a = pixels_of(out_a);
        memcpy(mem_bytes(&wasm, mem_u32(&wasm, out_b + 16), (uint32_t)a.size()), a.data(), a.size());
    }
    const uint32_t src = rect(2, 1, 2 + c.sw, 1 + c.sh), dst = rect(3, 1, 3 + c.dw, 1 + c.dh);
    th10_native_triangle_enabled = 0;
    const uint32_t ra = w2c_th100x2Dgame_textures_resample(&wasm, out_a, dst, in, src, c.wrap);
    th10_native_triangle_enabled = 1;
    const unsigned calls = th10_native_triangle_calls;
    const uint32_t rb = w2c_th100x2Dgame_textures_resample(&wasm, out_b, dst, in, src, c.wrap);
    const bool same = ra == rb && pixels_of(out_a) == pixels_of(out_b);
    if (!same)
        printf("  mismatch: %u->%u %ux%u->%ux%u wrap %u (codes %d/%d)\n", c.in_format,
               c.out_format, c.sw, c.sh, c.dw, c.dh, c.wrap, (int)ra, (int)rb);
    CHECK(same);
    last_native = th10_native_triangle_calls != calls;
}
}  // namespace

int main() {
    static Host host;
    static w2c_th10__files fi;
    static w2c_th10__graphics gi;
    static w2c_th10__fonts fo;
    static w2c_th10__audio ai;
    static w2c_th10__time ti;
    static FilesHost files;
    static GraphicsHost graphics;
    static AudioHost audio;
    static FontsHost fonts;
    static AudioMixer mixer(44100);
    host.files = &files;
    host.graphics = &graphics;
    host.audio = &audio;
    host.fonts = &fonts;
    host.save_dir = host.data_dir = "/tmp";
    fi.host = gi.host = fo.host = ai.host = ti.host = &host;
    audio.mixer = &mixer;
    const bool have_font = fonts.init(&host, "/tmp");
    wasm_rt_init();
    wasm2c_th100x2Dgame_instantiate(&wasm, &ai, &fi, &fo, &gi, &ti);
    host.wasm = &wasm;
    graphics.host = audio.host = files.host = fonts.host = &host;

    // The game's mode: single precision, round to nearest (graphics device flags 0x40).
    w2c_th100x2Dgame_graphics_arithmetic_mode(&wasm, 32, 0);
    puts("resample equivalence");
    std::mt19937 rng(10);
    const Case cases[] = {
        {26, 2, 26, 2, 822, 40, 411, 20, 3},  // dialogue line: ARGB4444, 2x down
        {26, 2, 26, 2, 318, 36, 159, 18, 0},
        {26, 2, 26, 2, 101, 37, 33, 17, 1},   // odd ratios
        {21, 4, 21, 4, 64, 48, 32, 24, 3},    // ARGB8888
        {21, 4, 26, 2, 90, 30, 45, 15, 2},
        {22, 4, 23, 2, 50, 20, 25, 10, 3},    // XRGB -> RGB565
        {25, 2, 24, 2, 40, 40, 17, 23, 0},
        {26, 2, 26, 2, 16, 12, 37, 29, 3},    // upscale
        {20, 3, 21, 4, 30, 30, 30, 15, 3},    // one axis unchanged
    };
    for (const auto& c : cases) {
        compare(c, rng);
        CHECK(last_native);  // the fast path really ran
    }
    // Other arithmetic modes must take the module path (and still agree).
    w2c_th100x2Dgame_graphics_arithmetic_mode(&wasm, 80, 0);
    compare(cases[0], rng);
    CHECK(!last_native);
    w2c_th100x2Dgame_graphics_arithmetic_mode(&wasm, 32, 0);

    if (have_font) {
        puts("dialogue line cost (fonts_draw)");
        const uint32_t params = allocate(56), rng_state = allocate(16), tex = allocate(16),
                       line_rect = rect(0, 0, 448, 24), text = allocate(256);
        const uint32_t p14[14] = {640, 480, 22, 1, 0, 0, 1, 0, 1, 1, 80, 0, 0, 0};
        memcpy(mem_bytes(&wasm, params, 56), p14, 56);
        mem_wu32(&wasm, rng_state, 1);
        const uint32_t device = w2c_th100x2Dgame_graphics_create(&wasm, params, 0x40);
        const uint32_t f = w2c_th100x2Dgame_fonts_create(&wasm, device, rng_state, 0);
        w2c_th100x2Dgame_textures_empty(&wasm, device, tex, 512, 256, 1, 0);
        const uint32_t handle = mem_u32(&wasm, tex);
        const char* line = "\x82\xa0\x82\xe7\x81\x41\x82\xb1\x82\xf1\x82\xc8\x8f\x8a\x82\xc9\x90\x6c\x8a\xd4"
                           "\x82\xaa\x97\x88\x82\xe9\x82\xc8\x82\xf1\x82\xc4\x92\xbf\x82\xb5\x82\xa2\x82\xed\x82\xcb";
        memcpy(mem_bytes(&wasm, text, (uint32_t)strlen(line) + 1), line, strlen(line) + 1);
        auto run = [&](int native, int n) {
            th10_native_triangle_enabled = native;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < n; ++i)
                w2c_th100x2Dgame_fonts_draw(&wasm, f, line_rect, 0, 17, 0xffffffff, text, handle, 0);
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
        };
        const double slow = run(0, 20), fast = run(1, 200);
        printf("  per line: module %.2f ms, native %.3f ms\n", slow, fast);
        CHECK(fast * 5 < slow);
        th10_native_triangle_enabled = 1;
    }
    printf(failures ? "FAILED (%d)\n" : "resample tests passed\n", failures);
    return failures ? 1 : 0;
}
