// Minimal sys/mman.h stub for the devkitPro newlib Switch build.
// Horizon has no POSIX mmap; the wasm runtime is built with
// WASM_RT_USE_MMAP=0 (malloc-based memory), so these are never called.
// Declarations only exist so wasm-rt-impl.c compiles.
#ifndef TH10_SWITCH_SYS_MMAN_H
#define TH10_SWITCH_SYS_MMAN_H

#include <stddef.h>

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void*)-1)

#ifdef __cplusplus
extern "C" {
#endif

void* mmap(void* addr, size_t length, int prot, int flags, int fd,
           long offset);
int munmap(void* addr, size_t length);
int mprotect(void* addr, size_t length, int prot);

#ifdef __cplusplus
}
#endif

#endif
