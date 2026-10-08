#include "tansr/runtime.hpp"
#include <curl/curl.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
using namespace tansr;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> value) {
    if (!value)
        throw std::runtime_error(value.error().message);
    return std::move(value).value();
}
struct ScopeExit {
    std::function<void()> action;
    ~ScopeExit() { action(); }
};

// GUI 式手动泵送适配器；只接收有界工作，不产生自己的后台线程。
class HostDispatcher final : public CallbackDispatcher {
  public:
    explicit HostDispatcher(std::size_t capacity = 4) : capacity_(capacity) {}
    bool try_post(std::function<void()> work) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++posts_;
        changed_.notify_all();
        if (throw_next_) {
            throw_next_ = false;
            throw std::runtime_error("dispatcher failure");
        }
        if (closed_ || queue_.size() >= capacity_)
            return false;
        queue_.push_back(std::move(work));
        return true;
    }
    bool is_dispatch_thread() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_ != 0 && executing_ == std::this_thread::get_id();
    }
    bool is_idle() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty() && retained_.empty() && active_ == 0 && !other_work_;
    }
    void wait_posts(std::size_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        require(changed_.wait_for(lock, 2s, [&] { return posts_ >= count; }),
                "host post did not arrive");
    }
    void run_one(bool twice = false, bool retain = false) {
        std::function<void()> work;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            require(!queue_.empty(), "host queue empty");
            work = std::move(queue_.front());
            queue_.pop_front();
            ++active_;
            executing_ = std::this_thread::get_id();
        }
        work();
        if (twice)
            work();
        if (retain) {
            std::lock_guard<std::mutex> lock(mutex_);
            retained_.push_back(work);
        }
        work = {}; // 先销毁函数副本，才能对外报告 idle。
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
        }
    }
    void drop_pending() { clear(false); }
    void release_copies() { clear(true); }
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
            other_work_ = false;
        }
        drop_pending();
        release_copies();
    }
    void throw_next() {
        std::lock_guard<std::mutex> lock(mutex_);
        throw_next_ = true;
    }
    void other_work(bool present) {
        std::lock_guard<std::mutex> lock(mutex_);
        other_work_ = present;
    }
    bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

  private:
    void clear(bool retained) {
        std::deque<std::function<void()>> removed;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            removed.swap(retained ? retained_ : queue_);
            ++active_;
        }
        removed.clear();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
        }
    }
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> queue_, retained_;
    std::size_t capacity_, posts_{0}, active_{0};
    std::thread::id executing_;
    bool closed_{false}, throw_next_{false}, other_work_{false};
};
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
void close_socket(Socket value) { closesocket(value); }
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
void close_socket(Socket value) { close(value); }
#endif

