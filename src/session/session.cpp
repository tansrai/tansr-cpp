#include "internal.hpp"
#include <set>

namespace tansr::session {
using namespace detail;
namespace {
Result<void> validate_create(const CreateOptions &options, std::string_view family) {
    if (options.resume && options.fork)
        return invalid("resume and fork are mutually exclusive");
    if (options.resume) {
        auto v = nonempty(options.resume->session_id);
        if (!v)
            return v;
    }
    if (options.fork) {
        auto v = nonempty(options.fork->session_id);
        if (!v)
            return v;
        v = nonempty(options.fork->checkpoint_id);
        if (!v)
            return v;
    }
    if (family == "sdk2-offload-v1") {
        bool valid =
            options.request_id && !options.request_id->empty() && options.request_id->size() <= 128;
        if (valid)
            for (unsigned char c : *options.request_id)
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-'))
                    valid = false;
        if (options.fork || (!options.resume && !valid))
            return invalid(
                "offload create requires a retained requestId and does not support fork");
    } else if (options.request_id)
        return invalid("requestId belongs to offload creation; use request_key for SDK1");
    if (options.budget &&
        ((options.budget->max_usd &&
          (!std::isfinite(*options.budget->max_usd) || *options.budget->max_usd < 0)) ||
         (options.budget->max_tokens && *options.budget->max_tokens > max_safe_integer)))
        return invalid("budget must contain finite nonnegative safe values");
    return {};
}
Json create_json(const CreateOptions &v) {
    auto out = Json::object();
    optional(out, "requestId", v.request_id);
    optional(out, "model", v.model);
    optional(out, "prompt", v.prompt);
    optional(out, "profile", v.profile);
    optional(out, "capabilitiesProfile", v.capabilities_profile);
    optional(out, "cwd", v.cwd);
    if (v.budget) {
        auto b = Json::object();
        if (v.budget->max_usd)
            b.set("maxUsd", number(*v.budget->max_usd));
        optional(b, "maxTokens", v.budget->max_tokens);
        out.set("budget", std::move(b));
    }
    if (v.tools) {
        Json::Array a;
        for (const auto &t : *v.tools)
            a.emplace_back(t);
        out.set("tools", Json(std::move(a)));
    }
    if (v.client_tools)
        out.set("clientTools", Json(*v.client_tools));
    if (v.resume)
        out.set("resume", Json::object({{"sessionId", v.resume->session_id}}));
    if (v.fork)
        out.set("fork", Json::object({{"sessionId", v.fork->session_id},
                                      {"checkpointId", v.fork->checkpoint_id}}));
    return out;
}
Result<void> validate_input(const Input &v) {
    for (const auto *s : {&v.input_id, &v.target.history_epoch, &v.target.turn_id})
        if (s->empty() || utf16_units(*s) > 128)
            return invalid("input identities require 1 to 128 UTF-16 units");
    if (v.ack && *v.ack != "memory" && *v.ack != "durable")
        return invalid("input ACK must be memory or durable");
    if (v.content.text && !v.content.blocks && !v.content.text->empty() &&
        utf16_units(*v.content.text) <= 262144)
        return {};
    if (!v.content.text && v.content.blocks)
        return validate_blocks(*v.content.blocks, true);
    return invalid("input requires exactly one nonempty text form");
}
Json input_json(const Input &v) {
    auto content = Json::object();
    optional(content, "text", v.content.text);
    if (v.content.blocks)
        content.set("blocks", blocks_json(*v.content.blocks));
    auto out = Json::object({{"inputId", v.input_id},
                             {"target", Json::object({{"historyEpoch", v.target.history_epoch},
                                                      {"turnId", v.target.turn_id}})},
                             {"content", std::move(content)}});
    optional(out, "ack", v.ack);
    return out;
}
Result<void> check_receipt(const Json &raw, std::string_view id, std::string_view input,
                           const InputTarget &target, const std::optional<std::string> &ack) {
    const auto state = string(field(raw, "state")), durability = string(field(raw, "durability"));
    if (!eq(field(raw, "sessionId"), id) || !eq(field(raw, "inputId"), input) ||
        !eq(field(raw, "turnId"), target.turn_id) ||
        !eq(field(raw, "historyEpoch"), target.history_epoch) ||
        !eq(field(raw, "source"), "strict") || !state ||
        (*state != "reserved" && *state != "accepted" && *state != "consumed" &&
         *state != "closed" && *state != "cancelled") ||
        !durability || (*durability != "memory" && *durability != "durable") ||
        (ack && *durability != *ack))
        return contract("input receipt changed identity, state or requested durability");
    auto ordinal = safe_field(raw, "ordinal");
    if (!ordinal)
        return ordinal.error();
    auto revision = safe_field(raw, "revision");
    if (!revision)
        return revision.error();
    return {};
}
} // namespace

SessionClient::SessionClient(std::shared_ptr<ApiClient> api, CancellationToken cancel)
    : api_(std::move(api)), cancel_(std::move(cancel)) {}
Result<SessionClient> SessionClient::create(std::shared_ptr<ApiClient> api,
                                            CancellationToken cancel) {
    if (!api || (api->family() != "sdk1" && api->family() != "sdk2-offload-v1"))
        return invalid("unsupported session family");
    return SessionClient(std::move(api), std::move(cancel));
}
const std::shared_ptr<ApiClient> &SessionClient::api() const noexcept { return api_; }
Result<ApiResponse> SessionClient::call(std::string_view op, CallOptions options) const {
    options.cancel = CancellationToken::combine(cancel_, options.cancel);
    if (options.cancel.is_cancelled())
        return Error{ErrorCode::cancelled, "session request cancelled"};
    return api_->call(op, std::move(options));
}
Result<void> SessionClient::discover_family(const WriteOptions &write) const {
    CallOptions options;
    options.cancel = write.cancel;
    options.deadline_ms = write.deadline_ms;
    options.query = {{"protocol", "sdk2-ext-v1"}};
    auto response = call("session.capabilities", std::move(options));
    if (!response)
        return response.error();
    auto obj = object_response(response.value(), 200);
    if (!obj)
        return obj.error();
    const auto &raw = obj.value();
    const auto &entries = field(raw, "contracts");
    if (!eq(field(raw, "protocol"), "sdk2-ext-v1") || !entries.is_array() ||
        entries.as_array().size() > 2)
        return contract("invalid session family discovery");
    std::set<std::string> families;
    for (const auto &entry : entries.as_array()) {
        auto family = required_string(entry, "contract");
        if (!family)
            return family.error();
        auto availability = required_string(entry, "availability");
        if (!availability)
            return availability.error();
        if (!families.insert(family.value()).second ||
            !((family.value() == "sdk1" && availability.value() == "legacy-complete") ||
              (family.value() == "sdk2-offload-v1" && availability.value() == "source-required")))
            return contract("unsupported or duplicate session family");
    }
    if (!families.count(api_->family()))
        return invalid("selected family unavailable; no write attempted");
    return {};
}
Result<Session> SessionClient::create(CreateOptions options) const {
    auto valid = validate_create(options, api_->family());
    if (!valid)
        return valid.error();
    if (!options.write.deadline_ms)
        options.write.deadline_ms = api_->default_deadline_ms();
    auto discovery = discover_family(options.write);
    if (!discovery)
        return discovery.error();
    auto call_options = write_options(options.write);
    call_options.body = create_json(options);
    auto response = call("session.create", std::move(call_options));
    if (!response)
        return response.error();
    if (response.value().status != 200 && response.value().status != 201)
        return contract("unexpected create status");
    auto obj = object_response(response.value(), response.value().status);
    if (!obj)
        return obj.error();
    const auto &raw = obj.value();
    auto id = required_string(raw, "sessionId");
    if (!id)
        return id.error();
    auto seq = safe_field(raw, "lastSeq");
    if (!seq)
        return seq.error();
    const auto &resumed = field(raw, "resumed");
    if (!resumed.is_bool())
        return contract("create response lacks resumed flag");
    if (options.resume && options.resume->session_id != id.value())
        return contract("resume changed session identity");
    auto family = check_family(raw, api_->family());
    if (!family)
        return family.error();
    return Session(*this, Created{std::move(id).value(), resumed.as_bool(), seq.value()});
}
Result<Session> SessionClient::attach(std::string id) const {
    auto valid = nonempty(id);
    if (!valid)
        return valid.error();
    Session out(*this, Created{std::move(id), false, 0});
    auto meta = out.meta();
    if (!meta)
        return meta.error();
    out.created_.last_seq = meta.value().last_seq;
    return out;
}
Result<Session> SessionClient::resume(std::string id, WriteOptions write) const {
    CreateOptions options;
    options.resume = ResumeReference{std::move(id)};
    options.write = std::move(write);
    return create(std::move(options));
}
Result<SessionList> SessionClient::list(std::uint64_t offset, std::uint64_t limit) const {
    if (offset > max_safe_integer || limit == 0 || limit > max_safe_integer)
        return invalid("invalid session list range");
    CallOptions options;
    options.query = {{"offset", std::to_string(offset)}, {"limit", std::to_string(limit)}};
    auto response = call("session.list", std::move(options));
    if (!response)
        return response.error();
    auto obj = object_response(response.value(), 200);
    if (!obj)
        return obj.error();
    auto total = safe_field(obj.value(), "total");
    if (!total)
        return total.error();
    const auto &entries = field(obj.value(), "sessions");
    if (!entries.is_array())
        return contract("session list lacks sessions");
    SessionList out;
    out.total = total.value();
    for (const auto &entry : entries.as_array()) {
        auto meta = read_meta(entry, api_->family());
        if (!meta)
            return meta.error();
        out.sessions.push_back(std::move(meta).value());
    }
    return out;
}
Session::Session(SessionClient client, Created created)
    : client_(std::move(client)), created_(std::move(created)) {}
const std::string &Session::id() const noexcept { return created_.session_id; }
const Created &Session::created() const noexcept { return created_; }
const std::shared_ptr<ApiClient> &Session::api() const noexcept { return client_.api(); }
Result<CapabilityClosure> Session::capabilities() const { return capabilities_for({}); }
Result<CapabilityClosure> Session::capabilities_for(const CallOptions &original) const {
    CallOptions options;
    options.parameters = {{"id", id()}};
    options.cancel = original.cancel;
    options.deadline_ms = original.deadline_ms;
    auto response = client_.call("discovery.session.capabilities", std::move(options));
    if (!response)
        return response.error();
    auto obj = object_response(response.value(), 200);
    if (!obj)
        return obj.error();
    auto closure = required_string(obj.value(), "closureId");
    if (!closure)
        return closure.error();
    if (!response.value().capability_closure ||
        *response.value().capability_closure != closure.value())
        return contract("capability closure header differs from body");
    const auto &entries = field(obj.value(), "operations");
    if (!entries.is_object())
        return contract("capability closure lacks operations");
    CapabilityClosure out;
    out.closure_id = std::move(closure).value();
    for (const auto &entry : entries.as_object()) {
        if (!entry.second.is_string() ||
            (entry.second.as_string() != "enabled" && entry.second.as_string() != "disabled" &&
             entry.second.as_string() != "unavailable"))
            return contract("invalid capability state");
        out.operations.emplace(entry.first, entry.second.as_string());
    }
    out.raw = std::move(obj).value();
    return out;
}
Result<Meta> Session::meta() const { return meta_query({}); }
Result<Meta> Session::application_prompt_meta() const {
    return meta_query({{"include", "applicationPrompt"}});
}
Result<Meta> Session::meta_query(std::map<std::string, std::string> query) const {
    auto response = read("session.get", std::move(query));
    if (!response)
        return response.error();
    auto obj = object_response(response.value(), 200);
    if (!obj)
        return obj.error();
    auto meta = read_meta(std::move(obj).value(), api()->family());
    if (!meta)
        return meta.error();
    if (meta.value().session_id != id())
        return contract("metadata changed session identity");
    return meta;
}
Result<ApiResponse> Session::read(std::string_view op,
                                  std::map<std::string, std::string> query) const {
    CallOptions options;
    options.parameters = {{"id", id()}};
    options.query = std::move(query);
    return client_.call(op, std::move(options));
}
Result<ApiResponse> Session::write(std::string_view op, std::optional<Json> body,
                                   WriteOptions write,
                                   std::optional<std::pair<std::string, std::string>> extra) const {
    auto options = write_options(write);
    options.body = std::move(body);
    return write_call(op, std::move(options), std::move(extra));
}
Result<ApiResponse>
Session::write_call(std::string_view op, CallOptions options,
                    std::optional<std::pair<std::string, std::string>> extra) const {
    if (!options.deadline_ms)
        options.deadline_ms = api()->default_deadline_ms();
    auto closure = capabilities_for(options);
    if (!closure)
        return closure.error();
    auto found = closure.value().operations.find(std::string(op));
    if (found == closure.value().operations.end() || found->second != "enabled")
        return invalid("operation is not enabled; no write attempted");
    options.parameters = {{"id", id()}};
    if (extra)
        options.parameters.insert(*extra);
    options.capability_closure = closure.value().closure_id;
    if (op == "session.audio.speak" || op == "session.audio.transcribe")
        options.max_response_bytes = media_bytes;
    return client_.call(op, std::move(options));
}
Result<Accepted> Session::accepted(std::string_view op, Json body, WriteOptions options,
                                   bool with_session,
                                   std::optional<std::pair<std::string, std::string>> extra) const {
    auto response = write(op, std::move(body), std::move(options), std::move(extra));
    if (!response)
        return response.error();
    return accepted_response(response.value(), id(), with_session);
}
Result<Accepted> Session::send(std::string prompt, WriteOptions write) const {
    if (blank(prompt))
        return invalid("prompt must not be blank");
    return accepted("session.message.send", Json::object({{"prompt", std::move(prompt)}}),
                    std::move(write), true);
}
Result<Accepted> Session::send_blocks(std::vector<Block> blocks, WriteOptions write) const {
    auto valid = validate_blocks(blocks, false);
    if (!valid)
        return valid.error();
    return accepted("session.message.send", Json::object({{"blocks", blocks_json(blocks)}}),
                    std::move(write), true);
}
Result<Accepted> Session::interrupt(WriteOptions write) const {
    return accepted("session.interrupt", Json::object(), std::move(write), true);
}
Result<Accepted> Session::close(WriteOptions write) const {
    auto options = write_options(write);
    options.parameters = {{"id", id()}};
    auto response = client_.call("session.close", std::move(options));
    if (!response)
        return response.error();
    return accepted_response(response.value(), id(), true);
}
Result<Accepted> Session::permission(std::string ticket, std::string digest, std::string verdict,
                                     WriteOptions write) const {
    if (!nonempty(ticket) || !nonempty(digest) || (verdict != "allow" && verdict != "deny"))
        return invalid("invalid permission decision");
    return accepted("session.permission.decide",
                    Json::object({{"digest", std::move(digest)}, {"verdict", std::move(verdict)}}),
                    std::move(write), false, std::make_pair("ticketId", std::move(ticket)));
}
Result<Accepted> Session::answer(std::string ticket, std::vector<Answer> answers,
                                 WriteOptions write) const {
    if (!nonempty(ticket) || answers.empty())
        return invalid("question ticket and answers are required");
    Json::Array array;
    for (const auto &answer : answers) {
        if (!nonempty(answer.question_id))
            return invalid("question identity is required");
        Json::Array selected;
        for (const auto &option : answer.selected_option_ids)
            selected.emplace_back(option);
        auto v = Json::object(
            {{"questionId", answer.question_id}, {"selectedOptionIds", Json(std::move(selected))}});
        optional(v, "freeText", answer.free_text);
        array.push_back(std::move(v));
    }
    return accepted("session.question.answer", Json::object({{"answers", Json(std::move(array))}}),
                    std::move(write), false, std::make_pair("ticketId", std::move(ticket)));
}
Result<Accepted> Session::tool_result(std::string call_id, Json receipt, WriteOptions write) const {
    if (!nonempty(call_id) || !receipt.is_object())
        return invalid("tool receipt requires identity and object");
    return accepted("session.tool.result", std::move(receipt), std::move(write), false,
                    std::make_pair("targetId", std::move(call_id)));
}
Result<Json> Session::history(std::uint64_t offset, std::uint64_t limit) const {
    if (offset > max_safe_integer || limit > max_safe_integer)
        return invalid("invalid history range");
    auto response = read("session.history.read",
                         {{"offset", std::to_string(offset)}, {"limit", std::to_string(limit)}});
    if (!response)
        return response.error();
    return object_response(response.value(), 200);
}
Result<Json> Session::input_capabilities() const {
    auto response = read("session.input.capabilities");
    if (!response)
        return response.error();
    return object_response(response.value(), 200);
}
Result<Json> Session::submit_input(Input input, WriteOptions write_options_value) const {
    auto valid = validate_input(input);
    if (!valid)
        return valid.error();
    if (!write_options_value.deadline_ms)
        write_options_value.deadline_ms = api()->default_deadline_ms();
    if (input.ack && *input.ack == "durable") {
        CallOptions options;
        options.cancel = write_options_value.cancel;
        options.deadline_ms = write_options_value.deadline_ms;
        options.parameters = {{"id", id()}};
        auto response = client_.call("session.input.capabilities", std::move(options));
        if (!response)
            return response.error();
        auto obj = object_response(response.value(), 200);
        if (!obj)
            return obj.error();
        const auto &durable = field(obj.value(), "durableAck");
        if (!durable.is_bool() || !durable.as_bool())
            return invalid("durable ACK unavailable; no write attempted");
    }
    auto response =
        write("session.input.submit", input_json(input), std::move(write_options_value));
    if (!response)
        return response.error();
    auto obj = object_response(response.value(), 202);
    if (!obj)
        return obj.error();
    if (!eq(field(obj.value(), "outcome"), "accepted"))
        return contract("input was not accepted");
    auto checked =
        check_receipt(field(obj.value(), "receipt"), id(), input.input_id, input.target, input.ack);
    if (!checked)
        return checked.error();
    return obj;
}
Result<Json> Session::input_status(std::string input, InputTarget target) const {
    if (!nonempty(input) || !nonempty(target.history_epoch) || !nonempty(target.turn_id))
        return invalid("input target identity is required");
    CallOptions options;
    options.parameters = {{"id", id()}, {"targetId", input}};
    options.query = {{"historyEpoch", target.history_epoch}, {"turnId", target.turn_id}};
    auto response = client_.call("session.input.status", std::move(options));
    if (!response)
        return response.error();
    auto obj = object_response(response.value(), 200);
    if (!obj)
        return obj.error();
    auto checked = check_receipt(field(obj.value(), "receipt"), id(), input, target, {});
    if (!checked)
        return checked.error();
    return obj;
}
} // namespace tansr::session
