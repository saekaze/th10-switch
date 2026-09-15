#include "gl_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "wasm_host.hpp"

namespace {

const char* kVertex = R"GLSL(
precision highp float;
layout(location=0) in vec4 position;
layout(location=1) in vec4 diffuse;
layout(location=2) in vec2 uv;
layout(location=3) in vec4 specular;
layout(location=4) in vec4 instanceWorld0;
layout(location=5) in vec4 instanceWorld1;
layout(location=6) in vec4 instanceWorld2;
layout(location=7) in vec4 instanceWorld3;
layout(location=8) in vec4 instanceFactor;
uniform mat4 world,view,projection,textureMatrix;
uniform vec4 factor;
uniform vec4 viewport;
uniform bool transformed,textureTransform,fogEnabled,rangeFog;
uniform bool instanced;
uniform int fogMode;
uniform vec3 fogParams;
out vec4 vColor,vSpecular;
out vec2 vUV;
out float vFog;
flat out vec4 vFactor;
void main(){
  mat4 model=instanced?mat4(instanceWorld0,instanceWorld1,instanceWorld2,instanceWorld3):world;
  vec4 eye=view*model*vec4(position.xyz,1.0);
  if(transformed){
vec2 xy=(position.xy-viewport.xy+vec2(.5))/viewport.zw;
gl_Position=vec4(xy.x*2.0-1.0,xy.y*2.0-1.0,position.z*2.0-1.0,1.0)/position.w;
}
  else {
gl_Position=projection*eye;
gl_Position.y=-gl_Position.y;
gl_Position.xy+=gl_Position.w/viewport.zw;
gl_Position.z=gl_Position.z*2.0-gl_Position.w;
}
  vColor=diffuse.bgra;
vSpecular=specular.bgra;
  vFactor=instanced?instanceFactor.bgra:factor;
  vUV=textureTransform&&!transformed?(textureMatrix*vec4(uv,1.0,0.0)).xy:uv;
  float z=rangeFog?length(eye.xyz):abs(eye.z);
  float fog=fogMode==1?exp(-fogParams.z*z):fogMode==2?exp(-pow(fogParams.z*z,2.0)):(fogParams.y-z)/(fogParams.y-fogParams.x);
  vFog=fogEnabled?(transformed?specular.a:fog):1.0;
}
)GLSL";

// Common fragment body. Writing gl_FragDepth disables early-Z on mobile GPUs,
// so the default program omits it; the depth16 program keeps the original
// 16-bit quantization (TH10's D3D9 depth buffer).
const char* kFragmentBody = R"GLSL(
precision highp float;
in vec4 vColor,vSpecular;
in vec2 vUV;
in float vFog;
flat in vec4 vFactor;
uniform sampler2D tex;
uniform bool hasTexture,alphaTest,fogEnabled;
uniform bool depth16;
uniform vec4 fogColor;
uniform int colorOp,alphaOp,colorArg1,colorArg2,alphaArg1,alphaArg2,alphaFunc;
uniform float alphaRef;
out vec4 outColor;
vec4 arg(int value,vec4 textureColor){
int source=value&15;
vec4 c=source==2?textureColor:source==3?vFactor:source==4?vSpecular:vColor;
if((value&16)!=0)c=1.0-c;
if((value&32)!=0)c=vec4(c.a);
return c;
}
vec4 op(int mode,vec4 a,vec4 b,vec4 textureColor){
 if(mode==2)return a;
if(mode==3)return b;
if(mode==4)return a*b;
if(mode==5)return a*b*2.0;
if(mode==6)return a*b*4.0;
if(mode==7)return a+b;
if(mode==8)return a+b-.5;
if(mode==9)return (a+b-.5)*2.0;
if(mode==10)return a-b;
if(mode==11)return a+b*(1.0-a);
 if(mode==12)return mix(b,a,vColor.a);
if(mode==13)return mix(b,a,textureColor.a);
if(mode==14)return mix(b,a,vFactor.a);
if(mode==15)return a+b*(1.0-textureColor.a);
if(mode==16)return mix(b,a,vColor.a);
return vColor;
}
void main(){
vec4 t=hasTexture?texture(tex,vUV):vec4(1.0);
 vec4 c=hasTexture&&colorOp!=1?op(colorOp,arg(colorArg1,t),arg(colorArg2,t),t):vColor;
 c.a=hasTexture&&alphaOp!=1?op(alphaOp,arg(alphaArg1,t),arg(alphaArg2,t),t).a:vColor.a;
c=clamp(c,0.0,1.0);
 if(alphaTest){
float a=c.a*255.0,ref=floor(alphaRef*255.0+.5);
bool pass=alphaFunc==8||(alphaFunc==2&&a<ref)||(alphaFunc==3&&a==ref)||(alphaFunc==4&&a<=ref)||(alphaFunc==5&&a>ref)||(alphaFunc==6&&a!=ref)||(alphaFunc==7&&a>=ref);
if(!pass)discard;
}
 if(fogEnabled)c.rgb=mix(fogColor.rgb,c.rgb,clamp(vFog,0.0,1.0));
outColor=c;
)GLSL";

