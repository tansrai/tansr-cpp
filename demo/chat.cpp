#include "common.hpp"
#include <condition_variable>
#include <deque>
#include <iostream>
#include <set>
#include <sstream>

namespace {
using namespace tansr;
using demo::take;
const char *help =
    "tansr-chat [--base ORIGIN] [--family sdk1|sdk2-offload-v1] [--resume ID | --attach ID]\n"
    "  [--model MODEL] [--message TEXT] [--last-event-id CURSOR] [--request-id STABLE_ID]\n"
    "  [--timeout SECONDS] [--token-file PRIVATE_FILE] [--scope-file PRIVATE_FILE]\n"
    "Offload durable creation (no turn is sent):\n"
    "  --prepare-create ABSOLUTE_PRIVATE_FILE --request-id STABLE_ID [--model MODEL]\n"
    "  --create-intent ORIGINAL_PRIVATE_FILE\n"
    "New offload sessions require --request-id. Credentials: TANSR_TOKEN_FILE and "
    "TANSR_SCOPE_FILE.\n"
    "Commands: /interrupt, /allow TICKET, /deny TICKET, /answers TICKET JSON_ARRAY,\n"
    "  /insert JSON_OBJECT, /target, /history, /quit. No approval is automatic.\n"
    "Ctrl+C and /quit cancel local observation only; /interrupt asks Serve to interrupt.\n";

// Demo 只开放 requestId/model 的创建子集；其余选项不能被静默丢弃或恢复时覆盖。
session::CreateOptions creation_options(const Json &saved, const demo::Host &host,
                                        CancellationToken cancel) {
    if (!saved.is_object() || saved.as_object().size() != 4 ||
        demo::field(saved, "format") != "tansr-cpp-session-create-v1")
        demo::fail("invalid session creation intent");
    if (saved.at("owner").dump() != demo::intent_owner(host).dump())
        demo::fail("creation intent belongs to another base, family or current scope",
                   ErrorCode::permission);
    const auto &body = saved.at("body"), &write = saved.at("write");
    if (!body.is_object() || !write.is_object() || write.as_object().size() != 2)
        demo::fail("invalid session creation body or write options");
    for (const auto &item : body.as_object())
        if (item.first != "requestId" && item.first != "model")
            demo::fail("unsupported stored session creation field");
    auto request = demo::field(body, "requestId"), key = demo::field(write, "requestKey");
    if (request.empty() || request.size() > 128 || key.empty() || key.size() > 128)
        demo::fail("creation request ID and request key are required");
    for (unsigned char c : request)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            demo::fail("creation request ID is invalid");
    for (unsigned char c : key)
        if (c < 33 || c > 126)
            demo::fail("creation request key is invalid");
    const auto deadline = write.at("deadlineMs").as_i64();
    if (deadline <= unix_time_ms())
        demo::fail("original creation deadline expired; retain intent for reconciliation",
                   ErrorCode::timeout);
    session::CreateOptions options;
    options.request_id = request;
    if (const auto *model = body.find("model")) {
        if (!model->is_string())
            demo::fail("stored creation model must be a string");
        options.model = model->as_string();
    }
    options.write = {key, deadline, cancel};
    return options;
}
void prepare_creation(const std::filesystem::path &path, const demo::Host &host,
                      const std::string &request, const std::optional<std::string> &model) {
    auto body = Json::object({{"requestId", request}});
    if (model)
        body.set("model", *model);
    auto saved = Json::object(
        {{"format", "tansr-cpp-session-create-v1"},
         {"owner", demo::intent_owner(host)},
         {"body", std::move(body)},
         {"write",
          Json::object({{"requestKey", demo::request_id()},
                        {"deadlineMs", Json(unix_time_ms() +
                                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                                host.config.timeout)
                                                .count())}})}});
    (void)creation_options(saved, host, {});
    const auto bytes = saved.dump();
    if (bytes.size() > 65536)
        demo::fail("session creation intent exceeds 64 KiB");
    auto directory = demo::private_directory(path.parent_path(), host);
    take(directory->write_atomic(path.filename().u8string(), bytes, false));
    std::cout
        << "session creation intent durably saved; no session was created; use --create-intent "
           "with the original file before its deadline\n";
}
session::CreateOptions load_creation(const std::filesystem::path &path, const demo::Host &host,
                                     CancellationToken cancel) {
    auto directory = demo::private_directory(path.parent_path(), host);
    auto bytes = take(directory->read(path.filename().u8string(), 65536));
    if (!bytes)
        demo::fail("original session creation intent missing", ErrorCode::not_found);
    return creation_options(take(Json::parse(*bytes, {65536, 16, 512})), host, cancel);
}

