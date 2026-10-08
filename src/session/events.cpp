#include "internal.hpp"
#include <cstdlib>
#include <thread>

namespace tansr::session {
using namespace detail;
std::string SessionEvent::kind() const { return string(field(envelope, "type")).value_or(""); }
const Json &SessionEvent::raw() const { return field(envelope, "raw"); }
std::optional<Outcome> SessionEvent::turn_outcome() const {
    const auto type = kind();
    const auto terminal = string(field(envelope, "terminalStatus"));
    OutcomeStatus status;
    if (type == "turn.completed" && terminal == "completed")
        status = OutcomeStatus::completed;
    else if (type == "turn.aborted" && terminal == "aborted")
        status = OutcomeStatus::aborted;
    else if (type == "turn.error" && terminal == "aborted" &&
             field(raw(), "recoverable").is_bool() && !field(raw(), "recoverable").as_bool())
        status = OutcomeStatus::failed;
    else if (type == "session.ended" && terminal == "completed")
        status = OutcomeStatus::session_ended;
    else if (type.compare(0, 5, "turn.") == 0 && terminal == "unknown")
        status = OutcomeStatus::unknown;
    else
        return {};
    return Outcome{status, string(field(raw(), "turnId")), string(field(raw(), "reason"))};
}
TurnTracker::TurnTracker(std::uint64_t after) : after_seq_(after) {}
Result<TurnTracker> TurnTracker::create(std::uint64_t after) {
    if (after > max_safe_integer)
        return invalid("turn watermark exceeds safe integer limit");
    return TurnTracker(after);
}
Result<TurnTracker> TurnTracker::resume(std::uint64_t after, std::string turn) {
    if (turn.empty() || utf16_units(turn) > 128)
        return invalid("resumed turn ID must contain 1 to 128 UTF-16 units");
    auto tracker = create(after);
    if (!tracker)
        return tracker.error();
    tracker.value().active_turn_id_ = std::move(turn);
    return tracker;
}
Result<TurnTracker> TurnTracker::from_replay(std::uint64_t after) {
    auto tracker = create(after);
    if (!tracker)
        return tracker.error();
    tracker.value().replaying_prefix_ = true;
    return tracker;
}
const std::optional<std::string> &TurnTracker::active_turn_id() const noexcept {
    return active_turn_id_;
}
bool TurnTracker::needs_reconciliation() const noexcept { return invalidated_; }
std::optional<Outcome> TurnTracker::observe(const SessionEvent &event) {
    if (finished_ || invalidated_)
        return {};
    const auto kind = event.kind();
    if (kind == "server.replay.gap") {
        invalidated_ = true;
        return {};
    }
    auto event_id = string(field(event.envelope, "eventId"));
    if (!event_id)
        return {};
    auto sequence = parse_sequence(*event_id);
    if (!sequence)
        return {};
    auto turn = string(field(event.raw(), "turnId"));
    if (turn && turn->empty())
        turn.reset();
    if (sequence.value() <= after_seq_) {
        if (replaying_prefix_) {
            if (kind == "turn.started")
                active_turn_id_ = turn;
            else if (auto outcome = event.turn_outcome();
                     outcome && (outcome->status == OutcomeStatus::session_ended ||
                                 (active_turn_id_ && turn == active_turn_id_)))
                active_turn_id_.reset();
        }
        return {};
    }
    replaying_prefix_ = false;
    if (kind == "turn.started" && !active_turn_id_) {
        active_turn_id_ = std::move(turn);
        return {};
    }
    auto outcome = event.turn_outcome();
    if (!outcome)
        return {};
    if (outcome->status == OutcomeStatus::session_ended) {
        finished_ = true;
        return outcome;
    }
    if (!active_turn_id_ || turn != active_turn_id_)
        return {};
    finished_ = true;
    return outcome;
}
Result<SessionEventStream> Session::events(std::optional<std::string> last_id,
                                           CancellationToken cancellation) const {
    if (last_id && last_id->empty())
        last_id.reset();
    if (last_id) {
        auto valid = parse_sequence(*last_id);
        if (!valid)
            return valid.error();
    }
    CancellationSource local;
    auto cancel = CancellationToken::combine(
        client_.cancel_, CancellationToken::combine(cancellation, local.token()));
    if (cancel.is_cancelled())
        return Error{ErrorCode::cancelled, "session observation cancelled"};
    CallOptions options;
    options.parameters = {{"id", id()}};
    options.last_event_id = last_id;
    options.cancel = cancel;
    auto inner = api()->events("session.events.observe", std::move(options));
    if (!inner)
        return inner.error();
    return SessionEventStream(std::move(inner).value(), id(), std::move(last_id), local,
                              std::move(cancel));
}
SessionEventStream::SessionEventStream(EventStream inner, std::string id,
                                       std::optional<std::string> cursor, CancellationSource local,
                                       CancellationToken cancel)
    : inner_(std::move(inner)), session_id_(std::move(id)), last_event_id_(std::move(cursor)),
      local_(std::move(local)), cancel_(std::move(cancel)) {
    if (last_event_id_)
        last_seq_ = parse_sequence(*last_event_id_).value();
}
SessionEventStream::~SessionEventStream() { close(); }
SessionEventStream::SessionEventStream(SessionEventStream &&other) noexcept
    : inner_(std::move(other.inner_)), session_id_(std::move(other.session_id_)),
      last_event_id_(std::move(other.last_event_id_)), last_seq_(other.last_seq_),
      local_(other.local_), cancel_(std::move(other.cancel_)) {
    other.inner_.reset();
}
SessionEventStream &SessionEventStream::operator=(SessionEventStream &&other) noexcept {
    if (this != &other) {
        close();
        inner_ = std::move(other.inner_);
        other.inner_.reset();
        session_id_ = std::move(other.session_id_);
        last_event_id_ = std::move(other.last_event_id_);
        last_seq_ = other.last_seq_;
        local_ = other.local_;
        cancel_ = std::move(other.cancel_);
    }
    return *this;
}
const std::optional<std::string> &SessionEventStream::last_event_id() const noexcept {
    return last_event_id_;
}
void SessionEventStream::close() noexcept {
    if (inner_) {
        local_.cancel();
        inner_->cancel();
        inner_.reset();
    }
}
void SessionEventStream::shutdown() noexcept { close(); }
Result<std::optional<SessionEvent>> SessionEventStream::decode(Json envelope) {
    if (!eq(field(envelope, "domain"), "session") || !field(envelope, "raw").is_object())
        return contract("session event has no matching object envelope");
    const auto kind = string(field(envelope, "type")).value_or("");
    const auto &raw = field(envelope, "raw");
    const auto &cursor = field(field(envelope, "cursorSet"), "eventCursor");
    if (kind == "server.replay.gap") {
        if (!field(envelope, "eventId").is_null() || !cursor.is_null() ||
            !eq(field(raw, "sessionId"), session_id_) || !eq(field(raw, "type"), kind))
            return contract("replay gap changed identity or cursor");
        if (eq(field(raw, "reason"), "ahead_of_log")) {
            last_seq_.reset();
            last_event_id_.reset();
        }
        return std::optional<SessionEvent>(SessionEvent{std::move(envelope)});
    }
    auto event_id = required_string(envelope, "eventId");
    if (!event_id)
        return event_id.error();
    if (!eq(cursor, event_id.value()))
        return contract("event cursor differs from event ID");
    auto sequence = parse_sequence(event_id.value());
    if (!sequence)
        return sequence.error();
    if (kind.compare(0, 7, "server.") == 0) {
        const auto &ts = field(raw, "ts");
        if (!ts.is_number())
            return contract("control event has no finite timestamp");
        std::istringstream stream(std::string(ts.number_token()));
        stream.imbue(std::locale::classic());
        double value = 0;
        stream >> value;
        if (!stream || !std::isfinite(value))
            return contract("control event has no finite timestamp");
    } else {
        auto seq = safe_field(raw, "seq");
        if (!eq(field(raw, "type"), kind) || !eq(field(raw, "sessionId"), session_id_) || !seq ||
            seq.value() != sequence.value())
            return contract("kernel event identity differs from its session stream");
    }
    if (last_seq_ && sequence.value() <= *last_seq_)
        return std::optional<SessionEvent>{};
    last_seq_ = sequence.value();
    last_event_id_ = std::move(event_id).value();
    return std::optional<SessionEvent>(SessionEvent{std::move(envelope)});
}
Result<std::optional<SessionEvent>> SessionEventStream::next(CancellationToken cancellation) {
    if (!inner_)
        return std::optional<SessionEvent>{};
    auto cancel = CancellationToken::combine(cancel_, cancellation);
    std::size_t duplicates = 0;
    for (;;) {
        if (cancel.is_cancelled()) {
            close();
            return Error{ErrorCode::cancelled, "session observation cancelled"};
        }
        auto next = inner_->next(cancel);
        if (!next) {
            auto error = next.error();
            close();
            return error;
        }
        if (!next.value()) {
            close();
            return std::optional<SessionEvent>{};
        }
        // 公共 API 已验证七键及 SSE id；本层只投影并核对会话身份。
        auto json = Json::parse(next.value()->data);
        if (!json) {
            auto error = json.error();
            close();
            return error;
        }
        auto event = decode(std::move(json).value());
        if (!event) {
            auto error = event.error();
            close();
            return error;
        }
        if (event.value())
            return event;
        if (++duplicates == 64) {
            duplicates = 0;
            std::this_thread::yield();
        }
    }
}
} // namespace tansr::session
