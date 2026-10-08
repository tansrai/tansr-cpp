#include "tansr/executor.hpp"
#include "tansr/crypto.hpp"
#include "tansr/session.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
using namespace tansr;
namespace ex = tansr::executor;
namespace se = tansr::session;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message + " [" + result.error().wire_code + "]");
    return std::move(result.value());
}
Json read_info(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "fixture info unavailable");
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    return take(Json::parse(bytes));
}
void control(Json request) {
    std::cout << "TANSR_CPP_CONTROL " << request.dump() << std::endl;
    std::string line;
    check(bool(std::getline(std::cin, line)), "fixture control closed");
    constexpr std::string_view prefix = "TANSR_RUST_CONTROL ";
    if (line.compare(0, prefix.size(), prefix) == 0)
        line.erase(0, prefix.size());
    auto result = take(Json::parse(line));
    check(result.at("requestId").as_string() == request.at("requestId").as_string() &&
              result.at("ok").as_bool(),
          "fixture control rejected");
}
CallOptions output_query(const ex::Operation &op, const std::string &family) {
    CallOptions o;
    o.parameters = {{"id", op.session_id}};
    o.query = {{"contract", "terminal-services-v1"},
               {"sessionContract", family},
               {"operationId", op.operation_id},
               {"requestDigest", op.digest}};
    return o;
}
struct Observer {
    CancellationSource stop;
    se::SessionEventStream stream;
    std::thread thread;
    std::mutex mutex;
    std::string error;
    std::atomic<bool> completed{false};
    Observer(se::Session session)
        : stream(take(session.events(std::to_string(session.created().last_seq), stop.token()))) {
        thread = std::thread([this, session]() mutable {
            try {
                while (!stop.token().is_cancelled()) {
                    auto next = stream.next(stop.token());
                    if (!next) {
                        if (stop.token().is_cancelled())
                            return;
                        throw std::runtime_error(next.error().message);
                    }
                    if (!next.value())
                        throw std::runtime_error("EOF before completed turn");
                    auto &event = *next.value();
                    if (event.kind() == "server.permission.request") {
                        const auto &raw = event.raw();
                        se::WriteOptions options;
                        options.request_key = "cpp-executor-approval";
                        take(session.permission(raw.at("requestId").as_string(),
                                                raw.at("digest").as_string(), "allow", options));
                    }
                    if (auto result = event.turn_outcome()) {
                        check(result->status == se::OutcomeStatus::completed,
                              "tool turn did not complete");
                        completed = true;
                        return;
                    }
                }
            } catch (const std::exception &e) {
                std::lock_guard<std::mutex> lock(mutex);
                error = e.what();
            }
        });
    }
    void assert_healthy() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!error.empty())
            throw std::runtime_error(error);
    }
    ~Observer() {
        stop.cancel();
        if (thread.joinable())
            thread.join();
        stream.close();
    }
};
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc >= 4, "usage: executor-integration info.json family absolute-os-temp-workspace "
                         "[normal|policy|auth|foreign]");
        auto info = read_info(std::filesystem::u8path(argv[1]));
        const std::string family = argv[2], mode = argc > 4 ? argv[4] : "normal";
        auto workspace_path = std::filesystem::u8path(argv[3]);
        check(workspace_path.is_absolute(), "workspace must be absolute");
        check(std::filesystem::exists(workspace_path), "fixture workspace missing");
        workspace_path = std::filesystem::canonical(workspace_path);
        auto runtime = take(Runtime::create());
        ClientOptions options;
        options.base_url = info.at("baseURL").as_string();
        options.family = family;
        const auto token = info.at("token").as_string();
        options.token_provider = [token](CancellationToken) -> Result<AuthToken> {
            return AuthToken{token, "synthetic/app-user"};
        };
        auto api = take(ApiClient::create(options, runtime));
        auto session_client = take(se::SessionClient::create(api));
        se::CreateOptions create;
        create.client_tools = std::vector<Json>{info.at("declaration")};
        auto session = take(session_client.create(create));
        ex::Scope scope{info.at("applicationScopeId").as_string(), info.at("endUserId").as_string(),
                        info.at("authorizationRevision").as_string()};
        auto client = take(ex::Client::create(api, scope));
        const auto digest = info.at("definitionDigest").as_string();
        check(take(ex::definition_digest(info.at("declaration"))) == digest,
              "Serve/CPP independent declaration digest differs");
        auto platform = ex::Platform::current();
        ex::Workspace workspace{"cpp-business-workspace", "1"};
        ex::Registration registration{"go-executor",
                                      platform,
                                      {workspace},
                                      {"tool.invoke"},
                                      {{"BusinessLookup", digest}},
                                      {}};
        auto connection = take(client->register_executor(registration));
        auto initialized = take(client->initialize(session.id(), platform,
                                                   std::vector<std::string>{"BusinessLookup"},
                                                   take(session.capabilities()).closure_id));
        auto bound =
            take(client->bind(session.id(), connection, workspace, initialized.capability_revision,
                              take(session.capabilities()).closure_id));
        check(bool(bound.binding), "first binding absent");
        auto terminal = take(
            client->negotiate_output({family, session.id()}, *bound.binding, "cpp-output-binding"));
        auto runner_client = client;
        if (mode == "foreign") {
            const auto other = info.at("otherToken").as_string();
            options.token_provider = [other](CancellationToken) -> Result<AuthToken> {
                return AuthToken{other, "synthetic/other-user"};
            };
            runner_client =
                take(ex::Client::create(take(ApiClient::create(options, runtime)), scope));
        }
        auto journal = take(
            ex::FileJournal::open(workspace_path / "journal", []() -> Result<void> { return {}; }));
        std::atomic<int> executions{0};
        std::atomic<bool> observed{false};
        std::optional<ex::Operation> active;
        ex::RunnerOptions run;
        run.client = runner_client;
        run.registration = registration;
        run.journal = journal;
        run.terminal = terminal;
        run.require_output = true;
        run.restricted_status = true;
        run.authorize = [&](const ex::Operation &op, CancellationToken cancel) -> Result<void> {
            if (cancel.is_cancelled())
                return Error{ErrorCode::cancelled, "cancelled"};
            if (op.session_id != session.id() ||
                op.scope.application_scope_id != scope.application_scope_id ||
                op.scope.end_user_id != scope.end_user_id ||
                op.scope.authorization_revision != scope.authorization_revision ||
                op.binding.target.workspace_id != workspace.workspace_id)
                return Error{ErrorCode::permission, "synthetic host denied"};
            return {};
        };
        run.tools = {
            {"BusinessLookup",
             {digest, [&](ex::ToolContext context, Json) -> ex::ToolResult {
                  ++executions;
                  check(bool(context.output) && active.has_value(),
                        "negotiated ordinary business output missing");
                  check(take(context.output->capture("stdout", "cpp-first-chunk")) == 15,
                        "first capture truncated");
                  const auto deadline = unix_time_ms() + 5000;
                  while (unix_time_ms() < deadline && !context.cancellation.is_cancelled()) {
                      auto state =
                          take(api->call("terminal.output.status", output_query(*active, family)))
                              .body;
                      if (state.at("acceptedThrough").is_string()) {
                          check(state.at("state").as_string() == "receiving" &&
                                    state.at("nextByteOffset").as_string() == "15",
                                "first business output not visible before completion");
                          observed = true;
                          break;
                      }
                      std::this_thread::sleep_for(std::chrono::milliseconds(10));
                  }
                  check(observed, "first chunk never reached real Serve");
                  check(take(context.output->capture("stderr", "cpp-second-chunk")) == 16,
                        "second capture truncated");
                  return Json::object(
                      {{"status", "ok"},
                       {"content", Json::array({Json::object(
                                       {{"t", "text"}, {"text", "go-terminal-fact"}})})}});
              }}}};
        auto runner = take(ex::Runner::create(std::move(run), connection));
        {
            Observer observer(session);
            se::WriteOptions send;
            send.request_key = "cpp-executor-message";
            take(session.send("GO-TOOL", send));
            const auto deadline = unix_time_ms() + 15000;
            while (unix_time_ms() < deadline) {
                observer.assert_healthy();
                auto batch = take(client->poll(connection));
                check(batch.operations.size() <= 1, "unexpected fixture batch");
                if (!batch.operations.empty()) {
                    active = batch.operations.front();
                    break;
                }
                check(!observer.completed, "turn ended without dispatch");
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            check(active.has_value(), "no dispatched operation");
            check(active->tool_name == "BusinessLookup", "wrong dispatched tool");
            if (mode == "policy")
                control(Json::object({{"command", "set-policy"},
                                      {"requestId", "cpp-executor-revoke-policy"},
                                      {"allowed", false},
                                      {"authorizationRevision", "2"}}));
            if (mode == "auth")
                control(Json::object({{"command", "set-auth"},
                                      {"requestId", "cpp-executor-revoke-auth"},
                                      {"allowed", false}}));
            if (mode != "normal") {
                check(!runner->execute(*active), "revoked/foreign executor reached business");
                check(executions == 0, "unauthorized handler ran");
                for (const auto &entry :
                     std::filesystem::directory_iterator(workspace_path / "journal"))
                    check(entry.path().extension() != ".claim", "unauthorized operation claimed");
                if (mode == "auth")
                    control(Json::object({{"command", "set-auth"},
                                          {"requestId", "cpp-executor-restore-auth"},
                                          {"allowed", true}}));
            } else {
                auto outcome = take(runner->execute_with_output(*active));
                check(outcome.receipt.status == "completed" && outcome.output_confirmed(),
                      "business/output not complete");
                auto repeated = take(runner->execute_with_output(*active));
                check(executions == 1 && ex::to_json(repeated.receipt).dump() ==
                                             ex::to_json(outcome.receipt).dump(),
                      "duplicate executed again");
                auto state =
                    take(api->call("terminal.output.status", output_query(*active, family))).body;
                check(state.at("state").as_string() == "complete" &&
                          state.at("seal").at("totalBytes").as_string() == "31",
                      "wrong final output seal");
                check(state.at("seal").at("payloadDigest").as_string() ==
                          take(crypto::sha256_hex("cpp-first-chunkcpp-second-chunk")),
                      "seal byte digest differs");
                take(client->submit(*active, outcome.receipt));
                take(client->submit(*active, outcome.receipt));
                const auto until = unix_time_ms() + 8000;
                while (!observer.completed && unix_time_ms() < until) {
                    observer.assert_healthy();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                check(observer.completed, "turn failed to complete after receipt");
                check(take(session.history(0, 20)).dump().find("go-tool-complete") !=
                          std::string::npos,
                      "tool result absent from core history");
            }
        }
        if (mode != "policy")
            take(session.close());
        runner.reset();
        journal.reset();
        api->shutdown();
        check(take(runtime->shutdown(std::chrono::seconds(5))) == ShutdownStatus::stopped,
              "runtime did not stop");
        std::cout << "TANSR_CPP_EXECUTOR_RESULT "
                  << Json::object({{"family", family},
                                   {"mode", mode},
                                   {"firstBinding", true},
                                   {"handlerCalls", std::uint64_t(executions.load())},
                                   {"firstChunkBeforeCompletion", observed.load()},
                                   {"passed", true}})
                         .dump()
                  << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "executor integration: " << error.what() << std::endl;
        return 1;
    }
}
