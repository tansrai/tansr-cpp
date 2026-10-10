#include "tansr/memory_publication.hpp"
#include "tansr/operations.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace tansr;
namespace mp = tansr::memory_publication;
namespace ex = tansr::executor;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
    return std::move(r).value();
}
void take(Result<void> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
}
Json request(const char *action, const char *id = "transfer") {
    auto r = Json::object({{"contract", "terminal-services-v1"},
                           {"sourceId", "source"},
                           {"sourceGeneration", "1"},
                           {"domainKey", "domain"},
                           {"action", action}});
    if (std::string(action) != "head" && std::string(action) != "read")
        r.set("transferId", id);
    return r;
}
Json begin_request(std::string_view body, const char *id = "transfer", Json expected = {}) {
    auto r = request("begin", id);
    r.set("expectedEtag", expected);
    r.set("byteLength", Json(static_cast<std::uint64_t>(body.size())));
    r.set("sha256", take(crypto::sha256_hex(body)));
    return r;
}
Json chunk(std::string_view body, const char *id = "transfer", std::uint64_t offset = 0) {
    auto r = request("chunk", id);
    r.set("offset", Json(offset));
    r.set("byteLength", Json(static_cast<std::uint64_t>(body.size())));
    r.set("base64", crypto::base64_encode(body));
    r.set("payloadDigest", take(crypto::sha256_hex(body)));
    return r;
}
ex::Operation operation(const Json &input, std::string id = "operation") {
    ex::Operation op{std::move(id),
                     "session",
                     {"app", "user", "1"},
                     {"binding", "1", {"device", "connection", "1", "workspace", "1", {}}},
                     "MemoryPublication",
                     {"tool.invoke", Json::object({{"name", mp::tool_name},
                                                   {"definitionDigest", mp::tool_digest},
                                                   {"argsJson", input.dump()}})},
                     "",
                     "2099-01-01T00:00:00Z"};
    op.digest = take(ex::operation_digest(op));
    return op;
}
Json owner(const ex::Operation &op) {
    auto raw = ex::to_json(op);
    return Json::object({{"scope", raw.at("scope")},
                         {"sessionId", raw.at("sessionId")},
                         {"binding", raw.at("binding")}});
}
std::string status(const Json &r) { return r.at("transfer").at("status").as_string(); }
std::string bytes(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
class Fixture final : public HttpTransport {
  public:
    ex::Operation op;
    explicit Fixture(ex::Operation value) : op(std::move(value)) {}
    Result<HttpResponse> request(const HttpRequest &, CancellationToken) override {
        auto state = Json::object({{"protocol", ex::protocol},
                                   {"operation", ex::to_json(op)},
                                   {"status", "pending"},
                                   {"receipt", Json()}});
        return HttpResponse{200,
                            {{"content-type", "application/json"},
                             {"tansr-contract", "unified-v1"},
                             {"tansr-manifest-revision", "7"},
                             {"tansr-schema-hash", "sha256:" + std::string(schema_hash)},
                             {"tansr-domain", "execution"}},
                            state.dump()};
    }
    Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &, CancellationToken) override {
        return Error{ErrorCode::internal, "unused"};
    }
};
} // namespace
int main() {
    std::filesystem::path root;
    try {
        root =
            std::filesystem::temp_directory_path() /
            ("tansr-pst-cpp-" +
             take(
                 crypto::sha256_hex(
                     std::to_string(unix_time_ms()) +
                     crypto::base64_encode(std::string(
                         reinterpret_cast<const char *>(take(crypto::random_bytes(8)).data()), 8))))
                 .substr(0, 16));
        take(storage::create_private_directory(root));
        ex::Scope current{"app", "user", "1"};
        crypto::Aes256Key key{};
        key.fill(0x37);
        bool key_missing = false, inject = false, revoke_at_replace = false;
        std::size_t temporary_scans = 0;
        mp::Options options;
        options.path = root / "memory.bin";
        options.create = true;
        options.identity = Json::object(
            {{"scope", Json::object({{"applicationScopeId", "app"}, {"endUserId", "user"}})},
             {"sourceId", "source"},
             {"sourceGeneration", "1"},
             {"domainKey", "domain"}});
        options.key_id = "test-key";
        options.read_context = [&]() -> Result<ex::Scope> { return current; };
        options.read_key = [&]() -> Result<crypto::Aes256Key> {
            if (key_missing)
                return Error{ErrorCode::crypto, "key unavailable"};
            return key;
        };
        options.limits.max_transfers = 3;
        options.commit_hook = [&](storage::CommitStage stage) -> Result<void> {
            if (stage == storage::CommitStage::file_synced) {
                for (const auto &entry : std::filesystem::directory_iterator(root)) {
                    if (entry.path().filename().string().rfind(".tansr-tmp-", 0) != 0)
                        continue;
                    const auto sealed = bytes(entry.path());
                    check(!sealed.empty(), "actual temporary snapshot readable");
                    check(sealed.find("sensitive publication") == std::string::npos &&
                              sealed.find("resultJson") == std::string::npos &&
                              sealed.find(crypto::base64_encode(
                                  "sensitive publication 正文 with journal marker")) ==
                                  std::string::npos &&
                              sealed.find("\"transfers\"") == std::string::npos &&
                              sealed.find(std::string(32, static_cast<char>(0x37))) ==
                                  std::string::npos,
                          "temporary publication and journal snapshot encrypted");
                    ++temporary_scans;
                }
            }
            if (stage == storage::CommitStage::replaced && revoke_at_replace)
                current.authorization_revision = "2";
            if (stage == storage::CommitStage::replaced && inject)
                return Error{ErrorCode::io, "injected after replace"};
            return {};
        };
        auto store = take(mp::FileStore::open(options));
        const auto input_owner = owner(operation(request("head")));
        check(!mp::FileStore::open(options), "single owner lock");
        check(take(store->execute(request("head"), input_owner)).at("publication").is_null(),
              "empty head");
        const std::string body = "sensitive publication 正文 with journal marker";
        auto start = begin_request(body);
        check(status(take(store->execute(start, input_owner))) == "staging", "begin");
        check(status(take(store->execute(start, input_owner))) == "staging", "begin duplicate");
        auto bad = start;
        bad.set("sha256", std::string(64, 'f'));
        check(!store->execute(bad, input_owner), "begin conflict");
        auto other = input_owner;
        other.at("binding").set("revision", "2");
        check(!store->execute(request("query"), other), "owner fence query");
        check(!store->execute(chunk(body), other), "owner fence write");
        auto old_scope = input_owner;
        old_scope.at("scope").set("authorizationRevision", "2");
        check(!store->execute(request("head"), old_scope), "current revision fence");
        bad = request("head");
        bad.set("sourceGeneration", "2");
        check(!store->execute(bad, input_owner), "source fence");
        bad = chunk(body);
        bad.set("offset", 1);
        check(!store->execute(bad, input_owner), "gap rejected");
        bad = chunk(body);
        bad.set("payloadDigest", std::string(64, '0'));
        check(!store->execute(bad, input_owner), "digest rejected");
        take(store->execute(chunk(body), input_owner));
        take(store->execute(chunk(body), input_owner));
        check(status(take(store->execute(request("commit"), input_owner))) == "committed",
              "commit");
        check(status(take(store->execute(request("commit"), input_owner))) == "committed",
              "commit duplicate");
        auto head = take(store->execute(request("head"), input_owner)).at("publication");
        auto read = request("read");
        read.set("etag", head.at("etag"));
        read.set("offset", 0);
        read.set("length", 12288);
        auto result = take(store->execute(read, input_owner));
        check(result.at("base64").as_string() == crypto::base64_encode(body), "read exact bytes");
        auto authorized = [](const ex::Operation &, CancellationToken) -> Result<void> {
            return {};
        };
        auto host = take(mp::create_host({store, store, authorized, true}));
        auto op = operation(read);
        take(ex::validate_operation(op));
        check(true, "reserved profile validates");
        auto forged = op;
        forged.tool_name = mp::tool_name;
        forged.digest = take(ex::operation_digest(forged));
        check(!ex::validate_operation(forged), "reserved profile cannot use ordinary permission");
        auto denied = host.tools.at(mp::tool_name).handler(ex::ToolContext{{}, {}}, read);
        check(std::holds_alternative<ex::ToolFailure>(denied), "missing original operation denied");
        ClientOptions client_options;
        client_options.base_url = "https://serve.example.test";
        client_options.token_provider = [](CancellationToken) -> Result<AuthToken> {
            return AuthToken{"test", "app/user"};
        };
        auto fixture = std::make_shared<Fixture>(op);
        auto api = take(ApiClient::create(client_options, fixture));
        ex::RunnerOptions runner_options;
        runner_options.client = take(ex::Client::create(api, current));
        runner_options.authorize = authorized;
        runner_options.registration.executor_id = "device";
        runner_options.registration.workspaces = {{"workspace", "1"}};
        runner_options.registration.operations = {"tool.invoke"};
        runner_options.registration.tools = {{mp::tool_name, mp::tool_digest}};
        runner_options.tools = host.tools;
        runner_options.journal = host.journal;
        auto runner =
            take(ex::Runner::create(runner_options, ex::Connection{"device", "connection", "1",
                                                                   "2099-01-01T00:00:00Z", 1000}));
        auto receipt = take(runner->execute(op));
        check(receipt.status == "completed", "public Runner executes reserved host");
        check(take(runner->execute(op)).digest == receipt.digest, "Runner journal replay");
        check(bytes(options.path).find(body) == std::string::npos &&
                  bytes(options.path).find(crypto::base64_encode(body)) == std::string::npos &&
                  bytes(options.path).find("resultJson") == std::string::npos,
              "body and journal encrypted");
        take(store->close());
        check(!store->capacity(), "closed refused");
        options.create = false;
        store = take(mp::FileStore::open(options));
        check(take(store->claim(op)).state == ex::ClaimState::receipt, "journal survives reopen");
        check(status(take(store->execute(request("query"), input_owner))) == "committed",
              "transfer survives reopen");
        take(store->execute(begin_request("second", "second"), input_owner));
        take(store->execute(chunk("second", "second"), input_owner));
        check(status(take(store->execute(request("commit", "second"), input_owner))) == "conflict",
              "CAS conflict persisted");
        check(take(store->execute(read, input_owner)).at("base64").as_string() ==
                  crypto::base64_encode(body),
              "CAS keeps old body");
        take(store->execute(begin_request("third", "third", head.at("etag")), input_owner));
        take(store->execute(chunk("third", "third"), input_owner));
        inject = true;
        check(!store->execute(request("commit", "third"), input_owner),
              "lost commit response error");
        check(!store->capacity(), "uncertain instance rejected");
        take(store->close());
        inject = false;
        store = take(mp::FileStore::open(options));
        check(status(take(store->execute(request("query", "third"), input_owner))) == "committed",
              "lost commit query recovered");
        check(!store->execute(begin_request("fourth", "fourth"), input_owner),
              "retained receipt capacity");
        check(status(take(store->execute(request("query", "unknown"), input_owner))) == "unknown",
              "unknown unchanged");
        check(take(store->capacity()).at("remainingTransfers").as_u64() == 0, "capacity observed");
        key_missing = true;
        check(!store->execute(request("head"), input_owner), "missing key fail closed");
        key_missing = false;
        take(store->close());
        check(temporary_scans >= 5, "temporary snapshots actually inspected");
        auto original = bytes(options.path);
        key[0] ^= 1;
        check(!mp::FileStore::open(options), "wrong key rejected");
        key[0] ^= 1;
        check(bytes(options.path) == original, "wrong key preserves original");
        const auto original_identity = options.identity;
        options.identity.set("domainKey", "foreign-domain");
        check(!mp::FileStore::open(options),
              "wrong AAD rejected before publication/journal delivery");
        check(bytes(options.path) == original, "wrong AAD preserves combined publication/journal");
        options.identity = original_identity;
        for (const auto length : {std::size_t(0), std::size_t(12), original.size() - 1}) {
            const auto shortened = original.substr(0, length);
            {
                std::ofstream file(options.path, std::ios::binary | std::ios::trunc);
                file.write(shortened.data(), static_cast<std::streamsize>(shortened.size()));
            }
            check(!mp::FileStore::open(options), "truncated combined publication/journal rejected");
            check(bytes(options.path) == shortened, "truncation evidence preserved");
        }
        {
            std::ofstream file(options.path, std::ios::binary | std::ios::trunc);
            file.write(original.data(), static_cast<std::streamsize>(original.size()));
        }
        options.authorize_recovery = [](const Json &, const Json &,
                                        std::string_view) -> Result<void> { return {}; };
        store = take(mp::FileStore::open(options));
        check(status(take(store->execute(request("query"), other))) == "committed",
              "explicit bounded recovery query");
        auto foreign = other;
        foreign.set("sessionId", "foreign");
        check(!store->execute(request("query"), foreign), "callback cannot widen recovery session");
        check(!store->execute(request("commit"), other), "recovery never authorizes write");
        take(store->close());
        options.path = root / "missing";
        check(!mp::FileStore::open(options), "reopen never creates empty domain");
        options.path = root / "revocation";
        options.create = true;
        store = take(mp::FileStore::open(options));
        revoke_at_replace = true;
        auto revoked = store->execute(begin_request("test"), input_owner);
        check(!revoked && revoked.error().code == ErrorCode::unknown,
              "post-commit revoke remains unknown");
        take(store->close());
        revoke_at_replace = false;
        current.authorization_revision = "1";
        options.create = false;
        store = take(mp::FileStore::open(options));
        check(status(take(store->execute(request("query"), input_owner))) == "staging",
              "revoked commit original survives");
        // 暂存上界、原 UTF-8、journal 容量和回调重入均不能破坏已保存事实。
        take(store->close());
        options.path = root / "limits";
        options.create = true;
        options.limits.max_staging_bytes = 4;
        options.limits.max_journal_entries = 1;
        store = take(mp::FileStore::open(options));
        check(!store->execute(begin_request("12345"), input_owner), "staging reservation bound");
        const std::string invalid_utf8(1, static_cast<char>(0xff));
        take(store->execute(begin_request(invalid_utf8), input_owner));
        take(store->execute(chunk(invalid_utf8), input_owner));
        check(!store->execute(request("commit"), input_owner), "invalid UTF8 commit rejected");
        check(take(store->execute(request("head"), input_owner)).at("publication").is_null(),
              "invalid UTF8 leaves head empty");
        auto claim_op = operation(request("head"), "only-claim");
        check(take(store->claim(claim_op)).state == ex::ClaimState::claimed,
              "journal claim durable");
        check(!store->claim(operation(request("head"), "over-capacity")), "journal capacity");
        take(store->close());
        options.create = false;
        store = take(mp::FileStore::open(options));
        check(take(store->claim(claim_op)).state == ex::ClaimState::pending,
              "only claim remains pending");
        take(store->close());
        bool reenter = false;
        auto original_provider = options.read_context;
        options.read_context = [&]() -> Result<ex::Scope> {
            if (reenter) {
                auto ignored = store->capacity();
                (void)ignored;
            }
            return current;
        };
        store = take(mp::FileStore::open(options));
        reenter = true;
        check(!store->execute(request("head"), input_owner), "swallowed reentry fails outer call");
        reenter = false;
        take(store->close());
        options.read_context = original_provider;
        auto cipher = bytes(options.path);
        {
            std::fstream file(options.path, std::ios::in | std::ios::out | std::ios::binary);
            file.seekp(-1, std::ios::end);
            const char changed = static_cast<char>(cipher.back() ^ 1);
            file.write(&changed, 1);
        }
        const auto corrupted = bytes(options.path);
        check(!mp::FileStore::open(options), "AEAD corruption rejects reopen");
        check(bytes(options.path) == corrupted, "AEAD corruption preserves evidence");
        std::filesystem::remove_all(root);
        std::cout << checks << " publication checks passed; temp root removed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "failed after " << checks << " checks: " << e.what() << "\ntemp: " << root
                  << '\n';
        return 1;
    }
}