#pragma once
#include "tansr/archive.hpp"
#include "tansr/canonical.hpp"
#include <algorithm>
#include <stdexcept>
namespace archive_test_support {
using namespace tansr;
using namespace tansr::archive;
inline int checks{};
inline void check(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
template <class T> T must(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
inline void must(Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
inline Json parse(std::string_view text) { return must(Json::parse(text)); }
inline std::string digest(std::string_view domain, const Json &value) {
    return must(canonical::digest(domain, value));
}
inline std::string raw_digest(std::string_view domain, std::string_view value) {
    return must(canonical::digest_bytes(domain, value));
}
inline Json without(Json value, std::string_view key) {
    auto &fields = value.as_object();
    fields.erase(std::remove_if(fields.begin(), fields.end(),
                                [&](const auto &field) { return field.first == key; }),
                 fields.end());
    return value;
}
struct Fixture {
    Json binding, status, page;
    Bodies bodies;
};
inline Fixture fixture() {
    auto binding = parse(
        R"({"protocol":"sdk2-ext-v1","bindingId":"binding-1","scope":{"applicationScopeId":"app-1","endUserId":"user-1","authorizationRevision":"1"},"target":{"sessionId":"session-1","generations":{"historyEpoch":"h-1","deletionGeneration":"0","projectionRevision":"1"},"sourceSnapshotDigest":"0000000000000000000000000000000000000000000000000000000000000000"},"revision":"1","state":"active","sourceId":"source-1","acceptedCapabilities":["archive-transfer-v1","context-materials-v1"],"rejectedCapabilities":[],"availability":"legacy-complete","operationEpoch":{"id":"epoch-1","issuedAt":"2030-01-01T00:00:00.000Z","expiresAt":"2030-01-01T00:01:00.000Z","state":"active"},"archiveAckFormat":"split-receipts-v1","limits":{"controlBytes":262144,"recordBytes":262144,"pageRecords":128,"pageBytes":1048576,"attachmentBytes":33554432,"chunkBytes":1024,"materialConcurrent":2,"materialQueue":16,"materialCandidates":32,"materialBytes":1048576,"materialDeadlineMs":30000,"pendingRecords":4096,"pendingBytes":67108864,"inflightReserveBytes":1048576,"offlineMs":1000,"eventRetentionMs":1000,"eventRetentionFrames":1,"eventRetentionBytes":1024,"terminalReceiptRetentionMs":60000,"epochLifetimeMs":60000,"materialChunkBytes":65536}})");
    // 空白与数字词法是被承诺的原正文，不从 DOM 重编码。
    const std::string body =
        "{ \"role\" : \"user\", \"n\" : 1.0, \"text\" : \"synthetic archive bytes\" }\n";
    const std::string attachment("raw\0attachment\xff", 15);
    auto ref = [&](const char *id, const std::string &bytes) {
        return Json::object({{"artifactId", id},
                             {"sourceId", "source-1"},
                             {"bytes", Json(static_cast<std::uint64_t>(bytes.size()))},
                             {"sha256", must(crypto::sha256_hex(bytes))},
                             {"mediaType", "application/octet-stream"}});
    };
    auto payload = ref("payload-1", body), binary = ref("attachment-1", attachment);
    auto record = Json::object({{"recordId", "record-1"},
                                {"sequence", "1"},
                                {"target", binding.at("target")},
                                {"turnId", "turn-1"},
                                {"recordKind", "turn"},
                                {"turnState", "completed"},
                                {"predecessorDigest", std::string(64, '0')},
                                {"payload", payload},
                                {"attachments", Json::array({binary})},
                                {"payloadDigest", raw_digest("tansr.sdk2.payload.v1", body)}});
    record.set("recordDigest", digest("tansr.sdk2.record.v1", record));
    auto status = Json::object(
        {{"protocol", protocol},
         {"bindingId", "binding-1"},
         {"revision", "1"},
         {"generations", binding.at("target").at("generations")},
         {"sourceId", "source-1"},
         {"sourceGeneration", "source-generation-1"},
         {"publishedThroughSequence", "1"},
         {"acknowledgedCoverage", Json()},
         {"releasableThroughSequence", Json()},
         {"pendingBytes", Json(static_cast<std::uint64_t>(body.size() + attachment.size()))},
         {"pendingRecords", 1},
         {"sessionPersistence", "unchanged"},
         {"state", "active"}});
    auto page = Json::object({{"protocol", protocol},
                              {"bindingId", "binding-1"},
                              {"generations", binding.at("target").at("generations")},
                              {"records", Json::array({record})},
                              {"nextAfterSequence", "1"},
                              {"complete", true},
                              {"publishedThroughSequence", "1"}});
    return {binding, status, page, {{"payload-1", body}, {"attachment-1", attachment}}};
}
inline StoreOptions options(const std::filesystem::path &path) {
    auto f = fixture();
    StoreOptions value;
    value.path = path;
    value.key.fill(7);
    value.key_id = "key-1";
    value.identity = must(identity_from_binding(f.binding, f.status));
    value.check_access = [](const Json &) -> Result<void> { return {}; };
    return value;
}
inline Json request(const char *id = "original") {
    return Json::object({{"requestId", id}, {"operationEpoch", "epoch-1"}});
}
inline Json receive(FileStore &store) {
    auto f = fixture();
    return must(
        store.receive(f.binding, f.status, f.page, f.bodies, request(), unix_time_ms() + 60000));
}
inline Json receipt(const Json &identity, const Json &ack, const char *revision = "2") {
    auto framed = Json::object(
        {{"scope", Json::array({identity.at("applicationScopeId"), identity.at("endUserId")})},
         {"operation", "archive-ack"},
         {"semantic", without(ack, "request")}});
    return Json::object({{"protocol", protocol},
                         {"request", ack.at("request")},
                         {"bindingId", ack.at("bindingId")},
                         {"operation", "archive-ack"},
                         {"semanticDigest", digest("tansr.sdk2.operation.v1", framed)},
                         {"state", "completed"},
                         {"revision", revision},
                         {"outcomeRef", "ack-result"}});
}
} // namespace archive_test_support
