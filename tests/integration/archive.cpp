// CPP-04：真实 Serve/kernel，只有上游认证/模型是已登记的合成工装。
// 参数：fixture info JSON、family、OS 临时工作目录。不会自行启动/修改 Serve。
#include "tansr/archive.hpp"
#include "tansr/canonical.hpp"
#include "tansr/session.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace tansr;
using namespace tansr::archive;
namespace {
int checks{};
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class T> T must(Result<T> value) {
    if (!value)
        throw std::runtime_error(value.error().message + " [" + value.error().wire_code + "]");
    return std::move(value).value();
}
void must(Result<void> value) {
    if (!value)
        throw std::runtime_error(value.error().message + " [" + value.error().wire_code + "]");
}
bool same(const Json &left, const Json &right) {
    return must(canonical::encode(left)) == must(canonical::encode(right));
}
std::string text(const Json &value, const char *key) { return value.at(key).as_string(); }
class Deadline {
  public:
    explicit Deadline(std::chrono::milliseconds timeout)
        : worker_([this, timeout] {
              if (!stop_.token().wait_for(timeout))
                  cancel_.cancel();
          }) {}
    ~Deadline() {
        stop_.cancel();
        worker_.join();
    }
    CancellationToken token() const { return cancel_.token(); }

  private:
    CancellationSource cancel_, stop_;
    std::thread worker_;
};
// 转发实际成功响应后，在共享 transport 的返回边界丢弃它；不伪造任何回执。
class LostResponse final : public HttpTransport {
  public:
    explicit LostResponse(std::shared_ptr<Runtime> runtime) : runtime_(std::move(runtime)) {}
    void arm(std::string suffix) { suffix_ = std::move(suffix); }
    int discarded{};
    std::string sent_body;
    Result<HttpResponse> request(const HttpRequest &request,
                                 CancellationToken cancel = {}) override {
        auto result = runtime_->request(request, cancel);
        if (result && !suffix_.empty() && request.url.find(suffix_) != std::string::npos &&
            result.value().status >= 200 && result.value().status < 300) {
            suffix_.clear();
            sent_body = request.body;
            ++discarded;
            return Error{ErrorCode::network, "test discarded actual committed response"};
        }
        return result;
    }
    Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &request,
                                               CancellationToken cancel = {}) override {
        return runtime_->stream(request, cancel);
    }

