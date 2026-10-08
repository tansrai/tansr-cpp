#include "tansr/cancellation.hpp"
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace tansr;
using namespace std::chrono_literals;
static void check(bool value, const char *text) {
    if (!value)
        throw std::runtime_error(text);
}
int main() {
    try {
        CancellationSource source;
        auto token = source.token();
        check(!token.is_cancelled(), "new token cancelled");
        check(!token.with_deadline(0).is_cancelled(), "zero deadline changed token");
        check(token.with_deadline(1).is_cancelled(), "past deadline ignored");
        check(!token.is_cancelled(), "derived deadline cancelled parent");
        check(!token.with_deadline(std::numeric_limits<std::uint64_t>::max()).is_cancelled(),
              "far deadline overflow");
        const auto before = std::chrono::steady_clock::now();
        auto timed = token.with_deadline(static_cast<std::uint64_t>(unix_time_ms() + 50));
        check(timed.wait_for(2s), "deadline not observed while waiting");
        check(std::chrono::steady_clock::now() - before < 1s, "deadline wait failed to wake");
        CancellationSource second;
        auto combined = CancellationToken::combine(token, second.token());
        second.cancel();
        check(combined.is_cancelled() && !token.is_cancelled(), "combined token propagation");
        auto child = token.with_deadline(static_cast<std::uint64_t>(unix_time_ms() + 10000));
        source.cancel();
        check(child.is_cancelled(), "parent cancellation lost");
        CancellationSource concurrent;
        std::thread writer([&] {
            std::this_thread::sleep_for(20ms);
            concurrent.cancel();
        });
        const bool observed = concurrent.token().wait_for(2s);
        writer.join();
        check(observed, "cross-thread cancellation lost");
        CancellationSource longest;
        std::thread long_writer([&] {
            std::this_thread::sleep_for(20ms);
            longest.cancel();
        });
        const bool longest_observed = longest.token().wait_for(std::chrono::milliseconds::max());
        long_writer.join();
        check(longest_observed, "maximum duration overflowed instead of observing cancellation");
        check(!CancellationToken{}.wait_for(std::chrono::milliseconds::min()),
              "negative duration overflowed instead of returning immediately");
        std::cout << "cancellation: 11 checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
