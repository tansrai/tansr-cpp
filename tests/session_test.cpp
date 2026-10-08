#include "tansr/operations.hpp"
#include "tansr/session.hpp"
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace tansr;
using namespace tansr::session;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> value) {
    if (!value)
        throw std::runtime_error(value.error().message);
    return std::move(value).value();
}
Json parse(std::string_view text) { return take(Json::parse(text)); }
std::string header(const Headers &headers, std::string_view key) {
    for (const auto &entry : headers)
        if (entry.first == key)
            return entry.second;
    return {};
}
HttpResponse reply(int status, Json body, std::string domain = "session") {
    return {status,
            {{"tansr-contract", "unified-v1"},
             {"tansr-manifest-revision", "7"},
             {"tansr-domain", std::move(domain)},
             {"tansr-schema-hash", "sha256:" + std::string(schema_hash)},
             {"content-type", "application/json"}},
            body.dump()};
}
HttpResponse discovery() {
    return reply(
        200,
        parse(
            R"({"protocol":"sdk2-ext-v1","contracts":[{"contract":"sdk1","availability":"legacy-complete"},{"contract":"sdk2-offload-v1","availability":"source-required"}]})"));
}
HttpResponse metadata(std::string family = "sdk1") {
    auto json = parse(R"({"sessionId":"s1","status":"idle","live":true,"lastSeq":0})");
    if (family == "sdk2-offload-v1") {
        json.set("contract", family);
        json.set("availability", "source-required");
    }
    return reply(200, std::move(json));
}
HttpResponse closure(std::string state = "enabled") {
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() / "contract" /
                      "unified-v1.schema.json";
    std::ifstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "frozen schema unavailable");
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    auto schema = parse(bytes);
    const auto &props = schema.at("definitions").at("CapabilityClosure").at("properties");
    auto operations = Json::object(), domains = Json::object();
    for (const auto &op : props.at("operations").at("required").as_array())
        operations.set(op.as_string(), state);
    for (const auto &domain : props.at("domains").at("required").as_array())
        domains.set(domain.as_string(), Json::object({{"installed", true}, {"revision", nullptr}}));
    auto result = reply(200,
                        Json::object({{"contract", "unified-v1"},
                                      {"closureId", std::string(64, 'a')},
                                      {"authorizationRevision", nullptr},
                                      {"domains", std::move(domains)},
                                      {"operations", std::move(operations)}}),
                        "discovery");
    result.headers.emplace_back("tansr-closure-id", std::string(64, 'a'));
    return result;
}
struct MemoryStream final : ByteStream {
    HttpResponse response_;
    std::deque<std::string> chunks;
    bool closed{false};
    Result<std::optional<std::string>> next(CancellationToken cancel) override {
        if (closed || cancel.is_cancelled())
            return Error{ErrorCode::cancelled, "test stream cancelled"};
        if (chunks.empty())
            return std::optional<std::string>{};
        auto chunk = std::move(chunks.front());
        chunks.pop_front();
        return std::optional<std::string>(std::move(chunk));
    }
    void cancel() noexcept override { closed = true; }
    const HttpResponse &response() const noexcept override { return response_; }
};
struct MockTransport final : HttpTransport {
    std::vector<HttpRequest> requests;
    std::function<Result<HttpResponse>(const HttpRequest &)> handler;
    std::shared_ptr<MemoryStream> event_stream;
    Result<HttpResponse> request(const HttpRequest &request, CancellationToken cancel) override {
        if (cancel.is_cancelled())
            return Error{ErrorCode::cancelled, "test request cancelled"};
        requests.push_back(request);
        return handler(request);
    }
    Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &request,
                                               CancellationToken cancel) override {
        if (cancel.is_cancelled())
            return Error{ErrorCode::cancelled, "test stream cancelled"};
        requests.push_back(request);
        if (!event_stream)
            return Error{ErrorCode::internal, "missing test stream"};
        return std::static_pointer_cast<ByteStream>(event_stream);
    }
};
SessionClient client(const std::shared_ptr<MockTransport> &transport, std::string family = "sdk1",
                     CancellationToken cancel = {}) {
    ClientOptions options;
    options.base_url = "http://127.0.0.1:54321";
    options.family = std::move(family);
    options.token_provider = [](CancellationToken) {
        return Result<AuthToken>(AuthToken{"fixture", "principal-1"});
    };
    return take(
        SessionClient::create(take(ApiClient::create(std::move(options), transport)), cancel));
}
bool ends(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}
WriteOptions write() {
    WriteOptions out;
    out.request_key = "retained-write";
    return out;
}
SessionEvent event(std::uint64_t seq, std::string kind, std::optional<std::string> terminal = {},
                   std::string turn = "current", std::string sid = "s1") {
    auto raw = Json::object(
        {{"type", kind}, {"sessionId", std::move(sid)}, {"seq", seq}, {"turnId", std::move(turn)}});
    return {Json::object({{"contract", "unified-v1"},
                          {"eventId", std::to_string(seq)},
                          {"domain", "session"},
                          {"type", std::move(kind)},
                          {"cursorSet", Json::object({{"eventCursor", std::to_string(seq)},
                                                      {"outputWatermark", nullptr},
                                                      {"materialConsumed", nullptr},
                                                      {"archiveCoverage", nullptr},
                                                      {"ackReceipt", nullptr}})},
                          {"terminalStatus", terminal ? Json(*terminal) : Json()},
                          {"raw", std::move(raw)}})};
}
std::string frame(const SessionEvent &value) {
    const auto &id = value.envelope.at("eventId");
    return (id.is_string() ? "id: " + id.as_string() + "\n" : "") +
           "event: session.event\ndata: " + value.envelope.dump() + "\n\n";
}
void trackers() {
    auto tracker = take(TurnTracker::create(4));
    require(!tracker.observe(event(4, "turn.completed", "completed", "old")),
            "old completion passed watermark");
    require(!tracker.observe(event(5, "turn.completed", "completed")),
            "terminal invented turn identity");
    require(!tracker.observe(event(6, "turn.started")), "start became completion");
    require(!tracker.observe(event(7, "turn.completed", "completed", "foreign")),
            "foreign turn completed tracker");
    auto done = tracker.observe(event(8, "turn.completed", "completed"));
    require(done && done->status == OutcomeStatus::completed, "current turn did not complete");
    require(!tracker.observe(event(9, "turn.completed", "completed")), "tracker completed twice");
    auto replay = take(TurnTracker::from_replay(10));
    replay.observe(event(1, "turn.started", {}, "old"));
    replay.observe(event(2, "turn.completed", "completed", "old"));
    require(!replay.active_turn_id(), "old replay terminal retained active identity");
    replay.observe(event(9, "turn.started"));
    require(replay.active_turn_id() == "current", "replay did not recover active turn");
    require(!replay.observe(event(11, "turn.completed", "completed", "old")),
            "replay accepted old turn");
    require(replay.observe(event(12, "turn.completed", "completed")).has_value(),
            "replayed active turn did not finish");
    auto resumed = take(TurnTracker::resume(10, "current"));
    require(!resumed.observe(event(10, "turn.completed", "completed")),
            "resume accepted old watermark");
    auto gap = event(11, "server.replay.gap");
    gap.envelope.set("eventId", nullptr);
    gap.envelope.at("cursorSet").set("eventCursor", nullptr);
    require(!resumed.observe(gap) && resumed.needs_reconciliation(),
            "gap did not invalidate tracker");
    require(!resumed.observe(event(12, "turn.completed", "completed")), "gap healed itself");
    require(!TurnTracker::create(max_safe_integer + 1), "unsafe watermark accepted");
    require(!TurnTracker::resume(0, std::string(129, 'x')), "long resumed turn accepted");
    auto failed = event(1, "turn.error", "aborted");
    failed.envelope.at("raw").set("recoverable", false);
    require(failed.turn_outcome()->status == OutcomeStatus::failed,
            "unrecoverable error conflated with abort");
    failed.envelope.at("raw").set("recoverable", true);
    require(!failed.turn_outcome(), "recoverable error became terminal");
    require(event(1, "session.ended", "completed").turn_outcome()->status ==
                OutcomeStatus::session_ended,
            "session end became success");
    require(event(1, "turn.future", "unknown").turn_outcome()->status == OutcomeStatus::unknown,
            "unknown outcome was lost");
}
void families_and_lost_create() {
    auto transport = std::make_shared<MockTransport>();
    transport->handler = [](const HttpRequest &request) -> Result<HttpResponse> {
        if (request.method == "GET")
            return discovery();
        const auto body = parse(request.body);
        auto response = parse(R"({"sessionId":"s1","lastSeq":0,"resumed":false})");
        if (header(request.headers, "tansr-session-family") == "sdk2-offload-v1") {
            response.set("contract", "sdk2-offload-v1");
            response.set("availability", "source-required");
        }
        return reply(body.contains("resume") ? 200 : 201, std::move(response));
    };
    auto legacy = client(transport);
    require(take(legacy.create()).id() == "s1", "legacy create failed");
    require(!take(legacy.resume("s1", write())).created().resumed, "live attach flag changed");
    auto offload = client(transport, "sdk2-offload-v1");
    const auto count = transport->requests.size();
    require(!offload.create(CreateOptions{}), "offload invented request identity");
    require(transport->requests.size() == count, "invalid create made request");
    CreateOptions options;
    options.request_id = "retained_request";
    require(take(offload.create(options)).id() == "s1", "offload create failed");
    require(take(offload.resume("s1", write())).id() == "s1", "offload resume failed");
    for (std::size_t i = 0; i < transport->requests.size(); i += 2)
        require(transport->requests[i].deadline_ms == transport->requests[i + 1].deadline_ms,
                "discovery renewed original deadline");
    transport->handler = [](const HttpRequest &request) -> Result<HttpResponse> {
        if (request.method == "GET")
            return discovery();
        return Error{ErrorCode::network, "response lost"};
    };
    const auto before = transport->requests.size();
    auto lost = legacy.create();
    require(!lost, "lost create became success");
    require(transport->requests.size() == before + 2,
            "lost create retried or queried guessed session");
}
void capability_and_input_boundaries() {
    auto transport = std::make_shared<MockTransport>();
    bool enabled = false, durable = false, foreign = false;
    transport->handler = [&](const HttpRequest &request) -> Result<HttpResponse> {
        if (ends(request.url, "/s1"))
            return metadata();
        if (request.url.find("/input-capabilities") != std::string::npos)
            return reply(200, Json::object({{"durableAck", durable}}));
        if (ends(request.url, "/capabilities"))
            return closure(enabled ? "enabled" : "disabled");
        if (request.url.find("/input") != std::string::npos) {
            auto receipt = parse(
                R"({"sessionId":"s1","inputId":"input-1","turnId":"current","historyEpoch":"epoch-1","source":"strict","state":"accepted","durability":"memory","ordinal":1,"revision":1})");
            if (foreign)
                receipt.set("historyEpoch", "epoch-2");
            return reply(202,
                         Json::object({{"outcome", "accepted"}, {"receipt", std::move(receipt)}}));
        }
        return Error{ErrorCode::network, "accepted response lost"};
    };
    auto session = take(client(transport).attach("s1"));
    require(!session.send(u8"　\u00a0"), "Unicode blank prompt accepted");
    require(!session.send("one", write()), "disabled send succeeded");
    require(transport->requests.size() == 2, "disabled send wrote");
    enabled = true;
    auto result = session.send("two", write());
    require(!result, "lost send became completed");
    require(transport->requests.size() == 4, "lost send retried");
    require(header(transport->requests.back().headers, "idempotency-key") == "retained-write",
            "request key lost");
    require(header(transport->requests.back().headers, "tansr-closure-id") == std::string(64, 'a'),
            "closure not forwarded");
    require(transport->requests[2].deadline_ms == transport->requests[3].deadline_ms,
            "closure refreshed deadline");
    Input input{"input-1",
                {"epoch-1", "current"},
                InputContent{std::string("same turn"), {}},
                std::string("durable")};
    auto before = transport->requests.size();
    require(!session.submit_input(input, write()), "durable unsupported fell back");
    require(transport->requests.size() == before + 1, "durable unsupported performed write");
    input.ack = "memory";
    auto accepted = take(session.submit_input(input, write()));
    require(accepted.at("receipt").at("state").as_string() == "accepted",
            "acceptance was promoted to consumption");
    foreign = true;
    require(!session.submit_input(input, write()), "foreign epoch accepted");
}
void checkpoints_and_controls() {
    auto transport = std::make_shared<MockTransport>();
    const std::string bytes = "{\n  \"raw\": \"snapshot\"\n}\n";
    transport->handler = [&](const HttpRequest &request) -> Result<HttpResponse> {
        if (ends(request.url, "/s1") && request.method == "GET")
            return metadata();
        if (ends(request.url, "/capabilities"))
            return closure();
        if (request.url.find("/history") != std::string::npos)
            return reply(200, parse(R"({"messages":[],"total":7})"));
        if (ends(request.url, "/export")) {
            auto out = reply(200, Json::object());
            out.body = bytes;
            out.headers.back().second = "application/octet-stream";
            return out;
        }
        if (request.url.find("/import") != std::string::npos) {
            require(request.body == bytes, "checkpoint raw bytes reencoded");
            return reply(201, parse(R"({"checkpointId":"c1","sessionId":"s1","messageCount":7})"));
        }
        if (ends(request.url, "/compact"))
            return reply(200, parse(R"({"status":"failed","reason":"model_error"})"));
        return reply(202, parse(R"({"sessionId":"s1","accepted":true})"));
    };
    auto session = take(client(transport).attach("s1"));
    require(take(session.history(0, 0)).at("total").as_u64() == 7, "limit zero rejected");
    require(take(session.export_checkpoint("c1")) == bytes, "checkpoint export bytes changed");
    require(take(session.import_checkpoint(bytes, "retained", write())).message_count == 7,
            "checkpoint import failed");
    require(take(session.compact({}, write())).at("status").as_string() == "failed",
            "compaction failure became success");
    const auto before = transport->requests.size();
    require(take(session.close(write())).accepted, "dormant close failed");
    require(transport->requests.size() == before + 1 &&
                header(transport->requests.back().headers, "tansr-closure-id").empty(),
            "close required live closure");
    require(!session.checkpoint(std::string(121, 'a')), "long checkpoint label accepted");
    SpeechRequest speech;
    speech.input = "hello";
    speech.format = "aac";
    require(!session.speak(speech), "unsupported speech format accepted");
}
void observation() {
    auto transport = std::make_shared<MockTransport>();
    transport->handler = [](const HttpRequest &) -> Result<HttpResponse> { return metadata(); };
    transport->event_stream = std::make_shared<MemoryStream>();
    auto &wire = *transport->event_stream;
    wire.response_ = reply(200, Json());
    wire.response_.headers.back().second = "text/event-stream";
    wire.response_.headers.emplace_back("tansr-event-envelope", "unified-v1");
    wire.chunks.push_back(frame(event(1, "turn.started")) + frame(event(1, "turn.started")) +
                          frame(event(2, "turn.completed", "completed")));
    CancellationSource parent;
    auto session = take(client(transport).attach("s1"));
    auto stream = take(session.events({}, parent.token()));
    require(!stream.last_event_id(), "buffered cursor delivered early");
    require(take(stream.next()).has_value() && stream.last_event_id() == "1",
            "first delivery cursor incorrect");
    require(take(stream.next())->turn_outcome()->status == OutcomeStatus::completed &&
                stream.last_event_id() == "2",
            "duplicate was delivered");
    require(!take(stream.next()), "EOF fabricated event");
    require(!parent.token().is_cancelled(), "stream close cancelled caller token");
    for (const auto *cursor : {"01", "+1", "-0", "1.0", "1e1", "9007199254740992", " 1"})
        require(!session.events(std::string(cursor)), "noncanonical cursor accepted");
    auto response = wire.response_;
    transport->event_stream = std::make_shared<MemoryStream>();
    transport->event_stream->response_ = std::move(response);
    transport->event_stream->chunks.push_back(
        frame(event(3, "turn.completed", "completed", "current", "foreign")));
    auto foreign = take(session.events("2"));
    require(!foreign.next(), "foreign session event delivered");
    require(foreign.last_event_id() == "2" && transport->event_stream->closed,
            "foreign event advanced cursor or kept stream open");
}
} // namespace
int main() {
    try {
        trackers();
        families_and_lost_create();
        capability_and_input_boundaries();
        checkpoints_and_controls();
        observation();
        std::cout << "session: 5 boundary groups passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "session: " << error.what() << '\n';
        return 1;
    }
}
