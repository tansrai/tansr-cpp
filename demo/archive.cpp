#include "common.hpp"
#include <iostream>
#include <tansr/archive.hpp>

namespace {
using namespace tansr;
using demo::private_directory;
using demo::take;
const char *help =
    "tansr-archive --mode MODE [--base ORIGIN] [--family sdk1|sdk2-offload-v1]\n"
    "prepare-create: --session ID --source ID --request-id STABLE_ID --intent "
    "ABSOLUTE_PRIVATE_FILE\n"
    "create / creation-status: --intent ORIGINAL_FILE\n"
    "status: --binding ID\n"
    "sync: --binding ID --file ABSOLUTE_PRIVATE_FILE --key-id ID [--max-pages 64]\n"
    "recover: --binding ID --file ORIGINAL_FILE --key-id ID --request-id STABLE_RECOVERY_ID\n"
    "materials: --binding ID --file ORIGINAL_FILE --key-id ID --request-id STABLE_RESPONSE_ID "
    "--intent NEW_PRIVATE_FILE\n"
    "material-submit / material-status: --intent ORIGINAL_FILE\n"
    "Credential files: TANSR_TOKEN_FILE, TANSR_SCOPE_FILE. FileStore also needs "
    "TANSR_ARCHIVE_KEY_FILE (64 hex digits).\n"
    "Use separate private directories for credentials, key, archive and intent. Original files are "
    "never replaced on failure.\n"
    "Source registration is a Serve-host responsibility. received is not core-consumed.\n";
void save_owner(storage::PrivateDirectory &directory, const std::string &leaf,
                const demo::Host &host) {
    take(directory.write_atomic(leaf + ".owner", demo::intent_owner(host).dump(), false));
}
void check_owner(storage::PrivateDirectory &directory, const std::string &leaf,
                 const demo::Host &host) {
    auto bytes = take(directory.read(leaf + ".owner", 16384));
    if (!bytes ||
        take(Json::parse(*bytes, {16384, 8, 64})).dump() != demo::intent_owner(host).dump())
        demo::fail("intent belongs to another base, family or current scope",
                   ErrorCode::permission);
}
archive::SavedIntent load_intent(const std::filesystem::path &path, const demo::Host &host) {
    auto directory = private_directory(path.parent_path(), host);
    check_owner(*directory, path.filename().u8string(), host);
    return take(archive::SavedIntent::load(*directory, path.filename().u8string()));
}
archive::SavedIntent save_intent(const std::filesystem::path &path, const demo::Host &host,
                                 std::string kind, Json body, std::int64_t deadline) {
    auto directory = private_directory(path.parent_path(), host);
    const auto leaf = path.filename().u8string();
    save_owner(*directory, leaf, host);
    return take(
        archive::SavedIntent::save(*directory, leaf, std::move(kind), std::move(body), deadline));
}
void require_completed(const Json &receipt) {
    if (demo::field(receipt, "state") != "completed")
        demo::fail("original operation is not confirmed completed", ErrorCode::unknown);
}
void materials(const archive::ArchiveClient &client, archive::FileStore &store,
               const demo::Host &host, demo::Stop &stop, const std::string &request_id,
               const std::filesystem::path &intent) {
    const auto binding_id = demo::field(store.identity(), "bindingId");
    auto binding = take(client.binding(binding_id, demo::call_options(stop.token())));
    auto stream = take(client.events(
        binding, {},
        demo::call_options(stop.token(),
                           unix_time_ms() + std::chrono::duration_cast<std::chrono::milliseconds>(
                                                host.config.timeout)
                                                .count())));
    std::cout << "waiting for one current material request; only requested verified records will "
                 "be supplied\n"
              << std::flush;
    while (!stop.token().is_cancelled()) {
        auto event = take(stream.next(stop.token()));
        if (!event)
            demo::fail("material stream ended before a request", ErrorCode::unknown);
        auto envelope = take(Json::parse(event->data));
        const auto &raw = envelope.at("raw");
        if (demo::field(raw, "eventType") != "material.request")
            continue;
        const auto &request = raw.at("payload");
        const auto ttl = request.at("remainingTtlMs").as_u64();
        if (ttl > 86400000)
            demo::fail("material TTL exceeds demo bound", ErrorCode::contract);
        const auto end = unix_time_ms() + static_cast<std::int64_t>(ttl);
        stream.cancel();
        // 首次收到时固定绝对截止并落盘；后续授权、查询和上传不重置 TTL。
        auto received_path = intent;
        received_path += std::filesystem::path(".request");
        auto received = save_intent(received_path, host, "material-request", request, end);
        binding = take(client.binding(binding_id, demo::call_options(stop.token(), end)));
        const auto &epoch = binding.at("operationEpoch");
        if (epoch.is_null())
            demo::fail("binding has no operation epoch", ErrorCode::contract);
        auto identity =
            Json::object({{"requestId", request_id}, {"operationEpoch", epoch.at("id")}});
        auto body = take(client.prepare_materials_before(store, received.body(), identity,
                                                         received.deadline_ms(),
                                                         demo::call_options(stop.token(), end)));
        auto saved = save_intent(intent, host, "material-response", std::move(body), end);
        auto receipt = take(client.submit_materials(saved, demo::call_options(stop.token(), end)));
        std::cout << "material state: " << demo::safe(demo::field(receipt, "state"))
                  << "; response saved, use material-status for core consumption\n";
        return;
    }
    demo::fail("material observation cancelled", ErrorCode::cancelled);
}
void run(demo::Host &host, demo::Stop &stop, demo::Args &args, const std::string &mode) {
    archive::ArchiveClient client(host.api);
    auto context = demo::call_options(
        stop.token(),
        unix_time_ms() +
            std::chrono::duration_cast<std::chrono::milliseconds>(host.config.timeout).count());
    if (mode == "prepare-create") {
        auto session = args.required("--session"), source = args.required("--source"),
             request = args.required("--request-id");
        auto path = demo::absolute_path(args.required("--intent"));
        args.finish();
        auto body = take(client.prepare_create(session, source, request, context));
        save_intent(path, host, "binding-create", std::move(body), *context.deadline_ms);
        std::cout << "binding intent durably saved; no binding was created; use create with the "
                     "original intent before its deadline\n";
        return;
    }
    if (mode == "create" || mode == "creation-status" || mode == "material-submit" ||
        mode == "material-status") {
        auto path = demo::absolute_path(args.required("--intent"));
        args.finish();
        auto intent = load_intent(path, host);
        if (mode == "create") {
            auto binding = take(client.create_binding(intent, context));
            std::cout << "binding: " << demo::safe(demo::field(binding, "bindingId")) << '\n';
            return;
        }
        if (mode == "creation-status") {
            if (intent.kind() != "binding-create")
                demo::fail("expected binding-create intent");
            auto receipt =
                take(client.creation_operation(demo::field(intent.body().at("target"), "sessionId"),
                                               intent.body().at("request"), context));
            std::cout << "creation state: " << demo::safe(demo::field(receipt, "state"))
                      << "; binding: " << demo::safe(demo::field(receipt, "bindingId")) << '\n';
            require_completed(receipt);
            return;
        }
        if (intent.kind() != "material-response")
            demo::fail("expected material-response intent");
        auto receipt = mode == "material-submit"
                           ? take(client.submit_materials(intent, context))
                           : take(client.material_status(
                                 demo::field(intent.body(), "bindingId"),
                                 demo::field(intent.body(), "materialRequestId"), context));
        std::cout << "material state: " << demo::safe(demo::field(receipt, "state"))
                  << " (received is not core-consumed)\n";
        if (mode == "material-status" && demo::field(receipt, "state") != "core-consumed")
            demo::fail("material consumption is not confirmed", ErrorCode::unknown);
        return;
    }
    if (mode == "status") {
        auto id = args.required("--binding");
        args.finish();
        auto status = take(client.status(id, context));
        std::cout << "binding state: " << demo::safe(demo::field(status, "state"))
                  << "; published=" << demo::safe(status.at("publishedThroughSequence").dump())
                  << "; coverage=" << demo::safe(status.at("acknowledgedCoverage").dump()) << '\n';
        return;
    }
    if (mode != "sync" && mode != "recover" && mode != "materials")
        demo::fail("unknown archive mode; see --help");
    auto id = args.required("--binding"), key_id = args.required("--key-id");
    auto path = demo::absolute_path(args.required("--file"));
    auto request = mode == "sync" ? std::string{} : args.required("--request-id");
    std::optional<std::filesystem::path> intent;
    if (mode == "materials")
        intent = demo::absolute_path(args.required("--intent"));
    auto pages_text = args.value("--max-pages", "64");
    std::size_t consumed = 0, pages = 0;
    try {
        pages = std::stoul(pages_text, &consumed);
    } catch (...) {
        demo::fail("max-pages must be 1..1024");
    }
    if (consumed != pages_text.size() || pages < 1 || pages > 1024)
        demo::fail("max-pages must be 1..1024");
    args.finish();
    if (mode != "sync" && !std::filesystem::is_regular_file(path))
        demo::fail("recover/materials require original archive file and key");
    auto binding = take(client.binding(id, context)), status = take(client.status(id, context));
    auto identity = take(archive::identity_from_binding(binding, status));
    const auto &scope = host.config.credentials->scope();
    if (demo::field(identity, "applicationScopeId") != scope.application_scope_id ||
        demo::field(identity, "endUserId") != scope.end_user_id)
        demo::fail("archive identity differs from authenticated scope", ErrorCode::permission);
    take(storage::create_private_directory(path.parent_path()));
    archive::StoreOptions options;
    options.path = path;
    options.key = demo::archive_key();
    options.key_id = key_id;
    options.identity = identity;
    options.check_access = [credentials = host.config.credentials, expected = identity.dump(),
                            cancel = stop.token()](const Json &actual) -> Result<void> {
        auto access = credentials->check(cancel);
        if (!access)
            return access;
        if (actual.dump() != expected)
            return Error{ErrorCode::permission, "archive scope changed"};
        return {};
    };
    auto store = take(archive::FileStore::open(std::move(options)));
    if (mode == "recover") {
        auto result = take(archive::recover_pending(client, *store, request, context));
        if (!result.recovered || !result.receipt)
            demo::fail("pending ACK is not confirmed", ErrorCode::unknown);
        require_completed(*result.receipt);
        std::cout << "pending ACK confirmed; recovery did not synchronize all remaining pages\n";
        return;
    }
    if (mode == "materials") {
        materials(client, *store, host, stop, request, *intent);
        return;
    }
    for (std::size_t i = 0; i < pages; ++i) {
        auto result = take(archive::sync_once(client, *store, demo::request_id(), context));
        std::cout << "page " << i + 1 << ": verified records=" << result.records
                  << " complete=" << (result.complete ? "true" : "false")
                  << " recovered=" << (result.recovered ? "true" : "false") << '\n';
        if (result.complete) {
            std::cout << "archive synchronized; coverage is separate from event cursor and "
                         "material consumption\n";
            return;
        }
    }
    demo::fail("page bound reached; continue with original binding/file/key", ErrorCode::capacity);
}
} // namespace
int main(int argc, char **argv) {
    return demo::report("tansr-archive", [&] {
        demo::Args args(argc, argv);
        if (args.flag("--help")) {
            std::cout << help;
            return;
        }
        auto config = demo::Configuration::read(args);
        auto mode = args.value("--mode", "sync");
        demo::Host host(config);
        demo::Stop stop(config.timeout);
        run(host, stop, args, mode);
        host.close();
    });
}
