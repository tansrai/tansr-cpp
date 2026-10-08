#pragma once
#include "tansr/session.hpp"
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace tansr::session::detail {
inline constexpr std::size_t media_bytes = 32U * 1024U * 1024U;
inline Error invalid(std::string message) {
    return {ErrorCode::invalid_input, "session: " + message};
}
inline Error contract(std::string message) { return {ErrorCode::contract, "session: " + message}; }
inline const Json &field(const Json &value, std::string_view key) {
    static const Json missing;
    const auto *p = value.find(key);
    return p ? *p : missing;
}
inline std::optional<std::string> string(const Json &value) {
    return value.is_string() ? std::optional<std::string>(value.as_string()) : std::nullopt;
}
inline bool eq(const Json &value, std::string_view text) {
    return value.is_string() && value.as_string() == text;
}
inline Result<std::string> required_string(const Json &value, std::string_view key) {
    const auto &v = field(value, key);
    if (!v.is_string() || v.as_string().empty())
        return contract("missing or empty string field");
    return v.as_string();
}
inline Result<std::uint64_t> safe_number(const Json &v) {
    if (!v.is_number())
        return contract("missing or unsafe integer");
    try {
        auto n = v.as_u64();
        if (n <= max_safe_integer)
            return n;
    } catch (const std::exception &) {
    }
    return contract("missing or unsafe integer");
}
inline Result<std::uint64_t> safe_field(const Json &v, std::string_view key) {
    return safe_number(field(v, key));
}
inline Result<Json> object_response(const ApiResponse &response, int status) {
    if (response.status != status || response.content_type != "application/json" ||
        !response.body.is_object())
        return contract("unexpected response status, content type or object");
    return response.body;
}
inline std::size_t utf16_units(std::string_view text) {
    if (!valid_utf8(text))
        return std::numeric_limits<std::size_t>::max();
    std::size_t count = 0;
    for (unsigned char c : text)
        if ((c & 0xc0U) != 0x80U)
            count += c >= 0xf0U ? 2U : 1U;
    return count;
}
inline bool blank(std::string_view text) {
    if (!valid_utf8(text))
        return true;
    for (std::size_t i = 0; i < text.size();) {
        auto code = static_cast<std::uint32_t>(static_cast<unsigned char>(text[i++]));
        unsigned continuation = 0;
        if (code >= 0xf0) {
            code &= 7;
            continuation = 3;
        } else if (code >= 0xe0) {
            code &= 15;
            continuation = 2;
        } else if (code >= 0xc0) {
            code &= 31;
            continuation = 1;
        }
        while (continuation--)
            code = (code << 6) | (static_cast<unsigned char>(text[i++]) & 63U);
        const bool whitespace =
            (code >= 9 && code <= 13) || code == 32 || code == 0x85 || code == 0xa0 ||
            code == 0x1680 || (code >= 0x2000 && code <= 0x200a) || code == 0x2028 ||
            code == 0x2029 || code == 0x202f || code == 0x205f || code == 0x3000;
        if (!whitespace)
            return false;
    }
    return true;
}
inline Result<void> nonempty(std::string_view v) {
    if (v.empty() || !valid_utf8(v))
        return invalid("value must be nonempty UTF-8");
    return {};
}
inline Result<void> label(std::string_view v) {
    if (utf16_units(v) > 120)
        return invalid("checkpoint label exceeds 120 UTF-16 units");
    return {};
}
inline CallOptions write_options(const WriteOptions &v) {
    CallOptions out;
    out.request_key = v.request_key;
    out.deadline_ms = v.deadline_ms;
    out.cancel = v.cancel;
    return out;
}
inline Json number(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return Json::number(stream.str()).value();
}
template <class T>
inline void optional(Json &target, const char *key, const std::optional<T> &value) {
    if (value)
        target.set(key, Json(*value));
}
inline Json blocks_json(const std::vector<Block> &blocks) {
    Json::Array out;
    for (const auto &block : blocks) {
        if (const auto *text = std::get_if<TextBlock>(&block))
            out.push_back(Json::object({{"t", "text"}, {"text", text->text}}));
        else {
            const auto &image = std::get<ImageBlock>(block);
            out.push_back(
                Json::object({{"t", "image"}, {"mime", image.mime}, {"data", image.data}}));
        }
    }
    return Json(std::move(out));
}
inline Result<void> validate_blocks(const std::vector<Block> &blocks, bool text_only) {
    if (blocks.empty() || blocks.size() > 64)
        return invalid("message requires 1 to 64 blocks");
    for (const auto &block : blocks) {
        if (const auto *text = std::get_if<TextBlock>(&block)) {
            if (text->text.empty() || utf16_units(text->text) > 262144)
                return invalid("unsupported or empty message block");
        } else {
            const auto &image = std::get<ImageBlock>(block);
            if (text_only || image.data.empty() || !valid_utf8(image.data) ||
                (image.mime != "image/png" && image.mime != "image/jpeg" &&
                 image.mime != "image/gif" && image.mime != "image/webp"))
                return invalid("unsupported or empty message block");
        }
    }
    return {};
}
inline Result<void> check_family(const Json &v, std::string_view family) {
    if (family == "sdk2-offload-v1" &&
        (!eq(field(v, "contract"), family) || !eq(field(v, "availability"), "source-required")))
        return contract("offload family/source lifecycle is missing");
    return {};
}
inline Result<Meta> read_meta(Json raw, std::string_view family) {
    auto id = required_string(raw, "sessionId");
    if (!id)
        return id.error();
    auto status = required_string(raw, "status");
    if (!status)
        return status.error();
    if (status.value() != "idle" && status.value() != "running" && status.value() != "ended")
        return contract("invalid session status");
    const auto &live = field(raw, "live");
    if (!live.is_bool())
        return contract("metadata must contain live");
    auto seq = safe_field(raw, "lastSeq");
    if (!seq)
        return seq.error();
    auto family_check = check_family(raw, family);
    if (!family_check)
        return family_check.error();
    return Meta{std::move(id).value(), std::move(status).value(), live.as_bool(), seq.value(),
                std::move(raw)};
}
inline Result<Accepted> accepted_response(const ApiResponse &response, std::string_view id,
                                          bool with_session) {
    auto obj = object_response(response, with_session ? 202 : 200);
    if (!obj)
        return obj.error();
    const auto &v = obj.value();
    const auto &accepted = field(v, "accepted");
    auto sid = string(field(v, "sessionId"));
    if (!accepted.is_bool() || !accepted.as_bool() || (with_session && (!sid || *sid != id)))
        return contract("invalid acceptance receipt");
    return Accepted{true, std::move(sid)};
}
inline Result<std::uint64_t> parse_sequence(std::string_view v) {
    if (v.empty() || (v.size() > 1 && v[0] == '0'))
        return invalid("cursor must be canonical nonnegative decimal");
    std::uint64_t n = 0;
    for (char c : v) {
        if (c < '0' || c > '9')
            return invalid("cursor must be canonical nonnegative decimal");
        const auto digit = static_cast<unsigned>(c - '0');
        if (n > (max_safe_integer - digit) / 10)
            return invalid("cursor exceeds safe integer limit");
        n = n * 10 + digit;
    }
    return n;
}
} // namespace tansr::session::detail
