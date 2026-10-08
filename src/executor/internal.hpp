#pragma once
#include "tansr/canonical.hpp"
#include "tansr/crypto.hpp"
#include "tansr/executor.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>

namespace tansr::executor::detail {
inline constexpr std::size_t control_bytes = 262144;
inline Error invalid(std::string text) { return {ErrorCode::contract, "executor: " + text}; }
inline Error unknown(std::string text) { return {ErrorCode::unknown, "executor: " + text}; }
inline Json optional_string(const std::optional<std::string> &v) { return v ? Json(*v) : Json(); }
inline std::optional<std::string> read_optional(const Json &v, std::string_view k) {
    auto p = v.find(k);
    return p && p->is_string() ? std::optional<std::string>(p->as_string()) : std::nullopt;
}
inline bool equal(const Json &a, const Json &b) {
    auto ca = canonical::encode(a);
    auto cb = canonical::encode(b);
    return ca && cb && ca.value() == cb.value();
}
Json json(const Scope &);
Json json(const Platform &);
Json json(const Workspace &);
Json json(const Interpreter &);
Json json(const Target &);
Json json(const Binding &);
Json json(const Registration &);
Json json(const Connection &);
Json json(const Resource &);
Json json(const TerminalSessionReference &);
Json json(const OutputOperationReference &);
Json json(const OutputLimits &);
Json json(const OutputSeal &);
Json json(const OutputStatus &);
Result<Connection> connection(const Json &);
Result<Capabilities> capabilities(const Json &);
Result<Operation> operation(const Json &);
Result<Receipt> receipt(const Json &);
Result<Status> status(const Json &);
Result<OutputStatus> output_status(const Json &);
Result<std::int64_t> expiry(std::string_view);
Result<void> live(const Connection &);
Result<void> registration(const Registration &);
Result<void> validate_output(const OutputStatus &);
Result<std::int64_t> sequence(const std::optional<std::string> &);
Receipt receipt_for(const Operation &, std::string status, std::optional<std::string> error = {},
                    std::optional<Resource> result = {});
Result<Json> call(const std::shared_ptr<ApiClient> &api, std::string_view operation,
                  std::string_view family, std::string_view schema, CallOptions options,
                  int expected = 200);
Result<OutputStatus> query_output(const std::shared_ptr<ApiClient> &,
                                  const TerminalSessionReference &,
                                  const OutputOperationReference &, std::size_t, CancellationToken,
                                  std::optional<std::int64_t> = {});
} // namespace tansr::executor::detail
