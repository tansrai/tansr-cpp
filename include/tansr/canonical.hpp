#pragma once

#include <tansr/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tansr::canonical {

inline constexpr std::size_t MAX_DEPTH = 32;
inline constexpr std::size_t MAX_NODES = 100000;
inline constexpr std::uint64_t MAX_SAFE_INTEGER = (std::uint64_t{1} << 53) - 1;
inline constexpr std::size_t DEFAULT_MAX_BYTES = 8 * 1024 * 1024;
inline constexpr std::string_view DOMAIN_CLOSURE = "tansr.unified.closure.v1";

// 普通 JSON 可有负数、小数和 Unicode 键；控制域拒绝这些数字词法/键。
TANSR_API Result<Json> decode(std::string_view bytes, std::size_t max_bytes = DEFAULT_MAX_BYTES);
TANSR_API Result<Json> parse_strict(std::string_view bytes,
                                    std::size_t max_bytes = DEFAULT_MAX_BYTES);
TANSR_API Result<std::string> encode(const Json &value);
TANSR_API Result<std::string> encode_limited(const Json &value, std::size_t max_bytes);
TANSR_API Result<std::string> digest(std::string_view domain, const Json &value);
TANSR_API Result<std::string> digest_bytes(std::string_view domain, std::string_view bytes);
TANSR_API std::string encode_path_segment(std::string_view value);

} // namespace tansr::canonical
