#include "GameProgression.hpp"
namespace th10 {
// Original offsets from the score data: stage slots 0x4e0, clear counts 0x4d0.
u8* ScoreStatistics::character_record(const GameEconomy& game) const noexcept {return data->characters[static_cast<u32>(game.character)*3+static_cast<u32>(game.shot_type)].statistics;}
void ScoreStatistics::unlock_stage(const GameEconomy& game) const noexcept {auto* record=character_record(game)+0x18+(static_cast<u32>(game.stage)+static_cast<u32>(game.difficulty)*6)*8;record[1]=1;record[0]=1;}
void ScoreStatistics::count_clear(const GameEconomy& game) const noexcept {auto* record=character_record(game)+0x08+static_cast<u32>(game.difficulty)*4;i32 count;std::memcpy(&count,record,4);if(count<99999){count=wrapping_add(count,1);std::memcpy(record,&count,4);}}
// 0x4175e0. Seven is retained when the stage table is refreshed.
void advance_stage(GameEconomy& game,const StageConfiguration* stages,const StageConfiguration** current) noexcept {if(game.stage<7)++game.stage;*current=stages+game.stage;}
static i32 product(i32 a,i32 b) noexcept {return static_cast<i32>(static_cast<u32>(a)*static_cast<u32>(b));}
// MSG opcode 20. Award components separately, preserving signed multiplication,
// division by ten and the score ceiling after each individual component.
void complete_stage(GameProgressionEnvironment& env){
    auto& game=*env.game;
    if(game.difficulty!=4)env.statistics.unlock_stage(game);
    if(game.flags&0x10){env.show_results();return;}
    if(game.stage==6||game.stage==7){
        (*env.gui)->display_flags|=0x20;
        game.add_score(product(game.item_value,1000));
        if(game.stage==7){game.add_score(product(game.lives,40000000));game.add_score(product(game.power,400000));}
        else if(static_cast<u32>(game.difficulty)<=4){constexpr i32 lives[]={20000000,25000000,35000000,40000000,40000000},power[]={100000,100000,200000,300000,400000};game.add_score(product(game.lives,lives[game.difficulty]));game.add_score(product(game.power,power[game.difficulty]));}
        if(*env.replay_mode==1){env.select_screen(4);return;}
        if(game.stage==6){env.fade_ending();(*env.gui)->ending_frames=0;(*env.gui)->display_flags|=0x10;}
        else env.show_results();
        env.statistics.count_clear(game);
        return;
    }
    env.stage_clear_notification();env.select_screen(11);advance_stage(game,env.stages,env.current_stage);
}
}