class Server {
  public:
    Server() {
#ifdef _WIN32
        WSADATA data{};
        require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "WSAStartup");
#endif
        socket_ = socket(AF_INET, SOCK_STREAM, 0);
        require(socket_ != invalid_socket, "create fixture socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        require(bind(socket_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0,
                "bind fixture");
#ifdef _WIN32
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        require(getsockname(socket_, reinterpret_cast<sockaddr *>(&address), &length) == 0,
                "fixture port");
        port_ = ntohs(address.sin_port);
        require(listen(socket_, 16) == 0, "listen fixture");
        thread_ = std::thread([this] { run(); });
    }
    ~Server() {
        stopping_.store(true);
        thread_.join();
        close_socket(socket_);
        for (auto &worker : workers_)
            worker.join();
#ifdef _WIN32
        WSACleanup();
#endif
    }
    std::string url(std::string path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }
    int requests() const { return requests_.load(); }

  private:
    static bool send_all(Socket socket, const std::string &bytes) {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
#ifdef _WIN32
            const int count =
                send(socket, bytes.data() + offset, static_cast<int>(bytes.size() - offset), 0);
#else
#ifdef MSG_NOSIGNAL
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const auto count = send(socket, bytes.data() + offset, bytes.size() - offset, flags);
#endif
            if (count <= 0)
                return false;
            offset += static_cast<std::size_t>(count);
        }
        return true;
    }
    void serve(Socket socket) {
        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 64U * 1024U) {
            const auto count = recv(socket, buffer, sizeof(buffer), 0);
            if (count <= 0) {
                close_socket(socket);
                return;
            }
            request.append(buffer, static_cast<std::size_t>(count));
        }
        ++requests_;
        if (request.find(" /redirect ") != std::string::npos) {
            (void)send_all(socket, "HTTP/1.1 302 Found\r\nLocation: " + url("/ok") +
                                       "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        } else if (request.find(" /idle ") != std::string::npos) {
            (void)send_all(
                socket,
                "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n");
            std::this_thread::sleep_for(350ms);
        } else if (request.find(" /stream ") != std::string::npos) {
            const std::string body(256U * 1024U, 'x');
            (void)send_all(socket,
                           "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
                               "\r\nConnection: close\r\n\r\n");
            (void)send_all(socket, body);
        } else {
            if (request.find(" /slow ") != std::string::npos)
                std::this_thread::sleep_for(250ms);
            if (request.find(" /headers ") != std::string::npos)
                (void)send_all(socket, "HTTP/1.1 200 OK\r\nX-Large: " + std::string(1024, 'x') +
                                           "\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
            else
                (void)send_all(socket, "HTTP/1.1 200 OK\r\nX-Fixture: yes\r\nContent-Length: "
                                       "2\r\nConnection: close\r\n\r\nok");
        }
        close_socket(socket);
    }
    void run() {
        while (!stopping_.load()) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(socket_, &readable);
            timeval timeout{};
            timeout.tv_usec = 20000;
#ifdef _WIN32
            const int selected = select(0, &readable, nullptr, nullptr, &timeout);
#else
            const int selected = select(socket_ + 1, &readable, nullptr, nullptr, &timeout);
#endif
            if (selected <= 0)
                continue;
            const auto client = accept(socket_, nullptr, nullptr);
            if (client != invalid_socket) {
#ifdef SO_NOSIGPIPE
                const int enabled = 1;
                (void)setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
                workers_.emplace_back([this, client] { serve(client); });
            }
        }
    }
    Socket socket_{invalid_socket};
    unsigned short port_{0};
    std::atomic<bool> stopping_{false};
    std::atomic<int> requests_{0};
    std::thread thread_;
    std::vector<std::thread> workers_;
};

HttpRequest get(const Server &server, const std::string &path) {
    HttpRequest request;
    request.method = "GET";
    request.url = server.url(path);
    request.deadline_ms = unix_time_ms() + 3000;
    return request;
}
void shutdown(const std::shared_ptr<Runtime> &runtime) {
    require(take(runtime->shutdown(3s)) == ShutdownStatus::stopped,
            "runtime did not become quiescent");
}

void request_and_validation(Server &server) {
    auto runtime = take(Runtime::create());
    auto response = take(runtime->request(get(server, "/ok")));
    require(response.status == 200 && response.body == "ok", "HTTP body/header owned response");
    const auto before = server.requests();
    auto invalid = get(server, "/ok");
    invalid.headers.emplace_back("Authorization", "secret\r\nInjected: value");
    auto result = runtime->request(invalid);
    require(!result && result.error().code == ErrorCode::invalid_input, "CRLF rejected");
    require(server.requests() == before, "invalid input emitted request");
    invalid = get(server, "/ok");
    invalid.url = "file:///etc/passwd";
    result = runtime->request(invalid);
    require(!result && result.error().code == ErrorCode::invalid_input, "non-HTTP rejected");
    shutdown(runtime);
}

void redirect_and_limits(Server &server) {
    auto runtime = take(Runtime::create());
    const auto before = server.requests();
    auto redirected = runtime->request(get(server, "/redirect"));
    require(!redirected && redirected.error().http_status == 302, "redirect must fail closed");
    require(server.requests() == before + 1, "redirect followed");
    auto request = get(server, "/ok");
    request.max_response_bytes = 1;
    auto limited = runtime->request(request);
    require(!limited && limited.error().code == ErrorCode::capacity, "body cap");
    request = get(server, "/headers");
    request.max_header_bytes = 128;
    limited = runtime->request(request);
    require(!limited && limited.error().code == ErrorCode::capacity, "header cap");
    shutdown(runtime);
}

void bounded_stream(Server &server) {
    RuntimeOptions options;
    options.max_stream_queue_bytes = CURL_MAX_WRITE_SIZE;
    auto runtime = take(Runtime::create(options));
    auto request = get(server, "/stream");
    request.max_response_bytes = 1; // 长流不是累计 ordinary response body。
    auto stream = take(runtime->stream(request));
    require(stream->response().status == 200, "stream metadata");
    std::this_thread::sleep_for(60ms); // 足够让有界队列暂停；随后必须完整恢复。
    std::size_t bytes = 0;
    for (;;) {
        auto chunk = take(stream->next());
        if (!chunk)
            break;
        require(chunk->size() <= CURL_MAX_WRITE_SIZE, "chunk bounded");
        require(chunk->find_first_not_of('x') == std::string::npos, "raw bytes changed");
        bytes += chunk->size();
    }
    require(bytes == 256U * 1024U, "paused stream lost or repeated bytes");
    stream.reset();
    shutdown(runtime);
}

