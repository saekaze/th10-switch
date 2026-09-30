#include "host_time.hpp"

#include <ctime>

#include "wasm_host.hpp"

static double start_origin = -1.0;
static bool virtual_on = false;
static double virtual_now = 0.0;
static bool frame_clock_on = false;
static double frame_now = 0.0;

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

double time_wall() {
    double now = wall_seconds();
    if (start_origin < 0) start_origin = now;
    return now - start_origin;
}

void time_set_frame_clock(bool on) {
    frame_clock_on = on;
    frame_now = time_wall();
}

// r4: vsync is the only clock. The game's ApplicationLoop runs a frame when
// `deadline < now` and then pushes the deadline past `now` in 1/60 steps; on
// the wall clock, a panel that is not exactly 60.000 Hz (and the extra sleep
// the old loop did after every swap) lets a vsync land just before the
// deadline, so that step renders nothing and the previous frame is shown
// twice - the 59.8 FPS judder, whose phase shifts after pauses such as the
// continue screen. Upstream fixed the same thing with a "scheduled tick"
// (Application::step(true)); this wasm build predates that export, so the
// host does it through the clock the game reads:
//   * each presented frame advances the game clock by exactly 1/60 s and
//     keeps it half a frame past the deadline, so every step runs exactly one
//     frame (the margin is far larger than the x87 rounding upstream notes);
//   * the clock never falls behind real time, so a genuinely slow frame is
//     still seen by the game (its FPS counter and slowdown stay honest).
void time_begin_frame() {
    if (!frame_clock_on) return;
    const double half_frame = 0.5 / 60.0;
    const double stepped = frame_now + 1.0 / 60.0;
    const double real = time_wall() + half_frame;
    frame_now = stepped > real ? stepped : real;
}

double time_monotonic() {
    // Process-relative seconds, like performance.now()/1000 (small origin,
    // so the game's first-frame catch-up loop stays short). Virtual mode
    // advances exactly 1/60 per step for deterministic faster-than-realtime
    // tests.
    if (virtual_on) return virtual_now;
    if (frame_clock_on) return frame_now;
    return time_wall();
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
