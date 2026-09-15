#include "host_audio.hpp"

#include <cmath>
#include <cstring>

#include "audio_mixer.hpp"
#include "wasm_host.hpp"

namespace {
const int kAudioArgs[24] = {0, 0, 3, 2, 2, 1, 1, 0, 1, 1, 3, 0,
                            1, 2, 7, 4, 2, 2, 1, 1, 1, 1, 3, 1};
const uint8_t kDefaultFormat[18] = {1, 0, 2, 0, 0x44, 0xAC, 0, 0,
                                    0x10, 0xB1, 2, 0, 4, 0, 16, 0,
                                    0, 0};
uint16_t rd16(const std::vector<uint8_t>& f, int o) {
    return f[o] | (f[o + 1] << 8);
}
uint32_t rd32(const std::vector<uint8_t>& f, int o) {
    return f[o] | (f[o + 1] << 8) | (f[o + 2] << 16) | (f[o + 3] << 24);
}
}  // namespace

uint32_t AudioHost::alloc(uint32_t length) {
    uint32_t p = w2c_th100x2Dgame_graphics_allocate(host->wasm, length);
    HOST_ASSERT(p != 0, "audio allocate(%u) failed", length);
    return p;
}
void AudioHost::free(uint32_t pointer) {
    w2c_th100x2Dgame_graphics_free(host->wasm, pointer);
}

SoundObject* AudioHost::create_object(const std::string& kind) {
    auto o = std::unique_ptr<SoundObject>(new SoundObject());
    o->address = next_handle++;
    o->kind = kind;
    SoundObject* p = o.get();
    objects[p->address] = std::move(o);
    return p;
}
SoundObject* AudioHost::get(uint32_t handle) {
    auto it = objects.find(handle);
    HOST_ASSERT(it != objects.end(), "unknown audio handle %u", handle);
    return it->second.get();
}
uint32_t AudioHost::add_ref_value(uint32_t handle) {
    SoundObject* o = get(handle);
    SoundObject* t = o->ref_owner ? o->ref_owner : o;
    return (uint32_t)++t->refs;
}
uint32_t AudioHost::release_value(uint32_t handle) {
    SoundObject* o = get(handle);
    if (o->ref_owner) return release_value(o->ref_owner->address);
    HOST_ASSERT(o->refs > 0, "audio object already released");
    int refs = --o->refs;
    if (!refs) {
        if (o->kind == "IDirectSoundBuffer") destroy_buffer(o);
        objects.erase(o->address);
    }
    return (uint32_t)refs;
}

uint32_t AudioHost::create() {
    return create_object("IDirectSound8")->address;
}

SoundObject* AudioHost::buffer(uint32_t flags, uint32_t length,
                               const std::vector<uint8_t>* format,
                               std::shared_ptr<SoundStorage> storage) {
    if (!length) length = 4096;
    bool shared = (bool)storage;
    if (!storage) {
        storage = std::make_shared<SoundStorage>();
        storage->data = alloc(length);
    }
    SoundObject* b = create_object("IDirectSoundBuffer");
    b->flags = flags;
    b->length = length;
    b->storage = storage;
    if (format)
        b->format = *format;
    else
        b->format.assign(kDefaultFormat, kDefaultFormat + 18);
    b->playing = false;
    b->cursor = 0;
    b->volume = 0;
    b->pan = 0;
    b->frequency = rd32(b->format, 4);
    if (!b->frequency) b->frequency = 44100;
    if (!shared) {
        uint8_t fill = rd16(b->format, 14) == 8 ? 128 : 0;
        memset(mem_bytes(host->wasm, storage->data, length), fill, length);
    }
    return b;
}

void AudioHost::destroy_buffer(SoundObject* b) {
    b->playing = false;
    MixerEvent e;
    e.type = MixerEvent::Remove;
    e.id = b->address;
    e.time = millis;
    mixer->push(std::move(e));
    uploaded.erase(b->address);
    if (b->storage.use_count() == 1) free(b->storage->data);
    if (b->notify_obj) objects.erase(b->notify_obj);
}

uint32_t AudioHost::position(SoundObject* b) {
    if (!b->playing) return b->cursor;
    uint32_t align = rd16(b->format, 12);
    if (!align) align = 1;
    uint32_t rate = b->frequency ? b->frequency : rd32(b->format, 4);
    uint64_t pos = b->cursor + (uint64_t)floor((millis - b->started) * rate /
                                               1000.0) *
                                   align;
    if (b->loop) return (uint32_t)(pos % b->length);
    if (pos >= b->length) {
        b->playing = false;
        b->cursor = 0;
        return 0;
    }
    return (uint32_t)pos;
}

