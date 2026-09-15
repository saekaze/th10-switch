// Shared draw-batch helpers (no GL). Mirrors the upstream GLES renderer:
//   - TH10 world-space UV sprites (FVF 0x102, stride 20, triangle strip
//     of 2) are instanced (world matrix + texture factor per sprite).
//   - Other triangle/strip/fan draws with identical pipeline state are
//     coalesced into one triangle list.
// Extracted so the conversion and compatibility rules can be unit-tested
// without a GPU.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace th10 {

constexpr uint32_t kFvfXyz = 0x002;
constexpr uint32_t kFvfXyzRhw = 0x004;
constexpr uint32_t kFvfDiffuse = 0x040;
constexpr uint32_t kFvfSpecular = 0x080;
constexpr uint32_t kFvfTex1 = 0x100;
constexpr uint32_t kFvfWorldUv = kFvfXyz | kFvfTex1;  // 0x102

constexpr uint32_t kPrimPoints = 1;
constexpr uint32_t kPrimLines = 2;
constexpr uint32_t kPrimLineStrip = 3;
constexpr uint32_t kPrimTriangles = 4;
constexpr uint32_t kPrimStrip = 5;
constexpr uint32_t kPrimFan = 6;

constexpr uint32_t kD3DtsWorld = 256;
constexpr uint32_t kD3DtsView = 2;
constexpr uint32_t kD3DtsProjection = 3;
constexpr uint32_t kD3DtsTexture = 16;

inline bool transformed_fvf(uint32_t fvf) { return (fvf & 14) == 4; }

inline uint32_t vertex_count(uint32_t primitive, uint32_t count) {
    switch (primitive) {
        case kPrimPoints: return count;
        case kPrimLines: return count * 2;
        case kPrimLineStrip: return count + 1;
        case kPrimTriangles: return count * 3;
        default: return count + 2;  // strip / fan
    }
}

inline bool is_triangle_like(uint32_t primitive) {
    return primitive >= kPrimTriangles && primitive <= kPrimFan;
}

// TH10 AnmVm sprites: local unit-quad strip, colour via texture factor.
inline bool is_world_uv_sprite(uint32_t fvf, uint32_t stride,
                               uint32_t primitive, uint32_t count) {
    return fvf == kFvfWorldUv && stride == 20 && primitive == kPrimStrip &&
           count == 2;
}

// Compact snapshot of the D3D9 pipeline at Draw*() time. Device maps are
// NOT retained — they mutate on the next SetRenderState.
// Layout: bytes before `world` are the batch key; `world` + `factor` may
// vary per instance.
struct CompactPipeline {
    uint8_t viewport[24]{};
    float view[16]{};
    float proj[16]{};
    float texmat[16]{};
    uint32_t texture = 0, target = 0, depth_target = 0, fvf = 0, stride = 0;
    uint32_t fog_enabled = 0, range_fog = 0, fog_mode = 0, fog_color = 0;
    float fog_near = 0, fog_far = 1, fog_density = 1;
    uint32_t color_op = 4, color_arg1 = 2, color_arg2 = 1;
    uint32_t alpha_op = 2, alpha_arg1 = 2, alpha_arg2 = 1;
    uint32_t alpha_test = 0, alpha_ref = 0, alpha_func = 8;
    uint32_t depth_test = 0, depth_write = 1, depth_func = 4;
    uint32_t blend = 0, src_blend = 2, dst_blend = 1, blend_eq = 1;
    uint32_t cull = 1, color_mask = 15, dither = 0, texture_transform = 0;
    uint32_t wrap_s = 1, wrap_t = 1, min_filter = 2, mag_filter = 2;
    // primitive / world / factor are allowed to differ inside a batch.
    uint32_t primitive = 0;
    float world[16]{};
    uint32_t factor = 0xffffffffu;
};

inline constexpr size_t kPipelineKeySize =
    offsetof(CompactPipeline, primitive);

inline bool pipeline_key_equal(const CompactPipeline& a,
                               const CompactPipeline& b) {
    return std::memcmp(&a, &b, kPipelineKeySize) == 0;
}

inline bool pipeline_equal(const CompactPipeline& a,
                           const CompactPipeline& b) {
    return std::memcmp(&a, &b, sizeof(CompactPipeline)) == 0;
}

// Screen-space (XYZRHW) draws ignore the world matrix; world-space draws
// only batch when the whole pipeline — including world — matches, unless
// they are instanced sprites.
inline bool can_triangle_batch(const CompactPipeline& a,
                               const CompactPipeline& b) {
    if (!is_triangle_like(a.primitive) || !is_triangle_like(b.primitive))
        return false;
    if (a.stride != b.stride || a.fvf != b.fvf) return false;
    if (!pipeline_key_equal(a, b)) return false;
    if (transformed_fvf(a.fvf)) return true;
    return std::memcmp(a.world, b.world, sizeof(a.world)) == 0 &&
           a.factor == b.factor;
}

inline bool can_instance_with(const CompactPipeline& batch,
                              const CompactPipeline& next,
                              const void* batch_quad, const void* next_quad,
                              uint32_t quad_bytes) {
    if (!is_world_uv_sprite(next.fvf, next.stride, next.primitive, 2))
        return false;
    if (!pipeline_key_equal(batch, next)) return false;
    return std::memcmp(batch_quad, next_quad, quad_bytes) == 0;
}

// Expand a triangle list / strip / fan into a standalone triangle list.
// `dest` must hold count * 3 * stride bytes.
inline uint32_t expand_to_triangles(uint8_t* dest, const uint8_t* src,
                                    uint32_t stride, uint32_t primitive,
                                    uint32_t count) {
    if (!count) return 0;
    const uint32_t tri_bytes = 3 * stride;
    if (primitive == kPrimTriangles) {
        std::memcpy(dest, src, (size_t)count * tri_bytes);
        return count;
    }
    std::memcpy(dest, src, tri_bytes);
    dest += tri_bytes;
    for (uint32_t i = 1; i < count; i++) {
        uint32_t a, b, c;
        if (primitive == kPrimFan) {
            a = 0;
            b = i + 1;
            c = i + 2;
        } else {
            // Triangle strip, preserving winding.
            a = (i & 1) ? i + 1 : i;
            b = (i & 1) ? i : i + 1;
            c = i + 2;
        }
        std::memcpy(dest, src + (size_t)a * stride, stride);
        dest += stride;
        std::memcpy(dest, src + (size_t)b * stride, stride);
        dest += stride;
        std::memcpy(dest, src + (size_t)c * stride, stride);
        dest += stride;
    }
    return count;
}

// Instance record: 4x4 world matrix + D3D COLOR texture factor (BGRA bytes).
struct InstanceRec {
    float world[16];
    uint32_t factor;
};
static_assert(sizeof(InstanceRec) == 68, "instance stride must be 68");

inline void write_instance(InstanceRec* rec, const CompactPipeline& p) {
    std::memcpy(rec->world, p.world, 64);
    rec->factor = p.factor;
}

// Don't sleep a 1ms leftover: that extra tick pushes a heavy frame past
// the next vsync and is what makes lunatic/bomb stutter feel worse.
inline int frame_sleep_ms(double delay) {
    if (delay < 2.0) return 0;
    if (delay > 50.0) return 50;
    return (int)delay;
}

}  // namespace th10
