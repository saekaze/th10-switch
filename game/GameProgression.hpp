#pragma once
#include "Gui.hpp"
#include "ScoreData.hpp"
namespace th10 {
// Views into the original score.dat statistics. The Extra clear count shares
// the unused stage-zero slot; keep the two indexes explicit instead of padding
// a C++ struct across overlapping fields.
// Switch port: addressed through ScoreData rather than 32-bit byte offsets
// from its start (it begins with two pointers).
struct ScoreStatistics {
    ScoreData* data;
    // CharacterRecord::statistics of the current character and shot type.
    u8* character_record(const GameEconomy& game) const noexcept;
    void unlock_stage(const GameEconomy& game) const noexcept;
    void count_clear(const GameEconomy& game) const noexcept;
};
// Switch port: resources[1..5] hold file-name pointers, so the slots are
// pointer-sized (u32 in the original 32-bit layout).
struct StageConfiguration {std::uintptr_t resources[6];const char* messages[2];const char* interface_animations;u32 reserved_024;const char* music;u32 reserved_02c;};
TH_LAYOUT_ASSERT(sizeof(StageConfiguration)==48);
struct GameProgressionEnvironment {
    GameEconomy* game;
    Gui** gui;
    ScoreStatistics statistics;
    const i32* replay_mode;
    const StageConfiguration* stages;
    const StageConfiguration** current_stage;
    virtual void stage_clear_notification()=0;
    virtual void select_screen(i32 screen)=0;
    virtual void show_results()=0;
    virtual void fade_ending()=0;
};
void advance_stage(GameEconomy& game,const StageConfiguration* stages,const StageConfiguration** current) noexcept;
void complete_stage(GameProgressionEnvironment& environment);
}
