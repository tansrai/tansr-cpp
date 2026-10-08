#include "common.hpp"
#include <algorithm>
#include <iostream>

namespace {
using namespace tansr;
using demo::take;
constexpr const char *tool_name = "DemoOrderStatus";
const char *help =
    "tansr-tools --journal ABSOLUTE_PRIVATE_DIRECTORY [--session ID] [--executor ID]\n"
    "  [--base ORIGIN] [--family sdk1|sdk2-offload-v1] [--request-id STABLE_ID]\n"
    "  [--require-output] [--run-once] [--timeout SECONDS]\n"
    "Requires TANSR_TOKEN_FILE and TANSR_SCOPE_FILE in one private directory.\n"
    "New offload sessions require --request-id. Explicit synthetic DemoOrderStatus only.\n"
    "--require-output negotiates stdout/stderr and a separate output seal. No Shell is installed.\n"
    "--run-once handles one queued operation; unknown results retain the original journal.\n";
Json declaration() {
    return Json::object(
        {{"name", tool_name},
         {"description", "Read sample order DEMO-001; demonstration data only."},
         {"parameters", Json::object({{"orderId", Json::object({{"type", "string"},
                                                                {"description",
                                                                 "Sample order ID: DEMO-001"}})}})},
         {"readOnly", true}});
}
executor::ToolResult lookup(executor::ToolContext context, Json arguments) {
    if (!arguments.is_object() || arguments.as_object().size() != 1 ||
        !arguments.contains("orderId") || !arguments.at("orderId").is_string())
        return executor::ToolFailure::rejected("invalid_order_arguments");
    if (context.cancellation.is_cancelled())
        return executor::ToolFailure::rejected("cancelled_before_lookup");
    if (context.output) {
        auto captured = context.output->capture("stdout", "order lookup started\n");
        if (!captured)
            return executor::ToolFailure::unknown("output_capture_unknown");
        if (context.cancellation.wait_for(std::chrono::milliseconds(750)))
            return executor::ToolFailure::unknown("cancelled_after_output");
        captured = context.output->capture("stderr", "order lookup completed\n");
        if (!captured)
            return executor::ToolFailure::unknown("output_capture_unknown");
    }
    if (arguments.at("orderId").as_string() != "DEMO-001")
        return Json::object(
            {{"status", "error"}, {"message", "Sample order not found / 演示订单不存在"}});
    return Json::object(
        {{"status", "ok"},
         {"content",
          Json::array({Json::object(
              {{"t", "text"},
               {"text", "DEMO-001: awaiting shipment (sample data) / 待发货（演示数据）"}})})}});
}
bool same_binding(const executor::Binding &a, const executor::Binding &b) {
    return a.binding_id == b.binding_id && a.revision == b.revision &&
           a.target.executor_id == b.target.executor_id &&
           a.target.connection_id == b.target.connection_id &&
           a.target.connection_revision == b.target.connection_revision &&
           a.target.workspace_id == b.target.workspace_id &&
           a.target.workspace_revision == b.target.workspace_revision;
}
void run(demo::Host &host, demo::Stop &stop, const std::filesystem::path &journal_path,
         std::optional<std::string> existing, std::optional<std::string> request,
         const std::string &executor_id, bool require_output, bool once) {
    auto sessions = take(session::SessionClient::create(host.api, stop.token()));
    session::CreateOptions create;
    create.request_id = request;
    create.client_tools = std::vector<Json>{declaration()};
    create.write = demo::write_options(stop.token());
    auto current = existing ? take(sessions.attach(*existing)) : take(sessions.create(create));
    std::cout << "session: " << demo::safe(current.id()) << '\n';
    auto client = take(executor::Client::create(host.api, host.config.credentials->scope()));
    const auto digest = take(executor::definition_digest(declaration()));
    const executor::Workspace workspace{"cpp-business", "1"};
    executor::Registration registration;
    registration.executor_id = executor_id;
    registration.workspaces = {workspace};
    registration.operations = {"tool.invoke"};
    registration.tools = {{tool_name, digest}};
    take(storage::create_private_directory(journal_path));
    auto credentials = host.config.credentials;
    auto journal = take(
        executor::FileJournal::open(journal_path, [credentials] { return credentials->check(); }));
    auto connection = take(client->register_executor(registration, stop.token()));
    auto closure = take(current.capabilities());
    const auto initialized = take(client->initialize(current.id(), registration.platform,
                                                     std::vector<std::string>{tool_name},
                                                     closure.closure_id, stop.token()));
    closure = take(current.capabilities());
    const auto capabilities =
        take(client->bind(current.id(), connection, workspace, initialized.capability_revision,
                          closure.closure_id, stop.token()));
    if (!capabilities.binding ||
        !std::any_of(capabilities.effective_tools.begin(), capabilities.effective_tools.end(),
                     [](const executor::EffectiveTool &value) {
                         return value.name == tool_name && value.available;
                     }))
        demo::fail("Serve did not enable DemoOrderStatus for the explicit binding");
    const auto binding = *capabilities.binding;
    executor::RunnerOptions options;
    options.client = client;
    options.registration = registration;
    options.journal = journal;
    options.tools.emplace(tool_name, executor::Tool{digest, lookup});
    options.poll_interval = std::chrono::milliseconds(250);
    options.require_output = require_output;
    options.authorize = [credentials, binding,
                         session_id = current.id()](const executor::Operation &operation,
                                                    CancellationToken cancel) -> Result<void> {
        auto access = credentials->check(cancel);
        if (!access)
            return access;
        const auto &scope = credentials->scope();
        if (operation.scope.application_scope_id != scope.application_scope_id ||
            operation.scope.end_user_id != scope.end_user_id ||
            operation.scope.authorization_revision != scope.authorization_revision ||
            operation.session_id != session_id || operation.tool_name != tool_name ||
            operation.request.operation != "tool.invoke" ||
            !same_binding(operation.binding, binding))
            return Error{ErrorCode::permission, "demo operation is outside current scope"};
        return {};
    };
    if (require_output) {
        const auto id = demo::request_id();
        std::cout << "output request: " << id << '\n';
        options.terminal = take(client->negotiate_output({host.config.family, current.id()},
                                                         binding, id, stop.token()));
    }
    std::cout << "ready: use tansr-chat --family " << host.config.family << " --attach "
              << demo::safe(current.id())
              << " in another terminal\nAsk: 查询订单 DEMO-001。Ctrl+C stops this executor, not "
                 "the Serve turn.\n"
              << std::flush;
    if (!once) {
        auto runner = take(executor::Runner::create(std::move(options), connection));
        take(runner->run(stop.token()));
        return;
    }
    auto next_heartbeat =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(connection.heartbeat_after_ms);
    while (!stop.token().is_cancelled()) {
        if (std::chrono::steady_clock::now() >= next_heartbeat) {
            connection = take(client->heartbeat(connection, stop.token()));
            next_heartbeat = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(connection.heartbeat_after_ms);
        }
        auto batch = take(client->poll(connection, stop.token()));
        if (!batch.operations.empty()) {
            auto runner = take(executor::Runner::create(std::move(options), connection));
            const auto &operation = batch.operations.front();
            auto outcome = take(runner->execute_with_output(operation, stop.token()));
            auto submitted = take(client->submit(operation, outcome.receipt, stop.token()));
            std::cout << "business receipt state: " << demo::safe(submitted.status)
                      << "; output confirmed=" << (outcome.output_confirmed() ? "true" : "false")
                      << '\n';
            if (!outcome.output_confirmed())
                demo::fail(
                    "business receipt retained; output seal is not confirmed; do not rerun handler",
                    ErrorCode::unknown);
            if (outcome.receipt.status == "unknown")
                demo::fail("business result is unknown", ErrorCode::unknown);
            return;
        }
        stop.token().wait_for(std::chrono::milliseconds(100));
    }
    demo::fail("executor observation cancelled", ErrorCode::cancelled);
}
} // namespace
int main(int argc, char **argv) {
    return demo::report("tansr-tools", [&] {
        demo::Args args(argc, argv);
        if (args.flag("--help")) {
            std::cout << help;
            return;
        }
        auto config = demo::Configuration::read(args);
        auto journal = demo::absolute_path(args.required("--journal"));
        auto existing = args.take("--session"), request = args.take("--request-id");
        auto executor = args.value("--executor", "cpp-demo");
        bool output = args.flag("--require-output"), once = args.flag("--run-once");
        if (config.family == "sdk2-offload-v1" && !existing && !request)
            demo::fail("new offload session requires a caller-preserved --request-id");
        args.finish();
        demo::Host host(config);
        demo::Stop stop(config.timeout);
        run(host, stop, journal, existing, request, executor, output, once);
        host.close();
    });
}
