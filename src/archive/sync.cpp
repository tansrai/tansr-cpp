#include "internal.hpp"

namespace tansr::archive {
using namespace detail;
namespace {
bool detail_matches(const Error &error, std::string_view wire, std::string_view code,
                    std::string_view reason = {}) {
    if (error.wire_code != wire)
        return false;
    auto parsed = Json::parse(error.detail);
    if (!parsed)
        return false;
    const auto *domain_code = parsed.value().find("domainCode");
    const auto *why = parsed.value().find("reason");
    return domain_code && domain_code->is_string() && domain_code->as_string() == code &&
           (reason.empty() || (why && why->is_string() && why->as_string() == reason));
}
bool stale(const Error &error) {
    return detail_matches(error, "precondition_failed", "binding_conflict", "if_match_stale") ||
           detail_matches(error, "precondition_failed", "stale_revision", "if_match_stale");
}
Json pending_receipt(const ArchiveClient &client, ArchiveStore &store, const Json &pending,
                     CallOptions context) {
    take(store.check_access());
    auto saved = take(store.pending_deadline());
    need(saved.has_value());
    context.deadline_ms = std::min(*context.deadline_ms, *saved);
    // 未提交与已过期查询均可能返回 receipt_expired；重放耐久原请求交由服务端判定。
    return take(client.acknowledge(pending, std::move(context)));
}
SyncResult resume_rebase(const ArchiveClient &client, ArchiveStore &store, const Json &intent,
                         CallOptions context) {
    take(store.check_access());
    auto saved = take(store.pending_deadline());
    need(saved.has_value());
    context.deadline_ms = std::min(*context.deadline_ms, *saved);
    auto result = client.rebase_acknowledgement(intent, context);
    if (!result) {
        if (detail_matches(result.error(), "conflict", "request_id_conflict")) {
            // 原 ACK 与 rebase 竞争完成时只认原 scope-framed completed 回执。
            auto receipt = client.operation(text(intent, "bindingId"), "archive-ack",
                                            intent.at("previous").at("request"), context);
            if (receipt) {
                take(store.confirm(receipt.value()));
                return {0, false, true, receipt.value()};
            }
        }
        throw Failure{result.error()};
    }
    take(store.confirm_rebase(result.value()));
    return {0, false, true, result.value().at("receipt")};
}
} // namespace
Result<SyncResult> sync_once(const ArchiveClient &client, ArchiveStore &store,
                             std::string_view request_id, CallOptions context) {
    return protect([&] {
        if (!context.deadline_ms)
            context.deadline_ms = client.default_deadline_ms();
        context = deadline(std::move(context));
        take(store.check_access());
        if (auto intent = take(store.pending_rebase()))
            return resume_rebase(client, store, *intent, context);
        if (auto pending = take(store.pending())) {
            auto receipt = pending_receipt(client, store, *pending, context);
            take(store.confirm(receipt));
            return SyncResult{0, false, true, receipt};
        }
        const auto &identity = store.identity();
        auto binding = take(client.binding(text(identity, "bindingId"), context));
        auto status = take(client.status(text(identity, "bindingId"), context));
        need(equal(take(identity_from_binding(binding, status)), identity));
        auto head = take(store.head());
        auto page = take(client.records(
            binding, head ? std::optional<std::string>{text(*head, "sequence")} : std::nullopt,
            context));
        if (page.at("records").as_array().empty())
            return SyncResult{0, page.at("complete").as_bool(), false, {}};
        need(!binding.at("operationEpoch").is_null());
        auto request = Json::object({{"requestId", Json(request_id)},
                                     {"operationEpoch", binding.at("operationEpoch").at("id")}});
        validate("RequestIdentity", request);
        std::map<std::string, Json> refs;
        std::size_t total{};
        const auto limits = store.limits();
        for (const auto &record : page.at("records").as_array()) {
            total = bounded_add(total, encode(record).size(), limits.max_batch_bytes);
            for (const auto &ref : references(record))
                if (refs.emplace(text(ref, "artifactId"), ref).second)
                    total = bounded_add(total, static_cast<std::size_t>(ref.at("bytes").as_u64()),
                                        limits.max_batch_bytes);
        }
        need(refs.size() <= limits.max_artifacts);
        Bodies bodies;
        for (const auto &item : refs) {
            take(store.check_access());
            bodies.emplace(item.first, take(client.artifact(binding, item.second, context)));
        }
        auto ack = take(
            store.receive(binding, status, page, std::move(bodies), request, *context.deadline_ms));
        take(store.check_access());
        auto receipt = take(client.acknowledge(ack, context));
        take(store.confirm(receipt));
        return SyncResult{page.at("records").as_array().size(), page.at("complete").as_bool(),
                          false, receipt};
    });
}
Result<SyncResult> recover_pending(const ArchiveClient &client, ArchiveStore &store,
                                   std::string_view request_id, CallOptions context) {
    return protect([&] {
        if (!context.deadline_ms)
            context.deadline_ms = client.default_deadline_ms();
        context = deadline(std::move(context));
        take(store.check_access());
        if (auto intent = take(store.pending_rebase()))
            return resume_rebase(client, store, *intent, context);
        auto pending = take(store.pending());
        need(pending.has_value());
        auto original = protect([&] { return pending_receipt(client, store, *pending, context); });
        if (original) {
            take(store.confirm(original.value()));
            return SyncResult{0, false, true, original.value()};
        }
        if (!stale(original.error()))
            throw Failure{original.error()};
        auto binding = take(client.binding(text(*pending, "bindingId"), context));
        verify_active_epoch(binding.at("operationEpoch"));
        need(equal(binding.at("operationEpoch").at("id"),
                   pending->at("request").at("operationEpoch")));
        auto request =
            Json::object({{"requestId", Json(request_id)},
                          {"operationEpoch", pending->at("request").at("operationEpoch")}});
        auto intent = take(store.prepare_rebase(request, *context.deadline_ms));
        return resume_rebase(client, store, intent, context);
    });
}
} // namespace tansr::archive
