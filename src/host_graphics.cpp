#include "host_graphics.hpp"

#include <cmath>
#include <cstring>

#include "wasm_host.hpp"

uint32_t gfx_bpp(uint32_t format) {
    switch (format) {
        case 20: return 3;
        case 21:
        case 22: return 4;
        case 23:
        case 24:
        case 25:
        case 26: return 2;
        case 28:
        case 50: return 1;
        case 51: return 2;
        default: return 4;
    }
}

namespace {
const int kDeviceArgs[34] = {3, 3, 2, 2, 1, 4, 2, 4, 3, 1, 6, 0,
                             0, 4, 4, 2, 2, 1, 1, 8, 6, 8, 6, 6,
                             1, 6, 8, 5, 4, 1, 1, 0, 2, 1};
const int kResourceArgs[10] = {0, 0, 1, 2, 3, 0, 4, 0, 1, 0};
const uint32_t kInvalidCall = 0x8876086cu;
const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

GfxKind kind_from_name(const std::string& kind) {
    if (kind == "IDirect3DDevice9") return GfxKind::Device;
    if (kind == "IDirect3DSurface9") return GfxKind::Surface;
    if (kind == "IDirect3DTexture9") return GfxKind::Texture;
    if (kind == "IDirect3DVertexBuffer9") return GfxKind::VertexBuffer;
    if (kind == "IDirect3DIndexBuffer9") return GfxKind::IndexBuffer;
    return GfxKind::Unknown;
}

void init_device_pipe(GfxObject* d) {
    d->pipe = th10::CompactPipeline{};
    memcpy(d->pipe.view, kIdentity, 64);
    memcpy(d->pipe.proj, kIdentity, 64);
    memcpy(d->pipe.texmat, kIdentity, 64);
    memcpy(d->pipe.world, kIdentity, 64);
    // Match snapshot_pipeline() defaults (map miss → these values).
    d->pipe.fog_far = 1.0f;
    d->pipe.fog_density = 1.0f;
    d->pipe.factor = 0xffffffffu;
    d->pipe.color_op = 4;
    d->pipe.color_arg1 = 2;
    d->pipe.color_arg2 = 1;
    d->pipe.alpha_op = 2;
    d->pipe.alpha_arg1 = 2;
    d->pipe.alpha_arg2 = 1;
    d->pipe.alpha_func = 8;
    d->pipe.depth_write = 1;
    d->pipe.depth_func = 4;
    d->pipe.src_blend = 2;
    d->pipe.dst_blend = 1;
    d->pipe.blend_eq = 1;
    d->pipe.cull = 1;
    d->pipe.color_mask = 15;
    d->pipe.wrap_s = 1;
    d->pipe.wrap_t = 1;
    d->pipe.min_filter = 0;
    d->pipe.mag_filter = 0;
}

void set_render_state(GfxObject* d, uint32_t k, uint32_t v) {
    switch (k) {
        case 28: d->pipe.fog_enabled = v; break;
        case 48: d->pipe.range_fog = v; break;
        case 140: d->pipe.fog_mode = v; break;
        case 34: d->pipe.fog_color = v; break;
        case 36: memcpy(&d->pipe.fog_near, &v, 4); break;
        case 37: memcpy(&d->pipe.fog_far, &v, 4); break;
        case 38: memcpy(&d->pipe.fog_density, &v, 4); break;
        case 60: d->pipe.factor = v; break;
        case 15: d->pipe.alpha_test = v; break;
        case 24: d->pipe.alpha_ref = v; break;
        case 25: d->pipe.alpha_func = v; break;
        case 7: d->pipe.depth_test = v; break;
        case 14: d->pipe.depth_write = v; break;
        case 23: d->pipe.depth_func = v; break;
        case 27: d->pipe.blend = v; break;
        case 19: d->pipe.src_blend = v; break;
        case 20: d->pipe.dst_blend = v; break;
        case 171: d->pipe.blend_eq = v; break;
        case 22: d->pipe.cull = v; break;
        case 168: d->pipe.color_mask = v; break;
        case 26: d->pipe.dither = v; break;
        default: break;
    }
}

void set_stage0(GfxObject* d, uint32_t op, uint32_t v) {
    switch (op) {
        case 1: d->pipe.color_op = v; break;
        case 2: d->pipe.color_arg1 = v; break;
        case 3: d->pipe.color_arg2 = v; break;
        case 4: d->pipe.alpha_op = v; break;
        case 5: d->pipe.alpha_arg1 = v; break;
        case 6: d->pipe.alpha_arg2 = v; break;
        case 13: d->pipe.wrap_s = v; break;
        case 14: d->pipe.wrap_t = v; break;
        case 16: d->pipe.mag_filter = v; break;
        case 17: d->pipe.min_filter = v; break;
        case 24: d->pipe.texture_transform = v; break;
        default: break;
    }
}

void set_viewport_u32(GfxObject* d, uint32_t x, uint32_t y, uint32_t w,
                      uint32_t h, uint32_t zn, uint32_t zf) {
    uint32_t vp[6] = {x, y, w, h, zn, zf};
    memcpy(d->pipe.viewport, vp, 24);
}

void mark_unlock_dirty(GfxObject* s) {
    s->version++;
    if (s->lock_full) {
        s->dirty_all = true;
        return;
    }
    int32_t x = s->lock_x, y = s->lock_y, r = s->lock_r, b = s->lock_b;
    if (r <= x || b <= y) {
        s->dirty_all = true;
        return;
    }
    if (s->dirty_all) return;
    if (!s->has_dirty) {
        s->dirty_x0 = x;
        s->dirty_y0 = y;
        s->dirty_x1 = r;
        s->dirty_y1 = b;
        s->has_dirty = true;
        return;
    }
    if (x < s->dirty_x0) s->dirty_x0 = x;
    if (y < s->dirty_y0) s->dirty_y0 = y;
    if (r > s->dirty_x1) s->dirty_x1 = r;
    if (b > s->dirty_y1) s->dirty_y1 = b;
}
}  // namespace