const char* kFragmentEndDefault = "}\n";
const char* kFragmentEndZ16 = R"GLSL(
 float scaled=gl_FragCoord.z*65536.0;
 gl_FragDepth=depth16?(floor(scaled)+floor(fract(scaled)-gl_FragCoord.z+0.5))/65535.0:gl_FragCoord.z;
}
)GLSL";

const char* kUniNames[] = {
    "viewport",         "transformed", "textureTransform", "instanced",
    "world",            "view",        "projection",       "textureMatrix",
    "fogEnabled",       "rangeFog",    "fogMode",          "fogParams",
    "fogColor",         "hasTexture",  "factor",           "colorOp",
    "colorArg1",        "colorArg2",   "alphaOp",          "alphaArg1",
    "alphaArg2",        "alphaTest",   "alphaRef",         "alphaFunc",
    "depth16",          "tex"};

float color_channel(uint32_t n, int shift) {
    return ((n >> shift) & 255) / 255.0f;
}

int expand5(int n) { return (n * 255 + 15) / 31; }
int expand6(int n) { return (n * 255 + 31) / 63; }

}  // namespace

GLRenderer::GLRenderer(Host* host, int out_w, int out_h, bool clip_control)
    : host_(host), out_w_(out_w), out_h_(out_h), want_clip_(clip_control) {}

GLRenderer::~GLRenderer() {
    on_flush();
    for (auto& kv : surfaces_) {
        glDeleteTextures(1, &kv.second.texture);
        glDeleteFramebuffers(1, &kv.second.framebuffer);
    }
    for (auto& kv : depths_) glDeleteRenderbuffers(1, &kv.second.buffer);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    if (program_z16_) glDeleteProgram(program_z16_);
    if (vert_shader_) glDeleteShader(vert_shader_);
}

GLuint GLRenderer::compile(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        host_log("GL shader compile failed: %s", log);
        return 0;
    }
    return s;
}

bool GLRenderer::link(GLuint vs, GLuint fs, GLuint* out, GLint* unis) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        host_log("GL program link failed: %s", log);
        glDeleteProgram(p);
        return false;
    }
    *out = p;
    for (int i = 0; i < 26; i++)
        unis[i] = glGetUniformLocation(p, kUniNames[i]);
    glUseProgram(p);
    if (unis[25] >= 0) glUniform1i(unis[25], 0);
    return true;
}

bool GLRenderer::init() {
    std::string vs = "#version 300 es\n";
    vs += kVertex;
    if (want_clip_) {
        auto pos = vs.find("position.z*2.0-1.0");
        if (pos != std::string::npos) vs.replace(pos, 20, "position.z");
        const char* line = "gl_Position.z=gl_Position.z*2.0-gl_Position.w;\n";
        pos = vs.find(line);
        if (pos != std::string::npos) vs.erase(pos, strlen(line));
    }
    vert_shader_ = compile(GL_VERTEX_SHADER, vs);
    if (!vert_shader_) return false;

    std::string fs = "#version 300 es\n";
    fs += kFragmentBody;
    fs += kFragmentEndDefault;
    GLuint frag = compile(GL_FRAGMENT_SHADER, fs);
    if (!frag) return false;
    if (!link(vert_shader_, frag, &program_, u_default_)) return false;
    glDeleteShader(frag);

    std::string fs16 = "#version 300 es\n";
    fs16 += kFragmentBody;
    fs16 += kFragmentEndZ16;
    GLuint frag16 = compile(GL_FRAGMENT_SHADER, fs16);
    if (!frag16) return false;
    if (!link(vert_shader_, frag16, &program_z16_, u_z16_)) return false;
    glDeleteShader(frag16);

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glGenBuffers(1, &ibo_);
    vtx_.id = vbo_;
    idx_.id = ebo_;
    inst_.id = ibo_;
    for (int i = 4; i <= 8; i++) glDisableVertexAttribArray(i);
    current_program_ = program_;
    u_ = u_default_;
    glUseProgram(program_);
    batch_bytes_.reserve(1048576);
    worlds_.reserve(2048 * sizeof(th10::InstanceRec));
    quad_.reserve(80);
    host_log("GL renderer ready (%s, batched+instanced)",
             want_clip_ ? "clip-control" : "standard");
    return true;
}

void GLRenderer::bind_texture(GLuint id) {
    if (bound_tex_ != id) {
        glBindTexture(GL_TEXTURE_2D, id);
        bound_tex_ = id;
    }
}

void GLRenderer::bind_framebuffer(GLenum target, GLuint id) {
    const bool read = target != GL_DRAW_FRAMEBUFFER;
    const bool draw = target != GL_READ_FRAMEBUFFER;
    if ((read && read_fbo_ != id) || (draw && draw_fbo_ != id)) {
        glBindFramebuffer(target, id);
        if (read) read_fbo_ = id;
        if (draw) draw_fbo_ = id;
    }
}

