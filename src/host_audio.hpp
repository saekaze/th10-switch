// C++ port of site/runtime/dsound.mjs (DirectSound8) + native-audio.mjs
// dispatch + the audio-bridge event feed (in-process, bytes copied).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
struct Host;
class AudioMixer;

struct SoundStorage {
    uint32_t data = 0;  // WASM pointer to PCM bytes
    uint32_t version = 0;
};

struct SoundObject {
    uint32_t address = 0;
    std::string kind;  // IDirectSound8 / IDirectSoundBuffer / IDirectSoundNotify
    int refs = 1;
    SoundObject* ref_owner = nullptr;
    // Buffer state.
    uint32_t flags = 0, length = 0;
    std::shared_ptr<SoundStorage> storage;
    std::vector<uint8_t> format;  // 18-byte WAVEFORMATEX
    bool playing = false, loop = false;
    uint32_t cursor = 0, frequency = 44100;
    int32_t volume = 0, pan = 0;
    double started = 0;
    uint32_t last_cursor = 0;
    bool has_last = false;
    struct Notif {
        uint32_t offset, event;
    };
    std::vector<Notif> notifications;
    uint32_t notify_obj = 0;
    SoundObject* parent = nullptr;  // notify -> buffer
};

struct AudioHost {
    Host* host = nullptr;
    AudioMixer* mixer = nullptr;
    std::map<uint32_t, std::unique_ptr<SoundObject>> objects;
    uint32_t next_handle = 1;
    double millis = 1000.0;
    std::map<uint32_t, bool> events;  // id -> signaled
    uint32_t next_event = 1;
    std::map<uint32_t, uint32_t> uploaded;  // buffer addr -> mixer version

    uint32_t alloc(uint32_t length);
    void free(uint32_t pointer);

    SoundObject* create_object(const std::string& kind);
    SoundObject* get(uint32_t handle);
    uint32_t add_ref_value(uint32_t handle);
    uint32_t release_value(uint32_t handle);

    uint32_t create();
    uint32_t call(uint32_t handle, uint32_t operation, uint32_t pointer);
    uint32_t event(uint32_t operation, uint32_t handle);
    void advance(uint32_t milliseconds);
    void sync();  // clock sync to mixer (after each step)

    uint32_t position(SoundObject* b);
    SoundObject* buffer(uint32_t flags, uint32_t length,
                        const std::vector<uint8_t>* format,
                        std::shared_ptr<SoundStorage> storage);
    void destroy_buffer(SoundObject* b);
    struct Range {
        int64_t offset;
        uint32_t length;
    };
    void send(SoundObject* b, const std::vector<Range>* ranges);
};
