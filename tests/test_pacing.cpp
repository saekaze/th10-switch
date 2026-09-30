// r4 pacing check: TH10's ApplicationLoop::step deadline logic (upstream
// th10_web/cpp/game/ApplicationLoop.cpp, Extended modelled as long double)
// driven by the old host clock (wall time + sleep to the deadline) and by
// the r4 frame clock (host_time.cpp: max(previous + 1/60, real + half a
// frame)). Asserts the frame clock runs exactly one game frame per vsync on
// panels slightly off 60 Hz, and still exactly one on a real hitch.
#include <cstdio>
#include <random>
typedef long double X;
struct Loop {
    double prev = 0, next = 0, sampled = 0; int frames = 0;
    void step(double now_d) {
        X now = now_d; bool back = now < (X)prev; sampled = now_d; if (back) next = now_d;
        X deadline = next; prev = now_d;
        if (!(deadline < now)) return;
        do { next = (double)((X)next + (X)(1.0 / 60.0)); } while ((X)next < (X)sampled);
        frames++;
    }
};
static double frame_clock(double previous, double real) {
    const double stepped = previous + 1.0 / 60.0, lead = real + 0.5 / 60.0;
    return stepped > lead ? stepped : lead;
}
int main() {
    std::mt19937 rng(1); std::normal_distribution<double> jitter(0, 0.0003);
    int failures = 0;
    for (double hz : {60.0, 60.05, 59.94}) {
        const int vsyncs = 60 * 60 * 30;
        Loop fresh, legacy; double clock = 0, t = 5, t_old = 5; int missed = 0, missed_old = 0;
        for (int i = 0; i < vsyncs; i++) {
            const double dt = 1.0 / hz + jitter(rng);
            t += dt; clock = frame_clock(clock, t);
            int before = fresh.frames; fresh.step(clock); if (fresh.frames - before != 1) missed++;
            t_old += dt; double now = t_old; const double d = legacy.next - now;
            if (d > 0 && d < 1.0 / hz) now = legacy.next + 0.0002;  // old SDL_Delay to deadline
            before = legacy.frames; legacy.step(now); if (legacy.frames == before) missed_old++;
        }
        std::printf("panel %.2f Hz, 30 min: r4 frame clock %d stutters, old wall clock %d\n", hz, missed, missed_old);
        if (missed) failures++;
    }
    Loop g; double clock = 0, t = 0; bool one = true;
    for (int i = 0; i < 1000; i++) { t += (i % 100 == 50) ? 0.025 : 1.0 / 60; clock = frame_clock(clock, t);
        int b = g.frames; g.step(clock); if (g.frames - b != 1) one = false; }
    std::printf("25 ms hitch: one game frame per vsync: %s\n", one ? "yes" : "NO");
    if (!one) failures++;
    return failures ? 1 : 0;
}