uint32_t GLRenderer::upload(Stream& s, GLenum target, const void* bytes,
                            uint32_t count, uint32_t minimum) {
    glBindBuffer(target, s.id);
    uint32_t offset = (s.used + 3u) & ~3u;
    if (s.frame != frame_ || offset + count > s.size) {
        s.size = std::max({s.size, minimum, count});
        glBufferData(target, s.size, nullptr, GL_STREAM_DRAW);
        s.used = offset = 0;
        s.frame = frame_;
    }
    glBufferSubData(target, offset, count, bytes);
    s.used = offset + count;
    return offset;
}

GLRenderer::Surface& GLRenderer::surface(GfxObject* s) {
    Surface& gpu = surfaces_[s->address];
    if (!gpu.texture) {
        glGenTextures(1, &gpu.texture);
        glGenFramebuffers(1, &gpu.framebuffer);
        gpu.version = -1;
        bind_texture(gpu.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gpu.sampler[0] = gpu.sampler[1] = GL_CLAMP_TO_EDGE;
        gpu.sampler[2] = gpu.sampler[3] = GL_LINEAR;
        bind_framebuffer(GL_FRAMEBUFFER, gpu.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, gpu.texture, 0);
    }
    if (gpu.version != (int)s->version) {
        w2c_th100x2Dgame* w = host_->wasm;
        bind_texture(gpu.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const bool first = gpu.version < 0;
        int x0 = 0, y0 = 0, x1 = (int)s->width, y1 = (int)s->height;
        if (!first && !s->dirty_all && s->has_dirty) {
            x0 = s->dirty_x0;
            y0 = s->dirty_y0;
            x1 = s->dirty_x1;
            y1 = s->dirty_y1;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > (int)s->width) x1 = (int)s->width;
            if (y1 > (int)s->height) y1 = (int)s->height;
            if (x1 <= x0 || y1 <= y0) {
                s->dirty_all = false;
                s->has_dirty = false;
                gpu.version = (int)s->version;
                gpu.rendered = false;
                return gpu;
            }
        }
        const uint32_t dw = (uint32_t)(x1 - x0);
        const uint32_t dh = (uint32_t)(y1 - y0);
        if (s->format == 26) {
            uint32_t count = dw * dh;
            if (up16_.size() < count) up16_.resize(count);
            uint8_t* bytes = mem_bytes(w, s->data, s->size);
            const uint32_t pitch = s->pitch;
            if (pitch == s->width * 2) {
                const uint16_t* in = (const uint16_t*)bytes;
                for (uint32_t y = 0; y < dh; y++)
                    for (uint32_t x = 0; x < dw; x++) {
                        uint16_t n = in[(uint32_t)(y0 + (int)y) * s->width +
                                        (uint32_t)(x0 + (int)x)];
                        up16_[y * dw + x] = (uint16_t)((n << 4) | (n >> 12));
                    }
            } else {
                for (uint32_t y = 0; y < dh; y++)
                    for (uint32_t x = 0; x < dw; x++) {
                        const uint8_t* p =
                            bytes + (uint32_t)(y0 + (int)y) * pitch +
                            (uint32_t)(x0 + (int)x) * 2;
                        uint16_t n = (uint16_t)(p[0] | (p[1] << 8));
                        up16_[y * dw + x] = (uint16_t)((n << 4) | (n >> 12));
                    }
            }
            if (first)
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA4, s->width, s->height, 0,
                             GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, up16_.data());
            else
                glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, (GLsizei)dw,
                                (GLsizei)dh, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4,
                                up16_.data());
        } else {
            uint32_t count = dw * dh;
            bool rgb = s->format == 20 || s->format == 22 || s->format == 23;
            uint32_t channels = rgb ? 3 : 4;
            if (rgb) {
                if (rgb_.size() < count * 3) rgb_.resize(count * 3);
            } else {
                if (rgba_.size() < count * 4) rgba_.resize(count * 4);
            }
            uint8_t* input = mem_bytes(w, s->data, s->size);
            uint8_t* out = rgb ? rgb_.data() : rgba_.data();
            for (uint32_t y = 0; y < dh; y++) {
                const uint8_t* row = input + (uint32_t)(y0 + (int)y) * s->pitch;
                uint8_t* dst = out + y * dw * channels;
                for (uint32_t x = 0; x < dw; x++) {
                    const uint32_t sx = (uint32_t)(x0 + (int)x);
                    int r = 0, g = 0, b = 0, a = 255;
                    if (s->format == 21 || s->format == 22) {
                        const uint8_t* p = row + sx * 4;
                        b = p[0];
                        g = p[1];
                        r = p[2];
                        a = s->format == 21 ? p[3] : 255;
                    } else if (s->format == 20) {
                        const uint8_t* p = row + sx * 3;
                        b = p[0];
                        g = p[1];
                        r = p[2];
                    } else if (s->format == 23 || s->format == 24 ||
                               s->format == 25 || s->format == 26) {
                        const uint8_t* p = row + sx * 2;
                        uint16_t n = (uint16_t)(p[0] | (p[1] << 8));
                        if (s->format == 26) {
                            b = (n & 15) * 17;
                            g = ((n >> 4) & 15) * 17;
                            r = ((n >> 8) & 15) * 17;
                            a = (n >> 12) * 17;
                        } else if (s->format == 23) {
                            b = expand5(n & 31);
                            g = expand6((n >> 5) & 63);
                            r = expand5(n >> 11);
                        } else {
                            b = expand5(n & 31);
                            g = expand5((n >> 5) & 31);
                            r = expand5((n >> 10) & 31);
                            a = s->format == 25 ? (n & 0x8000 ? 255 : 0) : 255;
                        }
                    } else if (s->format == 28) {
                        r = g = b = 255;
                        a = row[sx];
                    } else if (s->format == 50) {
                        r = g = b = row[sx];
                    } else {
                        HOST_ASSERT(false, "unimplemented texture format %u",
                                    s->format);
                    }
                    dst[0] = (uint8_t)r;
                    dst[1] = (uint8_t)g;
                    dst[2] = (uint8_t)b;
                    if (!rgb) dst[3] = (uint8_t)a;
                    dst += channels;
                }
            }
            const void* pixels = rgb ? (const void*)rgb_.data()
                                     : (const void*)rgba_.data();
            GLenum format = rgb ? GL_RGB : GL_RGBA;
            GLenum internal = s->format == 23
                                  ? GL_RGB565
                                  : (s->format == 24 || s->format == 25)
                                        ? GL_RGB5_A1
                                        : rgb ? GL_RGB8 : GL_RGBA8;
            if (first)
                glTexImage2D(GL_TEXTURE_2D, 0, internal, s->width, s->height, 0,
                             format, GL_UNSIGNED_BYTE, pixels);
            else
                glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, (GLsizei)dw,
                                (GLsizei)dh, format, GL_UNSIGNED_BYTE, pixels);
        }
        s->dirty_all = false;
        s->has_dirty = false;
        gpu.version = (int)s->version;
        gpu.rendered = false;
    }
    return gpu;
}

