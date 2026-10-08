#include "internal.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace tansr::executor {
namespace {
thread_local const void *active_handler = nullptr;
struct CallbackGuard {
    const void *previous = active_handler;
    explicit CallbackGuard(const void *owner) { active_handler = owner; }
    ~CallbackGuard() { active_handler = previous; }
};
Receipt business_receipt(const Operation &op, const ToolResult &result) {
    if (const auto *value = std::get_if<Json>(&result)) {
        if (verify_tool_result(*value))
            return detail::receipt_for(
                op, "completed", {},
                Resource{"tool.invoke", Json::object({{"resultJson", value->dump()}})});
        return detail::receipt_for(op, "unknown", "invalid_tool_result");
    }
    const auto &error = std::get<ToolFailure>(result);
    if (error.kind == ToolFailure::Kind::rejected && !error.code.empty() &&
        error.code.size() <= 128)
        return detail::receipt_for(op, "failed", error.code);
    return detail::receipt_for(op, "unknown", "execution_outcome_unknown");
}
} // namespace
struct Runner::Impl {
    RunnerOptions options;
    mutable std::mutex connection_gate;
    std::mutex connect_gate;
    std::timed_mutex execute_gate;
    std::optional<Connection> connection;
    std::atomic<bool> running{false};
    explicit Impl(RunnerOptions o) : options(std::move(o)) {}
    Result<Connection> current() const {
        std::lock_guard<std::mutex> lock(connection_gate);
        if (!connection)
            return detail::invalid("executor not connected");
        auto valid = detail::live(*connection);
        if (!valid)
            return valid.error();
        return *connection;
    }
    Result<void> check(const Operation &op, CancellationToken cancel) const {
        if (cancel.is_cancelled())
            return Error{ErrorCode::cancelled, "execution cancelled"};
        auto before = current();
        if (!before)
            return before.error();
        auto valid = options.client->check_identity(before.value(), op);
        if (!valid)
            return valid;
        const auto &t = op.binding.target;
        bool workspace = false;
        for (const auto &w : options.registration.workspaces)
            if (w.workspace_id == t.workspace_id && w.revision == t.workspace_revision)
                workspace = true;
        if (!workspace)
            return detail::invalid("workspace not registered");
        auto expiry = detail::expiry(op.expires_at);
        if (!expiry)
            return expiry.error();
        auto lease = detail::expiry(before.value().expires_at);
        if (!lease)
            return lease.error();
        const auto deadline = std::min(expiry.value(), lease.value());
        if (deadline <= unix_time_ms())
            return detail::invalid("operation expired");
        const auto authorization_cancel =
            cancel.with_deadline(static_cast<std::uint64_t>(deadline));
        try {
            CallbackGuard callback(this);
            valid = options.authorize(op, authorization_cancel);
        } catch (...) {
            valid = Error{ErrorCode::permission, "authorization callback failed"};
        }
        if (!valid)
            return valid;
        if (cancel.is_cancelled())
            return Error{ErrorCode::cancelled, "execution cancelled"};
        if (unix_time_ms() >= deadline)
            return detail::invalid("authorization exceeded lease");
        auto after = current();
        if (!after)
            return after.error();
        if (before.value().executor_id != after.value().executor_id ||
            before.value().connection_id != after.value().connection_id ||
            before.value().connection_revision != after.value().connection_revision)
            return detail::invalid("connection changed during authorization");
        return {};
    }
    Result<Status> read_status(const Operation &op, CancellationToken cancel) const {
        auto c = current();
        if (!c)
            return c.error();
        auto lease = detail::expiry(c.value().expires_at), expires = detail::expiry(op.expires_at);
        if (!lease)
            return lease.error();
        if (!expires)
            return expires.error();
        Result<Status> s =
            options.restricted_status
                ? options.client->executor_status(
                      {options.terminal->session_contract, op.session_id}, c.value(), op, cancel)
                : options.client->status(op.session_id, op.operation_id, cancel,
                                         std::min(lease.value(), expires.value()));
        if (!s)
            return s.error();
        if (!detail::equal(to_json(s.value().operation), to_json(op)))
            return detail::invalid("substituted execution status");
        return s;
    }
    Result<void> renew(CancellationToken cancel) {
        auto before = current();
        if (!before)
            return before.error();
        auto after = options.client->heartbeat(before.value(), cancel);
        if (!after)
            return after.error();
        if (after.value().connection_revision != before.value().connection_revision)
            return detail::invalid("connection generation changed");
        std::lock_guard<std::mutex> lock(connection_gate);
        if (!connection || connection->connection_id != before.value().connection_id ||
            connection->connection_revision != before.value().connection_revision)
            return detail::invalid("connection changed during renewal");
        connection = std::move(after.value());
        return {};
    }
    Result<ExecutionOutcome> execute(Operation op, CancellationToken parent, bool monitored) {
        if (active_handler == this)
            return Error{ErrorCode::reentrant, "executor handler cannot wait on its runner"};
        CallbackGuard callback(this);
        std::unique_lock<std::timed_mutex> execute_lock(execute_gate, std::defer_lock);
        while (!execute_lock.try_lock_for(std::chrono::milliseconds(10)))
            if (parent.is_cancelled())
                return Error{ErrorCode::cancelled, "execution cancelled while queued"};
        auto valid = validate_operation(op);
        if (!valid)
            return valid.error();
        if (op.request.operation != "tool.invoke")
            return detail::invalid("unsupported resource operation");
        auto tool = options.tools.find(op.tool_name);
        if (tool == options.tools.end())
            return detail::invalid("tool not installed");
        const auto *declared = op.request.args.find("definitionDigest");
        if (!declared || !declared->is_string() ||
            declared->as_string() != tool->second.definition_digest)
            return detail::invalid("tool definition substituted");
        valid = check(op, parent);
        if (!valid)
            return valid.error();
        auto remote = read_status(op, parent);
        if (!remote)
            return remote.error();
        if (remote.value().status != "pending" && !remote.value().receipt)
            return detail::invalid("remote execution is not pending and has no receipt");
        std::optional<OutputStatus> output_state;
        std::optional<OutputOutcome> output_outcome;
        if (options.terminal) {
            auto c = current();
            if (!c)
                return c.error();
            auto state = detail::query_output(
                options.client->api(), {options.terminal->session_contract, op.session_id},
                {op.operation_id, op.digest}, options.terminal->limits.max_control_bytes, parent,
                std::min(detail::expiry(op.expires_at).value(),
                         detail::expiry(c.value().expires_at).value()));
            if (!state)
                return state.error();
            output_state = std::move(state.value());
            output_outcome = OutputOutcome{output_state->state == "complete" ||
                                               output_state->state == "truncated",
                                           output_state};
            if (options.require_output && remote.value().status == "pending" &&
                (output_state->state == "unavailable" || output_state->state == "gap"))
                return detail::invalid("required output window unavailable");
        }
        auto claim = options.journal->claim(op);
        if (!claim)
            return claim.error();
        if (claim.value().state == ClaimState::receipt) {
            if (!claim.value().receipt)
                return detail::invalid("journal receipt missing");
            const auto &r = *claim.value().receipt;
            valid = validate_receipt(op, r);
            if (!valid)
                return valid.error();
            if (remote.value().receipt &&
                !detail::equal(to_json(*remote.value().receipt), to_json(r)))
                return detail::invalid("local and remote receipts disagree");
            return ExecutionOutcome{r, output_outcome};
        }
        if (claim.value().state == ClaimState::pending) {
            auto r = remote.value().receipt.value_or(
                detail::receipt_for(op, "unknown", "execution_outcome_unknown"));
            valid = options.journal->complete(op, r);
            if (!valid)
                return valid.error();
            return ExecutionOutcome{r, output_outcome};
        }
        auto persist = [&](Receipt r) -> Result<ExecutionOutcome> {
            auto checked = validate_receipt(op, r);
            if (!checked)
                return checked.error();
            auto saved = options.journal->complete(op, r);
            if (!saved)
                return saved.error();
            return ExecutionOutcome{std::move(r), output_outcome};
        };
        if (remote.value().receipt)
            return persist(*remote.value().receipt);
        if (output_state &&
            (output_state->state == "receiving" || output_state->state == "complete" ||
             output_state->state == "truncated"))
            return persist(detail::receipt_for(op, "unknown", "remote_execution_not_pending"));
        CancellationSource stop;
        auto cancel = CancellationToken::combine(parent, stop.token());
        valid = check(op, cancel);
        if (!valid)
            return persist(detail::receipt_for(op, "failed", "authorization_rejected"));
        // 认领和宿主授权可能阻塞；旧 available 不能跨越此窗口继续授予输出能力。
        // 只收窄原能力，不把初次不可用、之后才出现的窗口当作新增授权。
        if (output_state && output_state->state == "available") {
            auto c = current();
            if (!c)
                return persist(detail::receipt_for(op, "failed", "authorization_rejected"));
            auto state = detail::query_output(
                options.client->api(), {options.terminal->session_contract, op.session_id},
                {op.operation_id, op.digest}, options.terminal->limits.max_control_bytes, cancel,
                std::min(detail::expiry(op.expires_at).value(),
                         detail::expiry(c.value().expires_at).value()));
            if (!state)
                return persist(detail::receipt_for(op, "failed", "output_window_unconfirmed"));
            output_state = std::move(state.value());
            output_outcome = OutputOutcome{output_state->state == "complete" ||
                                               output_state->state == "truncated",
                                           output_state};
            if (output_state->state != "available")
                return persist(detail::receipt_for(
                    op, output_state->state == "unavailable" ? "failed" : "unknown",
                    "output_window_changed"));
            // 查询同样可能等待网络；进入宿主前再核当前授权和连接代际。
            valid = check(op, cancel);
            if (!valid)
                return persist(detail::receipt_for(op, "failed", "authorization_rejected"));
        }
        auto current_connection = current();
        if (!current_connection)
            return persist(detail::receipt_for(op, "failed", "authorization_rejected"));
        auto expires = detail::expiry(op.expires_at);
        if (!expires)
            return expires.error();
        auto lease = detail::expiry(current_connection.value().expires_at);
        if (!lease)
            return lease.error();
        const auto deadline =
            monitored ? expires.value() : std::min(expires.value(), lease.value());
        auto arguments = parse_tool_arguments(op.request.args.at("argsJson").as_string());
        if (!arguments)
            return persist(detail::receipt_for(op, "failed", "invalid_arguments"));
        std::shared_ptr<OutputWriter> output;
        if (output_state && output_state->state == "available") {
            OutputOptions o{{options.terminal->session_contract, op.session_id},
                            {op.operation_id, op.digest},
                            current_connection.value().executor_id,
                            current_connection.value().connection_id,
                            options.terminal->limits,
                            "binary",
                            cancel,
                            deadline};
            auto writer = OutputWriter::create(options.client->api(), std::move(o));
            if (!writer)
                return writer.error();
            output = std::move(writer.value());
        }
        std::mutex work_gate;
        std::condition_variable work_changed;
        bool done = false;
        std::optional<Receipt> fact;
        std::optional<Error> durable_error;
        // 不在网络回调线程执行宿主 handler；主等待者继续处理取消和截止。
        std::thread worker;
        try {
            worker = std::thread([&, handler = tool->second.handler,
                                  args = std::move(arguments.value())]() mutable {
                CallbackGuard worker_callback(this);
                bool receipt_saved = false;
                try {
                    ToolResult result = ToolFailure::unknown("handler exception");
                    try {
                        result = handler(ToolContext{cancel, output}, std::move(args));
                    } catch (...) {
                        result = ToolFailure::unknown("handler exception");
                    }
                    Receipt receipt = business_receipt(op, result);
                    // 业务事实先耐久，再等独立输出封口；输出失败不能擦掉已知结果。
                    {
                        std::lock_guard<std::mutex> lock(work_gate);
                        fact = receipt;
                    }
                    auto saved = options.journal->complete(op, receipt);
                    receipt_saved = bool(saved);
                    if (!saved) {
                        std::lock_guard<std::mutex> lock(work_gate);
                        durable_error = saved.error();
                    }
                    if (output) {
                        auto sealed = output->finish();
                        std::lock_guard<std::mutex> lock(work_gate);
                        output_outcome = sealed ? OutputOutcome{true, std::move(sealed.value())}
                                                : OutputOutcome{false, {}};
                    }
                } catch (...) {
                    if (output)
                        output->abort();
                    std::lock_guard<std::mutex> lock(work_gate);
                    if (!receipt_saved)
                        durable_error = detail::unknown("business receipt persistence uncertain");
                    if (output)
                        output_outcome = OutputOutcome{false, {}};
                }
                {
                    std::lock_guard<std::mutex> lock(work_gate);
                    done = true;
                }
                work_changed.notify_all();
            });
        } catch (...) {
            if (output)
                output->shutdown();
            return persist(detail::receipt_for(op, "failed", "handler_not_started"));
        }
        // 状态查询独占观察线程；run 的心跳线程不会被慢查询/handler阻塞。
        CancellationSource monitor_stop;
        auto monitor_cancel = CancellationToken::combine(cancel, monitor_stop.token());
        std::thread observer;
        try {
            observer = std::thread([&] {
                try {
                    while (!monitor_cancel.wait_for(std::chrono::milliseconds(100))) {
                        auto permission = check(op, monitor_cancel);
                        if (!permission) {
                            stop.cancel();
                            break;
                        }
                        auto state = read_status(op, monitor_cancel);
                        if (!state || state.value().status != "pending") {
                            stop.cancel();
                            break;
                        }
                    }
                } catch (...) {
                    stop.cancel();
                }
            });
        } catch (...) {
            stop.cancel();
        }
        {
            std::unique_lock<std::mutex> lock(work_gate);
            while (!done) {
                if (parent.is_cancelled() || unix_time_ms() >= deadline)
                    stop.cancel();
                if (cancel.is_cancelled() && output)
                    output->abort();
                work_changed.wait_for(lock, std::chrono::milliseconds(20));
            }
        }
        monitor_stop.cancel();
        if (observer.joinable())
            observer.join();
        worker.join();
        stop.cancel();
        if (output)
            output->shutdown();
        // 任意非合作C++ handler不能安全强杀；本调用一直等待其静止，不detach假装关闭。
        if (durable_error)
            return *durable_error;
        if (!fact)
            return detail::unknown("business fact unavailable");
        return ExecutionOutcome{*fact, output_outcome};
    }
};
Runner::Runner(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Runner::~Runner() = default;
Result<std::shared_ptr<Runner>> Runner::create(RunnerOptions options,
                                               std::optional<Connection> connection) {
    if (!options.client || !options.journal || !options.authorize)
        return detail::invalid("client, journal and authorizer are required");
    auto valid = detail::registration(options.registration);
    if (!valid)
        return valid.error();
    auto p = Platform::current();
    if (options.registration.platform.platform != p.platform ||
        options.registration.platform.arch != p.arch)
        return detail::invalid("registration platform is not this host");
    if (options.registration.operations != std::vector<std::string>{"tool.invoke"} ||
        options.tools.empty() || options.tools.size() != options.registration.tools.size() ||
        options.registration.interpreter)
        return detail::invalid("runner supports explicit business tools only");
    for (const auto &t : options.registration.tools) {
        auto h = options.tools.find(t.name);
        if (h == options.tools.end() || h->second.definition_digest != t.definition_digest ||
            !h->second.handler)
            return detail::invalid("registered handler digest mismatch");
    }
    if (options.poll_interval < std::chrono::milliseconds(10) ||
        options.poll_interval > std::chrono::seconds(60))
        return detail::invalid("poll interval");
    if ((options.require_output || options.restricted_status) && !options.terminal)
        return detail::invalid("negotiated terminal session required");
    if (options.terminal) {
        valid =
            validate_wire("terminal-services-v1", "Limits", detail::json(options.terminal->limits));
        if (!valid)
            return valid.error();
    }
    if (connection) {
        valid = detail::live(*connection);
        if (!valid)
            return valid.error();
        if (connection->executor_id != options.registration.executor_id)
            return detail::invalid("preconnected executor identity");
    }
    auto impl = std::make_unique<Impl>(std::move(options));
    impl->connection = std::move(connection);
    return std::shared_ptr<Runner>(new Runner(std::move(impl)));
}
std::optional<Connection> Runner::connection() const {
    std::lock_guard<std::mutex> lock(impl_->connection_gate);
    return impl_->connection;
}
Result<Connection> Runner::connect(CancellationToken cancel) {
    std::lock_guard<std::mutex> gate(impl_->connect_gate);
    if (auto c = connection()) {
        auto valid = detail::live(*c);
        if (!valid)
            return valid.error();
        return *c;
    }
    auto c = impl_->options.client->register_executor(impl_->options.registration, cancel);
    if (!c)
        return c.error();
    {
        std::lock_guard<std::mutex> lock(impl_->connection_gate);
        impl_->connection = c.value();
    }
    return c;
}
Result<ExecutionOutcome> Runner::execute_with_output(Operation op, CancellationToken cancel) {
    try {
        return impl_->execute(std::move(op), cancel, false);
    } catch (...) {
        return detail::unknown("executor callback or persistence exception");
    }
}
Result<Receipt> Runner::execute(Operation op, CancellationToken cancel) {
    auto r = execute_with_output(std::move(op), cancel);
    if (!r)
        return r.error();
    if (!r.value().output_confirmed()) {
        Error e{ErrorCode::unknown, "business receipt is durable; output seal unconfirmed"};
        e.wire_code = "output_incomplete";
        e.request_id = r.value().receipt.operation_id;
        return e;
    }
    return std::move(r.value().receipt);
}
Result<void> Runner::run(CancellationToken parent) {
    if (active_handler == impl_.get())
        return Error{ErrorCode::reentrant, "handler cannot run its executor"};
    bool expected = false;
    if (!impl_->running.compare_exchange_strong(expected, true))
        return detail::invalid("runner already running");
    struct Guard {
        std::atomic<bool> &v;
        ~Guard() { v.store(false); }
    } guard{impl_->running};
    auto connected = connect(parent);
    if (!connected)
        return connected.error();
    CancellationSource stop;
    auto cancel = CancellationToken::combine(parent, stop.token());
    std::mutex error_gate;
    std::optional<Error> heartbeat_error;
    std::thread heartbeat;
    try {
        heartbeat = std::thread([&] {
            try {
                while (!cancel.is_cancelled()) {
                    auto c = impl_->current();
                    if (!c) {
                        std::lock_guard<std::mutex> lock(error_gate);
                        heartbeat_error = c.error();
                        stop.cancel();
                        break;
                    }
                    const auto ms = std::min<std::uint64_t>(c.value().heartbeat_after_ms, 60000);
                    if (cancel.wait_for(std::chrono::milliseconds(ms)))
                        break;
                    auto result = impl_->renew(cancel);
                    if (!result) {
                        std::lock_guard<std::mutex> lock(error_gate);
                        heartbeat_error = result.error();
                        stop.cancel();
                        break;
                    }
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(error_gate);
                heartbeat_error = detail::unknown("executor renewal exception");
                stop.cancel();
            }
        });
    } catch (...) {
        return Error{ErrorCode::internal, "heartbeat worker unavailable"};
    }
    auto work = [&]() -> Result<void> {
        while (!cancel.is_cancelled()) {
            auto c = impl_->current();
            if (!c)
                return c.error();
            auto batch = impl_->options.client->poll(c.value(), cancel);
            if (!batch)
                return batch.error();
            for (auto &op : batch.value().operations) {
                auto outcome = impl_->execute(op, cancel, true);
                if (!outcome)
                    return outcome.error();
                auto submitted = impl_->options.client->submit(op, outcome.value().receipt, cancel);
                if (!submitted) {
                    auto status = impl_->read_status(op, cancel);
                    if (!status || !status.value().receipt ||
                        !detail::equal(to_json(*status.value().receipt),
                                       to_json(outcome.value().receipt)))
                        return submitted.error();
                }
                if (!outcome.value().output_confirmed()) {
                    Error e{ErrorCode::unknown,
                            "business receipt is durable; output seal unconfirmed"};
                    e.wire_code = "output_incomplete";
                    e.request_id = op.operation_id;
                    return e;
                }
            }
            if (cancel.wait_for(impl_->options.poll_interval))
                break;
        }
        return Error{ErrorCode::cancelled, "executor run cancelled"};
    };
    Result<void> result;
    try {
        result = work();
    } catch (...) {
        result = detail::unknown("executor runtime exception");
    }
    stop.cancel();
    heartbeat.join();
    {
        std::lock_guard<std::mutex> lock(error_gate);
        if (heartbeat_error)
            return *heartbeat_error;
    }
    return result;
}
} // namespace tansr::executor