void AudioHost::advance(uint32_t milliseconds) {
    millis += milliseconds;
    for (auto& kv : objects) {
        SoundObject* b = kv.second.get();
        if (b->kind != "IDirectSoundBuffer" || !b->playing) continue;
        uint32_t pos = position(b);
        uint32_t last = b->has_last ? b->last_cursor : pos;
        for (const auto& n : b->notifications) {
            bool fire = (pos >= last && n.offset >= last && n.offset < pos) ||
                        (pos < last && (n.offset >= last || n.offset < pos));
            if (fire) {
                auto it = events.find(n.event);
                if (it != events.end()) it->second = true;
            }
        }
        b->last_cursor = pos;
        b->has_last = true;
    }
}

void AudioHost::send(SoundObject* b, const std::vector<Range>* ranges) {
    if (b->flags & 1) return;  // primary buffer is not mixed
    MixerEvent e;
    e.type = MixerEvent::Update;
    e.id = b->address;
    e.time = millis;
    e.playing = b->playing;
    e.loop = b->loop;
    e.cursor = position(b);
    e.rate = b->frequency;
    e.channels = rd16(b->format, 2);
    e.bits = rd16(b->format, 14);
    e.align = rd16(b->format, 12);
    e.volume = b->volume;
    e.pan = b->pan;
    HOST_ASSERT(e.align > 0 && e.channels > 0 && (e.bits == 8 || e.bits == 16),
                "bad wave format align=%u ch=%u bits=%u", e.align, e.channels,
                e.bits);
    w2c_th100x2Dgame* w = host->wasm;
    uint32_t version = b->storage->version;
    auto it = uploaded.find(b->address);
    uint32_t have = it != uploaded.end() ? it->second : 0xffffffffu;
    if (have != version) {
        bool use_patches = false;
        if (have == version - 1 && ranges && !ranges->empty()) {
            use_patches = true;
            for (const auto& r : *ranges)
                if (r.offset < 0 || r.length == 0 ||
                    (uint64_t)r.offset + r.length > b->length)
                    use_patches = false;
        }
        if (use_patches) {
            for (const auto& r : *ranges) {
                MixerEvent::Patch p;
                p.offset = (uint32_t)r.offset;
                uint8_t* src = mem_bytes(w, b->storage->data + p.offset,
                                         r.length);
                p.bytes.assign(src, src + r.length);
                e.patches.push_back(std::move(p));
            }
        } else {
            uint8_t* src = mem_bytes(w, b->storage->data, b->length);
            e.data.assign(src, src + b->length);
        }
        uploaded[b->address] = version;
    }
    mixer->push(std::move(e));
}

void AudioHost::sync() {
    MixerEvent e;
    e.type = MixerEvent::Clock;
    e.time = millis;
    mixer->push(std::move(e));
}

uint32_t AudioHost::event(uint32_t operation, uint32_t handle) {
    if (operation == 0) {
        uint32_t id = next_event++;
        events[id] = false;
        return id;
    }
    if (operation == 1) {
        auto it = events.find(handle);
        if (it == events.end()) return 0xffffffffu;
        bool s = it->second;
        it->second = false;
        return s ? 0 : 0x102;
    }
    if (operation == 2) {
        events.erase(handle);
        return 0;
    }
    HOST_ASSERT(false, "unknown audio event operation %u", operation);
    return 0;
}

