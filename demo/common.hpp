#pragma once
#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <tansr/api.hpp>
#include <tansr/crypto.hpp>
#include <tansr/executor.hpp>
#include <tansr/session.hpp>
#include <tansr/storage.hpp>
#include <thread>

namespace demo {
struct Failure {
    tansr::Error error;
};
template <class T> T take(tansr::Result<T> value) {
    if (!value)
        throw Failure{value.error()};
    return std::move(value).value();
}
inline void take(tansr::Result<void> value) {
    if (!value)
        throw Failure{value.error()};
}
[[noreturn]] void fail(std::string message,
                       tansr::ErrorCode code = tansr::ErrorCode::invalid_input);
std::string safe(std::string_view text);
std::string field(const tansr::Json &, std::string_view name);
std::filesystem::path absolute_path(std::string_view value);
std::string request_id();
std::string environment(const char *name);
int report(std::string_view name, const std::function<void()> &action);

class Args {
  public:
    Args(int argc, char **argv);
    std::optional<std::string> take(std::string_view key);
    std::string value(std::string_view key, std::string fallback);
    std::string required(std::string_view key);
    bool flag(std::string_view key);
    void finish() const;

  private:
    std::map<std::string, std::string> values_;
};

// 这是示例宿主的可信身份输入，不能把终端自报 scope 当作 Serve 授权。
// 共享安全只读句柄允许 chat/tools 同时读取；每次请求/工具/档案访问重核。
class Credentials {
  public:
    static std::shared_ptr<Credentials> open(std::string token_file, std::string scope_file);
    tansr::Result<tansr::AuthToken> token(tansr::CancellationToken);
    tansr::Result<void> check(tansr::CancellationToken = {});
    const tansr::executor::Scope &scope() const noexcept { return scope_; }

  private:
    std::filesystem::path token_file_, scope_file_;
    tansr::executor::Scope scope_;
    std::mutex mutex_;
    tansr::executor::Scope read_scope();
    void check_locked(tansr::CancellationToken);
};

struct Configuration {
    std::string base, family;
    std::shared_ptr<Credentials> credentials;
    std::chrono::seconds timeout{600};
    static Configuration read(Args &);
};

// Runtime 的最终所有者在 main 栈；所有 demo worker 先停止并 join。
class Host {
  public:
    explicit Host(Configuration configuration);
    ~Host();
    void close();
    const Configuration config;
    std::shared_ptr<tansr::Runtime> runtime;
    std::shared_ptr<tansr::ApiClient> api;

  private:
    bool closed_{false};
};
tansr::Json intent_owner(const Host &);
std::unique_ptr<tansr::storage::PrivateDirectory> private_directory(const std::filesystem::path &,
                                                                    const Host &);

class Stop {
  public:
    explicit Stop(std::chrono::seconds timeout);
    ~Stop();
    tansr::CancellationToken token() const { return source_.token(); }
    void cancel() { source_.cancel(); }

  private:
    tansr::CancellationSource source_;
    std::atomic<bool> finished_{false};
    std::thread watcher_;
};
tansr::session::WriteOptions write_options(tansr::CancellationToken);
tansr::CallOptions call_options(tansr::CancellationToken,
                                std::optional<std::int64_t> deadline = {});
tansr::crypto::Aes256Key archive_key();
tansr::crypto::Aes256Key archive_key(const char *environment_name);

// 可取消的控制台/管道输入，不遗留 detached stdin 线程。
class Console {
  public:
    Console();
    ~Console();
    std::optional<std::string> poll();
    bool ended() const noexcept { return ended_; }

  private:
    std::string pending_;
    bool ended_{false};
#ifdef _WIN32
    void *handle_{nullptr};
    unsigned long original_mode_{0};
    bool console_{false};
    std::wstring wide_;
#endif
};
} // namespace demo
