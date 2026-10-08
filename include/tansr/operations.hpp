#pragma once
#include "tansr/export.hpp"
#include <optional>
#include <string_view>
#include <vector>

namespace tansr {
inline constexpr unsigned manifest_revision = 7;
inline constexpr std::string_view schema_hash =
    "b60e77ffcbf08d985a993dbdbd4cf610f12f7c7e70f090aee5ff8d523f70bb57";
struct ExpectedRevision {
    std::vector<std::string_view> path;
    std::string_view kind;
};
// 仅列出冻结 /api 元数据；不包含凭据、可变运行状态或路由回落。
struct Operation {
    std::string_view name;
    std::string_view domain;
    std::optional<std::string_view> family;
    std::string_view method;
    std::string_view path;
    std::string_view kind;
    bool sse;
    bool fenced;
    std::optional<std::string_view> request_schema;
    std::optional<std::string_view> response_schema;
    std::vector<std::string_view> query;
    std::vector<std::string_view> request_id_path;
    std::vector<std::string_view> etag_path;
    std::optional<ExpectedRevision> expected_revision;
};
TANSR_API const std::vector<Operation> &all_operations();
TANSR_API const Operation *find_operation(std::string_view name);
} // namespace tansr
