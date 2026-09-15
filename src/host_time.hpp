// C++ port of site/runtime/native-time.mjs.
#pragma once
#include <cstdint>
struct Host;
void time_local_date(Host* host, uint32_t timestamp, uint32_t pointer);
uint32_t time_timestamp();
double time_monotonic();
void time_set_virtual(bool v);
void time_advance_virtual(double dt);
