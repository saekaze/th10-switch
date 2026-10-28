// Host-side tests for the native TH10 Switch port (Linux; no game data).
//   text format   - "%s" words are pointer handles on LP64
//   archive seek  - in-memory archive streams seek with pointer-sized math
//   audio host    - DirectSound outputs (buffers, locks) written as pointers
//   stage table   - stage resource names survive in pointer-sized slots
//   renderer      - the SDL2/GLES3 renderer draws and presents (needs Xvfb)
#include "../game/TextFormat.hpp"
#include "../game/ArchiveStreams.hpp"
#include "../game/SoundStream.hpp"
#include "../game/SoundResources.hpp"
#include "../platform/Audio.hpp"
#include "../platform/MenuData.hpp"
#include "../src/Platform.hpp"
#include "../portable/sdl/Renderer.hpp"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace th10;
static int failures=0;
#define CHECK(cond) do{if(!(cond)){std::printf("  FAIL %s:%d: %s\n",__FILE__,__LINE__,#cond);++failures;}}while(0)
extern "C" {
u32 audio_host_create();i32 audio_host_call(u32,u32,const std::uintptr_t*);void sdl_audio_shutdown();
}

static void test_text_format(){
    std::puts("text format");
    static const char name[]="Reimu";std::string heap="Sanae (heap)";
    const u32 words[]{pointer_handle(name),42,pointer_handle(heap.c_str()),0};
    char out[128];CHECK(format_text(out,sizeof out,"%s %d %s %s",words,4)>0);
    std::printf("  \"%s\"\n",out);CHECK(std::strcmp(out,"Reimu 42 Sanae (heap) (null)")==0);
    CHECK(pointer_handle(name)==words[0]);CHECK(pointer_from_handle(words[2])==heap.c_str());
    const double value=59.94;u32 bits[2];std::memcpy(bits,&value,8);
    CHECK(format_text(out,sizeof out,"%.1ffps",bits,2)>0&&std::strcmp(out,"59.9fps")==0);
}

namespace {struct Memory final:CodecMemory{u8* allocate_bytes(u32 n)override{return static_cast<u8*>(std::malloc(n));}void release_bytes(void* p)override{std::free(p);}};}
static void test_archive_seek(){
    std::puts("archive seek");
    Memory memory;ArchiveMemoryStream s;s.initialize(false);s.length=100;s.bytes=memory.allocate_bytes(100);for(u32 i=0;i<100;++i)s.bytes[i]=u8(i);s.cursor=s.bytes;
    u8 b[4]{};
    CHECK(s.seek(10,0));CHECK(s.position()==10);CHECK(s.read(b,2)==2&&b[0]==10&&b[1]==11);
    CHECK(s.seek(-5,1));CHECK(s.position()==7);CHECK(s.read(b,1)==1&&b[0]==7);
    CHECK(s.seek(-1,2));CHECK(s.position()==99);CHECK(s.read(b,4)==1&&b[0]==99);
    CHECK(s.cursor>=s.bytes&&s.cursor<=s.bytes+100);
    CHECK(!s.seek(100,0));CHECK(!s.seek(1,2));
    s.close(memory);
}

