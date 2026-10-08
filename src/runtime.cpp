#include "tansr/runtime.hpp"
#include "time.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <curl/curl.h>
#include <deque>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace tansr {
namespace {
thread_local const void *runtime_thread = nullptr;
constexpr auto cancellation_poll = std::chrono::milliseconds(25);
class RuntimeThreadScope {
  public:
    explicit RuntimeThreadScope(const void *current) : previous_(runtime_thread) {
        runtime_thread = current;
    }
    ~RuntimeThreadScope() { runtime_thread = previous_; }

  private:
    const void *previous_;
};

// 只统计 SDK 自己取得的引用；借用模式不取得、也不清理宿主的引用。
class CurlGlobal {
  public:
    static Result<std::shared_ptr<CurlGlobal>> acquire(bool borrowed) {
        auto guard = std::shared_ptr<CurlGlobal>(new CurlGlobal(borrowed));
        if (!borrowed) {
            std::lock_guard<std::mutex> lock(global_mutex());
            if (owners() == 0 && curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                return Error{ErrorCode::internal, "curl global initialization failed"};
            ++owners();
            guard->acquired_ = true;
        }
        return guard;
    }
    ~CurlGlobal() {
        if (acquired_) {
            std::lock_guard<std::mutex> lock(global_mutex());
            if (--owners() == 0)
                curl_global_cleanup();
        }
    }

  private:
    explicit CurlGlobal(bool) {}
    static std::mutex &global_mutex() {
        static std::mutex value;
        return value;
    }
    static std::size_t &owners() {
        static std::size_t value = 0;
        return value;
    }
    bool acquired_{false};
};

bool token_character(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(ch)) !=
               std::string_view::npos;
}

Result<void> validate_request(const HttpRequest &request) {
    if (request.method.empty() ||
        !std::all_of(request.method.begin(), request.method.end(),
                     [](unsigned char ch) { return token_character(ch); }))
        return Error{ErrorCode::invalid_input, "invalid HTTP method"};
    if (request.url.empty() || request.url.size() > 64U * 1024U ||
        request.url.find_first_of("\r\n\t ") != std::string::npos ||
        request.url.find('\0') != std::string::npos || request.url.find('#') != std::string::npos ||
        (request.url.compare(0, 7, "http://") != 0 && request.url.compare(0, 8, "https://") != 0))
        return Error{ErrorCode::invalid_input, "invalid HTTP URL"};
    if (request.max_header_bytes == 0 || request.max_response_bytes == 0)
        return Error{ErrorCode::invalid_input, "HTTP byte limits must be positive"};
    std::size_t bytes = 0;
    for (const auto &header : request.headers) {
        if (header.first.empty() ||
            !std::all_of(header.first.begin(), header.first.end(),
                         [](unsigned char ch) { return token_character(ch); }) ||
            header.second.find_first_of("\r\n") != std::string::npos ||
            header.second.find('\0') != std::string::npos)
            return Error{ErrorCode::invalid_input, "invalid HTTP header"};
        if (header.first.size() >
            request.max_header_bytes - std::min(bytes, request.max_header_bytes))
            return Error{ErrorCode::capacity, "request headers exceed byte limit"};
        bytes += header.first.size();
        if (header.second.size() > request.max_header_bytes - bytes)
            return Error{ErrorCode::capacity, "request headers exceed byte limit"};
        bytes += header.second.size();
    }
    return {};
}

Error curl_error(CURLcode code) {
    if (code == CURLE_OPERATION_TIMEDOUT)
        return {ErrorCode::timeout, "HTTP deadline exceeded"};
    if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CONNECT_ERROR ||
        code == CURLE_SSL_CERTPROBLEM || code == CURLE_SSL_CACERT_BADFILE)
        return {ErrorCode::tls, "TLS verification or handshake failed"};
    return {ErrorCode::network, "HTTP transport failed"};
}
struct CommandQueue;
} // namespace

struct RequestHandle::State {
    HttpRequest request;
    CancellationToken cancellation;
    std::function<void(Result<HttpResponse>)> callback;
    std::weak_ptr<CommandQueue> owner;
    const void *owner_tag{nullptr};
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
    std::condition_variable cv;
    HttpResponse response;
    std::optional<Result<HttpResponse>> result;
    std::optional<Result<void>> callback_result;
    bool streaming{false};
    bool headers_ready{false};
    std::deque<std::string> chunks;
    std::size_t queued_bytes{0};
    std::size_t queue_limit{0};
    // 下列 curl/解析状态仅由 I/O owner 访问。
    CURL *easy{nullptr};
    curl_slist *headers{nullptr};
    std::size_t header_bytes{0};
    std::size_t body_bytes{0};
    std::chrono::steady_clock::time_point last_activity{std::chrono::steady_clock::now()};
    bool callback_exception{false};
    bool paused{false};
    bool published_headers{false};
    std::optional<Error> failure;
};

