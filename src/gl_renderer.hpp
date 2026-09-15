// C++ port of site/runtime/webgl.mjs (WebGLD3D9) for GLES3.
// Used identically on Linux/EGL (test) and Switch (SDL2/EGL/GLES3).
// Batches triangle-like draws and instances TH10 world-UV sprites the same
// way the upstream GLES renderer does — without changing simulation or
// pixel protocol.
#pragma once
#include <GLES3/gl3.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "draw_batch.hpp"
#include "host_graphics.hpp"

struct GLRenderer : GfxRenderer {
    GLRenderer(Host* host, int out_w, int out_h, bool clip_control);
    ~GLRenderer() override;
    bool init();

    void on_draw(const DrawPacket& d) override;
    void on_clear(const ClearPacket& c) override;
    bool on_copy(GfxObject* src, const int32_t rect[4], GfxObject* dst,
                 const int32_t point[2]) override;
    void on_present(GfxObject* device) override;
    void on_read_surface(GfxObject* s) override;
    void on_release(GfxObject* s) override;
    void on_flush() override;

    uint64_t draws = 0;    // game Draw*() calls
    uint64_t batches = 0;  // actual glDraw* submissions

  private:
    int out_w_, out_h_;
    bool want_clip_;
    GLuint program_ = 0;       // default (no gl_FragDepth)
    GLuint program_z16_ = 0;   // depth16 quantization
    GLuint vert_shader_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0, ebo_ = 0, ibo_ = 0;
    GLint u_default_[32]{};
    GLint u_z16_[32]{};
    GLint* u_ = u_default_;
    GLuint current_program_ = 0;

    struct Surface {
        GLuint texture = 0, framebuffer = 0;
        int version = -1;
        bool rendered = false;
        GLuint attached_depth = 0xFFFFFFFFu;
        GLint sampler[4] = {-1, -1, -1, -1};
    };
    std::map<uint32_t, Surface> surfaces_;
    struct Depth {
        GLuint buffer = 0;
        bool stencil = false;
    };
    std::map<uint32_t, Depth> depths_;
    std::vector<uint8_t> rgba_;
    std::vector<uint8_t> rgb_;
    std::vector<uint16_t> up16_;

    Host* host_ = nullptr;

    // Streaming VBOs. Orphaned once per frame so uploads never stall.
    struct Stream {
        GLuint id = 0;
        uint32_t size = 0, used = 0, frame = 0xFFFFFFFFu;
    };
    Stream vtx_{}, idx_{}, inst_{};
    uint32_t frame_ = 0;

    GLuint bound_tex_ = 0xFFFFFFFFu;
    GLuint read_fbo_ = 0xFFFFFFFFu, draw_fbo_ = 0xFFFFFFFFu;

    struct GlCache {
        bool valid = false;
        bool dither = false, depth_test = false, depth_write = true, blend = false;
        uint32_t depth_func = 4, src_blend = 2, dst_blend = 1, blend_eq = 1;
        uint32_t cull = 1, color_mask = 15;
        uint32_t vx = 0xFFFFFFFFu, vy = 0, vw = 0, vh = 0;
        float zn = 0, zf = 1;
    } cache_;

    // Last pipeline submitted to GL. Matching keys skip uniform / enable
    // traffic; world+factor are compared separately (not in the batch key).
    th10::CompactPipeline last_pipe_{};
    bool last_pipe_valid_ = false;
    bool last_instanced_ = false;
    bool last_depth16_ = false;

    // Pending batch.
    bool batching_ = false, instancing_ = false;
    th10::CompactPipeline batch_pipe_{};
    std::vector<uint8_t> batch_bytes_;
    std::vector<uint8_t> quad_;
    std::vector<uint8_t> worlds_;
    uint32_t batch_count_ = 0;

    GLuint compile(GLenum type, const std::string& src);
    bool link(GLuint vs, GLuint fs, GLuint* out, GLint* unis);
    Surface& surface(GfxObject* s);
    void target(GfxObject* s, GfxObject* depth);
    void bind_texture(GLuint id);
    void bind_framebuffer(GLenum target, GLuint id);
    uint32_t upload(Stream& s, GLenum target, const void* bytes, uint32_t count,
                    uint32_t minimum);
    void issue(const DrawPacket& d, const void* instance, uint32_t instance_bytes);
    void apply_pipeline(const th10::CompactPipeline& p, bool instanced,
                        bool depth16);
};
