#include "internal.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace tansr::executor::detail {
Result<std::int64_t> sequence(const std::optional<std::string> &value) {
    if (!value)
        return std::int64_t(-1);
    std::int64_t n = 0;
    auto r = std::from_chars(value->data(), value->data() + value->size(), n);
    if (r.ec != std::errc{} || r.ptr != value->data() + value->size() || n < 0 ||
        std::to_string(n) != *value)
        return invalid("output sequence");
    return n;
}
Result<void> validate_output(const OutputStatus &s) {
    auto ack = sequence(s.accepted_through), durable = sequence(s.durable_through),
         retained = sequence(s.retained_from), offset = sequence(s.next_byte_offset);
    if (!ack)
        return ack.error();
    if (!durable)
        return durable.error();
    if (!retained)
        return retained.error();
    if (!offset)
        return offset.error();
    if (durable.value() > ack.value() || retained.value() > ack.value())
        return invalid("impossible output watermark");
    if (offset.value() < 0) {
        if (s.state != "unavailable" || ack.value() != -1 || durable.value() != -1 ||
            retained.value() != -1 || s.seal)
            return invalid("missing output offset");
    } else if ((ack.value() == -1 && offset.value() != 0) ||
               (ack.value() != -1 && offset.value() <= ack.value()))
        return invalid("output offset");
    if (s.seal) {
        auto last = sequence(s.seal->last_seq);
        if (!last)
            return last.error();
        if (last.value() != ack.value() ||
            s.next_byte_offset != std::optional<std::string>(s.seal->total_bytes))
            return invalid("output seal watermark");
        if (ack.value() == -1) {
            auto empty = crypto::sha256_hex("");
            if (!empty)
                return empty.error();
            if (s.seal->total_bytes != "0" || s.seal->payload_digest != empty.value())
                return invalid("empty output seal");
        }
    }
    bool valid = false;
    if (s.state == "complete")
        valid = s.seal && !s.seal->truncated;
    else if (s.state == "truncated")
        valid = s.seal && s.seal->truncated;
    else if (s.state == "available")
        valid = ack.value() == -1 && !s.seal;
    else if (s.state == "receiving")
        valid = ack.value() != -1 && !s.seal;
    else if (s.state == "gap")
        valid = ack.value() != -1;
    else if (s.state == "unavailable")
        valid = true;
    if (!valid)
        return invalid("output state");
    return {};
}
Result<OutputStatus> query_output(const std::shared_ptr<ApiClient> &api,
                                  const TerminalSessionReference &session,
                                  const OutputOperationReference &operation, std::size_t max,
                                  CancellationToken cancel, std::optional<std::int64_t> deadline) {
    CallOptions o;
    o.parameters = {{"id", session.session_id}};
    o.query = {{"contract", "terminal-services-v1"},
               {"sessionContract", session.session_contract},
               {"operationId", operation.operation_id},
               {"requestDigest", operation.request_digest}};
    o.cancel = cancel;
    o.deadline_ms = deadline.value_or(unix_time_ms() + 30000);
    o.max_response_bytes = max;
    auto v = call(api, "terminal.output.status", "terminal-services-v1", "OutputStatus", o);
    if (!v)
        return v.error();
    auto s = output_status(v.value());
    if (!s)
        return s.error();
    if (!equal(json(s.value().operation), json(operation)))
        return invalid("output operation identity");
    return s;
}
} // namespace tansr::executor::detail

