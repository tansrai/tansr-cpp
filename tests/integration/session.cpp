// 显式真 Serve 验收入口：夹具进程、平台模型替身及外层超时均由宿主负责。
#include "tansr/session.hpp"
#include "tansr/executor.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace {
using namespace tansr;
using namespace tansr::session;
using Clock = std::chrono::steady_clock;

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
void take(Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
Json read_json(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "cannot open fixture info");
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    return take(Json::parse(bytes));
}
std::string text(const Json &value, std::string_view field) {
    const auto *v = value.find(field);
    require(v && v->is_string() && !v->as_string().empty(),
            "required fixture/response string missing");
    return v->as_string();
}
bool equal(const Json &value, std::string_view expected) {
    return value.is_string() && value.as_string() == expected;
}
bool same(const Json &first, const Json &second) {
    if (first.is_object() && second.is_object()) {
        if (first.as_object().size() != second.as_object().size())
            return false;
        for (const auto &entry : first.as_object()) {
            const auto *value = second.find(entry.first);
            if (!value || !same(entry.second, *value))
                return false;
        }
        return true;
    }
    if (first.is_array() && second.is_array()) {
        if (first.as_array().size() != second.as_array().size())
            return false;
        for (std::size_t i = 0; i < first.as_array().size(); ++i)
            if (!same(first.at(i), second.at(i)))
                return false;
        return true;
    }
    return first.dump() == second.dump();
}
void reject(const Error &error, std::initializer_list<int> status) {
    require(std::find(status.begin(), status.end(), error.http_status) != status.end(),
            "expected authenticated Serve HTTP rejection");
}
template <class T> void rejected(Result<T> result, std::initializer_list<int> status) {
    require(!result, "operation unexpectedly succeeded");
    reject(result.error(), status);
}
void input_rejected(Result<Json> result, std::string_view code, int domain_status) {
    require(!result, "invalid input unexpectedly succeeded");
    const auto &error = result.error();
    require(error.http_status == 500 && error.wire_code == "internal_error" &&
                error.retry_action == "none",
            "input error changed frozen unified classification");
    const auto detail = take(Json::parse(error.detail));
    require(equal(detail.at("domainCode"), code) &&
                detail.at("domainStatus").as_i64() == domain_status,
            "input error lost original domain code/status");
}
SessionClient sessions(const Json &info, std::string family,
                       const std::shared_ptr<Runtime> &runtime, bool other = false) {
    ClientOptions options;
    options.base_url = text(info, "baseURL");
    options.family = std::move(family);
    const auto token = text(info, other ? "otherToken" : "token");
    options.token_provider = [token, other](CancellationToken cancel) -> Result<AuthToken> {
        if (cancel.is_cancelled())
            return Error{ErrorCode::cancelled, "integration credential read cancelled"};
        return AuthToken{token, other ? "fixture-other" : "fixture-owner"};
    };
    options.default_timeout = std::chrono::seconds(30);
    return take(SessionClient::create(take(ApiClient::create(std::move(options), runtime))));
}
Session make_session(const SessionClient &client, std::string request) {
    CreateOptions options;
    if (client.api()->family() == "sdk2-offload-v1")
        options.request_id = std::move(request);
    return take(client.create(std::move(options)));
}
void wait_idle(const Session &session) {
    const auto end = Clock::now() + std::chrono::seconds(10);
    while (Clock::now() < end) {
        const auto meta = take(session.meta());
        if (meta.status == "idle")
            return;
        require(meta.status == "running", "session unexpectedly ended");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("kernel did not settle after terminal event");
}
SessionEvent next(SessionEventStream &stream) {
    auto event = take(stream.next());
    require(event.has_value(), "EOF is not turn completion");
    return std::move(*event);
}
Json wait_kind(SessionEventStream &stream, std::string_view kind) {
    const auto end = Clock::now() + std::chrono::seconds(25);
    while (Clock::now() < end) {
        auto event = next(stream);
        if (event.kind() == kind)
            return event.raw();
        require(!event.turn_outcome(), "turn ended before requested event");
    }
    throw std::runtime_error("expected event deadline exceeded");
}
std::string finish(SessionEventStream &stream, TurnTracker &tracker, OutcomeStatus expected) {
    std::string answer;
    const auto end = Clock::now() + std::chrono::seconds(25);
    while (Clock::now() < end) {
        auto event = next(stream);
        if (event.kind() == "server.replay.gap")
            throw std::runtime_error("turn observation requires replay-gap reconciliation: " +
                                     event.raw().dump());
        require(event.envelope.is_object() && event.envelope.as_object().size() == 7,
                "event envelope lost exact seven keys");
        if (event.kind() == "msg.text.delta") {
            const auto *delta = event.raw().find("text");
            if (delta && delta->is_string())
                answer += delta->as_string();
        }
        if (auto outcome = tracker.observe(event)) {
            require(outcome->status == expected, "unexpected current-turn outcome");
            require(outcome->turn_id && tracker.active_turn_id() == outcome->turn_id,
                    "terminal changed current turn identity");
            return answer;
        }
    }
    throw std::runtime_error("matching current-turn terminal deadline exceeded");
}
std::string complete(const Session &session, std::string prompt) {
    const auto floor = take(session.meta()).last_seq;
    auto tracker = take(TurnTracker::create(floor));
    auto stream = take(session.events(std::to_string(floor)));
    require(take(session.send(std::move(prompt))).accepted, "send was not accepted");
    auto answer = finish(stream, tracker, OutcomeStatus::completed);
    require(stream.last_event_id().has_value(), "processed cursor missing");
    stream.shutdown();
    wait_idle(session);
    return answer;
}
Json control(Json command) {
    const auto id = text(command, "requestId");
    std::cout << "TANSR_CPP_CONTROL " << command.dump() << std::endl;
    std::string line;
    require(static_cast<bool>(std::getline(std::cin, line)), "fixture control acknowledgement EOF");
    const std::string prefix = "TANSR_RUST_CONTROL ";
    if (line.compare(0, prefix.size(), prefix) == 0)
        line.erase(0, prefix.size());
    const auto receipt = take(Json::parse(line));
    require(text(receipt, "requestId") == id && receipt.at("ok").is_bool() &&
                receipt.at("ok").as_bool(),
            "fixture control acknowledgement mismatch/rejection");
    return receipt.at("result");
}
void record(Json &receipt, std::string name) {
    receipt.at("passed").as_array().emplace_back(name);
    std::cout << "TANSR_CPP_PHASE "
              << Json::object({{"phase", std::move(name)}, {"status", "passed"}}).dump()
              << std::endl;
}
void local_cancel(const Session &session) {
    const auto floor = take(session.meta()).last_seq;
    CancellationSource source;
    auto stream = take(session.events(std::to_string(floor), source.token()));
    source.cancel();
    auto result = stream.next();
    require(!result && result.error().code == ErrorCode::cancelled,
            "local cancellation was not observed");
    stream.shutdown();
    require(take(session.meta()).status == "idle",
            "local stream cancellation altered remote session");
}
void owner_isolation(const Json &info, const std::shared_ptr<Runtime> &runtime,
                     const SessionClient &client, const Session &session) {
    auto other = sessions(info, client.api()->family(), runtime, true);
    rejected(other.attach(session.id()), {403, 404});
    rejected(other.resume(session.id()), {403, 404});
    require(take(other.list(0, 20)).total == 0, "rejected resume created a substitute session");
}
void family_isolation(const Json &info, const std::shared_ptr<Runtime> &runtime,
                      const SessionClient &client, const Session &session) {
    auto wrong =
        sessions(info, client.api()->family() == "sdk1" ? "sdk2-offload-v1" : "sdk1", runtime);
    const auto before = take(client.list(0, 100)).total;
    for (auto result : {wrong.attach(session.id()), wrong.resume(session.id())}) {
        require(!result, "wrong family attached or resumed original session");
        const auto &error = result.error();
        require(error.code == ErrorCode::invalid_input || error.code == ErrorCode::contract ||
                    error.http_status == 400 || error.http_status == 404 ||
                    error.http_status == 409,
                "wrong family failed for an unrelated transport condition");
    }
    require(take(client.list(0, 100)).total == before,
            "wrong family created a replacement session");
    require(take(session.meta()).session_id == session.id(), "wrong family changed owner identity");
}
void snapshot_and_media(const Json &info, const std::shared_ptr<Runtime> &runtime,
                        const SessionClient &client, const Session &session) {
    const auto floor = take(session.meta()).last_seq;
    auto tracker = take(TurnTracker::create(floor));
    auto stream = take(session.events(std::to_string(floor)));
    std::vector<Block> blocks{
        TextBlock{"CPP-SNAPSHOT-BLOCK"},
        ImageBlock{"image/png", "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/"
                                "x8AAwMCAO+y9k8AAAAASUVORK5CYII="}};
    require(take(session.send_blocks(std::move(blocks))).accepted, "image prompt not accepted");
    require(finish(stream, tracker, OutcomeStatus::completed).find("go-real-serve-answer") !=
                std::string::npos,
            "image turn did not use fixture model");
    stream.shutdown();
    wait_idle(session);
    require(take(session.history(0, 30)).dump().find("image/png") != std::string::npos,
            "image history omitted");
    const auto before = take(session.history(0, 0));
    const auto checkpoint = take(session.checkpoint(u8"原始快照"));
    require(checkpoint.message_count == before.at("total").as_u64(),
            "checkpoint count changed history");
    const auto list = take(session.checkpoints());
    require(std::any_of(
                list.begin(), list.end(),
                [&](const auto &item) { return item.checkpoint_id == checkpoint.checkpoint_id; }),
            "created checkpoint not listed");
    const auto bytes = take(session.export_checkpoint(checkpoint.checkpoint_id));
    const auto exported = take(Json::parse(bytes));
    require(exported.at("checkpoint").at("messages").dump().find("go-real-serve-answer") !=
                std::string::npos,
            "checkpoint omitted real history");
    const auto imported = take(session.import_checkpoint(bytes, u8"导入副本"));
    require(imported.checkpoint_id != checkpoint.checkpoint_id &&
                imported.message_count == checkpoint.message_count,
            "import did not preserve copy identity/count");
    const auto reexported =
        take(Json::parse(take(session.export_checkpoint(imported.checkpoint_id))));
    require(
        same(reexported.at("checkpoint").at("messages"), exported.at("checkpoint").at("messages")),
        "raw snapshot roundtrip changed messages");
    require(same(take(session.history(0, 0)).at("total"), before.at("total")),
            "import silently changed live history");
    const auto restored = take(session.restore(imported.checkpoint_id, false));
    require(equal(restored.at("status"), "restored") &&
                same(restored.at("toMessages"), before.at("total")),
            "restore receipt inconsistent");
    auto other = sessions(info, client.api()->family(), runtime, true);
    CallOptions export_options;
    export_options.parameters = {{"id", session.id()}, {"targetId", checkpoint.checkpoint_id}};
    rejected(other.api()->call("session.checkpoint.export", std::move(export_options)), {403});
    take(session.delete_checkpoint(imported.checkpoint_id));
    require(!session.export_checkpoint(imported.checkpoint_id),
            "deleted checkpoint still exported");
    require(!session.restore("missing", false), "missing checkpoint restored");
    require(!session.import_checkpoint("invalid-snapshot"), "invalid snapshot imported");
    const auto after = take(session.history(0, 0));
    TranscriptionRequest asr;
    asr.audio = "data:audio/wav;base64,UklGRi1ydXN0LXN5bnRoZXRpYy1hdWRpbw==";
    asr.language = "en";
    require(equal(take(session.transcribe(asr)).at("text"), "rust synthetic transcript"),
            "ASR response differs from fixture");
    SpeechRequest tts;
    tts.input = "one";
    tts.format = "wav";
    const auto speech = take(session.speak(tts));
    require(equal(speech.at("audio").at("mime"), "audio/wav") &&
                equal(speech.at("audio").at("b64"), "UklGRi1ydXN0LXN5bnRoZXRpYy1hdWRpbw=="),
            "TTS bytes differ from fixture");
    require(same(take(session.history(0, 0)).at("total"), after.at("total")),
            "media silently started a turn");
    CompactOptions compact;
    compact.checkpoint = true;
    const auto result = take(session.compact(compact));
    const auto status = text(result, "status");
    require(status == "compacted" || status == "rejected" || status == "failed",
            "unknown compaction outcome");
    if (status != "compacted")
        require(!text(result, "reason").empty(), "compaction failure lost reason");
}
void persisted_resume_and_gap(const SessionClient &client, Session &session) {
    const auto history = take(session.history(0, 50));
    const auto floor = take(session.meta()).last_seq;
    auto ending = take(session.events(std::to_string(floor)));
    require(take(session.close()).accepted, "close not accepted");
    const auto ended = wait_kind(ending, "session.ended");
    require(equal(ended.at("sessionId"), session.id()), "session end changed identity");
    ending.shutdown();
    auto resumed = take(client.resume(session.id()));
    require(resumed.id() == session.id() && resumed.created().resumed,
            "dormant resume did not reconstruct original session");
    require(same(take(resumed.history(0, 50)).at("messages"), history.at("messages")),
            "persisted resume changed messages");
    auto stream = take(resumed.events("9007199254740991"));
    auto gap = next(stream);
    require(gap.kind() == "server.replay.gap" && equal(gap.raw().at("reason"), "ahead_of_log") &&
                gap.envelope.at("eventId").is_null(),
            "ahead cursor did not return original gap");
    auto tracker = take(TurnTracker::from_replay(floor));
    require(!tracker.observe(gap) && tracker.needs_reconciliation(),
            "gap did not invalidate tracker");
    require(!stream.last_event_id(), "ahead gap did not reset cursor");
    stream.shutdown();
    require(same(take(resumed.history(0, 0)).at("total"), history.at("total")),
            "explicit reconciliation changed history");
    session = std::move(resumed);
}
void interrupt_and_input(const Json &info, const std::shared_ptr<Runtime> &runtime,
                         const SessionClient &client, Json &receipt) {
    auto blocked = make_session(client, "unused-sdk1-interrupt");
    const auto floor = take(blocked.meta()).last_seq;
    auto stream = take(blocked.events(std::to_string(floor)));
    take(blocked.send("GO-BLOCK"));
    wait_kind(stream, "msg.text.delta");
    const auto capabilities = take(blocked.input_capabilities());
    const InputTarget target{text(capabilities.at("target"), "historyEpoch"),
                             text(capabilities.at("target"), "turnId")};
    auto tracker = take(TurnTracker::resume(floor, target.turn_id));
    require(take(blocked.interrupt()).accepted, "interrupt not accepted");
    finish(stream, tracker, OutcomeStatus::aborted);
    stream.shutdown();
    take(blocked.close());
    record(receipt, "explicit-interrupt-distinct-from-eof");

    auto session = make_session(client, "unused-sdk1-input");
    complete(session, "CPP-BEFORE-INPUT");
    const auto checkpoint = take(session.checkpoint("pre-input-generation"));
    const auto input_floor = take(session.meta()).last_seq;
    auto input_stream = take(session.events(std::to_string(input_floor)));
    take(session.send("GO-BLOCK"));
    wait_kind(input_stream, "msg.text.delta");
    const auto caps = take(session.input_capabilities());
    Input input;
    input.input_id = "cpp-same-turn-original";
    input.target = {text(caps.at("target"), "historyEpoch"), text(caps.at("target"), "turnId")};
    input.content.text = "CPP-INSERTED actual consumption";
    input.ack = "memory";
    auto durable = input;
    durable.input_id = "cpp-durable-unsupported";
    durable.ack = "durable";
    require(caps.at("durableAck").is_bool() && !caps.at("durableAck").as_bool(),
            "fixture durable setting changed");
    require(!session.submit_input(durable), "durable unsupported downgraded to memory");
    std::promise<void> start;
    const auto gate = start.get_future().share();
    auto one = std::async(std::launch::async, [&] {
        gate.wait();
        return session.submit_input(input);
    });
    auto two = std::async(std::launch::async, [&] {
        gate.wait();
        return session.submit_input(input);
    });
    start.set_value();
    const auto first = take(one.get());
    const auto duplicate = take(two.get());
    require(same(first, duplicate) && equal(first.at("receipt").at("state"), "accepted"),
            "input replay changed original receipt or acceptance became consumption");
    receipt.set("concurrentInput", Json::object({{"callers", 2},
                                                 {"sharedStartBarrier", true},
                                                 {"sameReceipt", true},
                                                 {"acceptedReceipt", first.at("receipt")}}));
    auto changed = input;
    changed.content.text = "altered intent";
    input_rejected(session.submit_input(changed), "input_conflict", 409);
    auto wrong = input;
    wrong.input_id = "cpp-wrong-turn";
    wrong.target.turn_id = "not-the-active-turn";
    input_rejected(session.submit_input(wrong), "turn_mismatch", 409);
    auto other = sessions(info, "sdk1", runtime, true);
    CallOptions foreign;
    foreign.parameters = {{"id", session.id()}};
    foreign.body =
        Json::object({{"inputId", input.input_id},
                      {"target", Json::object({{"historyEpoch", input.target.history_epoch},
                                               {"turnId", input.target.turn_id}})},
                      {"content", Json::object({{"text", *input.content.text}})},
                      {"ack", "memory"}});
    rejected(other.api()->call("session.input.submit", std::move(foreign)), {403, 404});
    control(Json::object(
        {{"command", "set-auth"}, {"requestId", "cpp-input-revoke-auth"}, {"allowed", false}}));
    rejected(session.submit_input(input), {401, 403});
    control(Json::object(
        {{"command", "set-auth"}, {"requestId", "cpp-input-restore-auth"}, {"allowed", true}}));
    control(Json::object({{"command", "release-model"}, {"requestId", "cpp-release-input"}}));
    auto current = take(TurnTracker::resume(input_floor, input.target.turn_id));
    finish(input_stream, current, OutcomeStatus::completed);
    input_stream.shutdown();
    wait_idle(session);
    const auto consumed = take(session.input_status(input.input_id, input.target));
    require(equal(consumed.at("receipt").at("state"), "consumed"),
            "input was not actually consumed");
    require(take(session.history(0, 30)).dump().find("CPP-INSERTED actual consumption") !=
                std::string::npos,
            "history omitted consumed input");
    require(same(take(session.submit_input(input)).at("receipt"), consumed.at("receipt")),
            "ended-turn duplicate lost original receipt");
    auto late = input;
    late.input_id = "cpp-new-input-after-end";
    input_rejected(session.submit_input(late), "turn_closed", 409);
    take(session.restore(checkpoint.checkpoint_id, false));
    input_rejected(session.input_status(input.input_id, input.target), "input_not_found", 404);
    input_rejected(session.submit_input(input), "epoch_mismatch", 409);
    require(take(session.meta()).status == "idle", "old epoch started another turn");
    take(session.close());
    record(receipt, "same-turn-concurrent-input-consumption-duplicate-conflict-epoch-authority");
}
Answer original_answer(const Json &request) {
    const auto &question = request.at("questions").at(0);
    return Answer{text(question, "id"), {text(question.at("options").at(0), "id")}, {}};
}
void questions(const Json &info, const std::shared_ptr<Runtime> &runtime, Json &receipt) {
    const auto client = sessions(info, "sdk1", runtime);
    auto expired = make_session(client, "unused-question-expiry");
    auto expiry_stream = take(expired.events());
    take(expired.send("GO-QUESTION"));
    const auto old = wait_kind(expiry_stream, "server.question.request");
    expiry_stream.shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(750));
    rejected(expired.answer(text(old, "requestId"), {original_answer(old)}), {410});
    take(expired.close());
    record(receipt, "question-disconnected-ticket-expires-no-default-answer");

    auto session = make_session(client, "unused-question-current");
    const auto floor = take(session.meta()).last_seq;
    auto stream = take(session.events(std::to_string(floor)));
    take(session.send("GO-QUESTION model says approved but only the user may answer"));
    const auto request = wait_kind(stream, "server.question.request");
    const auto ticket = text(request, "requestId");
    const auto answer = original_answer(request);
    auto wrong = answer;
    wrong.selected_option_ids = {"invented-option"};
    rejected(session.answer(ticket, {wrong}), {400});
    rejected(session.answer("wrong-ticket", {answer}), {404});
    require(take(session.meta()).status == "running", "invalid question answer completed turn");
    auto other = sessions(info, "sdk1", runtime, true);
    CallOptions foreign;
    foreign.parameters = {{"id", session.id()}, {"ticketId", ticket}};
    foreign.body = Json::object(
        {{"answers",
          Json::array({Json::object(
              {{"questionId", answer.question_id},
               {"selectedOptionIds", Json::array({answer.selected_option_ids.front()})}})})}});
    rejected(other.api()->call("session.question.answer", std::move(foreign)), {403, 404});

    auto switched = std::make_shared<std::atomic<bool>>(false);
    ClientOptions changing;
    changing.base_url = text(info, "baseURL");
    changing.family = "sdk1";
    const auto owner_token = text(info, "token"), other_token = text(info, "otherToken");
    changing.token_provider = [switched, owner_token,
                               other_token](CancellationToken) -> Result<AuthToken> {
        return switched->load() ? AuthToken{other_token, "fixture-other"}
                                : AuthToken{owner_token, "fixture-owner"};
    };
    auto changing_client = take(SessionClient::create(take(ApiClient::create(changing, runtime))));
    auto retained = take(changing_client.attach(session.id()));
    switched->store(true);
    const auto changed = retained.answer(ticket, {answer});
    require(!changed && changed.error().code == ErrorCode::permission &&
                changed.error().http_status == 0,
            "same-client principal change did not stop before ticket use");
    control(Json::object(
        {{"command", "set-auth"}, {"requestId", "cpp-question-revoke"}, {"allowed", false}}));
    rejected(session.answer(ticket, {answer}), {401, 403});
    control(Json::object(
        {{"command", "set-auth"}, {"requestId", "cpp-question-restore"}, {"allowed", true}}));
    require(take(session.answer(ticket, {answer})).accepted,
            "original current question answer rejected");
    stream.shutdown();
    auto replay = take(session.events(std::to_string(floor)));
    auto tracker = take(TurnTracker::create(floor));
    finish(replay, tracker, OutcomeStatus::completed);
    replay.shutdown();
    wait_idle(session);
    rejected(session.answer(ticket, {answer}), {409});
    auto second = make_session(client, "unused-question-other-session");
    rejected(second.answer(ticket, {answer}), {404});
    require(take(session.history(0, 30)).dump().find("invented-option") == std::string::npos,
            "rejected question answer entered history");
    take(second.close());
    take(session.close());
    record(receipt, "question-original-options-wrong-ticket-subject-principal-revocation-reuse");
}

