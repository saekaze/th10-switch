// Switch port addition: see pointer_handle in Types.hpp.
#include "Types.hpp"
#include <unordered_map>
#include <vector>
namespace th10 {
#if UINTPTR_MAX==0xffffffffu
u32 pointer_handle(const void* p) noexcept {return u32(reinterpret_cast<std::uintptr_t>(p));}
const void* pointer_from_handle(u32 h) noexcept {return reinterpret_cast<const void*>(std::uintptr_t(h));}
#else
namespace {
// Handles are only minted for text arguments (names, stage and difficulty
// strings) and ECL return addresses (instructions inside loaded ECL files):
// small sets of addresses. Handle 0 is nullptr, so a zero word still prints
// "(null)" / ends a script like the original.
std::unordered_map<const void*,u32> handles;
std::vector<const void*> pointers{nullptr};
}
u32 pointer_handle(const void* p) noexcept {
    if(!p)return 0;
    const auto found=handles.find(p);if(found!=handles.end())return found->second;
    const u32 handle=u32(pointers.size());pointers.push_back(p);handles.emplace(p,handle);return handle;
}
const void* pointer_from_handle(u32 h) noexcept {return h<pointers.size()?pointers[h]:nullptr;}
#endif
}
