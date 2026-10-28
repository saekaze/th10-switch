#include "../game/CallbackNames.hpp"
#include "World.hpp"
namespace th10::browser {
namespace {
struct Popups final:ScorePopupEnvironment {
    World& w;explicit Popups(World& world):w(world){current=&w.actors.popups;effects=&w.common.value->effects;chain=&w.chain;callbacks=&w.engine.callback_environment;update_callback=callback_id::PopupsUpdate;draw_callback=callback_id::PopupsDraw;player=&w.actors.player;display_flags=&w.state.configuration.display_flags;fog=&w.engine.fog_enabled;}
    ScorePopups* allocate() override{return static_cast<ScorePopups*>(std::malloc(sizeof(ScorePopups)));}
    void delete_object(void* p) override{std::free(p);}void free_bytes(void* p) override{std::free(p);}
    void flush() override{w.engine.flush();}
    void disable_fog() override{auto render=w.engine.renderer();RenderCommands(render).SetFogEnabled(false);}
    void draw_animation(AnmVm& vm) override{auto render=w.engine.renderer();const auto flags=AnmRenderer::axis_geometry(vm,render.quad,true);AnmRenderer{w.engine.manager,render}.submit(vm,flags);}
};
}
bool World::create_popups(){Popups env(*this);return ScorePopups::create(env)!=nullptr;}
void World::destroy_popups(ScorePopups* popups){Popups env(*this);popups->shutdown(env);std::free(popups);}
i32 World::draw_popups(){Popups env(*this);return actors.popups->draw(env);}
void World::popup(const Vec3& position,i32 score,u32 color){actors.popups->spawn(score,position,color,&engine.speed);}
}
