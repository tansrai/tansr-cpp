#pragma once
#include "tansr/canonical.hpp"
#include "tansr/executor.hpp"
#include <algorithm>

// 示例宿主只拼装原 terminal-services-v1 请求，不新增 SDK wire 或恢复授权。
namespace demo::memory {
inline tansr::Json binding_json(const tansr::executor::Binding &binding) {
    const auto &t = binding.target;
    auto target = tansr::Json::object({{"executorId", t.executor_id},
                                       {"connectionId", t.connection_id},
                                       {"connectionRevision", t.connection_revision},
                                       {"workspaceId", t.workspace_id},
                                       {"workspaceRevision", t.workspace_revision}});
    if (t.interpreter)
        target.set("interpreter", tansr::Json::object({{"id", t.interpreter->id},
                                                       {"revision", t.interpreter->revision},
                                                       {"hostShell", t.interpreter->host_shell}}));
    return tansr::Json::object({{"bindingId", binding.binding_id},
                                {"revision", binding.revision},
                                {"target", std::move(target)}});
}
inline bool equal(const tansr::Json &a, const tansr::Json &b) {
    auto left = tansr::canonical::encode(a), right = tansr::canonical::encode(b);
    return left && right && left.value() == right.value();
}
inline bool same_binding(const tansr::executor::Binding &a, const tansr::executor::Binding &b) {
    return equal(binding_json(a), binding_json(b));
}
inline tansr::Result<void> negotiate(const std::shared_ptr<tansr::ApiClient> &api,
                                     const tansr::executor::Scope &scope,
                                     const tansr::executor::TerminalSessionReference &session,
                                     const tansr::executor::Binding &binding,
                                     const std::string &request_id,
                                     tansr::CancellationToken cancel = {}) {
    using namespace tansr;
    const auto reference = Json::object(
        {{"sessionContract", session.session_contract}, {"sessionId", session.session_id}});
    const auto target = binding_json(binding);
    const auto expected_scope =
        Json::object({{"applicationScopeId", scope.application_scope_id},
                      {"endUserId", scope.end_user_id},
                      {"authorizationRevision", scope.authorization_revision}});
    const auto body = Json::object({{"contract", "terminal-services-v1"},
                                    {"requestId", request_id},
                                    {"session", reference},
                                    {"executionBinding", target},
                                    {"required", Json::array({"memory-lifecycle-v1"})},
                                    {"optional", Json::array()}});
    auto valid = validate_wire("terminal-services-v1", "BindingRequest", body);
    if (!valid)
        return valid;
    CallOptions options;
    options.body = body;
    options.cancel = cancel;
    auto response = api->call("terminal.binding.create", options);
    if (!response)
        return response.error();
    const auto &value = response.value().body;
    valid = validate_wire("terminal-services-v1", "BindingResponse", value);
    if (!valid)
        return valid;
    bool accepted = false;
    for (const auto &feature : value.at("accepted").as_array())
        if (feature.as_string() == "memory-lifecycle-v1")
            accepted = true;
    if (!accepted || value.at("requestId").as_string() != request_id ||
        !equal(value.at("session"), reference) || !equal(value.at("scope"), expected_scope) ||
        !equal(value.at("executionBinding"), target))
        return Error{ErrorCode::contract, "publication binding identity or capability"};
    return {};
}
} // namespace demo::memory