  private:
    std::shared_ptr<Runtime> runtime_;
    std::string suffix_;
};
void control(const Json &command) {
    std::cout << "TANSR_CPP_CONTROL " << command.dump() << std::endl;
    std::string line;
    check(static_cast<bool>(std::getline(std::cin, line)), "fixture control reply missing");
    constexpr std::string_view prefix = "TANSR_RUST_CONTROL ";
    if (line.rfind(prefix, 0) == 0)
        line.erase(0, prefix.size());
    auto reply = must(Json::parse(line));
    check(same(reply.at("requestId"), command.at("requestId")) && reply.at("ok").as_bool(),
          "fixture rejected control");
}
session::Session create(const session::SessionClient &sessions, const std::string &family,
                        const std::string &id) {
    session::CreateOptions options;
    if (family == "sdk2-offload-v1")
        options.request_id = id;
    options.write.deadline_ms = unix_time_ms() + 30000;
    return must(sessions.create(std::move(options)));
}
std::string turn(const session::Session &session, const std::string &id) {
    auto before = must(session.meta());
    auto tracker = must(session::TurnTracker::create(before.last_seq));
    Deadline timeout(std::chrono::seconds(25));
    auto stream = must(session.events(std::to_string(before.last_seq), timeout.token()));
    session::WriteOptions write;
    write.request_key = id;
    write.deadline_ms = unix_time_ms() + 25000;
    write.cancel = timeout.token();
    must(session.send("CPP-ARCHIVE synthetic retained material", write));
    std::string text;
    for (;;) {
        auto event = must(stream.next(timeout.token()));
        check(event.has_value(), "turn EOF before completion");
        if (event->kind() == "msg.text.delta")
            if (const auto *delta = event->raw().find("text"))
                text += delta->as_string();
        if (auto outcome = tracker.observe(*event)) {
            check(outcome->status == session::OutcomeStatus::completed,
                  "actual current turn completed");
            break;
        }
    }
    stream.shutdown();
    const auto settle = unix_time_ms() + 10000;
    for (;;) {
        auto meta = must(session.meta());
        if (meta.status == "idle")
            break;
        check(meta.status == "running" && unix_time_ms() < settle,
              "archive capture settle bounded");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return text;
}
StoreOptions store_options(const std::filesystem::path &path, const Json &identity) {
    StoreOptions options;
    options.path = path;
    options.identity = identity;
    options.key.fill(0x71);
    options.key_id = "cpp-integration-key";
    options.check_access = [identity](const Json &actual) -> Result<void> {
        if (!same(actual, identity) || text(actual, "applicationScopeId") != "go-app" ||
            text(actual, "endUserId") != "go-user")
            return Error{ErrorCode::permission, "synthetic archive principal mismatch"};
        return {};
    };
    return options;
}
struct Captured {
    Json binding, status, page, identity, ack;
    Bodies bodies;
};
Captured capture(const ArchiveClient &client, const session::Session &session,
                 const std::filesystem::path &path, std::unique_ptr<FileStore> &store,
                 const char *request_id) {
    auto target = must(client.binding_target(session.id()));
    check(!target.at("bindingId").is_null(), "host supplied binding");
    const auto id = text(target, "bindingId");
    auto binding = must(client.binding(id)), status = must(client.status(id));
    auto identity = must(identity_from_binding(binding, status));
    store = must(FileStore::open(store_options(path, identity)));
    auto page = must(client.records(binding));
    check(!page.at("records").as_array().empty(), "actual turn published records");
    Bodies bodies;
    for (const auto &record : page.at("records").as_array()) {
        std::vector<Json> references{record.at("payload")};
        for (const auto &ref : record.at("attachments").as_array())
            references.push_back(ref);
        for (const auto &ref : references)
            if (!bodies.count(text(ref, "artifactId")))
                bodies.emplace(text(ref, "artifactId"), must(client.artifact(binding, ref)));
    }
    auto request = Json::object(
        {{"requestId", request_id}, {"operationEpoch", binding.at("operationEpoch").at("id")}});
    auto ack = must(store->receive(binding, status, page, bodies, request, unix_time_ms() + 60000));
    check(!must(store->coverage()), "disk receive does not invent coverage");
    for (const auto &record : page.at("records").as_array())
        check(must(store->body(record.at("payload"))) ==
                  bodies.at(text(record.at("payload"), "artifactId")),
              "original payload bytes survive encrypted store");
    return {binding, status, page, identity, ack, std::move(bodies)};
}
void manual(const ArchiveClient &client, LostResponse &transport, const Json &info,
            const std::filesystem::path &root) {
    const auto session = text(info, "sessionId"), source = text(info, "sourceId");
    check(must(client.binding_target(session)).at("bindingId").is_null(),
          "manual target initially unbound");
    CallOptions context;
    context.deadline_ms = unix_time_ms() + 60000;
    auto input = must(client.prepare_create(session, source, "cpp-first-binding", context));
    auto directory =
        must(storage::PrivateDirectory::open(root, []() -> Result<void> { return {}; }));
    auto intent = must(
        SavedIntent::save(*directory, "creation", "binding-create", input, *context.deadline_ms));
    transport.arm("/api/archive/bindings");
    check(!client.create_binding(intent, context) && transport.discarded == 1,
          "actual creation response discarded");
    check(same(must(Json::parse(transport.sent_body)), input),
          "creation sent persisted exact body");
    directory.reset();
    directory = must(storage::PrivateDirectory::open(root, []() -> Result<void> { return {}; }));
    auto recovered = must(SavedIntent::load(*directory, "creation"));
    auto receipt =
        must(client.creation_operation(session, recovered.body().at("request"), context));
    check(text(receipt, "state") == "completed", "original creation query completed");
    auto binding = must(client.binding(text(receipt, "bindingId")));
    check(text(binding, "sourceId") == source && same(binding.at("target"), input.at("target")),
          "original creation binding identity");
    check(text(must(client.create_binding(recovered, context)), "bindingId") ==
              text(binding, "bindingId"),
          "replay does not create second binding");
    check(!client.prepare_create(session, source, "must-not-recreate"),
          "second creation intent rejected");
}
void ordinary(const ArchiveClient &client, LostResponse &transport,
              const session::SessionClient &sessions, const std::string &family,
              const std::filesystem::path &root) {
    const auto first_dir = root / "first";
    must(storage::create_private_directory(first_dir));
    const auto path = first_dir / "archive";
    auto session = create(sessions, family, "cpp-archive-create");
    check(turn(session, "cpp-archive-first").find("go-archive-answer") != std::string::npos,
          "actual synthetic model text");
    std::unique_ptr<FileStore> store;
    auto data = capture(client, session, path, store, "cpp-original-ack");
    const auto id = text(data.binding, "bindingId");
    CallOptions original_ack;
    original_ack.deadline_ms = *must(store->pending_deadline());
    transport.arm("/archive/acks");
    check(!client.acknowledge(data.ack, original_ack) && transport.discarded == 1,
          "actual ACK success response discarded");
    check(same(must(Json::parse(transport.sent_body)), data.ack), "sent original durable ACK body");
    store.reset();
    store = must(FileStore::open(store_options(path, data.identity)));
    auto recovered = must(sync_once(client, *store, "must-not-replace-original"));
    check(recovered.recovered && recovered.receipt &&
              same(recovered.receipt->at("request"), data.ack.at("request")),
          "lost ACK reconciles original request");
    check(same(*must(store->coverage()), data.ack.at("coverage")) &&
              same(must(client.status(id)).at("acknowledgedCoverage"), data.ack.at("coverage")),
          "local and actual coverage agree");
    store.reset();
    store = must(FileStore::open(store_options(path, data.identity)));
    check(must(store->coverage()).has_value(), "confirmed encrypted state cold reopens");
    auto current = must(client.binding(id));
    auto events = must(client.events(current));
    Json::Array records;
    for (const auto &record : data.page.at("records").as_array())
        records.push_back(record.at("recordId"));
    auto subject = Json::object({{"endUserId", "go-user"}, {"sessionId", session.id()}});
    control(Json::object({{"requestId", "cpp-issue-material"},
                          {"command", "request-materials"},
                          {"subject", subject},
                          {"request", Json::object({{"materialRequestId", "cpp-recall-original"},
                                                    {"recordIds", Json(std::move(records))},
                                                    {"purpose", "context-recall"}})}}));
    Json material;
    {
        Deadline timeout(std::chrono::seconds(10));
        for (;;) {
            auto event = must(events.next(timeout.token()));
            check(event.has_value(), "archive events remain open");
            auto envelope = must(Json::parse(event->data));
            const auto &raw = envelope.at("raw");
            if (text(raw, "eventType") == "material.request") {
                material = raw.at("payload");
                break;
            }
        }
    }
    const auto original_deadline =
        unix_time_ms() + static_cast<std::int64_t>(material.at("remainingTtlMs").as_u64());
    const auto intents_path = root / "intents";
    must(storage::create_private_directory(intents_path));
    auto intents =
        must(storage::PrivateDirectory::open(intents_path, []() -> Result<void> { return {}; }));
    must(SavedIntent::save(*intents, "request", "material-request", material, original_deadline));
    auto response = must(client.prepare_materials_before(
        *store, material,
        Json::object({{"requestId", "cpp-original-material"},
                      {"operationEpoch", current.at("operationEpoch").at("id")}}),
        original_deadline));
    auto intent = must(
        SavedIntent::save(*intents, "response", "material-response", response, original_deadline));
    const auto before_material = *must(store->coverage());
    transport.arm("/material-responses");
    auto submitted = client.submit_materials(intent);
    check(!submitted && transport.discarded == 2, "actual material response discarded");
    intents.reset();
    intents =
        must(storage::PrivateDirectory::open(intents_path, []() -> Result<void> { return {}; }));
    auto saved = must(SavedIntent::load(*intents, "response"));
    check(text(must(client.material_status(id, text(material, "materialRequestId"))), "state") ==
              "received",
          "received is not consumed");
    check(same(*must(store->coverage()), before_material),
          "material ingress does not change archive coverage");
    check(text(must(client.submit_materials(saved)), "state") == "received",
          "same response replay retains identity");
    control(Json::object(
        {{"requestId", "cpp-enqueue-material"},
         {"command", "enqueue-materials"},
         {"subject", subject},
         {"request", Json::object({{"materialRequestId", material.at("materialRequestId")},
                                   {"leaseId", "cpp-material-consumer"}})}}));
    check(!turn(session, "cpp-material-turn").empty(), "material consumption turn completes");
    const auto until = unix_time_ms() + 10000;
    for (;;) {
        auto state =
            text(must(client.material_status(id, text(material, "materialRequestId"))), "state");
        if (state == "core-consumed")
            break;
        check((state == "received" || state == "verified") && unix_time_ms() < until,
              "core consumption bounded");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    events.cancel();
    auto next = must(sync_once(client, *store, "cpp-after-material-ack"));
    check(next.records > 0 && next.receipt.has_value(), "next archive page after material turn");
    store.reset();
    must(session.close());

    // 第二会话触发真实 stale If-Match；恢复意图不更改原 pending/epoch。
    const auto stale_dir = root / "stale";
    must(storage::create_private_directory(stale_dir));
    const auto stale_path = stale_dir / "archive";
    auto stale_session = create(sessions, family, "cpp-stale-create");
    turn(stale_session, "cpp-stale-first");
    auto stale_data = capture(client, stale_session, stale_path, store, "cpp-stale-original");
    turn(stale_session, "cpp-stale-second");
    const auto changed = must(client.binding(text(stale_data.binding, "bindingId")));
    check(!same(changed.at("revision"), stale_data.ack.at("expectedRevision")),
          "second actual turn advances binding revision");
    auto refused = sync_once(client, *store, "must-not-auto-rebase");
    if (!refused)
        std::cerr << "TANSR_CPP_STALE "
                  << Json::object({{"errorCode", static_cast<int>(refused.error().code)},
                                   {"httpStatus", refused.error().http_status},
                                   {"wireCode", refused.error().wire_code},
                                   {"message", refused.error().message},
                                   {"detail", refused.error().detail}})
                         .dump()
                  << std::endl;
    check(!refused && refused.error().wire_code == "precondition_failed",
          "ordinary sync reports actual stale revision");
    check(!must(store->coverage()) && same(*must(store->pending()), stale_data.ack),
          "stale preserves pending original");
    auto recovery_request =
        Json::object({{"requestId", "cpp-fixed-recovery"},
                      {"operationEpoch", stale_data.ack.at("request").at("operationEpoch")}});
    auto recovery = must(store->prepare_rebase(recovery_request, unix_time_ms() + 60000));
    CallOptions rebase_context;
    rebase_context.deadline_ms = *must(store->pending_deadline());
    transport.arm("/archive/ack-rebases");
    check(!client.rebase_acknowledgement(recovery, rebase_context) && transport.discarded == 3,
          "actual rebase success response discarded");
    store.reset();
    store = must(FileStore::open(store_options(stale_path, stale_data.identity)));
    auto result = must(recover_pending(client, *store, "must-not-replace-recovery"));
    check(result.recovered && result.receipt &&
              same(result.receipt->at("request"), recovery_request),
          "cold rebase uses original recovery identity");
    check(same(*must(store->coverage()), stale_data.ack.at("coverage")) &&
              !must(store->pending_rebase()),
          "exact rebase receipt confirms original coverage");
    check(must(sync_once(client, *store, "cpp-after-rebase")).records > 0,
          "rebase is not entire archive completion");
    store.reset();
    must(stale_session.close());
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "usage: archive INFO_JSON FAMILY OS_TEMP_WORKSPACE");
        std::ifstream input(std::filesystem::u8path(argv[1]), std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(input), {}};
        auto info = must(Json::parse(bytes));
        check(info.at("manifestRevision").as_u64() == 7, "fixture frozen revision");
        const std::string family = argv[2];
        check(family == "sdk1" || family == "sdk2-offload-v1", "explicit family");
        const auto workspace = std::filesystem::canonical(std::filesystem::u8path(argv[3]));
        const auto root = workspace / ("cpp-archive-" + family);
        must(storage::create_private_directory(root));
        auto runtime = must(Runtime::create());
        auto transport = std::make_shared<LostResponse>(runtime);
        ClientOptions options;
        options.base_url = text(info, "baseURL");
        options.family = family;
        const auto token = text(info, "token");
        options.token_provider = [token](CancellationToken) -> Result<AuthToken> {
            return AuthToken{token, "go-app/go-user"};
        };
        auto api = must(ApiClient::create(std::move(options), transport));
        ArchiveClient client(api);
        if (text(info, "mode") == "archive-manual")
            manual(client, *transport, info, root);
        else {
            auto sessions = must(session::SessionClient::create(api));
            ordinary(client, *transport, sessions, family, root);
        }
        api->shutdown();
        check(must(runtime->shutdown(std::chrono::seconds(5))) == ShutdownStatus::stopped,
              "runtime quiescent");
        std::cout << "TANSR_CPP_ARCHIVE "
                  << Json::object({{"family", family},
                                   {"checks", checks},
                                   {"discardedActualResponses", transport->discarded},
                                   {"status", "passed"}})
                         .dump()
                  << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "archive integration failed: " << error.what() << "\n";
        return 1;
    }
}
