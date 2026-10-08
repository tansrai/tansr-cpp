// Demo 进程验收的合成宿主：只用公开 SDK 准备凭据/事实及精确 ACK 失回边界。
#include "tansr/archive.hpp"
#include "tansr/canonical.hpp"
#include "tansr/executor.hpp"
#include "tansr/session.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace tansr;
template <class T> T take(Result<T> value) {
    if (!value)
        throw std::runtime_error(value.error().message + " [" + value.error().wire_code + "]");
    return std::move(value).value();
}
void take(Result<void> value) {
    if (!value)
        throw std::runtime_error(value.error().message + " [" + value.error().wire_code + "]");
}
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string text(const Json &value, const char *key) { return value.at(key).as_string(); }
// 共享 transport 实际收到 2xx 后丢回包；不伪造服务端 ACK 或修改原请求。
class LostAckResponse final : public HttpTransport {
  public:
    explicit LostAckResponse(std::shared_ptr<Runtime> runtime) : runtime_(std::move(runtime)) {}
    bool armed{false}, discarded{false};
    Result<HttpResponse> request(const HttpRequest &request,
                                 CancellationToken cancel = {}) override {
        auto result = runtime_->request(request, cancel);
        if (armed && result && request.url.find("/archive/acks") != std::string::npos &&
            result.value().status >= 200 && result.value().status < 300) {
            armed = false;
            discarded = true;
            return Error{ErrorCode::network, "fixture discarded actual accepted ACK response"};
        }
        return result;
    }
    Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &request,
                                               CancellationToken cancel = {}) override {
        return runtime_->stream(request, cancel);
    }

  private:
    std::shared_ptr<Runtime> runtime_;
};
void credentials(const Json &info, const std::filesystem::path &root) {
    require(text(info, "token") == "go-integration-token",
            "only registered synthetic token allowed");
    require(text(info, "applicationScopeId") == "go-app" && text(info, "endUserId") == "go-user" &&
                text(info, "authorizationRevision") == "1",
            "only registered synthetic scope allowed");
    for (const char *leaf : {"credentials", "keys", "archive", "intents", "materials", "journal"})
        take(storage::create_private_directory(root / leaf));
    auto access = []() -> Result<void> { return {}; };
    auto directory = take(storage::PrivateDirectory::open(root / "credentials", access));
    take(directory->write_atomic("token.txt", text(info, "token") + "\n", false));
    take(directory->write_atomic(
        "scope.json",
        Json::object({{"applicationScopeId", info.at("applicationScopeId")},
                      {"endUserId", info.at("endUserId")},
                      {"authorizationRevision", info.at("authorizationRevision")}})
            .dump(),
        false));
    directory.reset();
    directory = take(storage::PrivateDirectory::open(root / "keys", access));
    std::string key;
    for (int i = 0; i < 32; ++i)
        key += "71";
    take(directory->write_atomic("archive.key", key + "\n", false));
}
void idle(const session::Session &session) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        const auto meta = take(session.meta());
        if (meta.live && meta.status == "idle")
            return;
        require(meta.live && meta.status == "running" &&
                    std::chrono::steady_clock::now() < deadline,
                "actual session did not settle idle");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
