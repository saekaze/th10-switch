// C++ port of the native backend of site/runtime/d3d9.mjs
// (createDevice/deviceMethods/surface/texture/buffer/draw packets).
// The v86/COM frontend (d3dMethods) is intentionally omitted.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "draw_batch.hpp"
struct Host;

struct GfxObject;
struct DrawPacket;
struct ClearPacket;

enum class GfxKind : uint8_t {
    Unknown = 0,
    Device,
    Surface,
    Texture,
    VertexBuffer,
    IndexBuffer
};

// Renderer callbacks (implemented by gl_renderer; mirrors d3d9 on* hooks).
struct GfxRenderer {
    virtual ~GfxRenderer() = default;
    virtual void on_draw(const DrawPacket& d) = 0;
    virtual void on_clear(const ClearPacket& c) = 0;
    // GPU surface copy. Returns true when handled.
    virtual bool on_copy(GfxObject* src, const int32_t rect[4], GfxObject* dst,
                         const int32_t point[2]) = 0;
    virtual void on_present(GfxObject* device) = 0;
    virtual void on_read_surface(GfxObject* s) = 0;
    virtual void on_release(GfxObject* s) = 0;
    virtual void on_flush() = 0;
};

struct GfxObject {
    uint32_t address = 0;
    GfxKind kind_id = GfxKind::Unknown;
    std::string kind;  // IDirect3DDevice9 / IDirect3DSurface9 /
                       // IDirect3DTexture9 / IDirect3DVertexBuffer9 /
                       // IDirect3DIndexBuffer9
    int refs = 1;
    GfxObject* ref_owner = nullptr;
    // Surface state.
    uint32_t width = 0, height = 0, format = 0, pool = 0, usage = 0, size = 0,
             pitch = 0, data = 0, version = 0;
    // CPU lock → GPU dirty rect (UnlockSurface). Full-surface on first upload.
    int32_t lock_x = 0, lock_y = 0, lock_r = 0, lock_b = 0;
    bool lock_full = true;
    int32_t dirty_x0 = 0, dirty_y0 = 0, dirty_x1 = 0, dirty_y1 = 0;
    bool dirty_all = true;
    bool has_dirty = false;
    // Texture state.
    GfxObject* surface = nullptr;
    // Buffer state.
    uint32_t fvf = 0;  // buffer format / device FVF
    // Device state. Live CompactPipeline is the Draw*() snapshot — Set*
    // mutates it in place so spell-card sprites do not heap-allocate or
    // walk std::map on every SetTransform/SetRenderState.
    GfxObject *back = nullptr, *depth = nullptr;
    GfxObject *target = nullptr, *depth_target = nullptr;
    uint8_t params[56]{};
    th10::CompactPipeline pipe{};
    uint32_t textures[16]{};
    struct Stream {
        uint32_t address = 0, offset = 0, stride = 0;
        bool bound = false;
    };
    Stream streams[4]{};
    struct Indices {
        uint32_t address = 0;
        bool bound = false;
    };
    Indices indices;
};

struct DrawPacket {
    uint32_t primitive = 0, count = 0, stride = 0;
    const uint8_t* vertices = nullptr;
    uint32_t vertex_bytes = 0;
    const uint8_t* indices = nullptr;
    uint32_t index_bytes = 0;
    bool has_indices = false;
    uint32_t index_format = 0;
    th10::CompactPipeline pipe;
    // Headless path only: keep a copy after the wasm pointer is gone.
    std::vector<uint8_t> owned_vertices;
    std::vector<uint8_t> owned_indices;
};

struct ClearPacket {
    uint32_t target = 0, depth_target = 0;
    uint8_t viewport[24]{};
    uint32_t flags = 0, color = 0, stencil = 0;
    float depth = 1.0f;
    std::vector<uint32_t> rects;  // empty = whole viewport
};

uint32_t gfx_bpp(uint32_t format);

struct GraphicsHost {
    Host* host = nullptr;
    GfxRenderer* renderer = nullptr;
    // Handle 0 is unused. Sequential addresses, never reused — O(1) get().
    std::vector<std::unique_ptr<GfxObject>> objects;
    uint32_t next_handle = 1;
    uint64_t frames = 0;
    double millis = 1000.0;  // graphics clock; advanced by Present vblank
    double vblank = 0.0;
    bool has_vblank = false;
    bool yielded = false;
    std::vector<DrawPacket> draws;
    std::vector<ClearPacket> clears;
    GfxObject* device = nullptr;  // current device for deviceMethods

    uint32_t alloc(uint32_t length);
    void free(uint32_t pointer);

    GfxObject* create_object(const std::string& kind);
    GfxObject* get(uint32_t handle);
    uint32_t add_ref_value(uint32_t handle);
    uint32_t release_value(uint32_t handle);
    void forget(GfxObject* o);

    uint32_t create(uint32_t params, uint32_t flags);
    uint32_t call_device(uint32_t handle, uint32_t operation, uint32_t pointer);
    uint32_t call_resource(uint32_t handle, uint32_t operation,
                           uint32_t pointer);

    void mode(uint32_t out);
    void caps(uint32_t out);
    GfxObject* create_device(uint32_t params);
    void destroy_device(GfxObject* d);
    GfxObject* surface(uint32_t w, uint32_t h, uint32_t format, uint32_t pool,
                       uint32_t usage);
    void destroy_surface(GfxObject* s);
    GfxObject* texture(uint32_t w, uint32_t h, uint32_t levels, uint32_t usage,
                       uint32_t format, uint32_t pool);
    void destroy_texture(GfxObject* t);
    GfxObject* buffer(const std::string& kind, uint32_t size, uint32_t usage,
                      uint32_t fvf, uint32_t pool);
    void destroy_buffer(GfxObject* b);
    void destroy_object(GfxObject* o);

    uint32_t draw(uint32_t primitive, uint32_t count, uint32_t address,
                  uint32_t stride, bool has_indices, uint32_t index_data,
                  uint32_t index_format, uint32_t index_min,
                  uint32_t index_count);
    uint32_t stretch_rect(GfxObject* src, uint32_t source_rect, GfxObject* dst,
                          uint32_t dest_rect, uint32_t filter);
    uint32_t copy_rects(GfxObject* src, uint32_t rects, uint32_t count,
                        GfxObject* dst, uint32_t points);
    void lock_surface(GfxObject* s, uint32_t out, uint32_t rect);
    void describe(GfxObject* s, uint32_t out);
};
