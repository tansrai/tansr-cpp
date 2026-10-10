#include "tansr/canonical.hpp"
#include "tansr/crypto.hpp"
#include "tansr/executor.hpp"
#include "tansr/operations.hpp"
#include "tansr/storage.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
using namespace tansr;
namespace ex = tansr::executor;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result.value());
}
Json parse(std::string_view v) { return take(Json::parse(v)); }
HttpResponse reply(Json value, int code = 200, std::string domain = "execution") {
    return {code,
            {{"content-type", "application/json"},
             {"tansr-contract", "unified-v1"},
             {"tansr-manifest-revision", "7"},
             {"tansr-schema-hash", "sha256:" + std::string(schema_hash)},
             {"tansr-domain", std::move(domain)}},
            value.dump()};
}
ex::Scope scope() { return {"app", "user", "1"}; }
ex::Connection connection() { return {"device", "connection", "1", "2099-01-01T00:00:00Z", 1000}; }
ex::Registration registration() {
    return {"device",
            ex::Platform::current(),
            {{"workspace", "1"}},
            {"tool.invoke"},
            {{"Lookup", std::string(64, 'a')}},
            {}};
}
ex::Operation operation() {
    ex::Operation op{
        "op",
        "session",
        scope(),
        {"binding", "1", {"device", "connection", "1", "workspace", "1", {}}},
        "Lookup",
        {"tool.invoke", Json::object({{"name", "Lookup"},
                                      {"definitionDigest", std::string(64, 'a')},
                                      {"argsJson", "{\"value\":-1.5,\"名称\":true}"}})},
        "",
        "2099-01-01T00:00:00Z"};
    op.digest = take(ex::operation_digest(op));
    return op;
}
Json okay() {
    return parse(R"({"status":"ok","content":[{"t":"text","text":"client-only fact"}]})");
}
class MemoryJournal final : public ex::Journal {
  public:
    std::mutex gate;
    std::optional<std::string> claim_digest;
    std::optional<ex::Receipt> result;
    std::atomic<int> claims{0}, completes{0};
    Result<ex::ClaimResult> claim(const ex::Operation &op) override {
        std::lock_guard<std::mutex> lock(gate);
        ++claims;
        if (!claim_digest) {
            claim_digest = op.digest;
            return ex::ClaimResult{ex::ClaimState::claimed, {}};
        }
        if (*claim_digest != op.digest)
            return Error{ErrorCode::conflict, "digest conflict"};
        if (result)
            return ex::ClaimResult{ex::ClaimState::receipt, result};
        return ex::ClaimResult{ex::ClaimState::pending, {}};
    }
    Result<void> complete(const ex::Operation &op, const ex::Receipt &receipt) override {
        auto valid = ex::validate_receipt(op, receipt);
        if (!valid)
            return valid;
        std::lock_guard<std::mutex> lock(gate);
        ++completes;
        if (!claim_digest || *claim_digest != op.digest)
            return Error{ErrorCode::conflict, "missing claim"};
        if (result && ex::to_json(*result).dump() != ex::to_json(receipt).dump())
            return Error{ErrorCode::conflict, "different receipt"};
        result = receipt;
        return {};
    }
};
class Fixture final : public HttpTransport {
  public:
    std::mutex gate;
    ex::Operation op = operation();
    std::optional<ex::Receipt> receipt;
    std::vector<std::string> routes;
    Json::Array blocks;
    std::string bytes;
    Json seal;
    std::atomic<int> posts{0}, statuses{0}, heartbeats{0};
    bool output_failure{false}, lose_first{false}, forge_offset{false}, restricted{false};
    std::atomic<bool> block_upload{false}, upload_started{false}, release_upload{false},
        stall_status{false}, revoke{false};
    std::string output_state_override;
    std::function<void()> receipt_accepted;
    bool lose_receipt{false}, stale_status_once{false}, forge_seal{false}, gap_after_upload{false};
    std::string heartbeat_revision{"1"};
    std::atomic<int> output_queries{0};
    std::function<void(int)> output_queried;
    std::vector<std::string> batch_bodies;
    Json output_status() {
        auto seq = blocks.empty() ? Json() : blocks.back().at("seq");
        return Json::object(
            {{"contract", "terminal-services-v1"},
             {"operation",
              Json::object({{"operationId", op.operation_id}, {"requestDigest", op.digest}})},
             {"state",
              output_state_override.empty()
                  ? (seal.is_null() ? (blocks.empty() ? "available" : "receiving")
                                    : (seal.at("truncated").as_bool() ? "truncated" : "complete"))
                  : output_state_override},
             {"acceptedThrough", seq},
             {"durableThrough", seq},
             {"retainedFrom", blocks.empty() ? Json() : Json("0")},
             {"nextByteOffset", std::to_string(bytes.size() + (forge_offset ? 1U : 0U))},
             {"seal", seal}});
    }
    Result<HttpResponse> request(const HttpRequest &request, CancellationToken cancel) override {
        if (cancel.is_cancelled())
            return Error{ErrorCode::cancelled, "fixture cancelled"};
        const bool batch = request.url.find("/output-batches") != std::string::npos;
        const bool state = request.url.find("/executions/") != std::string::npos;
        if (batch) {
            upload_started.store(true);
            while (block_upload && !release_upload) {
                if (cancel.wait_for(std::chrono::milliseconds(10)))
                    return Error{ErrorCode::cancelled, "fixture cancelled"};
            }
        }
        if (state && ++statuses > 1 && stall_status) {
            while (!cancel.is_cancelled() && unix_time_ms() < request.deadline_ms)
                cancel.wait_for(std::chrono::milliseconds(10));
            return Error{ErrorCode::cancelled, "slow fixture cancelled"};
        }
        std::lock_guard<std::mutex> lock(gate);
        routes.push_back(request.url);
        if (revoke)
            return Error{ErrorCode::permission, "fixture revoked"};
        if (request.url.find("/heartbeats") != std::string::npos) {
            ++heartbeats;
            return reply(Json::object({{"protocol", ex::protocol},
                                       {"executorId", "device"},
                                       {"connectionId", "connection"},
                                       {"connectionRevision", heartbeat_revision},
                                       {"expiresAt", connection().expires_at},
                                       {"heartbeatAfterMs", 1000}}));
        }
        if (request.url.find("/operations?") != std::string::npos)
            return reply(Json::object(
                {{"protocol", ex::protocol},
                 {"executorId", "device"},
                 {"connectionId", "connection"},
                 {"operations", receipt ? Json::array() : Json::array({ex::to_json(op)})}}));
        if (batch || request.url.find("/tool-output-status") != std::string::npos) {
            if (!batch) {
                ++output_queries;
                if (output_queried)
                    output_queried(output_queries.load());
                if (stale_status_once && posts == 1 && output_queries == 1) {
                    auto stale = output_status();
                    stale.set("state", "available");
                    stale.set("acceptedThrough", Json());
                    stale.set("durableThrough", Json());
                    stale.set("retainedFrom", Json());
                    stale.set("nextByteOffset", "0");
                    stale.set("seal", Json());
                    return reply(std::move(stale), 200, "terminal");
                }
            }
            if (batch) {
                ++posts;
                batch_bodies.push_back(request.body);
                if (output_failure)
                    return Error{ErrorCode::network, "output response lost"};
                auto b = parse(request.body);
                for (const auto &block : b.at("blocks").as_array()) {
                    const auto sequence = std::stoull(block.at("seq").as_string());
                    if (sequence < blocks.size()) {
                        check(block.dump() == blocks[static_cast<std::size_t>(sequence)].dump(),
                              "retry changed original block");
                        continue;
                    }
                    check(block.at("seq").as_string() == std::to_string(blocks.size()),
                          "duplicated/out-of-order output");
                    check(block.at("byteOffset").as_string() == std::to_string(bytes.size()),
                          "wrong byte offset");
                    auto part = take(crypto::base64_decode(block.at("base64").as_string()));
                    std::string text(part.begin(), part.end());
                    check(take(crypto::sha256_hex(text)) == block.at("payloadDigest").as_string(),
                          "wrong block digest");
                    bytes += text;
                    blocks.push_back(block);
                }
                if (!b.at("seal").is_null())
                    seal = b.at("seal");
                if (forge_seal && !seal.is_null())
                    seal.set("payloadDigest", std::string(64, 'f'));
                if (gap_after_upload && !blocks.empty())
                    output_state_override = "gap";
                if (lose_first && posts == 1)
                    return Error{ErrorCode::network, "accepted response lost"};
            }
            return reply(output_status(), 200, "terminal");
        }
        if (request.url.find("/receipts") != std::string::npos) {
            auto body = parse(request.body);
            check(body.at("operationId").as_string() == op.operation_id, "receipt identity");
            ex::Receipt r{body.at("executorId").as_string(),
                          body.at("connectionId").as_string(),
                          body.at("operationId").as_string(),
                          body.at("digest").as_string(),
                          body.at("status").as_string(),
                          {},
                          {}};
            if (!body.at("result").is_null())
                r.result = ex::Resource{body.at("result").at("operation").as_string(),
                                        body.at("result").at("args")};
            if (!body.at("errorCode").is_null())
                r.error_code = body.at("errorCode").as_string();
            receipt = std::move(r);
            if (receipt_accepted)
                receipt_accepted();
            if (lose_receipt)
                return Error{ErrorCode::network, "accepted receipt response lost"};
        }
        auto s = Json::object({{"protocol", ex::protocol},
                               {"operation", ex::to_json(op)},
                               {"status", receipt ? receipt->status : "pending"},
                               {"receipt", receipt ? ex::to_json(*receipt) : Json()}});
        if (request.url.find("/terminal/") != std::string::npos)
            return reply(Json::object({{"contract", "terminal-services-v1"},
                                       {"session", Json::object({{"sessionContract", "sdk1"},
                                                                 {"sessionId", op.session_id}})},
                                       {"execution", s}}),
                         200, "terminal");
        if (restricted)
            return Error{ErrorCode::permission, "controller endpoint forbidden"};
        return reply(s);
    }
    Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &, CancellationToken) override {
        return Error{ErrorCode::internal, "not used"};
    }
};
std::shared_ptr<ApiClient> api(const std::shared_ptr<Fixture> &fixture) {
    ClientOptions o;
    o.base_url = "https://serve.example.test";
    o.token_provider = [](CancellationToken) -> Result<AuthToken> {
        return AuthToken{"fixture", "app/user"};
    };
    return take(ApiClient::create(o, fixture));
}
ex::OutputLimits limits(std::size_t pending = 8192, std::size_t block = 16) {
    return {4096, block, 1024, pending, 8192};
}
std::shared_ptr<ex::OutputWriter> writer(const std::shared_ptr<Fixture> &fixture,
                                         std::size_t pending = 8192, std::size_t block = 16) {
    return take(
        ex::OutputWriter::create(api(fixture), {{"sdk1", "session"},
                                                {fixture->op.operation_id, fixture->op.digest},
                                                "device",
                                                "connection",
                                                limits(pending, block),
                                                "binary",
                                                {},
                                                unix_time_ms() + 5000}));
}
std::shared_ptr<ex::Runner>
runner(const std::shared_ptr<Fixture> &f, const std::shared_ptr<ex::Journal> &journal,
       ex::ToolHandler handler, bool output = false, ex::Authorizer auth = {},
       ex::Registration registered = registration(), ex::Connection connected = connection(),
       bool require_output = true) {
    ex::RunnerOptions o;
    o.client = take(ex::Client::create(api(f), scope()));
    o.registration = std::move(registered);
    o.journal = journal;
    o.tools = {{"Lookup", {std::string(64, 'a'), std::move(handler)}}};
    o.authorize =
        auth ? auth : [](const ex::Operation &, CancellationToken) -> Result<void> { return {}; };
    o.poll_interval = std::chrono::milliseconds(10);
    if (output)
        o.terminal = ex::TerminalOptions{"sdk1", limits()};
    o.require_output = output && require_output;
    o.restricted_status = f->restricted;
    return take(ex::Runner::create(std::move(o), std::move(connected)));
}
class HookJournal final : public ex::Journal {
  public:
    explicit HookJournal(std::shared_ptr<ex::Journal> original) : inner(std::move(original)) {}
    std::shared_ptr<ex::Journal> inner;
    std::function<void()> before_claim, after_claim, after_complete;
    bool lose_claim{false}, lose_complete{false};
    Result<ex::ClaimResult> claim(const ex::Operation &op) override {
        if (before_claim)
            before_claim();
        auto result = inner->claim(op);
        if (result && after_claim)
            after_claim();
        if (result && lose_claim)
            return Error{ErrorCode::unknown, "committed claim response lost"};
        return result;
    }
    Result<void> complete(const ex::Operation &op, const ex::Receipt &receipt) override {
        auto result = inner->complete(op, receipt);
        if (result && after_complete)
            after_complete();
        if (result && lose_complete)
            return Error{ErrorCode::unknown, "committed receipt response lost"};
        return result;
    }
};
void wait_until(const std::function<bool()> &predicate, const char *message) {
    const auto deadline = unix_time_ms() + 3000;
    while (!predicate() && unix_time_ms() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(predicate(), message);
}
void declaration_and_arguments() {
    auto declaration = parse(
        u8R"({"name":"UnicodeLookup","description":"订单 😀 𝄞 é é \u2028\u2029","parameters":{"orderId":{"type":"string","description":"订单 😀"},"10":{"type":"number"},"2":{"type":"string"}}})");
    check(take(ex::definition_digest(declaration)) ==
              "9502c0d7cd61033b20b49f2de253a236041c8519d044288b10e8d5f8f7538ba4",
          "independent Node digest");
    declaration.set("readOnly", false);
    check(take(ex::definition_digest(declaration)) ==
              "76515ae630a3db646d0ae7dc798dc38d8fd01d0c996fbccfb952fcf537a9b7a8",
          "absent/false digest");
    declaration.set("parameters", parse(R"({"__proto__":{"type":"string"}})"));
    check(!ex::definition_digest(declaration), "prototype accepted");
    for (const auto *text :
         {u8R"({"n":-1.25,"名称":true})", R"({"n":1e3})", R"({"n":-0})", R"({"n":1e-400})",
          R"({"n":-1e-400})", R"({"n":0e99999})", R"({"n":1.7976931348623157e308})",
          R"({"n":-1.7976931348623157e308})", R"({"n":1.7976931348623158e308})"})
        check(bool(ex::parse_tool_arguments(text)), "ordinary numeric JSON rejected");
    for (const auto *text :
         {R"({"x":1,"x":2})", R"({"s":"\ud800"})", R"({"n":1e400})", R"({"n":-1e400})",
          R"({"n":1.7976931348623159e308})", R"({"n":-1.7976931348623159e308})", R"([])"})
        check(!ex::parse_tool_arguments(text), "invalid argument accepted");
    check(bool(ex::verify_tool_result(parse(R"({"status":"error","message":"business error"})"))),
          "business error not deterministic");
    check(!ex::verify_tool_result(parse(R"({"status":"ok","content":[]})")),
          "empty result accepted");
}
void journal_facts() {
    auto random = take(crypto::random_bytes(12));
    std::string suffix =
        take(crypto::sha256_hex(std::string(random.begin(), random.end()))).substr(0, 16);
    auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                  ("tansr-executor-" + suffix);
    std::filesystem::create_directory(parent);
    auto path = parent / "private";
    auto access = []() -> Result<void> { return {}; };
    auto j = take(ex::FileJournal::open(path, access));
    check(!ex::FileJournal::open(path, access), "concurrent directory lock accepted");
    auto op = operation();
    check(take(j->claim(op)).state == ex::ClaimState::claimed, "initial durable claim");
    j.reset();
    j = take(ex::FileJournal::open(path, access));
    check(take(j->claim(op)).state == ex::ClaimState::pending, "claim lost on reopen");
    ex::Receipt r{
        "device", "connection", "op", op.digest, "unknown", {}, "execution_outcome_unknown"};
    check(bool(j->complete(op, r)), "persist unknown receipt");
    j.reset();
    j = take(ex::FileJournal::open(path, access));
    check(take(j->claim(op)).state == ex::ClaimState::receipt, "receipt lost on reopen");
    auto changed = op;
    changed.expires_at = "2098-01-01T00:00:00Z";
    changed.digest = take(ex::operation_digest(changed));
    check(!j->claim(changed), "different digest accepted");
    j.reset();
    bool check_reentry = false;
    auto callback = [&]() -> Result<void> {
        if (j && check_reentry) {
            auto nested = j->claim(op);
            check(!nested && nested.error().code == ErrorCode::reentrant,
                  "journal callback reentry not rejected");
        }
        return {};
    };
    j = take(ex::FileJournal::open(path, callback));
    check_reentry = true;
    check(take(j->claim(op)).state == ex::ClaimState::receipt, "journal callback reentry");
    j.reset();
    auto k = take(
        crypto::sha256_hex(take(canonical::encode(Json::array({"app", "user", "device", "op"})))));
    std::filesystem::remove(path / (k + ".claim"));
    j = take(ex::FileJournal::open(path, access));
    check(!j->claim(op) && !std::filesystem::exists(path / (k + ".claim")),
          "orphan receipt recreated execution permission");
    j.reset();
    {
        std::ofstream file(path / (k + ".claim"), std::ios::binary | std::ios::trunc);
        file << "partial";
    }
    j = take(ex::FileJournal::open(path, access));
    check(!j->claim(op), "corrupt claim became permission");
    j.reset();
    std::filesystem::remove_all(parent);
}
void deterministic_and_unknown() {
    for (int mode = 0; mode < 6; ++mode) {
        auto f = std::make_shared<Fixture>();
        auto j = std::make_shared<MemoryJournal>();
        std::atomic<int> calls{0};
        auto r = runner(f, j, [&, mode](ex::ToolContext, Json args) -> ex::ToolResult {
            ++calls;
            check(j->claim_digest.has_value(), "handler before claim");
            check(args.at("value").number_token() == "-1.5", "business args changed");
            if (mode == 0)
                return okay();
            if (mode == 1)
                return ex::ToolFailure::unknown();
            if (mode == 2)
                return ex::ToolFailure::rejected("not_found");
            if (mode == 3)
                throw std::runtime_error("fixture panic");
            if (mode == 5)
                return parse(R"({"status":"ok","content":[]})");
            return parse(R"({"status":"error","message":"business unavailable"})");
        });
        auto first = take(r->execute(f->op));
        check(first.status == (mode == 0 || mode == 4 ? "completed"
                               : mode == 2            ? "failed"
                                                      : "unknown"),
              "wrong business fact");
        check(j->result && j->completes == 1 &&
                  ex::to_json(*j->result).dump() == ex::to_json(first).dump(),
              "business fact not persisted before return");
        if (mode == 5)
            check(first.error_code == "invalid_tool_result" && !first.result,
                  "invalid handler result became a confirmed business result");
        auto repeated = take(r->execute(f->op));
        check(ex::to_json(repeated).dump() == ex::to_json(first).dump() && calls == 1 &&
                  j->completes == 1,
              "duplicate executed handler");
    }
    auto f = std::make_shared<Fixture>();
    auto j = std::make_shared<MemoryJournal>();
    take(j->claim(f->op));
    int calls = 0;
    auto r = runner(f, j, [&](ex::ToolContext, Json) -> ex::ToolResult {
        ++calls;
        return okay();
    });
    check(take(r->execute(f->op)).status == "unknown" && calls == 0, "only-claim reexecuted");
}
void authorization() {
    auto f = std::make_shared<Fixture>();
    auto j = std::make_shared<MemoryJournal>();
    int authorizations = 0, calls = 0;
    auto r = runner(
        f, j,
        [&](ex::ToolContext, Json) -> ex::ToolResult {
            ++calls;
            return okay();
        },
        false,
        [&](const ex::Operation &, CancellationToken) -> Result<void> {
            if (++authorizations >= 2)
                return Error{ErrorCode::permission, "denied"};
            return {};
        });
    check(take(r->execute(f->op)).status == "failed" && calls == 0,
          "handler before second authorization");
    auto wrong = f->op;
    wrong.scope.end_user_id = "other";
    wrong.digest = take(ex::operation_digest(wrong));
    check(!r->execute(wrong), "foreign user accepted");
    auto f2 = std::make_shared<Fixture>();
    auto j2 = std::make_shared<MemoryJournal>();
    std::atomic<bool> started{false};
    CancellationSource stop;
    auto r2 = runner(f2, j2, [&](ex::ToolContext context, Json) -> ex::ToolResult {
        started = true;
        while (!context.cancellation.wait_for(std::chrono::milliseconds(10))) {
        }
        return ex::ToolFailure::unknown();
    });
    std::thread cancel([&] {
        while (!started)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        stop.cancel();
    });
    auto result = r2->execute(f2->op, stop.token());
    cancel.join();
    check(bool(result) && result.value().status == "unknown", "cancel forged no side effect");
    auto restricted = std::make_shared<Fixture>();
    restricted->restricted = true;
    auto restricted_runner = runner(
        restricted, std::make_shared<MemoryJournal>(),
        [](ex::ToolContext, Json) -> ex::ToolResult { return okay(); }, true);
    check(take(restricted_runner->execute(restricted->op)).status == "completed",
          "restricted execution state unavailable");
    for (const auto &route : restricted->routes)
        check(route.find("/sessions/session/executions/") == std::string::npos,
              "restricted runner used controller state");
    auto denied = std::make_shared<Fixture>();
    denied->restricted = true;
    denied->revoke = true;
    auto denied_journal = std::make_shared<MemoryJournal>();
    int denied_calls = 0;
    auto denied_runner = runner(
        denied, denied_journal,
        [&](ex::ToolContext, Json) -> ex::ToolResult {
            ++denied_calls;
            return okay();
        },
        true);
    check(!denied_runner->execute(denied->op) && denied_calls == 0 && denied_journal->claims == 0,
          "restricted failure ran handler");
    for (const auto &route : denied->routes)
        check(route.find("/sessions/session/executions/") == std::string::npos,
              "restricted failure escalated to controller");
    auto reentrant = std::make_shared<Fixture>();
    std::shared_ptr<ex::Runner> reentrant_runner;
    reentrant_runner = runner(
        reentrant, std::make_shared<MemoryJournal>(),
        [&](ex::ToolContext, Json) -> ex::ToolResult {
            auto nested = reentrant_runner->execute(reentrant->op);
            check(!nested && nested.error().code == ErrorCode::reentrant, "handler reentry");
            return okay();
        },
        false,
        [&](const ex::Operation &, CancellationToken) -> Result<void> {
            auto nested = reentrant_runner->execute(reentrant->op);
            if (nested || nested.error().code != ErrorCode::reentrant)
                return Error{ErrorCode::internal, "authorizer reentry"};
            return {};
        });
    check(take(reentrant_runner->execute(reentrant->op)).status == "completed",
          "callback reentry deadlocked or changed fact");
}
void output_transport() {
    auto f = std::make_shared<Fixture>();
    f->lose_first = true;
    auto w = writer(f, 8192, 2);
    check(take(w->capture("stdout", std::string("\xf0\x9f\x98", 3))) == 3, "capture first");
    check(take(w->capture("stderr", std::string("\x80!", 2))) == 2, "capture second");
    auto status = take(w->finish());
    check(status.state == "complete" && f->bytes == std::string("\xf0\x9f\x98\x80!", 5),
          "bytes reordered/reencoded");
    check(status.seal->payload_digest == take(crypto::sha256_hex(f->bytes)), "seal digest");
    check(w->snapshot().pending_bytes == 0 && !w->capture("stdout", "late"), "seal acceptance");
    auto slow = std::make_shared<Fixture>();
    slow->block_upload = true;
    auto bounded = writer(slow, 520, 16);
    take(bounded->capture("stdout", "first-in-flight!"));
    while (!slow->upload_started)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(bounded->snapshot().pending_bytes > 0, "inflight released early");
    auto kept = take(bounded->capture("stderr", std::string(8192, 'x')));
    check(kept < 8192 && bounded->snapshot().pending_bytes <= 520, "pending unbounded");
    check(take(bounded->capture("stdout", "discard")) == 0, "prefix resumed after truncation");
    slow->release_upload = true;
    check(take(bounded->finish()).state == "truncated", "truncated seal lost");
    auto forged = std::make_shared<Fixture>();
    forged->forge_offset = true;
    auto bad = writer(forged);
    take(bad->capture("stdout", "once"));
    check(!bad->finish() && bad->snapshot().pending_bytes > 0, "impossible ACK consumed prefix");
}
void identity_matrix() {
    using Mutation = std::pair<const char *, std::function<void(ex::Operation &)>>;
    const std::vector<Mutation> mutations{
        {"application", [](auto &o) { o.scope.application_scope_id = "foreign"; }},
        {"user", [](auto &o) { o.scope.end_user_id = "foreign"; }},
        {"authorization", [](auto &o) { o.scope.authorization_revision = "2"; }},
        {"session", [](auto &o) { o.session_id = "foreign"; }},
        {"executor", [](auto &o) { o.binding.target.executor_id = "foreign"; }},
        {"connection", [](auto &o) { o.binding.target.connection_id = "foreign"; }},
        {"generation", [](auto &o) { o.binding.target.connection_revision = "2"; }},
        {"workspace", [](auto &o) { o.binding.target.workspace_id = "foreign"; }},
        {"workspace revision", [](auto &o) { o.binding.target.workspace_revision = "2"; }},
        {"binding", [](auto &o) { o.binding.binding_id = "foreign"; }},
        {"binding revision", [](auto &o) { o.binding.revision = "2"; }},
        {"lease", [](auto &o) { o.expires_at = "2000-01-01T00:00:00Z"; }},
        {"missing tool",
         [](auto &o) {
             o.tool_name = "Shell";
             o.request.args.set("name", "Shell");
         }},
        {"definition",
         [](auto &o) { o.request.args.set("definitionDigest", std::string(64, 'b')); }}};
    for (const auto &[name, mutate] : mutations) {
        auto f = std::make_shared<Fixture>();
        auto j = std::make_shared<MemoryJournal>();
        int calls = 0;
        auto r = runner(f, j, [&](ex::ToolContext, Json) -> ex::ToolResult {
            ++calls;
            return okay();
        });
        auto wrong = f->op;
        mutate(wrong);
        wrong.digest = take(ex::operation_digest(wrong));
        check(!r->execute(wrong) && calls == 0 && j->claims == 0, name);
    }
    auto f = std::make_shared<Fixture>();
    auto j = std::make_shared<MemoryJournal>();
    int calls = 0;
    auto r = runner(f, j, [&](ex::ToolContext, Json) -> ex::ToolResult {
        ++calls;
        return okay();
    });
    auto wrong = f->op;
    wrong.digest = std::string(64, 'b');
    check(!r->execute(wrong) && calls == 0 && j->claims == 0, "digest substitution");
    auto expired = connection();
    expired.expires_at = "2000-01-01T00:00:00Z";
    bool refused = false;
    try {
        (void)runner(
            f, j, [](ex::ToolContext, Json) -> ex::ToolResult { return okay(); }, false, {},
            registration(), expired);
    } catch (const std::exception &) {
        refused = true;
    }
    check(refused, "expired connection accepted");
}
void active_revocation_and_workspaces() {
    for (int mode = 0; mode < 6; ++mode) {
        auto f = std::make_shared<Fixture>();
        auto j = std::make_shared<MemoryJournal>();
        std::atomic<int> calls{0};
        std::atomic<bool> cancelled{false};
        auto r = runner(f, j, [&](ex::ToolContext ctx, Json) -> ex::ToolResult {
            ++calls;
            {
                std::lock_guard<std::mutex> lock(f->gate);
                if (mode == 0)
                    f->revoke = true;
                if (mode == 1)
                    f->op.scope.end_user_id = "foreign";
                if (mode == 2)
                    f->op.binding.revision = "2";
                if (mode == 3)
                    f->op.binding.target.connection_revision = "2";
                if (mode == 4)
                    f->op.expires_at = "2000-01-01T00:00:00Z";
                if (mode == 5)
                    f->heartbeat_revision = "2";
                f->op.digest = take(ex::operation_digest(f->op));
            }
            cancelled = ctx.cancellation.wait_for(std::chrono::seconds(3));
            return ex::ToolFailure::unknown();
        });
        const auto original = operation();
        if (mode == 5) {
            auto result = r->run({});
            check(!result && f->heartbeats >= 1 && j->result && j->result->status == "unknown",
                  "renewal generation change ignored");
        } else {
            auto result = take(r->execute(original));
            check(result.status == "unknown", "active revocation forged a known result");
        }
        check(calls == 1 && j->completes == 1 && cancelled,
              "revocation cancellation was not observed");
    }
    auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                  ("tansr-workspaces-" +
                   take(crypto::sha256_hex(std::to_string(unix_time_ms()))).substr(0, 16));
    std::filesystem::create_directory(parent);
    auto journal =
        take(ex::FileJournal::open(parent / "journal", []() -> Result<void> { return {}; }));
    std::atomic<int> entered{0}, finished{0};
    std::vector<std::thread> workers;
    std::atomic<bool> valid{true};
    for (int i = 0; i < 2; ++i) {
        workers.emplace_back([&, i] {
            try {
                auto f = std::make_shared<Fixture>();
                auto reg = registration();
                reg.workspaces[0].workspace_id += std::to_string(i);
                f->op.binding.target.workspace_id = reg.workspaces[0].workspace_id;
                f->op.operation_id += std::to_string(i);
                f->op.digest = take(ex::operation_digest(f->op));
                auto r = runner(
                    f, journal,
                    [&](ex::ToolContext, Json) -> ex::ToolResult {
                        ++entered;
                        wait_until([&] { return entered == 2; }, "one workspace blocked another");
                        ++finished;
                        return okay();
                    },
                    false, {}, reg);
                check(take(r->execute(f->op)).status == "completed", "workspace result");
                check(take(r->execute(f->op)).status == "completed", "workspace replay");
            } catch (...) {
                valid = false;
            }
        });
    }
    for (auto &worker : workers)
        worker.join();
    check(valid && entered == 2 && finished == 2, "concurrent workspace isolation");
    journal.reset();
    std::filesystem::remove_all(parent);
}
void concurrent_output_and_cancel() {
    {
        auto f = std::make_shared<Fixture>();
        auto w = writer(f, 131072, 8);
        std::atomic<bool> valid{true};
        std::vector<std::thread> producers;
        for (int p = 0; p < 4; ++p)
            producers.emplace_back([&, p] {
                for (int i = 0; i < 64; ++i) {
                    const std::string bytes = std::string(1, static_cast<char>('A' + p)) +
                                              std::string(1, static_cast<char>('!' + i)) +
                                              "\xf0\x9f\x98\x80!!";
                    auto captured = w->capture(p % 2 ? "stderr" : "stdout", bytes);
                    if (!captured || captured.value() != bytes.size())
                        valid = false;
                }
            });
        for (auto &producer : producers)
            producer.join();
        auto status = take(w->finish());
        check(valid && status.state == "complete" && f->blocks.size() == 256,
              "multi-producer complete capture");
        int positions[4] = {0, 0, 0, 0};
        for (const auto &block : f->blocks) {
            const auto decoded = take(crypto::base64_decode(block.at("base64").as_string()));
            check(decoded.size() == 8 && decoded[0] >= 'A' && decoded[0] <= 'D', "producer bytes");
            const auto p = static_cast<std::size_t>(decoded[0] - 'A');
            check(decoded[1] == static_cast<unsigned char>('!' + positions[p]++) &&
                      block.at("channel").as_string() == (p % 2 ? "stderr" : "stdout"),
                  "producer order or channel changed");
        }
        for (auto count : positions)
            check(count == 64, "producer lost output");
        check(status.seal->payload_digest == take(crypto::sha256_hex(f->bytes)),
              "shared stream digest");
    }
    for (bool cancel : {false, true}) {
        auto f = std::make_shared<Fixture>();
        f->block_upload = true;
        auto w = writer(f, 1024, 16);
        take(w->capture("stdout", "first"));
        wait_until([&] { return f->upload_started.load(); }, "no inflight block");
        std::atomic<std::uint64_t> kept{5}, sent{5};
        std::atomic<bool> valid{true};
        std::vector<std::thread> producers;
        for (int p = 0; p < 4; ++p) {
            producers.emplace_back([&, p] {
                const std::string bytes(4096, static_cast<char>('a' + p));
                for (int i = 0; i < 128; ++i) {
                    auto n = w->capture(p % 2 ? "stderr" : "stdout", bytes);
                    if (!n) {
                        valid = false;
                        return;
                    }
                    kept += n.value();
                    sent += bytes.size();
                    auto snapshot = w->snapshot();
                    if (snapshot.pending_bytes > 1024 || snapshot.pending_blocks > 8)
                        valid = false;
                }
            });
        }
        for (auto &producer : producers)
            producer.join();
        auto snapshot = w->snapshot();
        check(valid && snapshot.truncated && snapshot.captured_bytes == kept &&
                  snapshot.dropped_bytes + kept == sent && snapshot.pending_bytes <= 1024,
              "multi-producer bounded accounting");
        if (cancel) {
            w->abort();
            check(bool(w->shutdown()), "cancel join");
            const auto before = w->snapshot();
            for (int i = 0; i < 512; ++i)
                check(take(w->capture("stderr", "drain")) == 0, "cancelled output stopped drain");
            check(w->snapshot().pending_bytes == before.pending_bytes &&
                      w->snapshot().dropped_bytes == before.dropped_bytes + 2560,
                  "drain after cancellation was retained");
            check(!w->finish(), "cancelled output falsely sealed");
        } else {
            f->release_upload = true;
            const auto status = take(w->finish());
            check(status.state == "truncated" && f->bytes.size() == kept &&
                      w->snapshot().pending_bytes == 0 &&
                      status.seal->payload_digest == take(crypto::sha256_hex(f->bytes)),
                  "multi-producer prefix/seal");
        }
    }
}
void output_windows_and_recovery() {
    {
        auto f = std::make_shared<Fixture>();
        f->lose_first = true;
        f->stale_status_once = true;
        auto w = writer(f);
        take(w->capture("stdout", "same-block"));
        check(take(w->finish()).state == "complete" && f->bytes == "same-block" &&
                  f->batch_bodies.size() == 3 && f->batch_bodies[0] == f->batch_bodies[1],
              "lost ACK/status changed block or repeated bytes");
    }
    for (bool gap : {false, true}) {
        auto f = std::make_shared<Fixture>();
        f->gap_after_upload = gap;
        f->forge_seal = !gap;
        auto w = writer(f);
        take(w->capture("stdout", "original"));
        check(!w->finish(), "gap or forged seal confirmed");
        check(f->bytes == "original", "reconcile duplicated original bytes");
    }
    for (bool late : {false, true}) {
        auto f = std::make_shared<Fixture>();
        auto inner = std::make_shared<MemoryJournal>();
        auto j = std::make_shared<HookJournal>(inner);
        if (late)
            j->after_claim = [f] {
                std::lock_guard<std::mutex> lock(f->gate);
                f->output_state_override = "unavailable";
            };
        else
            f->output_state_override = "unavailable";
        int calls = 0;
        auto r = runner(
            f, j,
            [&](ex::ToolContext, Json) -> ex::ToolResult {
                ++calls;
                return okay();
            },
            true);
        auto result = r->execute_with_output(f->op);
        check(calls == 0 && (!result || result.value().receipt.status != "completed"),
              "unavailable/late output window allowed required-output handler");
    }
    {
        auto f = std::make_shared<Fixture>();
        f->output_state_override = "unavailable";
        auto j = std::make_shared<HookJournal>(std::make_shared<MemoryJournal>());
        j->after_claim = [f] {
            std::lock_guard<std::mutex> lock(f->gate);
            f->output_state_override.clear();
        };
        bool got_output = false;
        auto r = runner(
            f, j,
            [&](ex::ToolContext ctx, Json) -> ex::ToolResult {
                got_output = bool(ctx.output);
                return okay();
            },
            true, {}, registration(), connection(), false);
        const auto result = take(r->execute_with_output(f->op));
        check(result.receipt.status == "completed" && !got_output && f->posts == 0 &&
                  f->output_queries == 1,
              "late available window silently expanded original permission");
    }
    {
        auto f = std::make_shared<Fixture>();
        std::atomic<bool> authorized{true};
        f->output_queried = [&](int count) {
            if (count == 2)
                authorized = false;
        };
        int calls = 0;
        auto r = runner(
            f, std::make_shared<MemoryJournal>(),
            [&](ex::ToolContext, Json) -> ex::ToolResult {
                ++calls;
                return okay();
            },
            true,
            [&](const ex::Operation &, CancellationToken) -> Result<void> {
                if (!authorized)
                    return Error{ErrorCode::permission, "revoked while querying output"};
                return {};
            });
        const auto result = take(r->execute_with_output(f->op));
        check(calls == 0 && result.receipt.status == "failed",
              "authorization lost during output query ran handler");
    }
    auto f = std::make_shared<Fixture>();
    auto old = writer(f);
    take(old->capture("stdout", "before-crash"));
    wait_until([&] { return f->posts.load() > 0; }, "cold output first block");
    check(bool(old->shutdown()), "cold output shutdown");
    old.reset();
    auto j = std::make_shared<MemoryJournal>();
    int calls = 0;
    auto r = runner(
        f, j,
        [&](ex::ToolContext, Json) -> ex::ToolResult {
            ++calls;
            return okay();
        },
        true);
    auto result = take(r->execute_with_output(f->op));
    check(calls == 0 && result.receipt.status == "unknown" && !result.output_confirmed(),
          "cold receiving output restarted business");
    f->output_state_override = "gap";
    auto cold = runner(
        f, std::make_shared<MemoryJournal>(),
        [&](ex::ToolContext, Json) -> ex::ToolResult {
            ++calls;
            return okay();
        },
        true);
    check(!cold->execute_with_output(f->op) && calls == 0, "cold output gap reexecuted business");
}
void output_result_separation() {
    auto f = std::make_shared<Fixture>();
    f->output_failure = true;
    auto j = std::make_shared<MemoryJournal>();
    int calls = 0;
    auto r = runner(
        f, j,
        [&](ex::ToolContext context, Json) -> ex::ToolResult {
            ++calls;
            check(bool(context.output), "ordinary invoke output missing");
            take(context.output->capture("stdout", "business output"));
            return okay();
        },
        true);
    auto outcome = take(r->execute_with_output(f->op));
    check(outcome.receipt.status == "completed" && !outcome.output_confirmed(),
          "lost seal erased business fact");
    check(j->result && j->result->status == "completed", "business fact not durable");
    auto again = take(r->execute_with_output(f->op));
    check(again.receipt.status == "completed" && calls == 1, "output failure reexecuted business");
}
void early_output_and_renewal() {
    auto f = std::make_shared<Fixture>();
    f->stall_status = true;
    auto j = std::make_shared<MemoryJournal>();
    CancellationSource stop;
    std::atomic<bool> first_before_return{false};
    auto r = runner(
        f, j,
        [&](ex::ToolContext context, Json) -> ex::ToolResult {
            take(context.output->capture("stdout", "early"));
            const auto limit = unix_time_ms() + 2000;
            while (f->posts.load() == 0 && unix_time_ms() < limit)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            first_before_return = f->posts > 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(1240));
            return okay();
        },
        true);
    std::thread cancel([&] {
        const auto limit = unix_time_ms() + 5000;
        while (j->completes.load() == 0 && unix_time_ms() < limit)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        stop.cancel();
    });
    auto result = r->run(stop.token());
    cancel.join();
    check(!result && first_before_return && f->heartbeats >= 1, "no early output or renewal");
}
// direct execute 不拥有外部 heartbeat 的更新；串行消费者须把新 lease 交给新 Runner。
void direct_execute_renewed_connection() {
    for (const bool use_renewed : {false, true}) {
        auto f = std::make_shared<Fixture>();
        auto j = std::make_shared<MemoryJournal>();
        auto client = take(ex::Client::create(api(f), scope()));
        auto original = connection();
        const auto expiry_ms = unix_time_ms() + 600;
        const auto seconds = static_cast<std::time_t>(expiry_ms / 1000);
        std::tm calendar{};
#ifdef _WIN32
        check(gmtime_s(&calendar, &seconds) == 0, "UTC fixture conversion");
#else
        check(gmtime_r(&seconds, &calendar) != nullptr, "UTC fixture conversion");
#endif
        std::ostringstream timestamp;
        timestamp << std::put_time(&calendar, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
                  << std::setw(3) << expiry_ms % 1000 << 'Z';
        original.expires_at = timestamp.str();
        const auto renewed = take(client->heartbeat(original));
        check(renewed.executor_id == original.executor_id &&
                  renewed.connection_id == original.connection_id &&
                  renewed.connection_revision == original.connection_revision &&
                  renewed.expires_at != original.expires_at,
              "heartbeat must renew the exact original connection");
        std::atomic<int> effects{0};
        auto handler = [&](ex::ToolContext context, Json) -> ex::ToolResult {
            ++effects; // 模拟 publication 已写入、返回前跨过原 lease。
            if (context.cancellation.wait_for(std::chrono::milliseconds(850)))
                return ex::ToolFailure::unknown("original lease expired after durable effect");
            return okay();
        };
        auto active =
            runner(f, j, handler, false, {}, registration(), use_renewed ? renewed : original);
        const auto digest = f->op.digest;
        const auto receipt = take(active->execute(f->op));
        check(receipt.status == (use_renewed ? "completed" : "unknown") && effects == 1 &&
                  receipt.digest == digest && receipt.operation_id == f->op.operation_id &&
                  j->result && j->result->status == receipt.status,
              "direct execute did not preserve the lease-bound durable fact");
        active.reset();
        active = runner(f, j, handler, false, {}, registration(), renewed);
        const auto replay = take(active->execute(f->op));
        check(ex::to_json(replay).dump() == ex::to_json(receipt).dump() && effects == 1,
              "renewal changed original permanent receipt or repeated the effect");
        check(f->heartbeats == 1 && f->op.digest == digest,
              "direct execution silently renewed or replaced the original operation");
    }
}
int journal_process(const std::string &phase, const std::filesystem::path &parent) {
    check(parent.is_absolute(), "process workspace must be absolute");
    check(bool(storage::create_private_directory(parent / "effects")), "effect directory");
    auto effects = take(
        storage::PrivateDirectory::open(parent / "effects", []() -> Result<void> { return {}; }));
    auto original =
        take(ex::FileJournal::open(parent / "journal", []() -> Result<void> { return {}; }));
    auto journal = std::make_shared<HookJournal>(original);
    auto f = std::make_shared<Fixture>();
    const auto remote = take(effects->read("remote", 16384));
    if (remote) {
        auto value = parse(*remote);
        ex::Receipt r{value.at("executorId").as_string(),
                      value.at("connectionId").as_string(),
                      value.at("operationId").as_string(),
                      value.at("digest").as_string(),
                      value.at("status").as_string(),
                      {},
                      {}};
        if (!value.at("result").is_null())
            r.result = ex::Resource{value.at("result").at("operation").as_string(),
                                    value.at("result").at("args")};
        if (!value.at("errorCode").is_null())
            r.error_code = value.at("errorCode").as_string();
        f->receipt = std::move(r);
    }
    const auto crash = [] {
        std::cout << "durable crash boundary reached\n" << std::flush;
        std::_Exit(86);
    };
    if (phase == "before-claim")
        journal->before_claim = crash;
    if (phase == "after-claim")
        journal->after_claim = crash;
    if (phase == "after-receipt")
        journal->after_complete = crash;
    journal->lose_claim = phase == "lost-claim";
    journal->lose_complete = phase == "lost-receipt";
    int calls = 0;
    auto r = runner(f, journal, [&](ex::ToolContext, Json) -> ex::ToolResult {
        ++calls;
        check(take(original->claim(f->op)).state == ex::ClaimState::pending,
              "handler ran without durable claim");
        const auto previous = take(effects->read("count", 32));
        const auto count = previous ? std::stoi(*previous) : 0;
        check(bool(effects->write_atomic("count", std::to_string(count + 1))),
              "effect persistence");
        if (phase == "after-handler")
            crash();
        return okay();
    });
    auto result = r->execute(f->op);
    if (phase == "lost-claim" || phase == "lost-receipt") {
        check(!result, "lost commit falsely reported confirmed");
        std::cout << Json::object({{"phase", phase}, {"calls", calls}, {"uncertain", true}}).dump()
                  << '\n';
        return 42;
    }
    auto fact = take(std::move(result));
    f->receipt_accepted = [&] {
        auto local = take(original->claim(f->op));
        check(local.state == ex::ClaimState::receipt &&
                  ex::to_json(*local.receipt).dump() == ex::to_json(fact).dump(),
              "submission preceded durable receipt");
        check(bool(effects->write_atomic("remote", ex::to_json(*f->receipt).dump())),
              "remote receipt persistence");
        if (phase == "after-submit")
            crash();
    };
    f->lose_receipt = phase == "lost-submit";
    auto client = take(ex::Client::create(api(f), scope()));
    auto submitted = client->submit(f->op, fact);
    if (phase == "lost-submit")
        check(!submitted, "lost submit falsely reported confirmed");
    else
        check(bool(submitted), "receipt resubmit failed");
    const auto count = take(effects->read("count", 32));
    std::cout << Json::object({{"phase", phase},
                               {"calls", calls},
                               {"sideEffects", count ? std::stoi(*count) : 0},
                               {"receipt", ex::to_json(fact)}})
                     .dump()
              << '\n';
    return phase == "lost-submit" ? 42 : 0;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--journal-process")
            return journal_process(argv[2], std::filesystem::path(argv[3]));
        const std::vector<std::pair<const char *, void (*)()>> groups{
            {"declaration_and_arguments", declaration_and_arguments},
            {"journal_facts", journal_facts},
            {"deterministic_and_unknown", deterministic_and_unknown},
            {"authorization", authorization},
            {"identity_matrix", identity_matrix},
            {"active_revocation_and_workspaces", active_revocation_and_workspaces},
            {"output_transport", output_transport},
            {"output_result_separation", output_result_separation},
            {"concurrent_output_and_cancel", concurrent_output_and_cancel},
            {"output_windows_and_recovery", output_windows_and_recovery},
            {"early_output_and_renewal", early_output_and_renewal},
            {"direct_execute_renewed_connection", direct_execute_renewed_connection}};
        std::size_t executed = 0;
        for (const auto &[name, execute] : groups) {
            if (argc == 3 && std::string(argv[1]) == "--group" && std::string(argv[2]) != name)
                continue;
            std::cout << name << '\n' << std::flush;
            execute();
            ++executed;
        }
        check(executed != 0, "unknown test group");
        std::cout << "executor: " << executed
                  << " focused groups passed; no shell or host fallback\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "executor: " << e.what() << '\n';
        return 1;
    }
}
