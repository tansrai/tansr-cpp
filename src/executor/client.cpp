#include "internal.hpp"

namespace tansr::executor {
Client::Client(std::shared_ptr<ApiClient> api, Scope scope)
    : api_(std::move(api)), scope_(std::move(scope)) {}
Result<std::shared_ptr<Client>> Client::create(std::shared_ptr<ApiClient> api, Scope scope) {
    if (!api)
        return detail::invalid("missing API client");
    auto r = validate_wire(protocol, "Scope", detail::json(scope));
    if (!r)
        return r.error();
    return std::shared_ptr<Client>(new Client(std::move(api), std::move(scope)));
}
const Scope &Client::scope() const noexcept { return scope_; }
const std::shared_ptr<ApiClient> &Client::api() const noexcept { return api_; }
Result<Connection> Client::register_executor(const Registration &r,
                                             CancellationToken cancel) const {
    auto check = detail::registration(r);
    if (!check)
        return check.error();
    CallOptions o;
    o.body = detail::json(r);
    o.cancel = cancel;
    auto v = detail::call(api_, "executor.register", protocol, "ExecutorConnection", o, 201);
    if (!v)
        return v.error();
    auto c = detail::connection(v.value());
    if (!c)
        return c.error();
    if (c.value().executor_id != r.executor_id)
        return detail::invalid("executor identity");
    check = detail::live(c.value());
    if (!check)
        return check.error();
    return c;
}
Result<Connection> Client::heartbeat(const Connection &c, CancellationToken cancel) const {
    auto check = detail::live(c);
    if (!check)
        return check.error();
    CallOptions o;
    o.parameters = {{"id", c.executor_id}};
    o.body = Json::object(
        {{"protocol", protocol}, {"executorId", c.executor_id}, {"connectionId", c.connection_id}});
    o.cancel = cancel;
    o.deadline_ms = detail::expiry(c.expires_at).value();
    auto v = detail::call(api_, "executor.heartbeat", protocol, "ExecutorConnection", o);
    if (!v)
        return v.error();
    auto r = detail::connection(v.value());
    if (!r)
        return r.error();
    if (r.value().executor_id != c.executor_id || r.value().connection_id != c.connection_id)
        return detail::invalid("heartbeat identity");
    check = detail::live(r.value());
    if (!check)
        return check.error();
    return r;
}
Result<Batch> Client::poll(const Connection &c, CancellationToken cancel) const {
    auto check = detail::live(c);
    if (!check)
        return check.error();
    CallOptions o;
    o.parameters = {{"id", c.executor_id}};
    o.query = {{"connectionId", c.connection_id}};
    o.cancel = cancel;
    o.deadline_ms = detail::expiry(c.expires_at).value();
    auto v = detail::call(api_, "executor.operations.poll", protocol, "ExecutionBatch", o);
    if (!v)
        return v.error();
    try {
        Batch b{
            v.value().at("executorId").as_string(), v.value().at("connectionId").as_string(), {}};
        if (b.executor_id != c.executor_id || b.connection_id != c.connection_id)
            return detail::invalid("poll identity");
        std::set<std::string> seen;
        for (const auto &e : v.value().at("operations").as_array()) {
            auto op = detail::operation(e);
            if (!op)
                return op.error();
            if (!seen.insert(op.value().operation_id).second)
                return detail::invalid("duplicate operation");
            check = check_identity(c, op.value());
            if (!check)
                return check.error();
            b.operations.push_back(std::move(op.value()));
        }
        return b;
    } catch (...) {
        return detail::invalid("batch shape");
    }
}
Result<Capabilities> Client::initialize(const std::string &session, const Platform &platform,
                                        std::optional<std::vector<std::string>> tools,
                                        const std::string &closure,
                                        CancellationToken cancel) const {
    auto body = Json::object(
        {{"protocol", protocol}, {"sessionId", session}, {"platform", detail::json(platform)}});
    if (tools) {
        std::set<std::string> seen;
        Json::Array a;
        for (const auto &t : *tools) {
            if (!seen.insert(t).second)
                return detail::invalid("duplicate tool");
            a.emplace_back(t);
        }
        body.set("requestedTools", Json(std::move(a)));
    }
    auto check = validate_wire(protocol, "SessionInitializeRequest", body);
    if (!check)
        return check.error();
    CallOptions o;
    o.parameters = {{"id", session}};
    o.body = body;
    o.capability_closure = closure;
    o.cancel = cancel;
    auto v =
        detail::call(api_, "execution.initialize", protocol, "SessionExecutionCapabilities", o);
    if (!v)
        return v.error();
    auto c = detail::capabilities(v.value());
    if (!c)
        return c.error();
    if (c.value().session_id != session || !c.value().platform ||
        !detail::equal(detail::json(*c.value().platform), detail::json(platform)))
        return detail::invalid("initialization identity");
    return c;
}
Result<Capabilities> Client::execution_capabilities(const std::string &session,
                                                    CancellationToken cancel) const {
    CallOptions o;
    o.parameters = {{"id", session}};
    o.cancel = cancel;
    auto v =
        detail::call(api_, "execution.capabilities", protocol, "SessionExecutionCapabilities", o);
    if (!v)
        return v.error();
    auto c = detail::capabilities(v.value());
    if (!c)
        return c.error();
    if (c.value().session_id != session)
        return detail::invalid("session identity");
    return c;
}
Result<Capabilities> Client::bind(const std::string &session, const Connection &c,
                                  const Workspace &w, const std::string &revision,
                                  const std::string &closure, CancellationToken cancel) const {
    auto live = detail::live(c);
    if (!live)
        return live.error();
    auto body = Json::object({{"protocol", protocol},
                              {"sessionId", session},
                              {"executorId", c.executor_id},
                              {"connectionId", c.connection_id},
                              {"workspaceId", w.workspace_id},
                              {"expectedCapabilityRevision", revision}});
    auto check = validate_wire(protocol, "ExecutionBindingRequest", body);
    if (!check)
        return check.error();
    CallOptions o;
    o.parameters = {{"id", session}};
    o.body = body;
    o.capability_closure = closure;
    o.cancel = cancel;
    o.deadline_ms = detail::expiry(c.expires_at).value();
    auto v =
        detail::call(api_, "execution.binding.create", protocol, "SessionExecutionCapabilities", o);
    if (!v)
        return v.error();
    auto out = detail::capabilities(v.value());
    if (!out)
        return out.error();
    if (!out.value().binding)
        return detail::invalid("binding missing");
    const auto &t = out.value().binding->target;
    if (out.value().session_id != session || t.executor_id != c.executor_id ||
        t.connection_id != c.connection_id || t.connection_revision != c.connection_revision ||
        t.workspace_id != w.workspace_id || t.workspace_revision != w.revision)
        return detail::invalid("binding identity");
    return out;
}
Result<TerminalOptions> Client::negotiate_output(const TerminalSessionReference &session,
                                                 const Binding &binding,
                                                 const std::string &request_id,
                                                 CancellationToken cancel) const {
    auto body = Json::object({{"contract", "terminal-services-v1"},
                              {"requestId", request_id},
                              {"session", detail::json(session)},
                              {"executionBinding", detail::json(binding)},
                              {"required", Json::array({"execution-stream-v1"})},
                              {"optional", Json::array()}});
    auto check = validate_wire("terminal-services-v1", "BindingRequest", body);
    if (!check)
        return check.error();
    CallOptions o;
    o.body = body;
    o.cancel = cancel;
    auto v =
        detail::call(api_, "terminal.binding.create", "terminal-services-v1", "BindingResponse", o);
    if (!v)
        return v.error();
    try {
        const auto &x = v.value();
        bool found = false;
        for (const auto &a : x.at("accepted").as_array())
            if (a.is_string() && a.as_string() == "execution-stream-v1")
                found = true;
        if (x.at("requestId").as_string() != request_id ||
            !detail::equal(x.at("session"), detail::json(session)) ||
            !detail::equal(x.at("executionBinding"), detail::json(binding)) ||
            !detail::equal(x.at("scope"), detail::json(scope_)) || !found)
            return detail::invalid("output binding identity or capability");
        const auto &l = x.at("limits");
        return TerminalOptions{session.session_contract,
                               {std::size_t(l.at("maxControlBytes").as_u64()),
                                std::size_t(l.at("maxBlockBytes").as_u64()),
                                std::size_t(l.at("maxBatchBytes").as_u64()),
                                std::size_t(l.at("maxPendingBytes").as_u64()),
                                std::size_t(l.at("maxRetainedBytes").as_u64())}};
    } catch (...) {
        return detail::invalid("output binding shape");
    }
}
Result<void> Client::check_identity(const Connection &c, const Operation &op) const {
    const auto &t = op.binding.target;
    if (!detail::equal(detail::json(op.scope), detail::json(scope_)) ||
        t.executor_id != c.executor_id || t.connection_id != c.connection_id ||
        t.connection_revision != c.connection_revision)
        return detail::invalid("operation identity or generation");
    return {};
}
Result<void> Client::validate_status(const Status &s) const {
    auto check = validate_operation(s.operation);
    if (!check)
        return check;
    if (s.operation.scope.application_scope_id != scope_.application_scope_id ||
        s.operation.scope.end_user_id != scope_.end_user_id)
        return detail::invalid("status scope");
    if (s.status == "pending" || (s.status == "unknown" && !s.receipt)) {
        if (s.receipt)
            return detail::invalid("pending receipt");
        return {};
    }
    if (!s.receipt || s.receipt->status != s.status)
        return detail::invalid("receipt status");
    return validate_receipt(s.operation, *s.receipt);
}
Result<Status> Client::status(const std::string &session, const std::string &id,
                              CancellationToken cancel,
                              std::optional<std::int64_t> deadline) const {
    CallOptions o;
    o.parameters = {{"id", session}, {"targetId", id}};
    o.cancel = cancel;
    o.deadline_ms = deadline;
    auto v = detail::call(api_, "execution.status", protocol, "ExecutionStatus", o);
    if (!v)
        return v.error();
    auto s = detail::status(v.value());
    if (!s)
        return s.error();
    auto check = validate_status(s.value());
    if (!check)
        return check.error();
    if (s.value().operation.session_id != session || s.value().operation.operation_id != id)
        return detail::invalid("status identity");
    return s;
}
Result<Status> Client::executor_status(const TerminalSessionReference &session, const Connection &c,
                                       const Operation &op, CancellationToken cancel) const {
    auto check = detail::live(c);
    if (!check)
        return check.error();
    check = validate_operation(op);
    if (!check)
        return check.error();
    check = check_identity(c, op);
    if (!check)
        return check.error();
    check = validate_wire("terminal-services-v1", "SessionReference", detail::json(session));
    if (!check)
        return check.error();
    if (session.session_id != op.session_id)
        return detail::invalid("terminal session identity");
    CallOptions o;
    o.parameters = {{"id", c.executor_id}, {"targetId", op.operation_id}};
    o.query = {{"sessionContract", session.session_contract},
               {"sessionId", session.session_id},
               {"requestDigest", op.digest},
               {"connectionId", c.connection_id}};
    o.cancel = cancel;
    o.deadline_ms =
        std::min(detail::expiry(c.expires_at).value(), detail::expiry(op.expires_at).value());
    auto v =
        detail::call(api_, "terminal.execution.state", "terminal-services-v1", "ExecutionState", o);
    if (!v)
        return v.error();
    if (!detail::equal(v.value().at("session"), detail::json(session)))
        return detail::invalid("terminal status session");
    auto s = detail::status(v.value().at("execution"));
    if (!s)
        return s.error();
    check = validate_status(s.value());
    if (!check)
        return check.error();
    if (!detail::equal(to_json(s.value().operation), to_json(op)))
        return detail::invalid("terminal status operation");
    return s;
}
Result<Status> Client::submit(const Operation &op, const Receipt &r,
                              CancellationToken cancel) const {
    if (op.scope.application_scope_id != scope_.application_scope_id ||
        op.scope.end_user_id != scope_.end_user_id)
        return detail::invalid("receipt scope");
    auto check = validate_receipt(op, r);
    if (!check)
        return check.error();
    CallOptions o;
    o.parameters = {{"id", r.executor_id}};
    o.body = to_json(r);
    o.cancel = cancel;
    auto v = detail::call(api_, "executor.receipt.submit", protocol, "ExecutionStatus", o);
    if (!v)
        return v.error();
    auto s = detail::status(v.value());
    if (!s)
        return s.error();
    check = validate_status(s.value());
    if (!check)
        return check.error();
    if (!detail::equal(to_json(s.value().operation), to_json(op)) || !s.value().receipt ||
        !detail::equal(to_json(*s.value().receipt), to_json(r)) || s.value().status != r.status)
        return detail::invalid("receipt response identity");
    return s;
}
} // namespace tansr::executor
