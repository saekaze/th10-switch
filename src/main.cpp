// Touhou 10 for Nintendo Switch: native entry point.
//
// This replaces th10_web/cpp/sdl/GameHost.cpp and ApplicationHost.cpp. The
// session wiring (files, input, game state, graphics, animation, fonts,
// audio, effects, application) is upstream's; the browser parts become:
// files on the SD card, a vsync-paced 60 Hz loop instead of
// requestAnimationFrame, and Switch buttons / the touch screen feeding the
// same input snapshot and TouchController the web launcher fills.
#include "../platform/Application.hpp"
#include "Platform.hpp"
#include "../portable/sdl/FrameCadence.hpp"
#include "../portable/sdl/Renderer.hpp"
#include "../portable/input/TouchController.hpp"
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>
#ifdef __SWITCH__
#include <switch.h>
#endif
using namespace th10;using namespace th10::browser;
extern "C" {
FileSystem* files_create();void files_destroy(FileSystem*);u32 files_attach(FileSystem*,const char*);
Input* input_create();void input_destroy(Input*);GameState* game_state_create(Input*,u32);void game_state_destroy(GameState*);
GraphicsDevice* graphics_create(const GraphicsPresentation*,u32);void graphics_destroy(GraphicsDevice*);
AnimationEngine* animation_engine_create(FileSystem*,GraphicsDevice*,Rng*,Rng*,float*);void animation_engine_destroy(AnimationEngine*);
Fonts* fonts_create(GraphicsDevice*,Rng*,u32);void fonts_destroy(Fonts*);Audio* audio_create(FileSystem*);void audio_destroy(Audio*);
ScreenEffects* effects_create(AnimationEngine*,const u32*,const u32*);void effects_destroy(ScreenEffects*);
Application* application_create(FileSystem*,Input*,GameState*,AnimationEngine*,Fonts*,Audio*,ScreenEffects*);void application_destroy(Application*);
void application_touch_state(Application*,u32*);void sdl_files_root(u32);
void sdl_audio_shutdown();void sdl_fonts_shutdown();void sdl_shutdown();
void sdl_audio_pump();void sdl_audio_pause(u32);
touhou::sdl::Renderer* sdl_renderer();
const touhou::sdl::Surface* sdl_back_surface();
}
namespace {
struct Session {
    FileSystem* files=nullptr;Input* input=nullptr;GameState* state=nullptr;GraphicsDevice* device=nullptr;
    AnimationEngine* animation=nullptr;Fonts* fonts=nullptr;Audio* audio=nullptr;ScreenEffects* effects=nullptr;Application* app=nullptr;
    Rng random{},visual{};float rate=1;u32 quitting=0;
    ~Session(){if(app)application_destroy(app);if(effects)effects_destroy(effects);if(audio)audio_destroy(audio);sdl_audio_shutdown();if(fonts)fonts_destroy(fonts);sdl_fonts_shutdown();if(animation)animation_engine_destroy(animation);if(device)graphics_destroy(device);if(state)game_state_destroy(state);if(input)input_destroy(input);if(files)files_destroy(files);sdl_shutdown();}
};
std::unique_ptr<Session> session;
std::string failure;

// Frame timing for th10-switch.log, averaged over 600 played frames: the game
// step (logic and draw calls) and the present (GPU submit and vsync wait).
struct FrameTiming {
    double step=0,present=0,since=0;u32 frames=0;
    void add(double s,double p){step+=s;present+=p;if(!frames++)since=host::seconds()-s-p;}
    bool due() const {return frames>=600;}
    void report(){const double elapsed=host::seconds()-since;
        host::log("timing (ms/frame): step %.2f, present %.2f, fps %.1f",step*1000/frames,present*1000/frames,elapsed>0?frames/elapsed:0.);
        *this={};}
} timing;

// Upstream sdl_game_open, Japanese data only.
Application* open_game(u32 seed){
    session=std::make_unique<Session>();auto& s=*session;sdl_files_root(0);u32 rng[]{seed,0};std::memcpy(&s.random,rng,8);std::memcpy(&s.visual,rng,8);
    s.files=files_create();if(!s.files||!files_attach(s.files,"th10.dat")){failure="Unable to open th10.dat";return nullptr;}
    const u32 parameters[]{640,480,22,1,0,0,1,0,1,1,80,0,0,0};s.input=input_create();s.state=game_state_create(s.input,0);
    s.device=graphics_create(reinterpret_cast<const GraphicsPresentation*>(parameters),0x40);if(!s.device){failure="Unable to create the OpenGL ES renderer";return nullptr;}
    s.animation=animation_engine_create(s.files,s.device,&s.random,&s.visual,&s.rate);s.fonts=fonts_create(s.device,&s.random,0);s.audio=audio_create(s.files);s.effects=effects_create(s.animation,&s.quitting,nullptr);
    s.app=application_create(s.files,s.input,s.state,s.animation,s.fonts,s.audio,s.effects);if(!s.app)failure="Unable to start the game";return s.app;
}

struct Key {const char* code;const char* sdl;u32 scan,vk;bool hosted=false;SDL_Scancode native=SDL_SCANCODE_UNKNOWN;};
#include "../portable/input/KeyboardMap.inc"
Key* key(const char* code){for(auto& k:keyboard_map)if(!std::strcmp(k.code,code))return &k;return nullptr;}
void host_key(const char* code,bool down){if(auto* k=key(code))k->hosted=down;}
touhou::input::TouchController gestures;
touhou::input::TouchState touch_state(){touhou::input::TouchState s;if(!session||!session->app)return s;u32 raw[8];application_touch_state(session->app,raw);float values[5];std::memcpy(values,raw+3,20);
    s.context=raw[0];s.instance=raw[1];s.ready=raw[2];s.x=values[0];s.y=values[1];s.fast=values[2];s.slow=values[3];s.min_x=-184;s.max_x=184;s.min_y=32;s.max_y=432;return s;}
void cancel_touch(){gestures.cancel();if(session&&session->app&&session->app->world)session->app->world->motion.target(0,0,0);}
void key(InputSnapshot& s,u32 scan,u32 vk){s.scan_keys[scan]=128;s.virtual_keys[vk]=128;if(vk>=160&&vk<=165)s.virtual_keys[16+(vk-160)/2]=128;}

// Switch controller as TH10's first gamepad, numbered as Key Config shows
// them. TH10's default pad config is shot 0, bomb 1, slow 2, pause 3, skip 4,
// so a fresh th10.cfg gives the classic layout (same as th10-switch r5):
//   0 B  1 A  2 L/ZL  3 +  4 R/ZR  5 X  6 Y
// The d-pad and sticks only move (axes and arrow keys) - never buttons, so
// Key Config cannot pick them. X = Enter, Y = R (retry), − = P (screenshot).
struct PadInput {u32 buttons=0;float x=0,y=0;bool dleft=false,dright=false,dup=false,ddown=false,minus=false,connected=false;};
#ifdef __SWITCH__
PadState pad;
PadInput read_pad(){
    padUpdate(&pad);PadInput p;if(!padIsConnected(&pad))return p;p.connected=true;
    const u64 held=padGetButtons(&pad);const auto stick=padGetStickPos(&pad,0);
    const u64 map[7]{HidNpadButton_B,HidNpadButton_A,HidNpadButton_L|HidNpadButton_ZL,HidNpadButton_Plus,HidNpadButton_R|HidNpadButton_ZR,HidNpadButton_X,HidNpadButton_Y};
    for(u32 b=0;b<7;++b)if(held&map[b])p.buttons|=1u<<b;
    p.x=float(stick.x)/32767.f;p.y=-float(stick.y)/32767.f;
    p.dleft=held&HidNpadButton_Left;p.dright=held&HidNpadButton_Right;p.dup=held&HidNpadButton_Up;p.ddown=held&HidNpadButton_Down;
    p.minus=held&HidNpadButton_Minus;return p;
}
#else
PadInput read_pad(){return {};}
#endif

#ifndef __SWITCH__
// Linux test build only: TH10_SCRIPT plays the game from a script, one tick
// per frame, so a run repeats exactly (with TH10_SEED). Lines:
//   <frames> <KeyZ,ArrowUp,...|->   hold those keys for that many frames
//   shot <tag>                      save the frame as TH10_SHOTS/NNNN-tag.png
//   repeat <n> ... end              run the lines in between n times
//   log <text>                      print text with the frame number
//   unlock                          open every stage of every rank in Practice
//   god                             player stays invulnerable (no deaths, no bomb clears)
// Each spell card prints its number, name and game-step time while active.
struct ScriptLine {std::string op,arg;int frames=0;std::vector<std::string> keys;};
std::vector<ScriptLine> script;size_t script_at=0;int script_left=0;std::vector<std::pair<size_t,int>> script_loops;
std::vector<std::string> script_keys;u64 script_frame=0;int script_shot=0;bool scripted=false;
bool load_script(const char* path){
    FILE* f=std::fopen(path,"r");if(!f)return false;char line[512];
    while(std::fgets(line,sizeof line,f)){
        std::string t(line);while(!t.empty()&&(t.back()=='\n'||t.back()=='\r'||t.back()==' '))t.pop_back();if(t.empty()||t[0]=='#')continue;
        ScriptLine l;const auto sp=t.find(' ');l.op=t.substr(0,sp);l.arg=sp==std::string::npos?"":t.substr(sp+1);
        if(std::isdigit(static_cast<unsigned char>(l.op[0]))){l.frames=std::atoi(l.op.c_str());l.op="hold";
            for(size_t a=0;a<l.arg.size();){const auto c=l.arg.find(',',a);const auto k=l.arg.substr(a,c==std::string::npos?std::string::npos:c-a);if(k!="-"&&!k.empty())l.keys.push_back(k);if(c==std::string::npos)break;a=c+1;}}
        script.push_back(l);
    }
    std::fclose(f);return true;
}
void save_shot(const std::string& tag);
bool god_mode=false;
// After each scripted step: keep the player invulnerable, and time spell cards.
struct SpellTiming {i32 number=-1;u64 started=0;double total=0,worst=0;u32 frames=0;} spell_timing;
void script_frame_done(double step){
    auto* world=session&&session->app?session->app->world:nullptr;if(!world)return;auto& actors=world->actors;
    if(god_mode&&actors.player){auto& t=actors.player->invulnerability;t.current=t.previous=100;t.fractional=100;}
    auto& t=spell_timing;const i32 active=actors.spell&&(actors.spell->spell_flags&1)?actors.spell->number:-1;
    if(active!=t.number){
        if(t.number>=0&&t.frames)std::printf("[frame %llu] spell %d ended: %u frames, step avg %.2f ms, worst %.2f ms\n",static_cast<unsigned long long>(script_frame),t.number,t.frames,t.total*1000/t.frames,t.worst*1000);
        t={};t.number=active;t.started=script_frame;
        if(active>=0){std::printf("[frame %llu] spell %d started:",static_cast<unsigned long long>(script_frame),active);for(const char* c=actors.spell->name;*c;++c)std::printf(" %02x",static_cast<u8>(*c));std::printf("\n");}
    }
    if(active>=0){t.total+=step;t.worst=std::max(t.worst,step);++t.frames;}
}
// Stage slots as GameProgression::unlock_stage marks them, ranks 0-3, stages 1-6.
void unlock_practice(){
    auto* app=session?session->app:nullptr;auto* scores=app&&app->startup?app->startup->scores:nullptr;if(!scores||!scores->data)return;
    for(auto& record:scores->data->characters)for(u32 rank=0;rank<4;++rank)for(u32 stage=1;stage<=6;++stage){u8* slot=record.statistics+0x18+(stage+rank*6)*8;slot[0]=slot[1]=1;}
}
// Advances the script by one frame; false when it has finished.
bool script_step(){
    while(script_at<script.size()){
        auto& l=script[script_at];
        if(l.op=="hold"){if(!script_left){script_left=l.frames;script_keys=l.keys;}if(--script_left<=0){++script_at;script_left=0;}return true;}
        if(l.op=="shot")save_shot(l.arg);
        else if(l.op=="log"){std::printf("[frame %llu] %s\n",static_cast<unsigned long long>(script_frame),l.arg.c_str());long pages=0,resident=0;if(FILE* f=std::fopen("/proc/self/statm","r")){if(std::fscanf(f,"%ld %ld",&pages,&resident)!=2)resident=0;std::fclose(f);}std::printf("  resident %ld KB\n",resident*4);if(auto* w=session&&session->app?session->app->world:nullptr)if(auto* pl=w->actors.player)std::printf("  player state %d invulnerable %d lives %d\n",pl->state,pl->invulnerability.current,w->state.game.lives);}
        else if(l.op=="unlock")unlock_practice();
        else if(l.op=="god")god_mode=true;
        else if(l.op=="repeat"){script_loops.push_back({script_at+1,std::atoi(l.arg.c_str())});}
        else if(l.op=="end"&&!script_loops.empty()){auto& top=script_loops.back();if(--top.second>0){script_at=top.first;continue;}script_loops.pop_back();}
        else if(l.op=="quit"){script_at=script.size();break;}
        ++script_at;
    }
    script_keys.clear();return false;
}
u32 png_crc(const u8* d,size_t n,u32 c=0xffffffff){for(size_t i=0;i<n;++i){c^=d[i];for(int k=0;k<8;++k)c=c&1?0xedb88320^(c>>1):c>>1;}return c;}
// The frame as an uncompressed PNG (stored deflate blocks).
void save_shot(const std::string& tag){
    const char* dir=std::getenv("TH10_SHOTS");if(!dir)return;const auto* surface=sdl_back_surface();if(!surface||!surface->data)return;
    const u32 w=surface->width,h=surface->height;std::vector<u8> raw;raw.reserve(size_t(w*3+1)*h);
    for(u32 y=0;y<h;++y){raw.push_back(0);const u8* row=surface->data+size_t(y)*surface->pitch;for(u32 x=0;x<w;++x){raw.push_back(row[x*4+2]);raw.push_back(row[x*4+1]);raw.push_back(row[x*4]);}}
    std::vector<u8> z{0x78,1};
    for(size_t at=0;at<raw.size();){const size_t n=std::min<size_t>(65535,raw.size()-at);z.push_back(at+n==raw.size());z.push_back(u8(n));z.push_back(u8(n>>8));z.push_back(u8(~n));z.push_back(u8(~n>>8));z.insert(z.end(),raw.begin()+long(at),raw.begin()+long(at+n));at+=n;}
    u32 a=1,b=0;for(u8 c:raw){a=(a+c)%65521;b=(b+a)%65521;}const u32 adler=b<<16|a;for(int i=3;i>=0;--i)z.push_back(u8(adler>>(8*i)));
    std::vector<u8> png{0x89,'P','N','G',13,10,26,10};
    const auto chunk=[&](const char* type,const std::vector<u8>& body){for(int i=3;i>=0;--i)png.push_back(u8(body.size()>>(8*i)));std::vector<u8> t(type,type+4);t.insert(t.end(),body.begin(),body.end());png.insert(png.end(),t.begin(),t.end());const u32 c=~png_crc(t.data(),t.size());for(int i=3;i>=0;--i)png.push_back(u8(c>>(8*i)));};
    std::vector<u8> ih;for(u32 v:{w,h})for(int i=3;i>=0;--i)ih.push_back(u8(v>>(8*i)));ih.insert(ih.end(),{8,2,0,0,0});chunk("IHDR",ih);chunk("IDAT",z);chunk("IEND",{});
    char name[512];std::snprintf(name,sizeof name,"%s/%04d-%s.png",dir,script_shot++,tag.c_str());
    if(FILE* f=std::fopen(name,"wb")){std::fwrite(png.data(),1,png.size(),f);std::fclose(f);std::printf("[frame %llu] saved %s\n",static_cast<unsigned long long>(script_frame),name);}
}
#endif

// Upstream sdl_native_input with the Switch pad in place of SDL joysticks.
void native_input(Application* app){
    const PadInput pad=read_pad();
    host_key("ArrowLeft",pad.dleft||pad.x<-.5f);host_key("ArrowRight",pad.dright||pad.x>.5f);
    host_key("ArrowUp",pad.dup||pad.y<-.5f);host_key("ArrowDown",pad.ddown||pad.y>.5f);
    host_key("Enter",pad.buttons&(1u<<5));host_key("KeyR",pad.buttons&(1u<<6));host_key("KeyP",pad.minus);
#ifndef __SWITCH__
    if(scripted){for(auto& k:keyboard_map)k.hosted=false;for(const auto& k:script_keys)host_key(k.c_str(),true);}
#endif
    auto& snapshot=app->input.snapshot;std::memset(&snapshot,0,sizeof(snapshot));snapshot.focused=1;
    const u8* physical=SDL_GetKeyboardState(nullptr);
    for(const auto& k:keyboard_map)if(k.hosted||(k.native!=SDL_SCANCODE_UNKNOWN&&physical[k.native]))key(snapshot,k.scan,k.vk);
    if(pad.connected){
        snapshot.connected[0]=1;auto& d=snapshot.direct[0];auto& l=snapshot.legacy[0];l.size=52;l.flags=255;
        const float axes[6]{std::clamp(pad.x,-1.f,1.f),std::clamp(pad.y,-1.f,1.f),0,0,0,0};
        for(int n=0;n<6;n++){d.axes[n]=i32(std::floor(axes[n]*1000+.5));(&l.x)[n]=u32(std::floor((axes[n]+1)*32767.5+.5));}
        for(u32 n=0;n<7;n++)if(pad.buttons&(1u<<n)){d.buttons[n]=128;l.buttons|=1u<<n;}
        l.pov=~0u;for(int n=0;n<4;n++)d.pov[n]=~0u;if(l.buttons)l.button_number=__builtin_ctz(l.buttons)+1;
    }
    const auto sample=gestures.sample(touch_state(),SDL_GetTicks(),snapshot.virtual_keys[16],snapshot.virtual_keys[37]||snapshot.virtual_keys[38]||snapshot.virtual_keys[39]||snapshot.virtual_keys[40]);
    for(const auto& k:keyboard_map)if(sample.keys[k.vk]||(k.vk>=160&&k.vk<=165&&sample.keys[16+(k.vk-160)/2]))key(snapshot,k.scan,k.vk);
    if(app->world)app->world->motion.target(sample.motion,sample.x,sample.y);
}

void touch_event(const SDL_Event& e){
    // SDL2 reports touches normalised to the whole screen; the game wants the
    // 640x480 picture, which is pillarboxed inside it.
    auto* r=sdl_renderer();if(!r)return;const auto& p=r->picture;if(p.width<=0||p.height<=0)return;
    const float x=(e.tfinger.x*p.drawable_width-p.x)/p.width,y=(e.tfinger.y*p.drawable_height-p.y)/p.height;
    const int type=e.type==SDL_FINGERDOWN?0:e.type==SDL_FINGERUP?2:1;
    if(type==0&&(x<0||x>1||y<0||y>1))return;
    gestures.pointer(type,int(e.tfinger.fingerId),std::clamp(x,0.f,1.f),std::clamp(y,0.f,1.f),SDL_GetTicks(),touch_state(),session&&session->input&&session->input->snapshot.virtual_keys[16]);
}

void fatal(const std::string& message){
    host::log("fatal: %s",message.c_str());
    // Tear down the game (GL context, window, audio) so the console can take
    // the framebuffer, then wait for +.
    session.reset();SDL_Quit();
#ifdef __SWITCH__
    consoleInit(nullptr);
    std::printf("\n  Touhou 10: Mountain of Faith - Switch port\n\n  Error: %s\n\n",message.c_str());
    std::printf("  Data folder: %s\n  Needed: th10.dat (v1.00a). Optional: thbgm.dat, msgothic.ttc\n\n  Press + to exit.\n",host::paths().data.c_str());
    consoleUpdate(nullptr);
    while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;consoleUpdate(nullptr);}
    consoleExit(nullptr);
#else
    std::fprintf(stderr,"%s\n",message.c_str());
#endif
}
}