uint32_t GraphicsHost::alloc(uint32_t length) {
    uint32_t p = w2c_th100x2Dgame_graphics_allocate(host->wasm, length);
    HOST_ASSERT(p != 0, "graphics_allocate(%u) failed", length);
    return p;
}
void GraphicsHost::free(uint32_t pointer) {
    w2c_th100x2Dgame_graphics_free(host->wasm, pointer);
}

GfxObject* GraphicsHost::create_object(const std::string& kind) {
    auto o = std::unique_ptr<GfxObject>(new GfxObject());
    uint32_t h = next_handle++;
    o->address = h;
    o->kind = kind;
    o->kind_id = kind_from_name(kind);
    if (objects.size() <= h) objects.resize(h + 1);
    GfxObject* p = o.get();
    objects[h] = std::move(o);
    return p;
}
GfxObject* GraphicsHost::get(uint32_t handle) {
    HOST_ASSERT(handle < objects.size() && objects[handle],
                "unknown graphics handle %u", handle);
    return objects[handle].get();
}
uint32_t GraphicsHost::add_ref_value(uint32_t handle) {
    GfxObject* o = get(handle);
    GfxObject* t = o->ref_owner ? o->ref_owner : o;
    return (uint32_t)++t->refs;
}
void GraphicsHost::forget(GfxObject* o) { objects[o->address].reset(); }
uint32_t GraphicsHost::release_value(uint32_t handle) {
    GfxObject* o = get(handle);
    if (o->ref_owner) return release_value(o->ref_owner->address);
    HOST_ASSERT(o->refs > 0, "graphics object already released");
    int refs = --o->refs;
    if (!refs) {
        destroy_object(o);
        forget(o);
    }
    return (uint32_t)refs;
}
void GraphicsHost::destroy_object(GfxObject* o) {
    switch (o->kind_id) {
        case GfxKind::Device: destroy_device(o); break;
        case GfxKind::Surface: destroy_surface(o); break;
        case GfxKind::Texture: destroy_texture(o); break;
        case GfxKind::VertexBuffer:
        case GfxKind::IndexBuffer: destroy_buffer(o); break;
        default:
            HOST_ASSERT(false, "destroy unknown kind %s", o->kind.c_str());
    }
}

uint32_t GraphicsHost::create(uint32_t params, uint32_t flags) {
    w2c_th100x2Dgame_graphics_configure_arithmetic(host->wasm, flags);
    return create_device(params)->address;
}

