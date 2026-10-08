#include "internal.hpp"

namespace tansr::archive {
using namespace detail;
namespace {
void verify_upload(const Json &value, std::string_view binding, std::string_view request,
                   std::string_view artifact) {
    validate("MaterialUploadStatus", value);
    need(text(value, "bindingId") == binding && text(value, "materialRequestId") == request &&
         text(value.at("artifact"), "artifactId") == artifact);
    const auto chunk = value.at("chunkBytes").as_u64(),
               total = value.at("artifact").at("bytes").as_u64();
    need(chunk > 0);
    std::optional<std::uint64_t> previous;
    std::uint64_t bytes{};
    for (const auto &item : value.at("receivedOffsets").as_array()) {
        const auto offset = item.as_u64();
        need((!previous || offset > *previous) && offset % chunk == 0 && offset < total);
        bytes += std::min(chunk, total - offset);
        previous = offset;
    }
    need(bytes == value.at("receivedBytes").as_u64() &&
         total / chunk + (total % chunk ? 1U : 0U) <= 16 &&
         (text(value, "state") == "committed") == (bytes == total));
}
} // namespace
Result<Json> ArchiveClient::upload_material_chunk(const Json &input, CallOptions options) const {
    return protect([&] {
        validate("MaterialUploadChunkRequest", input);
        auto bytes = take(crypto::base64_decode(text(input, "base64")));
        need(bytes.size() == input.at("bytes").as_u64() &&
             hash(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size())) ==
                 text(input, "chunkSha256"));
        options = context(std::move(options));
        options.parameters = {{"id", text(input, "bindingId")},
                              {"targetId", text(input, "materialRequestId")},
                              {"uploadId", text(input, "artifactId")}};
        options.body = input;
        auto value =
            take(call("material.upload.chunk", "MaterialUploadStatus", std::move(options)));
        verify_upload(value, text(input, "bindingId"), text(input, "materialRequestId"),
                      text(input, "artifactId"));
        const auto offset = input.at("offset").as_u64(), chunk = value.at("chunkBytes").as_u64(),
                   total = value.at("artifact").at("bytes").as_u64();
        need(equal(value.at("artifact").at("sourceId"), input.at("sourceId")) &&
             offset % chunk == 0 && offset < total &&
             input.at("bytes").as_u64() == std::min(chunk, total - offset));
        bool found = false;
        for (const auto &item : value.at("receivedOffsets").as_array())
            if (item.as_u64() == offset)
                found = true;
        need(found);
        if (offset == 0 && input.at("bytes").as_u64() == total)
            need(equal(value.at("artifact").at("sha256"), input.at("chunkSha256")));
        return value;
    });
}
Result<Json> ArchiveClient::material_upload_status(std::string_view binding,
                                                   std::string_view request,
                                                   std::string_view artifact,
                                                   CallOptions options) const {
    return protect([&] {
        for (auto id : {binding, request, artifact})
            validate("Id", Json(id));
        options = reading(binding, context(std::move(options)));
        options.parameters["targetId"] = std::string(request);
        options.parameters["uploadId"] = std::string(artifact);
        auto value =
            take(call("material.upload.status", "MaterialUploadStatus", std::move(options)));
        verify_upload(value, binding, request, artifact);
        return value;
    });
}
Result<Json> ArchiveClient::prepare_materials_before(ArchiveStore &store, const Json &request,
                                                     const Json &identity,
                                                     std::int64_t original_deadline_ms,
                                                     CallOptions options) const {
    return protect([&] {
        validate("MaterialRequest", request);
        validate("RequestIdentity", identity);
        const auto &expected = store.identity();
        const auto limits = store.limits();
        for (const auto *key : {"bindingId", "sourceId", "sourceGeneration"})
            need(equal(request.at(key), expected.at(key)));
        need(equal(request.at("target").at("sessionId"), expected.at("sessionId")) &&
             equal(request.at("target").at("generations"), expected.at("generations")));
        const auto chunk = request.at("chunkBytes").as_u64();
        need(chunk > 0);
        const auto ttl = request.at("remainingTtlMs").as_u64();
        need(ttl <=
             static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() - unix_time_ms()));
        const auto observed = unix_time_ms() + static_cast<std::int64_t>(ttl);
        options.deadline_ms = std::min(std::min(original_deadline_ms, observed),
                                       options.deadline_ms.value_or(original_deadline_ms));
        options = context(std::move(options));
        take(store.check_access());
        std::vector<std::string> ids;
        for (const auto &item : request.at("requestedRecords").as_array())
            ids.push_back(text(item, "recordId"));
        const auto saved = take(store.records_by_id(ids));
        need(saved.size() == ids.size());
        std::map<std::string, Json> records, refs;
        std::set<std::string> asked_ids;
        std::size_t total{};
        for (const auto &record : saved) {
            verify_record(record, 262144);
            need(records.emplace(text(record, "recordId"), record).second);
        }
        for (const auto &asked : request.at("requestedRecords").as_array()) {
            const auto &record = records.at(text(asked, "recordId"));
            need(asked_ids.insert(text(asked, "recordId")).second);
            need(equal(record.at("recordDigest"), asked.at("digest")) &&
                 equal(record.at("payload"), asked.at("payload")) &&
                 equal(record.at("attachments"), asked.at("attachments")));
            need(equal(record.at("target").at("sessionId"), request.at("target").at("sessionId")) &&
                 equal(record.at("target").at("generations"),
                       request.at("target").at("generations")));
            for (const auto &ref : references(record)) {
                const auto size = ref.at("bytes").as_u64();
                need(equal(ref.at("sourceId"), expected.at("sourceId")) &&
                     size / chunk + (size % chunk ? 1U : 0U) <= 16);
                auto inserted = refs.emplace(text(ref, "artifactId"), ref);
                if (!inserted.second)
                    need(equal(inserted.first->second, ref));
                else
                    total =
                        bounded_add(total, static_cast<std::size_t>(size), limits.max_batch_bytes);
            }
        }
        need(total <= request.at("maxBytes").as_u64() && refs.size() <= limits.max_artifacts);
        // 全部记录、附件、字节和分块额度在首个上传前验证。
        Bodies bodies;
        for (const auto &item : refs) {
            auto body = take(store.body(item.second));
            need(body.size() == item.second.at("bytes").as_u64() &&
                 hash(body) == text(item.second, "sha256"));
            bodies.emplace(item.first, std::move(body));
        }
        for (const auto &record : saved)
            need(domain("tansr.sdk2.payload.v1",
                        bodies.at(text(record.at("payload"), "artifactId"))) ==
                 text(record, "payloadDigest"));
        std::map<std::string, Json> uploads;
        for (const auto &item : refs) {
            const auto &body = bodies.at(item.first);
            std::optional<Json> last;
            for (std::size_t offset = 0; offset < body.size();) {
                take(store.check_access());
                options = deadline(std::move(options));
                const auto size =
                    std::min<std::size_t>(static_cast<std::size_t>(chunk), body.size() - offset);
                const auto bytes = std::string_view(body).substr(offset, size);
                auto input = Json::object({{"protocol", protocol},
                                           {"bindingId", request.at("bindingId")},
                                           {"materialRequestId", request.at("materialRequestId")},
                                           {"target", request.at("target")},
                                           {"sourceId", request.at("sourceId")},
                                           {"sourceGeneration", request.at("sourceGeneration")},
                                           {"artifactId", item.first},
                                           {"offset", Json(static_cast<std::uint64_t>(offset))},
                                           {"bytes", Json(static_cast<std::uint64_t>(size))},
                                           {"chunkSha256", hash(bytes)},
                                           {"base64", crypto::base64_encode(bytes)}});
                last = take(upload_material_chunk(input, options));
                need(equal(last->at("artifact"), item.second) &&
                     last->at("chunkBytes").as_u64() == chunk);
                offset += size;
            }
            if (!last || text(*last, "state") != "committed")
                last = take(material_upload_status(text(request, "bindingId"),
                                                   text(request, "materialRequestId"), item.first,
                                                   options));
            need(text(*last, "state") == "committed" && equal(last->at("artifact"), item.second));
            uploads.emplace(item.first, Json::object({{"uploadId", last->at("uploadId")}}));
        }
        Json::Array results;
        for (const auto &asked : request.at("requestedRecords").as_array()) {
            Json::Array attachments;
            for (const auto &ref : asked.at("attachments").as_array())
                attachments.push_back(uploads.at(text(ref, "artifactId")));
            results.push_back(
                Json::object({{"recordId", asked.at("recordId")},
                              {"digest", asked.at("digest")},
                              {"payload", uploads.at(text(asked.at("payload"), "artifactId"))},
                              {"attachments", Json(std::move(attachments))}}));
        }
        auto response = Json::object({{"protocol", protocol},
                                      {"request", identity},
                                      {"bindingId", request.at("bindingId")},
                                      {"materialRequestId", request.at("materialRequestId")},
                                      {"target", request.at("target")},
                                      {"sourceId", request.at("sourceId")},
                                      {"sourceGeneration", request.at("sourceGeneration")},
                                      {"results", Json(std::move(results))}});
        validate("MaterialResponseRequest", response);
        take(store.check_access());
        deadline(options);
        return response;
    });
}
Result<Json> ArchiveClient::submit_materials(const SavedIntent &intent, CallOptions options) const {
    return protect([&] {
        need(intent.kind() == "material-response");
        const auto &input = intent.body();
        validate("MaterialResponseRequest", input);
        options.deadline_ms = options.deadline_ms
                                  ? std::min(*options.deadline_ms, intent.deadline_ms())
                                  : intent.deadline_ms();
        std::set<std::string> ids;
        for (const auto &item : input.at("results").as_array())
            need(ids.insert(text(item, "recordId")).second);
        auto value =
            take(call("material.response.submit", "MaterialReceipt",
                      mutation(text(input, "bindingId"), input, context(std::move(options))), 202));
        std::set<std::string> accepted;
        for (const auto &id : value.at("acceptedRecordIds").as_array())
            need(accepted.insert(id.as_string()).second);
        need(equal(value.at("bindingId"), input.at("bindingId")) &&
             equal(value.at("materialRequestId"), input.at("materialRequestId")) &&
             text(value, "state") == "received" && text(value, "revision") != "0" &&
             ids == accepted);
        return value;
    });
}
Result<Json> ArchiveClient::material_status(std::string_view binding, std::string_view request,
                                            CallOptions options) const {
    return protect([&] {
        validate("Id", Json(binding));
        validate("Id", Json(request));
        options = reading(binding, context(std::move(options)));
        options.parameters["targetId"] = std::string(request);
        auto value = take(call("material.status", "MaterialReceipt", std::move(options)));
        need(text(value, "bindingId") == binding && text(value, "materialRequestId") == request);
        if (text(value, "state") == "pending")
            need(text(value, "revision") == "0" &&
                 value.at("acceptedRecordIds").as_array().empty());
        return value;
    });
}
} // namespace tansr::archive
