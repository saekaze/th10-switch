#include "Timer.hpp"
namespace th10 {
// 0x401f40 / 0x401f90: rate is retained, including custom rates.
void Timer::reset() noexcept { initialize(-999999); }
void Timer::initialize(i32 previous_value) noexcept {
    previous = previous_value;
    current = 0;
    fractional = 0;
}
static bool unscaled(float rate) noexcept { return 0.99f < rate && rate < 1.01f; }
// 0x404ed0 / 0x44bfa0. The scaled branch converts the extended sum
// to an integer BEFORE the rounded float stored in fractional is reloaded.
i32 Timer::tick() noexcept {
    previous = current;
    if (unscaled(*rate)) {
        current = wrapping_add(current, 1);
        fractional = Scalar::add(fractional,1.0f);
    } else {
        const auto sum = number(*rate) + number(fractional);
        fractional = sum.to_float();
        current = sum.truncate_int();
    }
    return current;
}
// 0x44bf40: unlike tick(), this path reloads the rounded float first.
void Timer::advance(float frames) noexcept {
    previous = current;
    auto delta = number(frames);
    if (!unscaled(*rate)) delta = delta * number(*rate);
    fractional = (delta + number(fractional)).to_float();
    current = Scalar::truncate(fractional);
}
}