// 流读取和控制请求分离；满队列等待，不丢帧，也不累积无限正文。
class Events {
  public:
    Events(session::SessionEventStream stream, CancellationToken parent)
        : cancel_(CancellationToken::combine(local_.token(), parent)),
          worker_([this, stream = std::move(stream)]() mutable {
              try {
                  while (!cancel_.is_cancelled()) {
                      auto event = stream.next(cancel_);
                      std::unique_lock<std::mutex> lock(mutex_);
                      ready_.wait(lock,
                                  [&] { return queue_.size() < 16 || cancel_.is_cancelled(); });
                      if (cancel_.is_cancelled())
                          break;
                      const bool ended = !event || !event.value();
                      queue_.push_back(std::move(event));
                      if (ended)
                          break;
                  }
              } catch (...) {
                  std::lock_guard<std::mutex> lock(mutex_);
                  queue_.emplace_back(Error{ErrorCode::internal, "event reader stopped"});
              }
              stream.close();
          }) {}
    ~Events() {
        local_.cancel();
        ready_.notify_all();
        if (worker_.joinable())
            worker_.join();
    }
    std::optional<Result<std::optional<session::SessionEvent>>> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty())
            return {};
        auto result = std::move(queue_.front());
        queue_.pop_front();
        ready_.notify_one();
        return result;
    }

  private:
    CancellationSource local_;
    CancellationToken cancel_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Result<std::optional<session::SessionEvent>>> queue_;
    std::thread worker_;
};
struct Tickets {
    std::map<std::string, std::string> permissions;
    std::set<std::string> questions;
};
void display(const session::SessionEvent &event, Tickets &tickets) {
    const auto &raw = event.raw();
    const auto kind = event.kind();
    if (kind == "msg.text.delta") {
        std::cout << demo::safe(demo::field(raw, "text")) << std::flush;
        return;
    }
    const auto ticket = demo::field(raw, "requestId");
    if (kind == "server.permission.request") {
        const auto digest = demo::field(raw, "digest");
        if (ticket.empty() || digest.empty())
            demo::fail("approval has no identity", ErrorCode::contract);
        tickets.permissions[ticket] = digest;
        std::cout << "\n[permission " << demo::safe(ticket) << "] "
                  << demo::safe(demo::field(raw, "name")) << " "
                  << demo::safe(demo::field(raw, "summary")) << "\n/allow " << demo::safe(ticket)
                  << " or /deny " << demo::safe(ticket) << '\n';
    } else if (kind == "server.permission.closed")
        tickets.permissions.erase(ticket);
    else if (kind == "server.question.request") {
        const auto *questions = raw.find("questions");
        if (ticket.empty() || !questions || !questions->is_array())
            demo::fail("question has no identity", ErrorCode::contract);
        tickets.questions.insert(ticket);
        std::cout << "\n[question " << demo::safe(ticket) << "] " << demo::safe(questions->dump())
                  << "\n/answers TICKET JSON_ARRAY\n";
    } else if (kind == "server.question.closed")
        tickets.questions.erase(ticket);
    else if (kind == "server.tool.request")
        demo::fail("inline tool requests require an explicitly bound executor; run tansr-tools");
    else if (kind.rfind("msg.", 0) != 0)
        std::cout << "\n[event: " << demo::safe(kind) << "]\n";
}
session::Input input_from(const Json &json) {
    if (!json.is_object())
        demo::fail("insert requires an object");
    for (const auto &item : json.as_object())
        if (item.first != "inputId" && item.first != "target" && item.first != "content" &&
            item.first != "ack")
            demo::fail("insert contains an unsupported field");
    session::Input input;
    input.input_id = demo::field(json, "inputId");
    const auto &target = json.at("target");
    if (!target.is_object() || target.as_object().size() != 2)
        demo::fail("target requires historyEpoch and turnId only");
    input.target = {demo::field(target, "historyEpoch"), demo::field(target, "turnId")};
    const auto &content = json.at("content");
    const auto *text = content.find("text");
    if (!content.is_object() || content.as_object().size() != 1)
        demo::fail("this demo supports content.text only");
    if (text && text->is_string())
        input.content.text = text->as_string();
    else
        demo::fail("this demo insert requires content.text");
    const auto *ack = json.find("ack");
    if (ack) {
        if (!ack->is_string())
            demo::fail("ack must be a string");
        input.ack = ack->as_string();
    }
    return input;
}
std::vector<session::Answer> answers_from(const Json &json) {
    if (!json.is_array())
        demo::fail("answers must be an array");
    std::vector<session::Answer> answers;
    for (const auto &item : json.as_array()) {
        session::Answer answer;
        answer.question_id = demo::field(item, "questionId");
        if (const auto *ids = item.find("selectedOptionIds")) {
            if (!ids->is_array())
                demo::fail("selectedOptionIds must be an array");
            for (const auto &id : ids->as_array()) {
                if (!id.is_string())
                    demo::fail("option ID must be a string");
                answer.selected_option_ids.push_back(id.as_string());
            }
        }
        if (const auto *text = item.find("freeText")) {
            if (!text->is_string())
                demo::fail("freeText must be a string");
            answer.free_text = text->as_string();
        }
        answers.push_back(std::move(answer));
    }
    return answers;
}
void chat(const session::Session &current, const std::optional<std::string> &message,
          const std::optional<std::string> &supplied_cursor, CancellationToken cancel) {
    auto meta = take(current.meta());
    if (!meta.live || meta.status == "ended")
        demo::fail("session is not live");
    bool active = meta.status == "running";
    if (active && message)
        demo::fail("turn is running; attach without --message");
    auto floor = meta.last_seq;
    auto tracker = take(session::TurnTracker::create(floor));
    bool replaying = false;
    if (active) {
        auto caps = take(current.input_capabilities());
        const auto *target = caps.find("target");
        const auto turn = target ? demo::field(*target, "turnId") : "";
        if (!turn.empty())
            tracker = take(session::TurnTracker::resume(floor, turn));
        else {
            auto latest = take(current.meta());
            if (latest.live && latest.status == "idle") {
                active = false;
                floor = latest.last_seq;
                tracker = take(session::TurnTracker::create(floor));
            } else if (latest.live && latest.status == "running") {
                tracker = take(session::TurnTracker::from_replay(floor));
                replaying = true;
            } else
                demo::fail("running turn identity unavailable", ErrorCode::unknown);
        }
    }
    if (supplied_cursor && replaying && *supplied_cursor != "0")
        demo::fail("unknown active turn requires replay from cursor 0");
    auto cursor = supplied_cursor.value_or(active ? "0" : std::to_string(floor));
    Events events(take(current.events(cursor, cancel)), cancel);
    Tickets tickets;
    demo::Console console;
    if (message) {
        take(current.send(*message, demo::write_options(cancel)));
        active = true;
    }
    while (!cancel.is_cancelled()) {
        if (auto incoming = events.pop()) {
            auto event = take(std::move(*incoming));
            if (!event)
                demo::fail("stream EOF is not a turn completion", ErrorCode::unknown);
            if (event->kind() == "server.replay.gap")
                demo::fail("replay gap requires reconciliation", ErrorCode::unknown);
            display(*event, tickets);
            std::cout << std::flush;
            if (message && (event->kind() == "server.permission.request" ||
                            event->kind() == "server.question.request"))
                demo::fail(
                    "noninteractive run cannot answer; attach interactively to the same session");
            auto outcome = tracker.observe(*event);
            if (replaying) {
                const auto seq = demo::field(event->envelope, "eventId");
                if (!seq.empty() && std::stoull(seq) >= floor) {
                    replaying = false;
                    if (!tracker.active_turn_id() && !outcome) {
                        auto latest = take(current.meta());
                        if (latest.live && latest.status == "idle") {
                            active = false;
                            tracker = take(session::TurnTracker::create(latest.last_seq));
                            tickets = {};
                        } else
                            demo::fail("replay did not establish active turn", ErrorCode::unknown);
                    }
                }
            }
            if (outcome && active) {
                active = false;
                tickets = {};
                if (outcome->status != session::OutcomeStatus::completed)
                    demo::fail("turn did not complete", ErrorCode::unknown);
                std::cout << "\n[turn completed]\n" << std::flush;
                if (message || console.ended())
                    return;
            }
        }
        if (!message) {
            auto line = console.poll();
            if (console.ended() && !line && !active)
                return;
            if (line && !line->empty()) {
                std::istringstream words(*line);
                std::string command, ticket;
                words >> command;
                if (command.empty())
                    continue;
                if (command == "/quit") {
                    std::cout
                        << "local observation stopped; no remote interruption was requested\n";
                    return;
                }
                if (command == "/interrupt") {
                    take(current.interrupt(demo::write_options(cancel)));
                    std::cout << "interruption accepted; waiting for actual terminal event\n";
                } else if (command == "/allow" || command == "/deny") {
                    words >> ticket;
                    auto it = tickets.permissions.find(ticket);
                    if (it == tickets.permissions.end()) {
                        std::cout << "ticket absent or closed\n";
                        continue;
                    }
                    take(current.permission(ticket, it->second,
                                            command == "/allow" ? "allow" : "deny",
                                            demo::write_options(cancel)));
                    tickets.permissions.erase(it);
                } else if (command == "/answers") {
                    words >> ticket;
                    std::string body;
                    std::getline(words, body);
                    if (!tickets.questions.count(ticket)) {
                        std::cout << "question absent or closed\n";
                        continue;
                    }
                    auto answers = answers_from(take(Json::parse(body, {262144, 32, 100000})));
                    take(current.answer(ticket, std::move(answers), demo::write_options(cancel)));
                    tickets.questions.erase(ticket);
                } else if (command == "/insert") {
                    std::string body;
                    std::getline(words, body);
                    auto input = input_from(take(Json::parse(body, {262144, 32, 100000})));
                    take(current.submit_input(std::move(input), demo::write_options(cancel)));
                    std::cout << "input acknowledged; this is not core consumption\n";
                } else if (command == "/target")
                    std::cout << demo::safe(take(current.input_capabilities()).dump()) << '\n';
                else if (command == "/history")
                    std::cout << demo::safe(take(current.history(0, 0)).dump()) << '\n';
                else if (command.front() == '/')
                    std::cout << "unknown command; see --help\n";
                else {
                    if (active) {
                        std::cout << "turn is running; use /insert or wait\n";
                        continue;
                    }
                    auto before = take(current.meta());
                    if (before.status != "idle")
                        demo::fail("session is not idle");
                    tracker = take(session::TurnTracker::create(before.last_seq));
                    take(current.send(*line, demo::write_options(cancel)));
                    active = true;
                }
                // 控制回执必须立即交付管道宿主，不能等待下一条模型事件刷新缓冲。
                std::cout << std::flush;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    demo::fail("local observation cancelled", ErrorCode::cancelled);
}
} // namespace
int main(int argc, char **argv) {
    return demo::report("tansr-chat", [&] {
        demo::Args args(argc, argv);
        if (args.flag("--help")) {
            std::cout << help;
            return;
        }
        auto config = demo::Configuration::read(args);
        const auto resume = args.take("--resume"), attach = args.take("--attach"),
                   message = args.take("--message"), cursor = args.take("--last-event-id"),
                   model = args.take("--model"), prepare = args.take("--prepare-create"),
                   create_intent = args.take("--create-intent");
        if (resume && attach)
            demo::fail("use either --resume or --attach");
        auto request = args.take("--request-id");
        if ((prepare || create_intent) &&
            (config.family != "sdk2-offload-v1" || resume || attach || message || cursor ||
             (prepare && create_intent) || (create_intent && (request || model))))
            demo::fail("creation intent modes require offload and cannot override creation or "
                       "conversation fields");
        if (config.family == "sdk2-offload-v1" && !resume && !attach && !request && !create_intent)
            demo::fail("new offload session requires a caller-preserved --request-id");
        args.finish();
        demo::Host host(config);
        demo::Stop stop(config.timeout);
        if (prepare) {
            prepare_creation(demo::absolute_path(*prepare), host, *request, model);
            host.close();
            return;
        }
        {
            auto sessions = take(session::SessionClient::create(host.api, stop.token()));
            session::CreateOptions options;
            if (create_intent)
                options = load_creation(demo::absolute_path(*create_intent), host, stop.token());
            else {
                options.request_id = request;
                options.model = model;
                options.write = demo::write_options(stop.token());
            }
            auto current =
                resume ? take(sessions.resume(*resume, demo::write_options(stop.token())))
                       : (attach ? take(sessions.attach(*attach)) : take(sessions.create(options)));
            std::cout << "session: " << demo::safe(current.id()) << std::endl;
            if (!create_intent)
                chat(current, message, cursor, stop.token());
        }
        host.close();
    });
}
