#include "host_fonts.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftmodapi.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <unordered_map>

#include "cp932_table.hpp"
#include "cp936_table.hpp"
#include "wasm_host.hpp"

namespace {

uint32_t table_lookup(const uint32_t* table, uint32_t count, uint32_t code) {
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t key = table[mid] >> 16;
        if (key == code) return table[mid] & 0xffff;
        if (key < code)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0xfffd;
}

bool is_sjis_lead(uint8_t b) {
    return (b >= 0x81 && b <= 0x9f) || (b >= 0xe0 && b <= 0xfc);
}
bool is_sjis_trail(uint8_t b) {
    return (b >= 0x40 && b <= 0x7e) || (b >= 0x80 && b <= 0xfc);
}
bool is_gbk_lead(uint8_t b) { return b >= 0x81 && b <= 0xfe; }
bool is_gbk_trail(uint8_t b) {
    return (b >= 0x40 && b <= 0x7e) || (b >= 0x80 && b <= 0xfe);
}

}  // namespace

namespace {
bool sjis_pair(uint8_t lead, uint8_t trail, uint32_t& out) {
    if (!is_sjis_lead(lead) || !is_sjis_trail(trail)) return false;
    uint32_t code = ((uint32_t)lead << 8) | trail;
    uint32_t uni = table_lookup(cp932_table_TABLE, cp932_table_COUNT, code);
    if (uni == 0xfffd) return false;
    out = uni;
    return true;
}
bool gbk_pair(uint8_t lead, uint8_t trail, uint32_t& out) {
    if (!is_gbk_lead(lead) || !is_gbk_trail(trail)) return false;
    uint32_t code = ((uint32_t)lead << 8) | trail;
    uint32_t uni = table_lookup(cp936_table_TABLE, cp936_table_COUNT, code);
    if (uni == 0xfffd) return false;
    out = uni;
    return true;
}
}  // namespace

std::vector<uint32_t> decode932(const uint8_t* bytes, uint32_t length) {
    // Fast path: WHATWG shift_jis for well-formed text.
    std::vector<uint32_t> fast;
    fast.reserve(length);
    bool clean = true;
    for (uint32_t pos = 0; pos < length;) {
        uint8_t b = bytes[pos];
        if (b < 0x80) {
            uint32_t c = b == 0x5c ? 0xa5 : b == 0x7e ? 0x203e : b;
            if (c == 0x1a || c == 0x1c || c == 0x7f) clean = false;
            fast.push_back(c);
            pos++;
        } else if (b >= 0xa1 && b <= 0xdf) {
            fast.push_back(0xff61 + b - 0xa1);
            pos++;
        } else if (is_sjis_lead(b) && pos + 1 < length) {
            uint32_t uni;
            if (sjis_pair(b, bytes[pos + 1], uni)) {
                fast.push_back(uni);
            } else {
                fast.push_back(0xfffd);
                clean = false;
            }
            pos += 2;
        } else {
            fast.push_back(0xfffd);
            clean = false;
            pos++;
        }
    }
    if (clean) return fast;
    // Fallback: Windows CP932 consumes malformed pairs with U+30FB.
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < length; i++) {
        uint8_t b = bytes[i];
        if (b <= 0x80) {
            out.push_back(b);
        } else if (b >= 0xa1 && b <= 0xdf) {
            out.push_back(0xff61 + b - 0xa1);
        } else if (b == 0xa0) {
            out.push_back(0xf8f0);
        } else if (b >= 0xfd) {
            out.push_back(0xf8f1 + b - 0xfd);
        } else {
            uint8_t trail = i + 1 < length ? bytes[i + 1] : 0;
            bool dangling = i + 1 >= length || trail == 0;
            if (dangling) {
                out.push_back(0x30fb);
                continue;
            }
            uint32_t uni;
            out.push_back(sjis_pair(b, trail, uni) ? uni : 0x30fb);
            i++;
        }
    }
    return out;
}

