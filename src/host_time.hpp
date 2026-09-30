// C++ port of site/runtime/native-time.mjs.
#pragma once
#include <cstdint>
struct Host;
void time_local_date(Host* host, uint32_t timestamp, uint32_t pointer);
uint32_t time_timestamp();
double time_monotonic();
double time_wall();
void time_set_virtual(bool v);
void time_advance_virtual(double dt);
// Vsync-driven game clock (see host_time.cpp): enable once, then call
// time_begin_frame() before each application_step.
void time_set_frame_clock(bool on);
void time_begin_frame();
