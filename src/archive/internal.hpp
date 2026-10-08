#pragma once
#include "tansr/archive.hpp"
#include "tansr/canonical.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <type_traits>

namespace tansr::archive::detail {
struct Failure {
    Error error;
};
[[noreturn]] inline void fail(ErrorCode code = ErrorCode::contract,
                              const char *text = "archive integrity or identity mismatch") {
    throw Failure{{code, text}};
}
inline void need(bool condition) {
    if (!condition)
        fail();
}
template <class T> T take(Result<T> result) {
    if (!result)
        throw Failure{result.error()};
    return std::move(result).value();
}
inline void take(Result<void> result) {
    if (!result)
        throw Failure{result.error()};
}
template <class F> auto protect(F &&f) -> Result<decltype(f())> {
    try {
        if constexpr (std::is_void_v<decltype(f())>) {
            f();
            return {};
        } else
            return f();
    } catch (const Failure &e) {
        return e.error;
    } catch (const std::bad_alloc &) {
        return Error{ErrorCode::capacity, "archive allocation limit exceeded"};
    } catch (...) {
        return Error{ErrorCode::contract, "archive value rejected"};
    }
}
inline void validate(std::string_view definition, const Json &value) {
    take(validate_wire(protocol, definition, value));
}
inline std::string text(const Json &value, std::string_view key) {
    return value.at(key).as_string();
}
inline bool equal(const Json &a, const Json &b) {
    return take(canonical::encode(a)) == take(canonical::encode(b));
}
inline std::string encode(const Json &value, std::size_t maximum = 2U << 20) {
    return take(canonical::encode_limited(value, maximum));
}
inline std::string hash(std::string_view bytes) { return take(crypto::sha256_hex(bytes)); }
inline std::string domain(std::string_view name, std::string_view bytes) {
    return take(canonical::digest_bytes(name, bytes));
}
inline Json without(Json value, std::string_view key) {
    auto &fields = value.as_object();
    fields.erase(std::remove_if(fields.begin(), fields.end(),
                                [&](const auto &field) { return field.first == key; }),
                 fields.end());
    return value;
}
inline std::uint64_t seq(const Json &value) {
    validate("Sequence", value);
    const auto &s = value.as_string();
    std::uint64_t out{};
    auto parsed = std::from_chars(s.data(), s.data() + s.size(), out);
    need(parsed.ec == std::errc{} && parsed.ptr == s.data() + s.size());
    return out;
}
inline bool has(const Json &values, std::string_view value) {
    for (const auto &item : values.as_array())
        if (item.as_string() == value)
            return true;
    return false;
}
inline std::optional<Json> optional(const Json &value) {
    return value.is_null() ? std::optional<Json>{} : value;
}
inline std::size_t bounded_add(std::size_t first, std::size_t second, std::size_t maximum) {
    if (second > maximum || first > maximum - second)
        fail(ErrorCode::capacity, "archive capacity exceeded");
    return first + second;
}
inline CallOptions deadline(CallOptions value) {
    if (!value.deadline_ms)
        value.deadline_ms = unix_time_ms() + 30000;
    if (value.cancel.is_cancelled())
        fail(ErrorCode::cancelled, "archive operation cancelled");
    if (*value.deadline_ms <= unix_time_ms())
        fail(ErrorCode::timeout, "archive deadline expired");
    return value;
}
inline CallOptions reading(std::string_view id, CallOptions value = {}) {
    value = deadline(std::move(value));
    if (!id.empty())
        value.parameters["id"] = std::string(id);
    value.query["protocol"] = protocol;
    return value;
}
inline CallOptions mutation(std::string_view id, const Json &body, CallOptions value = {}) {
    value = deadline(std::move(value));
    if (!id.empty())
        value.parameters["id"] = std::string(id);
    value.body = body;
    value.request_key = text(body.at("request"), "requestId");
    if (const auto *revision = body.find("expectedRevision"))
        value.if_match = "\"" + revision->as_string() + "\"";
    return value;
}
inline void generations(CallOptions &options, const Json &value) {
    for (const auto *key : {"historyEpoch", "deletionGeneration", "projectionRevision"})
        options.query[key] = text(value, key);
}
void verify_epoch(const Json &, std::uint64_t maximum = 0);
void verify_active_epoch(const Json &);
void verify_binding(const Json &);
void verify_coverage(const Json &);
void verify_record(const Json &, std::size_t maximum);
void verify_page(const Json &binding, const std::optional<std::string> &after, const Json &page);
void verify_receipt(const Json &identity, const Json &ack, const Json &receipt);
void verify_rebase(const Json &);
void verify_rebase_result(const Json &, const Json &);
inline std::vector<Json> references(const Json &record) {
    std::vector<Json> refs{record.at("payload")};
    for (const auto &item : record.at("attachments").as_array())
        refs.push_back(item);
    return refs;
}
} // namespace tansr::archive::detail