namespace {
struct CommandQueue {
    std::mutex mutex;
    std::condition_variable dispatch_cv;
    std::condition_variable lifecycle_cv;
    std::deque<std::shared_ptr<RequestHandle::State>> submitted;
    std::deque<std::weak_ptr<RequestHandle::State>> cancellations;
    std::deque<std::shared_ptr<RequestHandle::State>> dispatch;
    CURLM *multi{nullptr};
    std::atomic<bool> stopping{false};
    bool initialized{false};
    bool io_done{false};
    std::size_t workers_done{0};
    std::size_t outstanding{0};
    std::size_t callbacks_reserved{0};
    std::shared_ptr<CallbackDispatcher> dispatcher;
    std::size_t capacity{0};
    std::optional<Error> startup_error;

    void wake() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        if (multi)
            (void)curl_multi_wakeup(multi);
    }
    void cancel(const std::shared_ptr<RequestHandle::State> &state) noexcept {
        state->cancelled.store(true, std::memory_order_release);
        // 即使提示队列满，owner 仍扫描每个请求的原子取消位，绝不丢失取消。
        try {
            std::lock_guard<std::mutex> lock(mutex);
            if (cancellations.size() < capacity)
                cancellations.emplace_back(state);
            if (multi)
                (void)curl_multi_wakeup(multi);
        } catch (...) {
            wake();
        }
        state->cv.notify_all();
    }
};

// 宿主可以复制或重复调用工作；一次性票据保证用户完成通知最多执行一次。
// 最后一个副本销毁前始终占用 admission 预算，不能靠迁入宿主队列绕开容量。
class CallbackTicket {
  public:
    CallbackTicket(std::shared_ptr<RequestHandle::State> state, std::shared_ptr<CommandQueue> owner)
        : state_(std::move(state)), owner_(std::move(owner)) {}
    ~CallbackTicket() {
        if (!claimed_.load(std::memory_order_acquire))
            reject(Error{ErrorCode::closed, "completion callback discarded by dispatcher"});
        {
            std::lock_guard<std::mutex> lock(owner_->mutex);
            --owner_->callbacks_reserved;
        }
        owner_->lifecycle_cv.notify_all();
    }
    void run() noexcept {
        if (claimed_.exchange(true, std::memory_order_acq_rel))
            return;
        RuntimeThreadScope scope(state_->owner_tag);
        std::optional<Error> error;
        try {
            Result<HttpResponse> result = [&] {
                std::lock_guard<std::mutex> lock(state_->mutex);
                return *state_->result;
            }();
            auto callback = std::move(state_->callback);
            callback(std::move(result));
        } catch (...) {
            error = Error{ErrorCode::internal, "HTTP completion callback threw"};
        }
        finish(std::move(error));
    }
    void reject(Error error) noexcept {
        if (claimed_.exchange(true, std::memory_order_acq_rel))
            return;
        RuntimeThreadScope scope(state_->owner_tag);
        finish(std::move(error));
    }

  private:
    void finish(std::optional<Error> error) noexcept {
        // 目标对象的析构也在 callback 生命周期内，先释放再公布投递终态。
        state_->callback = {};
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (error)
                state_->callback_result.emplace(std::move(*error));
            else
                state_->callback_result.emplace();
        }
        state_->cv.notify_all();
    }
    std::shared_ptr<RequestHandle::State> state_;
    std::shared_ptr<CommandQueue> owner_;
    std::atomic<bool> claimed_{false};
};

void cancel_state(const std::shared_ptr<RequestHandle::State> &state) noexcept {
    if (!state)
        return;
    state->cancelled.store(true, std::memory_order_release);
    if (const auto owner = state->owner.lock())
        owner->cancel(state);
    state->cv.notify_all();
}