void GLRenderer::target(GfxObject* s, GfxObject* depth) {
    Surface& gpu = surface(s);
    GLuint dep = 0, sten = 0;
    if (depth) {
        Depth& d = depths_[depth->address];
        if (!d.buffer) {
            glGenRenderbuffers(1, &d.buffer);
            d.stencil = depth->format == 75;
            glBindRenderbuffer(GL_RENDERBUFFER, d.buffer);
            glRenderbufferStorage(GL_RENDERBUFFER,
                                 d.stencil ? GL_DEPTH24_STENCIL8
                                 : depth->format == 80
                                     ? GL_DEPTH_COMPONENT16
                                     : GL_DEPTH_COMPONENT24,
                                 depth->width, depth->height);
        }
        dep = d.buffer;
        if (d.stencil) sten = d.buffer;
    }
    bind_framebuffer(GL_FRAMEBUFFER, gpu.framebuffer);
    if (gpu.attached_depth != dep) {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, dep);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, sten);
        gpu.attached_depth = dep;
    }
}

void GLRenderer::on_flush() {
    if (!batching_) return;
    batching_ = false;
    DrawPacket d;
    d.pipe = batch_pipe_;
    d.stride = batch_pipe_.stride;
    if (instancing_) {
        const void* inst =
            worlds_.size() > sizeof(th10::InstanceRec) ? worlds_.data()
                                                       : nullptr;
        uint32_t ib =
            worlds_.size() > sizeof(th10::InstanceRec) ? (uint32_t)worlds_.size()
                                                       : 0;
        d.primitive = th10::kPrimStrip;
        d.count = 2;
        d.vertices = quad_.data();
        d.vertex_bytes = (uint32_t)quad_.size();
        d.pipe.primitive = th10::kPrimStrip;
        issue(d, inst, ib);
    } else {
        d.primitive = th10::kPrimTriangles;
        d.count = batch_count_;
        d.vertices = batch_bytes_.data();
        d.vertex_bytes = (uint32_t)batch_bytes_.size();
        d.pipe.primitive = th10::kPrimTriangles;
        issue(d, nullptr, 0);
    }
    batch_bytes_.clear();
    worlds_.clear();
    quad_.clear();
    batch_count_ = 0;
    instancing_ = false;
}

void GLRenderer::on_draw(const DrawPacket& d) {
    draws++;
    const th10::CompactPipeline& p = d.pipe;
    if (d.has_indices || !th10::is_triangle_like(p.primitive)) {
        on_flush();
        issue(d, nullptr, 0);
        return;
    }
    const bool inst =
        th10::is_world_uv_sprite(p.fvf, p.stride, p.primitive, d.count) &&
        d.vertex_bytes >= 80;
    if (inst) {
        if (batching_ &&
            (!instancing_ ||
             !th10::can_instance_with(batch_pipe_, p, quad_.data(), d.vertices,
                                      80)))
            on_flush();
        if (!batching_) {
            batch_pipe_ = p;
            quad_.assign(d.vertices, d.vertices + 80);
            batching_ = instancing_ = true;
        }
        size_t old = worlds_.size();
        worlds_.resize(old + sizeof(th10::InstanceRec));
        th10::write_instance((th10::InstanceRec*)(worlds_.data() + old), p);
        if (worlds_.size() >= 2048 * sizeof(th10::InstanceRec)) on_flush();
        return;
    }
    if (batching_ &&
        (instancing_ || !th10::can_triangle_batch(batch_pipe_, p)))
        on_flush();
    if (!batching_) {
        batch_pipe_ = p;
        batching_ = true;
        instancing_ = false;
    }
    size_t start = batch_bytes_.size();
    batch_bytes_.resize(start + (size_t)d.count * 3 * p.stride);
    th10::expand_to_triangles(batch_bytes_.data() + start, d.vertices, p.stride,
                              p.primitive, d.count);
    batch_count_ += d.count;
    if (batch_bytes_.size() >= 1048576u) on_flush();
}

