#include "PlayerLifecycle.hpp"
namespace th10 {
namespace {
void reset(Timer& timer,u32& flags,i32 frames,const float* rate){
    if(!(flags&1)){timer.rate=rate;flags|=1;}
    timer.previous=wrapping_add(frames,-1);timer.current=frames;timer.fractional=Extended::from_int(frames).to_float();
}
}
void PlayerLifecycleEnvironment::cancel_spell_capture() const noexcept {
    if(*spell_elapsed<=59)return;
    *spell_bonus=0;*spell_flags&=~2u;for(auto* flags:spell_animation_flags)*flags&=~2u;
}
// 0x426cf0. The short deathbomb window begins before the actual life is lost.
void Player::hit(PlayerLifecycleEnvironment& env){
    state=4;
    const Vec3 effect_position{Scalar::add(position.x,224.0f),Scalar::add(position.y,16.0f),position.z};
    for(i32 i=0;i<33;++i){
        const u32 id=env.manager->create(*env.effect_file,i?0x163:0x162,0,AnimationPlacement::WorldBack,*env.animations,*env.allocation);
        env.manager->registry.set_position(id,effect_position,false);
    }
    reset(state_timer,state_timer_flags,0,env.default_rate);
    if(env.death_sound_enabled)env.play_death_sound();
    reset(invulnerability,invulnerability_flags,6,env.default_rate);
    animation_file->initialize_script(animation,0,*env.animations,env.manager->started_scripts);
    env.cancel_spell_capture();
}
// 0x4269d0. Preserve the integer reciprocal and wrapping multiply/divide used
// for the faith loss, including values beyond the normal score range.
void Player::die(PlayerLifecycleEnvironment& env){
    auto& economy=*env.economy;const i32 excess=wrapping_add(economy.item_value,-5000);
    const i32 third=static_cast<i32>((static_cast<i64>(excess)*0x55555555LL)>>32);
    const i32 difference=wrapping_add(third,static_cast<i32>(0u-static_cast<u32>(excess)));
    const i32 loss=(difference>>1)+(difference<0?1:0);
    const i32 scaled=static_cast<i32>(static_cast<u32>(loss)*10u);
    economy.item_value=wrapping_add(economy.item_value,scaled/10);if(economy.item_value<5000)economy.item_value=5000;
    economy.lives=wrapping_add(economy.lives,-1);if(economy.lives>=0)env.update_lives(economy.lives);
    state=2;reset(state_timer,state_timer_flags,0,env.default_rate);reset(invulnerability,invulnerability_flags,180,env.default_rate);
    animation_file->initialize_script(animation,0,*env.animations,env.manager->started_scripts);
    for(auto& option:options){option.active=0;env.manager->registry.interrupt(option.animations[0],1);env.manager->registry.interrupt(option.animations[1],1);}
    option_count=0;if(env.replay_mode!=1)env.show_caution(position);env.cancel_spell_capture();economy.add_rank(-1024);
}
}
