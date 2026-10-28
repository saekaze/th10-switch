#pragma once
#include "AudioManager.hpp"
#include "ScoreData.hpp"
namespace th10 {
struct AudioGame {
    AudioManager& manager;ScoreData** scores;const u32* display_flags;const float* rate;
    i32 prepare(i32 slot,const char* name);
    void play(i32 slot,i32 track);
    void play_file(const char* name,i32 track);
    void stop();
    void fade(float seconds);
    static i32 enabled(u8 mode) noexcept {return mode==1||mode==2?0:-1;}
private:
    static bool wave_name(char* output,const char* name);
    void unlock(i32 track);
};
}