namespace ex = tansr::executor;
struct PermissionWorker {
    CancellationSource cancel;
    std::thread thread;
    std::optional<Error> error;
    explicit PermissionWorker(const std::shared_ptr<ex::Runner> &runner) {
        thread = std::thread([this, runner] {
            auto result = runner->run(cancel.token());
            if (!result)
                error = result.error();
        });
    }
    void stop() {
        cancel.cancel();
        if (thread.joinable())
            thread.join();
    }
    void check_stopped() const {
        require(error && error->code == ErrorCode::cancelled,
                "permission worker failed before explicit local stop");
    }
    ~PermissionWorker() { stop(); }
};
void permissions(const Json &info, const std::shared_ptr<Runtime> &runtime,
                 const std::filesystem::path &workspace, Json &receipt) {
    const auto client = sessions(info, "sdk1", runtime);
    std::optional<std::string> expired_ticket, expired_digest;
    for (int round = 0; round < 2; ++round) {
        CreateOptions create;
        create.client_tools = std::vector<Json>{info.at("declaration")};
        auto session = take(client.create(create));
        const ex::Scope scope{text(info, "applicationScopeId"), text(info, "endUserId"),
                              text(info, "authorizationRevision")};
        auto executor = take(ex::Client::create(client.api(), scope));
        const auto platform = ex::Platform::current();
        const ex::Workspace target{"cpp-permission-workspace", "1"};
        const auto digest = text(info, "definitionDigest");
        require(take(ex::definition_digest(info.at("declaration"))) == digest,
                "permission fixture declaration digest changed");
        const ex::Registration registration{
            "go-executor", platform, {target}, {"tool.invoke"}, {{"BusinessLookup", digest}}, {}};
        const auto connection = take(executor->register_executor(registration));
        const auto initialized = take(
            executor->initialize(session.id(), platform, std::vector<std::string>{"BusinessLookup"},
                                 take(session.capabilities()).closure_id));
        take(executor->bind(session.id(), connection, target, initialized.capability_revision,
                            take(session.capabilities()).closure_id));
        const auto journal_path =
            workspace / (round == 0 ? "permission-expired" : "permission-current");
        auto journal =
            take(ex::FileJournal::open(journal_path, []() -> Result<void> { return {}; }));
        std::atomic<int> executions{0};
        ex::RunnerOptions options;
        options.client = executor;
        options.registration = registration;
        options.journal = journal;
        options.poll_interval = std::chrono::milliseconds(20);
        options.authorize = [scope, target, id = session.id()](
                                const ex::Operation &op, CancellationToken cancel) -> Result<void> {
            if (cancel.is_cancelled())
                return Error{ErrorCode::cancelled, "permission test cancelled"};
            if (op.session_id != id ||
                op.scope.application_scope_id != scope.application_scope_id ||
                op.scope.end_user_id != scope.end_user_id ||
                op.scope.authorization_revision != scope.authorization_revision ||
                op.binding.target.workspace_id != target.workspace_id)
                return Error{ErrorCode::permission, "permission test scope mismatch"};
            return {};
        };
        options.tools = {
            {"BusinessLookup",
             {digest, [&](ex::ToolContext, Json) -> ex::ToolResult {
                  ++executions;
                  return Json::object(
                      {{"status", "ok"},
                       {"content", Json::array({Json::object(
                                       {{"t", "text"}, {"text", "go-terminal-fact"}})})}});
              }}}};
        auto runner = take(ex::Runner::create(std::move(options), connection));
        const auto floor = take(session.meta()).last_seq;
        auto stream = take(session.events(std::to_string(floor)));
        auto worker = std::make_unique<PermissionWorker>(runner);
        take(session.send("GO-TOOL"));
        const auto ticket = wait_kind(stream, "server.permission.request");
        require(equal(ticket.at("name"), "BusinessLookup"),
                "question confused with tool permission");
        const auto id = text(ticket, "requestId"), original_digest = text(ticket, "digest");
        const auto no_execution = [&] {
            require(executions.load() == 0, "invalid permission invoked business handler");
            require(take(executor->poll(connection)).operations.empty(),
                    "invalid permission dispatched an executor operation");
            for (const auto &entry : std::filesystem::directory_iterator(journal_path))
                require(entry.path().extension() != ".claim" &&
                            entry.path().extension() != ".receipt",
                        "invalid permission wrote execution fact");
        };
        if (round == 0) {
            require(same(ticket.at("ttlMs"), info.at("permissionTimeoutMs")),
                    "permission fixture timeout changed");
            for (;;) {
                auto event = next(stream);
                if (event.kind() == "tool.permission.decided")
                    require(!equal(event.raw().at("decision"), "allow"),
                            "expired permission auto-approved");
                if (event.kind() == "server.permission.closed" &&
                    equal(event.raw().at("requestId"), id))
                    break;
                require(!event.turn_outcome(), "permission turn ended without closure");
            }
            rejected(session.permission(id, original_digest, "allow"), {409, 410});
            no_execution();
            receipt.set("expiredPermissionHandlerCalls", executions.load());
            expired_ticket = id;
            expired_digest = original_digest;
            record(receipt, "permission-expiry-no-auto-approval-no-dispatch-no-handler");
        } else {
            require(id != *expired_ticket, "permission ticket identity reused");
            const auto wrong_digest = original_digest == std::string(64, '0')
                                          ? std::string(64, '1')
                                          : std::string(64, '0');
            auto other = sessions(info, "sdk1", runtime, true);
            CallOptions foreign;
            foreign.parameters = {{"id", session.id()}, {"ticketId", id}};
            foreign.body = Json::object({{"digest", original_digest}, {"verdict", "allow"}});
            auto wrong = std::async(std::launch::async,
                                    [&] { return session.permission(id, wrong_digest, "allow"); });
            auto old = std::async(std::launch::async, [&] {
                return session.permission(*expired_ticket, *expired_digest, "allow");
            });
            auto outside = std::async(std::launch::async, [&] {
                return other.api()->call("session.permission.decide", foreign);
            });
            rejected(wrong.get(), {409});
            rejected(old.get(), {404, 409, 410});
            rejected(outside.get(), {403, 404});
            no_execution();
            worker->stop();
            worker->check_stopped();
            control(Json::object({{"command", "set-auth"},
                                  {"requestId", "cpp-permission-revoke"},
                                  {"allowed", false}}));
            rejected(session.permission(id, original_digest, "allow"), {401, 403});
            require(executions.load() == 0, "revoked permission invoked handler");
            control(Json::object({{"command", "set-auth"},
                                  {"requestId", "cpp-permission-restore"},
                                  {"allowed", true}}));
            no_execution();
            worker = std::make_unique<PermissionWorker>(runner);
            require(take(session.permission(id, original_digest, "allow")).accepted,
                    "current valid permission did not release exact call");
            stream.shutdown();
            auto replay = take(session.events(std::to_string(floor)));
            auto tracker = take(TurnTracker::create(floor));
            finish(replay, tracker, OutcomeStatus::completed);
            replay.shutdown();
            require(executions.load() == 1,
                    "exact current permission did not execute exactly once");
            receipt.set("invalidPermissionHandlerCalls", 0);
            receipt.set("validCurrentPermissionHandlerCalls", executions.load());
            record(receipt,
                   "permission-wrong-digest-old-ticket-subject-revocation-handler-zero-then-one");
        }
        worker->stop();
        worker->check_stopped();
        stream.shutdown();
        take(session.close());
    }
}
void lost_create(const Json &info, const std::shared_ptr<Runtime> &runtime, Json &receipt) {
    auto client = sessions(info, "sdk1", runtime);
    CreateOptions create;
    create.write.request_key = "cpp-sdk1-create-loss";
    const auto result = client.create(create);
    require(!result && (result.error().code == ErrorCode::network ||
                        result.error().code == ErrorCode::timeout),
            "lost SDK1 create must remain an unresolved transport outcome");
    receipt.set("outcome", "unknown");
    receipt.set("sessionIdKnown", false);
    receipt.set("recoveryAction", "none-no-contract-recovery-path");
    // 不查询列表、不猜 sessionId，不重建。宿主代理独立证明原 POST 已由真实 Serve 受理。
    record(receipt, "sdk1-create-response-lost-unknown-no-retry-no-list-no-create-status");
}
void disabled_and_prompt(const Json &info, const std::shared_ptr<Runtime> &runtime, Json &receipt) {
    auto monitored = info;
    monitored.set("baseURL", text(info, "proxyBaseURL"));
    const auto offload = sessions(monitored, "sdk2-offload-v1", runtime);
    CreateOptions disabled_create;
    disabled_create.request_id = "cpp-unavailable-offload";
    auto missing = offload.create(disabled_create);
    require(!missing && missing.error().code == ErrorCode::http &&
                missing.error().http_status == 404,
            "unavailable offload family did not reject before creation");
    receipt.set("unavailableFamilyDiscoveryStatus", missing.error().http_status);
    record(receipt, "offload-family-unavailable-no-create-no-sdk1-fallback");

    const auto client = sessions(info, "sdk1", runtime);
    auto session = make_session(client, "unused-media-disabled");
    require(!take(session.meta()).raw.find("applicationPrompt"),
            "ordinary metadata unexpectedly exposes application prompt");
    const auto prompt = take(session.application_prompt_meta()).raw.at("applicationPrompt");
    require(prompt.is_object() && prompt.as_object().size() == 2 &&
                equal(prompt.at("policy"), "fallback") && equal(prompt.at("source"), "none"),
            "Serve default application prompt facts changed");
    complete(session, "CPP-DISABLED-MEDIA-TEXT");
    require(same(take(session.application_prompt_meta()).raw.at("applicationPrompt"), prompt),
            "C++ user prompt replaced platform/host system facts");
    receipt.set("applicationPrompt", prompt);
    record(receipt, "explicit-application-prompt-default-readback-no-client-override");
    const auto before = take(session.history(0, 0));
    TranscriptionRequest asr;
    asr.audio = "data:audio/wav;base64,UklGRg==";
    SpeechRequest speech;
    speech.input = "synthetic disabled speech";
    require(!session.transcribe(asr) && !session.speak(speech),
            "disabled media returned synthetic success");
    require(same(take(session.history(0, 0)).at("total"), before.at("total")),
            "disabled speech requests inserted history");
    const auto floor = take(session.meta()).last_seq;
    auto tracker = take(TurnTracker::create(floor));
    auto stream = take(session.events(std::to_string(floor)));
    require(take(session.send_blocks({ImageBlock{
                     "image/png", "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/"
                                  "x8AAwMCAO+y9k8AAAAASUVORK5CYII="}}))
                .accepted,
            "image route did not accept syntactically valid request");
    finish(stream, tracker, OutcomeStatus::failed);
    stream.shutdown();
    wait_idle(session);
    take(session.close());
    record(receipt, "media-disabled-speech-rejected-image-202-followed-by-failed");
    receipt.at("pending").as_array().emplace_back(
        "nonempty-platform-system-prompt-policy-needs-configurable-fixture");
}
void offload_create_loss(const Json &info, const std::shared_ptr<Runtime> &runtime, Json &receipt) {
    auto losing_info = info;
    losing_info.set("baseURL", text(info, "proxyBaseURL"));
    const auto client = sessions(losing_info, "sdk2-offload-v1", runtime);
    CreateOptions original;
    original.request_id = "cpp-offload-original-create";
    original.prompt = "CPP-OFFLOAD-ORIGINAL-PROMPT";
    const auto lost = client.create(original);
    require(!lost && (lost.error().code == ErrorCode::network ||
                      lost.error().code == ErrorCode::timeout),
            "lost offload create response did not retain unknown outcome");
    receipt.set("initialOutcome", "unknown");
    // 原创建身份与原体显式重取；不猜 sessionId、不发明查询路由、不换 requestId。
    const auto recovered = take(client.create(original));
    auto changed = original;
    changed.prompt = "CPP-OFFLOAD-CHANGED-PROMPT";
    rejected(client.create(changed), {409});
    auto attached = take(sessions(info, "sdk2-offload-v1", runtime).attach(recovered.id()));
    wait_idle(attached);
    const auto history = take(attached.history(0, 50));
    require(history.at("total").as_u64() == 2 &&
                history.dump().find("CPP-OFFLOAD-ORIGINAL-PROMPT") != std::string::npos &&
                history.dump().find("CPP-OFFLOAD-CHANGED-PROMPT") == std::string::npos,
            "offload create reconciliation duplicated or changed initial turn");
    require(complete(attached, "CPP-OFFLOAD-AFTER-RECOVERY").find("go-archive-second") !=
                std::string::npos,
            "reconciled offload identity did not continue original model/history");
    receipt.set("sessionId", recovered.id());
    receipt.set("requestId", *original.request_id);
    take(attached.close());
    record(receipt, "offload-lost-201-original-create-identity-200-same-session-conflict-409");
}
void interrupt_loss(const Json &info, const std::shared_ptr<Runtime> &runtime, Json &receipt) {
    const auto client = sessions(info, "sdk1", runtime);
    auto session = make_session(client, "unused-interrupt-loss");
    auto observing = take(session.events());
    take(session.send("GO-BLOCK"));
    wait_kind(observing, "msg.text.delta");
    const auto capabilities = take(session.input_capabilities());
    const auto turn = text(capabilities.at("target"), "turnId");
    const auto floor = take(session.meta()).last_seq;
    observing.shutdown();
    require(take(session.meta()).status == "running", "local stream drop interrupted Serve");
    auto stream = take(session.events(std::to_string(floor)));
    auto tracker = take(TurnTracker::resume(floor, turn));
    auto losing_info = info;
    losing_info.set("baseURL", text(info, "proxyBaseURL"));
    const auto original = take(sessions(losing_info, "sdk1", runtime).attach(session.id()));
    WriteOptions write;
    write.request_key = "cpp-original-lost-interrupt";
    const auto lost = original.interrupt(write);
    require(!lost && (lost.error().code == ErrorCode::network ||
                      lost.error().code == ErrorCode::timeout),
            "lost interrupt response did not retain unknown outcome");
    receipt.set("initialOutcome", "unknown");
    finish(stream, tracker, OutcomeStatus::aborted);
    stream.shutdown();
    wait_idle(session);
    require(original.id() == session.id() && take(original.meta()).status == "idle",
            "interrupt loss reconciliation changed original identity");
    receipt.set("sessionId", session.id());
    receipt.set("turnId", turn);
    receipt.set("processedWatermark", std::to_string(floor));
    take(session.close());
    record(receipt, "lost-real-202-interrupt-original-session-current-turn-aborted-no-retry");
}
void process_restart(const Json &info, const std::shared_ptr<Runtime> &runtime, Json &receipt) {
    auto session = make_session(sessions(info, "sdk1", runtime), "unused-restart");
    complete(session, "CPP-BEFORE-PROCESS-RESTART");
    const auto checkpoint = take(session.checkpoint("cpp-independent-restart"));
    const auto exported = take(session.export_checkpoint(checkpoint.checkpoint_id));
    const auto history = take(session.history(0, 50));
    const auto floor = take(session.meta()).last_seq;
    auto ending = take(session.events(std::to_string(floor)));
    take(session.close());
    wait_kind(ending, "session.ended");
    require(ending.last_event_id().has_value(), "processed terminal cursor missing");
    const auto processed = *ending.last_event_id();
    ending.shutdown();
    // 宿主只重启原 Node 进程与目录，SDK 用新端点对账原 sessionId。
    const auto next_info = control(
        Json::object({{"requestId", "cpp-restart-original-serve"}, {"command", "restart-serve"}}));
    require(text(next_info, "mode") == "session" &&
                text(next_info, "baseURL") != text(info, "baseURL"),
            "independent Serve restart did not provide a new endpoint");
    const auto client = sessions(next_info, "sdk1", runtime);
    const auto dormant = take(client.attach(session.id()));
    require(!take(dormant.meta()).live, "restarted host unexpectedly had a live old session");
    auto resumed = take(client.resume(session.id()));
    require(resumed.id() == session.id() && resumed.created().resumed &&
                resumed.created().last_seq > std::stoull(processed),
            "process restart did not resume original identity/watermark");
    require(same(take(resumed.history(0, 50)).at("messages"), history.at("messages")),
            "process restart changed persisted history");
    require(take(resumed.export_checkpoint(checkpoint.checkpoint_id)) == exported,
            "process restart changed raw checkpoint export bytes");
    auto replay = take(resumed.events(processed));
    const auto event = next(replay);
    require(event.kind() != "server.replay.gap" && replay.last_event_id() &&
                std::stoull(*replay.last_event_id()) > std::stoull(processed),
            "process restart did not replay beyond the processed watermark");
    replay.shutdown();
    complete(resumed, "CPP-AFTER-PROCESS-RESTART");
    require(take(resumed.history(0, 0)).at("total").as_u64() == history.at("total").as_u64() + 2,
            "resumed process did not append exactly one new turn");
    require(take(client.list(0, 50)).total == 1, "process restart created a replacement session");
    receipt.set("sessionId", resumed.id());
    receipt.set("processedWatermark", processed);
    take(resumed.close());
    record(receipt, "independent-serve-process-restart-history-checkpoint-watermark-original-id");
    receipt.at("pending").as_array().emplace_back(
        "offload-independent-restart-needs-persistent-cold-reopen-fixture");
}
void callback_release(const Json &info, const std::shared_ptr<Runtime> &runtime, Json &receipt) {
    const auto client = sessions(info, "sdk1", runtime);
    auto owned_session = std::make_unique<Session>(make_session(client, "unused-callback-release"));
    const auto id = owned_session->id();
    auto owned_stream = std::make_unique<SessionEventStream>(take(owned_session->events()));
    take(owned_session->send("GO-BLOCK"));
    wait_kind(*owned_stream, "msg.text.delta");
    const auto turn = text(take(owned_session->input_capabilities()).at("target"), "turnId");
    const auto floor = take(owned_session->meta()).last_seq;
    std::atomic<int> callbacks{0};
    std::atomic<bool> successful_response{false};
    HttpRequest request;
    request.method = "GET";
    request.url = text(info, "baseURL") + "/api/sessions/" + id;
    request.headers = {{"authorization", "Bearer " + text(info, "token")},
                       {"tansr-session-family", "sdk1"}};
    request.deadline_ms = client.api()->default_deadline_ms();
    // 最终 Runtime 所有者由 main 保持；完成回调只释放公开 Session/Stream。
    auto handle = take(runtime->request_async(request, {}, [&](Result<HttpResponse> response) {
        successful_response = response && response.value().status == 200;
        owned_stream.reset();
        owned_session.reset();
        ++callbacks;
    }));
    require(take(handle.wait()).status == 200, "callback trigger request did not reach real Serve");
    take(handle.wait_callback());
    require(handle.callback_ready() && callbacks.load() == 1 && successful_response.load() &&
                !owned_session && !owned_stream,
            "public object callback destruction did not complete exactly once");
    const auto original = take(client.attach(id));
    require(take(original.meta()).status == "running",
            "public object destruction automatically interrupted or closed remote session");
    auto observing = take(original.events(std::to_string(floor)));
    auto tracker = take(TurnTracker::resume(floor, turn));
    take(original.interrupt());
    finish(observing, tracker, OutcomeStatus::aborted);
    observing.shutdown();
    wait_idle(original);
    take(original.close());
    receipt.set("callbackCount", callbacks.load());
    receipt.set("runtimeFinalOwner", "main-thread");
    record(receipt, "callback-destroys-public-session-and-stream-once-remote-turn-stays-running");
}
void platform_prompt(const Json &info, const std::shared_ptr<Runtime> &runtime, Json &receipt) {
    const auto policy = text(info, "promptPolicy");
    require(policy == "fallback" || policy == "prepend", "unexpected platform prompt policy");
    auto session = make_session(sessions(info, "sdk1", runtime), "unused-platform-prompt");
    const auto expected = Json::object(
        {{"policy", policy}, {"source", policy == "fallback" ? "sdk" : "platform+sdk"}});
    require(same(take(session.application_prompt_meta()).raw.at("applicationPrompt"), expected),
            "initial configured platform/host prompt projection differs");
    for (int round = 0; round < 2; ++round) {
        complete(session, "CPP-USER-TURN-IS-NOT-SYSTEM-OVERRIDE");
        const auto meta = take(session.application_prompt_meta()).raw;
        require(same(meta.at("applicationPrompt"), expected),
                "configured platform/host prompt projection changed across user turns");
        const auto serialized = meta.dump();
        require(serialized.find("CPP_PLATFORM_P") == std::string::npos &&
                    serialized.find("CPP_HOST_S") == std::string::npos &&
                    serialized.find("CPP_APPEND_A") == std::string::npos,
                "metadata leaked configured system prompt text");
    }
    const auto observations = control(Json::object(
        {{"requestId", "cpp-prompt-observations"}, {"command", "prompt-observations"}}));
    const auto expected_wire = policy == "fallback"
                                   ? Json::array({"CPP_HOST_S", "CPP_APPEND_A"})
                                   : Json::array({"CPP_PLATFORM_P", "CPP_HOST_S", "CPP_APPEND_A"});
    require(equal(observations.at("policy"), policy) &&
                observations.at("exchanges").as_array().size() == 2,
            "fixture did not observe exactly two real model exchanges");
    for (const auto &exchange : observations.at("exchanges").as_array())
        require(same(exchange, expected_wire),
                "real model system segments changed precedence/order");
    require(!take(session.meta()).raw.find("applicationPrompt"),
            "ordinary metadata unexpectedly includes prompt provenance");
    receipt.set("applicationPrompt", expected);
    receipt.set("observedSyntheticSystemSegments", observations);
    take(session.close());
    require(!take(session.application_prompt_meta()).raw.find("applicationPrompt"),
            "dormant prompt provenance was guessed");
    record(receipt, "configured-platform-host-append-real-model-order-and-opt-in-readback");
}
void offload_restart(const Json &info, const std::shared_ptr<Runtime> &runtime, Json &receipt) {
    require(text(info, "persistenceMode") == "create" &&
                text(info, "coldStore") == "kernel-createFsBlobStore",
            "offload restart fixture did not start original filesystem cold store");
    CreateOptions original;
    original.request_id = "cpp-offload-restart-original";
    original.prompt = "CPP-OFFLOAD-COLD-ORIGINAL " + std::string(4096, 'x');
    auto session = take(sessions(info, "sdk2-offload-v1", runtime).create(original));
    auto stream = take(session.events());
    auto tracker = take(TurnTracker::from_replay(0));
    finish(stream, tracker, OutcomeStatus::completed);
    require(stream.last_event_id().has_value(), "offload processed watermark missing");
    const auto processed = *stream.last_event_id();
    stream.shutdown();
    wait_idle(session);
    const auto history = take(session.history(0, 50));
    require(history.at("total").as_u64() == 2, "offload original create did not finish one turn");
    const auto checkpoint = take(session.checkpoint("cpp-offload-cold-restart"));
    const auto exported = take(session.export_checkpoint(checkpoint.checkpoint_id));
    const auto next_info = control(
        Json::object({{"requestId", "cpp-restart-offload-serve"}, {"command", "restart-serve"}}));
    require(text(next_info, "mode") == "archive-offload-durable" &&
                text(next_info, "persistenceMode") == "reopen" &&
                text(next_info, "coldStore") == text(info, "coldStore") &&
                text(next_info, "seedSha256") == text(info, "seedSha256") &&
                text(next_info, "sourceId") == text(info, "sourceId") &&
                text(next_info, "sourceGeneration") == text(info, "sourceGeneration"),
            "offload restart changed original source/seed/store mode");
    const auto client = sessions(next_info, "sdk2-offload-v1", runtime);
    auto resumed = take(client.create(original));
    record(receipt, "offload-reopened-original-create-returned");
    require(resumed.id() == session.id() && resumed.created().resumed,
            "offload source reopen changed original creation identity");
    require(same(take(resumed.history(0, 50)).at("messages"), history.at("messages")),
            "offload cold source recovery changed or reran original history");
    require(take(resumed.export_checkpoint(checkpoint.checkpoint_id)) == exported,
            "offload source reopen changed checkpoint bytes");
    record(receipt, "offload-reopened-history-and-checkpoint-original-bytes");
    receipt.set("reopenedLastSeq", std::to_string(take(resumed.meta()).last_seq));
    receipt.set("processedWatermark", processed);
    auto replay = take(resumed.events(processed));
    auto old_tracker = take(TurnTracker::from_replay(std::stoull(processed)));
    const auto gap = next(replay);
    require(gap.kind() == "server.replay.gap" && equal(gap.raw().at("reason"), "evicted") &&
                !old_tracker.observe(gap) && old_tracker.needs_reconciliation() &&
                replay.last_event_id() == std::optional<std::string>(processed),
            "offload retained-history gap did not invalidate the old tracker/watermark");
    receipt.set("restartReplayGap", gap.raw());
    replay.shutdown();
    const auto reconciled = take(resumed.meta());
    require(reconciled.session_id == session.id() && reconciled.last_seq > std::stoull(processed) &&
                same(take(resumed.history(0, 50)).at("messages"), history.at("messages")),
            "offload gap reconciliation changed original identity/history");
    record(receipt,
           "offload-restart-evicted-gap-invalidates-old-tracker-original-history-reconciled");
    replay = take(resumed.events(std::to_string(reconciled.last_seq)));
    auto replay_tracker = take(TurnTracker::create(reconciled.last_seq));
    take(resumed.send("CPP-OFFLOAD-AFTER-PROCESS-RESTART"));
    record(receipt, "offload-reopened-next-turn-accepted");
    require(finish(replay, replay_tracker, OutcomeStatus::completed).find("go-archive-answer") !=
                std::string::npos,
            "offload restored process did not execute exactly its first new model turn");
    require(replay.last_event_id() && std::stoull(*replay.last_event_id()) > std::stoull(processed),
            "offload recovered event watermark did not advance");
    replay.shutdown();
    wait_idle(resumed);
    require(take(resumed.history(0, 0)).at("total").as_u64() == 4 &&
                take(client.list(0, 50)).total == 1,
            "offload process recovery duplicated initial turn/session");
    receipt.set("sessionId", resumed.id());
    receipt.set("requestId", *original.request_id);
    receipt.set("sourceId", text(info, "sourceId"));
    receipt.set("sourceGeneration", text(info, "sourceGeneration"));
    receipt.set("seedSha256", text(info, "seedSha256"));
    receipt.set("processedWatermark", processed);
    take(resumed.close());
    record(receipt,
           "offload-independent-process-reopen-original-source-create-id-cold-history-watermark");
}
} // namespace