std::vector<uint32_t> decode936(const uint8_t* bytes, uint32_t length) {
    std::vector<uint32_t> fast;
    fast.reserve(length);
    bool clean = true;
    for (uint32_t pos = 0; pos < length;) {
        uint8_t b = bytes[pos];
        if (b < 0x80) {
            fast.push_back(b);
            pos++;
        } else if (b == 0x80) {
            fast.push_back(0x20ac);
            pos++;
        } else if (b == 0xff) {
            fast.push_back('?');
            pos++;
        } else if (is_gbk_lead(b) && pos + 1 < length) {
            uint32_t uni;
            if (gbk_pair(b, bytes[pos + 1], uni)) {
                fast.push_back(uni);
            } else {
                fast.push_back(0xfffd);
                clean = false;
            }
            pos += 2;
        } else {
            fast.push_back(0xfffd);
            clean = false;
            pos++;
        }
    }
    if (clean) return fast;
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < length; i++) {
        uint8_t b = bytes[i];
        if (b <= 0x80) {
            out.push_back(b == 0x80 ? 0x20ac : b);
        } else if (b == 255) {
            out.push_back('?');
        } else {
            bool dangling = i + 1 >= length || bytes[i + 1] == 0;
            if (dangling) {
                out.push_back('?');
                continue;
            }
            uint32_t uni;
            out.push_back(gbk_pair(b, bytes[i + 1], uni) ? uni : '?');
            i++;
        }
    }
    return out;
}

struct FontsHost::FtFace {
    FT_Library lib = nullptr;
    FT_Face face = nullptr;
    int cur_height = -1;
    std::vector<uint8_t> membuf;  // owned bytes for memory faces
    struct Glyph {
        int left = 0, top = 0, w = 0, rows = 0, pitch = 0, advance = 0;
        std::vector<uint8_t> bits;
        bool ok = false;
    };
    std::unordered_map<uint64_t, Glyph> glyphs;
};

FontsHost::FontsHost() = default;
FontsHost::~FontsHost() = default;

bool FontsHost::ensure_face(bool chs, int height) {
    std::unique_ptr<FtFace>& slot = chs ? chs_ : jp_;
    if (!slot || !slot->face) return false;
    if (slot->cur_height != height) {
        if (FT_Set_Pixel_Sizes(slot->face, 0, (FT_UInt)height)) return false;
        slot->cur_height = height;
    }
    return true;
}

