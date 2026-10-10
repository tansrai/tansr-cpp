#include "common.hpp"
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// shellapi 声明依赖 Windows 基础类型，保持独立 include 组。
#include <shellapi.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace demo {
namespace {
volatile std::sig_atomic_t interrupted = 0;
void signal_handler(int) { interrupted = 1; }
std::string trim_line(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
        value.pop_back();
    return value;
}
#ifdef _WIN32
std::string utf8(const wchar_t *value, int count) {
    if (count == 0)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, count, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0)
        fail("Windows text contains invalid Unicode");
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, count, text.data(), size,
                             nullptr, nullptr))
        fail("Windows text conversion failed");
    return text;
}
#endif
} // namespace
[[noreturn]] void fail(std::string message, tansr::ErrorCode code) {
    throw Failure{{code, std::move(message)}};
}
std::string safe(std::string_view text) {
    if (!tansr::valid_utf8(text))
        return "[invalid UTF-8]";
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t n = c < 128 ? 1 : (c < 224 ? 2 : (c < 240 ? 3 : 4));
        const auto part = text.substr(i, n);
        i += n;
        if (n == 1 && c != '\n' && c != '\t' && (c < 32 || c == 127))
            continue;
        if ((n == 2 && c == 0xc2 && static_cast<unsigned char>(part[1]) < 0xa0) ||
            part == "\xd8\x9c" || part == "\xe2\x80\x8e" || part == "\xe2\x80\x8f" ||
            (n == 3 && c == 0xe2 && static_cast<unsigned char>(part[1]) == 0x80 &&
             static_cast<unsigned char>(part[2]) >= 0xaa &&
             static_cast<unsigned char>(part[2]) <= 0xae) ||
            (n == 3 && c == 0xe2 && static_cast<unsigned char>(part[1]) == 0x81 &&
             static_cast<unsigned char>(part[2]) >= 0xa6 &&
             static_cast<unsigned char>(part[2]) <= 0xa9))
            continue;
        out.append(part);
    }
    return out;
}
std::string field(const tansr::Json &value, std::string_view key) {
    const auto *item = value.find(key);
    return item && item->is_string() ? item->as_string() : std::string{};
}
std::filesystem::path absolute_path(std::string_view value) {
    if (!tansr::valid_utf8(value))
        fail("path must be UTF-8");
    auto path = std::filesystem::u8path(value);
    if (!path.is_absolute() || path.filename().empty())
        fail("an absolute private path is required");
    return path;
}
std::string request_id() {
    const auto bytes = take(tansr::crypto::random_bytes(16));
    const char *hex = "0123456789abcdef";
    std::string result = "cpp-";
    for (auto byte : bytes) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}
std::string environment(const char *name) {
#ifdef _WIN32
    const std::string ascii(name);
    std::wstring key(ascii.begin(), ascii.end());
    const DWORD size = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (!size)
        return {};
    std::wstring value(size, L'\0');
    const DWORD actual = GetEnvironmentVariableW(key.c_str(), value.data(), size);
    if (!actual || actual >= size)
        fail("environment changed during read");
    return utf8(value.data(), static_cast<int>(actual));
#else
    const char *value = std::getenv(name);
    return value ? value : "";
#endif
}
int report(std::string_view name, const std::function<void()> &action) {
    try {
        action();
        return 0;
    } catch (const Failure &e) {
        std::cerr << name << ": code=" << static_cast<int>(e.error.code)
                  << " status=" << e.error.http_status;
        if (!e.error.wire_code.empty())
            std::cerr << " wire=" << safe(e.error.wire_code)
                      << " retry=" << safe(e.error.retry_action);
        if (e.error.code == tansr::ErrorCode::invalid_input)
            std::cerr << " " << safe(e.error.message);
        else
            std::cerr
                << "; outcome is not confirmed; retain the original session, intent and journal";
        std::cerr << '\n';
        return 1;
    } catch (...) {
        std::cerr << name << ": local failure; original detail withheld; retain existing state\n";
        return 1;
    }
}
Args::Args(int argc, char **argv) {
    std::vector<std::string> arguments;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int count = 0;
    auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide)
        fail("cannot read process arguments");
    try {
        for (int i = 0; i < count; ++i)
            arguments.push_back(utf8(wide[i], static_cast<int>(std::wcslen(wide[i]))));
    } catch (...) {
        LocalFree(wide);
        throw;
    }
    LocalFree(wide);
