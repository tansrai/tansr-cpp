#include "internal.hpp"
#include <regex>

namespace tansr::archive::detail {
namespace {
std::int64_t timestamp(const std::string &value) {
    static const std::regex pattern(
        R"(^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?(Z|[+-]\d{2}:\d{2})$)");
    std::smatch parts;
    need(std::regex_match(value, parts, pattern));
    int year = std::stoi(parts[1]), month = std::stoi(parts[2]), day = std::stoi(parts[3]);
    const int hour = std::stoi(parts[4]), minute = std::stoi(parts[5]),
              second = std::stoi(parts[6]);
    need(month >= 1 && month <= 12 && day >= 1 && hour < 24 && minute < 60 && second < 60);
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    need(day <= days[month - 1] + (month == 2 && leap ? 1 : 0));
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const auto yoe = static_cast<unsigned>(year - era * 400);
    const auto doy =
        static_cast<unsigned>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const auto unix_days = static_cast<std::int64_t>(era) * 146097 + doe - 719468;
    std::string fraction = parts[7];
    fraction.resize(3, '0');
    std::int64_t offset{};
    const std::string zone = parts[8];
    if (zone != "Z") {
        const int hours = std::stoi(zone.substr(1, 2)), minutes = std::stoi(zone.substr(4, 2));
        need(hours < 24 && minutes < 60);
        offset = (hours * 60 + minutes) * 60000LL * (zone[0] == '-' ? -1 : 1);
    }
    return ((unix_days * 24 + hour) * 60 + minute) * 60000 + second * 1000 + std::stoi(fraction) -
           offset;
}
} // namespace
void verify_epoch(const Json &epoch, std::uint64_t maximum) {
    if (epoch.is_null())
        return;
    const auto start = timestamp(text(epoch, "issuedAt")),
               end = timestamp(text(epoch, "expiresAt"));
    need(end > start && (!maximum || static_cast<std::uint64_t>(end - start) <= maximum));
}
void verify_active_epoch(const Json &epoch) {
    need(!epoch.is_null() && text(epoch, "state") == "active");
    verify_epoch(epoch);
    const auto now = unix_time_ms();
    need(timestamp(text(epoch, "issuedAt")) <= now && timestamp(text(epoch, "expiresAt")) > now);
}
void verify_binding(const Json &binding) {
    validate("BindingView", binding);
    const auto &limits = binding.at("limits");
    verify_epoch(binding.at("operationEpoch"), limits.at("epochLifetimeMs").as_u64());
    std::set<std::string> accepted;
    for (const auto &item : binding.at("acceptedCapabilities").as_array())
        need(accepted.insert(item.as_string()).second);
    for (const auto &item : binding.at("rejectedCapabilities").as_array())
        need(accepted.insert(text(item, "capability")).second);
    need(limits.at("inflightReserveBytes").as_u64() <= limits.at("pendingBytes").as_u64());
    const auto &format = binding.at("archiveAckFormat");
    need(has(binding.at("acceptedCapabilities"), "archive-transfer-v1") ==
         (format.is_string() && format.as_string() == "split-receipts-v1"));
}
void verify_coverage(const Json &coverage) {
    validate("Coverage", coverage);
    need(seq(coverage.at("fromSequence")) > 0 &&
         seq(coverage.at("throughSequence")) >= seq(coverage.at("fromSequence")));
}
void verify_record(const Json &record, std::size_t maximum) {
    validate("ArchiveRecord", record);
    encode(record, maximum);
    need(domain("tansr.sdk2.record.v1", encode(without(record, "recordDigest"))) ==
         text(record, "recordDigest"));
    if (const auto *range = record.find("sourceEventRange"))
        need(range->at("firstSeq").as_u64() <= range->at("lastSeq").as_u64());
    if (const auto *projection = record.find("projection"))
        verify_coverage(projection->at("coverage"));
}
void verify_page(const Json &binding, const std::optional<std::string> &after, const Json &page) {
    validate("ArchivePage", page);
    need(equal(page.at("bindingId"), binding.at("bindingId")) &&
         equal(page.at("generations"), binding.at("target").at("generations")));
    const auto &limits = binding.at("limits");
    const auto &records = page.at("records").as_array();
    need(records.size() <= limits.at("pageRecords").as_u64());
    encode(page, static_cast<std::size_t>(limits.at("pageBytes").as_u64()));
    std::uint64_t previous = after ? seq(Json(*after)) : 0;
    std::optional<std::string> digest;
    std::set<std::string> ids;
    std::map<std::string, Json> refs;
    for (const auto &record : records) {
        need(previous != std::numeric_limits<std::uint64_t>::max() &&
             seq(record.at("sequence")) == previous + 1);
        need(ids.insert(text(record, "recordId")).second);
        need(equal(record.at("target").at("sessionId"), binding.at("target").at("sessionId")) &&
             equal(record.at("target").at("generations"), binding.at("target").at("generations")));
        if (!previous)
            need(text(record, "predecessorDigest") == std::string(64, '0'));
        if (digest)
            need(text(record, "predecessorDigest") == *digest);
        verify_record(record, static_cast<std::size_t>(limits.at("recordBytes").as_u64()));
        for (const auto &ref : references(record)) {
            need(equal(ref.at("sourceId"), binding.at("sourceId")) &&
                 ref.at("bytes").as_u64() <= limits.at("attachmentBytes").as_u64());
            auto inserted = refs.emplace(text(ref, "artifactId"), ref);
            need(inserted.second || equal(inserted.first->second, ref));
        }
        previous = seq(record.at("sequence"));
        digest = text(record, "recordDigest");
    }
    const Json next =
        records.empty() ? (after ? Json(*after) : Json()) : records.back().at("sequence");
    need(equal(page.at("nextAfterSequence"), next));
    const auto &published = page.at("publishedThroughSequence");
    if (published.is_null())
        need(records.empty() && !after && page.at("complete").as_bool());
    else {
        const auto end = seq(published);
        need(end > 0 && previous <= end && page.at("complete").as_bool() == (previous == end));
        need(page.at("complete").as_bool() || !records.empty());
    }
}
void verify_receipt(const Json &identity, const Json &ack, const Json &receipt) {
    validate("MutationReceipt", receipt);
    need(text(receipt, "state") == "completed" && text(receipt, "operation") == "archive-ack" &&
         equal(receipt.at("bindingId"), identity.at("bindingId")) &&
         equal(receipt.at("bindingId"), ack.at("bindingId")) &&
         equal(receipt.at("request"), ack.at("request")) &&
         seq(receipt.at("revision")) > seq(ack.at("expectedRevision")));
    auto frame = Json::object(
        {{"scope", Json::array({identity.at("applicationScopeId"), identity.at("endUserId")})},
         {"operation", "archive-ack"},
         {"semantic", without(ack, "request")}});
    need(domain("tansr.sdk2.operation.v1", encode(frame)) == text(receipt, "semanticDigest"));
}
void verify_rebase(const Json &input) {
    take(validate_wire("sdk2-archive-recovery-v1", "AckRebaseRequest", input));
    const auto &previous = input.at("previous");
    verify_coverage(previous.at("coverage"));
    need(equal(input.at("bindingId"), previous.at("bindingId")) &&
         !equal(input.at("request"), previous.at("request")) &&
         equal(input.at("request").at("operationEpoch"),
               previous.at("request").at("operationEpoch")));
    encode(previous, 262144);
    encode(input, 263168);
}
void verify_rebase_result(const Json &input, const Json &result) {
    verify_rebase(input);
    take(validate_wire("sdk2-archive-recovery-v1", "AckRebaseReceipt", result));
    for (const auto *key : {"protocol", "bindingId", "previous", "request"})
        need(equal(result.at(key), input.at(key)));
    need(equal(result.at("next").at("request"), input.at("request")) &&
         seq(result.at("next").at("expectedRevision")) >
             seq(input.at("previous").at("expectedRevision")));
    auto old = result.at("next");
    old.set("request", input.at("previous").at("request"));
    old.set("expectedRevision", input.at("previous").at("expectedRevision"));
    need(equal(old, input.at("previous")));
    encode(result.at("next"), 262144);
}
} // namespace tansr::archive::detail

namespace tansr::archive {
Result<Json> identity_from_binding(const Json &binding, const Json &status) {
    return detail::protect([&] {
        using namespace detail;
        verify_binding(binding);
        validate("ArchiveStatus", status);
        for (const auto *key : {"bindingId", "sourceId", "revision", "state"})
            need(equal(binding.at(key), status.at(key)));
        need(equal(binding.at("target").at("generations"), status.at("generations")) &&
             text(binding, "state") != "closed");
        return Json::object({{"applicationScopeId", binding.at("scope").at("applicationScopeId")},
                             {"endUserId", binding.at("scope").at("endUserId")},
                             {"bindingId", binding.at("bindingId")},
                             {"sessionId", binding.at("target").at("sessionId")},
                             {"generations", binding.at("target").at("generations")},
                             {"sourceId", binding.at("sourceId")},
                             {"sourceGeneration", status.at("sourceGeneration")}});
    });
}
} // namespace tansr::archive
