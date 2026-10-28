// Switch host: paths, files and logging shared by the devices.
#pragma once
#include "../game/Types.hpp"
#include <string>
#include <vector>

namespace th10::host {
// Game data folder (th10.dat, thbgm.dat, msgothic.ttc) and save folder. On Switch
// both are the folder the NRO lives in (sdmc:/switch/touhou/touhou10 by default), the
// same layout as the other Saekaze ports. Saves sit next to the data like the
// PC release: scoreth10.dat, th10.cfg, replay/, snapshot/.
struct Paths {
    std::string data="sdmc:/switch/touhou/touhou10";
    std::string save="sdmc:/switch/touhou/touhou10";
    std::string data_file(const char* name)const{return data+"/"+name;}
    std::string save_file(const char* name)const{return save+"/"+name;}
};
Paths& paths();
void locate_data(int argc,char** argv);

void log(const char* fmt,...);
bool exists(const std::string& path);
bool make_directory(const std::string& path);
bool read_file(const std::string& path,std::vector<u8>& data,u64 limit);
// Writes through a temporary file and renames, like the upstream IDBFS path.
bool write_file(const std::string& path,const u8* data,size_t size);
inline bool write_file(const std::string& path,const std::vector<u8>& data){return write_file(path,data.data(),data.size());}
double seconds();
}