Json stage(const archive::ArchiveClient &client, const std::string &id,
           const std::filesystem::path &root, LostAckResponse &transport, bool lose_accepted) {
    auto binding = take(client.binding(id)), status = take(client.status(id));
    auto identity = take(archive::identity_from_binding(binding, status));
    archive::StoreOptions options;
    options.path = root / "archive" / "history.bin";
    options.key.fill(0x71);
    options.key_id = "cpp-demo-test-key";
    options.identity = identity;
    options.check_access = [identity](const Json &actual) -> Result<void> {
        auto a = canonical::encode(actual), b = canonical::encode(identity);
        if (!a || !b || a.value() != b.value() || text(actual, "applicationScopeId") != "go-app" ||
            text(actual, "endUserId") != "go-user")
            return Error{ErrorCode::permission, "synthetic scope changed"};
        return {};
    };
    auto store = take(archive::FileStore::open(std::move(options)));
    require(!take(store->pending()), "unexpected existing pending ACK");
    auto head = take(store->head());
    auto page = take(client.records(
        binding, head ? std::optional<std::string>{text(*head, "sequence")} : std::nullopt));
    require(!page.at("records").as_array().empty(), "real next page required for pending ACK");
    archive::Bodies bodies;
    for (const auto &record : page.at("records").as_array()) {
        std::vector<Json> references{record.at("payload")};
        for (const auto &item : record.at("attachments").as_array())
            references.push_back(item);
        for (const auto &ref : references)
            if (!bodies.count(text(ref, "artifactId")))
                bodies.emplace(text(ref, "artifactId"), take(client.artifact(binding, ref)));
    }
    auto request = Json::object({{"requestId", "cpp-demo-pending-original"},
                                 {"operationEpoch", binding.at("operationEpoch").at("id")}});
    const auto original_deadline = unix_time_ms() + 90000;
    auto ack =
        take(store->receive(binding, status, page, std::move(bodies), request, original_deadline));
    require(take(store->pending()).has_value(), "pending ACK not durably retained");
    if (lose_accepted) {
        transport.armed = true;
        CallOptions context;
        context.deadline_ms = original_deadline;
        auto response = client.acknowledge(ack, context);
        require(!response && response.error().code == ErrorCode::network && transport.discarded,
                "actual accepted ACK response was not discarded");
        require(take(store->pending()).has_value() && !take(store->pending_rebase()),
                "lost response must retain original pending ACK");
    }
    // recover 必须由退出后另一个 Demo 进程完成，不能用本对象内存确认。
    Json::Array record_ids;
    for (const auto &record : page.at("records").as_array())
        record_ids.push_back(record.at("recordId"));
    return Json::object(
        {{"records", Json(static_cast<std::uint64_t>(page.at("records").as_array().size()))},
         {"recordIds", Json(std::move(record_ids))},
         {"request", ack.at("request")},
         {"ackBodySha256", take(crypto::sha256_hex(take(canonical::encode(ack))))},
         {"deadlineUnixMs", Json(original_deadline)},
         {"keyId", "cpp-demo-test-key"},
         {"ackSent", lose_accepted},
         {"acceptedResponseDiscarded", transport.discarded}});
}
Json observe_output(const std::shared_ptr<ApiClient> &api, const std::string &session_id,
                    const std::string &family) {
    auto observer_client = take(executor::Client::create(api, {"go-app", "go-user", "1"}));
    auto capabilities = take(observer_client->execution_capabilities(session_id));
    require(capabilities.binding.has_value(), "actual Demo binding missing");
    const auto &target = capabilities.binding->target;
    std::cout << "TANSR_CPP_DEMO_OUTPUT_READY" << std::endl;
    Json operation;
    const auto deadline = unix_time_ms() + 15000;
    while (operation.is_null()) {
        require(unix_time_ms() < deadline, "Demo operation was not observed");
        CallOptions poll;
        poll.parameters = {{"id", target.executor_id}};
        poll.query = {{"connectionId", target.connection_id}};
        auto batch = take(api->call("executor.operations.poll", std::move(poll))).body;
        for (const auto &candidate : batch.at("operations").as_array())
            if (text(candidate, "sessionId") == session_id)
                operation = candidate;
        if (operation.is_null())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CallOptions query;
    query.parameters = {{"id", session_id}};
    query.query = {{"contract", "terminal-services-v1"},
                   {"sessionContract", family},
                   {"operationId", text(operation, "operationId")},
                   {"requestDigest", text(operation, "digest")}};
    Json first;
    for (;;) {
        require(unix_time_ms() < deadline, "Demo output observation exceeded deadline");
        auto output = take(api->call("terminal.output.status", query)).body;
        if (first.is_null() && output.at("acceptedThrough").is_string()) {
            require(text(output, "state") == "receiving" && text(output, "nextByteOffset") == "21",
                    "first Demo stdout was not delivered during handler execution");
            const auto business =
                take(observer_client->status(session_id, text(operation, "operationId")));
            require(business.status == "pending",
                    "business completed before first output observation");
            first = Json::object({{"output", output},
                                  {"businessState", business.status},
                                  {"observedAtUnixMs", Json(unix_time_ms())}});
            std::cout << "TANSR_CPP_DEMO_OUTPUT_FIRST " << first.dump() << std::endl;
        }
        if (text(output, "state") == "complete") {
            require(
                !first.is_null() && text(output.at("seal"), "totalBytes") == "44" &&
                    !output.at("seal").at("truncated").as_bool() &&
                    text(output.at("seal"), "payloadDigest") ==
                        take(crypto::sha256_hex("order lookup started\norder lookup completed\n")),
                "Demo final output seal mismatch");
            return Json::object({{"operationId", operation.at("operationId")},
                                 {"first", first},
                                 {"final", output},
                                 {"completedAtUnixMs", Json(unix_time_ms())}});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
} // namespace
int main(int argc, char **argv) {
    std::shared_ptr<tansr::Runtime> runtime;
    try {
        require(argc >= 5 && argc <= 7,
                "usage: serve_demo_seed INFO FAMILY OS_TEMP_ROOT COMMAND [SESSION] [CHECKPOINT]");
        std::ifstream file(std::filesystem::u8path(argv[1]), std::ios::binary);
        require(static_cast<bool>(file), "fixture info missing");
        auto info = take(tansr::Json::parse(std::string{std::istreambuf_iterator<char>(file), {}}));
        require(info.at("manifestRevision").as_u64() == 7, "fixture is not revision 7");
        const std::string family = argv[2], command = argv[4];
        require(family == "sdk1" || family == "sdk2-offload-v1", "unknown explicit family");
        const auto root = std::filesystem::canonical(std::filesystem::u8path(argv[3]));
        runtime = take(tansr::Runtime::create());
        tansr::ClientOptions options;
        options.base_url = text(info, "baseURL");
        options.family = family;
        require(text(info, "token") == "go-integration-token",
                "only synthetic fixture credentials allowed");
        options.token_provider = [](tansr::CancellationToken) -> tansr::Result<tansr::AuthToken> {
            return tansr::AuthToken{"go-integration-token", "go-app/go-user"};
        };
        auto transport = std::make_shared<LostAckResponse>(runtime);
        auto api = take(tansr::ApiClient::create(std::move(options), transport));
        auto sessions = take(tansr::session::SessionClient::create(api));
        tansr::Json result = tansr::Json::object({{"command", command}});
        if (command == "credentials") {
            require(argc == 5, "credentials takes no session");
            credentials(info, root);
        } else {
            require(argc >= 6, "session is required");
            auto current = take(sessions.attach(argv[5]));
            if (command != "observe-output")
                idle(current);
            if (command == "observe-output") {
                result.set("output", observe_output(api, current.id(), family));
            } else if (command == "ack-status") {
                require(argc == 7, "ack-status needs original recovery request ID");
                archive::ArchiveClient client(api);
                auto target = take(client.binding_target(current.id()));
                auto request =
                    Json::object({{"requestId", argv[6]},
                                  {"operationEpoch", target.at("operationEpoch").at("id")}});
                result.set("receipt", take(client.operation(text(target, "bindingId"),
                                                            "archive-ack", request)));
            } else if (command == "target") {
                tansr::archive::ArchiveClient client(api);
                result.set("target", take(client.binding_target(current.id())));
            } else if (command == "archive-facts") {
                tansr::archive::ArchiveClient client(api);
                auto target = take(client.binding_target(current.id()));
                const auto binding_id = text(target, "bindingId");
                result.set("target", std::move(target));
                result.set("binding", take(client.binding(binding_id)));
                result.set("sourceStatus", take(client.status(binding_id)));
            } else if (command == "stage-pending" || command == "stage-accepted-pending") {
                tansr::archive::ArchiveClient client(api);
                auto target = take(client.binding_target(current.id()));
                result.set("staged", stage(client, text(target, "bindingId"), root, *transport,
                                           command == "stage-accepted-pending"));
            } else if (command == "checkpoint") {
                result.set("checkpointId",
                           take(current.checkpoint("demo-before-restore")).checkpoint_id);
            } else if (command == "restore") {
                require(argc == 7, "restore needs original checkpoint");
                // 组合失败必须保存真正 idle 的现场，不能用终态事件推断核心已完成清理。
                const auto meta = take(current.meta());
                require(meta.live && meta.status == "idle", "restore precondition changed");
                archive::ArchiveClient archives(api);
                auto target = take(archives.binding_target(current.id()));
                auto source_status = take(archives.status(text(target, "bindingId")));
                auto checkpoints = take(current.checkpoints());
                Json original_checkpoint;
                for (const auto &checkpoint : checkpoints)
                    if (checkpoint.checkpoint_id == argv[6])
                        original_checkpoint = checkpoint.raw;
                require(!original_checkpoint.is_null(), "original checkpoint missing");
                const auto snapshot = take(current.export_checkpoint(argv[6]));
                auto precondition = Json::object(
                    {{"sessionId", current.id()},
                     {"live", meta.live},
                     {"status", meta.status},
                     {"lastSeq", Json(meta.last_seq)},
                     {"checkpoint", original_checkpoint},
                     {"checkpointSnapshotSha256", take(crypto::sha256_hex(snapshot))},
                     {"checkpointSnapshotBytes", Json(static_cast<std::uint64_t>(snapshot.size()))},
                     {"bindingTarget", target},
                     {"sourceStatus", source_status},
                     {"historyTotal", take(current.history(0, 0)).at("total")}});
                std::cout << "TANSR_CPP_DEMO_RESTORE_PRECONDITION " << precondition.dump()
                          << std::endl;
                auto restored = current.restore(argv[6], false);
                if (!restored) {
                    // 仅此合成 fixture 显式留存结构化错误，不改变产品默认脱敏日志。
                    const auto &error = restored.error();
                    std::cout << "TANSR_CPP_DEMO_RESTORE_ERROR "
                              << Json::object({{"httpStatus", error.http_status},
                                               {"wireCode", error.wire_code},
                                               {"requestId", error.request_id},
                                               {"detail", error.detail.substr(0, 16384)}})
                                     .dump()
                              << std::endl;
                }
                result.set("receipt", take(std::move(restored)));
            } else if (command == "history") {
                result.set("history", take(current.history(0, 50)));
            } else
                throw std::runtime_error("unknown seed command");
        }
        api->shutdown();
        runtime->stop();
        require(take(runtime->shutdown(std::chrono::seconds(10))) == tansr::ShutdownStatus::stopped,
                "runtime shutdown pending");
        result.set("status", "passed");
        std::cout << "TANSR_CPP_DEMO_SEED " << result.dump() << std::endl;
        return 0;
    } catch (const std::exception &e) {
        if (runtime) {
            runtime->stop();
            (void)runtime->shutdown(std::chrono::seconds(10));
        }
        std::cerr << "demo seed failed: " << e.what() << '\n';
        return 1;
    }
}
