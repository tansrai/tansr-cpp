#include "internal.hpp"
#include <cstdio>

namespace tansr::executor {
Platform Platform::current() {
    Platform p;
#if defined(_WIN32)
    p.platform = "windows";
#elif defined(__APPLE__)
    p.platform = "macos";
#elif defined(__linux__)
    p.platform = "linux";
#else
    p.platform = "unknown";
#endif
#if defined(_M_X64) || defined(__x86_64__)
    p.arch = "amd64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    p.arch = "arm64";
#else
    p.arch = "unknown";
#endif
    p.language = "cpp";
    p.runtime_version = "native";
    p.adapter_version = "cpp-executor/0.1.0";
    return p;
}
ToolFailure ToolFailure::rejected(std::string code) { return {Kind::rejected, std::move(code)}; }
ToolFailure ToolFailure::unknown(std::string code) { return {Kind::unknown, std::move(code)}; }
Json to_json(const Operation &x) {
    return Json::object({{"protocol", protocol},
                         {"operationId", x.operation_id},
                         {"sessionId", x.session_id},
                         {"scope", detail::json(x.scope)},
                         {"binding", detail::json(x.binding)},
                         {"toolName", x.tool_name},
                         {"request", detail::json(x.request)},
                         {"digest", x.digest},
                         {"expiresAt", x.expires_at}});
}
Json to_json(const Receipt &x) {
    return Json::object({{"protocol", protocol},
                         {"executorId", x.executor_id},
                         {"connectionId", x.connection_id},
                         {"operationId", x.operation_id},
                         {"digest", x.digest},
                         {"status", x.status},
                         {"result", x.result ? detail::json(*x.result) : Json()},
                         {"errorCode", detail::optional_string(x.error_code)}});
}
Result<std::string> operation_digest(const Operation &op) {
    auto v = to_json(op);
    v.set("digest", Json(std::string(64, '0')));
    auto checked = validate_wire(protocol, "ExecutionOperation", v);
    if (!checked)
        return checked.error();
    auto &o = v.as_object();
    o.erase(std::remove_if(o.begin(), o.end(), [](const auto &p) { return p.first == "digest"; }),
            o.end());
    return canonical::digest("tansr.sdk2.execution.v1", v);
}
Result<void> validate_operation(const Operation &op) {
    auto digest = operation_digest(op);
    if (!digest)
        return digest.error();
    if (digest.value() != op.digest)
        return detail::invalid("operation digest");
    auto expires = detail::expiry(op.expires_at);
    if (!expires)
        return expires.error();
    if (op.request.operation == "tool.invoke") {
        const auto *n = op.request.args.find("name");
        const auto *a = op.request.args.find("argsJson");
        if (!n || !n->is_string() || n->as_string() != op.tool_name || !a || !a->is_string())
            return detail::invalid("substituted tool name or arguments");
        auto parsed = parse_tool_arguments(a->as_string());
        if (!parsed)
            return parsed.error();
    }
    return {};
}
Result<void> validate_receipt(const Operation &op, const Receipt &r) {
    auto valid = validate_operation(op);
    if (!valid)
        return valid;
    valid = validate_wire(protocol, "ExecutionReceiptRequest", to_json(r));
    if (!valid)
        return valid;
    if (r.operation_id != op.operation_id || r.digest != op.digest ||
        r.executor_id != op.binding.target.executor_id ||
        r.connection_id != op.binding.target.connection_id)
        return detail::invalid("receipt identity");
    if (r.status != "completed") {
        if (r.result || !r.error_code)
            return detail::invalid("non-completed receipt");
        return {};
    }
    if (!r.result || r.error_code || r.result->operation != op.request.operation ||
        r.result->operation != "tool.invoke")
        return detail::invalid("unsupported result");
    const auto *text = r.result->args.find("resultJson");
    if (!text || !text->is_string())
        return detail::invalid("missing result JSON");
    auto parsed = parse_tool_arguments(text->as_string());
    if (!parsed)
        return parsed.error();
    return verify_tool_result(parsed.value());
}
} // namespace tansr::executor

