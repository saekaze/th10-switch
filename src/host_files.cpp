#include "host_files.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cctype>

#include "wasm_host.hpp"

namespace {

bool is_dir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void mkdir_parents(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); i++) {
        if (path[i] == '/') {
            if (!cur.empty() && !is_dir(cur)) mkdir(cur.c_str(), 0755);
        }
        cur += path[i];
    }
    if (!cur.empty() && !is_dir(cur)) mkdir(cur.c_str(), 0755);
}

// Manual case-insensitive wildcard match ('*' and '?'), like the JS RegExp.
bool wild_match(const char* pattern, const char* str) {
    const char *star = nullptr, *back = nullptr;
    while (*str) {
        if (*pattern == '*') {
            star = pattern++;
            back = str;
        } else if (*pattern == '?' ||
                   tolower((unsigned char)*pattern) ==
                       tolower((unsigned char)*str)) {
            pattern++;
            str++;
        } else if (star) {
            pattern = star + 1;
            str = ++back;
        } else {
            return false;
        }
    }
    while (*pattern == '*') pattern++;
    return *pattern == '\0';
}

}  // namespace

bool files_normalize(const std::string& in, std::string& out) {
    std::vector<std::string> parts;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty() || cur == ".") {
        } else if (cur == "..") {
            if (parts.empty()) return false;
            parts.pop_back();
        } else {
            for (char& c : cur) c = tolower((unsigned char)c);
            parts.push_back(cur);
        }
        cur.clear();
        return true;
    };
    for (char c : in) {
        if (c == '/' || c == '\\') {
            if (!flush()) return false;
        } else {
            cur += c;
        }
    }
    if (!flush()) return false;
    out.clear();
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) out += '/';
        out += parts[i];
    }
    return true;
}

bool FilesHost::init(Host* h, const std::string& data_dir,
                     const std::string& save_dir, bool chinese) {
    host = h;
    mkdir_parents(save_dir);
    std::string base = chinese ? "th10c.dat" : "th10.dat";
    archive_name = base;  // already normalized
    std::string apath = data_dir + "/" + base;
    FILE* f = fopen(apath.c_str(), "rb");
    if (!f) {
        host_log("files: cannot open archive %s", apath.c_str());
        return false;
    }
    fseek(f, 0, SEEK_END);
    long alen = ftell(f);
    fseek(f, 0, SEEK_SET);
    archive.resize(alen > 0 ? (size_t)alen : 0);
    if (!archive.empty() &&
        fread(archive.data(), 1, archive.size(), f) != archive.size()) {
        host_log("files: short read on %s", apath.c_str());
        fclose(f);
        return false;
    }
    fclose(f);
    host_log("files: archive %s (%u bytes)", apath.c_str(),
             (unsigned)archive.size());
    std::string mpath = data_dir + "/thbgm.dat";
    music = fopen(mpath.c_str(), "rb");
    if (music) {
        fseek(music, 0, SEEK_END);
        long mlen = ftell(music);
        music_len = mlen > 0 ? (uint64_t)mlen : 0;
        fseek(music, 0, SEEK_SET);
        music_silent = false;
        host_log("files: music %s (%llu bytes)", mpath.c_str(),
                 (unsigned long long)music_len);
    } else {
        music_silent = true;
        music_len = 403789620;  // manifest.music size; reads zero-fill
        host_log("files: no thbgm.dat, music will be silent");
    }
    scan_disk(save_dir);
    return true;
}

void FilesHost::shutdown() {
    if (music) fclose(music);
    music = nullptr;
}

void FilesHost::scan_disk(const std::string& save_dir) {
    // Recursive scan with POSIX dirent (works on Linux and Switch newlib).
    std::vector<std::string> stack{""};
    while (!stack.empty()) {
        std::string rel = stack.back();
        stack.pop_back();
        std::string full = rel.empty() ? save_dir : save_dir + "/" + rel;
        DIR* dir = opendir(full.c_str());
        if (!dir) continue;
        while (dirent* e = readdir(dir)) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            std::string r = rel.empty() ? n : rel + "/" + n;
            std::string norm;
            if (!files_normalize(r, norm)) continue;
            bool dir_entry = false;
            if (e->d_type == DT_DIR)
                dir_entry = true;
            else if (e->d_type == DT_UNKNOWN)
                dir_entry = is_dir(full + "/" + n);
            if (dir_entry)
                stack.push_back(r);
            else
                disk_names.insert(norm);
        }
        closedir(dir);
    }
    host_log("files: %u pre-existing save files", (unsigned)disk_names.size());
}

