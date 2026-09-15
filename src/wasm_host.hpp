// Shared host context for the TH10 wasm2c port (Linux test + Nintendo Switch).
// Faithful C++ ports of th10/site/runtime/*.mjs. All WASM linear-memory
// accesses go through the helpers below; the data pointer is NEVER cached
// across calls because graphics_allocate may grow the memory.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../wasm2c/th10_wasm.h"

void host_log(const char* fmt, ...);

#define HOST_ASSERT(cond, ...)                                         \
    do {                                                               \
        if (!(cond)) {                                                 \
            host_log("ASSERT %s:%d: ", __FILE__, __LINE__);             \
            host_log(__VA_ARGS__);                                     \
            host_log("\n");                                            \
            fflush(stdout);                                            \
            abort();                                                   \
        }                                                              \
    } while (0)

// ---- WASM linear memory helpers (re-read data/size on every call) ----
inline uint8_t* mem_bytes(w2c_th100x2Dgame* w, uint32_t addr, uint32_t len) {
#ifdef NDEBUG
    (void)len;
    return &w->w2c_memory.data[addr];
#else
    HOST_ASSERT((uint64_t)addr + (uint64_t)len <= (uint64_t)w->w2c_memory.size,
                "OOB memory access addr=%u len=%u size=%u", addr, len,
                (unsigned)w->w2c_memory.size);
    return &w->w2c_memory.data[addr];
#endif
}
inline uint32_t mem_u32(w2c_th100x2Dgame* w, uint32_t addr) {
    uint32_t v;
    memcpy(&v, mem_bytes(w, addr, 4), 4);
    return v;
}
inline int32_t mem_i32(w2c_th100x2Dgame* w, uint32_t addr) {
    int32_t v;
    memcpy(&v, mem_bytes(w, addr, 4), 4);
    return v;
}
inline float mem_f32(w2c_th100x2Dgame* w, uint32_t addr) {
    float v;
    memcpy(&v, mem_bytes(w, addr, 4), 4);
    return v;
}
inline void mem_wu32(w2c_th100x2Dgame* w, uint32_t addr, uint32_t v) {
    memcpy(mem_bytes(w, addr, 4), &v, 4);
}
inline void mem_wi32(w2c_th100x2Dgame* w, uint32_t addr, int32_t v) {
    memcpy(mem_bytes(w, addr, 4), &v, 4);
}
inline void mem_wf32(w2c_th100x2Dgame* w, uint32_t addr, float v) {
    memcpy(mem_bytes(w, addr, 4), &v, 4);
}
inline std::string mem_cstr(w2c_th100x2Dgame* w, uint32_t addr) {
    uint32_t size = w->w2c_memory.size;
    uint32_t end = addr;
    while (end < size && w->w2c_memory.data[end]) end++;
    HOST_ASSERT(end < size, "unterminated string at %u", addr);
    return std::string((const char*)&w->w2c_memory.data[addr], end - addr);
}

// Forward declarations of subsystem contexts (defined in their headers).
struct FilesHost;
struct GraphicsHost;
struct AudioHost;
struct FontsHost;

struct Host {
    w2c_th100x2Dgame* wasm = nullptr;
    FilesHost* files = nullptr;
    GraphicsHost* graphics = nullptr;
    AudioHost* audio = nullptr;
    FontsHost* fonts = nullptr;
    // Save/output directory (Linux: ./th10_save, Switch: sdmc:/switch/th10).
    std::string save_dir;
    std::string data_dir;
    bool chinese = false;
};

// The wasm2c header only forward-declares these; the host defines them.
struct w2c_th10__files {
    Host* host = nullptr;
};
struct w2c_th10__graphics {
    Host* host = nullptr;
};
struct w2c_th10__fonts {
    Host* host = nullptr;
};
struct w2c_th10__audio {
    Host* host = nullptr;
};
struct w2c_th10__time {
    Host* host = nullptr;
};