void GraphicsHost::mode(uint32_t out) {
    w2c_th100x2Dgame* w = host->wasm;
    mem_wu32(w, out, 640);
    mem_wu32(w, out + 4, 480);
    mem_wu32(w, out + 8, 60);
    mem_wu32(w, out + 12, 22);
}

void GraphicsHost::caps(uint32_t out) {
    w2c_th100x2Dgame* w = host->wasm;
    uint32_t v[76];
    for (int i = 0; i < 76; i++) v[i] = 0;
    for (int i = 2; i < 22; i++) v[i] = 0xffffffffu;
    v[0] = 1;
    v[22] = 4096;
    v[23] = 4096;
    v[24] = 256;
    v[25] = 8192;
    v[26] = 4096;
    v[27] = 16;
    v[15] = 0x00005;
    v[35] = 0x10008;
    v[36] = 0x3ffffff;
    v[37] = 8;
    v[38] = 8;
    v[39] = 0x7f;
    v[40] = 8;
    v[41] = 6;
    v[42] = 4;
    v[43] = 255;
    v[45] = 0xfffff;
    v[46] = 0xfffff;
    v[47] = 16;
    v[48] = 255;
    v[49] = 0xfffe0101;
    v[50] = 96;
    v[51] = 0xffff0104;
    memcpy(mem_bytes(w, out, 76 * 4), v, sizeof(v));
    mem_wf32(w, out + 28 * 4, 1e10f);
    mem_wf32(w, out + 44 * 4, 64.0f);
    mem_wf32(w, out + 52 * 4, 8.0f);
}

GfxObject* GraphicsHost::create_device(uint32_t params) {
    w2c_th100x2Dgame* w = host->wasm;
    uint32_t bw = mem_u32(w, params);
    if (!bw) bw = 640;
    uint32_t bh = mem_u32(w, params + 4);
    if (!bh) bh = 480;
    uint32_t bf = mem_u32(w, params + 8);
    if (!bf) bf = 22;
    GfxObject* back = surface(bw, bh, bf, 0, 0);
    GfxObject* depth = surface(back->width, back->height, 80, 0, 0);
    GfxObject* d = create_object("IDirect3DDevice9");
    d->back = back;
    d->depth = depth;
    d->target = back;
    d->depth_target = depth;
    memcpy(d->params, mem_bytes(w, params, 56), 56);
    init_device_pipe(d);
    d->pipe.target = back->address;
    d->pipe.depth_target = depth->address;
    set_viewport_u32(d, 0, 0, back->width, back->height, 0, 0x3f800000);
    add_ref_value(back->address);
    add_ref_value(depth->address);
    device = d;
    return d;
}

void GraphicsHost::destroy_device(GfxObject* d) {
    if (renderer) renderer->on_flush();
    for (int i = 0; i < 16; i++)
        if (d->textures[i]) release_value(d->textures[i]);
    for (int i = 0; i < 4; i++)
        if (d->streams[i].bound && d->streams[i].address)
            release_value(d->streams[i].address);
    if (d->indices.bound && d->indices.address)
        release_value(d->indices.address);
    release_value(d->target->address);
    if (d->depth_target) release_value(d->depth_target->address);
    release_value(d->back->address);
    release_value(d->depth->address);
}

GfxObject* GraphicsHost::surface(uint32_t w, uint32_t h, uint32_t format,
                                 uint32_t pool, uint32_t usage) {
    uint32_t size = w * h * gfx_bpp(format);
    GfxObject* s = create_object("IDirect3DSurface9");
    s->width = w;
    s->height = h;
    s->format = format;
    s->pool = pool;
    s->usage = usage;
    s->size = size;
    s->pitch = w * gfx_bpp(format);
    s->data = alloc(size);
    s->version = 0;
    s->dirty_all = true;
    s->has_dirty = false;
    memset(mem_bytes(host->wasm, s->data, size), 0, size);
    return s;
}

void GraphicsHost::destroy_surface(GfxObject* s) {
    if (renderer) renderer->on_release(s);
    free(s->data);
}

