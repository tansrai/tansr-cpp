#include "archive_test_support.hpp"
#include "tansr/archive.hpp"
#include "tansr/canonical.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace tansr;
using namespace tansr::archive;
namespace {
using namespace archive_test_support;
class PendingTransport final : public HttpTransport {
  public:
    Json completed;
    bool ack_expired{true};
    std::vector<HttpRequest> requests;
    Result<HttpResponse> request(const HttpRequest &input, CancellationToken) override {
        requests.push_back(input);
        const bool expired = input.url.find("/operations") != std::string::npos || ack_expired;
        // Serve 对未提交过的查询也返回此值，不能以它代替重放原 ACK 的结果。
        const auto body =
            expired
                ? R"({"contract":"unified-v1","traceId":"archive-unit-trace","requestId":"original","code":"gone","status":410,"message":"receipt_expired","retryAction":"none","detail":{"domainCode":"receipt_expired","domainStatus":410,"domainRetryAction":"none"}})"
                : completed.dump();
        return HttpResponse{
            expired ? 410 : 200,
            {{"tansr-contract", "unified-v1"},
             {"tansr-manifest-revision", "7"},
             {"tansr-schema-hash",
              "sha256:969273844ca9196f19dd71b292b65a49307d63be0d20a0caf557e105ba6d8605"},
             {"tansr-domain", "archive"},
             {"content-type", "application/json"}},
            body};
    }
    Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &, CancellationToken) override {
        return Error{ErrorCode::internal, "no stream in pending recovery fixture"};
    }
};
void pending_replay(const std::filesystem::path &root) {
    const auto directory = root / "pending-replay";
    must(storage::create_private_directory(directory));
    auto store = must(FileStore::open(options(directory / "archive")));
    const auto ack = receive(*store);
    const auto original_deadline = *must(store->pending_deadline());
    auto transport = std::make_shared<PendingTransport>();
    transport->completed = receipt(store->identity(), ack);
    ClientOptions config;
    config.base_url = "https://serve.example.test";
    config.token_provider = [](CancellationToken) -> Result<AuthToken> {
        return AuthToken{"synthetic-token", "app-1/user-1"};
    };
    ArchiveClient client(must(ApiClient::create(std::move(config), transport)));
    CallOptions context;
    context.deadline_ms = original_deadline;
    auto missing = client.operation("binding-1", "archive-ack", ack.at("request"), context);
    check(!missing && missing.error().http_status == 410 && missing.error().wire_code == "gone" &&
              parse(missing.error().detail).at("domainCode").as_string() == "receipt_expired",
          "unsubmitted operation query is receipt_expired");
    transport->requests.clear();
    auto expired = sync_once(client, *store, "must-not-replace", context);
    check(!expired && expired.error().http_status == 410 && expired.error().wire_code == "gone" &&
              parse(expired.error().detail).at("domainCode").as_string() == "receipt_expired" &&
              !must(store->coverage()) &&
              must(canonical::encode(*must(store->pending()))) == must(canonical::encode(ack)),
          "actual expired ACK remains pending without automatic rebase");
    transport->ack_expired = false;
    auto recovered = must(sync_once(client, *store, "must-not-replace", context));
    check(recovered.recovered && recovered.receipt && !must(store->pending()) &&
              must(store->coverage()).has_value() && transport->requests.size() == 2,
          "query expiry cannot truncate explicit original ACK replay");
    for (const auto &sent : transport->requests) {
        check(sent.method == "POST" && sent.url.find("/archive/acks") != std::string::npos &&
                  sent.deadline_ms == original_deadline &&
                  must(canonical::encode(parse(sent.body))) == must(canonical::encode(ack)),
              "recovery sends exact durable body epoch and deadline");
        const auto header = [&](const char *name, const char *value) {
            return std::find(sent.headers.begin(), sent.headers.end(),
                             std::make_pair(std::string(name), std::string(value))) !=
                   sent.headers.end();
        };
        check(header("idempotency-key", "original") && header("if-match", "\"1\""),
              "recovery retains original key and revision");
    }
}
Fixture material_fixture(std::size_t attachment_bytes) {
    auto value = fixture();
    auto &record = value.page.at("records").at(0);
    auto &ref = record.at("attachments").at(0);
    auto &body = value.bodies.at("attachment-1");
    body.assign(attachment_bytes, '\x93');
    ref.set("bytes", Json(static_cast<std::uint64_t>(body.size())));
    ref.set("sha256", must(crypto::sha256_hex(body)));
    record.set("recordDigest", digest("tansr.sdk2.record.v1", without(record, "recordDigest")));
    value.status.set("pendingBytes", Json(static_cast<std::uint64_t>(
                                         body.size() + value.bodies.at("payload-1").size())));
    return value;
}
Json material_request(const Fixture &value, std::uint64_t chunk = 64) {
    const auto &record = value.page.at("records").at(0);
    return Json::object(
        {{"protocol", protocol},
         {"bindingId", "binding-1"},
         {"materialRequestId", "material-original"},
         {"target", value.binding.at("target")},
         {"sourceId", "source-1"},
         {"sourceGeneration", "source-generation-1"},
         {"purpose", "context-recall"},
         {"maxBytes", 1048576},
         {"remainingTtlMs", 30000},
         {"chunkBytes", Json(chunk)},
         {"requestedRecords",
          Json::array({Json::object({{"recordId", record.at("recordId")},
                                     {"digest", record.at("recordDigest")},
                                     {"payload", record.at("payload")},
                                     {"attachments", record.at("attachments")}})})}});
}
Json material_receipt() {
    return Json::object({{"protocol", protocol},
                         {"bindingId", "binding-1"},
                         {"materialRequestId", "material-original"},
                         {"state", "received"},
                         {"revision", "1"},
                         {"acceptedRecordIds", Json::array({"record-1"})}});
}
class MaterialTransport final : public HttpTransport {
  public:
    Fixture data;
    Json issued;
    std::vector<HttpRequest> requests;
    std::map<std::string, std::set<std::uint64_t>> uploaded;
    std::function<void(Json &)> mutate;
    std::function<void()> before_reply;
    std::optional<Json> receipt_override;
    int submit_status{202};
    bool lose_once{};
    Result<HttpResponse> request(const HttpRequest &input, CancellationToken) override {
        requests.push_back(input);
        Json value;
        int status = 200;
        if (input.url.find("/uploads/") != std::string::npos) {
            const bool post = input.method == "POST";
            const auto body = post ? parse(input.body) : Json();
            const auto id = post ? body.at("artifactId").as_string()
                            : input.url.find("payload-1") != std::string::npos ? "payload-1"
                                                                               : "attachment-1";
            const auto &record = data.page.at("records").at(0);
            const auto &ref =
                id == "payload-1" ? record.at("payload") : record.at("attachments").at(0);
            const auto chunk = issued.at("chunkBytes").as_u64();
            const auto total = ref.at("bytes").as_u64();
            if (post) {
                const auto offset = body.at("offset").as_u64();
                auto decoded = must(crypto::base64_decode(body.at("base64").as_string()));
                const std::string original(decoded.begin(), decoded.end());
                check(original == data.bodies.at(id).substr(static_cast<std::size_t>(offset),
                                                            static_cast<std::size_t>(chunk)),
                      "material upload preserves original binary slice");
                uploaded[id].insert(offset);
            }
            Json::Array offsets;
            std::uint64_t bytes{};
            for (auto offset : uploaded[id]) {
                offsets.emplace_back(offset);
                bytes += std::min(chunk, total - offset);
            }
            value = Json::object({{"protocol", protocol},
                                  {"bindingId", issued.at("bindingId")},
                                  {"materialRequestId", issued.at("materialRequestId")},
                                  {"uploadId", "upload-" + id},
                                  {"artifact", ref},
                                  {"state", bytes == total ? "committed" : "receiving"},
                                  {"chunkBytes", Json(chunk)},
                                  {"receivedOffsets", Json(std::move(offsets))},
                                  {"receivedBytes", Json(bytes)},
                                  {"remainingTtlMs", 30000}});
        } else {
            value = receipt_override.value_or(material_receipt());
            if (input.method == "POST")
                status = submit_status;
        }
        if (mutate)
            mutate(value);
        if (before_reply)
            before_reply();
        if (lose_once) {
            lose_once = false;
            return Error{ErrorCode::network, "synthetic committed upload response lost"};
        }
        return HttpResponse{
            status,
            {{"tansr-contract", "unified-v1"},
             {"tansr-manifest-revision", "7"},
             {"tansr-schema-hash",
              "sha256:969273844ca9196f19dd71b292b65a49307d63be0d20a0caf557e105ba6d8605"},
             {"tansr-domain", "archive"},
             {"content-type", "application/json"}},
            value.dump()};
    }
    Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &, CancellationToken) override {
        return Error{ErrorCode::internal, "no stream in material fixture"};
    }
    void reset() {
        requests.clear();
        uploaded.clear();
        mutate = {};
        before_reply = {};
        receipt_override.reset();
        submit_status = 202;
        lose_once = false;
    }
};
void material_boundaries(const std::filesystem::path &root) {
    const auto dir = root / "materials";
    must(storage::create_private_directory(dir));
    const auto data = material_fixture(1024);
    bool authorized = true;
    auto config = options(dir / "archive");
    config.check_access = [&](const Json &) -> Result<void> {
        if (!authorized)
            return Error{ErrorCode::permission, "material source authorization revoked"};
        return {};
    };
    auto store = must(FileStore::open(std::move(config)));
    auto ack = must(store->receive(data.binding, data.status, data.page, data.bodies, request(),
                                   unix_time_ms() + 60000));
    auto transport = std::make_shared<MaterialTransport>();
    transport->data = data;
    transport->issued = material_request(data);
    int providers{};
    ClientOptions client_options;
    client_options.base_url = "https://serve.example.test";
    client_options.token_provider = [&](CancellationToken) -> Result<AuthToken> {
        ++providers;
        return AuthToken{"synthetic-token", "app-1/user-1"};
    };
    ArchiveClient client(must(ApiClient::create(std::move(client_options), transport)));
    const auto identity = request("material-response-original");
    const auto original_deadline = unix_time_ms() + 20000;
    std::size_t zero_send_cases{};
    auto no_send = [&](const Json &issued, std::int64_t deadline, CallOptions context = {}) {
        must(validate_wire(protocol, "MaterialRequest", issued));
        const auto count = transport->requests.size();
        const auto provider_count = providers;
        auto result = client.prepare_materials_before(*store, issued, identity, deadline, context);
        check(!result && transport->requests.size() == count && providers == provider_count,
              "invalid material rejected before credentials and first request");
        ++zero_send_cases;
        return result.error().code;
    };
    check(no_send(transport->issued, unix_time_ms() - 1) == ErrorCode::timeout,
          "saved original deadline cannot renew from remaining TTL");
    CallOptions ended;
    ended.deadline_ms = unix_time_ms() - 1;
    check(no_send(transport->issued, original_deadline, ended) == ErrorCode::timeout,
          "earlier caller deadline wins");
    CancellationSource cancelled;
    cancelled.cancel();
    CallOptions stopped;
    stopped.cancel = cancelled.token();
    check(no_send(transport->issued, original_deadline, stopped) == ErrorCode::cancelled,
          "pre-cancelled material performs no upload");
    auto bad = transport->issued;
    bad.set("maxBytes", 1024);
    no_send(bad, original_deadline);
    bad = transport->issued;
    bad.set("chunkBytes", 63);
    no_send(bad, original_deadline);
    for (const auto *field : {"bindingId", "sourceId", "sourceGeneration"}) {
        bad = transport->issued;
        bad.set(field, "different");
        no_send(bad, original_deadline);
    }
    bad = transport->issued;
    bad.at("target").set("sessionId", "different");
    no_send(bad, original_deadline);
    for (const auto *field : {"historyEpoch", "deletionGeneration", "projectionRevision"}) {
        bad = transport->issued;
        bad.at("target").at("generations").set(field, "9");
        no_send(bad, original_deadline);
    }
    for (const auto *field : {"recordId", "digest"}) {
        bad = transport->issued;
        bad.at("requestedRecords")
            .at(0)
            .set(field, field == std::string("digest") ? std::string(64, '1') : "not-requested");
        no_send(bad, original_deadline);
    }
    for (const auto *field : {"payload", "attachments"}) {
        bad = transport->issued;
        auto &ref = field == std::string("payload")
                        ? bad.at("requestedRecords").at(0).at(field)
                        : bad.at("requestedRecords").at(0).at(field).at(0);
        ref.set("sha256", std::string(64, '1'));
        no_send(bad, original_deadline);
    }
    authorized = false;
    check(no_send(transport->issued, original_deadline) == ErrorCode::permission,
          "current authorization required before material bytes leave");
    authorized = true;

    // 单附件恰好16槽，但加上payload后超1MiB，必须整体预检，不能先发payload。
    const auto large_dir = root / "materials-large";
    must(storage::create_private_directory(large_dir));
    auto large = material_fixture(1048576);
    auto large_store = must(FileStore::open(options(large_dir / "archive")));
    must(large_store->receive(large.binding, large.status, large.page, large.bodies, request(),
                              original_deadline));
    const auto large_request = material_request(large, 65536);
    must(validate_wire(protocol, "MaterialRequest", large_request));
    check(!client.prepare_materials_before(*large_store, large_request, identity,
                                           original_deadline) &&
              providers == 0 && transport->requests.empty(),
          "1MiB whole-request cap rejects before any upload");
    ++zero_send_cases;
    large_store.reset();

    CallOptions bounded;
    bounded.deadline_ms = original_deadline - 1000;
    auto response = must(client.prepare_materials_before(*store, transport->issued, identity,
                                                         original_deadline, bounded));
    const auto expected_chunks = 16 + (data.bodies.at("payload-1").size() + 63) / 64;
    check(transport->requests.size() == expected_chunks && expected_chunks > 16,
          "16-slot cap is per artifact and exact boundary succeeds");
    for (const auto &sent : transport->requests)
        check(sent.deadline_ms == *bounded.deadline_ms,
              "all chunks retain earlier caller absolute deadline");
    check(!must(store->coverage()) && must(store->pending()).has_value(),
          "upload commit does not acknowledge archive coverage");

    // 首块已受理失回后，重放仍是原槽、原字节和原绝对截止。
    transport->reset();
    transport->lose_once = true;
    auto uncertain =
        client.prepare_materials_before(*store, transport->issued, identity, original_deadline);
    check(!uncertain && uncertain.error().code == ErrorCode::network &&
              transport->requests.size() == 1,
          "lost upload response stops without inventing success");
    const auto lost = transport->requests.front();
    CallOptions extended;
    extended.deadline_ms = original_deadline + 30000;
    auto replay = must(client.prepare_materials_before(*store, transport->issued, identity,
                                                       original_deadline, extended));
    check(transport->requests.size() == expected_chunks + 1 &&
              transport->requests.at(1).body == lost.body &&
              must(canonical::encode(replay)) == must(canonical::encode(response)),
          "retry retains original response identity and first slot bytes");
    for (const auto &sent : transport->requests)
        check(sent.deadline_ms == original_deadline, "retry cannot renew original deadline");
    CallOptions query_context;
    query_context.deadline_ms = original_deadline;
    must(client.material_upload_status("binding-1", "material-original", "attachment-1",
                                       query_context));
    check(transport->requests.back().deadline_ms == original_deadline,
          "upload query carries original deadline");

    transport->reset();
    // 此处验证首块在途越过原截止，不是预验证性能门；为插桩构建留足首发准备时间。
    // before_reply 仍等到同一绝对截止，后续块与过期重试必须保持零发送。
    const auto expires = unix_time_ms() + 1000;
    transport->before_reply = [expires] {
        const auto remaining = expires - unix_time_ms();
        if (remaining >= 0)
            CancellationToken{}.wait_for(std::chrono::milliseconds(remaining + 2));
    };
    auto timed_out = client.prepare_materials_before(*store, transport->issued, identity, expires);
    check(!timed_out && timed_out.error().code == ErrorCode::timeout &&
              transport->requests.size() == 1,
          "deadline during first chunk prevents remaining chunks");
    check(no_send(transport->issued, expires) == ErrorCode::timeout,
          "retry after original expiry remains zero request");
    const auto before_query = transport->requests.size();
    query_context.deadline_ms = expires;
    const auto before_provider = providers;
    check(!client.material_upload_status("binding-1", "material-original", "attachment-1",
                                         query_context) &&
              !client.material_status("binding-1", "material-original", query_context) &&
              transport->requests.size() == before_query && providers == before_provider,
          "expired original queries do not reach credentials or transport");
    zero_send_cases += 2;

    // 伪造的上传身份或见证不得导致继续传下一块。
    const std::vector<std::function<void(Json &)>> upload_faults{
        [](Json &value) { value.set("bindingId", "different"); },
        [](Json &value) { value.set("materialRequestId", "different"); },
        [](Json &value) { value.at("artifact").set("artifactId", "different"); },
        [](Json &value) { value.at("artifact").set("sourceId", "different"); },
        [](Json &value) { value.at("artifact").set("sha256", std::string(64, '1')); },
        [](Json &value) { value.set("receivedOffsets", Json::array({1})); },
        [](Json &value) { value.set("receivedBytes", 63); }};
    for (const auto &fault : upload_faults) {
        transport->reset();
        transport->mutate = fault;
        auto denied =
            client.prepare_materials_before(*store, transport->issued, identity, original_deadline);
        check(!denied && denied.error().code == ErrorCode::contract &&
                  transport->requests.size() == 1,
              "wrong upload identity witness or offsets stop after first response");
    }

    const auto intent_dir = root / "material-intents";
    must(storage::create_private_directory(intent_dir));
    auto intents =
        must(storage::PrivateDirectory::open(intent_dir, []() -> Result<void> { return {}; }));
    must(SavedIntent::save(*intents, "response", "material-response", response, original_deadline));
    intents.reset();
    intents =
        must(storage::PrivateDirectory::open(intent_dir, []() -> Result<void> { return {}; }));
    const auto saved = must(SavedIntent::load(*intents, "response"));
    const std::vector<std::function<void(Json &)>> receipt_faults{
        [](Json &value) { value.set("bindingId", "different"); },
        [](Json &value) { value.set("materialRequestId", "different"); },
        [](Json &value) { value.set("acceptedRecordIds", Json::array()); },
        [](Json &value) { value.set("acceptedRecordIds", Json::array({"not-requested"})); },
        [](Json &value) { value.set("acceptedRecordIds", Json::array({"record-1", "record-1"})); },
        [](Json &value) { value.set("revision", "0"); },
        [](Json &value) { value.set("state", "verified"); },
        [](Json &value) { value.set("state", "core-consumed"); }};
    for (const auto &fault : receipt_faults) {
        transport->reset();
        transport->mutate = fault;
        auto denied = client.submit_materials(saved, extended);
        check(!denied && denied.error().code == ErrorCode::contract &&
                  transport->requests.size() == 1 &&
                  transport->requests.front().deadline_ms == original_deadline,
              "202 with wrong material identity or state rejected under original deadline");
    }
    transport->reset();
    transport->submit_status = 200;
    check(!client.submit_materials(saved), "200 cannot substitute for required material 202");
    transport->reset();
    auto received = must(client.submit_materials(saved, extended));
    const auto &sent = transport->requests.front();
    check(received.at("state").as_string() == "received" && sent.deadline_ms == original_deadline &&
              must(canonical::encode(parse(sent.body))) == must(canonical::encode(response)) &&
              std::find(sent.headers.begin(), sent.headers.end(),
                        std::make_pair(std::string("idempotency-key"),
                                       std::string("material-response-original"))) !=
                  sent.headers.end(),
          "cold response submit preserves body key epoch and absolute deadline");
    transport->receipt_override = received;
    transport->receipt_override->set("state", "core-consumed");
    check(must(client.material_status("binding-1", "material-original")).at("state").as_string() ==
              "core-consumed",
          "only separate status reports core consumption");
    transport->receipt_override->set("materialRequestId", "other-material");
    check(!client.material_status("binding-1", "material-original"),
          "wrong material status identity rejected");
    check(!must(store->coverage()) &&
              must(canonical::encode(*must(store->pending()))) == must(canonical::encode(ack)) &&
              must(store->body(data.page.at("records").at(0).at("attachments").at(0))) ==
                  data.bodies.at("attachment-1"),
          "received and consumed receipts neither release archive bytes nor advance coverage");

    const auto expiring = must(
        SavedIntent::save(*intents, "expired", "material-response", response, unix_time_ms() + 25));
    CancellationToken{}.wait_for(std::chrono::milliseconds(30));
    transport->reset();
    const auto providers_before_expired = providers;
    auto expired = client.submit_materials(expiring, extended);
    check(!expired && expired.error().code == ErrorCode::timeout && transport->requests.empty() &&
              providers == providers_before_expired,
          "saved response expiry cannot be extended at submission");
    ++zero_send_cases;

    // 冻结 ArtifactRef.bytes 的 minimum=1；不能按上传DTO注释放宽点名对象。
    auto empty = material_fixture(0);
    transport->reset();
    const auto providers_before_empty = providers;
    const auto empty_request = material_request(empty);
    check(
        !validate_wire(protocol, "MaterialRequest", empty_request) &&
            !client.prepare_materials_before(*store, empty_request, identity, original_deadline) &&
            transport->requests.empty() && providers == providers_before_empty,
        "zero-byte artifact rejected by frozen schema before credentials or upload");
    ++zero_send_cases;
    std::cout << "TANSR_CPP_MATERIAL "
              << Json::object(
                     {{"zeroSendCases", Json(static_cast<std::uint64_t>(zero_send_cases))},
                      {"boundaryChunks", Json(static_cast<std::uint64_t>(expected_chunks))},
                      {"badUploadReceipts", Json(static_cast<std::uint64_t>(upload_faults.size()))},
                      {"bad202Receipts", Json(static_cast<std::uint64_t>(receipt_faults.size()))},
                      {"status", "passed"}})
                     .dump()
              << std::endl;
}
std::filesystem::path fresh() {
    auto root = std::filesystem::canonical(std::filesystem::temp_directory_path());
    auto random = must(crypto::random_bytes(12));
    std::string name = "tansr-cpp-archive-";
    const char *hex = "0123456789abcdef";
    for (auto byte : random) {
        name += hex[byte >> 4];
        name += hex[byte & 15];
    }
    root /= name;
    must(storage::create_private_directory(root));
    return root;
}
std::string file_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void put_bytes(const std::filesystem::path &path, const std::string &bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output)
        throw std::runtime_error("test write failed");
}
int run_child(const std::filesystem::path &program, const std::filesystem::path &path, int stage) {
#ifdef _WIN32
    std::wstring command = L"\"" + program.wstring() + L"\" --crash " + std::to_wstring(stage) +
                           L" \"" + path.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                         nullptr, nullptr, &startup, &process) != 0,
          "spawn crash child");
    auto waited = WaitForSingleObject(process.hProcess, 15000);
    DWORD code = 99;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    check(waited == WAIT_OBJECT_0, "crash child exits");
    return static_cast<int>(code);
