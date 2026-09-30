// Native fast path for the game's D3DX triangle-filter resample
// (TextureResample::triangle, wasm function f731 in wasm2c/th10_wasm.c).
//
// Every dialogue line is drawn by GDI at twice its size and then filtered
// down with this resample. The upstream snapshot the wasm was built from
// emulates each multiply-add of the filter in 80-bit software floating point,
// which costs ~27 ms per line on a desktop CPU and far more on the Switch:
// skipping dialogue quickly stalled the game.
//
// Upstream later added this exact fast path (YomotsuHisami/th10 3cd9d80,
// th10_web/cpp/platform/TextureResample.cpp): in the game's x87 mode (single
// precision, round to nearest) every operation of the filter rounds exactly
// like an IEEE float operation, because the weights are positive and >1e-5
// and the channel values are in [0,1], so nothing can over- or underflow.
// It also caches the coefficient tables and skips fully transparent source
// pixels, which only ever add +0. This file mirrors that code on the old
// module's memory layout. Any other arithmetic mode, and the plain-copy and
// error cases, return to the original wasm function unchanged.
//
// Must be compiled with -ffp-contract=off (no fused multiply-add), like the
// upstream build; see CMakeLists.txt.
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "wasm_host.hpp"

namespace {
using u8 = uint8_t;
using u32 = uint32_t;
using i32 = int32_t;
using u64 = uint64_t;

// SoftFloat globals in the module's data (see Arithmetic.cpp): precision is
// written by arithmetic_mode(), the rounding mode lives in .bss.
constexpr u32 kPrecisionAddress = 1189273u;  // extF80_roundingPrecision
constexpr u32 kRoundingAddress = 1189769u;   // softfloat_roundingMode

struct Layout {
    u32 bytes, masks[4], shifts[4];
};
Layout layout(u32 format) {
    switch (format) {
        case 20: return {3, {255, 255, 255, 0}, {16, 8, 0, 0}};
        case 21: return {4, {255, 255, 255, 255}, {16, 8, 0, 24}};
        case 22: return {4, {255, 255, 255, 0}, {16, 8, 0, 0}};
        case 23: return {2, {31, 63, 31, 0}, {11, 5, 0, 0}};
        case 24: return {2, {31, 31, 31, 0}, {10, 5, 0, 0}};
        case 25: return {2, {31, 31, 31, 1}, {10, 5, 0, 15}};
        case 26: return {2, {15, 15, 15, 15}, {8, 4, 0, 12}};
        default: return {};
    }
}
struct Surface {
    u32 format, width, height;
    i32 pitch;
    u32 pixels;  // wasm address
};
struct Rect {
    i32 left, top, right, bottom;
};
Surface read_surface(w2c_th100x2Dgame* w, u32 at) {
    Surface s;
    s.format = mem_u32(w, at);
    s.width = mem_u32(w, at + 4);
    s.height = mem_u32(w, at + 8);
    s.pitch = mem_i32(w, at + 12);
    s.pixels = mem_u32(w, at + 16);
    return s;
}
Rect read_rect(w2c_th100x2Dgame* w, u32 at) {
    return {mem_i32(w, at), mem_i32(w, at + 4), mem_i32(w, at + 8),
            mem_i32(w, at + 12)};
}
bool valid(w2c_th100x2Dgame* w, const Surface& image, const Rect& r,
           const Layout& format) {
    if (!(format.bytes && image.pixels && image.pitch > 0 && r.left >= 0 &&
          r.top >= 0 && r.right > r.left && r.bottom > r.top &&
          (u32)r.right <= image.width && (u32)r.bottom <= image.height &&
          (u64)r.right * format.bytes <= (u32)image.pitch))
        return false;
    // The rows touched must lie inside linear memory.
    const u64 end = (u64)image.pixels + (u64)(r.bottom - 1) * (u32)image.pitch +
                    (u64)r.right * format.bytes;
    return end <= (u64)w->w2c_memory.size;
}

struct Weight {
    u32 index;
    float weight;
};
struct CachedFilter {
    u32 source = 0, destination = 0;
    bool wrap = false;
    u64 used = 0;
    std::vector<u8> bytes;
};
// Coefficient tables built by the module's own TriangleCoefficients::create,
// copied out of linear memory. Text uses a handful of sizes; 32 entries of
// dimensions <= 1024 bound this to about 2 MiB (upstream's limit).
const u8* filter(w2c_th100x2Dgame* w, u32 source, u32 destination, bool wrap,
                 std::vector<u8>& owned) {
    static std::array<CachedFilter, 32> cache;
    static u64 clock = 0;
    const bool cacheable = source <= 1024 && destination <= 1024;
    CachedFilter* oldest = &cache[0];
    if (cacheable)
        for (auto& e : cache) {
            if (!e.bytes.empty() && e.source == source &&
                e.destination == destination && e.wrap == wrap) {
                e.used = ++clock;
                return e.bytes.data();
            }
            if (e.used < oldest->used) oldest = &e;
        }
    const u32 table = w2c_th100x2Dgame_textures_filter_table(w, source, destination,
                                                            wrap ? 1 : 0);
    if (!table) return nullptr;
    // 4-byte header, then one length-prefixed block per source pixel.
    u64 size = 4;
    for (u32 i = 0; i < source; ++i) {
        const u32 length = mem_u32(w, table + (u32)size);
        if (length < 4 || (length - 4) % 8) {
            w2c_th100x2Dgame_graphics_free(w, table);
            return nullptr;
        }
        size += length;
    }
    std::vector<u8> bytes(mem_bytes(w, table, (u32)size),
                          mem_bytes(w, table, (u32)size) + size);
    w2c_th100x2Dgame_graphics_free(w, table);
    if (!cacheable) {
        owned = std::move(bytes);
        return owned.data();
    }
    oldest->bytes = std::move(bytes);
    oldest->source = source;
    oldest->destination = destination;
    oldest->wrap = wrap;
    oldest->used = ++clock;
    return oldest->bytes.data();
}
u32 word(const u8* p) {
    u32 v;
    std::memcpy(&v, p, 4);
    return v;
}
}  // namespace