void FontsHost::draw_text(Object* dc, Object* bitmap, int x, int y,
                          const std::vector<uint32_t>& chars) {
    bool chs = dc->font->charset == 134;
    int height = dc->font->font_height < 0 ? -dc->font->font_height
                                           : dc->font->font_height;
    if (height <= 0) height = 32;
    if (!ensure_face(chs, height)) return;  // no font, leave blank
    std::unique_ptr<FtFace>& slot = chs ? chs_ : jp_;
    FT_Face face = slot->face;
    uint8_t* pixels = mem_bytes(host->wasm, bitmap->data, bitmap->size);
    int ascent = (int)(face->size->metrics.ascender + 32) >> 6;
    uint32_t color = dc->color;
    int src_r = color & 255, src_g = (color >> 8) & 255,
        src_b = (color >> 16) & 255;
    int pen = x;
    auto load_glyph = [&](uint32_t cp) -> const FtFace::Glyph* {
        uint64_t key = ((uint64_t)(uint32_t)height << 32) | cp;
        auto it = slot->glyphs.find(key);
        if (it != slot->glyphs.end()) return &it->second;
        FtFace::Glyph g;
        if (!FT_Load_Char(face, (FT_ULong)cp, FT_LOAD_RENDER)) {
            FT_GlyphSlot slotg = face->glyph;
            g.left = slotg->bitmap_left;
            g.top = slotg->bitmap_top;
            g.w = (int)slotg->bitmap.width;
            g.rows = (int)slotg->bitmap.rows;
            g.pitch = slotg->bitmap.pitch;
            g.advance = ((int)slotg->advance.x + 32) >> 6;
            size_t nbytes = (size_t)std::max(g.pitch, 0) * (size_t)std::max(g.rows, 0);
            if (nbytes && slotg->bitmap.buffer)
                g.bits.assign(slotg->bitmap.buffer, slotg->bitmap.buffer + nbytes);
            g.ok = true;
        } else {
            g.advance = height / 2;
            g.ok = false;
        }
        if (slot->glyphs.size() >= 8192) slot->glyphs.clear();
        auto ins = slot->glyphs.emplace(key, std::move(g));
        return &ins.first->second;
    };
    for (uint32_t cp : chars) {
        const FtFace::Glyph* cached = load_glyph(cp);
        if (!cached->ok) {
            pen += cached->advance;
            continue;
        }
        int left = cached->left, top = cached->top;
        int w = cached->w, rows = cached->rows;
        int pitch = cached->pitch;
        const uint8_t* buf = cached->bits.data();
        for (int j = 0; j < rows; j++) {
            int row = y + ascent - top + j;
            if (row < 0 || row >= (int)bitmap->height) continue;
            for (int i = 0; i < w; i++) {
                int col = pen + left + i;
                if (col < 0 || col >= (int)bitmap->width) continue;
                int src_a = buf[j * pitch + i];
                if (!src_a) continue;
                uint32_t off = (uint32_t)row * bitmap->pitch + (uint32_t)col * 2;
                uint16_t cur = pixels[off] | (pixels[off + 1] << 8);
                int dst_a = (((cur >> 12) & 0xf) ^ 0xf) * 17;
                int dst_r = ((cur >> 8) & 0xf) * 17;
                int dst_g = ((cur >> 4) & 0xf) * 17;
                int dst_b = (cur & 0xf) * 17;
                // "over" composite in 8-bit, then quantize to 4-bit with the
                // alpha nibble inverted for storage (pre-second-invert).
                uint64_t t = (uint64_t)dst_a * (255 - src_a);
                uint64_t a255 = (uint64_t)src_a * 255 + t;
                uint64_t out_a = (a255 + 127) / 255;
                uint64_t rr = ((uint64_t)src_r * src_a * 255 +
                               (uint64_t)dst_r * t + a255 / 2) /
                              a255;
                uint64_t gg = ((uint64_t)src_g * src_a * 255 +
                               (uint64_t)dst_g * t + a255 / 2) /
                              a255;
                uint64_t bb = ((uint64_t)src_b * src_a * 255 +
                               (uint64_t)dst_b * t + a255 / 2) /
                              a255;
                uint16_t a4 = (uint16_t)((out_a * 15 + 127) / 255);
                uint16_t r4 = (uint16_t)((rr * 15 + 127) / 255);
                uint16_t g4 = (uint16_t)((gg * 15 + 127) / 255);
                uint16_t b4 = (uint16_t)((bb * 15 + 127) / 255);
                uint16_t out = ((a4 ^ 0xf) << 12) | (r4 << 8) | (g4 << 4) | b4;
                pixels[off] = out & 255;
                pixels[off + 1] = out >> 8;
            }
        }
        pen += cached->advance;
    }
}