void GLRenderer::apply_pipeline(const th10::CompactPipeline& p, bool instanced,
                                bool depth16) {
    GLuint want = depth16 ? program_z16_ : program_;
    if (current_program_ != want) {
        glUseProgram(want);
        current_program_ = want;
        u_ = depth16 ? u_z16_ : u_default_;
        cache_.valid = false;  // uniforms belong to the other program
        last_pipe_valid_ = false;
    }

    GraphicsHost* g = host_->graphics;
    GfxObject* tex = nullptr;
    if (p.texture) tex = g->get(p.texture)->surface;
    Surface* texture = tex ? &surface(tex) : nullptr;
    GfxObject* t = g->get(p.target);
    GfxObject* dt = p.depth_target ? g->get(p.depth_target) : nullptr;
    target(t, dt);

    const bool key_same = last_pipe_valid_ && last_instanced_ == instanced &&
                          last_depth16_ == depth16 &&
                          th10::pipeline_key_equal(last_pipe_, p);

    auto seti = [&](int i, int v) {
        if (u_[i] >= 0) glUniform1i(u_[i], v);
    };
    auto set4 = [&](int i, const float* v) {
        if (u_[i] >= 0) glUniform4fv(u_[i], 1, v);
    };
    auto set3 = [&](int i, const float* v) {
        if (u_[i] >= 0) glUniform3fv(u_[i], 1, v);
    };
    auto setm = [&](int i, const float* v) {
        if (u_[i] >= 0) glUniformMatrix4fv(u_[i], 1, GL_FALSE, v);
    };
    auto setf = [&](int i, float v) {
        if (u_[i] >= 0) glUniform1f(u_[i], v);
    };

    const uint8_t* vp = p.viewport;
    uint32_t vx = vp[0] | (vp[1] << 8) | (vp[2] << 16) | (vp[3] << 24);
    uint32_t vy = vp[4] | (vp[5] << 8) | (vp[6] << 16) | (vp[7] << 24);
    uint32_t vw = vp[8] | (vp[9] << 8) | (vp[10] << 16) | (vp[11] << 24);
    uint32_t vh = vp[12] | (vp[13] << 8) | (vp[14] << 16) | (vp[15] << 24);
    float zn, zf;
    memcpy(&zn, vp + 16, 4);
    memcpy(&zf, vp + 20, 4);
    if (!cache_.valid || cache_.vx != vx || cache_.vy != vy || cache_.vw != vw ||
        cache_.vh != vh || cache_.zn != zn || cache_.zf != zf) {
        glViewport(vx, vy, vw, vh);
        glDepthRangef(zn, zf);
        cache_.vx = vx;
        cache_.vy = vy;
        cache_.vw = vw;
        cache_.vh = vh;
        cache_.zn = zn;
        cache_.zf = zf;
    }

    if (!key_same) {
        float vpv[4] = {(float)vx, (float)vy, (float)vw, (float)vh};
        set4(0, vpv);
        bool transformed = th10::transformed_fvf(p.fvf);
        seti(1, transformed ? 1 : 0);
        seti(2, p.texture_transform ? 1 : 0);
        seti(3, instanced ? 1 : 0);
        setm(5, p.view);
        setm(6, p.proj);
        setm(7, p.texmat);
        seti(8, p.fog_enabled ? 1 : 0);
        seti(9, p.range_fog ? 1 : 0);
        seti(10, (int)p.fog_mode);
        float fogp[3] = {p.fog_near, p.fog_far, p.fog_density};
        set3(11, fogp);
        float fogv[4] = {color_channel(p.fog_color, 16),
                         color_channel(p.fog_color, 8),
                         color_channel(p.fog_color, 0),
                         color_channel(p.fog_color, 24)};
        set4(12, fogv);
        seti(13, texture ? 1 : 0);
#ifndef NDEBUG
        auto check_op = [](uint32_t v) {
            if (v < 1 || v > 16)
                HOST_ASSERT(false, "unsupported texture op %u", v);
        };
        check_op(p.color_op);
        check_op(p.alpha_op);
#endif
        seti(15, (int)p.color_op);
        seti(16, (int)p.color_arg1);
        seti(17, (int)p.color_arg2);
        seti(18, (int)p.alpha_op);
        seti(19, (int)p.alpha_arg1);
        seti(20, (int)p.alpha_arg2);
        seti(21, p.alpha_test ? 1 : 0);
        setf(22, p.alpha_ref / 255.0f);
        seti(23, (int)p.alpha_func);
        seti(24, depth16 ? 1 : 0);
    }

    if (!instanced) {
        if (!key_same ||
            memcmp(last_pipe_.world, p.world, sizeof(p.world)) != 0) {
            setm(4, p.world);
        }
        if (!key_same || last_pipe_.factor != p.factor) {
            float factorv[4] = {color_channel(p.factor, 16),
                                color_channel(p.factor, 8),
                                color_channel(p.factor, 0),
                                color_channel(p.factor, 24)};
            set4(14, factorv);
        }
    }

    if (texture) {
        glActiveTexture(GL_TEXTURE0);
        bind_texture(texture->texture);
        if (!key_same) {
            auto address = [](uint32_t n) {
                switch (n) {
                    case 1: return (GLint)GL_REPEAT;
                    case 2: return (GLint)GL_MIRRORED_REPEAT;
                    case 3:
                    case 4:
                    case 5: return (GLint)GL_CLAMP_TO_EDGE;
                    default: return (GLint)GL_REPEAT;
                }
            };
            GLint samp[4] = {
                address(p.wrap_s), address(p.wrap_t),
                p.min_filter == 1 ? (GLint)GL_NEAREST : (GLint)GL_LINEAR,
                p.mag_filter == 1 ? (GLint)GL_NEAREST : (GLint)GL_LINEAR};
            const GLenum names[4] = {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T,
                                     GL_TEXTURE_MIN_FILTER,
                                     GL_TEXTURE_MAG_FILTER};
            for (int i = 0; i < 4; i++) {
                if (texture->sampler[i] != samp[i]) {
                    glTexParameteri(GL_TEXTURE_2D, names[i], samp[i]);
                    texture->sampler[i] = samp[i];
                }
            }
        }
    }

    if (!key_same) {
        if (!cache_.valid || cache_.dither != (bool)p.dither) {
            if (p.dither)
                glEnable(GL_DITHER);
            else
                glDisable(GL_DITHER);
            cache_.dither = (bool)p.dither;
        }
        if (!cache_.valid || cache_.depth_test != (bool)p.depth_test) {
            if (p.depth_test)
                glEnable(GL_DEPTH_TEST);
            else
                glDisable(GL_DEPTH_TEST);
            cache_.depth_test = (bool)p.depth_test;
        }
        if (!cache_.valid || cache_.depth_write != (bool)p.depth_write) {
            glDepthMask(p.depth_write ? GL_TRUE : GL_FALSE);
            cache_.depth_write = (bool)p.depth_write;
        }
        static const GLenum kCmp[9] = {0,          GL_NEVER,    GL_LESS,
                                       GL_EQUAL,   GL_LEQUAL,   GL_GREATER,
                                       GL_NOTEQUAL, GL_GEQUAL,  GL_ALWAYS};
        if (!cache_.valid || cache_.depth_func != p.depth_func) {
            glDepthFunc(kCmp[p.depth_func < 9 ? p.depth_func : 4]);
            cache_.depth_func = p.depth_func;
        }
        static const GLenum kBlend[12] = {
            0,         GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
            GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA,
            GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
            GL_SRC_ALPHA_SATURATE};
        static const GLenum kEq[6] = {0, GL_FUNC_ADD, GL_FUNC_SUBTRACT,
                                      GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX};
        if (!cache_.valid || cache_.blend != (bool)p.blend ||
            cache_.src_blend != p.src_blend || cache_.dst_blend != p.dst_blend ||
            cache_.blend_eq != p.blend_eq) {
            if (p.blend) {
                glEnable(GL_BLEND);
                uint32_t src = p.src_blend < 12 ? p.src_blend : 2;
                uint32_t dst = p.dst_blend < 12 ? p.dst_blend : 1;
                glBlendFunc(kBlend[src], kBlend[dst]);
                glBlendEquation(kEq[p.blend_eq < 6 ? p.blend_eq : 1]);
            } else {
                glDisable(GL_BLEND);
            }
            cache_.blend = (bool)p.blend;
            cache_.src_blend = p.src_blend;
            cache_.dst_blend = p.dst_blend;
            cache_.blend_eq = p.blend_eq;
        }
        if (!cache_.valid || cache_.cull != p.cull) {
            if (p.cull != 1) {
                glEnable(GL_CULL_FACE);
                glFrontFace(GL_CW);
                glCullFace(p.cull == 2 ? GL_BACK : GL_FRONT);
            } else {
                glDisable(GL_CULL_FACE);
            }
            cache_.cull = p.cull;
        }
        if (!cache_.valid || cache_.color_mask != p.color_mask) {
            uint32_t mask = p.color_mask;
            glColorMask((mask & 1) ? GL_TRUE : GL_FALSE,
                        (mask & 2) ? GL_TRUE : GL_FALSE,
                        (mask & 4) ? GL_TRUE : GL_FALSE,
                        (mask & 8) ? GL_TRUE : GL_FALSE);
            cache_.color_mask = p.color_mask;
        }
        cache_.valid = true;
    }

    last_pipe_ = p;
    last_instanced_ = instanced;
    last_depth16_ = depth16;
    last_pipe_valid_ = true;
}

