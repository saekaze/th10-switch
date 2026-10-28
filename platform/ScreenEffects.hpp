#pragma once
#include "AnimationEngine.hpp"
#include "MemoryPool.hpp"
#include "../game/ScreenEffect.hpp"
namespace th10::browser {
struct ScreenEffects final : ScreenEffectEnvironment,CallbackReceiver {
    AnimationEngine& engine;UpdateChain* update_chain;MemoryPool memory;
    ScreenEffects(AnimationEngine&,const u32& quitting,const u32* controller_flags=nullptr);
    ~ScreenEffects();
    void bind_callbacks(Callbacks&) override;
#ifndef TH_NATIVE_PLATFORM
    bool invoke(CallbackToken,void*,i32&) override;
#endif
    ScreenEffect* allocate() override;
    void destroy(ScreenEffect*) override;
    void fullscreen_viewport() override;
    void rectangle(const ScreenRect&,u32) override;
};
}
