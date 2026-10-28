#include "../game/CallbackNames.hpp"
#include "World.hpp"
#include "../game/GameObjectResources.hpp"
#include <cstdlib>
namespace th10::browser {
namespace {
struct Resources final:GameObjectResourceEnvironment {
    World& w;explicit Resources(World& world):w(world){items=&w.actors.items;bomb=&w.actors.bomb;effects=&w.actors.effects;chain=&w.chain;callbacks=&w.engine.callback_environment;item_update=callback_id::ItemsUpdate;item_draw=callback_id::ItemsDraw;bomb_update=callback_id::BombUpdate;bomb_draw=callback_id::BombDraw;effects_update=callback_id::EffectsUpdate;effects_draw=callback_id::EffectsDraw;}
    void* allocate(u32 size) override{return std::malloc(size);}
    void delete_object(void* p) override{std::free(p);}void release_geometry(void* p) override{std::free(p);}
    AnmFile* load_effect_animations() override{return w.engine.manager.load(7,"bullet.anm",w.engine.resources);}
    void report_effect_error() override{w.fail();}
};
struct Items final:ItemFrameEnvironment,ItemDrawEnvironment {
    World& w;explicit Items(World& world):w(world){
        animation_file=w.actors.bullets->animation_file;animations=&w.engine;started_animations=&w.engine.manager.started_scripts;default_rate=&w.engine.speed;power=&w.state.game.power;economy=&w.state.game;
        auto& player=*w.actors.player;player_position=&player.position;player_state=&player.state;player_attraction_speed=&player.profile->item_attraction_speed;auto_collect=&w.actors.bomb->active;input_keys=reinterpret_cast<const u32*>(&w.input.player_profiles[0].input.current);
        pickup_region=reinterpret_cast<const ItemRegion*>(&player.pickup_bounds);slow_region=reinterpret_cast<const ItemRegion*>(&player.slow_pickup_bounds);fast_region=reinterpret_cast<const ItemRegion*>(&player.fast_pickup_bounds);
    }
    void spawn_effect(const Vec3& p,i32 script) override{w.effect(*animation_file,script,p);}
    void show_notification(i32 script) override{HudEconomy env(*w.hud);env.show_notification(script);}
    void play_global_sound(i32 id) override{w.sound(id);}
    void update_lives(i32 lives) override{w.actors.gui->update_lives(lives);}
    void update_power_display(i32 whole,i32 fraction) override{w.hud->update_power(whole,fraction);}
    void refresh_player_power() override{w.configure_player();}
    void popup(const Vec3& p,i32 value,u32 color) override{w.popup(p,value,color);}
    void play_sound(i32 id,float x) override{w.sound(id,x);}
    void bind_item_sprite(AnmVm& vm,i32 sprite) override{animation_file->bind_sprite(vm,sprite);}
    void draw_animation(AnmVm& vm) override{w.engine.draw(vm);}
};
}
bool World::create_items(){Resources env(*this);return GameObjectResources{env}.create(GameObjectKind::Items)!=nullptr;}
void World::destroy_items(ItemManager* p){Resources env(*this);GameObjectResources{env}.shutdown(*p);std::free(p);}
i32 World::update_items(){Items env(*this);return actors.items->update(env);}
i32 World::draw_items(){Items env(*this);return actors.items->draw(env);}
i32 World::spawn_item(const Vec3& p,i32 kind,u32 color,float angle,float speed){Items env(*this);return actors.items->spawn(p,kind,color,angle,speed,env);}
i32 World::convert_power(){Items env(*this);return actors.items->convert_power(env);}
bool World::create_bomb(){Resources env(*this);return GameObjectResources{env}.create(GameObjectKind::Bomb)!=nullptr;}
void World::destroy_bomb(Bomb* p){Resources env(*this);GameObjectResources{env}.shutdown(*p);std::free(p);}
bool World::create_effects(){Resources env(*this);return GameObjectResources{env}.create(GameObjectKind::Effects)!=nullptr;}
void World::destroy_effects(GameEffects* p){Resources env(*this);GameObjectResources{env}.shutdown(*p);std::free(p);}
}
