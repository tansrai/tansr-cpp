#include "internal.hpp"
#include <cstring>
#include <mutex>

namespace tansr::archive {
using namespace detail;
namespace {
constexpr const char *magic = "Tansr-Cpp-Archive/1\n";
constexpr const char *format = "tansr-cpp-archive-v1";
constexpr std::size_t rebase_reserve = 528384;
void wipe(void *value, std::size_t count) {
    volatile auto *bytes = static_cast<volatile unsigned char *>(value);
    while (count--)
        *bytes++ = 0;
}
struct Secret {
    std::string value;
    ~Secret() {
        if (!value.empty())
            wipe(value.data(), value.size());
    }
};
Json limits_json(StoreLimits limits) {
    return Json::object(
        {{"maxRecords", Json(static_cast<std::uint64_t>(limits.max_records))},
         {"maxArtifacts", Json(static_cast<std::uint64_t>(limits.max_artifacts))},
         {"maxStoredBytes", Json(static_cast<std::uint64_t>(limits.max_stored_bytes))},
         {"maxBatchBytes", Json(static_cast<std::uint64_t>(limits.max_batch_bytes))}});
}
void check_limits(StoreLimits limits) {
    if (!limits.max_records || limits.max_records > 1000000 || !limits.max_artifacts ||
        limits.max_artifacts > 1000000 || !limits.max_stored_bytes ||
        limits.max_stored_bytes > (64U << 20) || !limits.max_batch_bytes ||
        limits.max_batch_bytes > limits.max_stored_bytes)
        fail(ErrorCode::capacity, "invalid archive limits");
}
void identity_valid(const Json &identity) {
    need(identity.is_object() && identity.as_object().size() == 7);
    for (const auto *key : {"applicationScopeId", "bindingId", "sourceId", "sourceGeneration"})
        validate("Id", identity.at(key));
    for (const auto *key : {"endUserId", "sessionId"})
        validate("LegacyId", identity.at(key));
    validate("Generations", identity.at("generations"));
}
std::string decoded(const Json &artifact) {
    auto bytes = take(crypto::base64_decode(text(artifact, "body")));
    std::string value(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    if (!bytes.empty())
        wipe(bytes.data(), bytes.size());
    return value;
}
std::optional<Json> head_of(const Json &state) {
    const auto &records = state.at("records").as_array();
    if (records.empty())
        return {};
    return Json::object({{"sequence", records.back().at("sequence")},
                         {"recordDigest", records.back().at("recordDigest")}});
}
bool reserved(const Json &state, const Json &request) {
    if (!state.at("lastReceipt").is_null() && equal(state.at("lastReceipt").at("request"), request))
        return true;
    for (const auto &row : state.at("rebases").as_array())
        if (equal(row.at("intent").at("request"), request) ||
            equal(row.at("intent").at("previous").at("request"), request))
            return true;
    return false;
}
std::optional<std::size_t> pending_row(const Json &state) {
    const auto &rows = state.at("rebases").as_array();
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].at("result").is_null() && rows[i].at("originalReceipt").is_null())
            return i;
    return {};
}
} // namespace
struct FileStore::Impl {
    StoreOptions options;
    std::unique_ptr<storage::PrivateDirectory> directory;
    std::string leaf;
    Json state;
    std::recursive_mutex mutex;
    bool entered{};
    bool uncertain{};
    ~Impl() {
        wipe(options.key.data(), options.key.size());
        if (auto *artifacts = state.find("artifacts"))
            for (auto &item : artifacts->as_object())
                if (auto *body = item.second.find("body"))
                    wipe(body->as_string().data(), body->as_string().size());
    }
    void authorize() {
        if (uncertain)
            fail(ErrorCode::unknown, "archive commit uncertain; close and reopen");
        take(directory->check_access());
    }
    void validate_state(const Json &next) {
        need(next.is_object() && next.as_object().size() == 13 && text(next, "format") == format);
        need(equal(next.at("identity"), options.identity) &&
             equal(next.at("limits"), limits_json(options.limits)));
        need(text(next, "keyId") == options.key_id &&
             next.at("writes").as_u64() <= options.max_encryptions);
        const auto &records = next.at("records").as_array();
        const auto &artifacts = next.at("artifacts");
        need(records.size() <= options.limits.max_records &&
             artifacts.as_object().size() <= options.limits.max_artifacts);
        std::size_t bytes{};
        std::string predecessor(64, '0');
        std::set<std::string> ids, seen;
        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto &record = records[i];
            verify_record(record, 262144);
            need(seq(record.at("sequence")) == i + 1 &&
                 text(record, "predecessorDigest") == predecessor &&
                 ids.insert(text(record, "recordId")).second);
            need(equal(record.at("target").at("sessionId"), options.identity.at("sessionId")) &&
                 equal(record.at("target").at("generations"), options.identity.at("generations")));
            predecessor = text(record, "recordDigest");
            bytes = bounded_add(bytes, encode(record).size(), options.limits.max_stored_bytes);
            for (const auto &ref : references(record)) {
                const auto &saved = artifacts.at(text(ref, "artifactId"));
                need(equal(saved.at("reference"), ref) &&
                     equal(ref.at("sourceId"), options.identity.at("sourceId")));
                Secret body{decoded(saved)};
                need(body.value.size() == ref.at("bytes").as_u64() &&
                     hash(body.value) == text(ref, "sha256"));
                if (seen.insert(text(ref, "artifactId")).second)
                    bytes = bounded_add(bytes, body.value.size(), options.limits.max_stored_bytes);
            }
            Secret payload{decoded(artifacts.at(text(record.at("payload"), "artifactId")))};
            need(domain("tansr.sdk2.payload.v1", payload.value) == text(record, "payloadDigest"));
        }
        need(seen.size() == artifacts.as_object().size());
        const auto &coverage = next.at("coverage");
        const auto &pending = next.at("pending");
        if (!coverage.is_null()) {
            verify_coverage(coverage);
            const auto end = seq(coverage.at("throughSequence"));
            need(end <= records.size());
            need(text(records[static_cast<std::size_t>(end - 1)], "recordDigest") ==
                 text(coverage, "headDigest"));
            const auto &receipt = next.at("lastReceipt");
            validate("MutationReceipt", receipt);
            need(equal(receipt.at("bindingId"), options.identity.at("bindingId")) &&
                 text(receipt, "operation") == "archive-ack" &&
                 text(receipt, "state") == "completed" && seq(receipt.at("revision")) > 0);
            need(!next.at("confirmedAck").is_null());
            verify_receipt(options.identity, next.at("confirmedAck"), receipt);
            need(equal(next.at("confirmedAck").at("coverage"), coverage));
        } else
            need(next.at("lastReceipt").is_null() && next.at("confirmedAck").is_null());
        if (!pending.is_null()) {
            validate("ArchiveAckRequest", pending);
            verify_coverage(pending.at("coverage"));
            for (const auto *key : {"bindingId", "sourceId", "sourceGeneration", "generations"})
                need(equal(pending.at(key), options.identity.at(key)));
            need(seq(pending.at("coverage").at("throughSequence")) == records.size() &&
                 text(pending.at("coverage"), "headDigest") == predecessor &&
                 seq(pending.at("coverage").at("fromSequence")) ==
                     (coverage.is_null() ? 0 : seq(coverage.at("throughSequence"))) + 1);
            need(next.at("pendingDeadline").is_number() && next.at("pendingDeadline").as_i64() > 0);
        } else {
            need(next.at("pendingDeadline").is_null());
            need(records.empty() ||
                 (!coverage.is_null() && seq(coverage.at("throughSequence")) == records.size()));
        }
        std::set<std::string> requests;
        unsigned unresolved{};
        for (const auto &row : next.at("rebases").as_array()) {
            const auto &intent = row.at("intent");
            const auto &old = intent.at("previous");
            verify_rebase(intent);
            need(row.at("deadline").as_i64() > 0);
            for (const auto *request : {&intent.at("request"), &old.at("request")})
                need(requests.insert(encode(*request)).second);
            for (const auto *key : {"bindingId", "sourceId", "sourceGeneration", "generations"})
                need(equal(old.at(key), options.identity.at(key)));
            const auto end = seq(old.at("coverage").at("throughSequence"));
            need(end > 0 && end <= records.size());
            need(text(records[static_cast<std::size_t>(end - 1)], "recordDigest") ==
                 text(old.at("coverage"), "headDigest"));
            bytes = bounded_add(bytes, encode(row).size(), options.limits.max_stored_bytes);
            if (!row.at("result").is_null()) {
                need(row.at("originalReceipt").is_null());
                verify_rebase_result(intent, row.at("result"));
                verify_receipt(options.identity, row.at("result").at("next"),
                               row.at("result").at("receipt"));
            } else if (!row.at("originalReceipt").is_null())
                verify_receipt(options.identity, old, row.at("originalReceipt"));
            else {
                ++unresolved;
                need(equal(pending, old));
                bytes = bounded_add(bytes, rebase_reserve, options.limits.max_stored_bytes);
            }
            if (!row.at("result").is_null() || !row.at("originalReceipt").is_null())
                need(!coverage.is_null() && seq(coverage.at("throughSequence")) >= end);
        }
        need(unresolved <= 1);
    }
    void save(Json next, bool replace = true) {
        authorize();
        auto writes = state.at("writes").as_u64();
        if (writes >= options.max_encryptions)
            fail(ErrorCode::capacity, "archive key rotation required");
        next.set("writes", Json(writes + 1));
        next.set("keyId", options.key_id);
        validate_state(next);
        Secret plain{next.dump()};
        const auto maximum = options.limits.max_stored_bytes * 2 + (4U << 20);
        take(Json::parse(plain.value, JsonLimits{maximum, 32, 100000}));
        auto random = take(crypto::random_bytes(12));
        crypto::GcmNonce nonce{};
        std::copy(random.begin(), random.end(), nonce.begin());
        auto encrypted = take(crypto::aes256_gcm_encrypt(options.key, nonce, plain.value, magic));
        std::string blob(magic);
        blob.append(reinterpret_cast<const char *>(nonce.data()), nonce.size());
        blob.append(reinterpret_cast<const char *>(encrypted.ciphertext.data()),
                    encrypted.ciphertext.size());
        blob.append(reinterpret_cast<const char *>(encrypted.tag.data()), encrypted.tag.size());
        if (blob.size() > maximum)
            fail(ErrorCode::capacity, "archive snapshot limit exceeded");
        // 出现写入错误后不继续增加同一进程的 nonce 使用量或假报旧状态可写。
        auto committed = directory->write_atomic(leaf, blob, replace, options.commit_hook);
        if (!committed) {
            uncertain = true;
            throw Failure{committed.error()};
        }
        state = std::move(next);
    }
    template <class F> auto run(F &&fn) -> Result<decltype(fn())> {
        return protect([&]() -> decltype(fn()) {
            std::lock_guard<std::recursive_mutex> held(mutex);
            if (entered)
                fail(ErrorCode::reentrant, "archive callback reentry rejected");
            struct Guard {
                bool &value;
                ~Guard() { value = false; }
            } guard{entered};
            entered = true;
            authorize();
            return fn();
        });
    }
};