GfxObject* GraphicsHost::texture(uint32_t w, uint32_t h, uint32_t levels,
                                 uint32_t usage, uint32_t format,
                                 uint32_t pool) {
    (void)levels;
    GfxObject* s = surface(w, h, format, pool, usage);
    GfxObject* t = create_object("IDirect3DTexture9");
    t->surface = s;
    t->width = w;
    t->height = h;
    t->format = format;
    s->ref_owner = t;
    return t;
}

void GraphicsHost::destroy_texture(GfxObject* t) {
    destroy_surface(t->surface);
    forget(t->surface);
}

GfxObject* GraphicsHost::buffer(const std::string& kind, uint32_t size,
                                uint32_t usage, uint32_t fvf, uint32_t pool) {
    GfxObject* b = create_object(kind);
    b->size = size;
    b->usage = usage;
    b->fvf = fvf;
    b->pool = pool;
    b->data = alloc(size);
    return b;
}

void GraphicsHost::destroy_buffer(GfxObject* b) { free(b->data); }

void GraphicsHost::describe(GfxObject* s, uint32_t out) {
    w2c_th100x2Dgame* w = host->wasm;
    uint32_t v[8] = {s->format, 1, s->usage, s->pool, 0, 0, s->width, s->height};
    for (int i = 0; i < 8; i++) mem_wu32(w, out + i * 4, v[i]);
}

void GraphicsHost::lock_surface(GfxObject* s, uint32_t out, uint32_t rect) {
    if (renderer) renderer->on_read_surface(s);
    w2c_th100x2Dgame* w = host->wasm;
    uint32_t x = 0, y = 0;
    if (rect) {
        int32_t left = mem_i32(w, rect);
        int32_t top = mem_i32(w, rect + 4);
        int32_t right = mem_i32(w, rect + 8);
        int32_t bottom = mem_i32(w, rect + 12);
        s->lock_x = left;
        s->lock_y = top;
        s->lock_r = right;
        s->lock_b = bottom;
        s->lock_full = !(right > left && bottom > top);
        x = (uint32_t)left;
        y = (uint32_t)top;
    } else {
        s->lock_full = true;
    }
    mem_wu32(w, out, s->pitch);
    mem_wu32(w, out + 4, s->data + y * s->pitch + x * gfx_bpp(s->format));
}

uint32_t GraphicsHost::stretch_rect(GfxObject* src, uint32_t source_rect,
                                    GfxObject* dst, uint32_t dest_rect,
                                    uint32_t filter) {
    if (src == dst || src->format != dst->format ||
        (filter != 0 && filter != 1 && filter != 2))
        return kInvalidCall;
    w2c_th100x2Dgame* w = host->wasm;
    int32_t from[4], to[4];
    if (source_rect)
        for (int i = 0; i < 4; i++) from[i] = mem_i32(w, source_rect + i * 4);
    else {
        from[0] = 0;
        from[1] = 0;
        from[2] = (int32_t)src->width;
        from[3] = (int32_t)src->height;
    }
    if (dest_rect)
        for (int i = 0; i < 4; i++) to[i] = mem_i32(w, dest_rect + i * 4);
    else {
        to[0] = 0;
        to[1] = 0;
        to[2] = (int32_t)dst->width;
        to[3] = (int32_t)dst->height;
    }
    auto valid = [](int32_t* r, GfxObject* s) {
        return r[0] >= 0 && r[1] >= 0 && r[2] > r[0] && r[3] > r[1] &&
               r[2] <= (int32_t)s->width && r[3] <= (int32_t)s->height;
    };
    if (!valid(from, src) || !valid(to, dst) || from[2] - from[0] != to[2] - to[0] ||
        from[3] - from[1] != to[3] - to[1])
        return kInvalidCall;
    int32_t point[2] = {to[0], to[1]};
    bool handled = renderer && renderer->on_copy(src, from, dst, point);
    if (!handled) {
        uint32_t pb = gfx_bpp(src->format);
        uint32_t row = (from[2] - from[0]) * pb;
        for (int32_t y = 0; y < from[3] - from[1]; y++)
            memcpy(mem_bytes(w, dst->data + (to[1] + y) * dst->pitch + to[0] * pb,
                             row),
                   mem_bytes(w, src->data + (from[1] + y) * src->pitch +
                                        from[0] * pb,
                             row),
                   row);
        dst->dirty_all = true;
    }
    dst->version++;
    return 0;
}

