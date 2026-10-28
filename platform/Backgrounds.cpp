#include "../game/CallbackNames.hpp"
#include "Backgrounds.hpp"
#include <cstdlib>
namespace th10::browser {
BackgroundScript::BackgroundScript(Backgrounds& o):owner(o){rate=&o.engine.speed;background_color=&o.state.background_color;world=&o.engine.world;}
i32 BackgroundScript::update_animation(AnmVm& vm){return owner.engine.update(vm);}
void BackgroundScript::initialize_animation(AnmFile& file,AnmVm& vm,i32 id){initialize_embedded_animation(file,vm,id,owner.engine,owner.engine.manager.started_scripts);}
void BackgroundScript::normalize(Vec3& output,const Vec3& input){GraphicsMath::normalize(output,input);}
BackgroundDraw::BackgroundDraw(Backgrounds& o):owner(o),renderer(o.engine.renderer()),graphics_camera(renderer){world=&o.engine.world;active=&o.engine.active;screen_space=&o.engine.screen_space;fog_enabled=&o.engine.fog_enabled;rate=&o.engine.speed;camera=&graphics_camera;}
void BackgroundDraw::translation(Matrix4& matrix,const Vec3& position){GraphicsMath::translation(matrix,position);}
void BackgroundDraw::project_points(Vec3* output,const Vec3* input,u32 count,const Camera& camera,const Matrix4& world){GraphicsMath::project_array(output,12,input,12,count,&camera.viewport,&camera.projection,&camera.view,&world);}
void BackgroundDraw::draw_animation(AnmVm& vm){owner.engine.draw(vm);}
void BackgroundDraw::draw_layer(u32 layer){owner.engine.manager.draw_layer(layer,owner.engine);}
void BackgroundDraw::clear(u32 flags,u32 color,const StageClearRect* rectangle){owner.engine.device.clear_target(flags,color,1.f,0,reinterpret_cast<const i32*>(rectangle),rectangle?1u:0u);}
void BackgroundDraw::fade(i32 kind,i32 duration){ScreenEffect::create(static_cast<ScreenEffectKind>(kind),duration,0,0,0,15,owner.effects);}
Backgrounds::Backgrounds(GameState& s,AnimationEngine& e,ScreenEffects& fx,FileSystem& f):state(s),engine(e),effects(fx),files(f),script(*this){
    background=&current;overlay=&previous;stage_number=&s.game.stage;game_flags=reinterpret_cast<const u8*>(&s.game.flags);world=&e.world;rate=&e.speed;filename=source_name;animation_slots=e.manager.files;
    chain=&e.chain_value;callbacks=&e.callback_environment;update_callback=callback_id::StageUpdate;background_callback=callback_id::StageBackground;foreground_callback=callback_id::StageForeground;e.register_receiver(*this);
}
Backgrounds::~Backgrounds(){destroy(current);destroy(previous);engine.unregister_receiver(*this);}
Stage* Backgrounds::create(const char* name,i32 offset){if(offset?previous:current)__builtin_trap();return StageResources::create(name,offset,*this);}
void Backgrounds::destroy(Stage* stage){if(stage){StageResources{*stage,*this}.release();std::free(stage);}}
#ifndef TH_NATIVE_PLATFORM
bool Backgrounds::invoke(CallbackToken token,void* object,i32& result){auto& stage=*static_cast<Stage*>(object);if(token==update_callback){result=stage.update(script);return true;}if(token==background_callback||token==foreground_callback){BackgroundDraw draw(*this);StageRenderer renderer{stage,draw};result=token==background_callback?renderer.draw_background():renderer.draw_foreground();return true;}return false;}
#endif
Stage* Backgrounds::allocate_stage(){return static_cast<Stage*>(std::malloc(sizeof(Stage)));}
void* Backgrounds::allocate_bytes(u32 size){return std::malloc(size);}
void Backgrounds::release_memory(void* bytes){std::free(bytes);}
u8* Backgrounds::read_file(const char* name,u32* size){return ResourceFiles{files}.load(name,size,false);}
AnmFile* Backgrounds::load_animations(i32 slot,const char* name){return engine.manager.load(slot,name,engine.resources);}
void Backgrounds::release_animations(AnmFile& file){file.release(engine.resources);}
void Backgrounds::report(StageResourceError){error=-1;}
}

namespace th10::browser {
void Backgrounds::bind_callbacks(Callbacks& b){callback_context=this;
 b.bind(update_callback,this,[](void* p,void* o,i32){return static_cast<Stage*>(o)->update(static_cast<Backgrounds*>(p)->script);});
 b.bind(background_callback,this,[](void* p,void* o,i32){BackgroundDraw d(*static_cast<Backgrounds*>(p));return StageRenderer{*static_cast<Stage*>(o),d}.draw_background();});
 b.bind(foreground_callback,this,[](void* p,void* o,i32){BackgroundDraw d(*static_cast<Backgrounds*>(p));return StageRenderer{*static_cast<Stage*>(o),d}.draw_foreground();});
}
}
