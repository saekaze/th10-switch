#include "../game/CallbackNames.hpp"
#include "World.hpp"
#include "GameplayData.hpp"
#include "../game/ProjectileSystems.hpp"
#include <cstdlib>
namespace th10::browser {
namespace {
struct Resources final:ProjectileSystemsEnvironment {
    World& w;explicit Resources(World& world):w(world){chain=&w.engine.chain_value;callbacks=&w.engine.callback_environment;animations=&w.engine;active_bullets=&w.actors.bullets;active_lasers=&w.actors.lasers;bullet_update=callback_id::BulletsUpdate;bullet_draw=callback_id::BulletsDraw;laser_update=callback_id::LasersUpdate;laser_draw=callback_id::LasersDraw;}
    AnmFile* load_bullet_animations() override{return w.engine.manager.load(7,"bullet.anm",w.engine.resources);}
    void discard_file_animations(AnmFile* file) override{w.engine.manager.registry.discard_file(file);}
    void missing_bullet_animations() override{w.fail();}
    void* allocate_manager(u32 size) override{return std::malloc(size);}
    void release_manager(void* p) override{std::free(p);}
    void destroy_laser(EnemyLaser& laser) override{w.destroy_laser(laser);}
};
struct Bullets final:BulletBehaviorEnvironment {
    World& w;explicit Bullets(World& world):w(world){
        default_rate=&w.engine.speed;manager=&w.engine.manager;effect_file=w.actors.bullets->animation_file;animations=&w.engine;allocation=&w.engine;controller_flags=w.actors.session?&w.actors.session->session_flags:nullptr;player_position=&w.actors.player->position;rng=&w.engine.script_random;
        sprite_scripts=gameplay_data::sprite_scripts;cancel_types=gameplay_data::cancel_types;cancel_scripts=gameplay_data::cancel_scripts;draw_layers=gameplay_data::draw_layers;hitbox_sizes=gameplay_data::hitbox_sizes;
    }
    void spawn_faith(const Vec3& p) override{w.spawn_item(p,8,0xffffffff,-1.5707964f,.6f);}
    void play_turn_sound(i32 id) override{w.sound(id);}
    void run_commands(EnemyBullet& bullet) override{bullet.process_commands(*this);}
    void emit(const BulletEmitter& emitter) override{emitter.fire(*w.actors.bullets,*this);}
    void update_feature(EnemyBullet& bullet,BulletFeature feature) override{bullet.update_feature(feature,*this);}
    i32 collide_player(const Vec3& p,const Vec2& size) override{return w.collide_player(p,size);}
    void play_sound(i32 id,float x) override{w.sound(id,x);}
    void submit(AnmVm& vm) override{w.engine.draw(vm);}
};
}
bool World::create_bullets(){Resources env(*this);return EnemyBulletManager::create(env)!=nullptr;}
void World::destroy_bullets(EnemyBulletManager* p){Resources env(*this);p->shutdown(env);std::free(p);}
void World::clear_bullets(){Resources env(*this);actors.bullets->clear(env);}
i32 World::update_bullets(){Bullets env(*this);return actors.bullets->tick(env);}
i32 World::draw_bullets(){Bullets env(*this);return actors.bullets->render(env);}
void World::fire(const BulletEmitter& emitter){Bullets env(*this);emitter.fire(*actors.bullets,env);}
void World::cancel_bullets(bool protection){Bullets env(*this);actors.bullets->cancel_all(protection,env);}
void World::cancel_bullet(EnemyBullet& bullet){Bullets env(*this);bullet.cancel(env);}
void World::cancel_bullet_circle(const Vec3& p,float radius,bool convert,bool protection){Bullets env(*this);th10::cancel_bullet_circle(actors.bullets->pool,p,radius,convert,protection,env);}
i32 World::cancel_bullet_rectangle(i32 convert){Bullets env(*this);return actors.bullets->cancel_rectangle(convert,gameplay_data::small_colors,gameplay_data::medium_colors,gameplay_data::large_colors,env);}
bool World::create_lasers(){Resources env(*this);return LaserManager::create(env)!=nullptr;}
void World::destroy_lasers(LaserManager* p){Resources env(*this);p->shutdown(env);std::free(p);}
}