class CurlByteStream final : public ByteStream {
  public:
    CurlByteStream(std::shared_ptr<RequestHandle::State> state, HttpResponse response)
        : state_(std::move(state)), response_(std::move(response)) {}
    ~CurlByteStream() override { cancel(); }
    Result<std::optional<std::string>> next(CancellationToken cancellation) override {
        if (runtime_thread == state_->owner_tag)
            return Error{ErrorCode::reentrant, "cannot wait for HTTP on its runtime thread"};
        std::unique_lock<std::mutex> lock(state_->mutex);
        for (;;) {
            if (cancellation.is_cancelled() || state_->cancelled.load(std::memory_order_acquire) ||
                state_->cancellation.is_cancelled()) {
                lock.unlock();
                cancel();
                return Error{ErrorCode::cancelled, "HTTP stream cancelled"};
            }
            if (!state_->chunks.empty()) {
                auto bytes = std::move(state_->chunks.front());
                state_->chunks.pop_front();
                state_->queued_bytes -= bytes.size();
                lock.unlock();
                if (auto owner = state_->owner.lock())
                    owner->wake();
                return std::optional<std::string>{std::move(bytes)};
            }
            if (state_->result) {
                if (!*state_->result)
                    return state_->result->error();
                return std::optional<std::string>{};
            }
            state_->cv.wait_for(lock, cancellation_poll);
        }
    }
    void cancel() noexcept override { cancel_state(state_); }
    const HttpResponse &response() const noexcept override { return response_; }

  private:
    std::shared_ptr<RequestHandle::State> state_;
    HttpResponse response_;
};
} // namespace

struct Runtime::Impl : std::enable_shared_from_this<Runtime::Impl> {
    RuntimeOptions options;
    std::shared_ptr<CurlGlobal> global;
    std::shared_ptr<CommandQueue> commands{std::make_shared<CommandQueue>()};
    std::thread io;
    std::vector<std::thread> workers;
    std::mutex join_mutex;
    std::unordered_map<CURL *, std::shared_ptr<RequestHandle::State>> active;

    Impl(RuntimeOptions value, std::shared_ptr<CurlGlobal> guard)
        : options(std::move(value)), global(std::move(guard)) {
        commands->capacity = options.max_pending_requests;
        commands->dispatcher = options.dispatcher;
    }

