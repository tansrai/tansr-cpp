#include "internal.hpp"

namespace tansr::session {
using namespace detail;
namespace {
Result<void> validate_compaction(const Json &raw) {
    const auto status = string(field(raw, "status"));
    if (status == "compacted") {
        auto id = required_string(raw, "compactionId");
        if (!id)
            return id.error();
        const auto &range = field(raw, "removedRange");
        if (!range.is_array() || range.as_array().size() != 2)
            return contract("compaction range must contain two safe integers");
        auto first = safe_number(range.at(0)), last = safe_number(range.at(1));
        if (!first || !last || first.value() > last.value())
            return contract("invalid compaction range");
    } else if (status == "rejected") {
        auto reason = string(field(raw, "reason"));
        if (reason != "empty_history" && reason != "not_configured" && reason != "hook_blocked")
            return contract("invalid compaction rejection");
    } else if (status == "failed") {
        auto reason = required_string(raw, "reason");
        if (!reason)
            return reason.error();
    } else
        return contract("invalid compaction status");
    return {};
}
} // namespace
Result<Json> Session::compact(CompactOptions compact, WriteOptions options) const {
    auto body = Json::object();
    if (compact.instructions) {
        if (utf16_units(*compact.instructions) > 4096)
            return invalid("compaction instructions exceed 4096 UTF-16 units");
        body.set("instructions", *compact.instructions);
    }
    if (compact.checkpoint) {
        if (const auto *toggle = std::get_if<bool>(&*compact.checkpoint))
            body.set("checkpoint", *toggle);
        else {
            const auto &value = std::get<LabeledCheckpoint>(*compact.checkpoint);
            auto labeled = Json::object();
            if (value.label) {
                auto valid = label(*value.label);
                if (!valid)
                    return valid.error();
                labeled.set("label", *value.label);
            }
            body.set("checkpoint", std::move(labeled));
        }
    }
    auto response = write("session.compact", std::move(body), std::move(options));
    if (!response)
        return response.error();
    auto obj = object_response(response.value(), 200);
    if (!obj)
        return obj.error();
    auto valid = validate_compaction(obj.value());
    if (!valid)
        return valid.error();
    return obj;
}
Result<Checkpoint> Session::read_checkpoint(Json raw) const {
    auto checkpoint_id = required_string(raw, "checkpointId");
    if (!checkpoint_id)
        return checkpoint_id.error();
    auto session_id = required_string(raw, "sessionId");
    if (!session_id)
        return session_id.error();
    auto count = safe_field(raw, "messageCount");
    if (!count)
        return count.error();
    if (session_id.value() != id())
        return contract("checkpoint changed session identity");
    return Checkpoint{std::move(checkpoint_id).value(), std::move(session_id).value(),
                      count.value(), std::move(raw)};
}
Result<std::vector<Checkpoint>> Session::checkpoints() const {
    auto response = read("session.checkpoint.list");
    if (!response)
        return response.error();
    auto obj = object_response(response.value(), 200);
    if (!obj)
        return obj.error();
    const auto &entries = field(obj.value(), "checkpoints");
    if (!entries.is_array())
        return contract("checkpoint list lacks checkpoints");
    std::vector<Checkpoint> out;
    for (const auto &entry : entries.as_array()) {
        auto item = read_checkpoint(entry);
        if (!item)
            return item.error();
        out.push_back(std::move(item).value());
    }
    return out;
}
Result<Checkpoint> Session::checkpoint(std::string value, WriteOptions options) const {
    auto valid = label(value);
    if (!valid)
        return valid.error();
    auto response = write("session.checkpoint.create", Json::object({{"label", std::move(value)}}),
                          std::move(options));
    if (!response)
        return response.error();
    auto obj = object_response(response.value(), 201);
    if (!obj)
        return obj.error();
    return read_checkpoint(std::move(obj).value());
}
Result<Json> Session::restore(std::string checkpoint_id, bool checkpoint,
                              WriteOptions options) const {
    auto valid = nonempty(checkpoint_id);
    if (!valid)
        return valid.error();
    auto response = write("session.checkpoint.restore", Json::object({{"checkpoint", checkpoint}}),
                          std::move(options), std::make_pair("targetId", checkpoint_id));
    if (!response)
        return response.error();
    auto obj = object_response(response.value(), 200);
    if (!obj)
        return obj.error();
    if (!eq(field(obj.value(), "status"), "restored") ||
        !eq(field(obj.value(), "checkpointId"), checkpoint_id))
        return contract("invalid restore receipt");
    auto from = safe_field(obj.value(), "fromMessages"), to = safe_field(obj.value(), "toMessages");
    if (!from)
        return from.error();
    if (!to)
        return to.error();
    return obj;
}
Result<void> Session::delete_checkpoint(std::string checkpoint_id, WriteOptions options) const {
    auto valid = nonempty(checkpoint_id);
    if (!valid)
        return valid.error();
    auto response = write("session.checkpoint.delete", {}, std::move(options),
                          std::make_pair("targetId", std::move(checkpoint_id)));
    if (!response)
        return response.error();
    if (response.value().status != 204 || !response.value().raw_body.empty())
        return contract("checkpoint deletion must return empty 204");
    return {};
}
Result<std::string> Session::export_checkpoint(std::string checkpoint_id) const {
    auto valid = nonempty(checkpoint_id);
    if (!valid)
        return valid.error();
    CallOptions options;
    options.parameters = {{"id", id()}, {"targetId", std::move(checkpoint_id)}};
    options.max_response_bytes = media_bytes;
    auto response = client_.call("session.checkpoint.export", std::move(options));
    if (!response)
        return response.error();
    if (response.value().status != 200 ||
        response.value().content_type != "application/octet-stream" ||
        response.value().raw_body.empty())
        return contract("invalid binary checkpoint response");
    return std::move(response).value().raw_body;
}
Result<Checkpoint> Session::import_checkpoint(std::string bytes, std::string value,
                                              WriteOptions write) const {
    if (bytes.empty() || bytes.size() > media_bytes)
        return invalid("checkpoint bytes must contain 1 to 32 MiB");
    auto valid = label(value);
    if (!valid)
        return valid.error();
    auto options = write_options(write);
    options.raw_body = std::move(bytes);
    options.content_type = "application/octet-stream";
    options.max_response_bytes = media_bytes;
    if (!value.empty())
        options.query = {{"label", std::move(value)}};
    auto response = write_call("session.checkpoint.import", std::move(options));
    if (!response)
        return response.error();
    auto obj = object_response(response.value(), 201);
    if (!obj)
        return obj.error();
    return read_checkpoint(std::move(obj).value());
}
Result<Json> Session::set_cwd(std::string cwd, WriteOptions options) const {
    auto valid = nonempty(cwd);
    if (!valid)
        return valid.error();
    auto response =
        write("session.cwd.set", Json::object({{"cwd", std::move(cwd)}}), std::move(options));
    if (!response)
        return response.error();
    return object_response(response.value(), 200);
}
Result<Json> Session::transcribe(TranscriptionRequest request, WriteOptions options) const {
    auto valid = nonempty(request.audio);
    if (!valid)
        return valid.error();
    auto body = Json::object({{"audio", std::move(request.audio)}});
    optional(body, "model", request.model);
    optional(body, "language", request.language);
    optional(body, "diarize", request.diarize);
    optional(body, "prompt", request.prompt);
    auto response = write("session.audio.transcribe", std::move(body), std::move(options));
    if (!response)
        return response.error();
    return object_response(response.value(), 200);
}
Result<Json> Session::speak(SpeechRequest request, WriteOptions options) const {
    auto valid = nonempty(request.input);
    if (!valid)
        return valid.error();
    if (request.format && *request.format != "mp3" && *request.format != "wav")
        return invalid("speech format must be mp3 or wav");
    if (request.speed && !std::isfinite(*request.speed))
        return invalid("speech speed must be finite");
    auto body = Json::object({{"input", std::move(request.input)}});
    optional(body, "model", request.model);
    optional(body, "voice", request.voice);
    optional(body, "format", request.format);
    if (request.speed)
        body.set("speed", number(*request.speed));
    auto response = write("session.audio.speak", std::move(body), std::move(options));
    if (!response)
        return response.error();
    return object_response(response.value(), 200);
}
} // namespace tansr::session
