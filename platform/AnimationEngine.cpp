#include "../game/CallbackNames.hpp"
#include "AnimationEngine.hpp"
#include <cstdlib>
namespace th10::browser {
namespace {
u32 pointer(const void* p){return static_cast<u32>(reinterpret_cast<uintptr_t>(p));}
constexpr CallbackToken animation_callbacks[]={callback_id::AnimationsWorld,callback_id::AnimationsUI,callback_id::AnimationLayer0,callback_id::AnimationLayer1,callback_id::AnimationLayer2,callback_id::AnimationLayer3,callback_id::AnimationLayer4,callback_id::AnimationLayer5,callback_id::AnimationLayer6,callback_id::AnimationLayer7,callback_id::AnimationLayer8,callback_id::AnimationLayer9,callback_id::AnimationLayer10,callback_id::AnimationLayer11,callback_id::AnimationLayer12,callback_id::AnimationLayer13,callback_id::AnimationLayer14,callback_id::AnimationLayer15,callback_id::AnimationLayer16,callback_id::AnimationLayer19};
constexpr CallbackToken layer_callbacks[]={callback_id::AnimationLayer0,callback_id::AnimationLayer1,callback_id::AnimationLayer2,callback_id::AnimationLayer3,callback_id::AnimationLayer4,callback_id::AnimationLayer5,callback_id::AnimationLayer6,callback_id::AnimationLayer7,callback_id::AnimationLayer8,callback_id::AnimationLayer9,callback_id::AnimationLayer10,callback_id::AnimationLayer11,callback_id::AnimationLayer12,callback_id::AnimationLayer13,callback_id::AnimationLayer14,callback_id::AnimationLayer15,callback_id::AnimationLayer16,0,0,callback_id::AnimationLayer19};
}
AnimationEngine::AnimationEngine(FileSystem& f,GraphicsDevice& d,Rng& script,Rng& visual,float& s):device(d),script_random(script),visual_random(visual),speed(s),callback_environment(*this),resources(f,d,manager){
    rate=&speed;script_rng=&script_random;visual_rng=&visual_random;reference_positions[0]=&world.position;reference_positions[1]=&world.reserved_024;camera_delta=&world.animation_delta;default_tangent=&tangent;
    initial_quad=initial_vertices;render_quad=vertices;model_quad=model_template;chain=&chain_value;callbacks=&callback_environment;
    std::memcpy(frame_callbacks,animation_callbacks,sizeof(frame_callbacks));allocation=this;
    AnmDistortionEnvironment::random=&script_random;update_callback=callback_id::DistortionUpdate;draw_callback=callback_id::DistortionDraw;

    callback_environment.bind(callback_id::AnimationsWorld,this,[](void* p,void* argument,i32){return static_cast<AnmManager*>(argument)->update_world(*static_cast<AnimationEngine*>(p));});
    callback_environment.bind(callback_id::AnimationsUI,this,[](void* p,void* argument,i32){return static_cast<AnmManager*>(argument)->update_ui(*static_cast<AnimationEngine*>(p));});
    for(i32 i=0;i<20;i++)if(layer_callbacks[i])callback_environment.bind(layer_callbacks[i],this,[](void* p,void*,i32 layer){return static_cast<AnimationEngine*>(p)->draw_layer(layer);},i);
    callback_environment.bind(callback_id::DistortionUpdate,this,[](void*,void* o,i32){auto& vm=*static_cast<AnmVm*>(o);return static_cast<AnmDistortion*>(vm.geometry)->update(vm);});
    callback_environment.bind(callback_id::DistortionDraw,this,[](void* p,void* o,i32){auto& s=*static_cast<AnimationEngine*>(p);auto& vm=*static_cast<AnmVm*>(o);auto env=s.renderer();AnmRenderer{s.manager,env}.draw_textured_fan(vm,static_cast<AnmDistortion*>(vm.geometry)->vertices,33);return 0;});
    chain_value.initialize();manager.initialize(*this);manager.initialize_model(*this);Camera::initialize(world,ui);configure_camera(true);
}
AnimationEngine::~AnimationEngine(){
    chain_value.clear_list(chain_value.update,callback_environment);chain_value.clear_list(chain_value.draw,callback_environment);manager.release(*this);
    for(i32 slot=0;slot<33;++slot)manager.unload(slot,resources);
    if(manager.model_vertex_buffer)device.release_resource(manager.model_vertex_buffer);
}
GraphicsRenderer AnimationEngine::renderer(){return GraphicsRenderer(device,manager,*active,world,vertices);}
#ifndef TH_NATIVE_PLATFORM
bool AnimationEngine::invoke(CallbackToken token,void* object,i32& result){
    if(token==callback_id::AnimationsWorld){result=static_cast<AnmManager*>(object)->update_world(*this);return true;}
    if(token==callback_id::AnimationsUI){result=static_cast<AnmManager*>(object)->update_ui(*this);return true;}
    for(u32 i=0;i<20;++i)if(layer_callbacks[i]&&layer_callbacks[i]==token){result=draw_layer(i);return true;}
    if(token==callback_id::DistortionUpdate){auto& vm=*static_cast<AnmVm*>(object);result=static_cast<AnmDistortion*>(vm.geometry)->update(vm);return true;}
    if(token==callback_id::DistortionDraw){auto& vm=*static_cast<AnmVm*>(object);auto env=renderer();AnmRenderer{manager,env}.draw_textured_fan(vm,static_cast<AnmDistortion*>(vm.geometry)->vertices,33);result=0;return true;}
    const auto cached=resolved_callbacks.find(token);
    if(cached!=resolved_callbacks.end())return cached->second->invoke(token,object,result);
    for(auto* receiver:receivers)if(receiver&&receiver->invoke(token,object,result)){resolved_callbacks[token]=receiver;return true;}
    return application_callbacks&&application_callbacks->invoke(token,object,result);
}
#endif
void AnimationEngine::register_receiver(CallbackReceiver& receiver){receiver.bind_callbacks(callback_environment);
#ifndef TH_NATIVE_PLATFORM
resolved_callbacks.clear();for(auto*& slot:receivers)if(!slot){slot=&receiver;return;}__builtin_trap();
#endif
}
void AnimationEngine::unregister_receiver(CallbackReceiver& receiver){callback_environment.unbind(receiver.callback_context);
#ifndef TH_NATIVE_PLATFORM
resolved_callbacks.clear();for(auto*& slot:receivers)if(slot==&receiver)slot=nullptr;
#endif
}
void AnimationEngine::callback(u32 token,AnmVm& vm){callback_environment.invoke(token,&vm);}
i32 AnimationEngine::update(AnmVm& vm){return vm.update(*this);}
void AnimationEngine::draw(AnmVm& vm){auto env=renderer();AnmRenderer{manager,env}.draw(vm);}
void AnimationEngine::bind_sprite(AnmVm& vm,i32 index){vm.animation_file->bind_sprite(vm,index);}
void AnimationEngine::change_draw_mode(AnmVm& vm){AnmDistortion::initialize(vm,*this);}
void* AnimationEngine::allocate_geometry(u32 bytes){return std::malloc(bytes);}
AnmVm* AnimationEngine::spawn_child(AnmVm& parent,i32 script,u32 mode){
    const auto placement=mode==88?AnimationPlacement::WorldBack:mode==90?AnimationPlacement::UiBack:mode==91?AnimationPlacement::WorldFront:AnimationPlacement::UiFront;
    u32 id=manager.create(*parent.animation_file,script,parent.owner_tag,placement,*this,*this);return manager.registry.find_and_clear(id);
}
AnmVm* AnimationEngine::allocate_animation(){return static_cast<AnmVm*>(std::malloc(sizeof(AnmVm)));}
void AnimationEngine::release_memory(void* p){std::free(p);}
void* AnimationEngine::allocate(u32 bytes){return std::malloc(bytes);}
void AnimationEngine::release(void* p){std::free(p);}
void AnimationEngine::clear_pixel_shader(){device.clear_shader();}
void AnimationEngine::create_model_buffer(void*& buffer){device.create_vertices(80,Layouts::World,buffer);}
void* AnimationEngine::lock_model_buffer(void* buffer){return device.map_vertices(buffer);}
void AnimationEngine::unlock_model_buffer(void* buffer){device.unmap_vertices(buffer);}
void AnimationEngine::bind_model_buffer(void* buffer){device.vertex_buffer(buffer,20);}
void AnimationEngine::begin_frame(){auto env=renderer();AnmRenderer{manager,env}.begin_frame();}
void AnimationEngine::flush(){auto env=renderer();AnmRenderer{manager,env}.flush();}
i32 AnimationEngine::update_all(){return chain_value.run(false,callback_environment);}
i32 AnimationEngine::draw_all(){return chain_value.run(true,callback_environment);}
i32 AnimationEngine::draw_layer(u32 layer){auto env=renderer();GraphicsCamera camera(env);return AnmLayers{world,ui,active,screen_space,fog_enabled,camera,*this}.draw(manager,layer);}
void AnimationEngine::configure_camera(bool flat){auto env=renderer();GraphicsCamera camera(env);if(flat)active->configure_flat(camera);else active->configure_world(camera);camera.set_viewport(active->viewport);}
}
