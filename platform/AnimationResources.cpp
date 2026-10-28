#include "AnimationResources.hpp"
#include <cstdlib>
namespace th10::browser {
namespace {u32 address(const void* value){return static_cast<u32>(reinterpret_cast<uintptr_t>(value));}}
AnimationResources::AnimationResources(FileSystem& f,GraphicsDevice& d,AnmManager& m,u32 flags):files(f),device(d),manager(m),display(flags),textures(d,display){registry=&m.registry;loader_flags=&loading;}
AnimationResources::~AnimationResources(){
#ifdef TH_NATIVE_PLATFORM
    for(const auto& entry:prepared)for(const auto& texture:entry.textures)if(texture.resident)device.release_resource(texture.resident);
#endif
}
AnmFile* AnimationResources::allocate_file(){return static_cast<AnmFile*>(std::malloc(sizeof(AnmFile)));}
void* AnimationResources::allocate_bytes(u32 size){return std::malloc(size);}
void AnimationResources::release_file(AnmFile* file){std::free(file);}
void AnimationResources::release_bytes(void* bytes){std::free(bytes);}
u8* AnimationResources::read_file(const char* name,bool external,u32* size){
#ifdef TH_NATIVE_PLATFORM
    u32 n=0;auto* data=ResourceFiles{files}.load(name,&n,external);if(size)*size=n;if(!external)last_read_size=n;return data;
#else
    return ResourceFiles{files}.load(name,size,external);
#endif
}
void AnimationResources::release_texture(void* texture){device.release_resource(texture);}
i32 AnimationResources::materialize(AnmFile& file,i32 texture,i32 sprite,i32 script,const AnmChunk* chunk){return file.materialize(texture,sprite,script,chunk,*this);}
void AnimationResources::wait_for_loading(AnmManager& owner,AnmFile&){owner.process_loading(*this);}
void AnimationResources::set_priority(void* texture,u32 priority){device.resource_priority(texture,priority);}
void AnimationResources::preload(void* texture){device.preload_resource(texture);}
AnmTextureDimensions AnimationResources::dimensions(void* texture){void* surface=textures.get_surface(texture);const auto desc=textures.describe_surface(surface);textures.release_surface(surface);return {desc.width,desc.height};}
}
