#include "audio_mixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

void AudioMixer::push(MixerEvent e) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(std::move(e));
}

void AudioMixer::apply(const MixerEvent& e) {
    if (e.type == MixerEvent::Clock) {
        latest_ = e.time;
        if (!has_time_) {
            time_ = e.time - 80;
            has_time_ = true;
        } else if (fabs(e.time - 80 - time_) > 250) {
            time_ = e.time - 80;
        }
        return;
    }
    if (e.type == MixerEvent::Remove) {
        voices_.erase(e.id);
        return;
    }
    Voice& b = voices_[e.id];
    b.playing = e.playing;
    b.loop = e.loop;
    b.cursor = e.cursor;
    b.time = e.time;
    b.rate = e.rate;
    b.channels = e.channels;
    b.bits = e.bits;
    b.align = e.align;
    b.volume = e.volume;
    b.pan = e.pan;
    if (!e.data.empty()) b.data = e.data;
    for (const auto& p : e.patches) {
        if (p.offset + p.bytes.size() <= b.data.size())
            memcpy(b.data.data() + p.offset, p.bytes.data(), p.bytes.size());
    }
    b.left = pow(10.0, (b.volume - (b.pan > 0 ? b.pan : 0)) / 2000.0);
    b.right = pow(10.0, (b.volume + (b.pan < 0 ? b.pan : 0)) / 2000.0);
}

float AudioMixer::sample(const Voice& b, int64_t frame, int channel) {
    int64_t count = (int64_t)b.data.size() / (int64_t)b.align;
    if (b.loop) {
        frame = ((frame % count) + count) % count;
    } else if (frame < 0 || frame >= count) {
        return 0;
    }
    int ch = channel < (int)b.channels - 1 ? channel : (int)b.channels - 1;
    if (ch < 0) ch = 0;
    uint64_t off = (uint64_t)frame * b.align + (uint64_t)ch * b.bits / 8;
    if (b.bits == 16) {
        int16_t v = (int16_t)(b.data[off] | (b.data[off + 1] << 8));
        return v / 32768.0f;
    }
    if (b.bits == 8) return (b.data[off] - 128) / 128.0f;
    return 0;
}

void AudioMixer::render(float* out, int frames) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bool added = false;
        for (auto& e : pending_) {
            if (e.type == MixerEvent::Clock) {
                apply(e);
                continue;
            }
            events_.push_back(std::move(e));
            added = true;
        }
        pending_.clear();
        if (added)
            std::stable_sort(events_.begin(), events_.end(),
                             [](const MixerEvent& a, const MixerEvent& b) {
                                 return a.time < b.time;
                             });
    }
    for (int i = 0; i < frames * 2; i++) out[i] = 0;
    if (!has_time_) return;
    double step = 1000.0 / rate_;
    for (int i = 0; i < frames; i++) {
        while (!events_.empty() && events_[0].time <= time_ + 0.000001) {
            apply(events_[0]);
            events_.erase(events_.begin());
        }
        double l = 0, r = 0;
        for (auto& kv : voices_) {
            Voice& b = kv.second;
            if (!b.playing || b.data.empty()) continue;
            double pos = (double)b.cursor / b.align +
                         (time_ - b.time) * b.rate / 1000.0;
            int64_t frame = (int64_t)floor(pos);
            double frac = pos - floor(pos);
            if (!b.loop && frame >= (int64_t)b.data.size() / (int64_t)b.align) {
                b.playing = false;
                continue;
            }
            float a = sample(b, frame, 0), c = sample(b, frame, 1);
            l += (a + (sample(b, frame + 1, 0) - a) * frac) * b.left;
            r += (c + (sample(b, frame + 1, 1) - c) * frac) * b.right;
        }
        float fl = (float)(l < -1 ? -1 : l > 1 ? 1 : l);
        float fr = (float)(r < -1 ? -1 : r > 1 ? 1 : r);
        out[i * 2] = fl;
        out[i * 2 + 1] = fr;
        if (fabs(fl) > peak_) peak_ = fabs(fl);
        if (fabs(fr) > peak_) peak_ = fabs(fr);
        time_ += step;
    }
    frames_ += frames;
}
