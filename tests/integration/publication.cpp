#include "../../demo/memory_binding.hpp"
#include "tansr/crypto.hpp"
#include "tansr/memory_publication.hpp"
#include "tansr/session.hpp"
#include "tansr/terminal_persistence.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace tansr;
namespace ex = tansr::executor;
namespace mp = tansr::memory_publication;
namespace tp = tansr::terminal_persistence;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> value) {
    if (!value)
        throw std::runtime_error(value.error().message + " [" + value.error().wire_code + "]");
    return std::move(value).value();
}
void take(Result<void> value) {
    if (!value)
        throw std::runtime_error(value.error().message + " [" + value.error().wire_code + "]");
}
std::string bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "missing fixture file");
    return {std::istreambuf_iterator<char>(input), {}};
}
Json read(const std::filesystem::path &path) { return take(Json::parse(bytes(path))); }
void save(const std::filesystem::path &path, const Json &value) {
    auto directory = take(
        storage::PrivateDirectory::open(path.parent_path(), []() -> Result<void> { return {}; }));
    take(directory->write_atomic(path.filename().u8string(), value.dump()));
}
void event(const char *name, Json detail = Json::object()) {
    detail.set("event", name);
    std::cout << "TANSR_CPP_PUBLICATION " << detail.dump() << std::endl;
}
ex::Binding binding_from(const Json &value) {
    const auto &target = value.at("target");
    check(!target.find("interpreter"), "fixture does not install Shell");
    return {value.at("bindingId").as_string(),
            value.at("revision").as_string(),
            {target.at("executorId").as_string(),
             target.at("connectionId").as_string(),
             target.at("connectionRevision").as_string(),
             target.at("workspaceId").as_string(),
             target.at("workspaceRevision").as_string(),
             {}}};
}
ex::Connection connection_from(const Json &value) {
    return {value.at("executorId").as_string(), value.at("connectionId").as_string(),
            value.at("connectionRevision").as_string(), value.at("expiresAt").as_string(),
            value.at("heartbeatAfterMs").as_u64()};
}
Json connection_json(const ex::Connection &value) {
    return Json::object({{"executorId", value.executor_id},
                         {"connectionId", value.connection_id},
                         {"connectionRevision", value.connection_revision},
                         {"expiresAt", value.expires_at},
                         {"heartbeatAfterMs", Json(value.heartbeat_after_ms)}});
}
class LossTransport final : public HttpTransport {
  public:
    std::shared_ptr<Runtime> runtime;
    bool drop = false;
    unsigned losses = 0;
    explicit LossTransport(std::shared_ptr<Runtime> value) : runtime(std::move(value)) {}
    Result<HttpResponse> request(const HttpRequest &request, CancellationToken cancel) override {
        auto response = runtime->request(request, cancel);
        if (drop && response && response.value().status >= 200 && response.value().status < 300) {
            drop = false;
            ++losses;
            check(request.method == "POST", "receipt loss must follow a real POST");
            auto body = take(Json::parse(response.value().body));
            check(body.find("receipt") && !body.at("receipt").is_null(),
                  "Serve did not accept receipt before loss");
            event("accepted-response-lost");
            return Error{ErrorCode::network,
                         "injected response loss after actual Serve acceptance"};
        }
        return response;
    }
    Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &request,
                                               CancellationToken cancel) override {
        return runtime->stream(request, cancel);
    }
};
class CountStore final : public mp::Store {
  public:
    std::shared_ptr<mp::FileStore> inner;
    std::size_t calls = 0;
    explicit CountStore(std::shared_ptr<mp::FileStore> value) : inner(std::move(value)) {}
    const Json &identity() const noexcept override { return inner->identity(); }
    bool atomic_durable_publication() const noexcept override {
        return inner->atomic_durable_publication();
    }
    bool encrypted_at_rest() const noexcept override { return true; }
    Result<Json> execute(const Json &request, const Json &owner) override {
        ++calls;
        auto result = inner->execute(request, owner);
        if (!result)
            event("storage-error", Json::object({{"action", request.at("action")},
                                                 {"code", static_cast<int>(result.error().code)},
                                                 {"message", result.error().message}}));
        return result;
    }
};
struct Context {
    Json info;
    std::filesystem::path root;
    std::string family;
    ex::Scope scope;
    std::shared_ptr<Runtime> runtime;
    std::shared_ptr<LossTransport> transport;
    std::shared_ptr<ApiClient> api;
    std::shared_ptr<ex::Client> execution;
    Context(Json value, std::filesystem::path directory, std::string contract)
        : info(std::move(value)), root(std::move(directory)), family(std::move(contract)),
          scope{info.at("applicationScopeId").as_string(), info.at("endUserId").as_string(),
                info.at("authorizationRevision").as_string()},
          runtime(take(Runtime::create())), transport(std::make_shared<LossTransport>(runtime)) {
        api = client(info.at("token").as_string());
        execution = take(ex::Client::create(api, scope));
    }
    std::shared_ptr<ApiClient> client(const std::string &token) {
        ClientOptions options;
        options.base_url = info.at("baseURL").as_string();
        options.family = family;
        options.default_timeout = std::chrono::seconds(100);
        options.token_provider = [token](CancellationToken) -> Result<AuthToken> {
            return AuthToken{token, "synthetic-" + token};
        };
        return take(ApiClient::create(options, transport));
    }
    bool persistence() const {
        const auto *profile = info.find("profile");
        return profile && profile->as_string() == "terminal-persistence-v1";
    }
    ex::Registration registration() {
        ex::Registration result;
        result.executor_id = info.at("executorId").as_string();
        result.workspaces = {{"cpp-memory", "1"}};
        result.operations = {"tool.invoke"};
        result.tools = {{persistence() ? tp::tool_name : mp::tool_name,
                         persistence() ? tp::tool_digest : mp::tool_digest}};
        return result;
    }
    mp::Options options(bool create) {
        mp::Options result;
        result.path = info.find("originalStoragePath")
                          ? std::filesystem::u8path(info.at("originalStoragePath").as_string())
                          : root / "media" / "publication.bin";
        result.create = create;
        result.identity = info.at("publicationIdentity");
        result.key_id = "cpp-real-publication-1";
        result.read_key = []() -> Result<crypto::Aes256Key> {
            crypto::Aes256Key key{};
            key.fill(31);
            return key;
        };
        result.read_context = [this]() -> Result<ex::Scope> { return scope; };
        return result;
    }
    Json state() { return read(root / "handoff" / "state.json"); }
    Json memory(const std::string &id, std::shared_ptr<ApiClient> selected = {}) {
        CallOptions options;
        options.parameters = {{"id", id}};
        options.query = {{"contract", "terminal-services-v1"}, {"sessionContract", family}};
        auto response = take((selected ? selected : api)->call("terminal.memory.read", options));
        take(validate_wire("terminal-services-v1", "MemoryStateResponse", response.body));
        return response.body;
    }
    void close() {
        check(take(runtime->shutdown(std::chrono::seconds(5))) == ShutdownStatus::stopped,
              "Runtime did not drain");
        event("runtime-stopped");
    }
};
void setup(Context &context, bool bind) {
    for (const char *leaf : {"handoff", "media", "credentials"})
        take(storage::create_private_directory(context.root / leaf));
    const auto sessions = take(session::SessionClient::create(context.api));
    session::CreateOptions create;
    create.tools = std::vector<std::string>{"SearchMemory"};
    if (context.family == "sdk2-offload-v1") {
        create.request_id = "cpp-publication-create";
        create.write.deadline_ms = unix_time_ms() + 100000;
        const auto intent = context.root / "handoff" / "create-intent.json";
        check(!std::filesystem::exists(intent), "retain and reconcile the original create intent");
        save(intent,
             Json::object({{"family", context.family},
                           {"baseURL", context.info.at("baseURL")},
                           {"deadlineMs", Json(*create.write.deadline_ms)},
                           {"body", Json::object({{"requestId", *create.request_id},
                                                  {"tools", Json::array({"SearchMemory"})}})}}));
    }
    const auto current = take(sessions.create(create));
    Json state = Json::object({{"sessionId", current.id()}});
    if (bind) {
        const auto registration = context.registration();
        const auto connection = take(context.execution->register_executor(registration));
        const auto initialized =
            take(context.execution->initialize(current.id(), registration.platform, std::nullopt,
                                               take(current.capabilities()).closure_id));
        const auto bound = take(context.execution->bind(
            current.id(), connection, registration.workspaces.front(),
            initialized.capability_revision, take(current.capabilities()).closure_id));
        for (const auto &tool : bound.effective_tools)
            event("binding-capability",
                  Json::object({{"name", tool.name},
                                {"available", tool.available},
                                {"reason", tool.unavailable_reason ? Json(*tool.unavailable_reason)
                                                                   : Json()}}));
        check(bool(bound.binding), "MemoryPublication binding missing");
        take(demo::memory::negotiate(context.api, context.scope, {context.family, current.id()},
                                     *bound.binding, "cpp-publication-binding"));
        state.set("connection", connection_json(connection));
        state.set("binding", demo::memory::binding_json(*bound.binding));
    }
    save(context.root / "handoff" / "state.json", state);
    auto credentials = take(storage::PrivateDirectory::open(context.root / "credentials",
                                                            []() -> Result<void> { return {}; }));
    take(credentials->write_atomic("token", context.info.at("token").as_string()));
    take(credentials->write_atomic("scope", context.info.at("scope").dump()));
    take(credentials->write_atomic("key", std::string(64, '0').replace(0, 64, [] {
        std::string key;
        for (int i = 0; i < 32; ++i)
            key += "1f";
        return key;
    }())));
    event("setup", Json::object({{"sessionId", current.id()}}));
}
void lifecycle(Context &context, bool resume) {
    auto state = context.state();
    const auto session_id = state.at("sessionId").as_string();
    const auto sessions = take(session::SessionClient::create(context.api));
    if (!resume) {
        if (context.persistence()) {
            const auto caps = take(context.execution->execution_capabilities(session_id));
            check(bool(caps.binding), "original live binding missing");
            state.set("binding", demo::memory::binding_json(*caps.binding));
            save(context.root / "handoff" / "state.json", state);
        }
        const auto current = take(sessions.attach(session_id));
        take(current.close());
        const auto until = unix_time_ms() + 30000;
        while (take(current.meta()).status != "ended" && unix_time_ms() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        check(take(current.meta()).status == "ended", "original session did not close");
        event("session-closed", Json::object({{"sessionId", session_id}}));
        return;
    }
    const auto original_binding = state.at("binding");
    auto connection = take(context.execution->heartbeat(connection_from(state.at("connection"))));
    const auto current = take(sessions.resume(session_id));
    check(current.id() == session_id && current.created().resumed,
          "cold source must resume the original session");
    const auto registration = context.registration();
    const auto initialized = take(context.execution->initialize(
        session_id, registration.platform, std::nullopt, take(current.capabilities()).closure_id));
    const auto bound = take(context.execution->bind(
        session_id, connection, registration.workspaces.front(), initialized.capability_revision,
        take(current.capabilities()).closure_id));
    check(bool(bound.binding), "resumed session execution binding missing");
    check(bound.binding->target.connection_id == connection.connection_id &&
              bound.binding->target.connection_revision == connection.connection_revision,
          "cold binding replaced the original connection");
    take(demo::memory::negotiate(context.api, context.scope, {context.family, session_id},
                                 *bound.binding, "cpp-publication-cold-binding"));
    state.set("connection", connection_json(connection));
    state.set("binding", demo::memory::binding_json(*bound.binding));
    save(context.root / "handoff" / "state.json", state);
    event("session-resumed", Json::object({{"sessionId", session_id},
                                           {"connection", connection_json(connection)},
                                           {"originalBinding", original_binding},
                                           {"currentBinding", state.at("binding")}}));
}
void trigger(Context &context, const std::string &mode, const std::string &label) {
    const auto session = context.state().at("sessionId").as_string();
    auto state = context.memory(session);
    if (mode == "read") {
        event("memory-read", Json::object({{"available", state.at("memory").at("available")}}));
        return;
    }
    const int count = 1;
    for (int i = 0; i < count; ++i) {
        const std::string key = label + "-" + std::to_string(i);
        const auto entropy = take(crypto::random_bytes(470));
        std::string synthetic = "合成" + key + ":";
        for (std::size_t at = 0; at < entropy.size(); at += 2) {
            const auto codepoint = 0x4e00U + ((static_cast<unsigned>(entropy[at]) << 8U |
                                               static_cast<unsigned>(entropy[at + 1])) %
                                              0x5000U);
            synthetic += static_cast<char>(0xe0U | (codepoint >> 12U));
            synthetic += static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU));
            synthetic += static_cast<char>(0x80U | (codepoint & 0x3fU));
        }
        const auto &identity = context.info.at("publicationIdentity");
        auto body = Json::object(
            {{"contract", "terminal-services-v1"},
             {"session",
              Json::object({{"sessionContract", context.family}, {"sessionId", session}})},
             {"requestId", "request-" + key},
             {"operationId", "operation-" + key},
             {"sourceId", identity.at("sourceId")},
             {"sourceGeneration", identity.at("sourceGeneration")},
             {"expectedRevision", state.at("memory").at("revision")},
             {"command", Json::object({{"kind", "pin"}, {"text", synthetic}})}});
        take(validate_wire("terminal-services-v1", "MemoryCommandRequest", body));
        CallOptions options;
        options.parameters = {{"id", session}};
        options.body = body;
        event("command-start", Json::object({{"expectedRevision", body.at("expectedRevision")},
                                             {"operationId", body.at("operationId")}}));
        auto called = context.api->call("terminal.memory.command", options);
        const auto busy_deadline = unix_time_ms() + 20000;
        std::uint64_t backoffs = 0;
        while (!called && called.error().http_status == 409 && unix_time_ms() < busy_deadline) {
            const auto detail = Json::parse(called.error().detail);
            if (!detail || !detail.value().find("domainCode") ||
                detail.value().at("domainCode").as_string() != "busy" ||
                !detail.value().find("domainRetryAction") ||
                detail.value().at("domainRetryAction").as_string() != "backoff")
                break;
            CallOptions lookup;
            lookup.parameters = {{"id", session}, {"targetId", "operation-" + key}};
            lookup.query = {{"contract", "terminal-services-v1"},
                            {"sessionContract", context.family},
                            {"requestId", "request-" + key}};
            const auto observed = take(context.api->call("terminal.memory.receipt", lookup));
            take(validate_wire("terminal-services-v1", "MemoryReceiptResponse", observed.body));
            check(
                observed.body.at("receipt").is_null(),
                "busy command already has a receipt; preserve original intent for reconciliation");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ++backoffs;
            called = context.api->call("terminal.memory.command", options);
        }
        if (backoffs)
            event("same-key-busy-backoff", Json::object({{"operationId", body.at("operationId")},
                                                         {"attempts", Json(backoffs)}}));
        if (!called)
            event("synthetic-command-error",
                  Json::object({{"httpStatus", called.error().http_status},
                                {"wireCode", called.error().wire_code},
                                {"detail", called.error().detail}}));
        auto response = take(std::move(called));
        take(validate_wire("terminal-services-v1", "MemoryReceiptResponse", response.body));
        const auto deadline = unix_time_ms() + 95000;
        while (response.body.at("receipt").at("status").as_string() == "pending" &&
               unix_time_ms() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            CallOptions lookup;
            lookup.parameters = {{"id", session}, {"targetId", "operation-" + key}};
            lookup.query = {{"contract", "terminal-services-v1"},
                            {"sessionContract", context.family},
                            {"requestId", "request-" + key}};
            response = take(context.api->call("terminal.memory.receipt", lookup));
            take(validate_wire("terminal-services-v1", "MemoryReceiptResponse", response.body));
        }
        const auto &receipt = response.body.at("receipt");
        const auto status = receipt.at("status").as_string();
        event("memory-command", Json::object({{"status", status},
                                              {"operationId", receipt.at("operationId")},
                                              {"durable", receipt.at("durable")}}));
        if (mode == "unknown") {
            check(status == "unknown" || status == "failed",
                  "claim-only must not report committed");
            return;
        }
        check(status == "committed" && receipt.at("durable").as_bool(),
              "memory pin not durably committed");
        if (context.persistence())
            save(context.root / "handoff" / "pin.json",
                 Json::object({{"command", body}, {"receipt", receipt}}));
        state = context.memory(session);
    }
}
Json request_of(const ex::Operation &op) {
    return take(ex::parse_tool_arguments(op.request.args.at("argsJson").as_string()));
}
void worker(Context &context, const std::string &mode) {
    auto state = context.state();
    const auto session = state.at("sessionId").as_string();
    auto connection = connection_from(state.at("connection"));
    const auto binding = binding_from(state.at("binding"));
    auto storage_options = context.options(mode == "warm");
    auto store = take(mp::FileStore::open(storage_options));
    auto counted = std::make_shared<CountStore>(store);
    ex::Authorizer authorize = [&](const ex::Operation &op,
                                   CancellationToken cancel) -> Result<void> {
        if (op.scope.application_scope_id != context.scope.application_scope_id ||
            op.scope.end_user_id != context.scope.end_user_id ||
            op.scope.authorization_revision != context.scope.authorization_revision ||
            op.session_id != session || op.tool_name != "MemoryPublication" ||
            !demo::memory::same_binding(op.binding, binding))
            return Error{ErrorCode::permission, "publication fixture current identity mismatch"};
        auto capabilities = context.execution->execution_capabilities(session, cancel);
        if (!capabilities)
            return capabilities.error();
        if (!capabilities.value().binding ||
            !demo::memory::same_binding(*capabilities.value().binding, binding))
            return Error{ErrorCode::permission, "publication fixture current permission revoked"};
        return {};
    };
    auto host = take(mp::create_host({counted, store, authorize, true}));
    ex::RunnerOptions options;
    options.client = context.execution;
    options.registration = context.registration();
    options.journal = host.journal;
    options.tools = host.tools;
    options.authorize = authorize;
    connection = take(context.execution->heartbeat(connection));
    state.set("connection", connection_json(connection));
    save(context.root / "handoff" / "state.json", state);
    auto runner = take(ex::Runner::create(options, connection));
    const auto submit = [&](const ex::Operation &op, const ex::Receipt &receipt, bool lose) {
        context.transport->drop = lose;
        auto result = context.execution->submit(op, receipt);
        if (lose) {
            check(!result && result.error().code == ErrorCode::network &&
                      context.transport->losses == 1,
                  "actual receipt loss injection absent");
            auto remote = take(context.execution->status(session, op.operation_id));
            check(remote.receipt &&
                      demo::memory::equal(ex::to_json(*remote.receipt), ex::to_json(receipt)),
                  "lost-response status differs");
            auto calls = counted->calls;
            auto replay = take(runner->execute(op));
            check(counted->calls == calls &&
                      demo::memory::equal(ex::to_json(replay), ex::to_json(receipt)),
                  "lost-response replay invoked storage");
            result = context.execution->submit(op, replay);
        }
        check(bool(result) && result.value().receipt &&
                  demo::memory::equal(ex::to_json(*result.value().receipt), ex::to_json(receipt)),
              "Serve receipt reconciliation differs");
    };
    if (mode == "resume-chunk" || mode == "resume-unknown") {
        const auto checkpoint = read(context.root / "handoff" / "checkpoint.json");
        const auto original =
            take(context.execution->status(session, checkpoint.at("operationId").as_string()));
        check(original.operation.digest == checkpoint.at("digest").as_string(),
              "original operation changed across process");
        check(original.status == "pending", "checkpoint operation must remain pending on Serve");
        const auto receipt = take(runner->execute(original.operation));
        check(counted->calls == 0, "restart replay re-entered Store");
        check(receipt.status == (mode == "resume-unknown" ? "unknown" : "completed"),
              "restart receipt status differs");
        submit(original.operation, receipt, mode == "resume-chunk");
        const auto second = take(runner->execute(original.operation));
        check(counted->calls == 0 && demo::memory::equal(ex::to_json(second), ex::to_json(receipt)),
              "permanent receipt changed");
        submit(original.operation, second, false);
        event("original-key-reconciled",
              Json::object({{"operationId", original.operation.operation_id},
                            {"digest", original.operation.digest},
                            {"status", receipt.status},
                            {"storeCalls", Json(0)}}));
    }
    event("worker-ready");
    const auto until = unix_time_ms() + 360000;
    auto heartbeat = unix_time_ms();
    bool checkpointed = false;
    std::string read_etag, read_bytes;
    while (unix_time_ms() < until && !std::filesystem::exists(context.root / "stop")) {
        if (unix_time_ms() >= heartbeat) {
            const auto renewed = take(context.execution->heartbeat(connection));
            check(renewed.connection_revision == connection.connection_revision,
                  "heartbeat changed original generation");
            connection = renewed;
            state.set("connection", connection_json(connection));
            save(context.root / "handoff" / "state.json", state);
            // 直接 execute 工装没有 run() 的内置心跳线程；串行静止点更新同一连接的期限。
            runner = take(ex::Runner::create(options, connection));
            heartbeat = unix_time_ms() + static_cast<std::int64_t>(std::min<std::uint64_t>(
                                             connection.heartbeat_after_ms, 1000));
        }
        const auto batch = take(context.execution->poll(connection));
        for (const auto &op : batch.operations) {
            const auto request = request_of(op);
            const auto action = request.at("action").as_string();
            if (mode == "claim" && action == "begin") {
                const auto claimed = take(store->claim(op));
                check(claimed.state == ex::ClaimState::claimed, "claim fixture was not fresh");
                save(context.root / "handoff" / "checkpoint.json",
                     Json::object({{"operationId", op.operation_id},
                                   {"digest", op.digest},
                                   {"action", action}}));
                checkpointed = true;
                event("claim-durable",
                      Json::object({{"operationId", op.operation_id}, {"digest", op.digest}}));
                break;
            }
            const auto receipt = take(runner->execute(op));
            if (receipt.status != "completed")
                event("unexpected-receipt",
                      Json::object(
                          {{"action", action},
                           {"status", receipt.status},
                           {"errorCode", receipt.error_code ? Json(*receipt.error_code) : Json()},
                           {"expiresAt", op.expires_at}}));
            check(receipt.status == "completed", "real publication operation did not complete");
            auto record = Json::object(
                {{"action", action}, {"operationId", op.operation_id}, {"digest", op.digest}});
            if (request.find("offset"))
                record.set("offset", request.at("offset"));
            if (request.find("byteLength"))
                record.set("byteLength", request.at("byteLength"));
            for (const auto *field : {"transferId", "sha256", "payloadDigest", "etag"})
                if (request.find(field))
                    record.set(field, request.at(field));
            check(bool(receipt.result), "publication receipt result missing");
            const auto tool_result =
                take(Json::parse(receipt.result->args.at("resultJson").as_string()));
            check(tool_result.at("status").as_string() == "ok", "publication tool reported error");
            const auto response = take(
                Json::parse(tool_result.at("content").as_array().at(0).at("text").as_string()));
            take(validate_wire("terminal-services-v1", "MemoryPublicationResponse", response));
            if (action == "head")
                record.set("publication", response.at("publication"));
            if (response.find("transfer")) {
                const auto &transfer = response.at("transfer");
                record.set("transferStatus", transfer.at("status"));
                record.set("receivedBytes", transfer.at("receivedBytes"));
                record.set("etag", transfer.at("etag"));
            }
            if (action == "read") {
                const auto body = take(crypto::base64_decode(response.at("base64").as_string()));
                check(body.size() == response.at("byteLength").as_u64() &&
                          take(crypto::sha256_hex(std::string_view(
                              reinterpret_cast<const char *>(body.data()), body.size()))) ==
                              response.at("payloadDigest").as_string(),
                      "cold read bytes or digest differ");
                if (response.at("offset").as_u64() == 0) {
                    read_etag = response.at("etag").as_string();
                    read_bytes.clear();
                }
                check(read_etag == response.at("etag").as_string() &&
                          read_bytes.size() == response.at("offset").as_u64() &&
                          read_bytes.size() + body.size() <= 1048576,
                      "cold publication read changed etag, offset or fixture bound");
                read_bytes.append(reinterpret_cast<const char *>(body.data()), body.size());
                if (response.at("complete").as_bool()) {
                    check(take(crypto::sha256_hex(read_bytes)) == read_etag,
                          "cold publication full digest differs from original etag");
                    record.set("verifiedPublicationSha256", read_etag);
                }
                for (const auto *field : {"byteLength", "nextOffset", "complete", "payloadDigest"})
                    record.set(field, response.at(field));
            }
            if (mode == "chunk" && action == "chunk") {
                check(request.at("byteLength").as_u64() > 0,
                      "checkpoint chunk must contain actual publication bytes");
                save(context.root / "handoff" / "checkpoint.json", record);
                checkpointed = true;
                event("chunk-receipt-durable", record);
                break;
            }
            submit(op, receipt, false);
            event("receipt", record);
        }
        if (checkpointed)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    check(checkpointed || std::filesystem::exists(context.root / "stop"),
          "worker bounded deadline reached");
    runner.reset();
    host.tools.clear();
    host.journal.reset();
    counted.reset();
    take(store->close());
    store.reset();
    storage_options.create = false;
    auto reopened = take(mp::FileStore::open(storage_options));
    take(reopened->close());
    event("storage-closed-and-reopened");
}
void inspect(Context &context, bool demo) {
    auto state = context.state();
    const auto session = state.at("sessionId").as_string();
    if (demo) {
        const auto capabilities = take(context.execution->execution_capabilities(session));
        check(bool(capabilities.binding), "Demo execution binding missing");
        state.set("binding", demo::memory::binding_json(*capabilities.binding));
    }
    auto options = context.options(false);
    auto store = take(mp::FileStore::open(options));
    auto request = context.info.at("publicationIdentity");
    request.as_object().erase(
        std::remove_if(request.as_object().begin(), request.as_object().end(),
                       [](const auto &item) { return item.first == "scope"; }),
        request.as_object().end());
    request.set("contract", "terminal-services-v1");
    request.set("action", "head");
    const auto owner = Json::object({{"scope", context.info.at("scope")},
                                     {"sessionId", session},
                                     {"binding", state.at("binding")}});
    const auto head = take(store->execute(request, owner));
    check(!head.at("publication").is_null(), "publication disappeared across reopen");
    const auto fingerprint = take(crypto::sha256_hex(bytes(options.path)));
    context.scope.end_user_id = "wrong-user";
    check(!store->execute(request, owner), "wrong current owner accessed publication");
    check(take(crypto::sha256_hex(bytes(options.path))) == fingerprint,
          "wrong-owner denial changed original ciphertext");
    context.scope.end_user_id = context.info.at("endUserId").as_string();
    take(store->close());
    check(bytes(options.path)
                  .find(context.info.at("publicationIdentity").at("domainKey").as_string()) ==
              std::string::npos,
          "plaintext identity on media");
    CallOptions denied;
    denied.parameters = {{"id", session}};
    denied.query = {{"contract", "terminal-services-v1"}, {"sessionContract", context.family}};
    const auto foreign = context.client(context.info.at("otherToken").as_string())
                             ->call("terminal.memory.read", denied);
    check(!foreign && (foreign.error().http_status == 401 || foreign.error().http_status == 403 ||
                       foreign.error().http_status == 404),
          "Serve did not reject wrong subject");
    event("inspection", Json::object({{"publication", head.at("publication")},
                                      {"ciphertextSha256", fingerprint},
                                      {"foreignHttpStatus", Json(foreign.error().http_status)}}));
}
#include "persistence.inc"
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc >= 5,
              "usage: serve_publication info.json family private-os-temp-root "
              "setup|seed|read|seed-pin|pin|unknown|warm|chunk|claim|resume-chunk|"
              "resume-unknown|close-session|resume-session|inspect|inspect-demo [label]");
        const std::filesystem::path root = std::filesystem::u8path(argv[3]);
        check(root.is_absolute(), "fixture root must be absolute");
        Context context(read(std::filesystem::u8path(argv[1])), root, argv[2]);
        const std::string mode = argv[4];
        if (mode == "setup" || mode == "seed")
            setup(context, mode == "setup");
        else if (mode == "read" || mode == "seed-pin" || mode == "pin" || mode == "unknown")
            trigger(context, mode, argc > 5 ? argv[5] : "seed");
        else if (mode == "close-session" || mode == "resume-session")
            lifecycle(context, mode == "resume-session");
        else if (mode == "inspect" || mode == "inspect-demo")
            inspect(context, mode == "inspect-demo");
        else if (mode == "persistence-keys")
            persistence_keys(context);
        else if (mode == "persistence-inspect")
            persistence_inspect(context);
        else if (mode == "persistence-warm" || mode == "persistence-cold")
            persistence_worker(context, mode == "persistence-warm");
        else
            worker(context, mode);
        context.close();
    } catch (const std::exception &error) {
        std::cerr << "publication integration: " << error.what() << "\n";
        return 1;
    }
}
