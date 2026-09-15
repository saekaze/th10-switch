#include "host_time.hpp"

#include <ctime>

#include "wasm_host.hpp"

static double start_origin = -1.0;
static bool virtual_on = false;
static double virtual_now = 0.0;

static double wall_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void time_set_virtual(bool v) {
    virtual_on = v;
    virtual_now = 0.0;
}
void time_advance_virtual(double dt) { virtual_now += dt; }

double time_monotonic() {
    // Process-relative seconds, like performance.now()/1000 (small origin,
    // so the game's first-frame catch-up loop stays short). Virtual mode
    // advances exactly 1/60 per step for deterministic faster-than-realtime
    // tests.
    if (virtual_on) return virtual_now;
    double now = wall_seconds();
    if (start_origin < 0) start_origin = now;
    return now - start_origin;
}

void time_local_date(Host* host, uint32_t timestamp, uint32_t pointer) {
    // struct tm as 9xi32, like the browser (isdst forced to 0).
    time_t t = (time_t)(int32_t)timestamp;
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    int32_t v[9] = {tmv.tm_sec,  tmv.tm_min,  tmv.tm_hour, tmv.tm_mday,
                    tmv.tm_mon,  tmv.tm_year, tmv.tm_wday, tmv.tm_yday, 0};
    w2c_th100x2Dgame* w = host->wasm;
    for (int i = 0; i < 9; i++) mem_wi32(w, pointer + i * 4, v[i]);
}

uint32_t time_timestamp() { return (uint32_t)time(nullptr); }

extern "C" {
void w2c_th10__time_local_date(struct w2c_th10__time* t, uint32_t timestamp,
                               uint32_t pointer) {
    time_local_date(t->host, timestamp, pointer);
}
double w2c_th10__time_monotonic(struct w2c_th10__time* t) {
    (void)t;
    return time_monotonic();
}
uint32_t w2c_th10__time_timestamp(struct w2c_th10__time* t) {
    (void)t;
    return time_timestamp();
}
}  // extern "C"
