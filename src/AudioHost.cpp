// Switch port of th10_web/cpp/sdl/AudioHost.cpp: the same miniaudio mixer,
// DirectSound gain units and notification offsets. Host changes: SDL2 queued
// audio instead of an SDL3 stream; argument words are pointer-sized (the
// platform passes lock/output pointers in them), and pointer outputs are
// written as whole pointers. Music is the real thbgm.dat, read by the game's
// own streaming code through FileHost.
#define MA_NO_DEVICE_IO
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_WAV
#define MA_NO_MP3
#define MA_NO_ENCODING
#define MA_NO_THREADING
#define MINIAUDIO_IMPLEMENTATION
#include "../portable/sdl/third_party/miniaudio.h"
#include <SDL.h>
#include "../platform/Audio.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace {
using th10::u32;using th10::i32;using th10::u8;
using Word=std::uintptr_t;
template<class T=u32>T* ptr(Word p){return reinterpret_cast<T*>(p);}
// A void** output slot: the platform passes the address of a void*.
void put_pointer(Word slot,const void* value){*reinterpret_cast<const void**>(slot)=value;}
void put_handle(Word slot,u32 id){*reinterpret_cast<void**>(slot)=reinterpret_cast<void*>(Word(id));}
u32 read32(const u8* p){u32 v;std::memcpy(&v,p,4);return v;}
unsigned read16(const u8* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
struct Buffer {
    u32 refs=1,flags=0,length=0,cursor=0,last_cursor=0,frequency=44100;
    i32 volume=0,pan=0;bool device=false,playing=false,loop=false,sound_ready=false,data_ready=false;
    uint64_t started=0;std::array<u8,18> format{{1,0,2,0,68,172,0,0,16,177,2,0,4,0,16,0,0,0}};
    std::shared_ptr<std::vector<u8>> pcm;
    std::vector<std::pair<u32,u32>> notifications;
    ma_audio_buffer data{};ma_sound sound{};
    ~Buffer(){if(sound_ready)ma_sound_uninit(&sound);if(data_ready)ma_audio_buffer_uninit(&data);}
};
struct Host {
    std::map<u32,std::unique_ptr<Buffer>> objects;std::map<u32,bool> events;
    std::vector<th10::browser::Audio*> owners;
    u32 next=1,next_event=1;uint64_t millis=1000;ma_engine engine{};
    SDL_AudioDeviceID device=0;bool ready=false,paused=false,refilling=true;
    // TH07's web queue policy: bounded 1024-frame work slices, hysteresis.
    static constexpr int chunk=1024,low=4096,high=6144;
    u32 pumps=0,empty_checks=0,mixed_frames=0,min_queued=~0u,error=0;
    float rms=0;bool primed=false;
    bool initialize(){
        if(ready)return true;
        auto cfg=ma_engine_config_init();cfg.noDevice=MA_TRUE;cfg.sampleRate=44100;cfg.channels=2;
        // DirectSound changes are immediate; do not add miniaudio's default
        // short gain ramp to the game's own fades.
        cfg.defaultVolumeSmoothTimeInPCMFrames=0;
        if(ma_engine_init(&cfg,&engine)!=MA_SUCCESS){error=1;return false;}
        if(SDL_InitSubSystem(SDL_INIT_AUDIO)==0){SDL_AudioSpec want{},have{};want.freq=44100;want.format=AUDIO_F32SYS;want.channels=2;want.samples=2048;device=SDL_OpenAudioDevice(nullptr,0,&want,&have,0);}
        if(!device){ma_engine_uninit(&engine);error=2;return false;}
        ready=true;paused=false;SDL_PauseAudioDevice(device,0);return true;
    }
    Buffer* get(u32 id){auto it=objects.find(id);return it==objects.end()?nullptr:it->second.get();}
    void destroy(){objects.clear();events.clear();if(device)SDL_CloseAudioDevice(device);device=0;if(ready)ma_engine_uninit(&engine);ready=false;}
    u32 position(Buffer& b){
        if(!b.playing)return b.cursor;
        const auto align=std::max(1u,read16(b.format.data()+12));
        // Stream storage can be rewritten only after the mixer has consumed
        // it. Wall time and browser playback can diverge during a stall or
        // suspension; using that clock here overwrites unread music samples.
        if(b.sound_ready&&!b.notifications.empty()){
            ma_uint64 frame=0;ma_sound_get_cursor_in_pcm_frames(&b.sound,&frame);
            return u32(frame*align%b.length);
        }
        const uint64_t pos=b.cursor+((millis-b.started)*b.frequency/1000)*align;
        if(b.loop)return u32(pos%b.length);
        if(pos>=b.length){b.playing=false;return b.cursor=0;}return u32(pos);
    }
    void controls(Buffer& b){if(!b.sound_ready)return;
        ma_sound_set_volume(&b.sound,std::pow(10.f,float(b.volume)/2000.f));
        ma_sound_set_pan_mode(&b.sound,ma_pan_mode_balance);
        ma_sound_set_pan(&b.sound,b.pan>=0?1.f-std::pow(10.f,-float(b.pan)/2000.f):std::pow(10.f,float(b.pan)/2000.f)-1.f);
        ma_sound_set_pitch(&b.sound,float(b.frequency)/std::max(1u,read32(b.format.data()+4)));
    }
    bool sound(Buffer& b){
        if(b.sound_ready||b.flags&1)return true;
        const auto channels=read16(b.format.data()+2),align=read16(b.format.data()+12),bits=read16(b.format.data()+14);
        if(read16(b.format.data())!=1||!align||!channels||(bits!=8&&bits!=16))return false;
        auto cfg=ma_audio_buffer_config_init(bits==8?ma_format_u8:ma_format_s16,channels,b.length/align,b.pcm->data(),nullptr);
        cfg.sampleRate=read32(b.format.data()+4);
        if(ma_audio_buffer_init(&cfg,&b.data)!=MA_SUCCESS)return false;b.data_ready=true;
        if(ma_sound_init_from_data_source(&engine,&b.data,MA_SOUND_FLAG_NO_SPATIALIZATION,nullptr,&b.sound)!=MA_SUCCESS)return false;
        b.sound_ready=true;controls(b);return true;
    }
    u32 buffer(u32 flags,u32 length,const u8* format,std::shared_ptr<std::vector<u8>> shared={}){
        auto b=std::make_unique<Buffer>();b->flags=flags;b->length=length?length:4096;
        if(format)std::memcpy(b->format.data(),format,18);b->frequency=read32(b->format.data()+4);
        b->pcm=shared?shared:std::make_shared<std::vector<u8>>(b->length,read16(b->format.data()+14)==8?128:0);
        const auto id=next++;objects[id]=std::move(b);return id;
    }
    bool notify(Buffer& b){
        const auto pos=position(b),last=b.last_cursor;bool signaled=false;
        for(const auto& note:b.notifications)if((pos>=last&&note.first>=last&&note.first<pos)||(pos<last&&(note.first>=last||note.first<pos))){
            auto it=events.find(note.second);if(it!=events.end()){it->second=true;signaled=true;}
        }
        b.last_cursor=pos;return signaled;
    }
    ma_uint64 render(float* out,ma_uint64 count){
        ma_uint64 done=0;
        while(done<count){
            ma_uint64 frames=0;const auto wanted=std::min<ma_uint64>(chunk,count-done);
            if(ma_engine_read_pcm_frames(&engine,out+done*2,wanted,&frames)!=MA_SUCCESS){error=3;break;}
            done+=frames;bool signaled=false;
            for(auto& entry:objects){auto& b=*entry.second;if(b.playing&&!b.notifications.empty())signaled=notify(b)||signaled;}
            // Service the original refill worker between mixer slices. A
            // single delayed display callback must not collapse several
            // quarter-second ring notifications into one auto-reset event.
            if(signaled)for(auto* owner:owners)owner->pump();
            if(frames<wanted)break;
        }
        return done;
    }
    void pump(){
        if(!ready||paused)return;
        int queued=int(SDL_GetQueuedAudioSize(device)/8);
        min_queued=std::min(min_queued,u32(queued));
        // An empty application queue is not proof of an audible underrun:
        // SDL/browser may already be playing the last transferred block.
        if(primed&&queued==0)++empty_checks;
        if(!refilling&&queued<low)refilling=true;if(!refilling)return;
        if(queued>=high){refilling=false;primed=true;return;}
        for(int block=0;block<(high+chunk-1)/chunk&&queued<high;++block){
        float pcm[chunk*2]{};
        if(render(pcm,chunk)!=chunk){error=3;return;}
        double energy=0;for(auto& sample:pcm){sample=std::clamp(sample,-1.f,1.f);energy+=double(sample)*sample;}
        rms=std::sqrt(energy/(chunk*2));
        if(SDL_QueueAudio(device,pcm,sizeof(pcm))!=0){error=4;return;}
        ++pumps;mixed_frames+=chunk;queued+=chunk;
        }
        if(queued>=high){refilling=false;primed=true;}
    }
}host;
}
extern "C" {
u32 audio_host_create(){if(!host.initialize())return 0;auto b=std::make_unique<Buffer>();b->device=true;const auto id=host.next++;host.objects[id]=std::move(b);return id;}
i32 audio_host_call(u32 id,u32 op,const std::uintptr_t* a){
    auto* p=host.get(id);if(!p)return i32(0x80070057);auto& b=*p;
    using O=th10::browser::AudioOperation;
    switch(static_cast<O>(op)){
    case O::AddRef:return ++b.refs;
    case O::Release:if(--b.refs)return b.refs;host.objects.erase(id);return 0;
    // DSBUFFERDESC: size, flags, bytes, reserved, then the format pointer
    // (pointer-sized and aligned on 64-bit; see SoundBufferDescription).
    case O::CreateBuffer:{const auto* desc=ptr<const th10::SoundBufferDescription>(a[0]);put_handle(a[1],host.buffer(desc->flags,desc->bytes,static_cast<const u8*>(desc->format)));break;}
    case O::Duplicate:{auto* src=host.get(a[0]);if(!src)return -1;const auto copy=host.buffer(src->flags,src->length,src->format.data(),src->pcm);auto& dest=*host.get(copy);dest.frequency=src->frequency;dest.volume=src->volume;dest.pan=src->pan;put_handle(a[1],copy);break;}
    case O::Cooperative:case O::Restore:case O::Unlock:break;
    case O::Format:if(b.sound_ready){ma_sound_uninit(&b.sound);b.sound_ready=false;}if(b.data_ready){ma_audio_buffer_uninit(&b.data);b.data_ready=false;}std::memcpy(b.format.data(),ptr(a[0]),18);b.frequency=read32(b.format.data()+4);break;
    case O::Status:host.position(b);*ptr(a[0])=b.playing?(b.loop?5:1):0;break;
    case O::Volume:b.volume=i32(a[0]);host.controls(b);break;
    case O::Pan:b.pan=i32(a[0]);host.controls(b);break;
    case O::Frequency:b.cursor=host.position(b);b.started=host.millis;b.frequency=a[0]?u32(a[0]):read32(b.format.data()+4);host.controls(b);break;
    case O::GetVolume:*ptr(a[0])=u32(b.volume);break;
    case O::GetPan:*ptr(a[0])=u32(b.pan);break;
    case O::GetFrequency:*ptr(a[0])=b.frequency;break;
    case O::Play:
        if(!host.sound(b)){host.error=5;return -1;}
        if(b.playing)b.cursor=host.position(b);b.playing=true;b.loop=!!(a[2]&1);b.started=host.millis;b.last_cursor=b.cursor;
        if(b.sound_ready){ma_sound_set_looping(&b.sound,b.loop);ma_sound_seek_to_pcm_frame(&b.sound,b.cursor/std::max(1u,read16(b.format.data()+12)));ma_sound_start(&b.sound);}break;
    case O::Stop:b.cursor=host.position(b);b.playing=false;if(b.sound_ready)ma_sound_stop(&b.sound);break;
    case O::Position:b.cursor=u32(a[0]%b.length);b.started=host.millis;if(b.sound_ready)ma_sound_seek_to_pcm_frame(&b.sound,b.cursor/std::max(1u,read16(b.format.data()+12)));break;
    case O::CurrentPosition:{const auto pos=host.position(b);if(a[0])*ptr(a[0])=pos;if(a[1])*ptr(a[1])=(pos+2048)%b.length;break;}
    case O::Lock:{const u32 offset=u32(a[0]%b.length),len=a[6]&2?b.length:u32(a[1]),first=std::min(len,b.length-offset);if(len>b.length)return -1;
        put_pointer(a[2],b.pcm->data()+offset);*ptr(a[3])=first;if(a[4])put_pointer(a[4],len>first?b.pcm->data():nullptr);if(a[5])*ptr(a[5])=len-first;break;}
    // One native buffer owns both interfaces. This preserves COM reference
    // ownership without building a second browser-side object graph.
    case O::Query:++b.refs;put_handle(a[1],id);break;
    case O::Notifications:{auto* notes=ptr(a[1]);b.notifications.clear();for(u32 i=0;i<a[0];i++)b.notifications.emplace_back(notes[i*2],notes[i*2+1]);break;}
    case O::GetFormat:if(a[2])*ptr(a[2])=18;if(a[0])std::memcpy(ptr(a[0]),b.format.data(),std::min<Word>(a[1],18));break;
    case O::Caps:{u32 caps[]{20,b.flags,b.length,0,0};std::memcpy(ptr(a[0]),caps,sizeof(caps));break;}
    default:return -1;
    }return 0;
}
u32 audio_host_event(u32 op,u32 id){
    if(op==0){const auto next=host.next_event++;host.events[next]=false;return next;}
    if(op==2){host.events.erase(id);return 0;}auto it=host.events.find(id);if(it==host.events.end())return ~0u;
    const bool signaled=it->second;it->second=false;return signaled?0:0x102;
}
void audio_host_advance(u32 ms){host.millis+=ms;}
void audio_host_attach(th10::browser::Audio* owner,bool attach){
    if(attach)host.owners.push_back(owner);
    else host.owners.erase(std::remove(host.owners.begin(),host.owners.end(),owner),host.owners.end());
}
void sdl_audio_pump(){host.pump();}
void sdl_audio_pause(u32 paused){if(host.paused==(paused!=0))return;host.paused=paused!=0;if(host.device)SDL_PauseAudioDevice(host.device,paused?1:0);}
void sdl_audio_shutdown(){host.destroy();}
u32 sdl_audio_queued(){return host.device?u32(SDL_GetQueuedAudioSize(host.device)/8):0;}
}

