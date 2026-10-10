#include "common.hpp"
#include "memory_binding.hpp"
#include "tansr/memory_publication.hpp"
#include "tansr/terminal_persistence.hpp"
#include <algorithm>
#include <iostream>

namespace {
using namespace tansr;
namespace mp = tansr::memory_publication;
namespace tp = tansr::terminal_persistence;
using demo::take;
const char *help =
    "tansr-memory --session ID --binding-request ID --file ABSOLUTE_PRIVATE_FILE --key-id ID\n"
    "  --source ID --generation SEQUENCE --domain ID [--create] [--executor ID]\n"
    "  [--base ORIGIN] [--family sdk1|sdk2-offload-v1] [--timeout SECONDS]\n"
    "  [--profile publication|persistence-v1]\n"
    "Needs TANSR_TOKEN_FILE, TANSR_SCOPE_FILE, TANSR_ARCHIVE_KEY_FILE.\n"
    "Use a dedicated publication key/private directory, separate from Archive.\n"
    "Reopen original file by default. --create never replaces an existing file.\n"
    "Serve must authorize this Source and MemoryPublication. No model tool or Shell "
    "is installed.\n"
    "Offline migration: omit --session, add --migrate-to NEW_PRIVATE_FILE\n"
    "  --new-key-id NEW_ID, set TANSR_MEMORY_NEXT_KEY_FILE to a fresh key.\n"
    "Stop the original Runner first. Preserve both files/keys; no automatic switch.\n";
} // namespace
int main(int argc, char **argv) {
    return demo::report("tansr-memory", [&] {
        demo::Args args(argc, argv);
        if (args.flag("--help")) {
            std::cout << help;
            return;
        }
        auto config = demo::Configuration::read(args);
        const auto migration_path = args.take("--migrate-to");
        const auto next_key_id = migration_path ? args.required("--new-key-id") : std::string();
        const auto session_id = args.value("--session", "");
        if (!migration_path && session_id.empty())
            demo::fail("missing required option --session");
        const auto binding_request =
            migration_path ? std::string() : args.required("--binding-request");
        const auto path = demo::absolute_path(args.required("--file"));
        const auto key_id = args.required("--key-id");
        const auto source = args.required("--source"), generation = args.required("--generation");
        const auto domain = args.required("--domain"),
                   executor_id = args.value("--executor", "cpp-memory");
        const auto create = args.flag("--create");
        const auto profile = args.value("--profile", "publication");
        if (profile != "publication" && profile != "persistence-v1")
            demo::fail("profile must be publication or persistence-v1");
        const bool v1 = profile == "persistence-v1";
        args.finish();
        if (migration_path && create)
            demo::fail("migration must reopen the original file; omit --create");
        auto credentials = config.credentials;
        const auto scope = credentials->scope();
        mp::Options storage_options;
        storage_options.path = path;
        storage_options.create = create;
        storage_options.key_id = key_id;
        storage_options.identity = Json::object(
            {{"scope", Json::object({{"applicationScopeId", scope.application_scope_id},
                                     {"endUserId", scope.end_user_id}})},
             {"sourceId", source},
             {"sourceGeneration", generation},
             {"domainKey", domain}});
        storage_options.read_context = [credentials]() -> Result<executor::Scope> {
            auto access = credentials->check();
            if (!access)
                return access.error();
            return credentials->scope();
        };
        storage_options.read_key = []() -> Result<crypto::Aes256Key> {
            try {
                return demo::archive_key();
            } catch (const demo::Failure &e) {
                return e.error;
            }
        };
        const auto persistence_options = [](const mp::Options &original) {
            tp::Options result;
            result.path = original.path;
            result.create = original.create;
            result.identity = original.identity;
            result.key_id = original.key_id;
            result.read_key = original.read_key;
            result.read_context = original.read_context;
            result.authorize_recovery = original.authorize_recovery;
            return result;
        };
        if (migration_path) {
            auto destination = storage_options;
            destination.path = demo::absolute_path(*migration_path);
            destination.create = true;
            destination.key_id = next_key_id;
            destination.read_key = []() -> Result<crypto::Aes256Key> {
                try {
                    return demo::archive_key("TANSR_MEMORY_NEXT_KEY_FILE");
                } catch (const demo::Failure &e) {
                    return e.error;
                }
            };
            take(storage::create_private_directory(destination.path.parent_path()));
            const auto receipt =
                v1 ? take(tp::FileStore::migrate(persistence_options(storage_options),
                                                 persistence_options(destination)))
                   : take(mp::FileStore::migrate(storage_options, destination));
            std::cout << Json::object(
                             {{"format", receipt.format},
                              {"identity", receipt.identity},
                              {"sourceSha256", receipt.source_sha256},
                              {"destinationSha256", receipt.destination_sha256},
                              {"transfers", Json(static_cast<std::uint64_t>(receipt.transfers))},
                              {"journalEntries",
                               Json(static_cast<std::uint64_t>(receipt.journal_entries))}})
                             .dump()
                      << (v1 ? "\nRead-only copy complete; retain the original file/key. "
                               "The copy permits verification only; execution takeover and "
                               "activation are unavailable.\n"
                             : "\nMigration complete; retain the original file/key offline. "
                               "Explicitly select one target before restarting the host.\n");
            return;
        }
        demo::Host host(config);
        demo::Stop stop(config.timeout);
        if (create)
            take(storage::create_private_directory(path.parent_path()));
        std::shared_ptr<mp::FileStore> store;
        std::shared_ptr<tp::FileStore> persistence;
        if (v1)
            persistence = take(tp::FileStore::open(persistence_options(storage_options)));
        else
            store = take(mp::FileStore::open(storage_options));
        auto sessions = take(session::SessionClient::create(host.api, stop.token()));
        auto current = take(sessions.attach(session_id));
        auto client = take(executor::Client::create(host.api, scope));
        executor::Registration registration;
        registration.executor_id = executor_id;
        registration.workspaces = {{"cpp-memory", "1"}};
        registration.operations = {"tool.invoke"};
        registration.tools = {
            {v1 ? tp::tool_name : mp::tool_name, v1 ? tp::tool_digest : mp::tool_digest}};
        const auto connection = take(client->register_executor(registration, stop.token()));
        auto closure = take(current.capabilities());
        const auto initialized = take(client->initialize(
            session_id, registration.platform, std::nullopt, closure.closure_id, stop.token()));
        closure = take(current.capabilities());
        const auto bound =
            take(client->bind(session_id, connection, registration.workspaces.front(),
                              initialized.capability_revision, closure.closure_id, stop.token()));
        if (!bound.binding)
            demo::fail("Serve did not return the execution binding");
        const auto binding = *bound.binding;
        take(demo::memory::negotiate(host.api, scope, {config.family, session_id}, binding,
                                     binding_request, stop.token()));
        executor::Authorizer authorize = [credentials, client, binding,
                                          session_id](const executor::Operation &op,
                                                      CancellationToken cancel) -> Result<void> {
            auto access = credentials->check(cancel);
            if (!access)
                return access;
            const auto &context = credentials->scope();
            if (op.scope.application_scope_id != context.application_scope_id ||
                op.scope.end_user_id != context.end_user_id ||
                op.scope.authorization_revision != context.authorization_revision ||
                op.session_id != session_id || op.tool_name != "MemoryPublication" ||
                !demo::memory::same_binding(op.binding, binding))
                return Error{ErrorCode::permission, "publication current identity mismatch"};
            auto latest = client->execution_capabilities(session_id, cancel);
            if (!latest)
                return latest.error();
            if (!latest.value().binding ||
                !demo::memory::same_binding(*latest.value().binding, binding))
                return Error{ErrorCode::permission, "publication binding or permission changed"};
            return {};
        };
        auto publication = v1 ? take(tp::create_host({persistence, persistence, authorize, true}))
                              : take(mp::create_host({store, store, authorize, true}));
        executor::RunnerOptions options;
        options.client = client;
        options.registration = registration;
        options.tools = publication.tools;
        options.journal = publication.journal;
        options.authorize = authorize;
        auto runner = take(executor::Runner::create(std::move(options), connection));
        std::cout << "ready: encrypted local publication and execution journal; source="
                  << demo::safe(source) << "; profile=" << profile
                  << "; domain=" << demo::safe(domain)
                  << "\nCtrl+C stops this host; retain the original file and key for recovery.\n"
                  << std::flush;
        auto result = runner->run(stop.token());
        runner.reset();
        if (persistence)
            take(persistence->close());
        if (store)
            take(store->close());
        host.close();
        if (!result && result.error().code != ErrorCode::cancelled)
            take(std::move(result));
    });
}