bool FontsHost::init(Host* h, const std::string& data_dir) {
    host = h;
    std::string jp_path, chs_path;
#ifdef __SWITCH__
    // Prefer the user's own font next to the game data (most faithful),
    // else the Switch shared system fonts (include Japanese; simplified
    // Chinese coverage comes from the dedicated shared font).
    std::string msgothic = data_dir + "/msgothic.ttc";
    FILE* probe = fopen(msgothic.c_str(), "rb");
    if (probe) {
        fclose(probe);
        jp_path = chs_path = msgothic;
    }
#else
    const char* env = getenv("TH10_FONT");
    if (env && *env) jp_path = chs_path = env;
    if (jp_path.empty()) {
        const char* candidates[] = {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
            "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
            "/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        };
        for (const char* c : candidates) {
            FILE* f = fopen(c, "rb");
            if (f) {
                fclose(f);
                jp_path = chs_path = c;
                break;
            }
        }
    }
#endif
    auto load = [&](const std::string& path) -> std::unique_ptr<FtFace> {
        std::unique_ptr<FtFace> f(new FtFace());
        if (FT_Init_FreeType(&f->lib)) return nullptr;
        if (!path.empty() && !FT_New_Face(f->lib, path.c_str(), 0, &f->face))
            return f;
        return nullptr;
    };
    jp_ = load(jp_path);
    chs_ = (chs_path == jp_path) ? load(chs_path) : load(chs_path);
#ifdef __SWITCH__
    if ((!jp_ || !chs_) && jp_path.empty()) {
        // Switch shared system fonts: always present on Horizon. The
        // standard font covers Japanese; simplified Chinese has its own.
        // Shared memory stays mapped for the process lifetime, so the
        // memory faces stay valid; the pl session is kept open likewise.
        static bool pl_tried = false;
        static PlFontData pl_jp{0}, pl_chs{0};
        if (!pl_tried) {
            pl_tried = true;
            if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
                if (R_FAILED(plGetSharedFontByType(
                        &pl_jp, PlSharedFontType_Standard)))
                    pl_jp.address = nullptr;
                if (R_FAILED(plGetSharedFontByType(
                        &pl_chs, PlSharedFontType_ChineseSimplified)))
                    pl_chs.address = nullptr;
            }
        }
        auto loadmem = [&](PlFontData& pf) -> std::unique_ptr<FtFace> {
            std::unique_ptr<FtFace> f(new FtFace());
            if (!pf.address || !pf.size) return nullptr;
            if (FT_Init_FreeType(&f->lib)) return nullptr;
            if (!FT_New_Memory_Face(f->lib, (const FT_Byte*)pf.address,
                                    (FT_Long)pf.size, 0, &f->face))
                return f;
            return nullptr;
        };
        if (!jp_) jp_ = loadmem(pl_jp);
        if (!chs_) chs_ = loadmem(pl_chs);
        if ((!chs_ || !chs_->face) && jp_ && jp_->face)
            chs_ = loadmem(pl_jp);  // CHS falls back to the JP face
        host_log("fonts: switch shared fonts (%s)",
                 (jp_ && jp_->face) ? "ok" : "FAILED");
    }
#endif
    if (!jp_ || !jp_->face) {
        host_log("fonts: no usable font file found");
        return false;
    }
    if (!chs_ || !chs_->face) chs_ = std::move(load(jp_path));
    host_log("fonts: %s", jp_path.c_str());
    return true;
}

void FontsHost::shutdown() {
    auto drop = [](std::unique_ptr<FtFace>& f) {
        if (f) {
            if (f->face) FT_Done_Face(f->face);
            if (f->lib) FT_Done_Library(f->lib);
            f.reset();
        }
    };
    drop(jp_);
    drop(chs_);
    objects.clear();
}

uint32_t FontsHost::create_bitmap(uint32_t description, uint32_t out) {
    w2c_th100x2Dgame* w = host->wasm;
    int32_t width = mem_i32(w, description + 4);
    int32_t h = mem_i32(w, description + 8);
    uint32_t height = h < 0 ? (uint32_t)-h : (uint32_t)h;
    uint8_t* p = mem_bytes(w, description + 14, 2);
    uint32_t bpp = p[0] | (p[1] << 8);
    if (width <= 0 || height == 0) return 0;
    uint64_t pitch = (((uint64_t)width * bpp + 31) >> 5) * 4;
    uint64_t size = pitch * height;
    if (size == 0 || size > 64ull * 1024 * 1024) return 0;
    uint32_t data = w2c_th100x2Dgame_graphics_allocate(w, (uint32_t)size +
                                                              (uint32_t)pitch *
                                                                  4);
    if (!data) return 0;
    mem_wu32(w, out, data);
    auto o = std::unique_ptr<Object>(new Object());
    o->handle = next_handle++;
    o->type = "bitmap";
    o->width = (uint32_t)width;
    o->height = height;
    o->bpp = bpp;
    o->pitch = (uint32_t)pitch;
    o->size = (uint32_t)size;
    o->data = data;
    uint32_t handle = o->handle;
    objects[handle] = std::move(o);
    return handle;
}

uint32_t FontsHost::create_context() {
    auto o = std::unique_ptr<Object>(new Object());
    o->handle = next_handle++;
    o->type = "context";
    o->mode = 2;
    o->color = 0;
    uint32_t handle = o->handle;
    objects[handle] = std::move(o);
    return handle;
}

uint32_t FontsHost::select(uint32_t context, uint32_t handle) {
    auto dcit = objects.find(context);
    HOST_ASSERT(dcit != objects.end() && dcit->second->type == "context",
                "invalid font context %u", context);
    Object* dc = dcit->second.get();
    auto oit = objects.find(handle);
    if (oit == objects.end()) return 0;
    Object* obj = oit->second.get();
    Object** slot = obj->type == "font" ? &dc->font : &dc->bitmap;
    Object* old = *slot;
    *slot = obj;
    return old ? old->handle : 0;
}

