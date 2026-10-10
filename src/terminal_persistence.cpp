#include "tansr/terminal_persistence.hpp"
#include "executor/internal.hpp"
#include <cstring>
#include <mutex>
#include <openssl/hmac.h>
#include <type_traits>

namespace tansr::terminal_persistence {
namespace {
constexpr const char *magic = "Tansr-Cpp-TerminalPersistence/1\n";
constexpr const char *protocol = "terminal-persistence-v1";
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
std::string text(const Json &v, std::string_view data_key) { return v.at(data_key).as_string(); }
std::string hash(std::string_view bytes) { return take(crypto::sha256_hex(bytes)); }
Json number(std::size_t n) { return Json(static_cast<std::uint64_t>(n)); }
std::string decode(const Json &v) {
    auto bytes = take(crypto::base64_decode(v.as_string()));
    Secret result{std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size())};
    if (!bytes.empty())
        wipe(bytes.data(), bytes.size());
    return std::move(result.value);
}
std::string budget_mac(const crypto::Aes256Key &key, std::string_view input) {
    unsigned char digest[32]{};
    unsigned int length = 0;
    if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
              reinterpret_cast<const unsigned char *>(input.data()), input.size(), digest,
              &length) ||
        length != 32)
        fail(ErrorCode::crypto, "persistence budget authentication failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (auto byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    wipe(digest, sizeof(digest));
    return result;
}
std::string new_budget_id() {
    const auto random = take(crypto::random_bytes(32));
    return hash(std::string_view(reinterpret_cast<const char *>(random.data()), random.size()));
}
Json limits_json(const Limits &l) {
    return Json::object({{"activeTransfers", number(l.active_transfers)},
                         {"stagingBytes", number(l.staging_bytes)},
                         {"receiptEntries", number(l.receipt_entries)},
                         {"transferFacts", number(l.transfer_facts)},
                         {"objects", number(l.objects)},
                         {"retainedBytes", number(l.retained_bytes)}});
}