static void test_audio_host(){
    std::puts("audio host");
    const u32 device=audio_host_create();if(!device){std::printf("  skipped: no audio device (%s)\n",SDL_GetError());return;}
    u8 format[18]{1,0,2,0,0x44,0xac,0,0,0x10,0xb1,2,0,4,0,16,0,0,0};
    SoundBufferDescription desc{};desc.size=sizeof desc;desc.flags=0;desc.bytes=4096;desc.format=format;
    void* buffer=reinterpret_cast<void*>(~std::uintptr_t(0)); // poisoned: the host must write the whole slot
    const std::uintptr_t create[]{reinterpret_cast<std::uintptr_t>(&desc),reinterpret_cast<std::uintptr_t>(&buffer),0};
    CHECK(audio_host_call(device,u32(browser::AudioOperation::CreateBuffer),create)==0);
    const auto id=reinterpret_cast<std::uintptr_t>(buffer);std::printf("  buffer handle %#lx\n",(unsigned long)id);CHECK(id>0&&id<0x10000);
    SoundLock lock{};lock.first=reinterpret_cast<u8*>(~std::uintptr_t(0));
    const std::uintptr_t lk[]{100,8000,reinterpret_cast<std::uintptr_t>(&lock.first),reinterpret_cast<std::uintptr_t>(&lock.first_bytes),reinterpret_cast<std::uintptr_t>(&lock.second),reinterpret_cast<std::uintptr_t>(&lock.second_bytes),0};
    CHECK(audio_host_call(u32(id),u32(browser::AudioOperation::Lock),lk)!=0); // longer than the buffer
    const std::uintptr_t lk2[]{100,4096,lk[2],lk[3],lk[4],lk[5],0};
    CHECK(audio_host_call(u32(id),u32(browser::AudioOperation::Lock),lk2)==0);
    CHECK(lock.first_bytes==3996&&lock.second_bytes==100&&lock.first&&lock.second);
    if(lock.first&&lock.second){std::memset(lock.first,0x11,lock.first_bytes);std::memset(lock.second,0x22,lock.second_bytes);CHECK(lock.first==lock.second+100);}
    i32 volume=-1234;const std::uintptr_t vol[]{std::uintptr_t(u32(volume))};CHECK(audio_host_call(u32(id),u32(browser::AudioOperation::Volume),vol)==0);
    u32 got=0;const std::uintptr_t gv[]{reinterpret_cast<std::uintptr_t>(&got)};CHECK(audio_host_call(u32(id),u32(browser::AudioOperation::GetVolume),gv)==0);CHECK(i32(got)==volume);
    sdl_audio_shutdown();
}

static void test_stage_table(){
    std::puts("stage table");
    const auto* stage=browser::menu_data(false).stages;CHECK(stage!=nullptr);if(!stage)return;
    const char* std_file=reinterpret_cast<const char*>(stage[1].resources[1]);
    std::printf("  stage 1: %s / %s\n",std_file,reinterpret_cast<const char*>(stage[1].resources[3]));
    CHECK(std::strcmp(std_file,"stage01.std")==0);CHECK(stage[1].resources[0]==1);
}

namespace {
struct Surfaces {std::map<u32,std::vector<u8>> pixels;std::map<u32,touhou::sdl::Surface> info;};
touhou::sdl::Surface resolve(void* owner,u32 id){auto& s=*static_cast<Surfaces*>(owner);auto it=s.info.find(id);if(it==s.info.end())return {};auto r=it->second;auto& p=s.pixels[id];r.data=p.data();r.size=u32(p.size());return r;}
void add(Surfaces& s,u32 id,u32 w,u32 h,touhou::graphics::PixelFormat f,u32 bpp){s.pixels[id].assign(size_t(w)*h*bpp,0);touhou::sdl::Surface surface;surface.handle=id;surface.width=w;surface.height=h;surface.format=f;surface.pitch=w*bpp;s.info[id]=surface;}
}
static void test_renderer(){
    std::puts("renderer (SDL2 + GLES3)");
    if(!std::getenv("DISPLAY")){std::puts("  skipped: no display");return;}
    using namespace touhou::graphics;
    Surfaces surfaces;add(surfaces,1,640,480,PixelFormat::Bgra8,4);add(surfaces,2,640,480,PixelFormat::Depth16,2);
    touhou::sdl::Renderer r(10,resolve,&surfaces);r.title="th10 native test";
    const bool ok=r.initialize();CHECK(ok);if(!ok){std::printf("  %s\n",r.error());return;}
    r.state.target=1;r.state.depth=2;r.viewport({0,0,640,480,0,1});r.clear(3,0xff0000ff,1,0);r.read(1);
    const auto* q=surfaces.pixels[1].data()+(240*640+320)*4;CHECK(q[0]==0xff&&q[1]==0&&q[2]==0);
    ++surfaces.info[1].version;r.present(1);CHECK(r.picture.width*3==r.picture.height*4);CHECK(glGetError()==GL_NO_ERROR);
}

int main(){
    SDL_setenv("SDL_AUDIODRIVER","dummy",0);
    test_text_format();test_archive_seek();test_stage_table();
    if(SDL_Init(SDL_INIT_AUDIO)==0){test_audio_host();}
    if(SDL_InitSubSystem(SDL_INIT_VIDEO)==0)test_renderer();else std::printf("renderer skipped: %s\n",SDL_GetError());
    SDL_Quit();
    std::printf(failures?"FAILED (%d)\n":"all host tests passed\n",failures);
    return failures?1:0;
}