uint32_t GraphicsHost::copy_rects(GfxObject* src, uint32_t rects,
                                  uint32_t count, GfxObject* dst,
                                  uint32_t points) {
    if (src->format != dst->format || src == dst) return kInvalidCall;
    w2c_th100x2Dgame* w = host->wasm;
    struct Copy {
        int32_t x, y, r, b, px, py;
    };
    std::vector<Copy> copies;
    uint32_t n = rects ? count : 1;
    for (uint32_t i = 0; i < n; i++) {
        Copy c;
        if (rects) {
            c.x = mem_i32(w, rects + i * 16);
            c.y = mem_i32(w, rects + i * 16 + 4);
            c.r = mem_i32(w, rects + i * 16 + 8);
            c.b = mem_i32(w, rects + i * 16 + 12);
        } else {
            c.x = 0;
            c.y = 0;
            c.r = (int32_t)src->width;
            c.b = (int32_t)src->height;
        }
        if (points) {
            c.px = mem_i32(w, points + i * 8);
            c.py = mem_i32(w, points + i * 8 + 4);
        } else {
            c.px = c.x;
            c.py = c.y;
        }
        if (c.x < 0 || c.y < 0 || c.r <= c.x || c.b <= c.y ||
            c.r > (int32_t)src->width || c.b > (int32_t)src->height ||
            c.px < 0 || c.py < 0 || c.px + c.r - c.x > (int32_t)dst->width ||
            c.py + c.b - c.y > (int32_t)dst->height)
            return kInvalidCall;
        copies.push_back(c);
    }
    for (const Copy& c : copies) {
        int32_t rect[4] = {c.x, c.y, c.r, c.b};
        int32_t point[2] = {c.px, c.py};
        if (renderer && renderer->on_copy(src, rect, dst, point)) continue;
        uint32_t pb = gfx_bpp(src->format);
        for (int32_t row = 0; row < c.b - c.y; row++)
            memcpy(mem_bytes(w, dst->data + (c.py + row) * dst->pitch +
                                        c.px * pb,
                             (c.r - c.x) * pb),
                   mem_bytes(w, src->data + (c.y + row) * src->pitch + c.x * pb,
                             (c.r - c.x) * pb),
                   (c.r - c.x) * pb);
        dst->dirty_all = true;
    }
    dst->version++;
    return 0;
}

uint32_t GraphicsHost::draw(uint32_t primitive, uint32_t count,
                            uint32_t address, uint32_t stride, bool has_indices,
                            uint32_t index_data, uint32_t index_format,
                            uint32_t index_min, uint32_t index_count) {
    w2c_th100x2Dgame* w = host->wasm;
    uint32_t elements = primitive == 1   ? count
                        : primitive == 2 ? count * 2
                        : primitive == 3 ? count + 1
                        : primitive == 4 ? count * 3
                                         : count + 2;
    uint32_t vertices = has_indices ? index_count : elements;
    DrawPacket d;
    d.primitive = primitive;
    d.count = count;
    d.stride = stride;
    d.has_indices = has_indices;
    d.index_format = index_format;
    if (has_indices) {
        uint32_t ib = elements * (index_format == 101 ? 2 : 4);
        const uint8_t* src = mem_bytes(w, index_data, ib);
        d.owned_indices.assign(src, src + ib);
        d.indices = d.owned_indices.data();
        d.index_bytes = ib;
        if (index_format == 101) {
            uint16_t* v = (uint16_t*)d.owned_indices.data();
            for (uint32_t i = 0; i < elements; i++) {
                v[i] = (uint16_t)(v[i] - index_min);
#ifndef NDEBUG
                HOST_ASSERT(v[i] < vertices,
                            "index outside declared vertex range");
#endif
            }
        } else {
            uint32_t* v = (uint32_t*)d.owned_indices.data();
            for (uint32_t i = 0; i < elements; i++) {
                v[i] = v[i] - index_min;
#ifndef NDEBUG
                HOST_ASSERT(v[i] < vertices,
                            "index outside declared vertex range");
#endif
            }
        }
    }
    uint32_t base = address + (has_indices ? index_min : 0) * stride;
    uint32_t vb = vertices * stride;
    d.vertices = mem_bytes(w, base, vb);
    d.vertex_bytes = vb;
    d.pipe = device->pipe;
    d.pipe.stride = stride;
    d.pipe.primitive = primitive;
    d.pipe.fvf = device->fvf;
    d.pipe.target = device->target ? device->target->address : 0;
    d.pipe.depth_target =
        device->depth_target ? device->depth_target->address : 0;
    if (renderer) {
        renderer->on_draw(d);
    } else {
        d.owned_vertices.assign(d.vertices, d.vertices + d.vertex_bytes);
        d.vertices = d.owned_vertices.data();
        draws.push_back(std::move(d));
    }
    return 0;
}