int main(int argc, char **argv) {
    Json receipt = Json::object({{"kind", "cpp-real-serve-session"},
                                 {"status", "running"},
                                 {"passed", Json::array()},
                                 {"pending", Json::array()}});
    std::filesystem::path output;
    std::shared_ptr<Runtime> runtime;
    try {
        require(argc == 4 || argc == 5,
                "usage: session-integration fixture-info.json family os-temp-workspace "
                "[normal|questions|permission|create-loss|disabled|offload-create-loss|"
                "interrupt-loss|process-restart|callback-release|platform-prompt|offload-restart]");
        const auto info = read_json(argv[1]);
        const std::string family = argv[2];
        const std::string variant = argc == 5 ? argv[4] : "normal";
        require(variant == "normal" || variant == "questions" || variant == "permission" ||
                    variant == "create-loss" || variant == "disabled" ||
                    variant == "offload-create-loss" || variant == "interrupt-loss" ||
                    variant == "process-restart" || variant == "callback-release" ||
                    variant == "platform-prompt" || variant == "offload-restart",
                "unknown session scenario");
        require(family == "sdk1" || family == "sdk2-offload-v1", "unknown session family");
        require(info.at("manifestRevision").as_u64() == 7, "fixture manifest revision mismatch");
        const auto mode = text(info, "mode");
        require((family == "sdk1" && variant == "platform-prompt" &&
                 (mode == "session-prompt-fallback" || mode == "session-prompt-prepend")) ||
                    (family == "sdk2-offload-v1" && variant == "offload-restart" &&
                     mode == "archive-offload-durable") ||
                    (family == "sdk1" && mode == (variant == "permission" ? "execution"
                                                  : variant == "disabled" ? "session-media-disabled"
                                                                          : "session")) ||
                    (family == "sdk2-offload-v1" && mode == "archive-offload" &&
                     (variant == "normal" || variant == "offload-create-loss")),
                "fixture family/mode mismatch");
        const auto workspace = std::filesystem::canonical(argv[3]);
        require(std::filesystem::is_directory(workspace), "OS temp workspace required");
        output = workspace / "session-integration.json";
        receipt.set("family", family);
        receipt.set("fixtureMode", mode);
        receipt.set("variant", variant);
        runtime = take(Runtime::create());
        if (variant == "questions")
            questions(info, runtime, receipt);
        else if (variant == "permission")
            permissions(info, runtime, workspace, receipt);
        else if (variant == "create-loss")
            lost_create(info, runtime, receipt);
        else if (variant == "disabled")
            disabled_and_prompt(info, runtime, receipt);
        else if (variant == "offload-create-loss")
            offload_create_loss(info, runtime, receipt);
        else if (variant == "interrupt-loss")
            interrupt_loss(info, runtime, receipt);
        else if (variant == "process-restart")
            process_restart(info, runtime, receipt);
        else if (variant == "callback-release")
            callback_release(info, runtime, receipt);
        else if (variant == "platform-prompt")
            platform_prompt(info, runtime, receipt);
        else if (variant == "offload-restart")
            offload_restart(info, runtime, receipt);
        else {
            receipt.set(
                "pending",
                Json::array({"independent-Serve-process-restart",
                             "accepted-interrupt-response-loss-proxy",
                             "offload-create-intent-response-loss", "concurrent-close-resume-races",
                             "behind-retention-replay-gap", "media-disabled-fixture",
                             "platform-prompt-configuration-readback"}));
            const auto client = sessions(info, family, runtime);
            auto session = make_session(client, "cpp-session-main");
            receipt.set("sessionId", session.id());
            const auto resumed = take(client.resume(session.id()));
            require(resumed.id() == session.id() && !resumed.created().resumed,
                    "live resume did not attach original identity");
            record(receipt, "create-live-resume");
            if (family == "sdk1") {
                const auto empty = take(session.compact());
                require(equal(empty.at("status"), "rejected"), "empty compaction reported success");
                record(receipt, "empty-compaction-rejected");
            }
            const auto first = complete(session, "CPP-FIRST"),
                       second = complete(session, "CPP-SECOND");
            if (family == "sdk1")
                require(first.find("go-real-serve-answer") != std::string::npos &&
                            second.find("go-real-serve-answer") != std::string::npos,
                        "model answer missing");
            else
                require(first.find("go-archive-answer") != std::string::npos &&
                            second.find("go-archive-second") != std::string::npos,
                        "offload model answers missing");
            record(receipt, "two-current-turn-terminals-with-original-turnId");
            const auto attached = take(client.attach(session.id()));
            const auto count = take(attached.history(0, 0));
            const auto history = take(attached.history(0, 20));
            require(count.at("messages").as_array().empty() && count.at("total").as_u64() >= 4,
                    "history count-only contract failed");
            require(same(count.at("total"), history.at("total")) &&
                        equal(history.at("sessionId"), session.id()),
                    "history identity/count changed");
            require(take(attached.meta()).last_seq > 0, "live sequence did not advance");
            record(receipt, "attach-history-count-only-and-content");
            local_cancel(attached);
            record(receipt, "local-stream-cancel-keeps-session-idle");
            owner_isolation(info, runtime, client, session);
            record(receipt, "other-principal-attach-resume-list-isolation");
            family_isolation(info, runtime, client, session);
            record(receipt, "wrong-family-attach-resume-no-replacement");
            if (family == "sdk1") {
                snapshot_and_media(info, runtime, client, session);
                record(receipt, "image-snapshot-roundtrip-restore-delete-media-compaction");
                persisted_resume_and_gap(client, session);
                record(receipt, "persisted-resume-and-ahead-gap-reconciliation");
                interrupt_and_input(info, runtime, client, receipt);
            } else {
                receipt.at("pending").as_array().emplace_back(
                    "offload-checkpoint-media-input-interrupt-not-covered-by-selected-archive-"
                    "fixture");
            }
            require(take(session.close()).accepted, "final close not accepted");
            record(receipt, "explicit-close");
        }
        receipt.set("status", "passed-covered-paths");
        const auto shutdown = take(runtime->shutdown(std::chrono::seconds(10)));
        require(shutdown == ShutdownStatus::stopped, "runtime shutdown still pending");
    } catch (const std::exception &error) {
        receipt.set("status", "failed");
        receipt.set("error", error.what());
        if (runtime) {
            runtime->stop();
            const auto ignored = runtime->shutdown(std::chrono::seconds(10));
            (void)ignored;
        }
    }
    if (!output.empty()) {
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        file << receipt.dump() << '\n';
        file.flush();
        if (!file) {
            receipt.set("status", "failed");
            receipt.set("error", "integration receipt write failed");
        }
    }
    std::cout << "TANSR_CPP_SESSION_RESULT " << receipt.dump() << std::endl;
    return equal(receipt.at("status"), "passed-covered-paths") ? 0 : 1;
}
