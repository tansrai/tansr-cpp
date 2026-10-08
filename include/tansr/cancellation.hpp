#pragma once
#include "tansr/export.hpp"
#include <chrono>
#include <cstdint>
#include <memory>

namespace tansr {
namespace detail {
struct CancellationState;
}
class CancellationSource;
class CancellationToken {
  public:
    CancellationToken() noexcept = default;
    TANSR_API bool is_cancelled() const noexcept;
    TANSR_API bool wait_for(std::chrono::milliseconds duration) const;
    // 联结只观察两个来源，不能反向取消宿主的令牌。
    TANSR_API static CancellationToken combine(CancellationToken first, CancellationToken second);
    // 将绝对业务截止转换为单调计时，只收紧当前令牌；0 表示不新增截止。
    TANSR_API CancellationToken with_deadline(std::uint64_t deadline_ms) const;

  private:
    TANSR_API explicit CancellationToken(std::shared_ptr<detail::CancellationState> state);
    std::shared_ptr<detail::CancellationState> state_;
    friend class CancellationSource;
};
class CancellationSource {
  public:
    TANSR_API CancellationSource();
    TANSR_API CancellationToken token() const noexcept;
    TANSR_API void cancel() noexcept;

  private:
    std::shared_ptr<detail::CancellationState> state_;
};
TANSR_API std::int64_t unix_time_ms();
} // namespace tansr