void cancellation_and_deadline(Server &server) {
    auto runtime = take(Runtime::create());
    auto handle = take(runtime->request_async(get(server, "/slow")));
    handle.cancel();
    handle.cancel();
    auto result = handle.wait();
    require(!result && result.error().code == ErrorCode::cancelled && handle.ready(),
            "request cancellation");
    CancellationSource source;
    auto second = take(runtime->request_async(get(server, "/slow"), source.token()));
    source.cancel();
    result = second.wait();
    require(!result && result.error().code == ErrorCode::cancelled, "external token cancellation");
    auto request = get(server, "/slow");
    request.deadline_ms = unix_time_ms() + 30;
    result = runtime->request(request);
    require(!result && result.error().code == ErrorCode::timeout, "absolute request deadline");
    auto stream = take(runtime->stream(get(server, "/idle")));
    stream->cancel();
    auto chunk = stream->next();
    require(!chunk && chunk.error().code == ErrorCode::cancelled, "stream cancellation");
    shutdown(runtime);
}

void stream_idle(Server &server) {
    RuntimeOptions options;
    options.stream_idle_timeout = 50ms;
    auto runtime = take(Runtime::create(options));
    auto request = get(server, "/idle");
    request.deadline_ms = 0;
    auto stream = take(runtime->stream(request));
    auto chunk = stream->next();
    require(!chunk && chunk.error().code == ErrorCode::timeout,
            "SSE idle remains bounded without total deadline");
    shutdown(runtime);
}

void bounded_submission(Server &server) {
    RuntimeOptions options;
    options.max_pending_requests = 1;
    auto runtime = take(Runtime::create(options));
    auto first = take(runtime->request_async(get(server, "/slow")));
    auto excess = runtime->request_async(get(server, "/slow"));
    require(!excess && excess.error().code == ErrorCode::capacity, "request admission bound");
    first.cancel();
    (void)first.wait();
    shutdown(runtime);
}

void callback_reentry_and_pending(Server &server) {
    RuntimeOptions options;
    options.dispatch_threads = 1;
    options.max_dispatch_queue = 1;
    auto runtime = take(Runtime::create(options));
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, release = false;
    std::atomic<int> count{0};
    std::atomic<bool> reentry_ok{false};
    ScopeExit release_on_failure{[&] {
        {
            std::lock_guard<std::mutex> lock(mutex);
            release = true;
        }
        cv.notify_all();
        (void)runtime->shutdown(3s);
    }};
    auto handle =
        take(runtime->request_async(get(server, "/ok"), {}, [&](Result<HttpResponse> response) {
            const auto attempted_wait = runtime->request(get(server, "/ok"));
            const auto attempted_shutdown = runtime->shutdown(0ms);
            reentry_ok.store(response && !attempted_wait &&
                             attempted_wait.error().code == ErrorCode::reentrant &&
                             !attempted_shutdown &&
                             attempted_shutdown.error().code == ErrorCode::reentrant);
            ++count;
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return release; });
            runtime->stop(); // callback 自关闭请求是安全、非等待动作。
        }));
    {
        std::unique_lock<std::mutex> lock(mutex);
        require(cv.wait_for(lock, 2s, [&] { return entered; }), "callback did not arrive");
    }
    auto excess = runtime->request_async(get(server, "/ok"), {}, [](Result<HttpResponse>) {});
    require(!excess && excess.error().code == ErrorCode::capacity, "dispatch reservation bound");
    auto independent = runtime->request(get(server, "/ok"));
    require(static_cast<bool>(independent), "slow callback blocked I/O");
    require(take(runtime->shutdown(20ms)) == ShutdownStatus::pending,
            "slow callback falsely quiescent");
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    cv.notify_all();
    shutdown(runtime);
    require(reentry_ok.load() && count.load() == 1 && handle.ready(),
            "callback once/reentrant errors");
}

void borrowed_and_multiple(Server &server) {
    require(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK, "host curl initialization");
    {
        RuntimeOptions borrowed;
        borrowed.borrow_curl_global = true;
        auto first = take(Runtime::create(borrowed));
        auto second = take(Runtime::create());
        require(take(first->request(get(server, "/ok"))).status == 200, "borrowed runtime");
        shutdown(second);
        second.reset();
        require(take(first->request(get(server, "/ok"))).status == 200, "host curl cleaned by SDK");
        shutdown(first);
    }
    auto *host = curl_easy_init();
    require(host != nullptr, "host curl still usable");
    curl_easy_cleanup(host);
    curl_global_cleanup();
}

