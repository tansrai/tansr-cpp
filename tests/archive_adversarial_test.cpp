#include "archive_test_support.hpp"
#include <ctime>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace archive_test_support;
namespace {
std::filesystem::path directory(const std::filesystem::path &parent, const std::string &name) {
    auto path = parent / name;
    must(storage::create_private_directory(path));
    return path;
}
std::string file_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void replace_bytes(const std::filesystem::path &path, const std::string &bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(output), "fixture replacement writes complete existing file");
}
std::string encoded(const Json &value) { return must(canonical::encode(value)); }
std::string iso(std::int64_t milliseconds) {
    const auto seconds = static_cast<std::time_t>(milliseconds / 1000);
    std::tm date{};
#ifdef _WIN32
    check(gmtime_s(&date, &seconds) == 0, "UTC fixture conversion");
#else
    check(gmtime_r(&seconds, &date) != nullptr, "UTC fixture conversion");
#endif
    std::ostringstream out;
    out << std::put_time(&date, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
Fixture two_records() {
    auto data = fixture();
    auto next = data.page.at("records").at(0);
    next.set("recordId", "record-2");
    next.set("turnId", "turn-2");
    next.set("sequence", "2");
    next.set("predecessorDigest", next.at("recordDigest"));
    next.set("recordDigest", digest("tansr.sdk2.record.v1", without(next, "recordDigest")));
    data.page.at("records").as_array().push_back(next);
    data.page.set("nextAfterSequence", "2");
    data.page.set("publishedThroughSequence", "2");
    data.status.set("publishedThroughSequence", "2");
    data.status.set("pendingRecords", 2);
    return data;
}
HttpResponse reply(Json body, int status = 200) {
    return {status,
            {{"tansr-contract", "unified-v1"},
             {"tansr-manifest-revision", "7"},
             {"tansr-schema-hash",
              "sha256:969273844ca9196f19dd71b292b65a49307d63be0d20a0caf557e105ba6d8605"},
             {"tansr-domain", "archive"},
             {"content-type", "application/json"}},
            body.dump()};
}
HttpResponse rejected(const char *code, int status, const char *domain, const char *reason = "") {
    auto detail = Json::object({{"domainCode", domain}});
    if (*reason)
        detail.set("reason", reason);
    auto body =
        Json::object({{"contract", "unified-v1"},
                      {"traceId", "archive-boundary"},
                      {"requestId", "original"},
                      {"code", code},
                      {"status", status},
                      {"retryAction", std::string(domain) == "closure_stale"       ? "rediscover"
                                      : std::string(code) == "precondition_failed" ? "refresh"
                                                                                   : "none"},
                      {"message", "synthetic archive rejection"},
                      {"detail", detail}});
    must(validate_wire("unified-v1", "UnifiedError", body));
    return reply(std::move(body), status);
}
class ArchiveTransport final : public HttpTransport {
  public:
    Fixture data = fixture();
    int acks{}, rebases{}, queries{}, artifacts{};
    std::vector<HttpRequest> requests;
    std::function<void(Json &)> mutate_artifact;
    std::function<void()> after_artifact, before_ack;
    std::optional<HttpResponse> ack_error;
    std::function<Result<HttpResponse>(const HttpRequest &)> rebase;
    std::optional<Json> queried;
    Result<HttpResponse> request(const HttpRequest &input, CancellationToken) override {
        requests.push_back(input);
        if (input.url.find("/ack-rebases") != std::string::npos) {
            ++rebases;
            if (rebase)
                return rebase(input);
            return Error{ErrorCode::network, "synthetic rebase response unavailable"};
        }
        if (input.url.find("/archive/acks") != std::string::npos) {
            ++acks;
            if (before_ack)
                before_ack();
            if (ack_error)
                return *ack_error;
            auto ack = parse(input.body);
            const auto revision =
                std::to_string(std::stoull(ack.at("expectedRevision").as_string()) + 1);
            auto result = receipt(must(identity_from_binding(data.binding, data.status)), ack,
                                  revision.c_str());
            return reply(result);
        }
        if (input.url.find("/operations") != std::string::npos) {
            ++queries;
            if (queried)
                return reply(*queried);
            return rejected("gone", 410, "receipt_expired");
        }
        if (input.url.find("/archive/status") != std::string::npos)
            return reply(data.status);
        if (input.url.find("/archive/records") != std::string::npos)
            return reply(data.page);
        if (input.url.find("/archive/artifacts/") != std::string::npos) {
            ++artifacts;
            const std::string id =
                input.url.find("payload-1") != std::string::npos ? "payload-1" : "attachment-1";
            const auto found = data.bodies.find(id);
            if (found == data.bodies.end())
                return rejected("not_found", 404, "not_found");
            const auto &record = data.page.at("records").at(0);
            const auto &ref =
                id == "payload-1" ? record.at("payload") : record.at("attachments").at(0);
            const auto &bytes = found->second;
            auto chunk = Json::object({{"protocol", protocol},
                                       {"bindingId", "binding-1"},
                                       {"artifactId", id},
                                       {"sourceId", "source-1"},
                                       {"generations", data.binding.at("target").at("generations")},
                                       {"offset", 0},
                                       {"bytes", Json(static_cast<std::uint64_t>(bytes.size()))},
                                       {"totalBytes", ref.at("bytes")},
                                       {"sha256", ref.at("sha256")},
                                       {"chunkSha256", must(crypto::sha256_hex(bytes))},
                                       {"base64", crypto::base64_encode(bytes)}});
            if (mutate_artifact)
                mutate_artifact(chunk);
            if (after_artifact)
                after_artifact();
            return reply(chunk);
        }
        return reply(data.binding);
    }
    Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &, CancellationToken) override {
        return Error{ErrorCode::internal, "no stream in archive boundary fixture"};
    }
};
ArchiveClient client(const std::shared_ptr<ArchiveTransport> &transport) {
    ClientOptions config;
    config.base_url = "https://serve.example.test";
    config.token_provider = [](CancellationToken) -> Result<AuthToken> {
        return AuthToken{"synthetic-token", "app-1/user-1"};
    };
    return ArchiveClient(must(ApiClient::create(std::move(config), transport)));
}
void page_chain(const std::filesystem::path &root) {
    for (int fault = 0; fault < 10; ++fault) {
        auto transport = std::make_shared<ArchiveTransport>();
        transport->data = two_records();
        auto &records = transport->data.page.at("records").as_array();
        bool authorized = true;
        auto config = options(directory(root, "page-" + std::to_string(fault)) / "archive");
        config.check_access = [&](const Json &) -> Result<void> {
            return authorized
                       ? Result<void>{}
                       : Result<void>{Error{ErrorCode::permission, "revoked during artifact read"}};
        };
        auto store = must(FileStore::open(config));
        const auto original = file_bytes(config.path);
        switch (fault) {
        case 0:
            std::swap(records[0], records[1]);
            break;
        case 1:
            records[1].set("sequence", "3");
            records[1].set("recordDigest",
                           digest("tansr.sdk2.record.v1", without(records[1], "recordDigest")));
            break;
        case 2:
            records[1].set("predecessorDigest", std::string(64, '1'));
            records[1].set("recordDigest",
                           digest("tansr.sdk2.record.v1", without(records[1], "recordDigest")));
            break;
        case 3:
            records[1].set("recordDigest", std::string(64, '1'));
            break;
        case 4:
            transport->data.bodies["payload-1"] = parse(transport->data.bodies["payload-1"]).dump();
            break;
        case 5:
            transport->data.bodies["attachment-1"][0] ^= 1;
            break;
        case 6:
            transport->mutate_artifact = [](Json &value) { value.set("sourceId", "other-source"); };
            break;
        case 7:
            transport->data.bodies.erase("attachment-1");
            break;
        case 8:
            transport->after_artifact = [&] { authorized = false; };
            break;
        case 9:
            records[1].at("payload").set("sourceId", "other-source");
            records[1].set("recordDigest",
                           digest("tansr.sdk2.record.v1", without(records[1], "recordDigest")));
            break;
        }
        auto result = sync_once(client(transport), *store, "must-not-ack");
        authorized = true;
        check(!result && transport->acks == 0 && !must(store->head()) && !must(store->pending()) &&
                  !must(store->coverage()) && file_bytes(config.path) == original,
              "bad page chain bytes source or read-time authority produces zero ACK and unchanged "
              "store");
    }
    auto transport = std::make_shared<ArchiveTransport>();
    transport->data = two_records();
    const auto second = transport->data.page.at("records").at(1);
    transport->data.page.at("records").as_array().pop_back();
    transport->data.page.set("complete", false);
    transport->data.page.set("nextAfterSequence", "1");
    auto store = must(FileStore::open(options(directory(root, "pages-positive") / "archive")));
    auto api = client(transport);
    check(must(sync_once(api, *store, "first-page")).records == 1, "first incomplete page commits");
    transport->data.binding.set("revision", "2");
    transport->data.status.set("revision", "2");
    transport->data.status.set("acknowledgedCoverage", *must(store->coverage()));
    transport->data.page.set("records", Json::array({second}));
    transport->data.page.set("nextAfterSequence", "2");
    transport->data.page.set("complete", true);
    auto result = must(sync_once(api, *store, "second-page"));
    check(result.records == 1 && result.complete && transport->acks == 2 &&
              must(store->head())->at("sequence").as_string() == "2",
          "second page joins persisted predecessor and advances exact coverage");
    auto foreign = second.at("payload");
    foreign.set("sourceId", "other-source");
    check(!store->body(foreign), "local archive rejects foreign Source witness");
    foreign = second.at("payload");
    foreign.set("artifactId", "https://untrusted.invalid/file");
    check(!store->body(foreign), "local archive does not interpret URL or path as artifact ID");
    std::cout << "CPP_A26 pageFaults=10 zeroAck=10 contiguousPages=2 passed\n";
}
void commit_and_rotation(const std::filesystem::path &root) {
    for (int stage = 0; stage < 5; ++stage) {
        bool armed{};
        auto config = options(directory(root, "commit-" + std::to_string(stage)) / "archive");
        config.commit_hook = [&](storage::CommitStage now) -> Result<void> {
            if (armed && static_cast<int>(now) == stage)
                return Error{ErrorCode::io, "injected durability boundary failure"};
            return {};
        };
        auto store = must(FileStore::open(config));
        const auto old = file_bytes(config.path);
        armed = true;
        auto transport = std::make_shared<ArchiveTransport>();
        check(!sync_once(client(transport), *store, "durable-before-ack") && transport->acks == 0,
              "failed commit stage never reaches ACK transport");
        store.reset();
        armed = false;
        auto reopened = must(FileStore::open(config));
        check(!must(reopened->coverage()) && must(reopened->pending()).has_value() == (stage >= 3),
              "cold commit failure exposes only complete old or new pending state");
        check((file_bytes(config.path) == old) == (stage < 3),
              "pre-replace failures preserve ciphertext");
    }
    bool durable{};
    auto config = options(directory(root, "commit-order") / "archive");
    config.commit_hook = [&](storage::CommitStage stage) -> Result<void> {
        if (stage == storage::CommitStage::written)
            durable = false;
        if (stage == storage::CommitStage::directory_synced)
            durable = true;
        return {};
    };
    auto store = must(FileStore::open(config));
    auto transport = std::make_shared<ArchiveTransport>();
    transport->before_ack = [&] {
        check(durable &&
                  file_bytes(config.path).find("synthetic archive bytes") == std::string::npos,
              "actual ACK entry occurs after encrypted file and directory commit");
    };
    must(sync_once(client(transport), *store, "ordered-ack"));
    check(transport->acks == 1 && must(store->coverage()).has_value(),
          "durable page receives exact ACK");
    store.reset();
    config = options(directory(root, "nonce-cap") / "archive");
    config.max_encryptions = 1;
    store = must(FileStore::open(config));
    const auto old = file_bytes(config.path);
    transport = std::make_shared<ArchiveTransport>();
    auto capped = sync_once(client(transport), *store, "cannot-encrypt");
    check(!capped && capped.error().code == ErrorCode::capacity && transport->acks == 0 &&
              file_bytes(config.path) == old && !must(store->pending()),
          "nonce usage cap refuses encryption before ACK and preserves original state");
    store.reset();
    config = options(directory(root, "nonce-confirm") / "archive");
    config.max_encryptions = 2;
    store = must(FileStore::open(config));
    transport = std::make_shared<ArchiveTransport>();
    auto unconfirmed = sync_once(client(transport), *store, "same-after-rotation");
    check(!unconfirmed && unconfirmed.error().code == ErrorCode::capacity && transport->acks == 1 &&
              must(store->pending()).has_value() && !must(store->coverage()),
          "confirmation cap keeps original durable pending after accepted ACK");
    crypto::Aes256Key new_key{};
    new_key.fill(23);
    must(store->rotate_key(new_key, "key-next"));
    must(sync_once(client(transport), *store, "ignored-replacement"));
    check(must(store->coverage()).has_value() && !must(store->pending()),
          "explicit key rotation enables confirmation with original pending identity");
    store.reset();
    for (int stage = 0; stage < 5; ++stage) {
        bool armed{};
        auto rotation = options(directory(root, "rotation-" + std::to_string(stage)) / "archive");
        rotation.commit_hook = [&](storage::CommitStage now) -> Result<void> {
            if (armed && static_cast<int>(now) == stage)
                return Error{ErrorCode::io, "injected rotation failure"};
            return {};
        };
        auto rotating = must(FileStore::open(rotation));
        receive(*rotating);
        const auto prior = file_bytes(rotation.path);
        armed = true;
        check(!rotating->rotate_key(new_key, "rotated") && !rotating->head(),
              "failed rotation freezes plaintext access until cold reopen");
        rotating.reset();
        armed = false;
        check((file_bytes(rotation.path) == prior) == (stage < 3),
              "rotation respects atomic old/new boundary");
        auto old_open = FileStore::open(rotation);
        check(static_cast<bool>(old_open) == (stage < 3),
              "rotation failure determines surviving old key");
        if (old_open)
            old_open.value().reset();
        rotation.key = new_key;
        rotation.key_id = "rotated";
        auto new_open = FileStore::open(rotation);
        check(static_cast<bool>(new_open) == (stage >= 3),
              "rotation never leaves mixed-key ciphertext");
    }
    std::cout << "CPP_A27 durabilityFaults=5 rotationFaults=5 nonceCaps=2 passed\n";
}
void recovery_boundaries(const std::filesystem::path &root) {
    const std::vector<HttpResponse> refused{
        rejected("precondition_failed", 412, "closure_stale"),
        rejected("precondition_failed", 412, "binding_conflict", "if_match_body_mismatch"),
        rejected("precondition_failed", 412, "unrelated", "if_match_stale"),
        rejected("capacity_exceeded", 429, "capacity_exceeded"),
        rejected("forbidden", 403, "forbidden"),
        rejected("gone", 410, "request_expired"),
        rejected("gone", 410, "receipt_expired")};
    for (std::size_t index = 0; index < refused.size(); ++index) {
        auto store = must(FileStore::open(
            options(directory(root, "non-stale-" + std::to_string(index)) / "archive")));
        const auto pending = receive(*store);
        const auto deadline = must(store->pending_deadline());
        auto transport = std::make_shared<ArchiveTransport>();
        transport->ack_error = refused[index];
        auto result = recover_pending(client(transport), *store, "must-not-create-rebase");
        check(!result && transport->acks == 1 && transport->rebases == 0 &&
                  !must(store->pending_rebase()) && !must(store->coverage()) &&
                  encoded(*must(store->pending())) == encoded(pending) &&
                  must(store->pending_deadline()) == deadline,
              "ordinary 412 busy revoked or expired does not create or send recovery identity");
    }
    for (int epoch_case = 0; epoch_case < 3; ++epoch_case) {
        auto store = must(FileStore::open(
            options(directory(root, "epoch-" + std::to_string(epoch_case)) / "archive")));
        const auto pending = receive(*store);
        auto transport = std::make_shared<ArchiveTransport>();
        transport->ack_error =
            rejected("precondition_failed", 412, "binding_conflict", "if_match_stale");
        auto &epoch = transport->data.binding.at("operationEpoch");
        const auto now = unix_time_ms();
        epoch.set("issuedAt", iso(now - 50000));
        epoch.set("expiresAt", iso(now + 5000));
        if (epoch_case == 0)
            epoch.set("id", "replacement-epoch");
        else if (epoch_case == 1) {
            epoch.set("issuedAt", iso(now - 55000));
            epoch.set("expiresAt", iso(now - 1000));
        } else {
            epoch.set("issuedAt", iso(now + 10000));
            epoch.set("expiresAt", iso(now + 20000));
        }
        auto result = recover_pending(client(transport), *store, "must-not-change-epoch");
        check(!result && transport->rebases == 0 && !must(store->pending_rebase()) &&
                  encoded(*must(store->pending())) == encoded(pending),
              "recovery cannot substitute expired future or different operation epoch");
    }
    for (int wrong_receipt = 0; wrong_receipt < 2; ++wrong_receipt) {
        auto config = options(directory(root, "ack-rebase-race-" + std::to_string(wrong_receipt)) /
                              "archive");
        auto store = must(FileStore::open(config));
        const auto pending = receive(*store);
        const auto intent =
            must(store->prepare_rebase(request("fixed-recovery"), unix_time_ms() + 30000));
        auto transport = std::make_shared<ArchiveTransport>();
        auto api = client(transport);
        transport->rebase = [&](const HttpRequest &sent) -> Result<HttpResponse> {
            check(encoded(parse(sent.body)) == encoded(intent),
                  "rebase transmits saved exact intent");
            // 确定性交错：恢复请求在途，另一线程按原身份完成 ACK，再返回冲突。
            auto competing =
                std::async(std::launch::async, [&] { return api.acknowledge(pending); });
            auto original = must(competing.get());
            if (wrong_receipt)
                original.set("semanticDigest", std::string(64, '1'));
            transport->queried = original;
            return rejected("conflict", 409, "request_id_conflict");
        };
        auto result = recover_pending(api, *store, "ignored-new-recovery");
        if (transport->rebases != 1 || transport->acks != 1 || transport->queries != 1) {
            std::cerr << "race counters rebase=" << transport->rebases << " ack=" << transport->acks
                      << " query=" << transport->queries;
            if (!result)
                std::cerr << " error=" << result.error().message
                          << " detail=" << result.error().detail;
            std::cerr << '\n';
        }
        check(transport->rebases == 1 && transport->acks == 1 && transport->queries == 1,
              "concurrent original completion queries original identity exactly once");
        check(static_cast<bool>(result) == !wrong_receipt &&
                  must(store->coverage()).has_value() == !wrong_receipt &&
                  must(store->pending_rebase()).has_value() == static_cast<bool>(wrong_receipt),
              "only exact original completed scope digest resolves rebase race");
        store.reset();
        auto cold = must(FileStore::open(config));
        check(must(cold->coverage()).has_value() == !wrong_receipt,
              "race conclusion survives cold encrypted reopen");
    }
    // 显式恢复请求/映射落盘失败时，绝不先发送 rebase。
    bool armed{};
    auto config = options(directory(root, "rebase-not-durable") / "archive");
    config.commit_hook = [&](storage::CommitStage stage) -> Result<void> {
        if (armed && stage == storage::CommitStage::before_replace)
            return Error{ErrorCode::io, "rebase intent persistence failed"};
        return {};
    };
    auto store = must(FileStore::open(config));
    const auto pending = receive(*store);
    const auto before = file_bytes(config.path);
    auto transport = std::make_shared<ArchiveTransport>();
    transport->ack_error =
        rejected("precondition_failed", 412, "binding_conflict", "if_match_stale");
    auto &epoch = transport->data.binding.at("operationEpoch");
    epoch.set("issuedAt", iso(unix_time_ms() - 10000));
    epoch.set("expiresAt", iso(unix_time_ms() + 40000));
    armed = true;
    check(!recover_pending(client(transport), *store, "recovery-before-send") &&
              transport->rebases == 0 && file_bytes(config.path) == before,
          "failed durable recovery intent sends zero rebase requests");
    store.reset();
    armed = false;
    store = must(FileStore::open(config));
    check(!must(store->pending_rebase()) && encoded(*must(store->pending())) == encoded(pending),
          "failed recovery persistence preserves original ACK on cold reopen");
    std::cout << "CPP_A29 wrongTriggers=7 epochGuards=3 concurrentRaces=2 "
                 "zeroRebaseOnWriteFailure=1 passed\n";
}
void capacity_and_format(const std::filesystem::path &root) {
    for (int mode = 0; mode < 4; ++mode) {
        auto data = two_records();
        std::size_t record_bytes{};
        for (const auto &record : data.page.at("records").as_array())
            record_bytes += encoded(record).size();
        const auto full_bytes = record_bytes + data.bodies.at("payload-1").size() +
                                data.bodies.at("attachment-1").size();
        auto config = options(directory(root, "capacity-" + std::to_string(mode)) / "archive");
        if (mode == 0)
            config.limits.max_records = 1;
        else if (mode == 1)
            config.limits.max_artifacts = 1;
        else if (mode == 2)
            config.limits.max_batch_bytes = full_bytes - 1;
        else {
            config.limits.max_stored_bytes = full_bytes - 1;
            config.limits.max_batch_bytes = full_bytes - 1;
        }
        auto store = must(FileStore::open(config));
        auto transport = std::make_shared<ArchiveTransport>();
        transport->data = data;
        if (mode == 3) {
            const auto first = receive(*store);
            must(store->confirm(receipt(store->identity(), first)));
            transport->data.binding.set("revision", "2");
            transport->data.status.set("revision", "2");
            transport->data.status.set("acknowledgedCoverage", *must(store->coverage()));
            transport->data.page.set("records", Json::array({data.page.at("records").at(1)}));
        }
        const auto before = file_bytes(config.path);
        check(!sync_once(client(transport), *store, "cannot-fit") && transport->acks == 0 &&
                  file_bytes(config.path) == before && !must(store->pending()),
              "record object total or batch capacity rejects without ACK or disk mutation");
        if (mode == 3)
            check(
                transport->artifacts == 2 &&
                    must(store->head())->at("sequence").as_string() == "1" &&
                    must(store->coverage())->at("throughSequence").as_string() == "1",
                "individually fitting second page exceeds total storage without changing history");
    }
    for (int mode = 0; mode < 2; ++mode) {
        auto transport = std::make_shared<ArchiveTransport>();
        auto &record = transport->data.page.at("records").at(0);
        if (mode == 0) {
            for (int index = 0; encoded(record).size() <= 1024; ++index) {
                auto ref = record.at("attachments").at(0);
                ref.set("artifactId", "extra-" + std::to_string(index));
                record.at("attachments").as_array().push_back(ref);
            }
        } else {
            transport->data.bodies["attachment-1"] = std::string(1025, 'b');
            auto &ref = record.at("attachments").at(0);
            ref.set("bytes", 1025);
            ref.set("sha256", must(crypto::sha256_hex(transport->data.bodies.at("attachment-1"))));
        }
        record.set("recordDigest", digest("tansr.sdk2.record.v1", without(record, "recordDigest")));
        auto api = client(transport);
        check(must(api.records(transport->data.binding)).at("records").as_array().size() == 1,
              "same valid record and attachment witnesses pass their original wider byte limits");
        transport->data.binding.at("limits").set(mode == 0 ? "recordBytes" : "attachmentBytes",
                                                 1024);
        must(validate_wire("sdk2-ext-v1", "BindingView", transport->data.binding));
        auto config = options(directory(root, "byte-cap-" + std::to_string(mode)) / "archive");
        auto store = must(FileStore::open(config));
        const auto before = file_bytes(config.path);
        check(!sync_once(api, *store, "cannot-fit-byte-cap") && transport->artifacts == 0 &&
                  transport->acks == 0 && file_bytes(config.path) == before,
              "record or attachment byte cap rejects before body download or ACK");
    }
    auto measure = must(FileStore::open(options(directory(root, "reserve-measure") / "archive")));
    const auto ack = receive(*measure);
    const auto deadline = unix_time_ms() + 60000;
    const auto intent = Json::object({{"protocol", protocol},
                                      {"bindingId", "binding-1"},
                                      {"previous", ack},
                                      {"request", request("reserve-recovery")}});
    const auto row = Json::object({{"intent", intent},
                                   {"deadline", Json(deadline)},
                                   {"result", Json()},
                                   {"originalReceipt", Json()}});
    const auto data = fixture();
    const auto used = encoded(data.page.at("records").at(0)).size() +
                      data.bodies.at("payload-1").size() + data.bodies.at("attachment-1").size();
    constexpr std::size_t maximum_rebase_receipt = 528384;
    const auto threshold = used + encoded(row).size() + maximum_rebase_receipt;
    measure.reset();
    for (int boundary = 0; boundary < 2; ++boundary) {
        auto config = options(directory(root, "reserve-" + std::to_string(boundary)) / "archive");
        config.limits.max_stored_bytes = threshold - (boundary == 0 ? 1U : 0U);
        config.limits.max_batch_bytes = config.limits.max_stored_bytes;
        auto store = must(FileStore::open(config));
        receive(*store);
        const auto before = file_bytes(config.path);
        auto saved = store->prepare_rebase(request("reserve-recovery"), deadline);
        check(static_cast<bool>(saved) == (boundary == 1),
              "recovery maximum-result reserve exact byte boundary");
        if (!saved) {
            check(saved.error().code == ErrorCode::capacity && file_bytes(config.path) == before &&
                      !must(store->pending_rebase()) &&
                      encoded(*must(store->pending())) == encoded(ack),
                  "insufficient recovery reserve preserves original file and identity");
        } else {
            auto next = ack;
            next.set("request", request("reserve-recovery"));
            next.set("expectedRevision", "2");
            auto completed = Json::object({{"protocol", protocol},
                                           {"bindingId", "binding-1"},
                                           {"previous", ack},
                                           {"request", request("reserve-recovery")},
                                           {"next", next},
                                           {"receipt", receipt(store->identity(), next, "3")}});
            must(store->confirm_rebase(completed));
            store.reset();
            store = must(FileStore::open(config));
            check(must(store->coverage()).has_value() && !must(store->pending_rebase()),
                  "reserved recovery completion persists and cold opens at capacity boundary");
        }
    }
    auto config = options(directory(root, "format") / "archive");
    auto store = must(FileStore::open(config));
    receive(*store);
    store.reset();
    const auto original = file_bytes(config.path);
    std::vector<std::string> invalid;
    invalid.push_back("SQLite format 3\0unrelated archive media");
    invalid.push_back(original.substr(0, 24));
    auto changed = original;
    changed[changed.find('1')] = '2';
    invalid.push_back(changed);
    changed = original;
    changed.back() ^= 1;
    invalid.push_back(changed);
    const auto prefix = original.find('\n') + 1;
    crypto::GcmNonce nonce{};
    crypto::GcmTag tag{};
    std::copy_n(reinterpret_cast<const std::uint8_t *>(original.data() + prefix), nonce.size(),
                nonce.begin());
    std::copy_n(
        reinterpret_cast<const std::uint8_t *>(original.data() + original.size() - tag.size()),
        tag.size(), tag.begin());
    auto plain = must(crypto::aes256_gcm_decrypt(
        config.key, nonce,
        std::string_view(original).substr(prefix + nonce.size(),
                                          original.size() - prefix - nonce.size() - tag.size()),
        tag, std::string_view(original).substr(0, prefix)));
    auto state =
        parse(std::string_view(reinterpret_cast<const char *>(plain.data()), plain.size()));
    state.set("format", "unrecognized-internal-format-v2");
    auto next_nonce = must(crypto::random_bytes(nonce.size()));
    std::copy(next_nonce.begin(), next_nonce.end(), nonce.begin());
    auto sealed = must(crypto::aes256_gcm_encrypt(config.key, nonce, state.dump(),
                                                  std::string_view(original).substr(0, prefix)));
    changed = original.substr(0, prefix);
    changed.append(reinterpret_cast<const char *>(nonce.data()), nonce.size());
    changed.append(reinterpret_cast<const char *>(sealed.ciphertext.data()),
                   sealed.ciphertext.size());
    changed.append(reinterpret_cast<const char *>(sealed.tag.data()), sealed.tag.size());
    invalid.push_back(changed);
    for (const auto &bytes : invalid) {
        replace_bytes(config.path, bytes);
        check(!FileStore::open(config) && file_bytes(config.path) == bytes,
              "unrelated truncated unauthenticated or unknown-version media returns no store and "
              "preserves bytes");
    }
    replace_bytes(config.path, original);
    check(static_cast<bool>(FileStore::open(config)),
          "original archive remains usable after rejected media probes");
    std::cout << "CPP_A32 capacityFaults=6 recoveryReserveBoundaries=2 formatFaults=5 passed\n";
}
void read_revocation(const std::filesystem::path &root) {
    bool armed{};
    int accesses{};
    auto config = options(directory(root, "read-revocation") / "archive");
    config.check_access = [&](const Json &) -> Result<void> {
        if (armed && ++accesses == 2)
            return Error{ErrorCode::permission, "authorization revoked before plaintext delivery"};
        return {};
    };
    auto store = must(FileStore::open(config));
    receive(*store);
    armed = true;
    auto body = store->body(fixture().page.at("records").at(0).at("payload"));
    check(!body && body.error().code == ErrorCode::permission && accesses == 2,
          "authorization revoked during local read releases zero plaintext");
}
} // namespace
int main() {
    std::filesystem::path root;
    try {
        auto random = must(crypto::random_bytes(8));
        std::string name = "tansr-archive-boundary-";
        for (auto byte : random)
            name += "0123456789abcdef"[byte & 15];
        root = directory(std::filesystem::canonical(std::filesystem::temp_directory_path()), name);
        page_chain(root);
        commit_and_rotation(root);
        recovery_boundaries(root);
        capacity_and_format(root);
        read_revocation(root);
        std::filesystem::remove_all(root);
        std::cout << "archive adversarial checks=" << checks << " passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "archive adversarial failed: " << error.what() << "; checks=" << checks
                  << "; retained=" << root.u8string() << '\n';
        return 1;
    }
}
