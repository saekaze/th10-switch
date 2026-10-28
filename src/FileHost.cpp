// Switch port of th10_web/cpp/sdl/FileHost.cpp: the game's file imports on
// stdio. Same name normalisation and lookup order as upstream (the save
// folder first, then the game data); the browser's virtual music stream is
// the real thbgm.dat here, and both folders are the SD-card folder the game
// was found in (see Platform.cpp).
#include "Platform.hpp"
#include "../game/Types.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>
using th10::u32;using th10::i32;using th10::u8;
namespace {
struct Handle {FILE* file=nullptr;std::vector<char> buffer;};
u32 next=1;
std::map<u32,Handle> handles;
std::set<u32> writers;
std::string normalize(const char* value){
    if(!value||!*value)return {};
    std::string out,part;auto flush=[&](){if(part=="..")return false;if(!part.empty()&&part!="."){if(!out.empty())out+='/';out+=part;}part.clear();return true;};
    for(const auto* p=value;*p;p++){char c=*p;if(c==':')return {};if(c=='/'||c=='\\'){if(!flush())return {};}else part+=c>='A'&&c<='Z'?char(c+32):c;}
    if(!flush())return {};return out;
}
void parents(const std::string& path){for(size_t i=1;i<path.size();i++)if(path[i]=='/')mkdir(path.substr(0,i).c_str(),0777);}
bool match(const char* pat,const char* s){
    if(std::strcmp(pat,"*.*")==0)pat="*";
    const char* star=nullptr;const char* retry=nullptr;
    while(*s){const char a=*pat>='A'&&*pat<='Z'?char(*pat+32):*pat,b=*s>='A'&&*s<='Z'?char(*s+32):*s;
        if(*pat=='?'||(a==b&&*pat)){++pat;++s;}else if(*pat=='*'){star=pat++;retry=s;}else if(star){pat=star+1;s=++retry;}else return false;}
    while(*pat=='*')++pat;return !*pat;
}
FILE* get(u32 id){auto it=handles.find(id);return it==handles.end()?nullptr:it->second.file;}
}
extern "C" {
u32 browser_open(const char* raw,u32 write){
    const auto name=normalize(raw);if(name.empty())return ~0u;FILE* f=nullptr;const auto& p=th10::host::paths();
    if(write){const auto path=p.save+"/"+name;parents(path);
        // Like the PC game: write in place. (Upstream's IDBFS sync is a no-op here.)
        f=std::fopen(path.c_str(),"wb");}
    else{f=std::fopen((p.save+"/"+name).c_str(),"rb");if(!f&&p.data!=p.save)f=std::fopen((p.data+"/"+name).c_str(),"rb");}
    if(!f)return ~0u;const auto id=next++;auto& h=handles[id];h.file=f;
    // thbgm.dat is read in small chunks while music plays; give it a buffer.
    if(!write){h.buffer.resize(name=="thbgm.dat"?64*1024:16*1024);std::setvbuf(f,h.buffer.data(),_IOFBF,h.buffer.size());}
    if(write)writers.insert(id);return id;
}
void browser_close(u32 id){auto it=handles.find(id);if(it!=handles.end()){std::fclose(it->second.file);handles.erase(it);writers.erase(id);}}
u32 browser_size(u32 id){auto* f=get(id);if(!f)return ~0u;const long at=std::ftell(f);std::fseek(f,0,SEEK_END);const long size=std::ftell(f);std::fseek(f,at,SEEK_SET);return size<0?~0u:u32(size);}
u32 browser_seek(u32 id,i32 offset,u32 origin){auto* f=get(id);if(!f||origin>2)return ~0u;if(std::fseek(f,offset,origin==0?SEEK_SET:origin==1?SEEK_CUR:SEEK_END)!=0)return ~0u;const long at=std::ftell(f);return at<0?~0u:u32(at);}
u32 browser_read(u32 id,u8* out,u32 size){auto* f=get(id);return f?u32(std::fread(out,1,size,f)):0;}
u32 browser_write(u32 id,const u8* in,u32 size){auto* f=get(id);return f?u32(std::fwrite(in,1,size,f)):0;}
u32 browser_list(const char* directory,const char* pattern,u32 index,char* out,u32 capacity){
    const auto dir=normalize(directory);std::vector<std::string> names;const auto& p=th10::host::paths();
    for(const auto& root:{p.data,p.save})if(auto* d=opendir((root+"/"+dir).c_str())){
        while(auto* e=readdir(d)){const std::string name=e->d_name;if(name=="."||name==".."||!match(pattern,name.c_str()))continue;
            struct stat st{};if(!stat((root+"/"+dir+"/"+name).c_str(),&st)&&S_ISREG(st.st_mode))names.push_back(name);
        }closedir(d);
        if(p.data==p.save)break;
    }
    std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
    if(index>=names.size()||names[index].size()+1>capacity)return 0;std::memcpy(out,names[index].c_str(),names[index].size()+1);return 1;
}
void sdl_files_root(u32){th10::host::make_directory(th10::host::paths().save_file("replay"));}
u32 sdl_file_handles(){return u32(handles.size());}
}