void GLRenderer::issue(const DrawPacket& d, const void* instance,
                       uint32_t instance_bytes) {
    if (!d.vertices || !d.vertex_bytes) return;
    batches++;
    GraphicsHost* g = host_->graphics;
    const th10::CompactPipeline& p = d.pipe;
    bool depth16 = p.depth_test && p.depth_target &&
                   g->get(p.depth_target)->format == 80;
    apply_pipeline(p, instance != nullptr, depth16);

    bool transformed = th10::transformed_fvf(p.fvf);
    glBindVertexArray(vao_);
    uint32_t base = upload(vtx_, GL_ARRAY_BUFFER, d.vertices, d.vertex_bytes,
                           1048576);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, transformed ? 4 : 3, GL_FLOAT, GL_FALSE, d.stride,
                          (const void*)(uintptr_t)base);
    size_t offset = transformed ? 16 : 12;
    if (p.fvf & 16) offset += 12;
    if (p.fvf & 32) offset += 4;
    const int locs[2] = {1, 3};
    const uint32_t flags[2] = {64, 128};
    for (int i = 0; i < 2; i++) {
        if (p.fvf & flags[i]) {
            glEnableVertexAttribArray(locs[i]);
            glVertexAttribPointer(locs[i], 4, GL_UNSIGNED_BYTE, GL_TRUE,
                                  d.stride,
                                  (const void*)(uintptr_t)(base + offset));
            offset += 4;
        } else {
            glDisableVertexAttribArray(locs[i]);
            glVertexAttrib4f(locs[i], 1, 1, 1, 1);
        }
    }
    if (p.fvf & 0xf00) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, d.stride,
                              (const void*)(uintptr_t)(base + offset));
    } else {
        glDisableVertexAttribArray(2);
        glVertexAttrib2f(2, 0, 0);
    }

    static const GLenum kModes[7] = {0,         GL_POINTS, GL_LINES,
                                     GL_LINE_STRIP, GL_TRIANGLES,
                                     GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN};
    uint32_t count = th10::vertex_count(d.primitive, d.count);
    GLenum mode = kModes[d.primitive < 7 ? d.primitive : 4];

    if (instance && instance_bytes >= 2 * sizeof(th10::InstanceRec)) {
        uint32_t b = upload(inst_, GL_ARRAY_BUFFER, instance, instance_bytes,
                            65536);
        for (int i = 0; i < 4; i++) {
            glEnableVertexAttribArray(4 + i);
            glVertexAttribPointer(4 + i, 4, GL_FLOAT, GL_FALSE, 68,
                                  (const void*)(uintptr_t)(b + i * 16));
            glVertexAttribDivisor(4 + i, 1);
        }
        glEnableVertexAttribArray(8);
        glVertexAttribPointer(8, 4, GL_UNSIGNED_BYTE, GL_TRUE, 68,
                              (const void*)(uintptr_t)(b + 64));
        glVertexAttribDivisor(8, 1);
        glDrawArraysInstanced(mode, 0, count,
                              (GLsizei)(instance_bytes / sizeof(th10::InstanceRec)));
        for (int i = 4; i <= 8; i++) {
            glVertexAttribDivisor(i, 0);
            glDisableVertexAttribArray(i);
        }
    } else if (d.has_indices) {
        uint32_t b = upload(idx_, GL_ELEMENT_ARRAY_BUFFER, d.indices,
                            d.index_bytes, 65536);
        glDrawElements(mode, count,
                       d.index_format == 101 ? GL_UNSIGNED_SHORT
                                             : GL_UNSIGNED_INT,
                       (const void*)(uintptr_t)b);
    } else {
        glDrawArrays(mode, 0, count);
    }
    surfaces_[p.target].rendered = true;
}

