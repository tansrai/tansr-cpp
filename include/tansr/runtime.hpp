#pragma once
#include "tansr/cancellation.hpp"
#include "tansr/error.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tansr {
using Headers = std::vector<std::pair<std::string, std::string>>;
struct HttpRequest {
    std::string method;
    std::string url;
    Headers headers;
    std::string body;
    std::int64_t deadline_ms{0};
    std::size_t max_response_bytes{8U * 1024U * 1024U};
    std::size_t max_header_bytes{64U * 1024U};
};
struct HttpResponse {
    int status{0};
    Headers headers;
    std::string body;
};
class ByteStream {
  public:
    TANSR_API virtual ~ByteStream();
    virtual Result<std::optional<std::string>> next(CancellationToken cancellation = {}) = 0;
    virtual void cancel() noexcept = 0;
    virtual const HttpResponse &response() const noexcept = 0;
};
class HttpTransport {
  public:
    TANSR_API virtual ~HttpTransport();
    virtual Result<HttpResponse> request(const HttpRequest &, CancellationToken = {}) = 0;
    virtual Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &,
                                                       CancellationToken = {}) = 0;
};

// 宿主适配器负责自己的有界队列。SDK 仅提交完成通知，不停止此调度器。
class CallbackDispatcher {
  public:
    TANSR_API virtual ~CallbackDispatcher();
    // 非阻塞提交，不等待容量；接受后在宿主调度线程运行，不内联调用。
    // try_post 的调用位置始终是 SDK dispatch 线程，绝不在 I/O。
    // true 接管工作：最终调用或销毁它；false 不保留、不调用，表示容量/关闭拒绝。
    // 停止时须销毁未运行工作，不能永久保留；工作及其副本均须在库卸载前释放。
    virtual bool try_post(std::function<void()> work) = 0;
    // 当前线程若负责执行本队列，应返回 true；SDK 会拒绝在该线程等待回调/关闭。
    virtual bool is_dispatch_thread() const noexcept = 0;
    // 非阻塞且线程安全：没有排队/运行工作，所有工作对象和副本均已销毁。
    // 不能在调用工作返回之前报告 idle。共享 executor 的其他工作也可能让它为 false。
    virtual bool is_idle() const noexcept = 0;
};
struct RuntimeOptions {
    std::chrono::milliseconds connect_timeout{30000};
    std::chrono::milliseconds stream_idle_timeout{120000};
    std::size_t max_pending_requests{256};
    std::size_t max_stream_queue_bytes{1024U * 1024U};
    std::size_t max_dispatch_queue{256};
    std::size_t dispatch_threads{2};
    // 空值使用默认有界调度池。Runtime 保有此适配器直至最终销毁；不会替宿主 stop。
    std::shared_ptr<CallbackDispatcher> dispatcher;
    std::string ca_file;
    std::string proxy;
    bool borrow_curl_global{false};
};
class RequestHandle {
  public:
    struct State;
    TANSR_API explicit RequestHandle(std::shared_ptr<State>);
    TANSR_API void cancel() noexcept;
    TANSR_API Result<HttpResponse> wait();
    TANSR_API bool ready() const noexcept;
    // 与 HTTP 结果分列：投递拒绝为 capacity、排队工作被宿主丢弃为 closed，
    // dispatcher/用户 callback 抛出异常为 internal。无 callback 时立即成功。
    TANSR_API Result<void> wait_callback();
    TANSR_API bool callback_ready() const noexcept;

  private:
    std::shared_ptr<State> state_;
};
enum class ShutdownStatus { stopped, pending };
class Runtime final : public HttpTransport {
  public:
    // 宿主非回调线程持有最终所有权，先 shutdown 至 stopped，再销毁。
    // callback 可 stop/cancel；不得在自身 dispatch 线程释放最后 Runtime 所有权。
    TANSR_API static Result<std::shared_ptr<Runtime>> create(RuntimeOptions = {});
    TANSR_API ~Runtime() override;
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    TANSR_API Result<HttpResponse> request(const HttpRequest &, CancellationToken = {}) override;
    TANSR_API Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &,
                                                         CancellationToken = {}) override;
    TANSR_API Result<RequestHandle> request_async(HttpRequest, CancellationToken = {},
                                                  std::function<void(Result<HttpResponse>)> = {});
    TANSR_API void stop() noexcept;
    // 外部 dispatcher 的排队、执行、保留副本或共享的他人工作仍存活时返回 pending。
    // 宿主须持续泵送/排空自己的队列。stopped 之前不可销毁 Runtime、调度器或卸载库。
    TANSR_API Result<ShutdownStatus> shutdown(std::chrono::milliseconds timeout);

  private:
    struct Impl;
    explicit Runtime(std::shared_ptr<Impl>);
    std::shared_ptr<Impl> impl_;
};
} // namespace tansr
