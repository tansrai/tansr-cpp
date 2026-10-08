#pragma once
#include <chrono>

namespace tansr::detail {
// 毫秒上限可能远大于 steady_clock 的剩余刻度，先比较再转换，避免有符号溢出。
inline std::chrono::steady_clock::time_point deadline_after(std::chrono::milliseconds duration) {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    if (duration <= std::chrono::milliseconds::zero())
        return now;
    const auto room =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::time_point::max() - now);
    if (duration >= room)
        return Clock::time_point::max();
    return now + duration;
}
} // namespace tansr::detail
