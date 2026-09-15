// C++ port of site/runtime/audio-mixer.mjs (DirectSoundMixer).
// Game thread pushes events; the SDL audio callback renders.
// All PCM bytes are copied at push time (like the browser's structured
// clone), so the audio thread never touches WASM memory.
#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

struct MixerEvent {
    enum Type { Clock, Update, Remove } type = Clock;
    double time = 0;  // host millis domain
    uint32_t id = 0;
    // Update fields (audio-bridge message shape).
    bool playing = false, loop = false;
    uint32_t cursor = 0, rate = 44100, channels = 2, bits = 16, align = 4;
    int32_t volume = 0, pan = 0;
    std::vector<uint8_t> data;  // full buffer, or empty when patches
    struct Patch {
        uint32_t offset = 0;
        std::vector<uint8_t> bytes;
    };
    std::vector<Patch> patches;
};

class AudioMixer {
  public:
    explicit AudioMixer(double sample_rate) : rate_(sample_rate) {}
    void set_rate(double r) { rate_ = r; }
    // Thread-safe push from the game thread.
    void push(MixerEvent e);
    // Called only from the audio thread. Renders interleaved stereo f32.
    void render(float* out, int frames);
    double peak() const { return peak_; }
    uint64_t frames() const { return frames_; }

  private:
    struct Voice {
        std::vector<uint8_t> data;
        bool playing = false, loop = false;
        uint32_t cursor = 0, rate = 44100, channels = 2, bits = 16, align = 4;
        double time = 0;
        int32_t volume = 0, pan = 0;
        double left = 1.0, right = 1.0;
    };
    double rate_;
    std::map<uint32_t, Voice> voices_;
    std::vector<MixerEvent> events_;  // time-sorted
    double time_ = 0;
    bool has_time_ = false;
    double latest_ = 0;
    double peak_ = 0;
    uint64_t frames_ = 0;
    std::mutex mutex_;
    std::vector<MixerEvent> pending_;

    void apply(const MixerEvent& e);
    float sample(const Voice& b, int64_t frame, int channel);
};