uint32_t GraphicsHost::call_device(uint32_t handle, uint32_t operation,
                                   uint32_t pointer) {
    HOST_ASSERT(operation < 34, "unknown graphics device operation %u",
                operation);
    w2c_th100x2Dgame* w = host->wasm;
    uint32_t a[8] = {0};
    for (int i = 0; i < kDeviceArgs[operation]; i++)
        a[i] = mem_u32(w, pointer + i * 4);
    GfxObject* d = get(handle);
    HOST_ASSERT(d->kind_id == GfxKind::Device, "expected graphics device");
    device = d;
    switch (operation) {
        case 0:  // TextureStage
            if (a[0] == 0) set_stage0(d, a[1], a[2]);
            return 0;
        case 1: {  // Sampler
            uint32_t key = 0;
            switch (a[1]) {
                case 1: key = 13; break;
                case 2: key = 14; break;
                case 3: key = 25; break;
                case 4: key = 15; break;
                case 5: key = 16; break;
                case 6: key = 17; break;
                case 7: key = 18; break;
                case 8: key = 19; break;
                case 9: key = 20; break;
                case 10: key = 21; break;
            }
            if (key && a[0] == 0) set_stage0(d, key, a[2]);
            return 0;
        }
        case 2:  // RenderState
            set_render_state(d, a[0], a[1]);
            return 0;
        case 3: {  // Texture
            uint32_t stage = a[0];
            if (stage >= 16) return 0;
            uint32_t old = d->textures[stage];
            if (old == a[1]) return 0;
            if (a[1]) add_ref_value(a[1]);
            d->textures[stage] = a[1];
            if (stage == 0) d->pipe.texture = a[1];
            if (old) release_value(old);
            return 0;
        }
        case 4:  // VertexFormat
            d->fvf = a[0];
            d->pipe.fvf = a[0];
            return 0;
        case 5:  // Triangles (DrawPrimitiveUP)
            return draw(a[0], a[1], a[2], a[3], false, 0, 0, 0, 0);
        case 6: {  // Transform — memcpy 64B, no heap
            uint8_t* p = mem_bytes(w, a[1], 64);
            float* dst = nullptr;
            switch (a[0]) {
                case th10::kD3DtsWorld: dst = d->pipe.world; break;
                case th10::kD3DtsView: dst = d->pipe.view; break;
                case th10::kD3DtsProjection: dst = d->pipe.proj; break;
                case th10::kD3DtsTexture: dst = d->pipe.texmat; break;
                default: break;
            }
            if (dst) memcpy(dst, p, 64);
            return 0;
        }
        case 7: {  // Stream
            uint32_t slot = a[0];
            if (slot >= 4) return 0;
            uint32_t old = d->streams[slot].bound ? d->streams[slot].address : 0;
            if (a[1]) add_ref_value(a[1]);
            d->streams[slot].address = a[1];
            d->streams[slot].offset = a[2];
            d->streams[slot].stride = a[3];
            d->streams[slot].bound = true;
            if (old) release_value(old);
            return 0;
        }
        case 8: {  // DrawBuffer
            HOST_ASSERT(d->streams[0].bound, "DrawPrimitive without stream 0");
            GfxObject* b = get(d->streams[0].address);
            return draw(a[0], a[2],
                        b->data + d->streams[0].offset +
                            a[1] * d->streams[0].stride,
                        d->streams[0].stride, false, 0, 0, 0, 0);
        }
        case 9: {  // Viewport
            uint8_t* p = mem_bytes(w, a[0], 24);
            memcpy(d->pipe.viewport, p, 24);
            return 0;
        }
        case 10: {  // Clear
            ClearPacket c;
            c.target = d->target->address;
            c.depth_target = d->depth_target ? d->depth_target->address : 0;
            memcpy(c.viewport, d->pipe.viewport, 24);
            c.flags = a[2];
            c.color = a[3];
            memcpy(&c.depth, &a[4], 4);
            c.stencil = a[5];
            if (a[0])
                for (uint32_t i = 0; i < a[0] * 4; i++)
                    c.rects.push_back(mem_u32(w, a[1] + i * 4));
            if (renderer)
                renderer->on_clear(c);
            else
                clears.push_back(std::move(c));
            return 0;
        }
        case 11:  // Begin
        case 12:  // End
            return 0;
        case 13: {  // Present
            frames++;
            const double interval = 1000.0 / 60.0;
            double prev = has_vblank ? vblank : millis;
            double steps = ceil((millis - prev - 1e-7) / interval);
            if (steps < 1) steps = 1;
            vblank = prev + steps * interval;
            has_vblank = true;
            millis = vblank;
            yielded = true;
            if (renderer) renderer->on_present(d);
            draws.clear();
            clears.clear();
            return 0;
        }
        case 14:  // BackBuffer
            add_ref_value(d->back->address);
            mem_wu32(w, a[3], d->back->address);
            return 0;
        case 15:  // GetTarget
            add_ref_value(d->target->address);
            mem_wu32(w, a[1], d->target->address);
            return 0;
        case 16: {  // SetTarget
            if (a[0] != 0) return kInvalidCall;
            GfxObject* target = get(a[1]);
            add_ref_value(target->address);
            if (renderer) renderer->on_flush();
            release_value(d->target->address);
            d->target = target;
            d->pipe.target = target->address;
            set_viewport_u32(d, 0, 0, target->width, target->height, 0,
                             0x3f800000);
            return 0;
        }
        case 17: {  // GetDepth
            GfxObject* s = d->depth_target;
            if (s) add_ref_value(s->address);
            mem_wu32(w, a[0], s ? s->address : 0);
            return s ? 0 : 0x88760866u;
        }
        case 18: {  // SetDepth
            GfxObject* depth = a[0] ? get(a[0]) : nullptr;
            if (depth) add_ref_value(depth->address);
            if (renderer) renderer->on_flush();
            if (d->depth_target) release_value(d->depth_target->address);
            d->depth_target = depth;
            d->pipe.depth_target = depth ? depth->address : 0;
            return 0;
        }
        case 19:  // CreateTexture
            mem_wu32(w, a[6],
                     texture(a[0], a[1], a[2], a[3], a[4], a[5])->address);
            return 0;
        case 20:  // CreateSurface
            mem_wu32(w, a[4], surface(a[0], a[1], a[2], a[3], 0)->address);
            return 0;
        case 21:  // CreateTarget
            mem_wu32(w, a[6], surface(a[0], a[1], a[2], 0, 1)->address);
            return 0;
        case 22:  // CreateVertices
            mem_wu32(w, a[4],
                     buffer("IDirect3DVertexBuffer9", a[0], a[1], a[2], a[3])
                         ->address);
            return 0;
        case 23:  // CreateIndices
            mem_wu32(w, a[4],
                     buffer("IDirect3DIndexBuffer9", a[0], a[1], a[2], a[3])
                         ->address);
            return 0;
        case 24: {  // Indices
            uint32_t old = d->indices.bound ? d->indices.address : 0;
            if (a[0]) add_ref_value(a[0]);
            d->indices.address = a[0];
            d->indices.bound = true;
            if (old) release_value(old);
            return 0;
        }
        case 25: {  // DrawIndexed
            HOST_ASSERT(d->streams[0].bound, "DrawIndexed without stream 0");
            HOST_ASSERT(d->indices.bound, "DrawIndexed without indices");
            GfxObject* b = get(d->streams[0].address);
            GfxObject* idx = get(d->indices.address);
            uint32_t ib = idx->fvf == 101 ? 2 : 4;
            return draw(a[0], a[5],
                        b->data + d->streams[0].offset +
                            (uint32_t)(int32_t)a[1] * d->streams[0].stride,
                        d->streams[0].stride, true, idx->data + a[4] * ib,
                        idx->fvf, a[2], a[3]);
        }
        case 26:  // DrawIndexedUp
            return draw(a[0], a[3], a[6], a[7], true, a[4], a[5], a[1], a[2]);
        case 27:  // Stretch
            return stretch_rect(get(a[0]), a[1], get(a[2]), a[3], a[4]);
        case 28:  // UpdateSurface
            return copy_rects(get(a[0]), a[1], 1, get(a[2]), a[3]);
        case 29: {  // Reset
            uint8_t* p = mem_bytes(w, a[0], 56);
            memcpy(d->params, p, 56);
            return 0;
        }
        case 30:  // Caps
            caps(a[0]);
            return 0;
        case 31:  // Cooperative
            return 0;
        case 32:  // DisplayMode
            mode(a[1]);
            return 0;
        case 33:  // PixelShader
            return 0;
    }
    return 0;
}

