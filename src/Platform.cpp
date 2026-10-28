#include "Platform.hpp"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <SDL.h>

namespace th10::host {
Paths& paths(){static Paths p;return p;}

namespace {
FILE* log_file=nullptr;
std::string directory_of(const char* path){
    std::string s=path?path:"";const auto slash=s.find_last_of('/');
    return slash==std::string::npos?std::string():s.substr(0,slash);
}
}

void locate_data(int argc,char** argv){
    std::vector<std::string> candidates;
#ifdef __SWITCH__
    // hbmenu passes the NRO path as argv[0]; the folder next to it wins.
    if(argc>0&&argv[0]){const auto dir=directory_of(argv[0]);if(!dir.empty())candidates.push_back(dir);}
    // Data folders: thNN / touhouN (any capitalisation - FAT is case-insensitive)
    // directly on the SD card, in switch/, in a shared touhou/ or switch/touhou/
    // folder, or in games/ and roms/.
    for(const char* root:{"sdmc:/switch/","sdmc:/","sdmc:/touhou/","sdmc:/switch/touhou/","sdmc:/games/","sdmc:/roms/"})
        for(const char* name:{"th10","touhou10","touhou 10"})candidates.push_back(std::string(root)+name);
#else
    if(argc>1&&argv[1])candidates.push_back(argv[1]);
    candidates.push_back(".");
#endif
    for(const auto& c:candidates)if(exists(c+"/th10.dat")){paths().data=paths().save=c;break;}
    if(!exists(paths().data+"/th10.dat")&&!candidates.empty())paths().data=paths().save=candidates.front();
    make_directory(paths().save);
    log_file=std::fopen(paths().save_file("th10-switch.log").c_str(),"w");
}

void log(const char* fmt,...){
    va_list ap;va_start(ap,fmt);char line[1024];std::vsnprintf(line,sizeof line,fmt,ap);va_end(ap);
    std::fprintf(stderr,"%s\n",line);
    if(log_file){std::fprintf(log_file,"%s\n",line);std::fflush(log_file);}
}
bool exists(const std::string& path){struct stat st;return stat(path.c_str(),&st)==0;}
bool make_directory(const std::string& path){return mkdir(path.c_str(),0777)==0||exists(path);}
bool read_file(const std::string& path,std::vector<u8>& data,u64 limit){
    FILE* f=std::fopen(path.c_str(),"rb");if(!f)return false;
    std::fseek(f,0,SEEK_END);const long size=std::ftell(f);std::fseek(f,0,SEEK_SET);
    if(size<0||u64(size)>limit){std::fclose(f);return false;}
    data.resize(size_t(size));const bool ok=data.empty()||std::fread(data.data(),1,data.size(),f)==data.size();std::fclose(f);return ok;
}
bool write_file(const std::string& path,const u8* data,size_t size){
    const auto temporary=path+".tmp";FILE* f=std::fopen(temporary.c_str(),"wb");if(!f)return false;
    const bool written=!size||std::fwrite(data,1,size,f)==size;const bool closed=std::fclose(f)==0;
    if(!written||!closed){std::remove(temporary.c_str());return false;}
    std::remove(path.c_str()); // FAT (sdmc) rename does not replace.
    return std::rename(temporary.c_str(),path.c_str())==0;
}
double seconds(){return double(SDL_GetPerformanceCounter())/double(SDL_GetPerformanceFrequency());}
}
