#include "tansr/cancellation.hpp"
#include "time.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace tansr {
namespace detail {
struct CancellationState {
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
    std::condition_variable changed;
    std::shared_ptr<CancellationState> first;
    std::shared_ptr<CancellationState> second;
    std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::time_point::max()};
};
bool cancelled(const std::shared_ptr<CancellationState> &s) noexcept {
    return s && (s->cancelled.load(std::memory_order_acquire) ||
                 std::chrono::steady_clock::now() >= s->deadline || cancelled(s->first) ||
                 cancelled(s->second));
}
} // namespace detail
CancellationToken::CancellationToken(std::shared_ptr<detail::CancellationState> state)
    : state_(std::move(state)) {}
bool CancellationToken::is_cancelled() const noexcept { return detail::cancelled(state_); }
bool CancellationToken::wait_for(std::chrono::milliseconds duration) const {
    const auto end = detail::deadline_after(duration);
    while (!is_cancelled()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= end)
            return false;
        const auto interval =
            std::min(std::chrono::duration_cast<std::chrono::milliseconds>(end - now),
                     std::chrono::milliseconds(10));
        if (state_) {
            std::unique_lock<std::mutex> lock(state_->mutex);
            state_->changed.wait_for(lock, interval);
        } else {
            std::mutex mutex;
            std::condition_variable changed;
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_for(lock, interval);
        }
    }
    return true;
}
CancellationToken CancellationToken::combine(CancellationToken first, CancellationToken second) {
    if (!first.state_)
        return second;
    if (!second.state_)
        return first;
    if (first.state_ == second.state_)
        return first;
    auto s = std::make_shared<detail::CancellationState>();
    s->first = std::move(first.state_);
    s->second = std::move(second.state_);
    return CancellationToken(std::move(s));
}
CancellationToken CancellationToken::with_deadline(std::uint64_t deadline_ms) const {
    if (deadline_ms == 0)
        return *this;
    auto s = std::make_shared<detail::CancellationState>();
    s->first = state_;
    const auto steady_now = std::chrono::steady_clock::now();
    const auto wall_now = static_cast<std::uint64_t>(std::max<std::int64_t>(0, unix_time_ms()));
    if (deadline_ms <= wall_now) {
        s->deadline = steady_now;
    } else {
        const auto room = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::time_point::max() - steady_now)
                              .count();
        const auto remaining = std::min(deadline_ms - wall_now, static_cast<std::uint64_t>(room));
        s->deadline = steady_now + std::chrono::milliseconds(remaining);
    }
    return CancellationToken(std::move(s));
}
CancellationSource::CancellationSource() : state_(std::make_shared<detail::CancellationState>()) {}
CancellationToken CancellationSource::token() const noexcept { return CancellationToken(state_); }
void CancellationSource::cancel() noexcept {
    state_->cancelled.store(true, std::memory_order_release);
    state_->changed.notify_all();
}
std::int64_t unix_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace tansr
