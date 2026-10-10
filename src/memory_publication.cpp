#include "tansr/memory_publication.hpp"
#include "executor/internal.hpp"
#include <cstring>
#include <mutex>
#include <type_traits>

namespace tansr::memory_publication {
namespace {
constexpr const char *magic = "Tansr-Cpp-MemoryPublication/1\n";
constexpr const char *protocol = "terminal-services-v1";
constexpr std::size_t max_body = 4U << 20;
struct Failure {
    Error error;
};
[[noreturn]] void fail(ErrorCode code, const char *message) { throw Failure{{code, message}}; }
void need(bool value, const char *message = "integrity_mismatch") {
    if (!value)
        fail(ErrorCode::conflict, message);
}
template <class T> T take(Result<T> result) {
    if (!result)
        throw Failure{result.error()};
    return std::move(result).value();
}
void take(Result<void> result) {
    if (!result)
        throw Failure{result.error()};
}
template <class F> auto protect(F &&fn) -> Result<decltype(fn())> {
    try {
        if constexpr (std::is_void_v<decltype(fn())>) {
            fn();
            return {};
        } else
            return fn();
    } catch (const Failure &e) {
        return e.error;
    } catch (const std::bad_alloc &) {
        return Error{ErrorCode::capacity, "publication allocation limit"};
    } catch (...) {
        return Error{ErrorCode::contract, "publication value rejected"};
    }
}
void wipe(void *value, std::size_t count) {
    auto *p = static_cast<volatile unsigned char *>(value);
    while (count--)
        *p++ = 0;
}
struct Key {
    crypto::Aes256Key value{};
    ~Key() { wipe(value.data(), value.size()); }
};
struct Secret {
    std::string value;
    ~Secret() {
        if (!value.empty())
            wipe(value.data(), value.size());
    }
};
std::string encode(const Json &v) { return take(canonical::encode(v)); }
bool equal(const Json &a, const Json &b) { return encode(a) == encode(b); }
std::string text(const Json &v, std::string_view key) { return v.at(key).as_string(); }
std::string hash(std::string_view bytes) { return take(crypto::sha256_hex(bytes)); }
Json number(std::size_t n) { return Json(static_cast<std::uint64_t>(n)); }
std::string decode(const Json &v) {
    auto bytes = take(crypto::base64_decode(v.as_string()));
    Secret result{std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size())};
    if (!bytes.empty())
        wipe(bytes.data(), bytes.size());
    return std::move(result.value);
}
Json limits_json(const Limits &l) {
    return Json::object({{"maxTransfers", number(l.max_transfers)},
                         {"maxStagingBytes", number(l.max_staging_bytes)},
                         {"maxJournalEntries", number(l.max_journal_entries)},
                         {"maxFileBytes", number(l.max_file_bytes)}});
}
void validate(std::string_view name, const Json &v) { take(validate_wire(protocol, name, v)); }
void owner_valid(const Json &owner) {
    need(owner.is_object() && owner.as_object().size() == 3, "invalid_request");
    take(validate_wire(executor::protocol, "Scope", owner.at("scope")));
    take(validate_wire(executor::protocol, "LegacyId", owner.at("sessionId")));
    take(validate_wire(executor::protocol, "ExecutionBinding", owner.at("binding")));
    need(encode(owner).size() <= 8192, "invalid_request");
}
Json owner_of(const executor::Operation &op) {
    const auto raw = executor::to_json(op);
    return Json::object({{"scope", raw.at("scope")},
                         {"sessionId", raw.at("sessionId")},
                         {"binding", raw.at("binding")}});
}
bool same_recovery_domain(const Json &a, const Json &b) {
    return equal(a.at("sessionId"), b.at("sessionId")) &&
           equal(a.at("scope").at("applicationScopeId"), b.at("scope").at("applicationScopeId")) &&
           equal(a.at("scope").at("endUserId"), b.at("scope").at("endUserId")) &&
           equal(a.at("binding").at("target").at("executorId"),
                 b.at("binding").at("target").at("executorId")) &&
           equal(a.at("binding").at("target").at("workspaceId"),
                 b.at("binding").at("target").at("workspaceId"));
}
std::string journal_key(const executor::Operation &op) {
    take(executor::validate_operation(op));
    return hash(encode(Json::array({op.scope.application_scope_id, op.scope.end_user_id,
                                    op.binding.target.executor_id, op.operation_id})));
}
} // namespace

