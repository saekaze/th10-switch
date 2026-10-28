#include "AudioGame.hpp"
namespace th10 {
bool AudioGame::wave_name(char* output,const char* name){
    const auto length=std::strlen(name);if(length>=256)return false;std::memcpy(output,name,length+1);
    char* extension=std::strrchr(output,'.');if(!extension||extension-output>251)return false;
    // Only three bytes are replaced; longer extensions retain their suffix.
    const bool short_extension=std::strlen(extension)<4;std::memcpy(extension+1,"wav",3);if(short_extension)extension[4]=0;return true;
}
// Original address: score data + 0x1d892 + track (32-bit layout).
void AudioGame::unlock(i32 track){(*scores)->settings.statistics[0x11+track]=1;}
i32 AudioGame::prepare(i32 slot,const char* name){char filename[256];if(!wave_name(filename,name))return 0;manager.queue_music(1,slot,filename);return 1;}
void AudioGame::play(i32 slot,i32 track){if(*display_flags&16)manager.queue_music(4,0,"dummy");manager.queue_music(2,slot,"dummy");unlock(track);}
void AudioGame::play_file(const char* name,i32 track){char filename[256];if(!wave_name(filename,name))return;unlock(track);manager.queue_music(2,-1,filename);}
void AudioGame::stop(){manager.queue_music(*display_flags&16?4:3,0,"dummy");}
void AudioGame::fade(float seconds){const auto speed=number(*rate);auto duration=number(seconds);if(!(speed==number(0))&&!(number(1)<speed))duration=duration/speed;manager.queue_music(5,duration.truncate_int(),"");}
}