uint32_t GraphicsHost::call_resource(uint32_t handle, uint32_t operation,
                                     uint32_t pointer) {
    HOST_ASSERT(operation < 10, "unknown graphics resource operation %u",
                operation);
    if (operation == 0) return add_ref_value(handle);
    if (operation == 1) return release_value(handle);
    w2c_th100x2Dgame* w = host->wasm;
    uint32_t a[4] = {0};
    for (int i = 0; i < kResourceArgs[operation]; i++)
        a[i] = mem_u32(w, pointer + i * 4);
    GfxObject* o = get(handle);
    bool is_surface = o->kind_id == GfxKind::Surface;
    bool is_texture = o->kind_id == GfxKind::Texture;
    bool is_buffer = o->kind_id == GfxKind::VertexBuffer ||
                     o->kind_id == GfxKind::IndexBuffer;
    switch (operation) {
        case 2:  // Description
            if (is_surface) {
                describe(o, a[0]);
                return 0;
            }
            if (is_buffer) {
                uint32_t v[6] = {100, 6, o->usage, o->pool, o->size, o->fvf};
                for (int i = 0; i < 6; i++) mem_wu32(w, a[0] + i * 4, v[i]);
                return 0;
            }
            break;
        case 3:  // Surface (GetSurfaceLevel)
            if (is_texture) {
                add_ref_value(o->surface->address);
                mem_wu32(w, a[1], o->surface->address);
                return 0;
            }
            break;
        case 4:  // LockSurface
            if (is_surface) {
                lock_surface(o, a[0], a[1]);
                return 0;
            }
            if (is_texture) {
                lock_surface(o->surface, a[1], a[2]);
                return 0;
            }
            break;
        case 5:  // UnlockSurface
            if (is_surface) {
                mark_unlock_dirty(o);
                return 0;
            }
            if (is_texture) {
                mark_unlock_dirty(o->surface);
                return 0;
            }
            break;
        case 6:  // LockBuffer
            if (is_buffer) {
                mem_wu32(w, a[2], o->data + a[0]);
                return 0;
            }
            break;
        case 7:  // UnlockBuffer
            if (is_buffer) return 0;
            break;
        case 8:  // Priority
        case 9:  // Preload
            if (is_texture) return 0;
            break;
    }
    HOST_ASSERT(false, "unsupported resource op %u on %s", operation,
                o->kind.c_str());
    return 0;
}

extern "C" {
uint32_t w2c_th10__graphics_create(struct w2c_th10__graphics* graphics,
                                   uint32_t params, uint32_t flags) {
    return graphics->host->graphics->create(params, flags);
}
uint32_t w2c_th10__graphics_device(struct w2c_th10__graphics* graphics,
                                   uint32_t handle, uint32_t operation,
                                   uint32_t pointer) {
    return graphics->host->graphics->call_device(handle, operation, pointer);
}
uint32_t w2c_th10__graphics_resource(struct w2c_th10__graphics* graphics,
                                     uint32_t handle, uint32_t operation,
                                     uint32_t pointer) {
    return graphics->host->graphics->call_resource(handle, operation, pointer);
}
}  // extern "C"
