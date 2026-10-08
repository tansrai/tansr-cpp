#include "internal.hpp"

namespace tansr::archive {
using namespace detail;
namespace {
void intent_valid(const std::string &kind, const Json &body) {
    if (kind == "binding-create")
        validate("BindingCreateRequest", body);
    else if (kind == "material-response")
        validate("MaterialResponseRequest", body);
    else if (kind == "material-request")
        validate("MaterialRequest", body);
    else
        fail(ErrorCode::invalid_input, "unsupported archive intent kind");
}
} // namespace
Result<SavedIntent> SavedIntent::save(storage::PrivateDirectory &directory, std::string_view leaf,
                                      std::string kind, Json body, std::int64_t deadline_ms) {
    return protect([&] {
        intent_valid(kind, body);
        need(deadline_ms > unix_time_ms());
        auto saved = Json::object({{"format", "tansr-cpp-intent-v1"},
                                   {"kind", kind},
                                   {"body", body},
                                   {"deadlineMs", Json(deadline_ms)}});
        auto bytes = encode(saved);
        take(directory.write_atomic(leaf, bytes, false));
        return SavedIntent(std::move(kind), std::move(body), deadline_ms);
    });
}
Result<SavedIntent> SavedIntent::load(storage::PrivateDirectory &directory, std::string_view leaf) {
    return protect([&] {
        auto bytes = take(directory.read(leaf, 2U << 20));
        need(bytes.has_value());
        auto saved = take(Json::parse(*bytes));
        need(saved.is_object() && saved.as_object().size() == 4 &&
             text(saved, "format") == "tansr-cpp-intent-v1");
        const auto kind = text(saved, "kind");
        intent_valid(kind, saved.at("body"));
        const auto end = saved.at("deadlineMs").as_i64();
        need(end > 0);
        return SavedIntent(kind, saved.at("body"), end);
    });
}
ArchiveClient::ArchiveClient(std::shared_ptr<ApiClient> api) : api_(std::move(api)) {}
std::int64_t ArchiveClient::default_deadline_ms() const {
    return api_ ? api_->default_deadline_ms() : 0;
}
CallOptions ArchiveClient::context(CallOptions value) const {
    need(static_cast<bool>(api_));
    if (!value.deadline_ms)
        value.deadline_ms = api_->default_deadline_ms();
    return deadline(std::move(value));
}
Result<Json> ArchiveClient::call(std::string_view operation, std::string_view definition,
                                 CallOptions options, int status) const {
    return protect([&] {
        auto result = take(api_->call(operation, context(std::move(options))));
        need(result.status == status);
        validate(definition, result.body);
        return result.body;
    });
}
Result<Json> ArchiveClient::capabilities(CallOptions options) const {
    return protect([&] {
        auto value =
            take(call("archive.capabilities", "CapabilitiesResponse", context(std::move(options))));
        const auto &limits = value.at("limits");
        need(limits.at("inflightReserveBytes").as_u64() <= limits.at("pendingBytes").as_u64());
        need(value.at("archiveAckFormats").as_array().size() ==
             (has(value.at("capabilities"), "archive-transfer-v1") ? 1U : 0U));
        verify_epoch(value.at("operationEpoch"), limits.at("epochLifetimeMs").as_u64());
        return value;
    });
}
Result<Json> ArchiveClient::binding_target(std::string_view session, CallOptions options) const {
    return protect([&] {
        validate("LegacyId", Json(session));
        auto value = take(call("archive.binding.target", "BindingTargetView",
                               reading(session, context(std::move(options)))));
        need(text(value.at("target"), "sessionId") == session);
        if (value.at("bindingId").is_null())
            need(text(value, "revision") == "0" && value.at("operationEpoch").is_null());
        verify_epoch(value.at("operationEpoch"));
        return value;
    });
}
Result<Json> ArchiveClient::prepare_create(std::string_view session, std::string_view source,
                                           std::string_view request_id, CallOptions options) const {
    return protect([&] {
        options = context(std::move(options));
        auto caps = take(capabilities(options));
        need(has(caps.at("capabilities"), "archive-transfer-v1") &&
             has(caps.at("archiveAckFormats"), "split-receipts-v1"));
        auto target = take(binding_target(session, options));
        need(target.at("bindingId").is_null());
        auto input = Json::object(
            {{"protocol", protocol},
             {"request", Json::object({{"requestId", Json(request_id)},
                                       {"operationEpoch", caps.at("operationEpoch").at("id")}})},
             {"target", target.at("target")},
             {"expectedRevision", target.at("revision")},
             {"requiredCapabilities", Json::array({"archive-transfer-v1"})},
             {"optionalCapabilities", has(caps.at("capabilities"), "context-materials-v1")
                                          ? Json::array({"context-materials-v1"})
                                          : Json::array()},
             {"archive", Json::object({{"strategy", "single-authorized-source"},
                                       {"sourceId", Json(source)},
                                       {"durability", "source-ack-with-durable-spool"},
                                       {"delivery", "required"},
                                       {"sessionAvailability", "legacy-complete"},
                                       {"ackFormat", "split-receipts-v1"}})}});
        validate("BindingCreateRequest", input);
        return input;
    });
}
Result<Json> ArchiveClient::create_binding(const SavedIntent &intent, CallOptions options) const {
    return protect([&] {
        need(intent.kind() == "binding-create");
        const auto &input = intent.body();
        validate("BindingCreateRequest", input);
        options.deadline_ms = options.deadline_ms
                                  ? std::min(*options.deadline_ms, intent.deadline_ms())
                                  : intent.deadline_ms();
        std::set<std::string> requested;
        for (const auto *key : {"requiredCapabilities", "optionalCapabilities"})
            for (const auto &item : input.at(key).as_array())
                need(requested.insert(item.as_string()).second);
        auto value = take(call("archive.binding.create", "BindingView",
                               mutation("", input, context(std::move(options))), 201));
        verify_binding(value);
        std::set<std::string> decisions;
        for (const auto &item : value.at("acceptedCapabilities").as_array())
            decisions.insert(item.as_string());
        for (const auto &item : value.at("rejectedCapabilities").as_array())
            decisions.insert(text(item, "capability"));
        need(equal(value.at("target"), input.at("target")) &&
             equal(value.at("sourceId"), input.at("archive").at("sourceId")) &&
             decisions == requested);
        for (const auto &item : input.at("requiredCapabilities").as_array())
            need(has(value.at("acceptedCapabilities"), item.as_string()));
        return value;
    });
}
Result<Json> ArchiveClient::close_binding(const Json &input, CallOptions options) const {
    return protect([&] {
        validate("BindingCloseRequest", input);
        auto result =
            take(call("archive.binding.close", "MutationReceipt",
                      mutation(text(input, "bindingId"), input, context(std::move(options)))));
        need(equal(result.at("bindingId"), input.at("bindingId")) &&
             equal(result.at("request"), input.at("request")) &&
             text(result, "operation") == "binding-close");
        return result;
    });
}
Result<Json> ArchiveClient::binding(std::string_view id, CallOptions options) const {
    return protect([&] {
        validate("Id", Json(id));
        auto value = take(
            call("archive.binding.get", "BindingView", reading(id, context(std::move(options)))));
        need(text(value, "bindingId") == id);
        verify_binding(value);
        return value;
    });
}
Result<Json> ArchiveClient::status(std::string_view id, CallOptions options) const {
    return protect([&] {
        validate("Id", Json(id));
        auto value =
            take(call("archive.status", "ArchiveStatus", reading(id, context(std::move(options)))));
        need(text(value, "bindingId") == id);
        for (const auto *key : {"publishedThroughSequence", "releasableThroughSequence"})
            if (!value.at(key).is_null())
                need(seq(value.at(key)) > 0);
        const auto &coverage = value.at("acknowledgedCoverage");
        if (!coverage.is_null()) {
            verify_coverage(coverage);
            need(!value.at("publishedThroughSequence").is_null() &&
                 seq(value.at("publishedThroughSequence")) >= seq(coverage.at("throughSequence")));
            if (!value.at("releasableThroughSequence").is_null())
                need(seq(value.at("releasableThroughSequence")) <=
                     seq(coverage.at("throughSequence")));
        } else
            need(value.at("releasableThroughSequence").is_null());
        return value;
    });
}
Result<Json> ArchiveClient::records(const Json &binding, std::optional<std::string> after,
                                    CallOptions options) const {
    return protect([&] {
        verify_binding(binding);
        options = reading(text(binding, "bindingId"), context(std::move(options)));
        generations(options, binding.at("target").at("generations"));
        options.query["limit"] = std::to_string(binding.at("limits").at("pageRecords").as_u64());
        options.query["maxBytes"] = std::to_string(binding.at("limits").at("pageBytes").as_u64());
        options.max_response_bytes =
            static_cast<std::size_t>(binding.at("limits").at("pageBytes").as_u64());
        if (after) {
            seq(Json(*after));
            options.query["afterSequence"] = *after;
        }
        auto value = take(call("archive.records.read", "ArchivePage", std::move(options)));
        verify_page(binding, after, value);
        return value;
    });
}
Result<std::string> ArchiveClient::artifact(const Json &binding, const Json &ref,
                                            CallOptions options) const {
    return protect([&] {
        options = context(std::move(options));
        verify_binding(binding);
        validate("ArtifactRef", ref);
        need(equal(ref.at("sourceId"), binding.at("sourceId")) &&
             ref.at("bytes").as_u64() <= binding.at("limits").at("attachmentBytes").as_u64());
        const auto total = static_cast<std::size_t>(ref.at("bytes").as_u64());
        std::string out;
        out.reserve(total);
        while (out.size() < total) {
            const auto maximum =
                std::min(static_cast<std::size_t>(binding.at("limits").at("chunkBytes").as_u64()),
                         total - out.size());
            need(maximum > 0);
            auto request = reading(text(binding, "bindingId"), options);
            request.parameters["targetId"] = text(ref, "artifactId");
            generations(request, binding.at("target").at("generations"));
            request.query["offset"] = std::to_string(out.size());
            request.query["maxBytes"] = std::to_string(maximum);
            request.max_response_bytes = 1U << 20;
            auto chunk = take(call("archive.artifact.read", "ArtifactChunk", std::move(request)));
            auto bytes = take(crypto::base64_decode(text(chunk, "base64")));
            for (const auto *key : {"artifactId", "sourceId", "sha256"})
                need(equal(chunk.at(key), ref.at(key)));
            need(equal(chunk.at("bindingId"), binding.at("bindingId")) &&
                 equal(chunk.at("generations"), binding.at("target").at("generations")) &&
                 chunk.at("totalBytes").as_u64() == total &&
                 chunk.at("offset").as_u64() == out.size() && chunk.at("bytes").as_u64() > 0 &&
                 chunk.at("bytes").as_u64() <= maximum &&
                 bytes.size() == chunk.at("bytes").as_u64());
            std::string_view raw(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            need(hash(raw) == text(chunk, "chunkSha256"));
            out.append(raw);
        }
        need(hash(out) == text(ref, "sha256"));
        return out;
    });
}
Result<Json> ArchiveClient::acknowledge(const Json &ack, CallOptions options) const {
    return protect([&] {
        validate("ArchiveAckRequest", ack);
        verify_coverage(ack.at("coverage"));
        need(seq(ack.at("coverage").at("throughSequence")) -
                 seq(ack.at("coverage").at("fromSequence")) <
             128);
        auto value = take(call("archive.ack.commit", "MutationReceipt",
                               mutation(text(ack, "bindingId"), ack, context(std::move(options)))));
        need(equal(value.at("bindingId"), ack.at("bindingId")) &&
             equal(value.at("request"), ack.at("request")) &&
             text(value, "operation") == "archive-ack");
        return value;
    });
}
Result<Json> ArchiveClient::operation(std::string_view binding_id, std::string_view operation_name,
                                      const Json &request, CallOptions options) const {
    return protect([&] {
        auto input = Json::object({{"protocol", protocol},
                                   {"bindingId", Json(binding_id)},
                                   {"operation", Json(operation_name)},
                                   {"request", request}});
        validate("OperationStatusRequest", input);
        options = context(std::move(options));
        options.query = {{"protocol", protocol},
                         {"bindingId", std::string(binding_id)},
                         {"operation", std::string(operation_name)},
                         {"operationEpoch", text(request, "operationEpoch")},
                         {"requestId", text(request, "requestId")}};
        auto value = take(call("archive.operation.query", "MutationReceipt", std::move(options)));
        need(equal(value.at("request"), request) && text(value, "operation") == operation_name &&
             text(value, "bindingId") == binding_id);
        return value;
    });
}
Result<Json> ArchiveClient::creation_operation(std::string_view session, const Json &request,
                                               CallOptions options) const {
    return protect([&] {
        auto input = Json::object({{"protocol", protocol},
                                   {"sessionId", Json(session)},
                                   {"operation", "binding-create"},
                                   {"request", request}});
        validate("OperationStatusRequest", input);
        options = context(std::move(options));
        options.query = {{"protocol", protocol},
                         {"sessionId", std::string(session)},
                         {"operation", "binding-create"},
                         {"operationEpoch", text(request, "operationEpoch")},
                         {"requestId", text(request, "requestId")}};
        auto value = take(call("archive.operation.query", "MutationReceipt", std::move(options)));
        need(equal(value.at("request"), request) && text(value, "operation") == "binding-create");
        return value;
    });
}
Result<Json> ArchiveClient::rebase_acknowledgement(const Json &input, CallOptions options) const {
    return protect([&] {
        verify_rebase(input);
        options = mutation(text(input, "bindingId"), input, context(std::move(options)));
        options.max_response_bytes = 528384;
        auto response = take(api_->call("archive.ack.rebase", std::move(options)));
        need(response.status == 200);
        verify_rebase_result(input, response.body);
        return response.body;
    });
}
Result<ArchiveEventStream> ArchiveClient::events(const Json &binding,
                                                 std::optional<std::string> cursor,
                                                 CallOptions options) const {
    return protect([&] {
        verify_binding(binding);
        options = reading(text(binding, "bindingId"), context(std::move(options)));
        options.last_event_id = std::move(cursor);
        return ArchiveEventStream(take(api_->events("archive.events.observe", std::move(options))),
                                  binding);
    });
}
ArchiveEventStream::ArchiveEventStream(EventStream stream, Json binding)
    : stream_(std::move(stream)), binding_(std::move(binding)) {}
Result<std::optional<SseEvent>> ArchiveEventStream::next(CancellationToken cancel) {
    return protect([&] {
        auto event = take(stream_.next(cancel));
        if (!event)
            return event;
        auto envelope = take(Json::parse(event->data));
        const auto &raw = envelope.at("raw");
        validate("EventFrame", raw);
        need(equal(raw.at("bindingId"), binding_.at("bindingId")) &&
             equal(raw.at("generations"), binding_.at("target").at("generations")));
        return event;
    });
}
void ArchiveEventStream::cancel() noexcept { stream_.cancel(); }
} // namespace tansr::archive