void FontsHost::delete_context(uint32_t handle) {
    auto it = objects.find(handle);
    HOST_ASSERT(it != objects.end() && it->second->type == "context",
                "invalid font context %u", handle);
    objects.erase(it);
}

void FontsHost::delete_object(uint32_t handle) {
    auto it = objects.find(handle);
    HOST_ASSERT(it != objects.end(), "invalid font object %u", handle);
    if (it->second->data)
        w2c_th100x2Dgame_graphics_free(host->wasm, it->second->data);
    objects.erase(it);
}

uint32_t FontsHost::create_font(int32_t height, const std::string& face,
                                uint32_t charset) {
    auto o = std::unique_ptr<Object>(new Object());
    o->handle = next_handle++;
    o->type = "font";
    o->font_height = height;
    o->charset = charset;
    o->face = face;
    uint32_t handle = o->handle;
    objects[handle] = std::move(o);
    return handle;
}

void FontsHost::background(uint32_t context, uint32_t mode) {
    auto it = objects.find(context);
    HOST_ASSERT(it != objects.end() && it->second->type == "context",
                "invalid font context %u", context);
    it->second->mode = mode;
}

void FontsHost::color(uint32_t context, uint32_t color) {
    auto it = objects.find(context);
    HOST_ASSERT(it != objects.end() && it->second->type == "context",
                "invalid font context %u", context);
    it->second->color = color;
}

void FontsHost::text(uint32_t context, int32_t x, int32_t y, uint32_t pointer,
                     uint32_t length) {
    auto dcit = objects.find(context);
    HOST_ASSERT(dcit != objects.end() && dcit->second->type == "context",
                "invalid font context %u", context);
    Object* dc = dcit->second.get();
    HOST_ASSERT(dc->bitmap && dc->font, "font context %u not ready", context);
    bool gbk = dc->font->charset == 134;
    uint8_t* bytes = mem_bytes(host->wasm, pointer, length);
    std::vector<uint32_t> chars =
        gbk ? decode936(bytes, length) : decode932(bytes, length);
    draw_text(dc, dc->bitmap, x, y, chars);
}

extern "C" {
uint32_t w2c_th10__fonts_bitmap(struct w2c_th10__fonts* fonts,
                                uint32_t description, uint32_t out) {
    return fonts->host->fonts->create_bitmap(description, out);
}
uint32_t w2c_th10__fonts_context(struct w2c_th10__fonts* fonts) {
    return fonts->host->fonts->create_context();
}
uint32_t w2c_th10__fonts_select(struct w2c_th10__fonts* fonts, uint32_t context,
                                uint32_t handle) {
    return fonts->host->fonts->select(context, handle);
}
void w2c_th10__fonts_delete_context(struct w2c_th10__fonts* fonts,
                                    uint32_t handle) {
    fonts->host->fonts->delete_context(handle);
}
void w2c_th10__fonts_delete_object(struct w2c_th10__fonts* fonts,
                                   uint32_t handle) {
    fonts->host->fonts->delete_object(handle);
}
uint32_t w2c_th10__fonts_font(struct w2c_th10__fonts* fonts, uint32_t height,
                              uint32_t face, uint32_t charset) {
    return fonts->host->fonts->create_font((int32_t)height,
                                           mem_cstr(fonts->host->wasm, face),
                                           charset);
}
void w2c_th10__fonts_background(struct w2c_th10__fonts* fonts, uint32_t context,
                                uint32_t mode) {
    fonts->host->fonts->background(context, mode);
}
void w2c_th10__fonts_color(struct w2c_th10__fonts* fonts, uint32_t context,
                           uint32_t color) {
    fonts->host->fonts->color(context, color);
}
void w2c_th10__fonts_text(struct w2c_th10__fonts* fonts, uint32_t context,
                          uint32_t x, uint32_t y, uint32_t pointer,
                          uint32_t length) {
    fonts->host->fonts->text(context, (int32_t)x, (int32_t)y, pointer, length);
}
}  // extern "C"