    static size_t write_callback(char *data, size_t size, size_t count, void *context) noexcept {
        auto &state = *static_cast<RequestHandle::State *>(context);
        try {
            if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
                return 0;
            const std::size_t length = size * count;
            if (length == 0)
                return 0;
            if (state.cancelled.load(std::memory_order_acquire) ||
                state.cancellation.is_cancelled())
                return 0;
            const bool body_limited = !state.streaming || state.response.status >= 400;
            if (length > CURL_MAX_WRITE_SIZE ||
                (body_limited && length > state.request.max_response_bytes - state.body_bytes)) {
                state.failure =
                    Error{ErrorCode::capacity, "response body or chunk exceeds byte limit"};
                return 0;
            }
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.streaming) {
                if (length > state.queue_limit - state.queued_bytes) {
                    state.paused = true;
                    return CURL_WRITEFUNC_PAUSE;
                }
                state.chunks.emplace_back(data, length);
                state.queued_bytes += length;
            } else {
                state.response.body.append(data, length);
            }
            if (body_limited)
                state.body_bytes += length;
            state.last_activity = std::chrono::steady_clock::now();
            state.cv.notify_all();
            return length;
        } catch (...) {
            // C ABI 边界绝不传播异常；最终映射为固定、脱敏网络错误。
            state.callback_exception = true;
            return 0;
        }
    }

    static size_t header_callback(char *data, size_t size, size_t count, void *context) noexcept {
        auto &state = *static_cast<RequestHandle::State *>(context);
        try {
            if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
                return 0;
            const std::size_t length = size * count;
            if (length > state.request.max_header_bytes - state.header_bytes) {
                state.failure = Error{ErrorCode::capacity, "response headers exceed byte limit"};
                return 0;
            }
            state.header_bytes += length;
            state.last_activity = std::chrono::steady_clock::now();
            if (state.published_headers)
                return length; // trailer 不修改已发布的响应头快照。
            std::string_view line(data, length);
            std::lock_guard<std::mutex> lock(state.mutex);
            if (line.compare(0, 5, "HTTP/") == 0) {
                const auto space = line.find(' ');
                if (space == std::string_view::npos || line.size() < space + 4)
                    return 0;
                int status = 0;
                for (std::size_t i = space + 1; i < space + 4; ++i) {
                    if (line[i] < '0' || line[i] > '9')
                        return 0;
                    status = status * 10 + (line[i] - '0');
                }
                state.response.status = status;
                state.response.headers.clear();
            } else if (line == "\r\n" || line == "\n") {
                if (state.response.status >= 200) {
                    if (state.response.status >= 300 && state.response.status < 400) {
                        state.failure = Error{ErrorCode::http, "HTTP redirects are not allowed"};
                        state.failure->http_status = state.response.status;
                        return 0;
                    }
                    state.published_headers = true;
                    state.headers_ready = true;
                    state.cv.notify_all();
                }
            } else {
                const auto colon = line.find(':');
                if (colon == std::string_view::npos)
                    return 0;
                auto value = line.substr(colon + 1);
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                    value.remove_prefix(1);
                while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                                          value.back() == ' ' || value.back() == '\t'))
                    value.remove_suffix(1);
                state.response.headers.emplace_back(std::string(line.substr(0, colon)),
                                                    std::string(value));
            }
            return length;
        } catch (...) {
            state.callback_exception = true;
            return 0;
        }
    }

    static int progress_callback(void *context, curl_off_t, curl_off_t, curl_off_t,
                                 curl_off_t) noexcept {
        const auto &state = *static_cast<RequestHandle::State *>(context);
        return state.cancelled.load(std::memory_order_acquire) || state.cancellation.is_cancelled()
                   ? 1
                   : 0;
    }

    Result<void> configure(RequestHandle::State &state) {
        state.last_activity = std::chrono::steady_clock::now();
        state.easy = curl_easy_init();
        if (!state.easy)
            return Error{ErrorCode::internal, "curl request initialization failed"};
        CURLU *parsed = curl_url();
        if (!parsed)
            return Error{ErrorCode::internal, "curl URL initialization failed"};
        const auto parsed_code = curl_url_set(parsed, CURLUPART_URL, state.request.url.c_str(), 0);
        char *user = nullptr;
        char *password = nullptr;
        const bool credentials =
            curl_url_get(parsed, CURLUPART_USER, &user, 0) == CURLUE_OK ||
            curl_url_get(parsed, CURLUPART_PASSWORD, &password, 0) == CURLUE_OK;
        curl_free(user);
        curl_free(password);
        curl_url_cleanup(parsed);
        if (parsed_code != CURLUE_OK || credentials)
            return Error{ErrorCode::invalid_input, "invalid HTTP URL or embedded credentials"};
        CURLcode code = CURLE_OK;
        auto set = [&](CURLoption option, auto value) {
            if (code == CURLE_OK)
                code = curl_easy_setopt(state.easy, option, value);
        };
        set(CURLOPT_URL, state.request.url.c_str());
        set(CURLOPT_CUSTOMREQUEST, state.request.method.c_str());
        set(CURLOPT_PROTOCOLS_STR, "http,https");
        set(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
        set(CURLOPT_FOLLOWLOCATION, 0L);
        set(CURLOPT_MAXREDIRS, 0L);
        set(CURLOPT_SSL_VERIFYPEER, 1L);
        set(CURLOPT_SSL_VERIFYHOST, 2L);
        set(CURLOPT_NOSIGNAL, 1L);
        set(CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED));
        set(CURLOPT_UNRESTRICTED_AUTH, 0L);
        set(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
        // 写请求不使用陈旧复用连接，避免 curl 在复用连接失效时隐式重发。
        if (state.request.method != "GET" && state.request.method != "HEAD") {
            set(CURLOPT_FRESH_CONNECT, 1L);
            set(CURLOPT_FORBID_REUSE, 1L);
        }
        set(CURLOPT_HTTP_CONTENT_DECODING, 0L);
        set(CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
        set(CURLOPT_BUFFERSIZE, static_cast<long>(CURL_MAX_WRITE_SIZE));
        set(CURLOPT_CONNECTTIMEOUT_MS,
            static_cast<long>(std::min<std::int64_t>(options.connect_timeout.count(), LONG_MAX)));
        set(CURLOPT_WRITEFUNCTION, &Impl::write_callback);
        set(CURLOPT_WRITEDATA, &state);
        set(CURLOPT_HEADERFUNCTION, &Impl::header_callback);
        set(CURLOPT_HEADERDATA, &state);
        set(CURLOPT_NOPROGRESS, 0L);
        set(CURLOPT_XFERINFOFUNCTION, &Impl::progress_callback);
        set(CURLOPT_XFERINFODATA, &state);
        // 显式空代理阻止环境中的代理规则改变本地/远端目标。
        set(CURLOPT_PROXY, options.proxy.c_str());
        if (!options.ca_file.empty())
            set(CURLOPT_CAINFO, options.ca_file.c_str());
        if (state.request.deadline_ms > 0) {
            const auto remaining = state.request.deadline_ms - unix_time_ms();
            if (remaining <= 0)
                return Error{ErrorCode::timeout, "HTTP deadline exceeded"};
            set(CURLOPT_TIMEOUT_MS, static_cast<long>(std::min<std::int64_t>(remaining, LONG_MAX)));
        }
        if (state.request.method == "HEAD")
            set(CURLOPT_NOBODY, 1L);
        if (!state.request.body.empty() || state.request.method == "POST" ||
            state.request.method == "PUT" || state.request.method == "PATCH") {
            set(CURLOPT_POSTFIELDS, state.request.body.data());
            set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(state.request.body.size()));
        }
        for (const auto &header : state.request.headers) {
            const auto line = header.first + ": " + header.second;
            auto *next = curl_slist_append(state.headers, line.c_str());
            if (!next)
                return Error{ErrorCode::internal, "HTTP header allocation failed"};
            state.headers = next;
        }
        set(CURLOPT_HTTPHEADER, state.headers);
        if (code != CURLE_OK)
            return Error{ErrorCode::internal, "curl request configuration failed"};
        return {};
    }

    void complete(const std::shared_ptr<RequestHandle::State> &state, std::optional<Error> error) {
        if (state->easy) {
            (void)curl_multi_remove_handle(commands->multi, state->easy);
            curl_easy_cleanup(state->easy);
            state->easy = nullptr;
        }
        if (state->headers) {
            curl_slist_free_all(state->headers);
            state->headers = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->result)
                return;
            if (error)
                state->result.emplace(std::move(*error));
            else
                state->result.emplace(std::move(state->response));
        }
        state->cv.notify_all();
        {
            std::lock_guard<std::mutex> lock(commands->mutex);
            --commands->outstanding;
            if (state->callback)
                commands->dispatch.emplace_back(state);
        }
        commands->dispatch_cv.notify_all();
    }

    void run_io() noexcept {
        runtime_thread = this;
        try {
            auto *multi = curl_multi_init();
            {
                std::lock_guard<std::mutex> lock(commands->mutex);
                commands->multi = multi;
                commands->initialized = true;
                if (!multi)
                    commands->startup_error =
                        Error{ErrorCode::internal, "curl multi initialization failed"};
            }
            commands->lifecycle_cv.notify_all();
            if (multi)
                io_loop();
        } catch (...) {
            commands->stopping.store(true, std::memory_order_release);
            // 系统资源耗尽与意外实现异常不会穿过线程入口。
            try {
                for (auto &item : active)
                    complete(item.second, Error{ErrorCode::internal, "HTTP runtime failed"});
                active.clear();
                std::deque<std::shared_ptr<RequestHandle::State>> pending;
                {
                    std::lock_guard<std::mutex> lock(commands->mutex);
                    pending.swap(commands->submitted);
                }
                for (auto &state : pending)
                    complete(state, Error{ErrorCode::internal, "HTTP runtime failed"});
            } catch (...) {
                std::terminate();
            }
        }
        {
            std::lock_guard<std::mutex> lock(commands->mutex);
            if (commands->multi) {
                curl_multi_cleanup(commands->multi);
                commands->multi = nullptr;
            }
            commands->io_done = true;
            commands->initialized = true;
        }
        commands->dispatch_cv.notify_all();
        commands->lifecycle_cv.notify_all();
        runtime_thread = nullptr;
    }

    void io_loop() {
        for (;;) {
            std::deque<std::shared_ptr<RequestHandle::State>> pending;
            {
                std::lock_guard<std::mutex> lock(commands->mutex);
                pending.swap(commands->submitted);
                commands->cancellations.clear();
            }
            for (auto &state : pending) {
                if (commands->stopping.load(std::memory_order_acquire) ||
                    state->cancelled.load(std::memory_order_acquire) ||
                    state->cancellation.is_cancelled()) {
                    complete(state, Error{ErrorCode::cancelled, "HTTP request cancelled"});
                    continue;
                }
                auto configured = configure(*state);
                if (!configured) {
                    complete(state, configured.error());
                    continue;
                }
                if (curl_multi_add_handle(commands->multi, state->easy) != CURLM_OK) {
                    complete(state, Error{ErrorCode::internal, "curl request scheduling failed"});
                    continue;
                }
                active.emplace(state->easy, state);
            }
            for (auto iter = active.begin(); iter != active.end();) {
                auto state = iter->second;
                std::optional<Error> error;
                if (commands->stopping.load(std::memory_order_acquire) ||
                    state->cancelled.load(std::memory_order_acquire) ||
                    state->cancellation.is_cancelled())
                    error = Error{ErrorCode::cancelled, "HTTP request cancelled"};
                else if (state->request.deadline_ms > 0 &&
                         unix_time_ms() >= state->request.deadline_ms)
                    error = Error{ErrorCode::timeout, "HTTP deadline exceeded"};
                else if (state->streaming && !state->paused &&
                         std::chrono::steady_clock::now() - state->last_activity >=
                             options.stream_idle_timeout)
                    error = Error{ErrorCode::timeout, "HTTP stream idle timeout"};
                if (error) {
                    iter = active.erase(iter);
                    complete(state, std::move(error));
                    continue;
                }
                bool resume = false;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    resume = state->paused &&
                             state->queue_limit - state->queued_bytes >= CURL_MAX_WRITE_SIZE;
                }
                if (resume) {
                    state->paused = false;
                    state->last_activity = std::chrono::steady_clock::now();
                    const auto code = curl_easy_pause(state->easy, CURLPAUSE_CONT);
                    if (code != CURLE_OK) {
                        iter = active.erase(iter);
                        complete(state, curl_error(code));
                        continue;
                    }
                }
                ++iter;
            }
            int running = 0;
            const auto performed = curl_multi_perform(commands->multi, &running);
            if (performed != CURLM_OK)
                throw std::runtime_error("curl multi failed");
            int remaining = 0;
            while (auto *message = curl_multi_info_read(commands->multi, &remaining)) {
                if (message->msg != CURLMSG_DONE)
                    continue;
                auto iter = active.find(message->easy_handle);
                if (iter == active.end())
                    continue;
                auto state = iter->second;
                const auto code = message->data.result;
                active.erase(iter);
                std::optional<Error> error = state->failure;
                if (state->callback_exception)
                    error = Error{ErrorCode::internal, "HTTP buffer allocation failed"};
                if (state->cancelled.load(std::memory_order_acquire) ||
                    state->cancellation.is_cancelled())
                    error = Error{ErrorCode::cancelled, "HTTP request cancelled"};
                else if (!error && code != CURLE_OK)
                    error = curl_error(code);
                complete(state, std::move(error));
            }
            {
                std::lock_guard<std::mutex> lock(commands->mutex);
                if (commands->stopping.load(std::memory_order_acquire) && active.empty() &&
                    commands->submitted.empty())
                    break;
            }
            int descriptors = 0;
            if (curl_multi_poll(commands->multi, nullptr, 0,
                                static_cast<int>(cancellation_poll.count()),
                                &descriptors) != CURLM_OK)
                throw std::runtime_error("curl poll failed");
        }
    }

    void run_dispatch() noexcept {
        runtime_thread = this;
        for (;;) {
            std::shared_ptr<RequestHandle::State> state;
            {
                std::unique_lock<std::mutex> lock(commands->mutex);
                commands->dispatch_cv.wait(
                    lock, [&] { return !commands->dispatch.empty() || commands->io_done; });
                if (commands->dispatch.empty() && commands->io_done)
                    break;
                state = std::move(commands->dispatch.front());
                commands->dispatch.pop_front();
            }
            std::shared_ptr<CallbackTicket> ticket;
            try {
                ticket = std::make_shared<CallbackTicket>(state, commands);
            } catch (...) {
                // 回调调度资源失败与已经完成的 HTTP 结果分列，不能杀死整个工作池。
                state->callback = {};
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->callback_result.emplace(Error{ErrorCode::internal, "dispatch OOM"});
                }
                state->cv.notify_all();
                {
                    std::lock_guard<std::mutex> lock(commands->mutex);
                    --commands->callbacks_reserved;
                }
                commands->lifecycle_cv.notify_all();
                continue;
            }
            if (options.dispatcher) {
                try {
                    if (!options.dispatcher->try_post([ticket] {
                            // 即使宿主从正在执行的回调中清理当前函数对象，也保活票据。
                            auto running = ticket;
                            running->run();
                        }))
                        ticket->reject(
                            Error{ErrorCode::capacity, "completion dispatcher rejected callback"});
                } catch (...) {
                    ticket->reject(Error{ErrorCode::internal, "completion dispatcher threw"});
                }
            } else
                ticket->run();
        }
        {
            std::lock_guard<std::mutex> lock(commands->mutex);
            ++commands->workers_done;
        }
        commands->lifecycle_cv.notify_all();
        runtime_thread = nullptr;
    }

    void start() {
        auto self = shared_from_this();
        try {
            for (std::size_t i = 0; i < options.dispatch_threads; ++i)
                workers.emplace_back([self] { self->run_dispatch(); });
            io = std::thread([self] { self->run_io(); });
        } catch (...) {
            commands->stopping.store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(commands->mutex);
                commands->io_done = true;
            }
            commands->dispatch_cv.notify_all();
            for (auto &worker : workers)
                if (worker.joinable())
                    worker.join();
            throw;
        }
        std::unique_lock<std::mutex> lock(commands->mutex);
        commands->lifecycle_cv.wait(lock, [&] { return commands->initialized; });
    }

    Result<std::shared_ptr<RequestHandle::State>>
    submit(HttpRequest request, CancellationToken cancellation,
           std::function<void(Result<HttpResponse>)> callback, bool streaming) {
        auto valid = validate_request(request);
        if (!valid)
            return valid.error();
        if (cancellation.is_cancelled())
            return Error{ErrorCode::cancelled, "HTTP request cancelled"};
        if (request.deadline_ms > 0 && request.deadline_ms <= unix_time_ms())
            return Error{ErrorCode::timeout, "HTTP deadline exceeded"};
        auto state = std::make_shared<RequestHandle::State>();
        state->request = std::move(request);
        state->cancellation = std::move(cancellation);
        state->callback = std::move(callback);
        if (!state->callback)
            state->callback_result.emplace();
        state->streaming = streaming;
        state->queue_limit = options.max_stream_queue_bytes;
        state->owner = commands;
        state->owner_tag = this;
        {
            std::lock_guard<std::mutex> lock(commands->mutex);
            if (commands->stopping.load(std::memory_order_acquire))
                return Error{ErrorCode::closed, "HTTP runtime is stopped"};
            if (commands->outstanding >= options.max_pending_requests ||
                (state->callback && commands->callbacks_reserved >= options.max_dispatch_queue))
                return Error{ErrorCode::capacity, "HTTP runtime queue is full"};
            commands->submitted.emplace_back(state);
            ++commands->outstanding;
            if (state->callback)
                ++commands->callbacks_reserved;
            if (commands->multi)
                (void)curl_multi_wakeup(commands->multi);
        }
        return state;
    }

    void stop() noexcept {
        commands->stopping.store(true, std::memory_order_release);
        commands->wake();
    }

    Result<ShutdownStatus> shutdown(std::chrono::milliseconds timeout) {
        if (runtime_thread == this ||
            (options.dispatcher && options.dispatcher->is_dispatch_thread()))
            return Error{ErrorCode::reentrant, "cannot shutdown from a runtime dispatcher thread"};
        stop();
        const auto end = detail::deadline_after(timeout);
        for (;;) {
            bool internal_idle = false;
            {
                std::lock_guard<std::mutex> lock(commands->mutex);
                internal_idle = commands->io_done && commands->workers_done == workers.size() &&
                                commands->callbacks_reserved == 0;
            }
            // 宿主代码只在调用线程/dispatch worker 上执行，绝不从 I/O owner 查询。
            if (internal_idle && (!options.dispatcher || options.dispatcher->is_idle()))
                break;
            const auto now = std::chrono::steady_clock::now();
            if (now >= end)
                return ShutdownStatus::pending;
            std::unique_lock<std::mutex> lock(commands->mutex);
            commands->lifecycle_cv.wait_until(lock, std::min(end, now + cancellation_poll));
        }
        std::lock_guard<std::mutex> lock(join_mutex);
        if (io.joinable())
            io.join();
        for (auto &worker : workers)
            if (worker.joinable())
                worker.join();
        return ShutdownStatus::stopped;
    }
};