void GLRenderer::on_clear(const ClearPacket& c) {
    on_flush();
    GraphicsHost* g = host_->graphics;
    GfxObject* t = g->get(c.target);
    GfxObject* dt = c.depth_target ? g->get(c.depth_target) : nullptr;
    target(t, dt);
    uint32_t vx = c.viewport[0] | (c.viewport[1] << 8) |
                  (c.viewport[2] << 16) | (c.viewport[3] << 24);
    uint32_t vy = c.viewport[4] | (c.viewport[5] << 8) |
                  (c.viewport[6] << 16) | (c.viewport[7] << 24);
    uint32_t vw = c.viewport[8] | (c.viewport[9] << 8) |
                  (c.viewport[10] << 16) | (c.viewport[11] << 24);
    uint32_t vh = c.viewport[12] | (c.viewport[13] << 8) |
                  (c.viewport[14] << 16) | (c.viewport[15] << 24);
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    cache_.valid = false;
    last_pipe_valid_ = false;
    glClearColor(color_channel(c.color, 16), color_channel(c.color, 8),
                 color_channel(c.color, 0), color_channel(c.color, 24));
    glClearDepthf(c.depth);
    glClearStencil(c.stencil);
    glStencilMask(255);
    GLbitfield bits = 0;
    if (c.flags & 1) bits |= GL_COLOR_BUFFER_BIT;
    if (c.flags & 2) bits |= GL_DEPTH_BUFFER_BIT;
    if (c.flags & 4) bits |= GL_STENCIL_BUFFER_BIT;
    if (c.rects.empty()) {
        glScissor(vx, vy, vw, vh);
        glClear(bits);
    } else {
        for (size_t i = 0; i + 3 < c.rects.size(); i += 4) {
            int32_t x = (int32_t)c.rects[i], y = (int32_t)c.rects[i + 1];
            int32_t r = (int32_t)c.rects[i + 2], b = (int32_t)c.rects[i + 3];
            glScissor(x, y, r - x, b - y);
            glClear(bits);
        }
    }
    glDisable(GL_SCISSOR_TEST);
    surfaces_[c.target].rendered = true;
}