// The game's monotonic() clock: seconds of running loop (TimeHost.cpp).
extern double th10_loop_elapsed;

int main(int argc,char** argv){
    host::locate_data(argc,argv);
#ifdef __SWITCH__
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleep);
#endif
    host::log("th10-switch (native): data %s",host::paths().data.c_str());
    if(!host::exists(host::paths().data_file("th10.dat"))){fatal("th10.dat not found");return 1;}
    for(auto& k:keyboard_map)k.native=SDL_GetScancodeFromName(k.sdl);
    u32 seed=u32(std::time(nullptr));
#ifndef __SWITCH__
    if(const char* path=std::getenv("TH10_SCRIPT")){if(!load_script(path)){std::fprintf(stderr,"cannot read %s\n",path);return 1;}scripted=true;std::setvbuf(stdout,nullptr,_IOLBF,0);}
    if(const char* fixed=std::getenv("TH10_SEED"))seed=u32(std::strtoul(fixed,nullptr,0));
#endif
    Application* app=open_game(seed);
    if(!app){fatal(failure);return 1;}
    // Vsync paces the loop at the panel's 60 Hz: one game tick per swap, the
    // th10-switch r4 frame clock. Measured time only adds catch-up ticks
    // after a real stall (bounded to 4 like upstream FrameCadence).
    constexpr double interval=touhou::sdl::FrameCadence::interval;
    double debt=0,previous=host::seconds(),audio_remainder=0;bool running=true;int result=0;
    while(running&&!result
#ifdef __SWITCH__
          &&appletMainLoop()
#endif
    ){
        SDL_Event e;while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT)running=false;
            else if(e.type==SDL_FINGERDOWN||e.type==SDL_FINGERUP||e.type==SDL_FINGERMOTION)touch_event(e);
            else if(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){for(auto& k:keyboard_map)k.hosted=false;cancel_touch();}
        }
        const double now=host::seconds(),delta=std::clamp(now-previous,0.,.1);previous=now;
        th10_loop_elapsed+=delta;audio_remainder+=delta*1000;const auto milliseconds=u32(std::floor(audio_remainder));audio_remainder-=milliseconds;
        app->audio.advance(milliseconds);
        auto* renderer=sdl_renderer();
        if(app->world&&app->world->loading){
            // Upstream shares the object creation sequence between loading
            // steps; spend most of a frame on it, then show the loading frame.
            const double deadline=host::seconds()+.012;
            do{app->world->advance_loading_step();}while(app->world->loading&&host::seconds()<deadline);
            app->sync_views();
            if(app->world->loading){app->engine.device.present_frame();sdl_audio_pump();debt=0;continue;}
        }
        debt+=delta;u32 ticks=u32(std::floor((debt+interval*.5)/interval));ticks=std::min(4u,std::max(ticks,1u));
        debt=std::clamp(debt-ticks*interval,-interval*.5,.1);
#ifndef __SWITCH__
        if(scripted){ticks=1;++script_frame;if(!script_step())break;}
#endif
        if(renderer)renderer->defer=true;
        const double started=host::seconds();
        for(u32 n=0;n<ticks&&!result;++n){native_input(app);result=app->step(true);}
        const double stepped=host::seconds();
        if(renderer){renderer->commit();renderer->defer=false;}
        sdl_audio_pump();
        timing.add(stepped-started,host::seconds()-stepped);if(timing.due())timing.report();
#ifndef __SWITCH__
        if(scripted)script_frame_done(stepped-started);
#endif
    }
    if(result==2){fatal("The game stopped with an error ("+std::to_string(app->error)+")");return 1;}
    session.reset();
    SDL_Quit();
    return 0;
}
