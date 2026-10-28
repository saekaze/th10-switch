#include "GameLog.hpp"
namespace th10 {
const char* GameLog::append(const char* format,const u32* arguments,bool error,GameLogEnvironment& env){
    env.enter();++*env.lock_depth;
    char formatted[8192];env.format(formatted,error?512:8192,format,arguments);const u32 length=std::strlen(formatted);
    // Switch port: pointer-sized comparison (the original compared 32-bit addresses).
    const std::uintptr_t end=reinterpret_cast<std::uintptr_t>(cursor)+length;
    if(end<reinterpret_cast<std::uintptr_t>(text)+8191){char* output=cursor;const char* input=formatted;char value;do{value=*input++;*output++=value;}while(value);cursor+=length;*cursor=0;}
    if(error)has_error=1;env.leave();--*env.lock_depth;return format;
}
}