#else
    const auto pid = fork();
    check(pid >= 0, "fork crash child");
    if (pid == 0) {
        auto number = std::to_string(stage);
        execl(program.c_str(), program.c_str(), "--crash", number.c_str(), path.c_str(), nullptr);
        _exit(99);
    }
    int status{};
    check(waitpid(pid, &status, 0) == pid, "wait crash child");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 99;
#endif
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--crash") {
        const int stage = std::stoi(argv[2]);
        bool armed = false;
        auto config = options(std::filesystem::u8path(argv[3]));
        config.commit_hook = [&](storage::CommitStage now) -> Result<void> {
            if (armed && static_cast<int>(now) == stage)
                std::_Exit(73);
            return {};
        };
        auto store = must(FileStore::open(std::move(config)));
        armed = true;
        receive(*store);
        return 99;
    }
    std::filesystem::path root;
    try {
        root = fresh();
        pending_replay(root);
        material_boundaries(root);
        const auto path = root / "archive.bin";
        auto f = fixture();
        auto store = must(FileStore::open(options(path)));
        check(!must(store->head()) && !must(store->coverage()) && !must(store->pending()),
              "initial state empty");
        auto wrong = f;
        wrong.bodies.erase("attachment-1");
        check(!store->receive(wrong.binding, wrong.status, wrong.page, wrong.bodies, request(),
                              unix_time_ms() + 60000),
              "missing attachment zero ACK");
        wrong = f;
        wrong.bodies["payload-1"] = parse(wrong.bodies["payload-1"]).dump();
        check(!store->receive(wrong.binding, wrong.status, wrong.page, wrong.bodies, request(),
                              unix_time_ms() + 60000),
              "reencoded payload zero ACK");
        wrong = f;
        wrong.page.at("records").at(0).set("predecessorDigest", std::string(64, '1'));
        check(!store->receive(wrong.binding, wrong.status, wrong.page, wrong.bodies, request(),
                              unix_time_ms() + 60000),
              "broken record digest rejected");
        auto ack = receive(*store);
        check(must(store->pending()).has_value() && !must(store->coverage()),
              "durable head differs from coverage");
        check(must(store->body(f.page.at("records").at(0).at("payload"))) ==
                  f.bodies.at("payload-1"),
              "payload original bytes");
        check(file_bytes(path).find("synthetic archive bytes") == std::string::npos,
              "disk is encrypted");
        check(!store->receive(f.binding, f.status, f.page, f.bodies, request("replacement"),
                              unix_time_ms() + 60000),
              "pending cannot be replaced");
        const auto deadline = must(store->pending_deadline());
        store.reset();
        store = must(FileStore::open(options(path)));
        check(must(canonical::encode(*must(store->pending()))) == must(canonical::encode(ack)) &&
                  must(store->pending_deadline()) == deadline,
              "cold reopen original body identity deadline");
        auto accepted = receipt(store->identity(), ack);
        auto invalid = accepted;
        invalid.set("state", "received");
        check(!store->confirm(invalid), "noncompleted cannot advance coverage");
        invalid = accepted;
        invalid.set("semanticDigest", std::string(64, '0'));
        check(!store->confirm(invalid), "wrong semantic digest rejected");
        must(store->confirm(accepted));
        must(store->confirm(accepted));
        check(!must(store->pending()) && must(store->coverage()).has_value(),
              "exact completed receipt confirms once");
        store.reset();
        auto original = file_bytes(path);
        auto bad_key = options(path);
        bad_key.key[0] ^= 1;
        check(!FileStore::open(std::move(bad_key)) && file_bytes(path) == original,
              "wrong key preserves ciphertext");
        auto bad_scope = options(path);
        bad_scope.identity.set("endUserId", "other-user");
        check(!FileStore::open(std::move(bad_scope)) && file_bytes(path) == original,
              "other scope cannot open archive");
        auto tampered = original;
        tampered.back() ^= 1;
        put_bytes(path, tampered);
        check(!FileStore::open(options(path)) && file_bytes(path) == tampered,
              "tamper releases no state and preserves bytes");
        put_bytes(path, original);
        store = must(FileStore::open(options(path)));
        crypto::Aes256Key next_key{};
        next_key.fill(9);
        must(store->rotate_key(next_key, "key-2"));
        store.reset();
        check(!FileStore::open(options(path)), "old key rejected after explicit rotation");
        auto rotated = options(path);
        rotated.key = next_key;
        rotated.key_id = "key-2";
        store = must(FileStore::open(std::move(rotated)));
        check(must(store->coverage()).has_value(), "rotation preserves confirmed state");
        store.reset();
        const auto recover_dir = root / "recovery";
        must(storage::create_private_directory(recover_dir));
        auto recover = must(FileStore::open(options(recover_dir / "archive")));
        auto previous = receive(*recover);
        auto cross_epoch = request("rebase");
        cross_epoch.set("operationEpoch", "epoch-2");
        check(!recover->prepare_rebase(cross_epoch, unix_time_ms() + 60000),
              "rebase cannot change epoch");
        auto intent = must(recover->prepare_rebase(request("rebase"), unix_time_ms() + 60000));
        check(must(canonical::encode(*must(recover->pending()))) ==
                  must(canonical::encode(previous)),
              "rebase intent retains original pending");
        auto next = previous;
        next.set("request", request("rebase"));
        next.set("expectedRevision", "2");
        auto result = Json::object({{"protocol", protocol},
                                    {"bindingId", "binding-1"},
                                    {"previous", previous},
                                    {"request", request("rebase")},
                                    {"next", next},
                                    {"receipt", receipt(recover->identity(), next, "3")}});
        auto bad_result = result;
        bad_result.at("next").set("sourceId", "other-source");
        check(!recover->confirm_rebase(bad_result), "rebase cannot change semantic body");
        must(recover->confirm_rebase(result));
        check(!must(recover->pending_rebase()) && !must(recover->pending()) &&
                  must(recover->coverage()).has_value(),
              "rebase completed scope receipt advances coverage");
        recover.reset();
        for (int stage = 0; stage < 5; ++stage) {
            const auto dir = root / ("crash-" + std::to_string(stage));
            must(storage::create_private_directory(dir));
            const auto target = dir / "archive";
            {
                auto initial = must(FileStore::open(options(target)));
            }
            check(run_child(std::filesystem::absolute(argv[0]), target, stage) == 73,
                  "actual child exits during commit");
            auto reopened = must(FileStore::open(options(target)));
            check(!must(reopened->coverage()), "crash never invents confirmed coverage");
            check(must(reopened->pending()).has_value() == (stage >= 3),
                  "cold state is complete old or new snapshot");
        }
        const auto intent_dir = root / "intents";
        must(storage::create_private_directory(intent_dir));
        auto directory =
            must(storage::PrivateDirectory::open(intent_dir, []() -> Result<void> { return {}; }));
        auto creation =
            Json::object({{"protocol", protocol},
                          {"request", request("create")},
                          {"target", f.binding.at("target")},
                          {"expectedRevision", "0"},
                          {"requiredCapabilities", Json::array({"archive-transfer-v1"})},
                          {"optionalCapabilities", Json::array()},
                          {"archive", Json::object({{"strategy", "single-authorized-source"},
                                                    {"sourceId", "source-1"},
                                                    {"durability", "source-ack-with-durable-spool"},
                                                    {"delivery", "required"},
                                                    {"sessionAvailability", "legacy-complete"},
                                                    {"ackFormat", "split-receipts-v1"}})}});
        auto saved = must(SavedIntent::save(*directory, "creation", "binding-create", creation,
                                            unix_time_ms() + 30000));
        auto loaded = must(SavedIntent::load(*directory, "creation"));
        check(saved.deadline_ms() == loaded.deadline_ms() &&
                  must(canonical::encode(saved.body())) == must(canonical::encode(loaded.body())),
              "creation intent exact cold roundtrip");
        check(!SavedIntent::save(*directory, "creation", "binding-create", creation,
                                 unix_time_ms() + 60000),
              "saved intent cannot extend deadline by overwrite");
        directory.reset();
        std::filesystem::remove_all(root);
        std::cout << "archive checks=" << checks << " passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "archive test failed: " << e.what() << "; retained root=" << root.u8string()
                  << "\n";
        return 1;
    }
}
