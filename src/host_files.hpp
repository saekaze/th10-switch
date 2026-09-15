// C++ port of site/runtime/native-file-store.mjs + real disk I/O.
// Layout mirrors the original PC game: archives + saves side by side.
// Switch: sdmc:/switch/th10/. Linux test: DATs in data_dir, saves in save_dir.
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

struct Host;

bool files_normalize(const std::string& in, std::string& out);

struct FilesHost {
    Host* host = nullptr;
    // Read-only base files.
    std::vector<uint8_t> archive;
    std::string archive_name;  // normalized ("th10.dat" / "th10c.dat")
    FILE* music = nullptr;     // streaming thbgm.dat (384MB, never fully loaded)
    uint64_t music_len = 0;
    bool music_silent = true;  // missing thbgm.dat -> zero-fill (silent play)
    // Written files (kept resident + flushed to disk on close).
    std::map<std::string, std::vector<uint8_t>> overlay;
    // Names visible to list() that live on disk but are not resident.
    std::set<std::string> disk_names;
    struct Handle {
        std::string name;
        uint64_t cursor = 0;
        bool writing = false;
    };
    std::map<uint32_t, Handle> handles;
    uint32_t next_handle = 1;

    bool init(Host* h, const std::string& data_dir, const std::string& save_dir,
              bool chinese);
    void shutdown();

    uint32_t open(const std::string& name, uint32_t writing);
    void close(uint32_t id);
    uint32_t size(uint32_t id);
    uint32_t seek(uint32_t id, uint32_t offset, uint32_t origin);
    uint32_t read(uint32_t id, uint32_t pointer, uint32_t length);
    uint32_t write(uint32_t id, uint32_t pointer, uint32_t length);
    uint32_t list(const std::string& directory, const std::string& pattern,
                  uint32_t index, uint32_t pointer, uint32_t capacity);

  private:
    // Resolves a read handle to (memory bytes) or (music stream/silent).
    // Returns false when the file does not exist.
    bool resolve(const Handle& h, const uint8_t** data, uint64_t* len,
                 bool* is_music);
    void flush_to_disk(const std::string& name);
    void scan_disk(const std::string& save_dir);
};
