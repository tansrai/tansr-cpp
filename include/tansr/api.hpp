#pragma once
#include "tansr/json.hpp"
#include "tansr/runtime.hpp"
#include "tansr/sse.hpp"
#include <map>
#include <mutex>

namespace tansr {
using SseEvent = sse::Frame;
struct AuthToken {
    std::string value;
    std::string principal;
};
using TokenProvider = std::function<Result<AuthToken>(CancellationToken)>;
struct ClientOptions {
    std::string base_url;
    std::string family{"sdk1"};
    TokenProvider token_provider;
    std::chrono::milliseconds default_timeout{30000};
    std::size_t max_response_bytes{8U * 1024U * 1024U};
};
struct CallOptions {
    std::map<std::string, std::string> parameters;
    std::map<std::string, std::string> query;
    std::optional<Json> body;
    std::optional<std::string> raw_body;
    std::optional<std::string> content_type;
    std::optional<std::string> if_match;
    std::optional<std::string> request_key;
    std::optional<std::string> capability_closure;
    std::optional<std::string> last_event_id;
    std::optional<std::size_t> max_response_bytes;
    std::optional<std::int64_t> deadline_ms;
    CancellationToken cancel;
};
struct ApiResponse {
    Json body;
    std::string raw_body;
    int status{0};
    std::optional<std::string> etag;
    std::optional<std::string> capability_closure;
    std::string content_type;
    std::string domain;
};
class EventStream {
  public:
    struct Impl;
    TANSR_API explicit EventStream(std::shared_ptr<Impl>);
    TANSR_API ~EventStream();
    TANSR_API EventStream(EventStream &&) noexcept;
    TANSR_API EventStream &operator=(EventStream &&) noexcept;
    EventStream(const EventStream &) = delete;
    EventStream &operator=(const EventStream &) = delete;
    TANSR_API Result<std::optional<SseEvent>> next(CancellationToken = {});
    TANSR_API void cancel() noexcept;

  private:
    std::shared_ptr<Impl> impl_;
};
class ApiClient {
  public:
    TANSR_API static Result<std::shared_ptr<ApiClient>> create(ClientOptions,
                                                               std::shared_ptr<HttpTransport>);
    TANSR_API Result<ApiResponse> call(std::string_view operation, CallOptions = {}) const;
    TANSR_API Result<EventStream> events(std::string_view operation, CallOptions = {}) const;
    TANSR_API Result<ApiResponse> retry_same_request(std::string_view operation, CallOptions,
                                                     const Error &previous) const;
    TANSR_API void shutdown() noexcept;
    TANSR_API const std::string &base_url() const noexcept;
    TANSR_API const std::string &family() const noexcept;
    TANSR_API std::int64_t default_deadline_ms() const;

  private:
    struct Impl;
    TANSR_API explicit ApiClient(std::shared_ptr<Impl>);
    std::shared_ptr<Impl> impl_;
};
TANSR_API Result<void> validate_wire(std::string_view family, std::string_view definition,
                                     const Json &);
} // namespace tansr