namespace tansr::executor::detail {
Json json(const Scope &x) {
    return Json::object({{"applicationScopeId", x.application_scope_id},
                         {"endUserId", x.end_user_id},
                         {"authorizationRevision", x.authorization_revision}});
}
Json json(const Platform &x) {
    return Json::object({{"platform", x.platform},
                         {"arch", x.arch},
                         {"language", x.language},
                         {"runtimeVersion", x.runtime_version},
                         {"adapterVersion", x.adapter_version}});
}
Json json(const Workspace &x) {
    return Json::object({{"workspaceId", x.workspace_id}, {"revision", x.revision}});
}
Json json(const Interpreter &x) {
    return Json::object({{"id", x.id}, {"revision", x.revision}, {"hostShell", x.host_shell}});
}
Json json(const Target &x) {
    auto j = Json::object({{"executorId", x.executor_id},
                           {"connectionId", x.connection_id},
                           {"connectionRevision", x.connection_revision},
                           {"workspaceId", x.workspace_id},
                           {"workspaceRevision", x.workspace_revision}});
    if (x.interpreter)
        j.set("interpreter", json(*x.interpreter));
    return j;
}
Json json(const Binding &x) {
    return Json::object(
        {{"bindingId", x.binding_id}, {"revision", x.revision}, {"target", json(x.target)}});
}
Json json(const Registration &x) {
    Json::Array ws, ops, ts;
    for (const auto &v : x.workspaces)
        ws.push_back(json(v));
    for (const auto &v : x.operations)
        ops.emplace_back(v);
    for (const auto &v : x.tools)
        ts.push_back(Json::object({{"name", v.name}, {"definitionDigest", v.definition_digest}}));
    auto j = Json::object({{"protocol", protocol},
                           {"executorId", x.executor_id},
                           {"platform", json(x.platform)},
                           {"workspaces", Json(std::move(ws))},
                           {"operations", Json(std::move(ops))}});
    if (!ts.empty())
        j.set("tools", Json(std::move(ts)));
    if (x.interpreter)
        j.set("interpreter", json(*x.interpreter));
    return j;
}
Json json(const Connection &x) {
    return Json::object({{"protocol", protocol},
                         {"executorId", x.executor_id},
                         {"connectionId", x.connection_id},
                         {"connectionRevision", x.connection_revision},
                         {"expiresAt", x.expires_at},
                         {"heartbeatAfterMs", x.heartbeat_after_ms}});
}
Json json(const Resource &x) {
    return Json::object({{"operation", x.operation}, {"args", x.args}});
}
Json json(const TerminalSessionReference &x) {
    return Json::object({{"sessionContract", x.session_contract}, {"sessionId", x.session_id}});
}
Json json(const OutputOperationReference &x) {
    return Json::object({{"operationId", x.operation_id}, {"requestDigest", x.request_digest}});
}
Json json(const OutputLimits &x) {
    return Json::object({{"maxControlBytes", std::uint64_t(x.max_control_bytes)},
                         {"maxBlockBytes", std::uint64_t(x.max_block_bytes)},
                         {"maxBatchBytes", std::uint64_t(x.max_batch_bytes)},
                         {"maxPendingBytes", std::uint64_t(x.max_pending_bytes)},
                         {"maxRetainedBytes", std::uint64_t(x.max_retained_bytes)}});
}
Json json(const OutputSeal &x) {
    return Json::object({{"lastSeq", optional_string(x.last_seq)},
                         {"totalBytes", x.total_bytes},
                         {"payloadDigest", x.payload_digest},
                         {"truncated", x.truncated}});
}
Json json(const OutputStatus &x) {
    return Json::object({{"contract", "terminal-services-v1"},
                         {"operation", json(x.operation)},
                         {"state", x.state},
                         {"acceptedThrough", optional_string(x.accepted_through)},
                         {"durableThrough", optional_string(x.durable_through)},
                         {"retainedFrom", optional_string(x.retained_from)},
                         {"nextByteOffset", optional_string(x.next_byte_offset)},
                         {"seal", x.seal ? json(*x.seal) : Json()}});
}
namespace {
std::string s(const Json &x, std::string_view k) { return x.at(k).as_string(); }
Platform platform(const Json &x) {
    return {s(x, "platform"), s(x, "arch"), s(x, "language"), s(x, "runtimeVersion"),
            s(x, "adapterVersion")};
}
Interpreter interpreter(const Json &x) { return {s(x, "id"), s(x, "revision"), s(x, "hostShell")}; }
Binding binding(const Json &x) {
    const auto &t = x.at("target");
    Target target{s(t, "executorId"),  s(t, "connectionId"),      s(t, "connectionRevision"),
                  s(t, "workspaceId"), s(t, "workspaceRevision"), {}};
    if (auto i = t.find("interpreter"); i && !i->is_null())
        target.interpreter = interpreter(*i);
    return {s(x, "bindingId"), s(x, "revision"), std::move(target)};
}
Resource resource(const Json &x) { return {s(x, "operation"), x.at("args")}; }
} // namespace
Result<Connection> connection(const Json &x) {
    try {
        return Connection{s(x, "executorId"), s(x, "connectionId"), s(x, "connectionRevision"),
                          s(x, "expiresAt"), x.at("heartbeatAfterMs").as_u64()};
    } catch (...) {
        return invalid("connection shape");
    }
}
Result<Capabilities> capabilities(const Json &x) {
    try {
        Capabilities c;
        c.session_id = s(x, "sessionId");
        c.capability_revision = s(x, "capabilityRevision");
        if (auto p = x.find("platform"); p && !p->is_null())
            c.platform = platform(*p);
        if (auto p = x.find("binding"); p && !p->is_null())
            c.binding = binding(*p);
        std::set<std::string> names;
        for (const auto &t : x.at("effectiveTools").as_array()) {
            auto name = s(t, "name");
            if (!names.insert(name).second)
                return invalid("duplicate effective tool");
            c.effective_tools.push_back({name, s(t, "executionKind"), t.at("available").as_bool(),
                                         read_optional(t, "unavailableReason")});
        }
        return c;
    } catch (...) {
        return invalid("capability shape");
    }
}
Result<Operation> operation(const Json &x) {
    auto checked = validate_wire(protocol, "ExecutionOperation", x);
    if (!checked)
        return checked.error();
    try {
        const auto &a = x.at("scope");
        Operation op{s(x, "operationId"),
                     s(x, "sessionId"),
                     {s(a, "applicationScopeId"), s(a, "endUserId"), s(a, "authorizationRevision")},
                     binding(x.at("binding")),
                     s(x, "toolName"),
                     resource(x.at("request")),
                     s(x, "digest"),
                     s(x, "expiresAt")};
        auto valid = validate_operation(op);
        if (!valid)
            return valid.error();
        return op;
    } catch (...) {
        return invalid("operation shape");
    }
}
Result<Receipt> receipt(const Json &x) {
    auto checked = validate_wire(protocol, "ExecutionReceiptRequest", x);
    if (!checked)
        return checked.error();
    try {
        Receipt r{s(x, "executorId"),
                  s(x, "connectionId"),
                  s(x, "operationId"),
                  s(x, "digest"),
                  s(x, "status"),
                  {},
                  read_optional(x, "errorCode")};
        if (auto p = x.find("result"); p && !p->is_null())
            r.result = resource(*p);
        return r;
    } catch (...) {
        return invalid("receipt shape");
    }
}
Result<Status> status(const Json &x) {
    auto checked = validate_wire(protocol, "ExecutionStatus", x);
    if (!checked)
        return checked.error();
    auto op = operation(x.at("operation"));
    if (!op)
        return op.error();
    try {
        Status st{std::move(op.value()), s(x, "status"), {}};
        if (auto p = x.find("receipt"); p && !p->is_null()) {
            auto r = receipt(*p);
            if (!r)
                return r.error();
            st.receipt = std::move(r.value());
        }
        return st;
    } catch (...) {
        return invalid("status shape");
    }
}
Result<OutputStatus> output_status(const Json &x) {
    auto checked = validate_wire("terminal-services-v1", "OutputStatus", x);
    if (!checked)
        return checked.error();
    try {
        const auto &o = x.at("operation");
        OutputStatus st{{s(o, "operationId"), s(o, "requestDigest")},
                        s(x, "state"),
                        read_optional(x, "acceptedThrough"),
                        read_optional(x, "durableThrough"),
                        read_optional(x, "retainedFrom"),
                        read_optional(x, "nextByteOffset"),
                        {}};
        if (auto p = x.find("seal"); p && !p->is_null())
            st.seal = OutputSeal{read_optional(*p, "lastSeq"), s(*p, "totalBytes"),
                                 s(*p, "payloadDigest"), p->at("truncated").as_bool()};
        auto valid = validate_output(st);
        if (!valid)
            return valid.error();
        return st;
    } catch (...) {
        return invalid("output status shape");
    }
}
Result<std::int64_t> expiry(std::string_view x) {
    // RFC3339 日历校验和明确偏移；不经本机时区或本地化日期解析。
    if (x.size() < 20)
        return invalid("invalid expiry");
    auto n = [&](std::size_t p, std::size_t count) -> int {
        int v = 0;
        if (p + count > x.size())
            return -1;
        for (std::size_t i = p; i < p + count; ++i) {
            if (x[i] < '0' || x[i] > '9')
                return -1;
            v = v * 10 + x[i] - '0';
        }
        return v;
    };
    const int y = n(0, 4), m = n(5, 2), d = n(8, 2), h = n(11, 2), min = n(14, 2), sec = n(17, 2);
    if (x[4] != '-' || x[7] != '-' || (x[10] != 'T' && x[10] != 't') || x[13] != ':' ||
        x[16] != ':' || y < 1970 || m < 1 || m > 12 || d < 1 || h < 0 || h > 23 || min < 0 ||
        min > 59 || sec < 0 || sec > 59)
        return invalid("invalid expiry");
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    if (d > days[m - 1] + (m == 2 && leap ? 1 : 0))
        return invalid("invalid expiry day");
    std::size_t pos = 19;
    int ms = 0;
    if (pos < x.size() && x[pos] == '.') {
        ++pos;
        auto start = pos;
        int scale = 100;
        while (pos < x.size() && x[pos] >= '0' && x[pos] <= '9') {
            if (scale) {
                ms += (x[pos] - '0') * scale;
                scale /= 10;
            }
            ++pos;
        }
        if (pos == start)
            return invalid("invalid expiry fraction");
    }
    int offset = 0;
    if (pos < x.size() && (x[pos] == 'Z' || x[pos] == 'z'))
        ++pos;
    else if (pos < x.size() && (x[pos] == '+' || x[pos] == '-')) {
        const int sign = x[pos] == '+' ? 1 : -1;
        int oh = n(pos + 1, 2), om = n(pos + 4, 2);
        if (pos + 6 != x.size() || x[pos + 3] != ':' || oh < 0 || oh > 23 || om < 0 || om > 59)
            return invalid("invalid expiry offset");
        offset = sign * (oh * 60 + om);
        pos += 6;
    } else
        return invalid("missing expiry offset");
    if (pos != x.size())
        return invalid("expiry trailing bytes");
    const int yy = y - (m <= 2 ? 1 : 0), era = yy / 400, yoe = yy - era * 400;
    const int mp = m + (m > 2 ? -3 : 9), doy = (153 * mp + 2) / 5 + d - 1,
              doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t epoch_days = std::int64_t(era) * 146097 + doe - 719468;
    return ((epoch_days * 24 + h) * 60 + min - offset) * 60000 + sec * 1000 + ms;
}
Result<void> live(const Connection &c) {
    auto v = validate_wire(protocol, "ExecutorConnection", json(c));
    if (!v)
        return v;
    auto t = expiry(c.expires_at);
    if (!t)
        return t.error();
    if (t.value() <= unix_time_ms())
        return invalid("connection lease expired");
    return {};
}
Result<void> registration(const Registration &r) {
    auto v = validate_wire(protocol, "ExecutorRegistrationRequest", json(r));
    if (!v)
        return v;
    std::set<std::string> ws, ts, ops;
    for (const auto &w : r.workspaces)
        if (!ws.insert(w.workspace_id).second)
            return invalid("duplicate workspace");
    for (const auto &t : r.tools)
        if (!ts.insert(t.name).second)
            return invalid("duplicate tool");
    for (const auto &o : r.operations)
        if (!ops.insert(o).second)
            return invalid("duplicate operation");
    if ((ops.count("tool.invoke") != 0) == r.tools.empty())
        return invalid("registration tool mismatch");
    return {};
}
Receipt receipt_for(const Operation &op, std::string status, std::optional<std::string> error,
                    std::optional<Resource> result) {
    return {op.binding.target.executor_id,
            op.binding.target.connection_id,
            op.operation_id,
            op.digest,
            std::move(status),
            std::move(result),
            std::move(error)};
}
Result<Json> call(const std::shared_ptr<ApiClient> &api, std::string_view op,
                  std::string_view family, std::string_view schema, CallOptions options,
                  int expected) {
    if (!api)
        return invalid("missing API client");
    if (!options.max_response_bytes)
        options.max_response_bytes = control_bytes;
    auto r = api->call(op, std::move(options));
    if (!r)
        return r.error();
    if (r.value().status != expected || r.value().content_type != "application/json")
        return invalid("HTTP status or media type");
    auto v = validate_wire(family, schema, r.value().body);
    if (!v)
        return v.error();
    return r.value().body;
}
} // namespace tansr::executor::detail