FileStore::FileStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FileStore::~FileStore() = default;
Result<std::unique_ptr<FileStore>> FileStore::open(StoreOptions options) {
    // 在移动参数前绑定原密钥字节，早期拒绝和正常返回都会清除此入参副本。
    struct KeyWiper {
        void *data;
        std::size_t size;
        ~KeyWiper() { wipe(data, size); }
    } key_wiper{options.key.data(), options.key.size()};
    return protect([&] {
        check_limits(options.limits);
        identity_valid(options.identity);
        validate("Id", Json(options.key_id));
        need(options.max_encryptions > 0 && options.max_encryptions <= (1U << 20));
        if (!options.check_access)
            fail(ErrorCode::permission, "current archive authorization required");
        take(options.check_access(options.identity));
        auto impl = std::make_unique<Impl>();
        impl->options = std::move(options);
        auto *owner = impl.get();
        impl->directory =
            take(storage::PrivateDirectory::open(impl->options.path.parent_path(), [owner] {
                return owner->options.check_access(owner->options.identity);
            }));
        impl->leaf = impl->options.path.filename().u8string();
        impl->state = Json::object({{"format", format},
                                    {"identity", impl->options.identity},
                                    {"limits", limits_json(impl->options.limits)},
                                    {"keyId", impl->options.key_id},
                                    {"writes", Json(std::uint64_t{0})},
                                    {"records", Json::array()},
                                    {"artifacts", Json::object()},
                                    {"pending", Json()},
                                    {"pendingDeadline", Json()},
                                    {"coverage", Json()},
                                    {"lastReceipt", Json()},
                                    {"confirmedAck", Json()},
                                    {"rebases", Json::array()}});
        const auto maximum = impl->options.limits.max_stored_bytes * 2 + (4U << 20);
        auto saved = take(impl->directory->read(impl->leaf, maximum));
        if (!saved)
            impl->save(impl->state, false);
        else {
            const std::string_view blob = *saved;
            const auto prefix = std::strlen(magic);
            need(blob.size() >= prefix + 12 + 16 && blob.substr(0, prefix) == magic);
            crypto::GcmNonce nonce{};
            crypto::GcmTag tag{};
            std::memcpy(nonce.data(), blob.data() + prefix, nonce.size());
            std::memcpy(tag.data(), blob.data() + blob.size() - tag.size(), tag.size());
            auto decrypted = take(crypto::aes256_gcm_decrypt(
                impl->options.key, nonce,
                blob.substr(prefix + nonce.size(),
                            blob.size() - prefix - nonce.size() - tag.size()),
                tag, magic));
            Secret plain{
                std::string(reinterpret_cast<const char *>(decrypted.data()), decrypted.size())};
            if (!decrypted.empty())
                wipe(decrypted.data(), decrypted.size());
            auto state = take(Json::parse(plain.value, JsonLimits{maximum, 32, 100000}));
            impl->validate_state(state);
            impl->state = std::move(state);
            impl->authorize();
        }
        return std::unique_ptr<FileStore>(new FileStore(std::move(impl)));
    });
}
const Json &FileStore::identity() const noexcept { return impl_->options.identity; }
StoreLimits FileStore::limits() const noexcept { return impl_->options.limits; }
Result<void> FileStore::check_access() {
    return impl_->run([] {});
}
Result<std::optional<Json>> FileStore::head() {
    return impl_->run([&] { return head_of(impl_->state); });
}
Result<std::optional<Json>> FileStore::coverage() {
    return impl_->run([&] { return optional(impl_->state.at("coverage")); });
}
Result<std::optional<Json>> FileStore::pending() {
    return impl_->run([&] { return optional(impl_->state.at("pending")); });
}
Result<std::optional<std::int64_t>> FileStore::pending_deadline() {
    return impl_->run([&]() -> std::optional<std::int64_t> {
        if (auto row = pending_row(impl_->state))
            return impl_->state.at("rebases").at(*row).at("deadline").as_i64();
        if (impl_->state.at("pendingDeadline").is_null())
            return {};
        return impl_->state.at("pendingDeadline").as_i64();
    });
}
Result<std::vector<Json>> FileStore::records_by_id(const std::vector<std::string> &ids) {
    return impl_->run([&] {
        need(!ids.empty() && ids.size() <= 128);
        std::set<std::string> seen;
        std::vector<Json> out;
        for (const auto &id : ids) {
            validate("Id", Json(id));
            need(seen.insert(id).second);
            bool found = false;
            for (const auto &record : impl_->state.at("records").as_array())
                if (text(record, "recordId") == id) {
                    out.push_back(record);
                    found = true;
                    break;
                }
            if (!found)
                fail(ErrorCode::not_found, "requested archive record unavailable");
        }
        return out;
    });
}
Result<std::string> FileStore::body(const Json &ref) {
    return impl_->run([&] {
        validate("ArtifactRef", ref);
        need(equal(ref.at("sourceId"), identity().at("sourceId")));
        const auto &saved = impl_->state.at("artifacts").at(text(ref, "artifactId"));
        need(equal(saved.at("reference"), ref));
        auto body = decoded(saved);
        need(body.size() == ref.at("bytes").as_u64() && hash(body) == text(ref, "sha256"));
        impl_->authorize();
        return body;
    });
}
Result<Json> FileStore::receive(const Json &binding, const Json &status, const Json &page,
                                Bodies bodies, const Json &request, std::int64_t deadline_ms) {
    return impl_->run([&] {
        need(impl_->state.at("pending").is_null() && deadline_ms > unix_time_ms());
        need(equal(take(identity_from_binding(binding, status)), identity()));
        validate("RequestIdentity", request);
        need(!binding.at("operationEpoch").is_null() &&
             equal(binding.at("operationEpoch").at("id"), request.at("operationEpoch")));
        need(has(binding.at("acceptedCapabilities"), "archive-transfer-v1") &&
             text(binding, "archiveAckFormat") == "split-receipts-v1");
        if (!equal(status.at("publishedThroughSequence"), page.at("publishedThroughSequence")))
            fail(ErrorCode::unknown, "archive publication changed before receive");
        need(!reserved(impl_->state, request));
        auto head = head_of(impl_->state);
        if (head)
            need(!status.at("acknowledgedCoverage").is_null() &&
                 equal(head->at("sequence"),
                       status.at("acknowledgedCoverage").at("throughSequence")) &&
                 equal(head->at("recordDigest"),
                       status.at("acknowledgedCoverage").at("headDigest")));
        else
            need(status.at("acknowledgedCoverage").is_null());
        verify_page(binding,
                    head ? std::optional<std::string>{text(*head, "sequence")} : std::nullopt,
                    page);
        const auto &records = page.at("records").as_array();
        need(!records.empty());
        need(text(records.front(), "predecessorDigest") ==
             (head ? text(*head, "recordDigest") : std::string(64, '0')));
        std::map<std::string, Json> refs;
        std::set<std::string> pseen, aseen;
        Json::Array payloads, attachments;
        std::size_t bytes{};
        for (const auto &record : records) {
            bytes = bounded_add(bytes, encode(record).size(), limits().max_batch_bytes);
            auto rrefs = references(record);
            for (std::size_t i = 0; i < rrefs.size(); ++i) {
                const auto &ref = rrefs[i];
                auto id = text(ref, "artifactId");
                if (refs.emplace(id, ref).second)
                    bytes = bounded_add(bytes, static_cast<std::size_t>(ref.at("bytes").as_u64()),
                                        limits().max_batch_bytes);
                Json receipt = Json::object({{"artifactId", id},
                                             {"sha256", ref.at("sha256")},
                                             {"state", "durably-stored"}});
                if (i == 0 && pseen.insert(id).second)
                    payloads.push_back(receipt);
                else if (i > 0 && aseen.insert(id).second)
                    attachments.push_back(receipt);
            }
        }
        need(refs.size() == bodies.size());
        auto next = impl_->state;
        for (const auto &item : refs) {
            const auto &body = bodies.at(item.first);
            const auto &ref = item.second;
            need(body.size() == ref.at("bytes").as_u64() && hash(body) == text(ref, "sha256"));
            if (const auto *old = next.at("artifacts").find(item.first))
                need(equal(old->at("reference"), ref));
            next.at("artifacts")
                .set(item.first,
                     Json::object({{"reference", ref}, {"body", crypto::base64_encode(body)}}));
        }
        auto ack = Json::object(
            {{"protocol", protocol},
             {"request", request},
             {"bindingId", identity().at("bindingId")},
             {"expectedRevision", binding.at("revision")},
             {"generations", identity().at("generations")},
             {"sourceId", identity().at("sourceId")},
             {"sourceGeneration", identity().at("sourceGeneration")},
             {"coverage", Json::object({{"fromSequence", records.front().at("sequence")},
                                        {"throughSequence", records.back().at("sequence")},
                                        {"headDigest", records.back().at("recordDigest")}})},
             {"attachments", Json(std::move(attachments))},
             {"ackFormat", "split-receipts-v1"},
             {"payloads", Json(std::move(payloads))}});
        validate("ArchiveAckRequest", ack);
        encode(ack, static_cast<std::size_t>(binding.at("limits").at("controlBytes").as_u64()));
        for (const auto &record : records)
            next.at("records").as_array().push_back(record);
        next.set("pending", ack);
        next.set("pendingDeadline", Json(deadline_ms));
        impl_->save(std::move(next));
        return ack;
    });
}
Result<void> FileStore::confirm(const Json &receipt) {
    return impl_->run([&] {
        const auto &pending = impl_->state.at("pending");
        if (pending.is_null()) {
            need(equal(impl_->state.at("lastReceipt"), receipt));
            return;
        }
        verify_receipt(identity(), pending, receipt);
        auto next = impl_->state;
        next.set("coverage", pending.at("coverage"));
        next.set("confirmedAck", pending);
        next.set("pending", Json());
        next.set("pendingDeadline", Json());
        next.set("lastReceipt", receipt);
        for (auto &row : next.at("rebases").as_array())
            if (row.at("result").is_null() && row.at("originalReceipt").is_null() &&
                equal(row.at("intent").at("previous"), pending))
                row.set("originalReceipt", receipt);
        impl_->save(std::move(next));
    });
}
Result<std::optional<Json>> FileStore::pending_rebase() {
    return impl_->run([&]() -> std::optional<Json> {
        auto row = pending_row(impl_->state);
        return row ? std::optional<Json>{impl_->state.at("rebases").at(*row).at("intent")}
                   : std::nullopt;
    });
}
Result<Json> FileStore::prepare_rebase(const Json &request, std::int64_t deadline_ms) {
    return impl_->run([&] {
        validate("RequestIdentity", request);
        need(deadline_ms > unix_time_ms());
        if (auto row = pending_row(impl_->state)) {
            const auto &intent = impl_->state.at("rebases").at(*row).at("intent");
            need(equal(intent.at("request"), request));
            return intent;
        }
        need(!reserved(impl_->state, request) && !impl_->state.at("pending").is_null());
        auto intent = Json::object({{"protocol", protocol},
                                    {"bindingId", identity().at("bindingId")},
                                    {"previous", impl_->state.at("pending")},
                                    {"request", request}});
        verify_rebase(intent);
        auto next = impl_->state;
        next.at("rebases").as_array().push_back(Json::object({{"intent", intent},
                                                              {"deadline", Json(deadline_ms)},
                                                              {"result", Json()},
                                                              {"originalReceipt", Json()}}));
        impl_->save(std::move(next));
        return intent;
    });
}
Result<void> FileStore::confirm_rebase(const Json &result) {
    return impl_->run([&] {
        auto next = impl_->state;
        Json *selected = nullptr;
        for (auto &row : next.at("rebases").as_array())
            if (equal(row.at("intent").at("request"), result.at("request"))) {
                selected = &row;
                break;
            }
        need(selected);
        verify_rebase_result(selected->at("intent"), result);
        verify_receipt(identity(), result.at("next"), result.at("receipt"));
        if (!selected->at("result").is_null()) {
            need(equal(selected->at("result"), result));
            return;
        }
        need(selected->at("originalReceipt").is_null() &&
             equal(next.at("pending"), selected->at("intent").at("previous")));
        selected->set("result", result);
        next.set("coverage", result.at("next").at("coverage"));
        next.set("confirmedAck", result.at("next"));
        next.set("lastReceipt", result.at("receipt"));
        next.set("pending", Json());
        next.set("pendingDeadline", Json());
        impl_->save(std::move(next));
    });
}
Result<void> FileStore::rotate_key(crypto::Aes256Key key, std::string key_id) {
    auto result = impl_->run([&] {
        validate("Id", Json(key_id));
        need(key_id != impl_->options.key_id && key != impl_->options.key);
        impl_->options.key = key;
        impl_->options.key_id = std::move(key_id);
        auto next = impl_->state;
        next.set("writes", Json(std::uint64_t{0}));
        impl_->state.set("writes", Json(std::uint64_t{0}));
        try {
            impl_->save(std::move(next));
        } catch (...) {
            impl_->uncertain = true;
            throw;
        }
    });
    wipe(key.data(), key.size());
    return result;
}
} // namespace tansr::archive
