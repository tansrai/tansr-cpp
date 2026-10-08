#include "tansr/api.hpp"
#include "tansr/canonical.hpp"
#include "tansr/crypto.hpp"
#include "tansr/operations.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <ctime>
#include <curl/curl.h>
#include <deque>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

namespace tansr {
namespace detail {
struct ReplayIdentity {
    std::shared_ptr<const int> client;
    std::string operation;
    std::string digest;
    std::int64_t deadline_ms{0};
    std::string wire_code;
    std::string retry_action;
    std::string request_id;
    std::string detail;
    std::optional<std::uint64_t> retry_after_ms;
};
} // namespace detail
namespace {
Error invalid(std::string text) { return {ErrorCode::invalid_input, std::move(text)}; }
Error contract(std::string text) { return {ErrorCode::contract, std::move(text)}; }
std::string lower(std::string value) {
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return value;
}
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}
bool visible(std::string_view s, std::size_t maximum, bool spaces = false) {
    return !s.empty() && s.size() <= maximum &&
           std::all_of(s.begin(), s.end(),
                       [spaces](unsigned char c) { return c >= (spaces ? 32 : 33) && c <= 126; });
}
bool hex64(std::string_view s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
Result<std::optional<std::string>> header(const Headers &values, std::string_view key) {
    std::optional<std::string> value;
    for (const auto &pair : values)
        if (lower(pair.first) == key) {
            if (value)
                return contract("duplicate response control header");
            if (pair.second.find('\r') != std::string::npos ||
                pair.second.find('\n') != std::string::npos ||
                pair.second.find('\0') != std::string::npos)
                return contract("invalid response control header");
            value = trim(pair.second);
        }
    return value;
}
Result<std::string> revision(std::string text) {
    text = trim(std::move(text));
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        text = text.substr(1, text.size() - 2);
    if (text.empty() || text.size() > 19 || (text.size() > 1 && text.front() == '0') ||
        !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return invalid("invalid strong revision");
    return text;
}
Result<std::string> origin(std::string value) {
    if (!visible(value, 8192) || value.find('\\') != std::string::npos)
        return invalid("invalid Serve origin");
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
    if (!url || curl_url_set(url.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK)
        return invalid("invalid Serve origin");
    auto part = [&](CURLUPart p) -> std::string {
        char *raw = nullptr;
        if (curl_url_get(url.get(), p, &raw, 0) != CURLUE_OK)
            return {};
        std::unique_ptr<char, decltype(&curl_free)> owned(raw, curl_free);
        return raw;
    };
    const auto scheme = part(CURLUPART_SCHEME);
    if ((scheme != "http" && scheme != "https") || part(CURLUPART_HOST).empty() ||
        !part(CURLUPART_USER).empty() || !part(CURLUPART_PASSWORD).empty() ||
        !part(CURLUPART_QUERY).empty() || !part(CURLUPART_FRAGMENT).empty() ||
        part(CURLUPART_PATH) != "/" || value.find('?') != std::string::npos ||
        value.find('#') != std::string::npos || value.find('@') != std::string::npos)
        return invalid(
            "Serve URL must be an HTTP(S) origin without credentials, path, query or fragment");
    auto normalized = part(CURLUPART_URL);
    while (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();
    return normalized;
}
Result<std::string> timestamp(std::int64_t milliseconds) {
    if (milliseconds <= 0 || milliseconds > 253402300799999LL)
        return invalid("deadline out of range");
    const auto seconds = static_cast<std::time_t>(milliseconds / 1000);
    std::tm tm{};
#ifdef _WIN32
    if (gmtime_s(&tm, &seconds) != 0)
        return invalid("deadline out of range");
#else
    if (!gmtime_r(&seconds, &tm))
        return invalid("deadline out of range");
#endif
    std::ostringstream text;
    text << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
         << (milliseconds % 1000) << 'Z';
    return text.str();
}
Result<void> active(const CallOptions &options, CancellationToken closed) {
    if (options.deadline_ms && *options.deadline_ms < 0)
        return invalid("deadline must be non-negative");
    if (options.deadline_ms && *options.deadline_ms > 0 && *options.deadline_ms <= unix_time_ms())
        return Error{ErrorCode::timeout, "original request deadline exceeded"};
    if (options.cancel.is_cancelled() || closed.is_cancelled())
        return Error{ErrorCode::cancelled, "local request cancelled; remote outcome is unchanged"};
    return {};
}
Result<void> validate_reference(std::optional<std::string_view> ref, const Json &value) {
    if (!ref)
        return {};
    const auto split = ref->find('#');
    if (split == std::string_view::npos)
        return contract("invalid generated schema reference");
    const auto family = ref->substr(0, split);
    if (family == "agent-session-v1")
        return {};
    return validate_wire(family, ref->substr(split + 1), value);
}
const Json *at_path(const Json &body, const std::vector<std::string_view> &path) {
    if (path.empty())
        return nullptr;
    const Json *value = &body;
    for (const auto key : path) {
        value = value->find(key);
        if (!value)
            return nullptr;
    }
    return value;
}
Result<void> map_header(std::optional<Json> &body, const std::vector<std::string_view> &path,
                        const Json &mapped) {
    if (!body || path.empty())
        return {};
    Json *value = &*body;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (!value->is_object())
            return invalid("header mapping requires an object");
        if (i + 1 == path.size()) {
            const auto current = value->find(path[i]);
            if (current && current->dump() != mapped.dump())
                return invalid("header and body conflict");
            value->set(std::string(path[i]), mapped);
        } else {
            if (!value->contains(path[i]))
                value->set(std::string(path[i]), Json::object());
            value = value->find(path[i]);
        }
    }
    return {};
}
Result<std::string> path_for(const Operation &op, const CallOptions &options) {
    std::string result;
    std::set<std::string> used;
    std::size_t start = 0;
    while (start < op.path.size()) {
        const auto end = op.path.find('/', start + 1);
        const auto segment =
            op.path.substr(start + 1, end == std::string_view::npos ? end : end - start - 1);
        result += '/';
        if (!segment.empty() && segment.front() == ':') {
            const std::string name(segment.substr(1));
            const auto found = options.parameters.find(name);
            if (found == options.parameters.end())
                return invalid("missing path parameter");
            const auto &text = found->second;
            if (text.empty() || text.size() > 512 || text == "." || text == ".." ||
                !valid_utf8(text) || std::any_of(text.begin(), text.end(), [](unsigned char c) {
                    return c < 32 || c == 127 || c == '/' || c == '\\';
                }))
                return invalid("invalid path parameter");
            used.insert(name);
            result += canonical::encode_path_segment(text);
        } else
            result += segment;
        if (end == std::string_view::npos)
            break;
        start = end;
    }
    if (used.size() != options.parameters.size())
        return invalid("unexpected path parameter");
    for (const auto &pair : options.query)
        if (std::find(op.query.begin(), op.query.end(), pair.first) == op.query.end())
            return invalid("unknown query parameter");
    bool first = true;
    for (const auto key : op.query) {
        std::optional<std::string> value;
        const auto found = options.query.find(std::string(key));
        if (found != options.query.end())
            value = found->second;
        else if (op.family) {
            if (key == "protocol" &&
                (*op.family == "agent-session-v1" || *op.family == "sdk2-ext-v1" ||
                 *op.family == "sdk2-archive-recovery-v1"))
                value = "sdk2-ext-v1";
            else if (key == "protocol" &&
                     (*op.family == "sdk2-cache-v1" || *op.family == "sdk2-cache-core-v1"))
                value = std::string(*op.family);
            else if (key == "contract" && (*op.family == "terminal-services-v1" ||
                                           *op.family == "terminal-observation-v1" ||
                                           *op.family == "terminal-profile-v1"))
                value = std::string(*op.family);
        }
        if (value) {
            if (value->size() > 8192 || !valid_utf8(*value))
                return invalid("invalid query value");
            result += (first ? '?' : '&');
            first = false;
            result +=
                canonical::encode_path_segment(key) + "=" + canonical::encode_path_segment(*value);
        }
    }
    return result;
}
struct Metadata {
    ApiResponse response;
    std::optional<std::uint64_t> retry_after_ms;
    std::uint64_t manifest_revision{0};
    std::string schema_hash;
};
Result<Metadata> metadata(const HttpResponse &response) {
    std::map<std::string, std::optional<std::string>> values;
    for (const auto *key :
         {"tansr-contract", "tansr-manifest-revision", "tansr-domain", "tansr-schema-hash",
          "tansr-closure-id", "tansr-event-envelope", "content-type", "etag", "retry-after"}) {
        auto h = header(response.headers, key);
        if (!h)
            return h.error();
        values[key] = std::move(h).value();
    }
    if (values["tansr-contract"] != "unified-v1")
        return contract("unified contract marker mismatch");
    const auto revision_text = values["tansr-manifest-revision"].value_or("");
    std::uint64_t revision_number = 0;
    const auto parsed_revision = std::from_chars(
        revision_text.data(), revision_text.data() + revision_text.size(), revision_number);
    if (revision_text.empty() || revision_text.size() > 10 || revision_text.front() == '0' ||
        parsed_revision.ec != std::errc{} ||
        parsed_revision.ptr != revision_text.data() + revision_text.size())
        return contract("invalid manifest revision header");
    const auto hash = values["tansr-schema-hash"].value_or("");
    if (hash != "none" && (hash.size() != 71 || hash.compare(0, 7, "sha256:") != 0 ||
                           !hex64(std::string_view(hash).substr(7))))
        return contract("invalid schema fingerprint header");
    const auto domain = values["tansr-domain"].value_or("");
    if (domain.empty() || domain.size() > 64 || domain.front() < 'a' || domain.front() > 'z' ||
        !std::all_of(domain.begin(), domain.end(),
                     [](char c) { return (c >= 'a' && c <= 'z') || c == '-'; }))
        return contract("invalid response domain");
    if (values["tansr-closure-id"] && !hex64(*values["tansr-closure-id"]))
        return contract("invalid response closure");
    if (values["tansr-event-envelope"] && values["tansr-event-envelope"] != "unified-v1")
        return contract("invalid event envelope negotiation");
    Metadata result;
    result.manifest_revision = revision_number;
    result.schema_hash = hash;
    result.response.status = response.status;
    result.response.domain = domain;
    result.response.capability_closure = values["tansr-closure-id"];
    auto mime = values["content-type"].value_or("");
    result.response.content_type = lower(trim(mime.substr(0, mime.find(';'))));
    if (values["etag"]) {
        const auto &etag = *values["etag"];
        if (etag.size() >= 2 && etag.front() == '"' && etag.back() == '"' && revision(etag))
            result.response.etag = etag;
    }
    if (values["retry-after"]) {
        const auto &value = *values["retry-after"];
        char *end = nullptr;
        const double seconds = std::strtod(value.c_str(), &end);
        if (end == value.c_str() + value.size() && end != value.c_str() && std::isfinite(seconds) &&
            seconds >= 0 && seconds <= 1e9)
            result.retry_after_ms = static_cast<std::uint64_t>(std::ceil(seconds * 1000));
        else {
            const auto when = curl_getdate(value.c_str(), nullptr);
            if (when >= 0)
                result.retry_after_ms = static_cast<std::uint64_t>(std::max<std::int64_t>(
                    0, static_cast<std::int64_t>(when) * 1000 - unix_time_ms()));
        }
    }
    return result;
}
Result<std::string> fingerprint(const HttpRequest &request) {
    std::string input = request.method;
    input.push_back('\0');
    input += request.url;
    input.push_back('\0');
    auto headers = request.headers;
    std::sort(headers.begin(), headers.end());
    for (const auto &pair : headers)
        if (lower(pair.first) != "authorization") {
            input += lower(pair.first);
            input.push_back('\0');
            input += pair.second;
            input.push_back('\0');
        }
    input += request.body;
    return crypto::sha256_hex(input);
}
Error decode_error(const HttpResponse &response, const Metadata &meta, const Operation &op) {
    if (meta.response.content_type != "application/json")
        return contract("non-JSON error response");
    auto parsed = Json::parse(
        response.body, JsonLimits{std::max<std::size_t>(1, response.body.size()), 32, 100000});
    if (!parsed)
        return parsed.error();
    const auto &body = parsed.value();
    const auto marker = body.find("contract");
    if (!marker || !marker->is_string() || marker->as_string() != "unified-v1") {
        if (op.family == "archive-sync-v1" && body.is_object()) {
            Error e{ErrorCode::http, "archive domain request rejected"};
            e.http_status = response.status;
            e.detail = response.body;
            return e;
        }
        return contract("unknown error envelope");
    }
    auto checked = validate_wire("unified-v1", "UnifiedError", body);
    if (!checked)
        return checked.error();
    try {
        if (body.at("status").as_u64() != static_cast<std::uint64_t>(response.status))
            return contract("error HTTP status mismatch");
        Error error{ErrorCode::http, "Serve request rejected; inspect structured code"};
        error.http_status = response.status;
        error.wire_code = body.at("code").as_string();
        error.retry_action = body.at("retryAction").as_string();
        if (const auto value = body.find("requestId"); value && value->is_string())
            error.request_id = value->as_string();
        if (const auto value = body.find("detail"))
            error.detail = value->dump();
        if (const auto value = body.find("retryAfterMs"))
            error.retry_after_ms = value->as_u64();
        else
            error.retry_after_ms = meta.retry_after_ms;
        return error;
    } catch (const std::exception &) {
        return contract("invalid error envelope values");
    }
}
} // namespace

struct ApiClient::Impl {
    ClientOptions client_options;
    std::shared_ptr<HttpTransport> transport;
    CancellationSource closed;
    std::shared_ptr<const int> identity = std::make_shared<const int>(0);
    std::mutex principal_mutex;
    std::optional<std::string> principal;

    Result<HttpRequest> prepare(const Operation &operation, CallOptions options, bool stream) {
        auto alive = active(options, closed.token());
        if (!alive)
            return alive.error();
        auto path = path_for(operation, options);
        if (!path)
            return path.error();
        if (options.body && options.raw_body)
            return invalid("JSON and byte bodies are mutually exclusive");
        if (options.raw_body && operation.name != "session.checkpoint.import")
            return invalid("byte body not declared for operation");
        if (stream && (options.body || options.raw_body))
            return invalid("stream request has no body");
        HttpRequest request;
        request.method = operation.method;
        request.url = this->client_options.base_url + path.value();
        request.deadline_ms = options.deadline_ms.value_or(0);
        request.max_response_bytes =
            options.max_response_bytes.value_or(this->client_options.max_response_bytes);
        if (!request.max_response_bytes || request.max_response_bytes > 256U * 1024U * 1024U)
            return invalid("invalid response capacity");
        request.headers = {
            {"accept", stream ? "text/event-stream" : "application/json, application/octet-stream"},
            {"tansr-session-family", this->client_options.family}};
        if (stream)
            request.headers.emplace_back("tansr-event-envelope", "unified-v1");
        if (options.capability_closure) {
            if (!hex64(*options.capability_closure) || operation.kind != "write" ||
                operation.path.substr(0, 18) != "/api/sessions/:id/")
                return invalid("invalid or inapplicable capability closure");
            request.headers.emplace_back("tansr-closure-id", *options.capability_closure);
        }
        auto effective = options.body;
        if (options.request_key) {
            if (operation.kind != "write" || !visible(*options.request_key, 128))
                return invalid("invalid or inapplicable request key");
            auto mapped =
                map_header(effective, operation.request_id_path, Json(*options.request_key));
            if (!mapped)
                return mapped.error();
            request.headers.emplace_back("idempotency-key", *options.request_key);
        }
        if (options.if_match) {
            auto rev = revision(*options.if_match);
            if (!rev)
                return rev.error();
            if (operation.kind != "write" || !operation.expected_revision)
                return invalid("If-Match is not applicable");
            Json mapped(rev.value());
            if (operation.expected_revision->kind == "integer") {
                std::uint64_t number = 0;
                const auto converted = std::from_chars(
                    rev.value().data(), rev.value().data() + rev.value().size(), number);
                if (converted.ec != std::errc{} || number > 9007199254740991ULL)
                    return invalid("unsafe revision");
                mapped = Json(number);
            }
            auto result = map_header(effective, operation.expected_revision->path, mapped);
            if (!result)
                return result.error();
            request.headers.emplace_back("if-match", "\"" + rev.value() + "\"");
        }
        if (options.deadline_ms && *options.deadline_ms > 0) {
            auto deadline = timestamp(*options.deadline_ms);
            if (!deadline)
                return deadline.error();
            request.headers.emplace_back("deadline", std::move(deadline).value());
        }
        if (options.last_event_id) {
            if (!stream || !visible(*options.last_event_id, 256, true))
                return invalid("invalid event cursor");
            request.headers.emplace_back("last-event-id", *options.last_event_id);
        }
        if (effective) {
            auto checked = validate_reference(operation.request_schema, *effective);
            if (!checked)
                return checked.error();
        }
        if (options.raw_body) {
            request.body = *options.raw_body;
            request.headers.emplace_back("content-type", "application/octet-stream");
        } else if (options.body) {
            if (operation.family && *operation.family != "agent-session-v1" &&
                *operation.family != "unified-v1") {
                auto encoded = canonical::encode(*options.body);
                if (!encoded)
                    return encoded.error();
                request.body = std::move(encoded).value();
            } else
                request.body = options.body->dump();
            request.headers.emplace_back("content-type", "application/json");
        }
        if (options.content_type &&
            ((!options.raw_body && *options.content_type != "application/json") ||
             (options.raw_body && *options.content_type != "application/octet-stream")))
            return invalid("content type differs from declared body");
        if (request.body.size() > request.max_response_bytes)
            return invalid("request body exceeds capacity");
        // 在任何凭据回调之前完成无副作用输入校验。provider 必须协作取消。
        auto provider_options = options;
        if (!provider_options.deadline_ms)
            provider_options.deadline_ms =
                unix_time_ms() + this->client_options.default_timeout.count();
        auto provider_cancel =
            CancellationToken::combine(options.cancel, closed.token())
                .with_deadline(static_cast<std::uint64_t>(*provider_options.deadline_ms));
        auto token = this->client_options.token_provider(provider_cancel);
        alive = active(provider_options, closed.token());
        if (!alive)
            return alive.error();
        if (!token)
            return token.error();
        if (!visible(token.value().value, 16384))
            return invalid("invalid authentication token");
        if (token.value().principal.empty() || !valid_utf8(token.value().principal))
            return invalid("authentication principal is required");
        {
            std::lock_guard<std::mutex> guard(principal_mutex);
            if (principal && *principal != token.value().principal)
                return Error{ErrorCode::permission,
                             "authentication principal changed; create a new client"};
            principal = token.value().principal;
        }
        request.headers.emplace_back("authorization", "Bearer " + token.value().value);
        return request;
    }
};

struct EventStream::Impl {
    std::shared_ptr<ByteStream> bytes;
    sse::Parser parser;
    std::deque<SseEvent> pending;
    CancellationToken cancellation;
    CancellationSource stopped;
    bool eof{false};
    std::mutex reader;
    explicit Impl(std::shared_ptr<ByteStream> stream, CancellationToken token)
        : bytes(std::move(stream)), parser(2U * 1024U * 1024U), cancellation(std::move(token)) {}
    ~Impl() { bytes->cancel(); }
};
EventStream::EventStream(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
EventStream::~EventStream() { cancel(); }
EventStream::EventStream(EventStream &&) noexcept = default;
EventStream &EventStream::operator=(EventStream &&) noexcept = default;
void EventStream::cancel() noexcept {
    if (impl_) {
        impl_->stopped.cancel();
        impl_->bytes->cancel();
    }
}
Result<std::optional<SseEvent>> EventStream::next(CancellationToken cancellation) {
    try {
        if (!impl_)
            return Error{ErrorCode::closed, "event stream is closed"};
        std::unique_lock<std::mutex> lock(impl_->reader, std::try_to_lock);
        if (!lock.owns_lock())
            return Error{ErrorCode::reentrant, "event stream supports one reader"};
        auto token = CancellationToken::combine(
            CancellationToken::combine(impl_->cancellation, std::move(cancellation)),
            impl_->stopped.token());
        for (;;) {
            if (token.is_cancelled()) {
                cancel();
                return Error{ErrorCode::cancelled, "local event stream cancelled"};
            }
            if (!impl_->pending.empty()) {
                auto event = std::move(impl_->pending.front());
                impl_->pending.pop_front();
                if (event.data.empty() && !event.id && !event.event)
                    continue;
                auto json = Json::parse(event.data);
                if (!json) {
                    cancel();
                    return json.error();
                }
                auto check = validate_wire("unified-v1", "EventEnvelope", json.value());
                if (!check) {
                    cancel();
                    return check.error();
                }
                const auto &value = json.value();
                const auto &id = value.at("eventId");
                const auto &cursor = value.at("cursorSet").at("eventCursor");
                const auto matches = [&](const Json &v) {
                    return event.id ? v.is_string() && v.as_string() == *event.id : v.is_null();
                };
                if (!matches(id) || !matches(cursor)) {
                    cancel();
                    return contract("event frame and envelope cursor differ");
                }
                if (token.is_cancelled()) {
                    cancel();
                    return Error{ErrorCode::cancelled, "local event stream cancelled"};
                }
                return std::optional<SseEvent>(std::move(event));
            }
            if (impl_->eof)
                return std::optional<SseEvent>{};
            auto chunk = impl_->bytes->next(token);
            if (token.is_cancelled()) {
                cancel();
                return Error{ErrorCode::cancelled, "local event stream cancelled"};
            }
            if (!chunk) {
                cancel();
                return chunk.error();
            }
            auto frames =
                chunk.value() ? impl_->parser.feed(*chunk.value()) : impl_->parser.finish();
            if (!frames) {
                cancel();
                return frames.error();
            }
            for (auto &frame : frames.value())
                impl_->pending.push_back(std::move(frame));
            if (!chunk.value())
                impl_->eof = true;
        }
    } catch (...) {
        cancel();
        return Error{ErrorCode::internal, "event stream reader failed"};
    }
}

ApiClient::ApiClient(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
Result<std::shared_ptr<ApiClient>> ApiClient::create(ClientOptions options,
                                                     std::shared_ptr<HttpTransport> transport) {
    if (!transport || !options.token_provider || options.default_timeout.count() <= 0 ||
        options.default_timeout > std::chrono::hours(24) || options.max_response_bytes < 1024 ||
        options.max_response_bytes > 256U * 1024U * 1024U)
        return invalid("invalid client configuration");
    if (options.family != "sdk1" && options.family != "sdk2-offload-v1")
        return invalid("unsupported session family");
    auto base = origin(options.base_url);
    if (!base)
        return base.error();
    options.base_url = std::move(base).value();
    auto impl = std::make_shared<Impl>();
    impl->client_options = std::move(options);
    impl->transport = std::move(transport);
    return std::shared_ptr<ApiClient>(new ApiClient(std::move(impl)));
}
const std::string &ApiClient::base_url() const noexcept { return impl_->client_options.base_url; }
const std::string &ApiClient::family() const noexcept { return impl_->client_options.family; }
std::int64_t ApiClient::default_deadline_ms() const {
    return unix_time_ms() + impl_->client_options.default_timeout.count();
}
void ApiClient::shutdown() noexcept { impl_->closed.cancel(); }
Result<ApiResponse> ApiClient::call(std::string_view name, CallOptions options) const {
    try {
        const auto *operation = find_operation(name);
        if (!operation || operation->kind == "stream")
            return invalid("unknown operation or stream used as ordinary request");
        if (!options.deadline_ms)
            options.deadline_ms = unix_time_ms() + impl_->client_options.default_timeout.count();
        auto prepared = impl_->prepare(*operation, options, false);
        if (!prepared)
            return prepared.error();
        auto digest = fingerprint(prepared.value());
        if (!digest)
            return digest.error();
        auto reply = impl_->transport->request(
            prepared.value(), CancellationToken::combine(options.cancel, impl_->closed.token())
                                  .with_deadline(static_cast<std::uint64_t>(*options.deadline_ms)));
        auto alive = active(options, impl_->closed.token());
        if (!alive)
            return alive.error();
        if (!reply)
            return reply.error();
        if (reply.value().status >= 300 && reply.value().status < 400)
            return contract("redirect refused");
        auto meta = metadata(reply.value());
        if (!meta)
            return meta.error();
        if (reply.value().body.size() > prepared.value().max_response_bytes)
            return contract("response exceeds capacity");
        if (reply.value().status >= 400) {
            auto error = decode_error(reply.value(), meta.value(), *operation);
            if (error.code == ErrorCode::http)
                error.replay_ = std::make_shared<detail::ReplayIdentity>(detail::ReplayIdentity{
                    impl_->identity, std::string(name), digest.value(), *options.deadline_ms,
                    error.wire_code, error.retry_action, error.request_id, error.detail,
                    error.retry_after_ms});
            return error;
        }
        if (reply.value().status < 200 || reply.value().status >= 300)
            return contract("unexpected HTTP status");
        auto result = std::move(meta.value().response);
        result.raw_body = std::move(reply).value().body;
        if (result.status != 204 && result.content_type != "application/json" &&
            result.content_type != "application/octet-stream")
            return contract("unexpected response content type");
        if (result.content_type == "application/octet-stream" &&
            name != "session.checkpoint.export")
            return contract("undeclared byte response");
        if (operation->response_schema &&
            (result.status == 204 || result.content_type != "application/json" ||
             result.raw_body.empty()))
            return contract("schema response requires JSON");
        if (result.content_type == "application/json" && result.status != 204) {
            auto value = Json::parse(result.raw_body,
                                     JsonLimits{prepared.value().max_response_bytes, 32, 100000});
            if (!value)
                return value.error();
            result.body = std::move(value).value();
            auto check = validate_reference(operation->response_schema, result.body);
            if (!check)
                return check.error();
            if (name == "discovery.manifest") {
                check = validate_wire("unified-v1", "Manifest", result.body);
                if (!check)
                    return check.error();
                if (result.body.at("revision").as_u64() != manifest_revision ||
                    meta.value().manifest_revision != manifest_revision ||
                    result.body.at("schemaHash").as_string() != schema_hash ||
                    meta.value().schema_hash != "sha256:" + std::string(schema_hash) ||
                    !result.body.at("runtime").is_object())
                    return contract("manifest response differs from frozen contract");
            } else if (name == "discovery.capabilities") {
                check = validate_wire("unified-v1", "Capabilities", result.body);
                if (!check)
                    return check.error();
                if (result.body.at("manifestRevision").as_u64() != manifest_revision ||
                    meta.value().manifest_revision != manifest_revision ||
                    result.body.at("schemaHash").as_string() !=
                        "sha256:" + std::string(schema_hash) ||
                    meta.value().schema_hash != "sha256:" + std::string(schema_hash))
                    return contract("capabilities response differs from frozen contract");
            } else if (name == "discovery.session.capabilities") {
                check = validate_wire("unified-v1", "CapabilityClosure", result.body);
                if (!check)
                    return check.error();
                if (!result.capability_closure ||
                    result.body.at("closureId").as_string() != *result.capability_closure)
                    return contract("capability closure body and header differ");
            }
        }
        return result;
    } catch (...) {
        return contract("invalid SDK request or response value");
    }
}
Result<EventStream> ApiClient::events(std::string_view name, CallOptions options) const {
    try {
        const auto *operation = find_operation(name);
        if (!operation || operation->kind != "stream")
            return invalid("operation is not an event stream");
        auto prepared = impl_->prepare(*operation, options, true);
        if (!prepared)
            return prepared.error();
        auto token =
            CancellationToken::combine(options.cancel, impl_->closed.token())
                .with_deadline(static_cast<std::uint64_t>(options.deadline_ms.value_or(0)));
        auto bytes = impl_->transport->stream(prepared.value(), token);
        if (!bytes)
            return bytes.error();
        auto stream = std::move(bytes).value();
        if (!stream)
            return contract("transport returned no event stream");
        struct Cleanup {
            std::shared_ptr<ByteStream> stream;
            ~Cleanup() {
                if (stream)
                    stream->cancel();
            }
        } cleanup{stream};
        auto alive = active(options, impl_->closed.token());
        if (!alive)
            return alive.error();
        const auto status = stream->response().status;
        if (status >= 300 && status < 400)
            return contract("redirect refused");
        auto meta = metadata(stream->response());
        if (!meta)
            return meta.error();
        if (status >= 400) {
            HttpResponse error_response = stream->response();
            const auto maximum = prepared.value().max_response_bytes;
            if (error_response.body.size() > maximum)
                return contract("response exceeds capacity");
            for (;;) {
                auto chunk = stream->next(token);
                alive = active(options, impl_->closed.token());
                if (!alive)
                    return alive.error();
                if (!chunk)
                    return chunk.error();
                if (!chunk.value())
                    break;
                if (chunk.value()->size() > maximum - error_response.body.size())
                    return contract("response exceeds capacity");
                error_response.body += *chunk.value();
            }
            return decode_error(error_response, meta.value(), *operation);
        }
        auto envelope = header(stream->response().headers, "tansr-event-envelope");
        if (status != 200 || meta.value().response.content_type != "text/event-stream" ||
            !envelope || envelope.value() != "unified-v1")
            return contract("unified SSE was not negotiated");
        EventStream result(std::make_shared<EventStream::Impl>(stream, token));
        cleanup.stream.reset();
        return result;
    } catch (...) {
        return contract("invalid event stream configuration");
    }
}
Result<ApiResponse> ApiClient::retry_same_request(std::string_view name, CallOptions options,
                                                  const Error &previous) const {
    try {
        if (!previous.replay_ || previous.replay_->client != impl_->identity ||
            previous.replay_->operation != name || previous.retry_action != "same-request" ||
            previous.wire_code == "result_unknown")
            return invalid(
                "retry is not authorized for this client and operation; reconcile original status");
        if (previous.wire_code != previous.replay_->wire_code ||
            previous.retry_action != previous.replay_->retry_action ||
            previous.request_id != previous.replay_->request_id ||
            previous.detail != previous.replay_->detail ||
            previous.retry_after_ms != previous.replay_->retry_after_ms)
            return invalid("retry evidence differs from original response");
        if (!previous.detail.empty()) {
            auto detail = Json::parse(previous.detail);
            if (!detail)
                return detail.error();
            const auto code = detail.value().find("domainCode");
            if (code && code->is_string() &&
                (code->as_string() == "result_unknown" || code->as_string() == "commit_unknown"))
                return invalid("unknown outcome requires status reconciliation");
        }
        if (!options.deadline_ms)
            options.deadline_ms = previous.replay_->deadline_ms;
        if (*options.deadline_ms != previous.replay_->deadline_ms)
            return invalid("retry cannot change original deadline");
        const auto *operation = find_operation(name);
        if (!operation)
            return invalid("unknown operation");
        auto id = options.request_key;
        if (!id && options.body)
            if (const auto value = at_path(*options.body, operation->request_id_path);
                value && value->is_string())
                id = value->as_string();
        if (!id || id->empty() || (!previous.request_id.empty() && previous.request_id != *id))
            return invalid("retry requires original request identity");
        auto request = impl_->prepare(*operation, options, false);
        if (!request)
            return request.error();
        auto digest = fingerprint(request.value());
        if (!digest)
            return digest.error();
        if (digest.value() != previous.replay_->digest)
            return invalid("retry bytes or preconditions changed");
        const auto wait = previous.retry_after_ms.value_or(0);
        if (*options.deadline_ms > 0 && wait > static_cast<std::uint64_t>(std::max<std::int64_t>(
                                                   0, *options.deadline_ms - unix_time_ms())))
            return Error{ErrorCode::timeout, "retry wait exceeds original deadline"};
        if (wait > static_cast<std::uint64_t>(std::chrono::milliseconds::max().count()))
            return invalid("retry wait exceeds supported duration");
        if (CancellationToken::combine(options.cancel, impl_->closed.token())
                .wait_for(std::chrono::milliseconds(wait)))
            return Error{ErrorCode::cancelled, "retry wait cancelled"};
        return call(name, std::move(options));
    } catch (...) {
        return contract("invalid original retry evidence");
    }
}
} // namespace tansr