ByteStream::~ByteStream() = default;
HttpTransport::~HttpTransport() = default;
CallbackDispatcher::~CallbackDispatcher() = default;
RequestHandle::RequestHandle(std::shared_ptr<State> state) : state_(std::move(state)) {}
void RequestHandle::cancel() noexcept { cancel_state(state_); }
bool RequestHandle::ready() const noexcept {
    if (!state_)
        return true;
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->result.has_value();
}
Result<HttpResponse> RequestHandle::wait() {
    if (!state_)
        return Error{ErrorCode::closed, "empty HTTP request handle"};
    if (runtime_thread == state_->owner_tag)
        return Error{ErrorCode::reentrant, "cannot wait for HTTP on its runtime thread"};
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->cv.wait(lock, [&] { return state_->result.has_value(); });
    return *state_->result;
}
bool RequestHandle::callback_ready() const noexcept {
    if (!state_)
        return true;
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->callback_result.has_value();
}
Result<void> RequestHandle::wait_callback() {
    if (!state_)
        return Error{ErrorCode::closed, "empty HTTP request handle"};
    const auto owner = state_->owner.lock();
    if (runtime_thread == state_->owner_tag ||
        (owner && owner->dispatcher && owner->dispatcher->is_dispatch_thread()))
        return Error{ErrorCode::reentrant, "cannot wait for callback on its dispatcher thread"};
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->cv.wait(lock, [&] { return state_->callback_result.has_value(); });
    return *state_->callback_result;
}