void validate(std::string_view name, const Json &v) {
    need(static_cast<bool>(validate_wire(protocol, name, v)), "invalid_request");
}
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
    std::string leaf, aad, disk_hash, key_fingerprint, budget_leaf, budget_hash;
    std::uint64_t budget_writes{0};
    std::recursive_mutex mutex;
    bool entered{false}, poisoned{false}, uncertain{false}, closed{false}, committed{false};
    Json context;
    Key data_key;
    Guard operation_guard;
    static std::unique_ptr<Impl> configure(Options options) {
        need(options.path.is_absolute() && options.read_context && options.read_key,
             "invalid_request");
        const auto &l = options.limits;
        need(l.active_transfers > 0 && l.active_transfers <= 32 && l.staging_bytes > 0 &&
                 l.staging_bytes <= (64U << 20) && l.receipt_entries > 0 &&
                 l.receipt_entries <= 1048576 && l.transfer_facts > 0 &&
                 l.transfer_facts <= 262144 && l.objects > 0 && l.objects <= 1048576 &&
                 l.retained_bytes >= 262144 && l.retained_bytes <= (1U << 30) &&
                 l.max_journal_entries > 0 && l.max_journal_entries <= 1048576 &&
                 l.max_file_bytes >= 4096 && l.max_file_bytes <= (256U << 20),
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
        validate("Request", head);
        take(validate_wire(executor::protocol, "Id", Json(options.key_id)));
        auto impl = std::make_unique<Impl>();
        impl->options = std::move(options);
        impl->leaf = impl->options.path.filename().u8string();
        impl->budget_leaf = impl->leaf + ".writes";
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
        if (operation_guard)
            take(operation_guard());
        need(equal(current_scope(), context), "stale_generation");
        Key current;
        current.value = take(options.read_key());
        if (current.value != data_key.value)
            fail(ErrorCode::crypto, "persistence key changed");
        need(equal(current_scope(), context), "stale_generation");
        if (poisoned)
            fail(ErrorCode::reentrant, "publication callback reentry");
    }
    void check_disk() {
        if (directory && !budget_hash.empty()) {
            auto bytes = take(directory->read(budget_leaf, 4096));
            need(bytes && hash(*bytes) == budget_hash);
        }
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
                    self->operation_guard = {};
                    wipe(self->data_key.value.data(), self->data_key.value.size());
                }
            } guard{this};
            try {
                context = current_scope();
                data_key.value = take(options.read_key());
                const auto fingerprint = hash(std::string_view(
                    reinterpret_cast<const char *>(data_key.value.data()), data_key.value.size()));
                if (key_fingerprint.empty())
                    key_fingerprint = fingerprint;
                else if (key_fingerprint != fingerprint)
                    fail(ErrorCode::crypto, "persistence key changed; explicit migration required");
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
    Json capacity_of(const Json &next);
    void validate_state(const Json &next);
    Json dispatch(Json &next, const Json &request, const Json &owner, bool &changed);
    std::uint64_t write_reserve(const Json &next);
    void physical_capacity(const Json &next);
    Json budget_record(std::uint64_t writes) {
        return Json::object({{"version", 1},
                             {"storeId", state.at("budgetId")},
                             {"identitySha256", hash(encode(options.identity))},
                             {"keyId", options.key_id},
                             {"writes", Json(writes)}});
    }
    void read_budget() {
        auto bytes = take(directory->read(budget_leaf, 4096));
        need(bytes.has_value(), "persistence original budget missing");
        const auto value = take(Json::parse(*bytes, JsonLimits{4096, 8, 64}));
        need(value.is_object() && value.as_object().size() == 6);
        budget_writes = value.at("writes").as_u64();
        need(budget_writes < (1U << 20) && budget_writes >= state.at("writes").as_u64());
        auto body = budget_record(budget_writes);
        body.set("tag", budget_mac(data_key.value, encode(body)));
        need(equal(value, body), "persistence budget authentication failed");
        budget_hash = hash(*bytes);
    }
    void burn_budget(std::uint64_t writes) {
        // 先耐久保留每一次 GCM 尝试；副本写入失败时绝不能开始正文加密。
        auto value = budget_record(writes);
        value.set("tag", budget_mac(data_key.value, encode(value)));
        const auto bytes = encode(value);
        check();
        auto saved = directory->write_atomic(budget_leaf, bytes, !budget_hash.empty());
        if (!saved) {
            uncertain = true;
            fail(ErrorCode::unknown, "persistence budget outcome unknown");
        }
        committed = true;
        budget_writes = writes;
        budget_hash = hash(bytes);
        check();
    }
    bool readonly_copy() const { return state.find("readOnlyCopy") != nullptr; }
    void save(Json next, bool replace = true) {
        check();
        if (replace && readonly_copy())
            fail(ErrorCode::permission, "persistence copy is read-only; cutover proof unavailable");
        const auto writes = std::max(state.at("writes").as_u64(), budget_writes);
        if (writes >= (1U << 20))
            fail(ErrorCode::capacity, "persistence key rotation required");
        next.set("writes", Json(writes + 1));
        validate_state(next);
        physical_capacity(next);
        Secret plain{next.dump()};
        if (plain.value.size() + std::strlen(magic) + 28 > options.limits.max_file_bytes)
            fail(ErrorCode::capacity, "capacity_exceeded");
        burn_budget(writes + 1);
        auto random = take(crypto::random_bytes(12));
        crypto::GcmNonce nonce{};
        std::copy(random.begin(), random.end(), nonce.begin());
        auto encrypted = take(crypto::aes256_gcm_encrypt(data_key.value, nonce, plain.value, aad));
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
};
#include "terminal_persistence_state.inc"
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
                need(!bytes && !take(impl->directory->read(impl->budget_leaf, 4096)),
                     "request_conflict");
                impl->state = Json::object(
                    {{"format", magic},
                     {"identity", impl->options.identity},
                     {"limits", limits_json(impl->options.limits)},
                     {"physicalLimits",
                      Json::object({{"fileBytes", number(l.max_file_bytes)},
                                    {"journalEntries", number(l.max_journal_entries)}})},
                     {"keyId", impl->options.key_id},
                     {"writes", Json(std::uint64_t{0})},
                     {"budgetId", new_budget_id()},
                     {"root", Json()},
                     {"objects", Json::object()},
                     {"entries", Json::array()},
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
                    impl->data_key.value, nonce,
                    blob.substr(prefix + nonce.size(),
                                blob.size() - prefix - nonce.size() - tag.size()),
                    tag, impl->aad));
                Secret plain{std::string(reinterpret_cast<const char *>(decrypted.data()),
                                         decrypted.size())};
                if (!decrypted.empty())
                    wipe(decrypted.data(), decrypted.size());
                impl->state =
                    take(Json::parse(plain.value, JsonLimits{l.max_file_bytes, 48, 1000000}));
                impl->read_budget();
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
                    if (from.data_key.value == target->data_key.value)
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
                    need(!take(target->directory->read(target->budget_leaf, 4096)),
                         "request_conflict");
                    target->state = from.state;
                    // 无共同源围栏时仅发布可核验副本；标记与全部事实一起认证持久化。
                    target->state.set("readOnlyCopy", true);
                    target->state.set("budgetId", new_budget_id());
                    target->state.set("keyId", target->options.key_id);
                    target->state.set("limits", limits_json(target->options.limits));
                    target->state.set(
                        "physicalLimits",
                        Json::object({{"fileBytes", number(target->options.limits.max_file_bytes)},
                                      {"journalEntries",
                                       number(target->options.limits.max_journal_entries)}}));
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
Result<Json> FileStore::execute(const Json &request, const Json &owner, Guard guard) {
    return impl_->run([&] {
        impl_->operation_guard = std::move(guard);
        impl_->check();
        impl_->scope_owner(owner);
        validate("Request", request);
        need(request.dump().size() <= 32768, "invalid_request");
        for (const auto *field : {"sourceId", "sourceGeneration", "domainKey"})
            need(equal(request.at(field), impl_->options.identity.at(field)), "stale_generation");
        const auto action = text(request, "action");
        if (impl_->readonly_copy() && (action == "begin" || action == "put" || action == "commit"))
            fail(ErrorCode::permission, "persistence copy is read-only; cutover proof unavailable");
        auto next = impl_->state;
        bool changed = false;
        auto response = impl_->dispatch(next, request, owner, changed);
        validate("Response", response);
        need(response.dump().size() <= 32768, "capacity_exceeded");
        if (changed)
            impl_->save(std::move(next));
        return response;
    });
}
Result<Json> FileStore::capacity() {
    return impl_->run([&] { return impl_->capacity_of(impl_->state); });
}
Result<executor::ClaimResult> FileStore::claim(const executor::Operation &op) {
    return impl_->run([&] {
        impl_->scope_owner(owner_of(op));
        take(executor::validate_operation(op));
        if (op.request.operation == "tool.invoke" && text(op.request.args, "name") == tool_name) {
            const auto request =
                take(executor::parse_tool_arguments(text(op.request.args, "argsJson")));
            for (const char *field : {"sourceId", "sourceGeneration", "domainKey"})
                need(equal(request.at(field), impl_->options.identity.at(field)),
                     "stale_generation");
        }
        const auto id = journal_key(op);
        const auto *old = impl_->state.at("journal").find(id);
        if (!old) {
            if (impl_->readonly_copy())
                fail(ErrorCode::permission,
                     "persistence copy is read-only; cutover proof unavailable");
            if (impl_->state.at("journal").as_object().size() >=
                impl_->options.limits.max_journal_entries)
                fail(ErrorCode::capacity, "capacity_exceeded");
            auto next = impl_->state;
            Json credit;
            if (op.request.operation == "tool.invoke" &&
                text(op.request.args, "name") == tool_name) {
                const auto request =
                    take(executor::parse_tool_arguments(text(op.request.args, "argsJson")));
                const auto action = text(request, "action");
                if (action == "put" || action == "commit") {
                    const auto *row = next.at("transfers").find(text(request, "transferId"));
                    if (row && text(row->at("transfer"), "status") == "staging" &&
                        equal(row->at("owner"), owner_of(op))) {
                        const auto target =
                            action == "commit" ? std::string("commit") : object_key(request);
                        bool assigned = false;
                        for (const auto &entry : next.at("journal").as_object()) {
                            const auto &prior = entry.second.at("credit");
                            if (entry.second.at("receipt").is_null() && !prior.is_null() &&
                                equal(prior.at("transferId"), request.at("transferId")) &&
                                text(prior, "target") == target)
                                assigned = true;
                        }
                        // 只为当前确实能推进同计划的原动作消费预留；错误/重复执行用未预留余量。
                        auto preview = next;
                        bool changed = false;
                        const auto prepared = protect([&] {
                            return impl_->dispatch(preview, request, owner_of(op), changed);
                        });
                        if (prepared && changed && !assigned)
                            credit = Json::object(
                                {{"transferId", request.at("transferId")}, {"target", target}});
                    }
                }
            }
            next.at("journal").set(id, Json::object({{"operation", executor::to_json(op)},
                                                     {"owner", owner_of(op)},
                                                     {"receipt", Json()},
                                                     {"credit", credit}}));
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
        take(executor::validate_operation(op));
        if (op.request.operation == "tool.invoke" && text(op.request.args, "name") == tool_name) {
            const auto request =
                take(executor::parse_tool_arguments(text(op.request.args, "argsJson")));
            for (const char *field : {"sourceId", "sourceGeneration", "domainKey"})
                need(equal(request.at(field), impl_->options.identity.at(field)),
                     "stale_generation");
        }
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
                    bool returned_response = false;
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
                        auto response =
                            take(options.store->execute(request, owner, [&]() -> Result<void> {
                                if (context.cancellation.is_cancelled())
                                    return Error{ErrorCode::cancelled, "persistence cancelled"};
                                return options.authorize(op, context.cancellation);
                            }));
                        returned_response = true;
                        validate("Response", response);
                        for (const char *field :
                             {"contract", "action", "sourceId", "sourceGeneration", "domainKey"})
                            need(equal(response.at(field), request.at(field)), "response_mismatch");
                        if (request.find("transferId"))
                            for (const char *field : {"transferId", "intentSha256"})
                                need(equal(response.at("transfer").at(field), request.at(field)),
                                     "response_mismatch");
                        if (!equal(options.store->identity(), identity))
                            fail(ErrorCode::unknown, "publication identity changed");
                        if (!options.authorize(op, context.cancellation))
                            fail(ErrorCode::unknown, "publication result withheld");
                        if (context.cancellation.is_cancelled())
                            fail(ErrorCode::unknown, "publication result withheld");
                        const auto action = text(request, "action");
                        if ((action == "begin" || action == "put" || action == "commit") &&
                            text(response.at("transfer"), "status") == "rejected")
                            return Json::object(
                                {{"status", "error"},
                                 {"message", response.at("transfer").at("rejection").at("code")}});
                        return Json::object(
                            {{"status", "ok"},
                             {"content", Json::array({Json::object(
                                             {{"t", "text"}, {"text", response.dump()}})})}});
                    });
                    if (result)
                        return std::move(result.value());
                    // 介质/授权变化或提交失回保 unknown；不把未知副作用伪报成未执行。
                    if (!returned_response && (result.error().code == ErrorCode::conflict ||
                                               result.error().code == ErrorCode::capacity))
                        return Json::object(
                            {{"status", "error"}, {"message", result.error().message}});
                    return executor::ToolFailure::unknown("memory_publication_outcome_unknown");
                }});
        return host;
    });
}
} // namespace tansr::terminal_persistence