bool FilesHost::resolve(const Handle& h, const uint8_t** data, uint64_t* len,
                        bool* is_music) {
    auto ov = overlay.find(h.name);
    if (ov != overlay.end()) {
        *data = ov->second.data();
        *len = ov->second.size();
        *is_music = false;
        return true;
    }
    if (h.name == archive_name) {
        *data = archive.data();
        *len = archive.size();
        *is_music = false;
        return true;
    }
    if (h.name == "thbgm.dat") {
        *data = nullptr;
        *len = music_len;
        *is_music = true;
        return true;
    }
    return false;
}

uint32_t FilesHost::open(const std::string& name, uint32_t writing) {
    std::string n;
    if (!files_normalize(name, n)) return 0xffffffffu;
    if (writing) {
        overlay[n] = std::vector<uint8_t>();
    } else {
        if (overlay.find(n) == overlay.end() && n != archive_name &&
            n != "thbgm.dat") {
            // Load on-disk saves on demand (replays/configs are small).
            if (disk_names.find(n) == disk_names.end()) return 0xffffffffu;
            std::string path = host->save_dir + "/" + n;
            FILE* f = fopen(path.c_str(), "rb");
            if (!f) return 0xffffffffu;
            fseek(f, 0, SEEK_END);
            long flen = ftell(f);
            fseek(f, 0, SEEK_SET);
            std::vector<uint8_t> bytes(flen > 0 ? (size_t)flen : 0);
            bool ok = bytes.empty() ||
                      fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
            fclose(f);
            if (!ok) return 0xffffffffu;
            overlay[n] = std::move(bytes);
        }
    }
    uint32_t id = next_handle++;
    handles[id] = Handle{n, 0, writing != 0};
    return id;
}

void FilesHost::close(uint32_t id) {
    auto it = handles.find(id);
    if (it == handles.end()) return;
    Handle h = it->second;
    handles.erase(it);
    if (h.writing) flush_to_disk(h.name);
}

void FilesHost::flush_to_disk(const std::string& name) {
    auto it = overlay.find(name);
    if (it == overlay.end()) return;
    std::string path = host->save_dir + "/" + name;
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) mkdir_parents(path.substr(0, slash));
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        host_log("files: cannot write %s", path.c_str());
        return;
    }
    if (!it->second.empty())
        fwrite(it->second.data(), 1, it->second.size(), f);
    fclose(f);
    disk_names.insert(name);
}

uint32_t FilesHost::size(uint32_t id) {
    auto it = handles.find(id);
    if (it == handles.end()) return 0xffffffffu;
    const uint8_t* data;
    uint64_t len;
    bool is_music;
    if (!resolve(it->second, &data, &len, &is_music)) return 0xffffffffu;
    return (uint32_t)len;
}

uint32_t FilesHost::seek(uint32_t id, uint32_t offset, uint32_t origin) {
    auto it = handles.find(id);
    if (it == handles.end() || origin > 2) return 0xffffffffu;
    const uint8_t* data;
    uint64_t len;
    bool is_music;
    if (!resolve(it->second, &data, &len, &is_music)) return 0xffffffffu;
    int64_t base = origin == 1 ? (int64_t)it->second.cursor
                               : origin == 2 ? (int64_t)len : 0;
    int64_t pos = base + (int64_t)(int32_t)offset;
    if (pos < 0 || pos > 0xffffffffll) return 0xffffffffu;
    it->second.cursor = (uint64_t)pos;
    return (uint32_t)pos;
}