Runtime::Runtime(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
Result<std::shared_ptr<Runtime>> Runtime::create(RuntimeOptions options) {
    if (options.max_pending_requests == 0 || options.max_dispatch_queue == 0 ||
        options.dispatch_threads == 0 || options.max_stream_queue_bytes < CURL_MAX_WRITE_SIZE ||
        options.connect_timeout.count() <= 0 || options.stream_idle_timeout.count() <= 0)
        return Error{ErrorCode::invalid_input, "invalid HTTP runtime capacity"};
    try {
        auto guard = CurlGlobal::acquire(options.borrow_curl_global);
        if (!guard)
            return guard.error();
        auto impl = std::make_shared<Impl>(std::move(options), std::move(guard).value());
        auto runtime = std::shared_ptr<Runtime>(new Runtime(impl));
        impl->start();
        if (impl->commands->startup_error)
            return *impl->commands->startup_error;
        return runtime;
    } catch (...) {
        return Error{ErrorCode::internal, "HTTP runtime initialization failed"};
    }
}
Runtime::~Runtime() {
    if (!impl_)
        return;
    if (runtime_thread == impl_.get()) {
        std::fputs("tansr: final Runtime owner must be destroyed outside runtime callbacks\n",
                   stderr);
        std::terminate();
    }
    impl_->stop();
    // 析构没有假超时或 detached 工作；宿主需要有界等待时先调用 shutdown。
    std::lock_guard<std::mutex> lock(impl_->join_mutex);
    if (impl_->io.joinable())
        impl_->io.join();
    for (auto &worker : impl_->workers)
        if (worker.joinable())
            worker.join();
    for (;;) {
        bool no_callbacks = false;
        {
            std::lock_guard<std::mutex> state_lock(impl_->commands->mutex);
            no_callbacks = impl_->commands->callbacks_reserved == 0;
        }
        if (no_callbacks && (!impl_->options.dispatcher || impl_->options.dispatcher->is_idle()))
            break;
        if (impl_->options.dispatcher && impl_->options.dispatcher->is_dispatch_thread()) {
            std::fputs("tansr: host dispatcher must drain before final Runtime destruction\n",
                       stderr);
            std::terminate();
        }
        std::unique_lock<std::mutex> state_lock(impl_->commands->mutex);
        impl_->commands->lifecycle_cv.wait_for(state_lock, cancellation_poll);
    }
}
Result<RequestHandle> Runtime::request_async(HttpRequest request, CancellationToken cancellation,
                                             std::function<void(Result<HttpResponse>)> callback) {
    auto state =
        impl_->submit(std::move(request), std::move(cancellation), std::move(callback), false);
    if (!state)
        return state.error();
    return RequestHandle(std::move(state).value());
}
Result<HttpResponse> Runtime::request(const HttpRequest &request, CancellationToken cancellation) {
    if (runtime_thread == impl_.get())
        return Error{ErrorCode::reentrant, "cannot wait for HTTP on its runtime thread"};
    auto handle = request_async(request, std::move(cancellation));
    if (!handle)
        return handle.error();
    return handle.value().wait();
}
Result<std::shared_ptr<ByteStream>> Runtime::stream(const HttpRequest &request,
                                                    CancellationToken cancellation) {
    if (runtime_thread == impl_.get())
        return Error{ErrorCode::reentrant, "cannot wait for HTTP on its runtime thread"};
    auto submitted = impl_->submit(request, std::move(cancellation), {}, true);
    if (!submitted)
        return submitted.error();
    auto state = std::move(submitted).value();
    std::unique_lock<std::mutex> lock(state->mutex);
    state->cv.wait(lock, [&] { return state->headers_ready || state->result.has_value(); });
    if (state->result && !*state->result)
        return state->result->error();
    // 已完成的响应被移动进 result；否则此处冻结响应头，trailer 不再改变它。
    HttpResponse response = state->result ? state->result->value() : state->response;
    return std::shared_ptr<ByteStream>(
        std::make_shared<CurlByteStream>(std::move(state), std::move(response)));
}
void Runtime::stop() noexcept { impl_->stop(); }
Result<ShutdownStatus> Runtime::shutdown(std::chrono::milliseconds timeout) {
    return impl_->shutdown(timeout);
}
} // namespace tansr