uint32_t AudioHost::call(uint32_t handle, uint32_t operation,
                         uint32_t pointer) {
    HOST_ASSERT(operation < 24, "unknown audio operation %u", operation);
    w2c_th100x2Dgame* w = host->wasm;
    uint32_t a[7] = {0};
    for (int i = 0; i < kAudioArgs[operation]; i++)
        a[i] = mem_u32(w, pointer + i * 4);
    if (operation == 0) return add_ref_value(handle);
    if (operation == 1) return release_value(handle);
    SoundObject* o = get(handle);
    bool is_device = o->kind == "IDirectSound8";
    bool is_buffer = o->kind == "IDirectSoundBuffer";
    bool is_notify = o->kind == "IDirectSoundNotify";
    switch (operation) {
        case 2: {  // CreateSoundBuffer (device)
            HOST_ASSERT(is_device, "CreateSoundBuffer on %s",
                        o->kind.c_str());
            uint32_t flags = mem_u32(w, a[0] + 4);
            uint32_t length = mem_u32(w, a[0] + 8);
            uint32_t fmt = mem_u32(w, a[0] + 16);
            std::vector<uint8_t> f;
            uint8_t* p = fmt ? mem_bytes(w, fmt, 18) : nullptr;
            if (p) f.assign(p, p + 18);
            SoundObject* b =
                buffer(flags, length, p ? &f : nullptr, nullptr);
            mem_wu32(w, a[1], b->address);
            return 0;
        }
        case 3: {  // DuplicateSoundBuffer (device)
            HOST_ASSERT(is_device, "DuplicateSoundBuffer on %s",
                        o->kind.c_str());
            SoundObject* src = get(a[0]);
            SoundObject* b = buffer(src->flags, src->length, &src->format,
                                    src->storage);
            b->frequency = src->frequency;
            b->volume = src->volume;
            b->pan = src->pan;
            mem_wu32(w, a[1], b->address);
            return 0;
        }
        case 4:  // SetCooperativeLevel
            return 0;
        case 5: {  // SetFormat
            HOST_ASSERT(is_buffer, "SetFormat on %s", o->kind.c_str());
            uint8_t* p = mem_bytes(w, a[0], 18);
            o->format.assign(p, p + 18);
            o->frequency = rd32(o->format, 4);
            send(o, nullptr);
            return 0;
        }
        case 6: {  // GetStatus
            HOST_ASSERT(is_buffer, "GetStatus on %s", o->kind.c_str());
            position(o);
            mem_wu32(w, a[0], o->playing ? (o->loop ? 5 : 1) : 0);
            return 0;
        }
        case 7:  // Restore
            return 0;
        case 8:  // SetVolume
            HOST_ASSERT(is_buffer, "SetVolume on %s", o->kind.c_str());
            o->volume = (int32_t)a[0];
            send(o, nullptr);
            return 0;
        case 9:  // SetPan
            HOST_ASSERT(is_buffer, "SetPan on %s", o->kind.c_str());
            o->pan = (int32_t)a[0];
            send(o, nullptr);
            return 0;
        case 10:  // Play
            HOST_ASSERT(is_buffer, "Play on %s", o->kind.c_str());
            if (o->playing) o->cursor = position(o);
            o->playing = true;
            o->loop = (a[2] & 1) != 0;
            o->started = millis;
            o->last_cursor = o->cursor;
            o->has_last = true;
            send(o, nullptr);
            return 0;
        case 11:  // Stop
            HOST_ASSERT(is_buffer, "Stop on %s", o->kind.c_str());
            o->cursor = position(o);
            o->playing = false;
            send(o, nullptr);
            return 0;
        case 12:  // SetCurrentPosition
            HOST_ASSERT(is_buffer, "SetCurrentPosition on %s",
                        o->kind.c_str());
            o->cursor = a[0] % o->length;
            o->started = millis;
            send(o, nullptr);
            return 0;
        case 13: {  // GetCurrentPosition
            HOST_ASSERT(is_buffer, "GetCurrentPosition on %s",
                        o->kind.c_str());
            uint32_t pos = position(o);
            if (a[0]) mem_wu32(w, a[0], pos);
            if (a[1]) mem_wu32(w, a[1], (pos + 2048) % o->length);
            return 0;
        }
        case 14: {  // Lock
            HOST_ASSERT(is_buffer, "Lock on %s", o->kind.c_str());
            uint32_t offset = a[0] % o->length;
            uint32_t len = (a[6] & 2) ? o->length : a[1];
            uint32_t first =
                len < o->length - offset ? len : o->length - offset;
            mem_wu32(w, a[2], o->storage->data + offset);
            mem_wu32(w, a[3], first);
            if (a[4]) mem_wu32(w, a[4], len > first ? o->storage->data : 0);
            if (a[5]) mem_wu32(w, a[5], len - first);
            return 0;
        }
        case 15: {  // Unlock
            HOST_ASSERT(is_buffer, "Unlock on %s", o->kind.c_str());
            o->storage->version++;
            std::vector<Range> ranges;
            for (int i = 0; i < 4; i += 2)
                if (a[i] && a[i + 1])
                    ranges.push_back(
                        {(int64_t)a[i] - (int64_t)o->storage->data, a[i + 1]});
            for (auto& kv : objects) {
                SoundObject* other = kv.second.get();
                if (other->kind == "IDirectSoundBuffer" &&
                    other->storage == o->storage)
                    send(other, &ranges);
            }
            return 0;
        }
        case 16: {  // QueryInterface
            HOST_ASSERT(is_buffer, "QueryInterface on %s", o->kind.c_str());
            uint32_t iid = mem_u32(w, a[0]);
            if (iid == 0xb0210783) {
                if (!o->notify_obj) {
                    SoundObject* n = create_object("IDirectSoundNotify");
                    n->ref_owner = o;
                    n->parent = o;
                    o->notify_obj = n->address;
                }
                add_ref_value(o->address);
                mem_wu32(w, a[1], o->notify_obj);
                return 0;
            }
            mem_wu32(w, a[1], o->address);
            add_ref_value(o->address);
            return 0;
        }
        case 17: {  // SetNotificationPositions (notify object)
            HOST_ASSERT(is_notify, "SetNotificationPositions on %s",
                        o->kind.c_str());
            SoundObject* b = o->parent;
            b->notifications.clear();
            for (uint32_t i = 0; i < a[0]; i++)
                b->notifications.push_back(
                    {mem_u32(w, a[1] + i * 8), mem_u32(w, a[1] + i * 8 + 4)});
            return 0;
        }
        case 18:  // SetFrequency
            HOST_ASSERT(is_buffer, "SetFrequency on %s", o->kind.c_str());
            o->cursor = position(o);
            o->started = millis;
            o->frequency = a[0] ? a[0] : rd32(o->format, 4);
            send(o, nullptr);
            return 0;
        case 19:  // GetVolume
            HOST_ASSERT(is_buffer, "GetVolume on %s", o->kind.c_str());
            mem_wu32(w, a[0], (uint32_t)o->volume);
            return 0;
        case 20:  // GetPan
            HOST_ASSERT(is_buffer, "GetPan on %s", o->kind.c_str());
            mem_wu32(w, a[0], (uint32_t)o->pan);
            return 0;
        case 21:  // GetFrequency
            HOST_ASSERT(is_buffer, "GetFrequency on %s", o->kind.c_str());
            mem_wu32(w, a[0], o->frequency);
            return 0;
        case 22: {  // GetFormat
            HOST_ASSERT(is_buffer, "GetFormat on %s", o->kind.c_str());
            if (a[2]) mem_wu32(w, a[2], 18);
            if (a[0]) {
                uint32_t n = a[1] < 18 ? a[1] : 18;
                memcpy(mem_bytes(w, a[0], n), o->format.data(), n);
            }
            return 0;
        }
        case 23: {  // GetCaps (device or buffer)
            if (is_device) {
                uint32_t size = mem_u32(w, a[0]);
                if (size > 4) memset(mem_bytes(w, a[0] + 4, size - 4), 0,
                                    size - 4);
                mem_wu32(w, a[0] + 4, 0x60f);
                mem_wu32(w, a[0] + 8, 100);
                mem_wu32(w, a[0] + 12, 200000);
            } else {
                HOST_ASSERT(is_buffer, "GetCaps on %s", o->kind.c_str());
                mem_wu32(w, a[0], 20);
                mem_wu32(w, a[0] + 4, o->flags);
                mem_wu32(w, a[0] + 8, o->length);
                mem_wu32(w, a[0] + 12, 0);
                mem_wu32(w, a[0] + 16, 0);
            }
            return 0;
        }
    }
    return 0;
}

extern "C" {
uint32_t w2c_th10__audio_create(struct w2c_th10__audio* audio) {
    return audio->host->audio->create();
}
uint32_t w2c_th10__audio_call(struct w2c_th10__audio* audio, uint32_t handle,
                              uint32_t operation, uint32_t pointer) {
    return audio->host->audio->call(handle, operation, pointer);
}
uint32_t w2c_th10__audio_event(struct w2c_th10__audio* audio,
                               uint32_t operation, uint32_t handle) {
    return audio->host->audio->event(operation, handle);
}
void w2c_th10__audio_advance(struct w2c_th10__audio* audio, uint32_t ms) {
    audio->host->audio->advance(ms);
}
}  // extern "C"