void callback_exception(Server &server) {
    RuntimeOptions options;
    options.dispatch_threads = 1;
    auto runtime = take(Runtime::create(options));
    std::atomic<int> calls{0};
    auto first = take(runtime->request_async(get(server, "/ok"), {}, [&](Result<HttpResponse>) {
        ++calls;
        throw std::runtime_error("test callback exception");
    }));
    auto second = take(
        runtime->request_async(get(server, "/ok"), {}, [&](Result<HttpResponse>) { ++calls; }));
    require(static_cast<bool>(first.wait()) && static_cast<bool>(second.wait()),
            "callbacks changed HTTP completion");
    const auto thrown = first.wait_callback();
    require(!thrown && thrown.error().code == ErrorCode::internal,
            "callback exception delivery outcome");
    require(static_cast<bool>(second.wait_callback()), "second callback did not complete");
    shutdown(runtime);
    require(calls.load() == 2, "throwing callback killed dispatcher or duplicated completion");
}

void stop_and_stream_destruction(Server &server) {
    RuntimeOptions options;
    options.max_stream_queue_bytes = CURL_MAX_WRITE_SIZE;
    auto runtime = take(Runtime::create(options));
    {
        auto stream = take(runtime->stream(get(server, "/stream")));
        std::this_thread::sleep_for(30ms);
        // 未排空的流析构取消自己的传输，不等待 runtime 或调用远端 interrupt。
    }
    require(take(runtime->request(get(server, "/ok"))).body == "ok",
            "stream destruction broke runtime");
    auto active = take(runtime->request_async(get(server, "/slow")));
    runtime->stop();
    runtime->stop();
    const auto result = active.wait();
    require(!result && result.error().code == ErrorCode::cancelled,
            "stop did not release active request");
    auto after_stop = runtime->request_async(get(server, "/ok"));
    require(!after_stop && after_stop.error().code == ErrorCode::closed,
            "stopped runtime admitted request");
    shutdown(runtime);
}

void external_dispatcher_ownership(Server &server) {
    auto host = std::make_shared<HostDispatcher>();
    RuntimeOptions options;
    options.dispatcher = host;
    options.max_dispatch_queue = 1;
    auto runtime = take(Runtime::create(options));
    ScopeExit cleanup{[&] {
        host->close();
        (void)runtime->shutdown(3s);
    }};
    std::optional<RequestHandle> handle;
    int calls = 0;
    bool reentrant = false;
    const auto gui_thread = std::this_thread::get_id();
    handle.emplace(
        take(runtime->request_async(get(server, "/ok"), {}, [&](Result<HttpResponse> response) {
            ++calls;
            const auto callback_wait = handle->wait_callback();
            const auto runtime_wait = runtime->shutdown(0ms);
            reentrant = !callback_wait && callback_wait.error().code == ErrorCode::reentrant &&
                        !runtime_wait && runtime_wait.error().code == ErrorCode::reentrant;
            require(std::this_thread::get_id() == gui_thread && response &&
                        response.value().body == "ok",
                    "host dispatch thread or owned response incorrect");
            runtime->stop();
        })));
    require(static_cast<bool>(handle->wait()), "external delivery changed HTTP result");
    host->wait_posts(1);
    require(!handle->callback_ready(), "queued host callback falsely complete");
    auto overflow = runtime->request_async(get(server, "/ok"), {}, [](Result<HttpResponse>) {});
    require(!overflow && overflow.error().code == ErrorCode::capacity,
            "external queue escaped SDK admission bound");
    require(take(runtime->request(get(server, "/ok"))).body == "ok",
            "queued host callback blocked I/O");
    require(take(runtime->shutdown(10ms)) == ShutdownStatus::pending,
            "queued host work falsely quiescent");
    require(!host->closed(), "SDK stopped host dispatcher");
    host->run_one(true, true);
    require(static_cast<bool>(handle->wait_callback()) && handle->callback_ready() && calls == 1 &&
                reentrant,
            "external callback not once or reentrant wait not rejected");
    require(take(runtime->shutdown(10ms)) == ShutdownStatus::pending,
            "retained callback copy falsely quiescent");
    host->other_work(true);
    host->release_copies();
    require(take(runtime->shutdown(10ms)) == ShutdownStatus::pending,
            "shared host unrelated work ignored");
    host->other_work(false);
    shutdown(runtime);
}

