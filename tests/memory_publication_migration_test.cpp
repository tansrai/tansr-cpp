#include "tansr/canonical.hpp"
#include "tansr/memory_publication.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace tansr;
namespace mp = tansr::memory_publication;
namespace ex = tansr::executor;
namespace {
constexpr const char *magic = "Tansr-Cpp-MemoryPublication/1\n";
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
void take(Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
std::string bytes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void put(const std::filesystem::path &path, const std::string &data) {
    auto directory = take(
        storage::PrivateDirectory::open(path.parent_path(), []() -> Result<void> { return {}; }));
    take(directory->write_atomic(path.filename().u8string(), data, false));
}
Json request(const char *action, const char *id = "published") {
    auto value = Json::object({{"contract", "terminal-services-v1"},
                               {"sourceId", "source"},
                               {"sourceGeneration", "1"},
                               {"domainKey", "domain"},
                               {"action", action}});
    if (std::string(action) != "head" && std::string(action) != "read")
        value.set("transferId", id);
    return value;
}
Json begin(std::string_view body, const char *id) {
    auto value = request("begin", id);
    value.set("expectedEtag", Json());
    value.set("byteLength", Json(static_cast<std::uint64_t>(body.size())));
    value.set("sha256", take(crypto::sha256_hex(body)));
    return value;
}
Json chunk(std::string_view body, const char *id) {
    auto value = request("chunk", id);
    value.set("offset", 0);
    value.set("byteLength", Json(static_cast<std::uint64_t>(body.size())));
    value.set("base64", crypto::base64_encode(body));
    value.set("payloadDigest", take(crypto::sha256_hex(body)));
    return value;
}
ex::Operation operation(const char *id) {
    ex::Operation op{id,
                     "session",
                     {"app", "user", "1"},
                     {"binding", "1", {"device", "connection", "1", "workspace", "1", {}}},
                     "MemoryPublication",
                     {"tool.invoke", Json::object({{"name", mp::tool_name},
                                                   {"definitionDigest", mp::tool_digest},
                                                   {"argsJson", request("head").dump()}})},
                     "",
                     "2099-01-01T00:00:00Z"};
    op.digest = take(ex::operation_digest(op));
    return op;
}
Json owner() {
    const auto value = ex::to_json(operation("owner"));
    return Json::object({{"scope", value.at("scope")},
                         {"sessionId", value.at("sessionId")},
                         {"binding", value.at("binding")}});
}
Json plaintext(const mp::Options &options) {
    const auto blob = bytes(options.path);
    const auto prefix = std::strlen(magic);
    check(blob.size() >= prefix + 28 && blob.substr(0, prefix) == magic, "versioned ciphertext");
    crypto::GcmNonce nonce{};
    crypto::GcmTag tag{};
    std::memcpy(nonce.data(), blob.data() + prefix, nonce.size());
    std::memcpy(tag.data(), blob.data() + blob.size() - tag.size(), tag.size());
    const auto plain = take(crypto::aes256_gcm_decrypt(
        take(options.read_key()), nonce,
        std::string_view(blob).substr(prefix + nonce.size(),
                                      blob.size() - prefix - nonce.size() - tag.size()),
        tag,
        std::string(magic) + take(canonical::encode(options.identity)) + "\n" + options.key_id));
    return take(
        Json::parse(std::string(reinterpret_cast<const char *>(plain.data()), plain.size())));
}
// 仅夹具构造已达到原加密次数上界的完整受保护快照。
void fixture_snapshot(const mp::Options &options, const Json &state) {
    auto random = take(crypto::random_bytes(12));
    crypto::GcmNonce nonce{};
    std::copy(random.begin(), random.end(), nonce.begin());
    const auto cipher = take(crypto::aes256_gcm_encrypt(
        take(options.read_key()), nonce, state.dump(),
        std::string(magic) + take(canonical::encode(options.identity)) + "\n" + options.key_id));
    std::string blob(magic);
    blob.append(reinterpret_cast<const char *>(nonce.data()), nonce.size());
    blob.append(reinterpret_cast<const char *>(cipher.ciphertext.data()), cipher.ciphertext.size());
    blob.append(reinterpret_cast<const char *>(cipher.tag.data()), cipher.tag.size());
    put(options.path, blob);
}
void exact_snapshot(const mp::Options &target, Json expected) {
    expected.set("keyId", target.key_id);
    expected.set("writes", Json(std::uint64_t{1}));
    check(take(canonical::encode(plaintext(target))) == take(canonical::encode(expected)),
          "all original transfer/owner/operation/digest/receipt/pending facts retained");
}
void public_facts(mp::Options target) {
    target.create = false;
    auto store = take(mp::FileStore::open(target));
    for (const auto &entry :
         {std::make_pair("published", "committed"), std::make_pair("conflict", "conflict"),
          std::make_pair("staged", "staging"), std::make_pair("absent", "unknown")})
        check(take(store->execute(request("query", entry.first), owner()))
                      .at("transfer")
                      .at("status")
                      .as_string() == entry.second,
              "original query status");
    check(take(store->claim(operation("pending"))).state == ex::ClaimState::pending,
          "only-claim never reexecutes");
    for (const auto *id : {"completed", "unknown"}) {
        const auto claim = take(store->claim(operation(id)));
        check(claim.state == ex::ClaimState::receipt && claim.receipt &&
                  claim.receipt->status == id,
              "original receipt survives");
    }
    auto forged = operation("completed");
    forged.expires_at = "2099-01-02T00:00:00Z";
    forged.digest = take(ex::operation_digest(forged));
    check(!store->claim(forged), "changed original operation rejected");
    auto foreign = owner();
    foreign.at("binding").set("revision", "2");
    check(!store->execute(request("query"), foreign), "migration does not transfer owner");
    take(store->close());
}
} // namespace
int main() {
    std::filesystem::path root;
    try {
        root = std::filesystem::temp_directory_path() /
               ("tansr-migration-" +
                take(crypto::sha256_hex(
                         std::to_string(unix_time_ms()) +
                         std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()))))
                    .substr(0, 16));
        take(storage::create_private_directory(root));
        take(storage::create_private_directory(root / "source"));
        ex::Scope current{"app", "user", "1"};
        crypto::Aes256Key old_key{};
        old_key.fill(0x35);
        mp::Options source;
        source.path = root / "source" / "memory.bin";
        source.create = true;
        source.identity = Json::object(
            {{"scope", Json::object({{"applicationScopeId", "app"}, {"endUserId", "user"}})},
             {"sourceId", "source"},
             {"sourceGeneration", "1"},
             {"domainKey", "domain"}});
        source.key_id = "original-key";
        source.read_key = [old_key]() -> Result<crypto::Aes256Key> { return old_key; };
        source.read_context = [&]() -> Result<ex::Scope> { return current; };
        auto original = take(mp::FileStore::open(source));
        const std::string body = "private publication正文" + std::string(4096, 'p');
        take(original->execute(begin(body, "published"), owner()));
        take(original->execute(chunk(body, "published"), owner()));
        take(original->execute(request("commit"), owner()));
        take(original->execute(begin("conflict", "conflict"), owner()));
        take(original->execute(chunk("conflict", "conflict"), owner()));
        take(original->execute(request("commit", "conflict"), owner()));
        take(original->execute(begin("unfinished", "staged"), owner()));
        take(original->execute(chunk("un", "staged"), owner()));
        for (const auto *id : {"completed", "unknown", "pending"}) {
            const auto op = operation(id);
            take(original->claim(op));
            if (std::string(id) == "pending")
                continue;
            ex::Receipt receipt{"device", "connection", id, op.digest, id, {}, {}};
            if (std::string(id) == "completed")
                receipt.result = ex::Resource{
                    "tool.invoke",
                    Json::object({{"resultJson",
                                   R"({"status":"error","message":"private execution result"})"}})};
            if (std::string(id) == "unknown")
                receipt.error_code = "execution_outcome_unknown";
            take(original->complete(op, receipt));
        }
        auto target_for = [&](const std::string &name) {
            take(storage::create_private_directory(root / name));
            auto target = source;
            target.path = root / name / "memory.bin";
            target.create = true;
            target.key_id = "new-" + name;
            auto material = take(crypto::random_bytes(32));
            crypto::Aes256Key key{};
            std::copy(material.begin(), material.end(), key.begin());
            target.read_key = [key]() -> Result<crypto::Aes256Key> { return key; };
            return target;
        };
        source.create = false;
        auto target = target_for("success");
        check(!mp::FileStore::migrate(source, target), "live source prevents migration");
        check(!std::filesystem::exists(target.path), "live source creates no target");
        take(original->close());
        const auto original_bytes = bytes(source.path);
        const auto state = plaintext(source);
        int publications = 0;
        target.commit_hook = [&](storage::CommitStage stage) -> Result<void> {
            if (stage == storage::CommitStage::replaced) {
                ++publications;
                exact_snapshot(target, state);
            }
            return {};
        };
        const auto receipt = take(mp::FileStore::migrate(source, target));
        check(publications == 1, "first and only publication is complete snapshot");
        check(bytes(source.path) == original_bytes, "source ciphertext is unchanged");
        check(receipt.format == magic &&
                  take(canonical::encode(receipt.identity)) ==
                      take(canonical::encode(source.identity)) &&
                  receipt.transfers == 3 && receipt.journal_entries == 3 &&
                  receipt.source_sha256 == take(crypto::sha256_hex(original_bytes)) &&
                  receipt.destination_sha256 == take(crypto::sha256_hex(bytes(target.path))) &&
                  receipt.source_bytes == original_bytes.size() &&
                  receipt.destination_bytes == bytes(target.path).size(),
              "scoped checksummed migration receipt");
        check(bytes(target.path).find(body) == std::string::npos &&
                  bytes(target.path).find("private execution result") == std::string::npos,
              "target remains encrypted");
        target.commit_hook = {};
        public_facts(target);
        public_facts(source);
        // 开库后换实际钥不能访问缓存快照；必须走显式新路径轮钥。
        auto rotating = source;
        auto changing_key = old_key;
        rotating.read_key = [&]() -> Result<crypto::Aes256Key> { return changing_key; };
        auto pinned = take(mp::FileStore::open(rotating));
        changing_key[0] ^= 1;
        check(!pinned->execute(request("head"), owner()), "hot key replacement must fail closed");
        take(pinned->close());
        auto wrong_key = target;
        wrong_key.create = false;
        wrong_key.read_key = source.read_key;
        check(!mp::FileStore::open(wrong_key), "old key cannot decrypt migrated file");
        const auto target_bytes = bytes(target.path);
        check(!mp::FileStore::migrate(source, target) && bytes(target.path) == target_bytes,
              "existing target never overwritten");
        for (int kind = 0; kind < 8; ++kind) {
            auto bad_source = source, bad_target = target_for("invalid-" + std::to_string(kind));
            if (kind == 0)
                bad_source.create = true;
            if (kind == 1)
                bad_target.create = false;
            if (kind == 2)
                bad_target.key_id = source.key_id;
            if (kind == 3)
                bad_target.read_key = source.read_key;
            if (kind == 4)
                bad_target.identity.set("domainKey", "foreign");
            if (kind == 5)
                bad_target.limits.max_transfers = 2;
            if (kind == 6)
                bad_target.limits.max_file_bytes = 4096;
            if (kind == 7)
                bad_source.path = source.path.parent_path() / "missing";
            check(!mp::FileStore::migrate(bad_source, bad_target),
                  "invalid migration fails closed");
            check(!std::filesystem::exists(bad_target.path) && bytes(source.path) == original_bytes,
                  "invalid migration preserves source and creates no empty target");
        }
        auto wrong = source;
        wrong.read_key = target.read_key;
        check(!mp::FileStore::migrate(wrong, target_for("wrong-source-key")),
              "wrong source key refused");
        auto same_directory = target_for("unused");
        same_directory.path = source.path.parent_path() / "new.bin";
        check(!mp::FileStore::migrate(source, same_directory) &&
                  !std::filesystem::exists(same_directory.path),
              "migration needs independent private directory");
        int index = 0;
        for (auto stage : {storage::CommitStage::written, storage::CommitStage::file_synced,
                           storage::CommitStage::before_replace, storage::CommitStage::replaced,
                           storage::CommitStage::directory_synced}) {
            auto interrupted = target_for("fault-" + std::to_string(index++));
            interrupted.commit_hook = [stage](storage::CommitStage current_stage) -> Result<void> {
                if (current_stage == stage)
                    return Error{ErrorCode::io, "injected migration interruption"};
                return {};
            };
            const auto migrated = mp::FileStore::migrate(source, interrupted);
            check(!migrated && migrated.error().code == ErrorCode::unknown,
                  "interrupted migration never reports success");
            check(bytes(source.path) == original_bytes,
                  "interruption preserves original ciphertext");
            const bool published = stage == storage::CommitStage::replaced ||
                                   stage == storage::CommitStage::directory_synced;
            check(std::filesystem::exists(interrupted.path) == published,
                  "target absent or complete, never empty");
            if (published) {
                interrupted.commit_hook = {};
                exact_snapshot(interrupted, state);
                public_facts(interrupted);
            }
        }
        for (const bool after : {false, true}) {
            auto revoked = target_for(after ? "revoke-after" : "revoke-before");
            revoked.commit_hook = [&](storage::CommitStage stage) -> Result<void> {
                if (stage ==
                    (after ? storage::CommitStage::replaced : storage::CommitStage::before_replace))
                    current.authorization_revision = "2";
                return {};
            };
            check(!mp::FileStore::migrate(source, revoked), "revocation rejects migration result");
            current.authorization_revision = "1";
            check(bytes(source.path) == original_bytes &&
                      std::filesystem::exists(revoked.path) == after,
                  "revocation leaves original and only complete target");
            if (after) {
                revoked.commit_hook = {};
                public_facts(revoked);
            }
        }
        for (const bool format : {false, true}) {
            auto damaged = source;
            const auto folder = root / (format ? "bad-version" : "bad-tag");
            take(storage::create_private_directory(folder));
            damaged.path = folder / "memory.bin";
            auto corrupted = original_bytes;
            corrupted[format ? 0 : corrupted.size() - 1] ^= 1;
            put(damaged.path, corrupted);
            const auto destination = target_for(format ? "reject-version" : "reject-tag");
            const auto refused = mp::FileStore::migrate(damaged, destination);
            check(!refused &&
                      refused.error().code == (format ? ErrorCode::conflict : ErrorCode::crypto),
                  "unknown format or bad tag refused by format/AEAD validation");
            check(bytes(damaged.path) == corrupted && !std::filesystem::exists(destination.path),
                  "damaged original preserved without empty target");
        }
        auto exhausted = source;
        take(storage::create_private_directory(root / "exhausted"));
        exhausted.path = root / "exhausted" / "memory.bin";
        auto exhausted_state = state;
        exhausted_state.set("writes", Json(std::uint64_t{1U << 20}));
        fixture_snapshot(exhausted, exhausted_state);
        auto capped = take(mp::FileStore::open(exhausted));
        check(!capped->execute(begin("blocked", "limit"), owner()),
              "old key usage limit refuses new write");
        take(capped->close());
        const auto exhausted_bytes = bytes(exhausted.path);
        auto renewed = target_for("renewed");
        take(mp::FileStore::migrate(exhausted, renewed));
        exact_snapshot(renewed, exhausted_state);
        public_facts(renewed);
        check(bytes(exhausted.path) == exhausted_bytes,
              "rotation does not reset original key counter");
        check(bytes(source.path) == original_bytes, "all migrations leave original intact");
        std::filesystem::remove_all(root);
        std::cout << checks << " migration checks passed; temp root removed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "failed after " << checks << " checks: " << error.what() << "\ntemp: " << root
                  << '\n';
        return 1;
    }
}