#else
    for (int i = 0; i < argc; ++i)
        arguments.emplace_back(argv[i]);
#endif
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        std::string key = arguments[i];
        if (key.rfind("--", 0) != 0 || key.size() == 2)
            fail("use --name value options");
        std::string value = "true";
        if (key != "--help" && key != "--require-output" && key != "--run-once" &&
            key != "--create") {
            if (++i >= arguments.size() || std::string_view(arguments[i]).rfind("--", 0) == 0)
                fail("option requires a value");
            value = arguments[i];
        }
        if (!values_.emplace(key, value).second)
            fail("duplicate option");
    }
}
std::optional<std::string> Args::take(std::string_view key) {
    auto it = values_.find(std::string(key));
    if (it == values_.end())
        return {};
    auto out = it->second;
    values_.erase(it);
    return out;
}
std::string Args::value(std::string_view key, std::string fallback) {
    return take(key).value_or(std::move(fallback));
}
std::string Args::required(std::string_view key) {
    auto result = take(key);
    if (!result || result->empty())
        fail("missing required option " + std::string(key));
    return *result;
}
bool Args::flag(std::string_view key) { return take(key).has_value(); }
void Args::finish() const {
    if (!values_.empty())
        fail("unknown option; see --help");
}
std::shared_ptr<Credentials> Credentials::open(std::string token_file, std::string scope_file) {
    const auto token = absolute_path(token_file), scope = absolute_path(scope_file);
    if (token.parent_path() != scope.parent_path() || token.filename() == scope.filename())
        fail("token and scope must be distinct files in one private credential directory");
    auto result = std::shared_ptr<Credentials>(new Credentials());
    result->token_file_ = token;
    result->scope_file_ = scope;
    result->scope_ = result->read_scope();
    return result;
}
tansr::executor::Scope Credentials::read_scope() {
    auto bytes = take(
        tansr::storage::read_private_file(scope_file_, 8192, [] { return tansr::Result<void>{}; }));
    if (!bytes)
        fail("scope file is missing");
    auto value = take(tansr::Json::parse(*bytes, {8192, 8, 32}));
    if (!value.is_object() || value.as_object().size() != 3)
        fail("scope must contain exactly applicationScopeId, endUserId, authorizationRevision");
    tansr::executor::Scope scope{field(value, "applicationScopeId"), field(value, "endUserId"),
                                 field(value, "authorizationRevision")};
    if (scope.application_scope_id.empty() || scope.end_user_id.empty() ||
        scope.authorization_revision.empty())
        fail("scope fields must be nonempty strings");
    return scope;
}
void Credentials::check_locked(tansr::CancellationToken cancel) {
    if (cancel.is_cancelled())
        fail("cancelled", tansr::ErrorCode::cancelled);
    const auto now = read_scope();
    if (now.application_scope_id != scope_.application_scope_id ||
        now.end_user_id != scope_.end_user_id ||
        now.authorization_revision != scope_.authorization_revision)
        fail("current scope changed", tansr::ErrorCode::permission);
}
tansr::Result<void> Credentials::check(tansr::CancellationToken cancel) {
    try {
        std::lock_guard<std::mutex> guard(mutex_);
        check_locked(cancel);
        return {};
    } catch (const Failure &e) {
        return e.error;
    } catch (...) {
        return tansr::Error{tansr::ErrorCode::io, "credential read failed"};
    }
}
tansr::Result<tansr::AuthToken> Credentials::token(tansr::CancellationToken cancel) {
    try {
        std::lock_guard<std::mutex> guard(mutex_);
        check_locked(cancel);
        auto data = take(tansr::storage::read_private_file(token_file_, 8192,
                                                           [] { return tansr::Result<void>{}; }));
        if (!data)
            fail("token file missing");
        auto value = trim_line(*data);
        if (value.empty() || !std::all_of(value.begin(), value.end(),
                                          [](unsigned char c) { return c >= 33 && c <= 126; }))
            fail("token must be one nonempty ASCII line");
        check_locked(cancel);
        return tansr::AuthToken{std::move(value),
                                tansr::Json::array({scope_.application_scope_id, scope_.end_user_id,
                                                    scope_.authorization_revision})
                                    .dump()};
    } catch (const Failure &e) {
        return e.error;
    } catch (...) {
        return tansr::Error{tansr::ErrorCode::io, "credential read failed"};
    }
}
Configuration Configuration::read(Args &args) {
    Configuration result;
    result.base = args.value("--base", environment("TANSR_BASE_URL"));
    if (result.base.empty())
        result.base = "http://127.0.0.1:8787";
    result.family = args.value("--family", "sdk1");
    if (result.family != "sdk1" && result.family != "sdk2-offload-v1")
        fail("family must be sdk1 or sdk2-offload-v1");
    auto seconds = args.value("--timeout", "600");
    std::size_t consumed = 0;
    unsigned long n = 0;
    try {
        n = std::stoul(seconds, &consumed);
    } catch (...) {
        fail("timeout must be 1..86400 seconds");
    }
    if (consumed != seconds.size() || n == 0 || n > 86400)
        fail("timeout must be 1..86400 seconds");
    result.timeout = std::chrono::seconds(n);
    result.credentials =
        Credentials::open(args.value("--token-file", environment("TANSR_TOKEN_FILE")),
                          args.value("--scope-file", environment("TANSR_SCOPE_FILE")));
    return result;
}
Host::Host(Configuration configuration) : config(std::move(configuration)) {
    runtime = take(tansr::Runtime::create());
    tansr::ClientOptions options;
    options.base_url = config.base;
    options.family = config.family;
    options.token_provider = [credentials = config.credentials](tansr::CancellationToken cancel) {
        return credentials->token(cancel);
    };
    api = take(tansr::ApiClient::create(std::move(options), runtime));
}
Host::~Host() {
    if (!closed_) {
        if (api)
            api->shutdown();
        if (runtime) {
            runtime->stop();
            auto result = runtime->shutdown(std::chrono::seconds(30));
            if (!result || result.value() != tansr::ShutdownStatus::stopped)
                std::cerr << "Runtime shutdown pending; not confirmed quiescent\n";
        }
    }
}
void Host::close() {
    api->shutdown();
    runtime->stop();
    auto status = take(runtime->shutdown(std::chrono::seconds(30)));
    if (status != tansr::ShutdownStatus::stopped)
        fail("runtime shutdown still pending", tansr::ErrorCode::unknown);
    closed_ = true;
}
tansr::Json intent_owner(const Host &host) {
    const auto &scope = host.config.credentials->scope();
    return tansr::Json::object({{"format", "tansr-cpp-demo-owner-v1"},
                                {"base", host.config.base},
                                {"family", host.config.family},
                                {"applicationScopeId", scope.application_scope_id},
                                {"endUserId", scope.end_user_id},
                                {"authorizationRevision", scope.authorization_revision}});
}
std::unique_ptr<tansr::storage::PrivateDirectory>
private_directory(const std::filesystem::path &path, const Host &host) {
    take(tansr::storage::create_private_directory(path));
    auto credentials = host.config.credentials;
    return take(tansr::storage::PrivateDirectory::open(
        path, [credentials] { return credentials->check(); }));
}
Stop::Stop(std::chrono::seconds timeout) {
    interrupted = 0;
    std::signal(SIGINT, signal_handler);
    watcher_ = std::thread([this, timeout] {
        const auto end = std::chrono::steady_clock::now() + timeout;
        while (!finished_.load()) {
            if (interrupted || std::chrono::steady_clock::now() >= end) {
                source_.cancel();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
}
Stop::~Stop() {
    finished_ = true;
    source_.cancel();
    if (watcher_.joinable())
        watcher_.join();
    std::signal(SIGINT, SIG_DFL);
}
tansr::session::WriteOptions write_options(tansr::CancellationToken cancel) {
    return {request_id(), tansr::unix_time_ms() + 30000, cancel};
}
tansr::CallOptions call_options(tansr::CancellationToken cancel,
                                std::optional<std::int64_t> deadline) {
    tansr::CallOptions options;
    options.cancel = cancel;
    options.deadline_ms = deadline.value_or(tansr::unix_time_ms() + 30000);
    return options;
}
tansr::crypto::Aes256Key archive_key() { return archive_key("TANSR_ARCHIVE_KEY_FILE"); }
tansr::crypto::Aes256Key archive_key(const char *environment_name) {
    const auto path = absolute_path(environment(environment_name));
    auto bytes =
        take(tansr::storage::read_private_file(path, 128, [] { return tansr::Result<void>{}; }));
    if (!bytes)
        fail("archive key missing");
    auto hex = trim_line(*bytes);
    if (hex.size() != 64)
        fail("archive key must contain 64 hexadecimal digits");
    tansr::crypto::Aes256Key key{};
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < key.size(); ++i) {
        int a = digit(hex[2 * i]), b = digit(hex[2 * i + 1]);
        if (a < 0 || b < 0)
            fail("archive key must contain hexadecimal digits");
        key[i] = static_cast<std::uint8_t>((a << 4) | b);
    }
    return key;
}
Console::Console() {
#ifdef _WIN32
    handle_ = GetStdHandle(STD_INPUT_HANDLE);
    console_ = GetConsoleMode(handle_, &original_mode_) != 0;
    if (console_)
        SetConsoleMode(handle_, original_mode_ & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
#endif
}
Console::~Console() {
#ifdef _WIN32
    if (console_)
        SetConsoleMode(handle_, original_mode_);
#endif
}
std::optional<std::string> Console::poll() {
    auto buffered = [this]() -> std::optional<std::string> {
        const auto end = pending_.find('\n');
        if (end == std::string::npos)
            return {};
        auto result = trim_line(pending_.substr(0, end));
        pending_.erase(0, end + 1);
        if (!tansr::valid_utf8(result))
            fail("console input must be UTF-8");
        return result;
    };
    if (auto line = buffered())
        return line;
    if (ended_)
        return {};
#ifdef _WIN32
    if (console_) {
        DWORD available = 0;
        if (!GetNumberOfConsoleInputEvents(handle_, &available)) {
            ended_ = true;
            return {};
        }
        while (available--) {
            INPUT_RECORD event{};
            DWORD n = 0;
            if (!ReadConsoleInputW(handle_, &event, 1, &n) || n == 0)
                break;
            if (event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown)
                continue;
            const wchar_t c = event.Event.KeyEvent.uChar.UnicodeChar;
            if (c == L'\r') {
                std::cout << '\n';
                int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_.data(),
                                               static_cast<int>(wide_.size()), nullptr, 0, nullptr,
                                               nullptr);
                if (size == 0 && !wide_.empty())
                    fail("console input is not valid Unicode");
                std::string line(static_cast<std::size_t>(size), '\0');
                WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_.data(),
                                    static_cast<int>(wide_.size()), line.data(), size, nullptr,
                                    nullptr);
                wide_.clear();
                return line;
            }
            if (c == 8) {
                if (!wide_.empty()) {
                    wide_.pop_back();
                    std::cout << "\b \b";
                }
                continue;
            }
            if (c >= 32 || c == L'\t') {
                wide_ += c;
                DWORD written = 0;
                WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), &c, 1, &written, nullptr);
                if (wide_.size() > 131072)
                    fail("console line exceeds bound");
            }
        }
        return {};
    }
    DWORD available = 0;
    const auto type = GetFileType(handle_);
    if (type == FILE_TYPE_PIPE) {
        if (!PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) {
            ended_ = true;
            return {};
        }
        if (!available)
            return {};
    }
    char bytes[4096]{};
    DWORD got = 0;
    if (!ReadFile(handle_, bytes, static_cast<DWORD>(sizeof(bytes)), &got, nullptr) || got == 0) {
        ended_ = true;
        if (!pending_.empty()) {
            auto result = std::move(pending_);
            pending_.clear();
            if (!tansr::valid_utf8(result))
                fail("console input must be UTF-8");
            return result;
        }
        return {};
    }
#else
    pollfd descriptor{STDIN_FILENO, POLLIN, 0};
    int ready = ::poll(&descriptor, 1, 0);
    if (ready <= 0)
        return {};
    char bytes[4096]{};
    const auto got = ::read(STDIN_FILENO, bytes, sizeof(bytes));
    if (got <= 0) {
        ended_ = true;
        if (!pending_.empty()) {
            auto result = std::move(pending_);
            pending_.clear();
            if (!tansr::valid_utf8(result))
                fail("console input must be UTF-8");
            return result;
        }
        return {};
    }
#endif
    pending_.append(bytes, static_cast<std::size_t>(got));
    if (pending_.size() > 262144)
        fail("console buffer exceeds 256 KiB");
    return buffered();
}
} // namespace demo
