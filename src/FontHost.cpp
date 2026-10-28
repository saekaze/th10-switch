// Switch port of th10_web/cpp/sdl/FontHost.cpp: the game's GDI font imports
// (CreateFont / SelectObject / SetTextColor / TextOut into a 16-bit DIB) on
// FreeType instead of SDL3_ttf. Text is decoded with the CP932 / GBK tables
// from th10-switch, glyphs come from msgothic.ttc (or the Switch's built-in
// font) and are cached per size, and pixels are blended exactly like
// upstream: TextOut writes antialiased RGB into the low 12 bits of the
// A4R4G4B4 target and clears the alpha nibble, which TextRaster::draw()
// flips afterwards.
#include "Platform.hpp"
#include "../platform/Fonts.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef __SWITCH__
#include <switch.h>
#endif
#include "cp932_table.hpp"
#include "cp936_table.hpp"
using th10::u32;using th10::i32;using th10::u8;using th10::u16;using th10::u64;

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

static std::vector<uint32_t> decode932(const uint8_t* bytes, uint32_t length) {
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

static std::vector<uint32_t> decode936(const uint8_t* bytes, uint32_t length) {
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

namespace {
struct Face {
    FT_Face face=nullptr;i32 size=-1;std::vector<u8> owned;
    struct Glyph {i32 left=0,top=0,width=0,rows=0,advance=0;std::vector<u8> coverage;bool ok=false;};
    std::unordered_map<u64,Glyph> glyphs;
};
FT_Library library=nullptr;
std::unique_ptr<Face> faces[2]; // 0 = Japanese, 1 = Simplified Chinese
bool initialized=false;
struct Object {
    enum Kind{Bitmap,Context,Font} kind;u32 font=0,bitmap=0,color=0,mode=2,charset=0;
    i32 width=0,height=0,pitch=0,bpp=0;std::vector<u8> pixels;
};
std::map<u32,std::unique_ptr<Object>> objects;u32 next=1,failures=0;
Object& get(u32 id){auto it=objects.find(id);if(it==objects.end())std::abort();return *it->second;}
u32 add(Object::Kind kind){const auto id=next++;auto value=std::make_unique<Object>();value->kind=kind;objects[id]=std::move(value);return id;}

std::unique_ptr<Face> open_file(const std::string& path){
    auto f=std::make_unique<Face>();if(FT_New_Face(library,path.c_str(),0,&f->face))return nullptr;return f;
}
#ifdef __SWITCH__
std::unique_ptr<Face> open_shared(PlSharedFontType type){
    static bool pl=false;if(!pl)pl=R_SUCCEEDED(plInitialize(PlServiceType_User));if(!pl)return nullptr;
    PlFontData font{};if(R_FAILED(plGetSharedFontByType(&font,type))||!font.address)return nullptr;
    auto f=std::make_unique<Face>();if(FT_New_Memory_Face(library,static_cast<const FT_Byte*>(font.address),FT_Long(font.size),0,&f->face))return nullptr;return f;
}
#endif
bool initialize(){
    if(initialized)return faces[0]!=nullptr;initialized=true;
    if(FT_Init_FreeType(&library)){th10::host::log("fonts: FreeType initialisation failed");return false;}
    const auto gothic=th10::host::paths().data_file("msgothic.ttc");
    if(th10::host::exists(gothic)){faces[0]=open_file(gothic);if(faces[0])th10::host::log("fonts: %s",gothic.c_str());}
#ifdef __SWITCH__
    if(!faces[0]){faces[0]=open_shared(PlSharedFontType_Standard);th10::host::log("fonts: Switch shared font (%s)",faces[0]?"ok":"FAILED");}
    faces[1]=open_shared(PlSharedFontType_ChineseSimplified);
#else
    if(!faces[0])for(const char* c:{"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc","/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc"})if(th10::host::exists(c)&&(faces[0]=open_file(c)))break;
#endif
    if(!faces[1]&&faces[0]){faces[1]=th10::host::exists(gothic)?open_file(gothic):nullptr;}
    if(!faces[0]){th10::host::log("fonts: no usable font found");return false;}
    if(!faces[1])faces[1]=open_file(th10::host::paths().data_file("msgothic.ttc"));
    return true;
}
Face* face_for(u32 charset){return charset==134&&faces[1]?faces[1].get():faces[0].get();}
const Face::Glyph* glyph(Face& f,i32 height,u32 code){
    const u64 key=(u64(u32(height))<<32)|code;auto it=f.glyphs.find(key);if(it!=f.glyphs.end())return &it->second;
    if(f.size!=height){if(FT_Set_Pixel_Sizes(f.face,0,FT_UInt(height)))return nullptr;f.size=height;}
    Face::Glyph g;
    if(!FT_Load_Char(f.face,FT_ULong(code),FT_LOAD_RENDER|FT_LOAD_TARGET_NORMAL)){
        const auto* s=f.face->glyph;g.left=s->bitmap_left;g.top=s->bitmap_top;g.width=i32(s->bitmap.width);g.rows=i32(s->bitmap.rows);
        g.advance=i32((s->advance.x+32)>>6);g.coverage.resize(size_t(g.width)*g.rows);
        for(i32 y=0;y<g.rows;++y)std::memcpy(g.coverage.data()+size_t(y)*g.width,s->bitmap.buffer+y*s->bitmap.pitch,size_t(g.width));g.ok=true;
    }else{g.advance=height/2;}
    if(f.glyphs.size()>=8192)f.glyphs.clear();
    return &f.glyphs.emplace(key,std::move(g)).first->second;
}
u32 blend_channel_4444(u32 before,u32 target,u32 coverage){
    const u32 source=(target*15+127)/255;
    return (before*(255-coverage)+source*coverage+127)/255;
}
}

extern "C" {
u32 fonts_bitmap(const th10::BitmapDescription* d,u8** out){
    if(!d)return 0;const i32 width=d->width,height=std::abs(d->height);const u16 bpp=d->bits;
    if(width<=0||height<=0||bpp!=16||uint64_t(width)*height>16777216)return 0;
    const auto id=add(Object::Bitmap);auto& b=get(id);b.width=width;b.height=height;b.bpp=bpp;b.pitch=((width*bpp+31)>>5)*4;b.pixels.resize(size_t(b.pitch)*(height+4));*out=b.pixels.data();return id;
}
u32 fonts_context(){return add(Object::Context);}
u32 fonts_select(u32 context,u32 id){auto& dc=get(context);auto it=objects.find(id);if(it==objects.end())return 0;u32& target=it->second->kind==Object::Font?dc.font:dc.bitmap;const auto old=target;target=id;return old;}
void fonts_delete_context(u32 id){objects.erase(id);}
void fonts_delete_object(u32 id){objects.erase(id);}
u32 fonts_font(i32 height,const char*,u32 charset){
    if(!initialize())std::abort();const auto id=add(Object::Font);auto& f=get(id);f.charset=charset;f.height=height;return id;
}
void fonts_background(u32 id,u32 mode){get(id).mode=mode;}
void fonts_color(u32 id,u32 color){get(id).color=color;}
void fonts_text(u32 id,i32 x,i32 y,const char* bytes,u32 length){
    auto& dc=get(id);auto& b=get(dc.bitmap);auto& f=get(dc.font);auto* face=face_for(f.charset);if(!face){++failures;return;}
    const auto* data=reinterpret_cast<const u8*>(bytes);
    const auto chars=f.charset==134?decode936(data,length):decode932(data,length);if(chars.empty())return;
    const i32 height=f.height<0?-f.height:f.height?f.height:32;
    if(face->size!=height&&FT_Set_Pixel_Sizes(face->face,0,FT_UInt(height))){++failures;return;}face->size=height;
    const i32 ascent=i32((face->face->size->metrics.ascender+32)>>6);
    const u32 r=dc.color&255,g=(dc.color>>8)&255,blue=(dc.color>>16)&255;i32 pen=x;
    for(const u32 code:chars){
        const auto* gl=glyph(*face,height,code);if(!gl){++failures;continue;}
        if(gl->ok)for(i32 j=0;j<gl->rows;++j){const i32 row=y+ascent-gl->top+j;if(row<0||row>=b.height)continue;
            for(i32 i=0;i<gl->width;++i){const i32 column=pen+gl->left+i;if(column<0||column>=b.width)continue;const u32 coverage=gl->coverage[size_t(j)*gl->width+i];if(!coverage)continue;
                auto* target=b.pixels.data()+size_t(row)*b.pitch+column*2;const u32 before=target[0]|(u32(target[1])<<8);
                const u16 result=u16((blend_channel_4444((before>>8)&15,r,coverage)<<8)|(blend_channel_4444((before>>4)&15,g,coverage)<<4)|blend_channel_4444(before&15,blue,coverage));target[0]=u8(result);target[1]=u8(result>>8);
            }}
        pen+=gl->advance;
    }
}
u32 sdl_fonts_errors(){return failures;}
void sdl_fonts_shutdown(){objects.clear();for(auto& f:faces)if(f){if(f->face)FT_Done_Face(f->face);f.reset();}if(library)FT_Done_FreeType(library);library=nullptr;initialized=false;}
}