void external_dispatcher_rejection(Server &server) {
    auto host = std::make_shared<HostDispatcher>(1);
    RuntimeOptions options;
    options.dispatcher = host;
    auto runtime = take(Runtime::create(options));
    ScopeExit cleanup{[&] {
        host->close();
        (void)runtime->shutdown(3s);
    }};
    std::atomic<int> calls{0};
    auto callback = [&](Result<HttpResponse>) { ++calls; };
    auto first = take(runtime->request_async(get(server, "/ok"), {}, callback));
    require(static_cast<bool>(first.wait()), "first host request failed");
    host->wait_posts(1);
    auto rejected = take(runtime->request_async(get(server, "/ok"), {}, callback));
    require(static_cast<bool>(rejected.wait()), "host rejection rewrote network result");
    const auto rejected_callback = rejected.wait_callback();
    require(!rejected_callback && rejected_callback.error().code == ErrorCode::capacity,
            "host rejection silently succeeded");
    host->drop_pending();
    const auto discarded = first.wait_callback();
    require(!discarded && discarded.error().code == ErrorCode::closed,
            "discarded host work silently succeeded");
    host->throw_next();
    auto thrown = take(runtime->request_async(get(server, "/ok"), {}, callback));
    require(static_cast<bool>(thrown.wait()), "throwing dispatcher rewrote HTTP result");
    const auto thrown_callback = thrown.wait_callback();
    require(!thrown_callback && thrown_callback.error().code == ErrorCode::internal,
            "host dispatcher exception escaped");
    host->close();
    auto closed = take(runtime->request_async(get(server, "/ok"), {}, callback));
    require(static_cast<bool>(closed.wait()), "closed dispatcher rewrote HTTP result");
    require(!closed.wait_callback() && calls.load() == 0, "closed host ran callback");
    shutdown(runtime);
}

void external_dispatcher_running(Server &server) {
    auto host = std::make_shared<HostDispatcher>();
    RuntimeOptions options;
    options.dispatcher = host;
    auto runtime = take(Runtime::create(options));
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, release = false;
    std::thread gui;
    ScopeExit cleanup{[&] {
        {
            std::lock_guard<std::mutex> lock(mutex);
            release = true;
        }
        cv.notify_all();
        if (gui.joinable())
            gui.join();
        host->close();
        (void)runtime->shutdown(3s);
    }};
    auto handle = take(runtime->request_async(get(server, "/ok"), {}, [&](Result<HttpResponse>) {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release; });
    }));
    host->wait_posts(1);
    gui = std::thread([&] { host->run_one(); });
    {
        std::unique_lock<std::mutex> lock(mutex);
        require(cv.wait_for(lock, 2s, [&] { return entered; }), "external callback did not start");
    }
    require(take(runtime->shutdown(10ms)) == ShutdownStatus::pending,
            "running host callback falsely quiescent");
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    cv.notify_all();
    gui.join();
    require(static_cast<bool>(handle.wait_callback()), "external callback did not finish");
    shutdown(runtime);
}
} // namespace

int main() {
    try {
        Server server;
        const std::vector<std::pair<const char *, std::function<void(Server &)>>> tests{
            {"request and input validation", request_and_validation},
            {"redirect and byte limits", redirect_and_limits},
            {"bounded raw stream pause/resume", bounded_stream},
            {"cancellation and deadline", cancellation_and_deadline},
            {"stream idle timeout", stream_idle},
            {"bounded submission", bounded_submission},
            {"callback reentry and pending shutdown", callback_reentry_and_pending},
            {"borrowed and multiple runtime", borrowed_and_multiple},
            {"callback exception isolation", callback_exception},
            {"stop and stream destruction", stop_and_stream_destruction},
            {"external dispatcher ownership and bounds", external_dispatcher_ownership},
            {"external dispatcher rejection and discard", external_dispatcher_rejection},
            {"external dispatcher running shutdown", external_dispatcher_running}};
        for (const auto &test : tests) {
            test.second(server);
            std::cout << "PASS " << test.first << '\n';
        }
        const auto *version = curl_version_info(CURLVERSION_NOW);
        std::cout << "curl=" << version->version
                  << " ssl=" << (version->ssl_version ? version->ssl_version : "none")
                  << " async_dns=" << ((version->features & CURL_VERSION_ASYNCHDNS) != 0) << '\n';
        std::cout << "runtime: " << tests.size() << " passed, 0 failed, 0 skipped\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
