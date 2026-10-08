// 原 A06 网络实证入口；DNS 服务器与 OS 临时控制文件由独立工装管理。
#include "tansr/json.hpp"
#include "tansr/runtime.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace tansr;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
std::int64_t elapsed_ms(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}
const char *error_name(ErrorCode code) {
    switch (code) {
    case ErrorCode::cancelled:
        return "cancelled";
    case ErrorCode::timeout:
        return "timeout";
    case ErrorCode::tls:
        return "tls";
    case ErrorCode::network:
        return "network";
    default:
        return "other";
    }
}
void shutdown(std::shared_ptr<Runtime> &runtime) {
    require(take(runtime->shutdown(3s)) == ShutdownStatus::stopped,
            "Runtime shutdown did not reach stopped");
    runtime.reset();
}

Json system_trust() {
    RuntimeOptions options;
    options.connect_timeout = 10s;
    require(options.ca_file.empty() && options.proxy.empty(), "default trust options changed");
    auto runtime = take(Runtime::create(options));
    HttpRequest request;
    request.method = "GET";
    request.url = "https://example.com/";
    request.deadline_ms = unix_time_ms() + 15000;
    request.max_response_bytes = 128U * 1024U;
    const auto started = Clock::now();
    auto response = runtime->request(request);
    const auto duration = elapsed_ms(started);
    shutdown(runtime);
    Json receipt = Json::object({{"mode", "system-trust"},
                                 {"url", request.url},
                                 {"explicitCaFile", false},
                                 {"credentialsSent", false},
                                 {"redirectsFollowed", false},
                                 {"runtimeShutdown", "stopped"},
                                 {"elapsedMs", duration}});
    if (!response) {
        receipt.set("status", "failed");
        receipt.set("errorCode", error_name(response.error().code));
        receipt.set("error", response.error().message);
    } else {
        receipt.set("httpStatus", response.value().status);
        receipt.set("bodyBytes", static_cast<std::uint64_t>(response.value().body.size()));
        receipt.set("status", response.value().status == 200 ? "passed" : "failed");
    }
    return receipt;
}

#ifdef __linux__
std::uint64_t proc_entries(const char *path) {
    std::uint64_t count = 0;
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        (void)entry;
        ++count;
    }
    return count;
}
#endif

Json blocked_dns(const std::string &host, const std::filesystem::path &observed) {
    require(host.size() > 9 && host.size() < 100 && host.compare(0, 4, "a06-") == 0 &&
                host.compare(host.size() - 5, 5, ".test") == 0 &&
                std::all_of(host.begin(), host.end(),
                            [](unsigned char ch) {
                                return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                                       ch == '-' || ch == '.';
                            }),
            "blocked DNS fixture requires a unique a06-*.test host");
    require(!std::filesystem::exists(observed), "query marker exists before request");
#ifdef __linux__
    const auto descriptors_before = proc_entries("/proc/self/fd");
    const auto threads_before = proc_entries("/proc/self/task");
#endif
    RuntimeOptions options;
    options.connect_timeout = 30s;
    auto runtime = take(Runtime::create(options));
    HttpRequest request;
    request.method = "GET";
    request.url = "http://" + host + "/";
    request.deadline_ms = unix_time_ms() + 60000;
    const auto started = Clock::now();
    auto handle = take(runtime->request_async(request));
    const auto query_deadline = started + 8s;
    while (!std::filesystem::exists(observed) && !handle.ready() && Clock::now() < query_deadline)
        std::this_thread::sleep_for(5ms);
    require(std::filesystem::exists(observed), "no actual DNS query observed before cancellation");
    std::ifstream file(observed, std::ios::binary);
    std::string marker_host;
    std::getline(file, marker_host);
    file.close();
    require(marker_host == host, "DNS query marker does not identify this request");
    require(!handle.ready(), "request was not blocked at observed DNS query");
    const auto observed_ms = elapsed_ms(started);
    const auto cancel_started = Clock::now();
    handle.cancel();
    const auto result = handle.wait();
    const auto cancel_ms = elapsed_ms(cancel_started);
    const auto total_ms = elapsed_ms(started);
    require(!result && result.error().code == ErrorCode::cancelled && handle.ready(),
            "blocked resolver did not return cancelled");
    require(cancel_ms < 3000 && total_ms < options.connect_timeout.count(),
            "resolver cancellation waited for connection timeout");
    shutdown(runtime);
    Json receipt = Json::object({{"mode", "blocked-dns"},
                                 {"status", "passed"},
                                 {"host", host},
                                 {"credentialsSent", false},
                                 {"queryMarkerVerified", true},
                                 {"pendingAtCancel", true},
                                 {"errorCode", "cancelled"},
                                 {"queryObservedMs", observed_ms},
                                 {"cancelMs", cancel_ms},
                                 {"totalMs", total_ms},
                                 {"connectTimeoutMs", std::int64_t{30000}},
                                 {"runtimeShutdown", "stopped"}});
#ifdef __linux__
    const auto descriptors_after = proc_entries("/proc/self/fd");
    const auto threads_after = proc_entries("/proc/self/task");
    receipt.set("descriptorsBefore", descriptors_before);
    receipt.set("descriptorsAfter", descriptors_after);
    receipt.set("threadsBefore", threads_before);
    receipt.set("threadsAfter", threads_after);
    require(descriptors_after == descriptors_before, "resolver cancellation leaked descriptors");
    require(threads_after == threads_before, "resolver cancellation leaked threads");
#endif
    return receipt;
}
} // namespace

int main(int argc, char **argv) {
    try {
        Json receipt;
        if (argc == 2 && std::string(argv[1]) == "system-trust")
            receipt = system_trust();
        else if (argc == 4 && std::string(argv[1]) == "blocked-dns")
            receipt = blocked_dns(argv[2], std::filesystem::u8path(argv[3]));
        else
            throw std::runtime_error(
                "usage: serve_network system-trust | blocked-dns HOST QUERY_MARKER");
        std::cout << receipt.dump() << '\n';
        return receipt.at("status").as_string() == "passed" ? 0 : 1;
    } catch (const std::exception &error) {
        std::cout << Json::object({{"status", "failed"}, {"error", error.what()}}).dump() << '\n';
        return 1;
    }
}