uint32_t FilesHost::read(uint32_t id, uint32_t pointer, uint32_t length) {
    auto it = handles.find(id);
    if (it == handles.end()) return 0;
    const uint8_t* data;
    uint64_t len;
    bool is_music;
    if (!resolve(it->second, &data, &len, &is_music)) return 0;
    uint64_t cursor = it->second.cursor;
    uint64_t avail = cursor < len ? len - cursor : 0;
    uint64_t count = length < avail ? length : avail;
    if (count == 0) return 0;
    uint8_t* out = mem_bytes(host->wasm, pointer, (uint32_t)count);
    if (!is_music) {
        memcpy(out, data + cursor, (size_t)count);
    } else if (music_silent || !music) {
        memset(out, 0, (size_t)count);
    } else {
        if (fseek(music, (long)cursor, SEEK_SET) != 0) return 0;
        size_t got = fread(out, 1, (size_t)count, music);
        if (got < count) memset(out + got, 0, (size_t)count - got);
    }
    it->second.cursor = cursor + count;
    return (uint32_t)count;
}

uint32_t FilesHost::write(uint32_t id, uint32_t pointer, uint32_t length) {
    auto it = handles.find(id);
    if (it == handles.end() || !it->second.writing) return 0;
    uint64_t end = it->second.cursor + length;
    if (end > 0xffffffffull) return 0;
    std::vector<uint8_t>& bytes = overlay[it->second.name];
    if (bytes.size() < end) bytes.resize((size_t)end);  // zero-filled gap
    if (length)
        memcpy(bytes.data() + it->second.cursor,
               mem_bytes(host->wasm, pointer, length), length);
    it->second.cursor = end;
    return length;
}

uint32_t FilesHost::list(const std::string& directory,
                         const std::string& pattern, uint32_t index,
                         uint32_t pointer, uint32_t capacity) {
    std::string folder;
    if (!files_normalize(directory, folder)) return 0;
    std::string prefix = folder.empty() ? "" : folder + "/";
    std::string pat = pattern == "*.*" ? "*" : pattern;
    std::set<std::string> names;
    for (const auto& kv : overlay) names.insert(kv.first);
    for (const auto& n : disk_names) names.insert(n);
    names.insert(archive_name);
    names.insert("thbgm.dat");
    std::vector<std::string> match;
    for (const auto& n : names) {
        if (n.size() < prefix.size() ||
            n.compare(0, prefix.size(), prefix) != 0)
            continue;
        std::string rest = n.substr(prefix.size());
        if (rest.find('/') != std::string::npos) continue;
        if (!wild_match(pat.c_str(), rest.c_str())) continue;
        match.push_back(rest);
    }
    std::sort(match.begin(), match.end());
    if (index >= match.size()) return 0;
    const std::string& name = match[index];
    if (name.size() + 1 > capacity) return 0;
    uint8_t* out = mem_bytes(host->wasm, pointer, (uint32_t)name.size() + 1);
    memcpy(out, name.data(), name.size());
    out[name.size()] = 0;
    return 1;
}

// ---- wasm2c imports ----
extern "C" {

uint32_t w2c_th10__files_open(struct w2c_th10__files* files, uint32_t name,
                              uint32_t writing) {
    return files->host->files->open(mem_cstr(files->host->wasm, name), writing);
}
void w2c_th10__files_close(struct w2c_th10__files* files, uint32_t id) {
    files->host->files->close(id);
}
uint32_t w2c_th10__files_size(struct w2c_th10__files* files, uint32_t id) {
    return files->host->files->size(id);
}
uint32_t w2c_th10__files_seek(struct w2c_th10__files* files, uint32_t id,
                              uint32_t offset, uint32_t origin) {
    return files->host->files->seek(id, offset, origin);
}
uint32_t w2c_th10__files_read(struct w2c_th10__files* files, uint32_t id,
                              uint32_t pointer, uint32_t length) {
    return files->host->files->read(id, pointer, length);
}
uint32_t w2c_th10__files_write(struct w2c_th10__files* files, uint32_t id,
                               uint32_t pointer, uint32_t length) {
    return files->host->files->write(id, pointer, length);
}
uint32_t w2c_th10__files_list(struct w2c_th10__files* files, uint32_t directory,
                              uint32_t pattern, uint32_t index,
                              uint32_t pointer, uint32_t capacity) {
    w2c_th100x2Dgame* w = files->host->wasm;
    return files->host->files->list(mem_cstr(w, directory),
                                    mem_cstr(w, pattern), index, pointer,
                                    capacity);
}

}  // extern "C"
