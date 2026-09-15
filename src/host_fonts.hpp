// C++ port of site/runtime/native-fonts.mjs with a FreeType backend
// replacing the baked GDI tables (38-120MB, unsuitable for Switch).
// Pixel protocol: the atlas is mid-double-invert when text() runs
// (stored alpha nibble is inverted); we un-invert, composite the glyph
// "over" the current pixel, and re-invert for storage.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
struct Host;

// WHATWG TextDecoder('shift_jis'/'gbk') + the encoding.mjs fallbacks.
std::vector<uint32_t> decode932(const uint8_t* bytes, uint32_t length);
std::vector<uint32_t> decode936(const uint8_t* bytes, uint32_t length);

struct FontsHost {
    FontsHost();
    ~FontsHost();
    Host* host = nullptr;
    struct Object {
        uint32_t handle = 0;
        std::string type;  // bitmap / context / font
        // bitmap
        uint32_t width = 0, height = 0, bpp = 0, pitch = 0, size = 0, data = 0;
        // context
        uint32_t mode = 2, color = 0;
        Object* bitmap = nullptr;
        Object* font = nullptr;
        // font
        int32_t font_height = 0;
        uint32_t charset = 0;
        std::string face;
    };
    std::map<uint32_t, std::unique_ptr<Object>> objects;
    uint32_t next_handle = 1;

    bool init(Host* h, const std::string& data_dir);
    void shutdown();

    uint32_t create_bitmap(uint32_t description, uint32_t out);
    uint32_t create_context();
    uint32_t select(uint32_t context, uint32_t handle);
    void delete_context(uint32_t handle);
    void delete_object(uint32_t handle);
    uint32_t create_font(int32_t height, const std::string& face,
                         uint32_t charset);
    void background(uint32_t context, uint32_t mode);
    void color(uint32_t context, uint32_t color);
    void text(uint32_t context, int32_t x, int32_t y, uint32_t pointer,
              uint32_t length);

  private:
    struct FtFace;
    std::unique_ptr<FtFace> jp_, chs_;
    bool ensure_face(bool chs, int height);
    void draw_text(Object* dc, Object* bitmap, int x, int y,
                   const std::vector<uint32_t>& chars);
};