namespace tansr::executor {
struct OutputWriter::Impl {
    struct Pending {
        Json block;
        std::size_t cost;
        std::int64_t seq, end;
    };
    std::shared_ptr<ApiClient> api;
    OutputOptions options;
    crypto::Sha256 hash;
    CancellationSource stop;
    CancellationToken cancel;
    mutable std::mutex mutex;
    std::mutex join_gate;
    std::condition_variable changed;
    std::deque<Pending> pending;
    std::size_t pending_bytes{0};
    std::int64_t next{0}, offset{0}, ack{-1}, ack_offset{0}, sent{-1};
    std::uint64_t dropped{0};
    bool truncated{false}, sealed{false}, done{false};
    std::optional<OutputSeal> seal;
    std::optional<OutputStatus> status;
    std::optional<Error> failed;
    std::thread worker;
    Impl(std::shared_ptr<ApiClient> a, OutputOptions o, crypto::Sha256 h)
        : api(std::move(a)), options(std::move(o)), hash(std::move(h)),
          cancel(CancellationToken::combine(options.cancellation, stop.token())) {}
    Json batch(Json::Array blocks, std::optional<OutputSeal> end) const {
        return Json::object({{"contract", "terminal-services-v1"},
                             {"session", detail::json(options.session)},
                             {"operation", detail::json(options.operation)},
                             {"executorId", options.executor_id},
                             {"connectionId", options.connection_id},
                             {"blocks", Json(std::move(blocks))},
                             {"seal", end ? detail::json(*end) : Json()}});
    }
    Result<void> accept(const OutputStatus &value) {
        auto valid = detail::validate_output(value);
        if (!valid)
            return valid;
        if (!detail::equal(detail::json(value.operation), detail::json(options.operation)))
            return detail::invalid("output operation identity");
        if (value.state == "gap" || value.state == "unavailable")
            return detail::unknown("output watermark unavailable; original prefix retained");
        auto n = detail::sequence(value.accepted_through);
        if (!n)
            return n.error();
        std::lock_guard<std::mutex> lock(mutex);
        if (n.value() < ack || n.value() > sent)
            return detail::invalid("output ACK outside sent range");
        std::int64_t end = ack_offset;
        if (n.value() != ack) {
            auto p = std::find_if(pending.begin(), pending.end(),
                                  [&](const Pending &b) { return b.seq == n.value(); });
            if (p == pending.end())
                return detail::invalid("ACK without pending block");
            end = p->end;
        }
        if (value.next_byte_offset != std::optional<std::string>(std::to_string(end)) ||
            (value.seal &&
             (!seal || !detail::equal(detail::json(*value.seal), detail::json(*seal)))))
            return detail::invalid("output ACK offset or seal mismatch");
        while (!pending.empty() && pending.front().seq <= n.value()) {
            pending_bytes -= pending.front().cost;
            pending.pop_front();
        }
        ack = n.value();
        ack_offset = end;
        sealed = bool(value.seal);
        status = value;
        changed.notify_all();
        return {};
    }
    bool acknowledged(std::int64_t seq, bool end) {
        std::lock_guard<std::mutex> lock(mutex);
        return end ? sealed : ack >= seq;
    }
    Result<void> upload(const Json &body, std::int64_t seq, bool end) {
        auto valid = validate_wire("terminal-services-v1", "OutputBatchRequest", body);
        if (!valid)
            return valid;
        auto encoded = canonical::encode_limited(body, options.limits.max_control_bytes);
        if (!encoded)
            return encoded.error();
        // 仅此领域允许：失回先查询原操作，最多重发一次完全相同的输出批。
        const auto deadline = options.deadline_ms.value_or(unix_time_ms() + 30000);
        for (int attempt = 0; attempt < 2; ++attempt) {
            CallOptions o;
            o.parameters = {{"id", options.executor_id}};
            o.body = body;
            o.cancel = cancel;
            o.deadline_ms = deadline;
            o.max_response_bytes = options.limits.max_control_bytes;
            auto r = detail::call(api, "terminal.output.batch", "terminal-services-v1",
                                  "OutputStatus", o);
            if (r) {
                auto state = detail::output_status(r.value());
                if (!state)
                    return state.error();
                auto accepted = accept(state.value());
                if (!accepted)
                    return accepted;
                if (acknowledged(seq, end))
                    return {};
            } else if (r.error().code == ErrorCode::cancelled ||
                       r.error().code == ErrorCode::permission || r.error().http_status == 401 ||
                       r.error().http_status == 403 || r.error().wire_code == "unauthorized" ||
                       r.error().wire_code == "forbidden")
                return r.error();
            auto state = detail::query_output(api, options.session, options.operation,
                                              options.limits.max_control_bytes, cancel, deadline);
            if (!state)
                return state.error();
            auto accepted = accept(state.value());
            if (!accepted)
                return accepted;
            if (acknowledged(seq, end))
                return {};
        }
        return detail::unknown("output ACK unknown; original pending bytes retained");
    }
    void pump() noexcept {
        try {
            for (;;) {
                Json body;
                std::int64_t seq = -1;
                bool end = false;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    changed.wait_for(lock, std::chrono::milliseconds(25), [&] {
                        return cancel.is_cancelled() || failed || sealed || !pending.empty() ||
                               seal;
                    });
                    if (cancel.is_cancelled() || failed || sealed)
                        break;
                    if (!pending.empty()) {
                        const auto &p = pending.front();
                        seq = p.seq;
                        sent = seq;
                        body = batch({p.block}, {});
                    } else if (seal) {
                        body = batch({}, seal);
                        end = true;
                    } else
                        continue;
                }
                auto result = upload(body, seq, end);
                if (!result) {
                    std::lock_guard<std::mutex> lock(mutex);
                    failed = result.error();
                    break;
                }
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            failed = detail::unknown("output worker exception");
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        changed.notify_all();
    }
    void join() {
        std::lock_guard<std::mutex> lock(join_gate);
        if (worker.joinable())
            worker.join();
    }
};
OutputWriter::OutputWriter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OutputWriter::~OutputWriter() {
    abort();
    impl_->join();
}
Result<std::shared_ptr<OutputWriter>> OutputWriter::create(std::shared_ptr<ApiClient> api,
                                                           OutputOptions options) {
    if (!api)
        return detail::invalid("missing output API");
    for (const auto &entry : std::vector<std::pair<std::string, Json>>{
             {"SessionReference", detail::json(options.session)},
             {"OperationReference", detail::json(options.operation)},
             {"Limits", detail::json(options.limits)},
             {"Id", Json(options.executor_id)},
             {"Id", Json(options.connection_id)}}) {
        auto valid = validate_wire("terminal-services-v1", entry.first, entry.second);
        if (!valid)
            return valid.error();
    }
    if (options.encoding != "binary" && options.encoding != "utf-8")
        return detail::invalid("output encoding");
    if (options.cancellation.is_cancelled())
        return Error{ErrorCode::cancelled, "output cancelled"};
    if (options.deadline_ms && *options.deadline_ms <= unix_time_ms())
        return Error{ErrorCode::timeout, "output deadline expired"};
    auto hash = crypto::Sha256::create();
    if (!hash)
        return hash.error();
    auto impl = std::make_unique<Impl>(std::move(api), std::move(options), std::move(hash.value()));
    auto writer = std::shared_ptr<OutputWriter>(new OutputWriter(std::move(impl)));
    auto *raw = writer->impl_.get();
    try {
        raw->worker = std::thread([raw] { raw->pump(); });
    } catch (...) {
        return Error{ErrorCode::internal, "output worker unavailable"};
    }
    return writer;
}
Result<std::size_t> OutputWriter::capture(std::string_view channel, std::string_view bytes) {
    if (channel != "stdout" && channel != "stderr")
        return detail::invalid("output channel");
    auto &s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.seal)
        return detail::invalid("output capture closed");
    auto drop = [&](std::size_t n) {
        s.truncated = true;
        s.dropped = UINT64_MAX - s.dropped < n ? UINT64_MAX : s.dropped + std::uint64_t(n);
    };
    if (s.truncated || s.failed || s.cancel.is_cancelled()) {
        drop(bytes.size());
        return std::size_t(0);
    }
    if (s.done)
        return detail::invalid("output capture closed");
    std::size_t retained = 0;
    const auto max = std::min(s.options.limits.max_block_bytes, s.options.limits.max_batch_bytes);
    while (retained < bytes.size()) {
        const auto used = static_cast<std::size_t>(s.offset);
        const auto remaining = used < s.options.limits.max_retained_bytes
                                   ? s.options.limits.max_retained_bytes - used
                                   : 0;
        const auto n = std::min({max, bytes.size() - retained, remaining});
        if (n == 0 || s.next == INT64_MAX || n > std::size_t(INT64_MAX - s.offset))
            break;
        const auto part = bytes.substr(retained, n);
        auto hash = crypto::sha256_hex(part);
        if (!hash)
            return hash.error();
        auto block = Json::object({{"seq", std::to_string(s.next)},
                                   {"byteOffset", std::to_string(s.offset)},
                                   {"channel", channel},
                                   {"encoding", s.options.encoding},
                                   {"byteLength", std::uint64_t(n)},
                                   {"payloadDigest", hash.value()},
                                   {"base64", crypto::base64_encode(part)}});
        auto encoded = canonical::encode_limited(block, s.options.limits.max_control_bytes);
        if (!encoded)
            return encoded.error();
        const auto cost = encoded.value().size();
        if (s.pending_bytes > s.options.limits.max_pending_bytes ||
            cost > s.options.limits.max_pending_bytes - s.pending_bytes)
            break;
        if (!canonical::encode_limited(s.batch({block}, {}), s.options.limits.max_control_bytes))
            break;
        auto updated = s.hash.update(part);
        if (!updated)
            return updated.error();
        s.pending.push_back({std::move(block), cost, s.next, s.offset + std::int64_t(n)});
        s.pending_bytes += cost;
        ++s.next;
        s.offset += std::int64_t(n);
        retained += n;
    }
    if (retained < bytes.size())
        drop(bytes.size() - retained);
    s.changed.notify_all();
    return retained;
}
OutputSnapshot OutputWriter::snapshot() const {
    auto &s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    return {s.pending_bytes,
            s.pending.size(),
            std::uint64_t(s.offset),
            s.dropped,
            s.truncated,
            s.sealed,
            bool(s.failed) || s.cancel.is_cancelled()};
}
void OutputWriter::abort() noexcept {
    impl_->stop.cancel();
    impl_->changed.notify_all();
}
Result<void> OutputWriter::shutdown() {
    abort();
    impl_->join();
    return {};
}
Result<OutputStatus> OutputWriter::finish() {
    auto &s = *impl_;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.seal) {
            auto hash = s.hash.hex();
            if (!hash)
                return hash.error();
            s.seal = OutputSeal{s.next ? std::optional<std::string>(std::to_string(s.next - 1))
                                       : std::nullopt,
                                std::to_string(s.offset), hash.value(), s.truncated};
        }
        s.changed.notify_all();
    }
    std::unique_lock<std::mutex> lock(s.mutex);
    while (!s.done)
        s.changed.wait_for(lock, std::chrono::milliseconds(25));
    if (s.failed)
        return *s.failed;
    if (s.sealed && s.status)
        return *s.status;
    if (s.cancel.is_cancelled())
        return Error{ErrorCode::cancelled, "output cancelled before confirmed seal"};
    return detail::unknown("output seal not confirmed");
}
} // namespace tansr::executor