struct FileStore::Impl {
    Options options;
    std::unique_ptr<storage::PrivateDirectory> directory;
    Json state;
    std::string leaf, aad, disk_hash, key_fingerprint;
    std::recursive_mutex mutex;
    bool entered{false}, poisoned{false}, uncertain{false}, closed{false}, committed{false};
    Json context;
    Key key;
    static std::unique_ptr<Impl> configure(Options options) {
        need(options.path.is_absolute() && options.read_context && options.read_key,
             "invalid_request");
        const auto &l = options.limits;
        need(l.max_transfers > 0 && l.max_transfers <= 1048576 && l.max_staging_bytes > 0 &&
                 l.max_staging_bytes <= 32U * 1024 * 1024 && l.max_journal_entries > 0 &&
                 l.max_journal_entries <= 1048576 && l.max_file_bytes >= 4096 &&
                 l.max_file_bytes <= 256U * 1024 * 1024,
             "invalid_request");
        need(options.identity.is_object() && options.identity.as_object().size() == 4 &&
                 options.identity.at("scope").is_object() &&
                 options.identity.at("scope").as_object().size() == 2,
             "invalid_request");
        Json head = options.identity;
        head.as_object().erase(std::remove_if(head.as_object().begin(), head.as_object().end(),
                                              [](const auto &p) { return p.first == "scope"; }),
                               head.as_object().end());
        head.set("contract", protocol);
        head.set("action", "head");
        validate("MemoryPublicationRequest", head);
        take(validate_wire(executor::protocol, "Id", Json(options.key_id)));
        auto impl = std::make_unique<Impl>();
        impl->options = std::move(options);
        impl->leaf = impl->options.path.filename().u8string();
        impl->aad =
            std::string(magic) + encode(impl->options.identity) + "\n" + impl->options.key_id;
        return impl;
    }
    Json current_scope() {
        auto scope = take(options.read_context());
        auto value = executor::detail::json(scope);
        take(validate_wire(executor::protocol, "Scope", value));
        need(value.at("applicationScopeId").as_string() ==
                     options.identity.at("scope").at("applicationScopeId").as_string() &&
                 value.at("endUserId").as_string() ==
                     options.identity.at("scope").at("endUserId").as_string(),
             "stale_generation");
        return value;
    }
    void check() {
        if (closed)
            fail(ErrorCode::closed, "publication closed");
        if (uncertain)
            fail(ErrorCode::unknown, "publication reconciliation required");
        if (poisoned)
            fail(ErrorCode::reentrant, "publication callback reentry");
        need(equal(current_scope(), context), "stale_generation");
        Key current;
        current.value = take(options.read_key());
        if (current.value != key.value)
            fail(ErrorCode::crypto, "publication key changed");
        need(equal(current_scope(), context), "stale_generation");
        if (poisoned)
            fail(ErrorCode::reentrant, "publication callback reentry");
    }
    void check_disk() {
        if (directory && !disk_hash.empty()) {
            auto bytes = take(directory->read(leaf, options.limits.max_file_bytes));
            need(bytes && hash(*bytes) == disk_hash);
        }
    }
    template <class F> auto run(F &&fn) -> Result<decltype(fn())> {
        return protect([&]() -> decltype(fn()) {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            if (entered) {
                poisoned = true;
                fail(ErrorCode::reentrant, "publication callback reentry");
            }
            if (closed)
                fail(ErrorCode::closed, "publication closed");
            if (uncertain)
                fail(ErrorCode::unknown, "publication reconciliation required");
            entered = true;
            poisoned = false;
            committed = false;
            struct Guard {
                Impl *self;
                ~Guard() {
                    self->entered = false;
                    wipe(self->key.value.data(), self->key.value.size());
                }
            } guard{this};
            try {
                context = current_scope();
                key.value = take(options.read_key());
                const auto fingerprint = hash(std::string_view(
                    reinterpret_cast<const char *>(key.value.data()), key.value.size()));
                if (key_fingerprint.empty())
                    key_fingerprint = fingerprint;
                else if (key_fingerprint != fingerprint)
                    fail(ErrorCode::crypto, "publication key changed; explicit migration required");
                check();
                if (directory)
                    take(directory->check_access());
                check_disk();
                if constexpr (std::is_void_v<decltype(fn())>) {
                    fn();
                    check();
                    if (directory)
                        take(directory->check_access());
                    check_disk();
                    return;
                } else {
                    auto result = fn();
                    check();
                    if (directory)
                        take(directory->check_access());
                    check_disk();
                    return result;
                }
            } catch (...) {
                if (committed) {
                    uncertain = true;
                    fail(ErrorCode::unknown, "publication reconciliation required");
                }
                throw;
            }
        });
    }
    void scope_owner(const Json &owner) {
        owner_valid(owner);
        need(equal(owner.at("scope"), context), "stale_generation");
    }
    std::size_t staging(const Json &next) {
        std::size_t count = 0;
        for (const auto &entry : next.at("transfers").as_object()) {
            const auto &row = entry.second;
            if (text(row, "status") == "staging")
                count += static_cast<std::size_t>(row.at("request").at("byteLength").as_u64());
        }
        return count;
    }
    void validate_state(const Json &next) {
        need(next.is_object() && next.as_object().size() == 8);
        need(text(next, "format") == magic && equal(next.at("identity"), options.identity) &&
             equal(next.at("limits"), limits_json(options.limits)) &&
             text(next, "keyId") == options.key_id && next.at("writes").as_u64() <= (1U << 20));
        const auto &pub = next.at("publication");
        if (!pub.is_null()) {
            need(pub.is_object() && pub.as_object().size() == 2);
            Secret body{decode(pub.at("body"))};
            need(body.value.size() <= max_body && valid_utf8(body.value) &&
                 hash(body.value) == text(pub, "etag"));
        }
        need(next.at("transfers").is_object() &&
             next.at("transfers").as_object().size() <= options.limits.max_transfers &&
             staging(next) <= options.limits.max_staging_bytes);
        for (const auto &item : next.at("transfers").as_object()) {
            const auto &row = item.second, &request = row.at("request");
            need(row.is_object() && row.as_object().size() == 6);
            validate("MemoryPublicationRequest", request);
            need(text(request, "action") == "begin" && text(request, "transferId") == item.first);
            for (const auto *field : {"sourceId", "sourceGeneration", "domainKey"})
                need(equal(request.at(field), options.identity.at(field)));
            owner_valid(row.at("owner"));
            for (const auto *field : {"applicationScopeId", "endUserId"})
                need(equal(row.at("owner").at("scope").at(field),
                           options.identity.at("scope").at(field)));
            const auto received = row.at("received").as_u64(),
                       size = request.at("byteLength").as_u64();
            need(received <= size);
            const auto status = text(row, "status");
            if (status == "staging") {
                Secret body{decode(row.at("body"))};
                need(body.value.size() == received && row.at("etag").is_null());
            } else {
                need((status == "committed" || status == "conflict") && row.at("body").is_null());
                need(status == "committed"
                         ? received == size && equal(row.at("etag"), request.at("sha256"))
                         : row.at("etag").is_null());
            }
        }
        need(next.at("journal").is_object() &&
             next.at("journal").as_object().size() <= options.limits.max_journal_entries);
        for (const auto &entry : next.at("journal").as_object()) {
            const auto &row = entry.second;
            need(row.is_object() && row.as_object().size() == 3);
            auto op = take(executor::detail::operation(row.at("operation")));
            need(journal_key(op) == entry.first && equal(row.at("owner"), owner_of(op)));
            for (const auto *field : {"applicationScopeId", "endUserId"})
                need(equal(row.at("owner").at("scope").at(field),
                           options.identity.at("scope").at(field)));
            if (!row.at("receipt").is_null())
                take(executor::validate_receipt(
                    op, take(executor::detail::receipt(row.at("receipt")))));
        }
    }
    void save(Json next, bool replace = true) {
        check();
        const auto writes = state.at("writes").as_u64();
        if (writes >= (1U << 20))
            fail(ErrorCode::capacity, "publication key rotation required");
        next.set("writes", Json(writes + 1));
        validate_state(next);
        Secret plain{next.dump()};
        if (plain.value.size() + std::strlen(magic) + 28 > options.limits.max_file_bytes)
            fail(ErrorCode::capacity, "capacity_exceeded");
        auto random = take(crypto::random_bytes(12));
        crypto::GcmNonce nonce{};
        std::copy(random.begin(), random.end(), nonce.begin());
        auto encrypted = take(crypto::aes256_gcm_encrypt(key.value, nonce, plain.value, aad));
        std::string blob(magic);
        blob.append(reinterpret_cast<const char *>(nonce.data()), nonce.size());
        blob.append(reinterpret_cast<const char *>(encrypted.ciphertext.data()),
                    encrypted.ciphertext.size());
        blob.append(reinterpret_cast<const char *>(encrypted.tag.data()), encrypted.tag.size());
        check();
        auto result = directory->write_atomic(leaf, blob, replace, options.commit_hook);
        if (!result) {
            uncertain = true;
            fail(ErrorCode::unknown, "publication commit outcome unknown");
        }
        committed = true;
        disk_hash = hash(blob);
        state = std::move(next);
        check();
    }
    Json transfer(const Json &request, const Json *row) {
        return Json::object({{"transferId", request.at("transferId")},
                             {"status", row ? row->at("status") : Json("unknown")},
                             {"receivedBytes", row ? row->at("received") : Json()},
                             {"etag", row ? row->at("etag") : Json()}});
    }
};
FileStore::FileStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FileStore::~FileStore() = default;
Result<std::shared_ptr<FileStore>> FileStore::open(Options options) {
    return protect([&] {
        auto impl = Impl::configure(std::move(options));
        const auto &l = impl->options.limits;
        auto *owner = impl.get();
        take(impl->run([&] {
            impl->directory =
                take(storage::PrivateDirectory::open(impl->options.path.parent_path(), [owner] {
                    return protect([&] { owner->check(); });
                }));
            auto bytes = take(impl->directory->read(impl->leaf, l.max_file_bytes));
            if (impl->options.create) {
                need(!bytes, "request_conflict");
                impl->state = Json::object({{"format", magic},
                                            {"identity", impl->options.identity},
                                            {"limits", limits_json(impl->options.limits)},
                                            {"keyId", impl->options.key_id},
                                            {"writes", Json(std::uint64_t{0})},
                                            {"publication", Json()},
                                            {"transfers", Json::object()},
                                            {"journal", Json::object()}});
                impl->save(impl->state, false);
            } else {
                if (!bytes)
                    fail(ErrorCode::not_found, "publication original file missing");
                const std::string_view blob = *bytes;
                const auto prefix = std::strlen(magic);
                need(blob.size() >= prefix + 28 && blob.substr(0, prefix) == magic);
                crypto::GcmNonce nonce{};
                crypto::GcmTag tag{};
                std::memcpy(nonce.data(), blob.data() + prefix, nonce.size());
                std::memcpy(tag.data(), blob.data() + blob.size() - tag.size(), tag.size());
                auto decrypted = take(crypto::aes256_gcm_decrypt(
                    impl->key.value, nonce,
                    blob.substr(prefix + nonce.size(),
                                blob.size() - prefix - nonce.size() - tag.size()),
                    tag, impl->aad));
                Secret plain{std::string(reinterpret_cast<const char *>(decrypted.data()),
                                         decrypted.size())};
                if (!decrypted.empty())
                    wipe(decrypted.data(), decrypted.size());
                impl->state =
                    take(Json::parse(plain.value, JsonLimits{l.max_file_bytes, 48, 1000000}));
                impl->validate_state(impl->state);
                impl->disk_hash = hash(*bytes);
            }
        }));
        return std::shared_ptr<FileStore>(new FileStore(std::move(impl)));
    });
}
Result<MigrationReceipt> FileStore::migrate(Options source, Options destination) {
    return protect([&] {
        need(!source.create && destination.create && equal(source.identity, destination.identity),
             "invalid_migration");
        need(source.key_id != destination.key_id, "migration requires a fresh key id");
        // 配置先验校验；源只读冷开并持锁，不创建空目标或变更源计数。
        auto target = Impl::configure(std::move(destination));
        auto original = take(FileStore::open(std::move(source)));
        auto &from = *original->impl_;
        MigrationReceipt receipt;
        try {
            take(from.run([&] {
                take(target->run([&] {
                    need(equal(from.context, target->context), "stale_generation");
                    if (from.key.value == target->key.value)
                        fail(ErrorCode::crypto, "migration requires a fresh encryption key");
                    // 同时持有两端锁；每个目标IO阶段继续核原件/授权/两把密钥。
                    target->directory = take(
                        storage::PrivateDirectory::open(target->options.path.parent_path(), [&] {
                            return protect([&] {
                                from.check();
                                from.check_disk();
                                target->check();
                            });
                        }));
                    need(!take(target->directory->read(target->leaf,
                                                       target->options.limits.max_file_bytes)),
                         "request_conflict");
                    target->state = from.state;
                    target->state.set("keyId", target->options.key_id);
                    target->state.set("limits", limits_json(target->options.limits));
                    target->state.set("writes", Json(std::uint64_t{0}));
                    target->validate_state(target->state);
                    // 不调用 open(create)：第一次可见目标已经包含完整 publication、
                    // transfer、原 operation/receipt 与 pending，不出现已创建的空库。
                    target->save(target->state, false);
                    receipt.format = magic;
                    receipt.identity = from.options.identity;
                    receipt.source_sha256 = from.disk_hash;
                    receipt.destination_sha256 = target->disk_hash;
                    const auto old_bytes =
                        take(from.directory->read(from.leaf, from.options.limits.max_file_bytes));
                    const auto new_bytes = take(target->directory->read(
                        target->leaf, target->options.limits.max_file_bytes));
                    need(old_bytes && hash(*old_bytes) == from.disk_hash && new_bytes &&
                         hash(*new_bytes) == target->disk_hash);
                    receipt.source_bytes = old_bytes->size();
                    receipt.destination_bytes = new_bytes->size();
                    receipt.transfers = target->state.at("transfers").as_object().size();
                    receipt.journal_entries = target->state.at("journal").as_object().size();
                }));
            }));
        } catch (...) {
            if (target->committed || target->uncertain)
                fail(ErrorCode::unknown, "migration outcome unknown; retain both paths and keys");
            throw;
        }
        return receipt;
    });
}
const Json &FileStore::identity() const noexcept { return impl_->options.identity; }
Result<Json> FileStore::execute(const Json &input, const Json &owner) {
    return impl_->run([&] {
        const Json request = input;
        validate("MemoryPublicationRequest", request);
        impl_->scope_owner(owner);
        for (const auto *field : {"sourceId", "sourceGeneration", "domainKey"})
            need(equal(request.at(field), identity().at(field)), "stale_generation");
        const auto action = text(request, "action");
        Json response = Json::object({{"contract", protocol},
                                      {"action", action},
                                      {"sourceId", request.at("sourceId")},
                                      {"sourceGeneration", request.at("sourceGeneration")},
                                      {"domainKey", request.at("domainKey")}});
        const auto &publication = impl_->state.at("publication");
        if (action == "head") {
            if (publication.is_null())
                response.set("publication", Json());
            else {
                Secret body{decode(publication.at("body"))};
                response.set("publication", Json::object({{"etag", publication.at("etag")},
                                                          {"byteLength", number(body.value.size())},
                                                          {"sha256", publication.at("etag")}}));
            }
        } else if (action == "read") {
            need(!publication.is_null() && equal(publication.at("etag"), request.at("etag")),
                 "revision_conflict");
            Secret body{decode(publication.at("body"))};
            const auto offset = static_cast<std::size_t>(request.at("offset").as_u64());
            need(offset <= body.value.size(), "invalid_request");
            Secret part{
                body.value.substr(offset, static_cast<std::size_t>(request.at("length").as_u64()))};
            response.set("etag", publication.at("etag"));
            response.set("offset", number(offset));
            response.set("byteLength", number(part.value.size()));
            response.set("base64", crypto::base64_encode(part.value));
            response.set("payloadDigest", hash(part.value));
            response.set("nextOffset", number(offset + part.value.size()));
            response.set("complete", offset + part.value.size() == body.value.size());
        } else {
            const auto id = text(request, "transferId");
            const auto *old = impl_->state.at("transfers").find(id);
            if (old && !equal(old->at("owner"), owner)) {
                need(action == "query" && impl_->options.authorize_recovery &&
                         same_recovery_domain(old->at("owner"), owner),
                     "request_conflict");
                take(impl_->options.authorize_recovery(old->at("owner"), owner, id));
                impl_->check();
            }
            if (action == "begin") {
                if (old)
                    need(equal(old->at("request"), request), "request_conflict");
                else {
                    if (impl_->state.at("transfers").as_object().size() >=
                            impl_->options.limits.max_transfers ||
                        request.at("byteLength").as_u64() >
                            impl_->options.limits.max_staging_bytes - impl_->staging(impl_->state))
                        fail(ErrorCode::capacity, "capacity_exceeded");
                    auto next = impl_->state;
                    next.at("transfers")
                        .set(id, Json::object({{"request", request},
                                               {"owner", owner},
                                               {"status", "staging"},
                                               {"received", 0},
                                               {"body", ""},
                                               {"etag", Json()}}));
                    impl_->save(std::move(next));
                }
            } else if (action == "chunk" && old) {
                Secret part{decode(request.at("base64"))};
                need(part.value.size() == request.at("byteLength").as_u64() &&
                     part.value.size() <= 12288 &&
                     hash(part.value) == text(request, "payloadDigest"));
                need(text(*old, "status") == "staging", "request_conflict");
                const auto offset = static_cast<std::size_t>(request.at("offset").as_u64());
                const auto received = static_cast<std::size_t>(old->at("received").as_u64());
                const auto size =
                    static_cast<std::size_t>(old->at("request").at("byteLength").as_u64());
                need(offset <= size && part.value.size() <= size - offset, "request_conflict");
                Secret body{decode(old->at("body"))};
                if (offset < received)
                    need(offset + part.value.size() <= received &&
                             body.value.substr(offset, part.value.size()) == part.value,
                         "request_conflict");
                else {
                    need(offset == received, "request_conflict");
                    body.value += part.value;
                    auto next = impl_->state;
                    auto &row = next.at("transfers").at(id);
                    row.set("received", number(body.value.size()));
                    row.set("body", crypto::base64_encode(body.value));
                    impl_->save(std::move(next));
                }
            } else if (action == "commit" && old && text(*old, "status") == "staging") {
                Secret body{decode(old->at("body"))};
                need(body.value.size() == old->at("request").at("byteLength").as_u64() &&
                     hash(body.value) == text(old->at("request"), "sha256") &&
                     valid_utf8(body.value));
                auto next = impl_->state;
                auto &row = next.at("transfers").at(id);
                const auto expected = row.at("request").at("expectedEtag");
                if (!equal(publication.is_null() ? Json() : publication.at("etag"), expected))
                    row.set("status", "conflict");
                else {
                    row.set("status", "committed");
                    row.set("etag", row.at("request").at("sha256"));
                    next.set("publication",
                             Json::object({{"etag", row.at("etag")}, {"body", row.at("body")}}));
                }
                row.set("body", Json());
                impl_->save(std::move(next));
            }
            response.set("transfer",
                         impl_->transfer(request, impl_->state.at("transfers").find(id)));
        }
        validate("MemoryPublicationResponse", response);
        return response;
    });
}
Result<Json> FileStore::capacity() {
    return impl_->run([&] {
        const auto &l = impl_->options.limits;
        const auto transfers = impl_->state.at("transfers").as_object().size();
        const auto journals = impl_->state.at("journal").as_object().size();
        const auto staged = impl_->staging(impl_->state);
        auto result = limits_json(l);
        result.set("storedTransfers", number(transfers));
        result.set("remainingTransfers", number(l.max_transfers - transfers));
        result.set("stagingBytes", number(staged));
        result.set("remainingStagingBytes", number(l.max_staging_bytes - staged));
        result.set("journalEntries", number(journals));
        result.set("remainingJournalEntries", number(l.max_journal_entries - journals));
        result.set("encodedStateBytes", number(impl_->state.dump().size()));
        return result;
    });
}
Result<executor::ClaimResult> FileStore::claim(const executor::Operation &op) {
    return impl_->run([&] {
        impl_->scope_owner(owner_of(op));
        const auto id = journal_key(op);
        const auto *old = impl_->state.at("journal").find(id);
        if (!old) {
            if (impl_->state.at("journal").as_object().size() >=
                impl_->options.limits.max_journal_entries)
                fail(ErrorCode::capacity, "capacity_exceeded");
            auto next = impl_->state;
            next.at("journal").set(id, Json::object({{"operation", executor::to_json(op)},
                                                     {"owner", owner_of(op)},
                                                     {"receipt", Json()}}));
            impl_->save(std::move(next));
            return executor::ClaimResult{executor::ClaimState::claimed, {}};
        }
        need(equal(old->at("operation"), executor::to_json(op)), "request_conflict");
        if (old->at("receipt").is_null())
            return executor::ClaimResult{executor::ClaimState::pending, {}};
        auto receipt = take(executor::detail::receipt(old->at("receipt")));
        take(executor::validate_receipt(op, receipt));
        return executor::ClaimResult{executor::ClaimState::receipt, std::move(receipt)};
    });
}
Result<void> FileStore::complete(const executor::Operation &op, const executor::Receipt &receipt) {
    return impl_->run([&] {
        impl_->scope_owner(owner_of(op));
        const auto id = journal_key(op);
        take(executor::validate_receipt(op, receipt));
        const auto *old = impl_->state.at("journal").find(id);
        need(old && equal(old->at("operation"), executor::to_json(op)), "request_conflict");
        if (!old->at("receipt").is_null()) {
            need(equal(old->at("receipt"), executor::to_json(receipt)), "request_conflict");
            return;
        }
        auto next = impl_->state;
        next.at("journal").at(id).set("receipt", executor::to_json(receipt));
        impl_->save(std::move(next));
    });
}
Result<void> FileStore::close() {
    return protect([&] {
        std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
        if (impl_->entered) {
            impl_->poisoned = true;
            fail(ErrorCode::reentrant, "publication callback reentry");
        }
        impl_->closed = true;
        impl_->directory.reset();
        impl_->state = Json();
    });
}
Result<Host> create_host(HostOptions options) {
    return protect([&] {
        need(options.store && options.journal && options.authorize &&
                 options.store->atomic_durable_publication(),
             "invalid_request");
        need(!options.require_encryption ||
                 (options.store->encrypted_at_rest() && options.journal->encrypted_at_rest()),
             "invalid_request");
        const Json identity = options.store->identity();
        Host host;
        host.journal = options.journal;
        host.tools.emplace(
            tool_name,
            executor::Tool{
                tool_digest,
                [options, identity](executor::ToolContext context,
                                    Json request) -> executor::ToolResult {
                    auto result = protect([&] {
                        if (!context.operation || context.cancellation.is_cancelled())
                            fail(ErrorCode::permission, "publication operation required");
                        need(equal(options.store->identity(), identity), "stale_generation");
                        const auto &op = *context.operation;
                        take(executor::validate_operation(op));
                        need(op.tool_name == "MemoryPublication" &&
                                 op.request.operation == "tool.invoke" &&
                                 text(op.request.args, "name") == tool_name &&
                                 text(op.request.args, "definitionDigest") == tool_digest,
                             "invalid_request");
                        need(equal(take(executor::parse_tool_arguments(
                                       text(op.request.args, "argsJson"))),
                                   request),
                             "invalid_request");
                        take(options.authorize(op, context.cancellation));
                        if (context.cancellation.is_cancelled())
                            fail(ErrorCode::cancelled, "publication cancelled");
                        const auto owner = owner_of(op);
                        for (const auto *field : {"applicationScopeId", "endUserId"})
                            need(equal(owner.at("scope").at(field),
                                       options.store->identity().at("scope").at(field)),
                                 "stale_generation");
                        for (const auto *field : {"sourceId", "sourceGeneration", "domainKey"})
                            need(equal(request.at(field), options.store->identity().at(field)),
                                 "stale_generation");
                        auto response = take(options.store->execute(request, owner));
                        validate("MemoryPublicationResponse", response);
                        if (!equal(options.store->identity(), identity))
                            fail(ErrorCode::unknown, "publication identity changed");
                        if (!options.authorize(op, context.cancellation))
                            fail(ErrorCode::unknown, "publication result withheld");
                        if (context.cancellation.is_cancelled())
                            fail(ErrorCode::unknown, "publication result withheld");
                        return Json::object(
                            {{"status", "ok"},
                             {"content", Json::array({Json::object(
                                             {{"t", "text"}, {"text", response.dump()}})})}});
                    });
                    if (result)
                        return std::move(result.value());
                    // 介质/授权变化或提交失回保 unknown；不把未知副作用伪报成未执行。
                    if (result.error().code == ErrorCode::conflict ||
                        result.error().code == ErrorCode::capacity)
                        return Json::object(
                            {{"status", "error"}, {"message", result.error().message}});
                    return executor::ToolFailure::unknown("memory_publication_outcome_unknown");
                }});
        return host;
    });
}
} // namespace tansr::memory_publication