bool GLRenderer::on_copy(GfxObject* src, const int32_t rect[4], GfxObject* dst,
                         const int32_t point[2]) {
    on_flush();
    Surface& from = surface(src);
    Surface& to = surface(dst);
    int32_t x = rect[0], y = rect[1], r = rect[2], b = rect[3];
    int32_t w = r - x, h = b - y;
    bind_framebuffer(GL_READ_FRAMEBUFFER, from.framebuffer);
    bind_framebuffer(GL_DRAW_FRAMEBUFFER, to.framebuffer);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(x, y, r, b, point[0], point[1], point[0] + w,
                      point[1] + h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    to.rendered = true;
    to.version = (int)dst->version + 1;
    return true;
}

void GLRenderer::on_present(GfxObject* device) {
    on_flush();
    GfxObject* s = device->back;
    Surface& gpu = surface(s);
    bind_framebuffer(GL_READ_FRAMEBUFFER, gpu.framebuffer);
    bind_framebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    int32_t dw = out_h_ * (int32_t)s->width / (int32_t)s->height;
    int32_t dx = (out_w_ - dw) / 2;
    glBlitFramebuffer(0, 0, s->width, s->height, dx, out_h_, dx + dw, 0,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    frame_++;
}

void GLRenderer::on_read_surface(GfxObject* s) {
    on_flush();
    auto it = surfaces_.find(s->address);
    if (it == surfaces_.end() || !it->second.rendered) return;
    Surface& gpu = it->second;
    w2c_th100x2Dgame* w = host_->wasm;
    uint32_t count = s->width * s->height;
    if (rgba_.size() < count * 4) rgba_.resize(count * 4);
    bind_framebuffer(GL_FRAMEBUFFER, gpu.framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, s->width, s->height, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba_.data());
    uint8_t* output = mem_bytes(w, s->data, s->size);
    for (uint32_t y = 0; y < s->height; y++)
        for (uint32_t x = 0; x < s->width; x++) {
            uint32_t p = (y * s->width + x) * 4;
            int r = rgba_[p], g = rgba_[p + 1], b = rgba_[p + 2],
                a = rgba_[p + 3];
            if (s->format == 21 || s->format == 22) {
                uint32_t i = y * s->pitch + x * 4;
                output[i] = (uint8_t)b;
                output[i + 1] = (uint8_t)g;
                output[i + 2] = (uint8_t)r;
                output[i + 3] = s->format == 21 ? (uint8_t)a : 255;
            } else if (s->format == 23 || s->format == 24 ||
                       s->format == 25 || s->format == 26) {
                uint32_t n;
                if (s->format == 26)
                    n = ((a >> 4) << 12) | ((r >> 4) << 8) |
                        ((g >> 4) << 4) | (b >> 4);
                else if (s->format == 23)
                    n = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                else
                    n = ((s->format == 25 && a >= 128) ? 0x8000 : 0) |
                        ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
                uint32_t i = y * s->pitch + x * 2;
                output[i] = n & 255;
                output[i + 1] = n >> 8;
            } else if (s->format == 28) {
                output[y * s->pitch + x] = (uint8_t)a;
            } else if (s->format == 50) {
                output[y * s->pitch + x] = (uint8_t)r;
            } else {
                HOST_ASSERT(false, "unsupported readback format %u", s->format);
            }
        }
    gpu.rendered = false;
}

void GLRenderer::on_release(GfxObject* s) {
    on_flush();
    auto dit = depths_.find(s->address);
    if (dit != depths_.end()) {
        glDeleteRenderbuffers(1, &dit->second.buffer);
        depths_.erase(dit);
    }
    auto it = surfaces_.find(s->address);
    if (it == surfaces_.end()) return;
    glDeleteTextures(1, &it->second.texture);
    glDeleteFramebuffers(1, &it->second.framebuffer);
    surfaces_.erase(it);
    bound_tex_ = read_fbo_ = draw_fbo_ = 0xFFFFFFFFu;
}
