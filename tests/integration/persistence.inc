// 同一真实 Serve 驱动的新 profile 分支；不另起宿主或改原 publication 行为。
tp::Options persistence_options(Context &context, bool create) {
    const auto original = context.options(create);
    tp::Options options;
    options.path = original.path;
    options.create = original.create;
    options.identity = original.identity;
    options.key_id = original.key_id;
    options.read_key = original.read_key;
    options.read_context = original.read_context;
    return options;
}
Json persistence_request(Context &context, const char *action) {
    const auto &identity = context.info.at("publicationIdentity");
    return Json::object({{"contract", "terminal-persistence-v1"},
                         {"action", action},
                         {"sourceId", identity.at("sourceId")},
                         {"sourceGeneration", identity.at("sourceGeneration")},
                         {"domainKey", identity.at("domainKey")}});
}
class PersistenceTrace final : public tp::Store {
  public:
    std::shared_ptr<tp::FileStore> inner;
    std::size_t calls = 0;
    explicit PersistenceTrace(std::shared_ptr<tp::FileStore> value) : inner(std::move(value)) {}
    const Json &identity() const noexcept override { return inner->identity(); }
    bool atomic_durable_publication() const noexcept override { return true; }
    bool encrypted_at_rest() const noexcept override { return true; }
    Result<Json> execute(const Json &request, const Json &owner, tp::Guard guard) override {
        ++calls;
        auto result = inner->execute(request, owner, std::move(guard));
        if (result) {
            auto detail = Json::object({{"action", request.at("action")}});
            if (request.find("key")) {
                detail.set("keyKind", request.at("key").at("kind"));
                detail.set("hit", !result.value().at("entry").is_null());
            }
            event("persistence-storage", detail);
        }
        return result;
    }
};
void persistence_worker(Context &context, bool create) {
    const auto state = context.state();
    const auto session = state.at("sessionId").as_string();
    auto connection = connection_from(state.at("connection"));
    const auto binding = binding_from(state.at("binding"));
    auto store = take(tp::FileStore::open(persistence_options(context, create)));
    if (!create) {
        const auto owner = Json::object({{"scope", context.info.at("scope")},
                                         {"sessionId", session},
                                         {"binding", state.at("binding")}});
        event(
            "cold-before-serve",
            Json::object(
                {{"root",
                  take(store->execute(persistence_request(context, "head"), owner)).at("root")}}));
    }
    auto counted = std::make_shared<PersistenceTrace>(store);
    ex::Authorizer authorize = [&](const ex::Operation &op,
                                   CancellationToken cancel) -> Result<void> {
        if (op.scope.application_scope_id != context.scope.application_scope_id ||
            op.scope.end_user_id != context.scope.end_user_id ||
            op.scope.authorization_revision != context.scope.authorization_revision ||
            op.session_id != session || op.tool_name != "MemoryPublication" ||
            !demo::memory::same_binding(op.binding, binding))
            return Error{ErrorCode::permission, "persistence current identity mismatch"};
        auto capabilities = context.execution->execution_capabilities(session, cancel);
        if (!capabilities)
            return capabilities.error();
        if (!capabilities.value().binding ||
            !demo::memory::same_binding(*capabilities.value().binding, binding))
            return Error{ErrorCode::permission, "persistence current binding changed"};
        return {};
    };
    auto host = take(tp::create_host({counted, store, authorize, true}));
    ex::RunnerOptions options;
    options.client = context.execution;
    options.registration = context.registration();
    options.journal = host.journal;
    options.tools = host.tools;
    options.authorize = authorize;
    auto runner = take(ex::Runner::create(options, connection));
    event("worker-ready");
    const auto until = unix_time_ms() + 360000;
    auto heartbeat = unix_time_ms();
    while (unix_time_ms() < until && !std::filesystem::exists(context.root / "stop")) {
        if (unix_time_ms() >= heartbeat) {
            connection = take(context.execution->heartbeat(connection));
            runner = take(ex::Runner::create(options, connection));
            heartbeat = unix_time_ms() + static_cast<std::int64_t>(std::min<std::uint64_t>(
                                             connection.heartbeat_after_ms, 1000));
        }
        for (const auto &op : take(context.execution->poll(connection)).operations) {
            const auto request = request_of(op);
            const auto receipt = take(runner->execute(op));
            check(receipt.status == "completed" && bool(receipt.result),
                  "real persistence operation did not complete");
            const auto tool = take(Json::parse(receipt.result->args.at("resultJson").as_string()));
            check(tool.at("status").as_string() == "ok", "persistence tool failed");
            const auto response =
                take(Json::parse(tool.at("content").as_array().at(0).at("text").as_string()));
            take(validate_wire("terminal-persistence-v1", "Response", response));
            const bool lose = create && context.transport->losses == 0 &&
                              request.at("action").as_string() == "commit";
            context.transport->drop = lose;
            auto submitted = context.execution->submit(op, receipt);
            if (lose) {
                check(!submitted && submitted.error().code == ErrorCode::network &&
                          context.transport->losses == 1,
                      "accepted commit loss missing");
                const auto status = take(context.execution->status(session, op.operation_id));
                check(status.receipt &&
                          demo::memory::equal(ex::to_json(status.operation), ex::to_json(op)) &&
                          demo::memory::equal(ex::to_json(*status.receipt), ex::to_json(receipt)),
                      "original HTTP execution query changed accepted receipt");
                const auto calls = counted->calls;
                const auto replay = take(runner->execute(op));
                check(counted->calls == calls &&
                          demo::memory::equal(ex::to_json(replay), ex::to_json(receipt)),
                      "original execution replay re-entered Store");
                save(context.root / "handoff" / "persistence-checkpoint.json",
                     Json::object({{"operationId", op.operation_id},
                                   {"sessionId", session},
                                   {"operation", ex::to_json(op)},
                                   {"receipt", ex::to_json(receipt)},
                                   {"transfer", response.at("transfer")}}));
                submitted = context.execution->submit(op, replay);
                event("original-key-reconciled", Json::object({{"operationId", op.operation_id},
                                                               {"digest", op.digest},
                                                               {"status", receipt.status},
                                                               {"storeCalls", Json(0)}}));
            }
            check(submitted && submitted.value().receipt &&
                      demo::memory::equal(ex::to_json(*submitted.value().receipt),
                                          ex::to_json(receipt)),
                  "Serve persistence receipt mismatch");
            event("persistence-receipt", Json::object({{"action", request.at("action")},
                                                       {"operationId", op.operation_id},
                                                       {"digest", op.digest}}));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    check(std::filesystem::exists(context.root / "stop"), "persistence worker deadline");
    if (create)
        check(context.transport->losses == 1, "real accepted commit loss was not exercised");
    runner.reset();
    host.tools.clear();
    host.journal.reset();
    counted.reset();
    take(store->close());
    event("storage-closed");
}
void persistence_keys(Context &context) {
    const auto session = context.state().at("sessionId").as_string();
    auto pin = read(context.root / "handoff" / "pin.json");
    auto expected = pin.at("receipt");
    expected.set("consumed", true);
    auto command = pin.at("command");
    command.set("session",
                Json::object({{"sessionContract", context.family}, {"sessionId", session}}));
    CallOptions lookup;
    lookup.parameters = {{"id", session}, {"targetId", command.at("operationId").as_string()}};
    lookup.query = {{"contract", "terminal-services-v1"},
                    {"sessionContract", context.family},
                    {"requestId", command.at("requestId").as_string()}};
    auto queried = take(context.api->call("terminal.memory.receipt", lookup));
    check(demo::memory::equal(queried.body.at("receipt"), expected), "archived receipt changed");
    CallOptions options;
    options.parameters = {{"id", session}};
    options.body = command;
    auto replay = take(context.api->call("terminal.memory.command", options));
    check(demo::memory::equal(replay.body.at("receipt"), expected), "primary key replay changed");
    command.set("operationId", "cpp-different-operation");
    options.body = command;
    const auto conflict = context.api->call("terminal.memory.command", options);
    check(!conflict && conflict.error().http_status == 409 &&
              take(Json::parse(conflict.error().detail)).at("domainCode").as_string() ==
                  "request_conflict",
          "secondary key failed to preserve original request");
    event("permanent-dual-keys",
          Json::object({{"receipt", expected}, {"secondaryHttpStatus", Json(409)}}));
}
void persistence_inspect(Context &context) {
    event("cold-open-start");
    auto store = take(tp::FileStore::open(persistence_options(context, false)));
    event("cold-open-complete");
    const auto state = context.state();
    const auto session = state.at("sessionId").as_string();
    const auto owner = Json::object({{"scope", context.info.at("scope")},
                                     {"sessionId", session},
                                     {"binding", state.at("binding")}});
    const auto head = take(store->execute(persistence_request(context, "head"), owner));
    event("cold-head-complete");
    check(!head.at("root").is_null() && head.at("root").at("index").at("count").as_string() == "1",
          "cold root/index missing");
    auto body_request = persistence_request(context, "read");
    body_request.set("part", "body");
    body_request.set("commitRoot", head.at("root").at("commitRoot"));
    body_request.set("offset", Json(0));
    body_request.set("length", head.at("root").at("body").at("byteLength"));
    const auto body_response = take(store->execute(body_request, owner));
    const auto decoded = take(crypto::base64_decode(body_response.at("base64").as_string()));
    const std::string original_body(decoded.begin(), decoded.end());
    check(take(crypto::sha256_hex(original_body)) ==
                  head.at("root").at("body").at("sha256").as_string() &&
              original_body.find("cpp-persistence") != std::string::npos,
          "cold body lost original real pin or does not match durable root");
    const auto checkpoint_path = context.root / "handoff" / "persistence-checkpoint.json";
    if (std::filesystem::exists(checkpoint_path)) {
        const auto checkpoint = read(checkpoint_path);
        const auto wire = checkpoint.at("operation");
        take(validate_wire(ex::protocol, "ExecutionOperation", wire));
        event("cold-operation-schema-complete");
        const auto &scope = wire.at("scope");
        ex::Operation operation{
            wire.at("operationId").as_string(),
            wire.at("sessionId").as_string(),
            {scope.at("applicationScopeId").as_string(), scope.at("endUserId").as_string(),
             scope.at("authorizationRevision").as_string()},
            binding_from(wire.at("binding")),
            wire.at("toolName").as_string(),
            {wire.at("request").at("operation").as_string(), wire.at("request").at("args")},
            wire.at("digest").as_string(),
            wire.at("expiresAt").as_string()};
        take(ex::validate_operation(operation));
        check(demo::memory::equal(ex::to_json(operation), wire), "cold original operation changed");
        const auto claim = take(store->claim(operation));
        check(claim.state == ex::ClaimState::receipt && claim.receipt &&
                  demo::memory::equal(ex::to_json(*claim.receipt), checkpoint.at("receipt")),
              "cold journal changed permanent execution receipt");
        auto query = request_of(operation);
        query.set("action", "query");
        const auto old_owner = Json::object({{"scope", wire.at("scope")},
                                             {"sessionId", wire.at("sessionId")},
                                             {"binding", wire.at("binding")}});
        const auto result = take(store->execute(query, old_owner));
        check(demo::memory::equal(result.at("transfer"), checkpoint.at("transfer")),
              "cold original transfer result changed");
        event(
            "cold-original-journal-and-transfer",
            Json::object({{"operationId", operation.operation_id}, {"digest", operation.digest}}));
    }
    take(store->close());
    const auto path = persistence_options(context, false).path;
    const auto body = bytes(path);
    check(body.find("synthetic") == std::string::npos &&
              body.find("cpp-persistence") == std::string::npos,
          "plaintext fixture material in encrypted media");
    check(std::filesystem::exists(path.u8string() + ".writes"), "durable key budget missing");
    event("persistence-inspection",
          Json::object(
              {{"root", head.at("root")}, {"ciphertextSha256", take(crypto::sha256_hex(body))}}));
}