// Tests compare the native path with the module's own code through this.
extern "C" {
int th10_native_triangle_enabled = 1;
unsigned th10_native_triangle_calls = 0;
}

// Returns 1 and stores the result when handled; 0 to run the wasm original.
extern "C" int th10_native_triangle(w2c_th100x2Dgame* w, u32* result, u32 output_at,
                                    u32 destination_at, u32 input_at, u32 source_at,
                                    u32 wrap_x, u32 wrap_y, u32 dither) {
    if (!th10_native_triangle_enabled) return 0;
    // Only the game's single-precision / nearest mode is bit-exact in float.
    if (mem_bytes(w, kPrecisionAddress, 1)[0] != 32 ||
        mem_bytes(w, kRoundingAddress, 1)[0] != 0)
        return 0;
    const Surface output = read_surface(w, output_at), input = read_surface(w, input_at);
    const Rect destination = read_rect(w, destination_at), source = read_rect(w, source_at);
    const Layout in = layout(input.format), out = layout(output.format);
    if (!valid(w, input, source, in) || !valid(w, output, destination, out)) return 0;
    const u32 width = destination.right - destination.left,
              height = destination.bottom - destination.top,
              source_width = source.right - source.left,
              source_height = source.bottom - source.top;
    // Plain copies and the oversize error stay in the module.
    if (width == source_width && height == source_height &&
        (!dither || input.format == output.format))
        return 0;
    if ((u64)width * height > 0x1000000u) return 0;

    std::vector<u8> owned_h, owned_v;
    const u8* horizontal = filter(w, source_width, width, (wrap_x & 1) != 0, owned_h);
    const u8* vertical = filter(w, source_height, height, (wrap_y & 1) != 0, owned_v);
    if (!horizontal || !vertical) return 0;

    std::vector<float> pixels((size_t)width * height * 4, 0.0f);
    std::vector<float> row((size_t)source_width * 4);
    float levels[4][256];
    for (u32 c = 0; c < 4; ++c) {
        const u32 mask = in.masks[c];
        if (!mask) {
            levels[c][0] = 1;
            continue;
        }
        const float unit = 1.0f / (float)mask;
        for (u32 n = 0; n <= mask; ++n) levels[c][n] = (float)n * unit;
    }
    float levels4444[16];  // Argb4444::unpack
    for (i32 i = 0; i < 16; ++i) levels4444[i] = (float)i * 0x1.111112p-4f;

    // Linear memory may have grown while the filter tables were built.
    const u8* input_pixels = mem_bytes(w, input.pixels, 0);
    const u8* y_block = vertical + 4;
    for (u32 sy = 0; sy < source_height; ++sy) {
        const u8* source_row = input_pixels + (u64)(source.top + sy) * (u32)input.pitch +
                               (u64)source.left * in.bytes;
        if (input.format == 26) {
            for (u32 i = 0; i < source_width; ++i) {
                uint16_t v;
                std::memcpy(&v, source_row + i * 2, 2);
                row[i * 4] = levels4444[(v >> 8) & 15];
                row[i * 4 + 1] = levels4444[(v >> 4) & 15];
                row[i * 4 + 2] = levels4444[v & 15];
                row[i * 4 + 3] = levels4444[v >> 12];
            }
        } else {
            for (u32 sx = 0; sx < source_width; ++sx) {
                u32 packed = 0;
                std::memcpy(&packed, source_row + sx * in.bytes, in.bytes);
                for (u32 c = 0; c < 4; ++c)
                    row[sx * 4 + c] = levels[c][(packed >> in.shifts[c]) & in.masks[c]];
            }
        }
        const u8* y_end = y_block + word(y_block);
        const u8* x_block = horizontal + 4;
        for (u32 sx = 0; sx < source_width; ++sx) {
            const u8* x_end = x_block + word(x_block);
            const float* s = &row[sx * 4];
            if (width == source_width && height == source_height) {
                std::memcpy(&pixels[((size_t)sy * width + sx) * 4], s, 16);
                x_block = x_end;
                continue;
            }
            // Fully transparent padding adds only +0 to nonnegative sums.
            if (s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0) {
                x_block = x_end;
                continue;
            }
            for (const u8* yp = y_block + 4; yp < y_end; yp += 8) {
                Weight y;
                std::memcpy(&y, yp, 8);
                for (const u8* xp = x_block + 4; xp < x_end; xp += 8) {
                    Weight x;
                    std::memcpy(&x, xp, 8);
                    float* pixel = &pixels[((size_t)y.index * width + x.index) * 4];
                    const float weight = x.weight * y.weight;
                    for (u32 c = 0; c < 4; ++c) {
                        const float scaled = weight * s[c];
                        pixel[c] = scaled + pixel[c];
                    }
                }
            }
            x_block = x_end;
        }
        y_block = y_end;
    }

    u8* output_pixels = mem_bytes(w, output.pixels, 0);
    static constexpr u8 thresholds[4][4] = {
        {31, 15, 27, 11}, {7, 23, 3, 19}, {25, 9, 29, 13}, {1, 17, 5, 21}};
    for (u32 y = 0; y < height; ++y) {
        float* values = &pixels[(size_t)y * width * 4];
        for (u32 i = 0; i < width * 4; ++i)
            values[i] = values[i] < 0 ? 0 : values[i] < 1 ? values[i] : 1;
        u8* destination_row = output_pixels +
                              (u64)(destination.top + y) * (u32)output.pitch +
                              (u64)destination.left * out.bytes;
        if (output.format == 26 && !dither) {  // Argb4444::pack
            const auto quantize = [](float value) {
                const i32 n = (i32)((double)value * 15 + 0.5);
                return n < 0 ? 0 : n > 15 ? 15 : n;
            };
            for (u32 x = 0; x < width; ++x) {
                const float* p = values + x * 4;
                const uint16_t n = (uint16_t)((quantize(p[3]) << 12) | (quantize(p[0]) << 8) |
                                              (quantize(p[1]) << 4) | quantize(p[2]));
                std::memcpy(destination_row + x * 2, &n, 2);
            }
        } else {
            for (u32 x = 0; x < width; ++x) {
                // D3DX ordered thresholds, indexed within the destination rect.
                const u32 scan_x = (y & 1) ? width - 1 - x : x;
                const double bias = dither ? thresholds[y & 3][scan_x & 3] / 32.0 : .5;
                u32 packed = 0;
                for (u32 c = 0; c < 4; ++c) {
                    const u32 max = out.masks[c];
                    const i32 quantized = (i32)((double)values[x * 4 + c] * max + bias);
                    packed |= (u32)(quantized < 0 ? 0 : quantized > (i32)max ? max : quantized)
                              << out.shifts[c];
                }
                std::memcpy(destination_row + x * out.bytes, &packed, out.bytes);
            }
        }
    }
    ++th10_native_triangle_calls;
    *result = 0;
    return 1;
}
