#include "tansr/api.hpp"
#include "tansr/operations.hpp"
#include <chrono>
#include <deque>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace tansr;
namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
HttpResponse response(std::string body = "{}", int status = 200) {
    return {status,
            {{"tansr-contract", "unified-v1"},
             {"tansr-manifest-revision", "7"},
             {"tansr-schema-hash",
              "sha256:6e1876edc476322e2fc3613958f5b44700bb80ea23550fa794f5729220674f3e"},
             {"tansr-domain", "session"},
             {"content-type", "application/json"}},
            std::move(body)};
}
class MockTransport final : public HttpTransport {
  public:
    std::vector<HttpRequest> requests;
    HttpResponse reply = response("[]");
    std::function<void(const HttpRequest &)> inspect;
    std::function<Result<std::shared_ptr<ByteStream>>(const HttpRequest &, CancellationToken)>
        open_stream;
    Result<HttpResponse> request(const HttpRequest &request, CancellationToken cancel) override {
        if (cancel.is_cancelled())
            return Error{ErrorCode::cancelled, "cancelled"};
        requests.push_back(request);
        if (inspect)
            inspect(request);
        return reply;
    }
    Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &request,
                                               CancellationToken token) override {
        requests.push_back(request);
        if (open_stream)
            return open_stream(request, token);
        return Error{ErrorCode::internal, "not used in this unit fixture"};
    }
};
bool has_header(const HttpRequest &request, std::string_view name, std::string_view value) {
    for (const auto &entry : request.headers)
        if (entry.first == name && entry.second == value)
            return true;
    return false;
}
class TestStream final : public ByteStream {
  public:
    HttpResponse head = ::response("");
    std::deque<std::string> chunks;
    bool cancelled = false;
    int reads = 0;
    std::function<void()> on_read;
    Result<std::optional<std::string>> next(CancellationToken) override {
        ++reads;
        if (on_read)
            on_read();
        if (chunks.empty())
            return std::optional<std::string>{};
        auto bytes = std::move(chunks.front());
        chunks.pop_front();
        return std::optional<std::string>{std::move(bytes)};
    }
    void cancel() noexcept override { cancelled = true; }
    const HttpResponse &response() const noexcept override { return head; }
};
std::shared_ptr<TestStream> event_bytes(std::string bytes) {
    auto stream = std::make_shared<TestStream>();
    stream->head.headers.back().second = "text/event-stream";
    stream->head.headers.emplace_back("tansr-event-envelope", "unified-v1");
    stream->chunks.push_back(std::move(bytes));
    return stream;
}
std::string frame(std::string id) {
    auto envelope = Json::object({{"contract", "unified-v1"},
                                  {"eventId", id},
                                  {"domain", "session"},
                                  {"type", "msg.text.delta"},
                                  {"cursorSet", Json::object({{"eventCursor", id},
                                                              {"archiveCoverage", nullptr},
                                                              {"outputWatermark", nullptr},
                                                              {"materialConsumed", nullptr},
                                                              {"ackReceipt", nullptr}})},
                                  {"terminalStatus", nullptr},
                                  {"raw", Json::object({{"text", "synthetic"}})}});
    return "id: " + id + "\ndata: " + envelope.dump() + "\n\n";
}
std::string error_body() {
    return R"({"contract":"unified-v1","traceId":"trace","requestId":null,"code":"forbidden","status":403,"retryAction":"none","message":"private diagnostic"})";
}
void event_boundaries() {
    auto transport = std::make_shared<MockTransport>();
    ClientOptions config;
    config.base_url = "https://serve.example.test";
    config.token_provider = [](CancellationToken) -> Result<AuthToken> {
        return AuthToken{"synthetic-token", "app/user"};
    };
    auto built = ApiClient::create(config, transport);
    check(bool(built), "event client create");
    auto client = built.value();
    std::shared_ptr<TestStream> source;
    transport->open_stream = [&](const HttpRequest &,
                                 CancellationToken) -> Result<std::shared_ptr<ByteStream>> {
        return std::static_pointer_cast<ByteStream>(source);
    };
    CallOptions options;
    options.parameters["id"] = "session-one";

    source = event_bytes(frame("12") + frame("13"));
    auto opened = client->events("session.events.observe", options);
    check(bool(opened), "event stream with true domain fingerprint rejected");
    auto first = opened.value().next();
    check(first && first.value() && first.value()->id == "12", "first event missing");
    opened.value().cancel();
    auto after_cancel = opened.value().next();
    check(!after_cancel && after_cancel.error().code == ErrorCode::cancelled &&
              source->reads == 1 && source->cancelled,
          "cancel delivered a buffered event or reread network");

    source = event_bytes("data: {}\n\n" + frame("14"));
    auto malformed = client->events("session.events.observe", options);
    check(bool(malformed), "malformed fixture open");
    auto rejected = malformed.value().next();
    check(!rejected && rejected.error().code == ErrorCode::contract, "bad event accepted");
    check(!malformed.value().next() && source->cancelled && source->reads == 1,
          "bad event did not terminally stop pending delivery");

    CancellationSource cancel;
    source = event_bytes(frame("15"));
    source->on_read = [&] { cancel.cancel(); };
    options.cancel = cancel.token();
    auto during_read = client->events("session.events.observe", options);
    check(bool(during_read), "cancel fixture open");
    auto cancelled = during_read.value().next();
    check(!cancelled && cancelled.error().code == ErrorCode::cancelled && source->cancelled,
          "bytes released on cancellation became an event");
    options.cancel = {};

    source = event_bytes("");
    source->on_read = [] { throw std::runtime_error("secret exception"); };
    auto throwing = client->events("session.events.observe", options);
    check(bool(throwing), "throwing fixture open");
    auto failure = throwing.value().next();
    check(!failure && source->cancelled &&
              failure.error().message.find("secret") == std::string::npos,
          "transport throw escaped or leaked content");

    source = std::make_shared<TestStream>();
    source->head = response("", 403);
    const auto body = error_body();
    source->chunks = {body.substr(0, 20), body.substr(20)};
    auto denied = client->events("session.events.observe", options);
    check(!denied && denied.error().code == ErrorCode::http && denied.error().http_status == 403 &&
              denied.error().wire_code == "forbidden" && denied.error().retry_action == "none",
          "stream handshake lost unified error");
    check(source->cancelled && source->reads == 3 &&
              denied.error().message.find("private") == std::string::npos,
          "stream error was not drained within limit/closed/redacted");

    source = std::make_shared<TestStream>();
    source->head = response("", 403);
    source->chunks = {body};
    options.max_response_bytes = 32;
    auto too_large = client->events("session.events.observe", options);
    check(!too_large && too_large.error().code == ErrorCode::contract && source->cancelled &&
              source->reads == 1,
          "stream error body ignored limit");
    options.max_response_bytes.reset();

    source = std::make_shared<TestStream>();
    source->head = {302, {{"location", "https://untrusted.example/"}}, ""};
    auto redirect = client->events("session.events.observe", options);
    check(!redirect && redirect.error().message == "redirect refused" && source->cancelled &&
              source->reads == 0,
          "stream redirect read body or obscured redirect");
}
void provider_deadline() {
    auto transport = std::make_shared<MockTransport>();
    ClientOptions config;
    config.base_url = "https://serve.example.test";
    bool saw_deadline = false;
    config.token_provider = [&](CancellationToken token) -> Result<AuthToken> {
        saw_deadline = token.wait_for(std::chrono::milliseconds(1000));
        return Error{ErrorCode::cancelled, "provider ended"};
    };
    auto built = ApiClient::create(config, transport);
    check(bool(built), "deadline client create");
    CallOptions options;
    options.deadline_ms = unix_time_ms() + 25;
    auto result = built.value()->call("session.list", options);
    check(!result && result.error().code == ErrorCode::timeout && saw_deadline &&
              transport->requests.empty(),
          "provider did not observe original deadline or timeout classification");
}
void retry_cancellation_boundaries() {
    using namespace std::chrono_literals;
    for (const bool during_refresh : {false, true}) {
        auto transport = std::make_shared<MockTransport>();
        CancellationSource stop;
        std::promise<void> provider_entered;
        auto entered = provider_entered.get_future();
        int provider_calls = 0;
        bool provider_saw_cancel = false;
        ClientOptions config;
        config.base_url = "https://serve.example.test";
        config.token_provider = [&](CancellationToken token) -> Result<AuthToken> {
            if (++provider_calls == 2 && during_refresh) {
                provider_entered.set_value();
                provider_saw_cancel = token.wait_for(2s);
                // 即使宿主仍返回刷新后的有效凭据，取消也必须阻止第二次写入。
            }
            return AuthToken{"synthetic-token", "app/user"};
        };
        auto built = ApiClient::create(config, transport);
        check(bool(built), "retry cancellation client");
        auto client = built.value();
        CallOptions write;
        write.parameters["id"] = "session-one";
        write.request_key = "original-id";
        write.deadline_ms = unix_time_ms() + 3000;
        write.cancel = stop.token();
        write.body = Json::object({{"text", "original body"}});
        write.capability_closure = std::string(64, 'a');
        auto conflict_body = Json::object({{"contract", "unified-v1"},
                                           {"traceId", "trace"},
                                           {"requestId", "original-id"},
                                           {"code", "conflict"},
                                           {"status", 409},
                                           {"retryAction", "same-request"},
                                           {"message", "synthetic conflict"}});
        if (!during_refresh)
            conflict_body.set("retryAfterMs", 1000);
        transport->reply = response(conflict_body.dump(), 409);
        auto conflict = client->call("session.message.send", write);
        check(!conflict && conflict.error().retry_action == "same-request" &&
                  transport->requests.size() == 1,
              "retry cancellation original error");
        transport->reply = response("{}", 202);
        auto retried = std::async(std::launch::async, [&] {
            return client->retry_same_request("session.message.send", write, conflict.error());
        });
        bool reached_boundary;
        if (during_refresh)
            reached_boundary = entered.wait_for(1s) == std::future_status::ready;
        else
            reached_boundary = retried.wait_for(100ms) == std::future_status::timeout;
        stop.cancel();
        auto result = retried.get();
        check(reached_boundary, "retry did not reach controlled wait boundary");
        check(!result && result.error().code == ErrorCode::cancelled &&
                  transport->requests.size() == 1,
              "cancelled retry emitted another write");
        // 原请求指纹核验会取一次凭据；等待取消后不能进入真正写入所需的第三次取票。
        check(provider_calls == 2 && (!during_refresh || provider_saw_cancel),
              "retry wait bypassed cancellation/provider boundary");
    }
}
void control_body_preflight() {
    auto transport = std::make_shared<MockTransport>();
    int provider_calls = 0;
    ClientOptions config;
    config.base_url = "https://serve.example.test";
    config.family = "sdk2-offload-v1";
    config.token_provider = [&](CancellationToken) -> Result<AuthToken> {
        ++provider_calls;
        return AuthToken{"synthetic-token", "app/user"};
    };
    auto client = ApiClient::create(config, transport);
    check(bool(client), "control preflight client");
    // 原 output-batch-sealed 金样是完整合法正文；每个反例只改一个原位。
    auto body = Json::parse(
        R"({"contract":"terminal-services-v1","session":{"sessionContract":"sdk2-offload-v1","sessionId":"existing session / 原样"},"operation":{"operationId":"op-1","requestDigest":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},"executorId":"executor-1","connectionId":"connection-1","blocks":[{"seq":"0","byteOffset":"0","channel":"stdout","encoding":"utf-8","byteLength":1,"payloadDigest":"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb","base64":"YQ=="}],"seal":{"lastSeq":"0","totalBytes":"1","payloadDigest":"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb","truncated":false}})");
    check(bool(body) &&
              bool(validate_wire("terminal-services-v1", "OutputBatchRequest", body.value())),
          "complete positive control fixture");
    transport->reply = response(
        R"({"contract":"terminal-services-v1","operation":{"operationId":"op-1","requestDigest":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},"state":"receiving","acceptedThrough":"9007199254740993","durableThrough":null,"retainedFrom":"9007199254740993","nextByteOffset":"9223372036854775807","seal":null})");
    transport->reply.headers[3].second = "terminal";
    CallOptions valid;
    valid.parameters["id"] = "executor-1";
    valid.body = body.value();
    check(bool(client.value()->call("terminal.output.batch", valid)) && provider_calls == 1 &&
              transport->requests.size() == 1,
          "valid control fixture did not reach transport");
    provider_calls = 0;
    transport->requests.clear();
    auto rejected_without_send = [&](CallOptions invalid) {
        auto result = client.value()->call("terminal.output.batch", std::move(invalid));
        check(!result && provider_calls == 0 && transport->requests.empty(),
              "invalid control body invoked provider or transport");
    };
    for (const auto *token : {"-0", "1.0", "1e0", "9007199254740993"}) {
        auto invalid = valid;
        invalid.body->at("blocks")
            .at(std::size_t{0})
            .set("byteLength", Json::number(token).value());
        rejected_without_send(std::move(invalid));
    }
    auto duplicate = valid;
    duplicate.body->at("blocks").at(std::size_t{0}).as_object().emplace_back("base64", "YQ==");
    rejected_without_send(std::move(duplicate));
    auto unicode = valid;
    unicode.body->at("session").at("sessionId").as_string() = "\xff";
    rejected_without_send(std::move(unicode));
    auto capacity = valid;
    capacity.max_response_bytes = 1;
    rejected_without_send(std::move(capacity));
}
} // namespace
int main() {
    try {
        auto transport = std::make_shared<MockTransport>();
        int provider_calls = 0;
        std::string principal = "app/user";
        ClientOptions config;
        config.base_url = "https://serve.example.test";
        config.token_provider = [&](CancellationToken) -> Result<AuthToken> {
            ++provider_calls;
            return AuthToken{"synthetic-private-token", principal};
        };
        auto built = ApiClient::create(config, transport);
        check(bool(built), "client create");
        auto client = built.value();
        for (const auto *bad : {"https://user:pass@example.test", "https://example.test/api",
                                "file:///tmp/x", "https://example.test/?q=x",
                                "https://example.test/#x", "https://example.test\\@evil.test"}) {
            auto invalid = config;
            invalid.base_url = bad;
            check(!ApiClient::create(invalid, transport), "invalid origin accepted");
        }
        check(!client->call("not.real"), "unknown operation");
        CallOptions malformed;
        malformed.parameters["id"] = "../secret";
        check(!client->call("session.get", malformed), "invalid path");
        check(transport->requests.empty() && provider_calls == 0,
              "invalid input invoked provider/network");
        auto listed = client->call("session.list");
        check(bool(listed), "list call");
        check(transport->requests.back().url == "https://serve.example.test/api/sessions",
              "unified path");
        check(has_header(transport->requests.back(), "authorization",
                         "Bearer synthetic-private-token"),
              "auth header");
        check(has_header(transport->requests.back(), "tansr-session-family", "sdk1"),
              "family header");
        const auto before = transport->requests.size();
        CallOptions bad_query;
        bad_query.query["invented"] = "x";
        check(!client->call("session.list", bad_query), "unknown query accepted");
        check(transport->requests.size() == before, "unknown query sent");
        CancellationSource cancelled;
        cancelled.cancel();
        CallOptions stopped;
        stopped.cancel = cancelled.token();
        check(!client->call("session.list", stopped), "cancelled call accepted");
        check(transport->requests.size() == before, "cancelled call sent");
        CallOptions expired;
        expired.deadline_ms = unix_time_ms() - 1;
        check(!client->call("session.list", expired), "expired deadline accepted");
        check(transport->requests.size() == before, "expired request sent");
        transport->reply.headers.emplace_back("Tansr-Contract", "unified-v1");
        auto duplicate = client->call("session.list");
        check(!duplicate && duplicate.error().code == ErrorCode::contract,
              "duplicate response header accepted");
        transport->reply = response("[]");
        transport->reply.headers[2].second = "sha256:" + std::string(64, 'g');
        check(!client->call("session.list"), "schema fingerprint mismatch accepted");
        transport->reply = response("[]");
        transport->reply.headers[2].second = "none";
        check(bool(client->call("session.list")), "declared unregistered family hash rejected");
        CallOptions unlimited;
        unlimited.deadline_ms = 0;
        check(bool(client->call("session.list", unlimited)), "explicit no-total-deadline rejected");
        check(transport->requests.back().deadline_ms == 0, "zero deadline changed");
        check(!has_header(transport->requests.back(), "deadline", "1970-01-01T00:00:00.000Z"),
              "zero deadline serialized");
        transport->reply = response("[]");
        transport->reply.body = "{\"a\":1,\"a\":2}";
        check(!client->call("session.list"), "duplicate JSON accepted");
        transport->reply = response("[]");
        CallOptions write;
        write.parameters["id"] = "session-one";
        write.request_key = "original-id";
        write.deadline_ms = unix_time_ms() + 30000;
        write.body = Json::object({{"text", "hello"}});
        write.capability_closure = std::string(64, 'a');
        transport->reply = response("{}", 202);
        auto written = client->call("session.message.send", write);
        check(bool(written), "send call");
        const auto original = transport->requests.back();
        check(has_header(original, "idempotency-key", "original-id"), "original key");
        check(has_header(original, "tansr-closure-id", std::string(64, 'a')), "closure header");
        transport->reply =
            response("{\"contract\":\"unified-v1\",\"traceId\":\"trace\",\"requestId\":\"original-"
                     "id\",\"code\":\"conflict\",\"status\":409,\"retryAction\":\"same-request\","
                     "\"message\":\"secret-must-not-be-public\"}",
                     409);
        auto conflict = client->call("session.message.send", write);
        check(!conflict, "error response accepted");
        check(conflict.error().wire_code == "conflict" &&
                  conflict.error().retry_action == "same-request",
              "structured error lost");
        check(conflict.error().message.find("secret-must") == std::string::npos,
              "error leaked server text");
        auto mutated = conflict.error();
        mutated.retry_after_ms = 1;
        const auto before_mutated = transport->requests.size();
        check(!client->retry_same_request("session.message.send", write, mutated) &&
                  transport->requests.size() == before_mutated,
              "edited retry evidence was trusted");
        auto changed = write;
        changed.body = Json::object({{"text", "different"}});
        const auto count = transport->requests.size();
        check(!client->retry_same_request("session.message.send", changed, conflict.error()),
              "changed retry accepted");
        check(transport->requests.size() == count, "changed retry sent");
        transport->reply = response("{}", 202);
        check(bool(client->retry_same_request("session.message.send", write, conflict.error())),
              "same request retry failed");
        check(transport->requests.back().body == original.body, "retry changed original bytes");
        principal = "other/user";
        const auto old_count = transport->requests.size();
        check(!client->call("session.list"), "changed principal accepted");
        check(transport->requests.size() == old_count, "changed principal sent");
        principal = "app/user";
        client->shutdown();
        check(!client->call("session.list"), "closed client sent");
        event_boundaries();
        provider_deadline();
        retry_cancellation_boundaries();
        control_body_preflight();
        std::cout << "api: origin, preflight, headers, lexical JSON, cancellation, deadline, "
                     "replay and identity checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
