#include "internal.hpp"
#include <cmath>
#include <locale>
#include <sstream>

namespace tansr::executor {
namespace {
std::size_t utf16_length(std::string_view text) {
    std::size_t count = 0;
    for (unsigned char b : text)
        if ((b & 0xc0) != 0x80)
            count += (b >= 0xf0 ? 2U : 1U);
    return count;
}
bool finite_numbers(const Json &v) {
    if (v.is_number()) {
        const auto &t = v.as_number().token;
        std::istringstream stream(t);
        stream.imbue(std::locale::classic());
        double number = 0;
        stream >> number;
        // 词法已由 JSON 验证；num_get 对下溢到 0 也可能设 failbit。
        // 溢出饱和为非零最大值，不能作为正常有限输入通过。
        return stream.eof() && std::isfinite(number) && (!stream.fail() || number == 0);
    }
    if (v.is_array())
        for (const auto &x : v.as_array())
            if (!finite_numbers(x))
                return false;
    if (v.is_object())
        for (const auto &x : v.as_object())
            if (!finite_numbers(x.second))
                return false;
    return true;
}
Result<std::string> legacy_json(const Json &v) {
    if (v.is_object()) {
        using Pair = std::pair<std::string, Json>;
        std::vector<const Pair *> keys;
        for (const auto &p : v.as_object())
            keys.push_back(&p);
        auto index = [](std::string_view k) -> std::optional<std::uint32_t> {
            std::uint32_t n = 0;
            auto r = std::from_chars(k.data(), k.data() + k.size(), n);
            if (r.ec != std::errc{} || r.ptr != k.data() + k.size() || n == UINT32_MAX ||
                std::to_string(n) != k)
                return {};
            return n;
        };
        std::sort(keys.begin(), keys.end(), [&](auto a, auto b) {
            auto ia = index(a->first), ib = index(b->first);
            if (ia && ib)
                return *ia < *ib;
            if (bool(ia) != bool(ib))
                return bool(ia);
            return a->first < b->first;
        });
        std::string out = "{";
        bool first = true;
        for (auto p : keys) {
            if (p->first == "__proto__")
                return detail::invalid("prototype key");
            auto key = canonical::encode(Json(p->first));
            if (!key)
                return key.error();
            auto value = legacy_json(p->second);
            if (!value)
                return value.error();
            if (!first)
                out += ',';
            first = false;
            out += key.value() + ":" + value.value();
        }
        return out + "}";
    }
    if (v.is_array()) {
        std::string out = "[";
        bool first = true;
        for (const auto &e : v.as_array()) {
            auto s = legacy_json(e);
            if (!s)
                return s.error();
            if (!first)
                out += ',';
            first = false;
            out += s.value();
        }
        return out + "]";
    }
    return canonical::encode(v);
}
Result<void> parameter(const Json &v, std::size_t depth) {
    if (!v.is_object() || depth > 8)
        return detail::invalid("parameter shape");
    const std::set<std::string> keys{"type", "description", "optional", "items", "properties"};
    for (const auto &p : v.as_object())
        if (!keys.count(p.first))
            return detail::invalid("unknown parameter key");
    auto type = v.find("type");
    if (!type || !type->is_string() ||
        !std::set<std::string>{"string", "number", "boolean", "array", "object"}.count(
            type->as_string()))
        return detail::invalid("parameter type");
    if (auto p = v.find("description");
        p && (!p->is_string() || utf16_length(p->as_string()) > 2048))
        return detail::invalid("parameter description");
    if (auto p = v.find("optional"); p && !p->is_bool())
        return detail::invalid("parameter optional");
    if (auto p = v.find("items")) {
        auto r = parameter(*p, depth + 1);
        if (!r)
            return r;
    }
    if (auto p = v.find("properties")) {
        if (!p->is_object())
            return detail::invalid("properties shape");
        for (const auto &i : p->as_object()) {
            auto r = parameter(i.second, depth + 1);
            if (!r)
                return r;
        }
    }
    return {};
}
} // namespace
Result<Json> parse_tool_arguments(std::string_view text) {
    auto v = Json::parse(text, JsonLimits{32768, 32, 100000});
    if (!v)
        return v.error();
    if (!v.value().is_object())
        return detail::invalid("tool arguments must be object");
    if (!finite_numbers(v.value()))
        return detail::invalid("business number outside finite range");
    return std::move(v.value());
}
Result<void> verify_tool_result(const Json &v) {
    auto valid = parse_tool_arguments(v.dump());
    if (!valid)
        return valid.error();
    const auto *status = v.find("status");
    if (!status || !status->is_string())
        return detail::invalid("tool result status");
    if (status->as_string() == "error") {
        auto m = v.find("message");
        if (!m || !m->is_string() || m->as_string().empty() || utf16_length(m->as_string()) > 4096)
            return detail::invalid("invalid business error");
        return {};
    }
    auto err = v.find("isError");
    if (status->as_string() != "ok" || (err && !err->is_bool()))
        return detail::invalid("invalid tool result");
    auto content = v.find("content");
    if (!content || !content->is_array() || content->as_array().empty() ||
        content->as_array().size() > 64)
        return detail::invalid("content limit");
    for (const auto &entry : content->as_array()) {
        auto t = entry.find("t");
        if (!t || !t->is_string())
            return detail::invalid("invalid content entry");
        if (t->as_string() == "text") {
            auto s = entry.find("text");
            if (!s || !s->is_string())
                return detail::invalid("text content");
        } else if (t->as_string() == "image") {
            auto m = entry.find("mime"), d = entry.find("data");
            if (!m || !m->is_string() ||
                !std::set<std::string>{"image/png", "image/jpeg", "image/webp", "image/gif"}.count(
                    m->as_string()) ||
                !d || !d->is_string())
                return detail::invalid("image content");
        } else
            return detail::invalid("unknown content entry");
    }
    return {};
}
Result<std::string> definition_digest(const Json &v) {
    auto controlled = canonical::encode_limited(v, detail::control_bytes);
    if (!controlled)
        return controlled.error();
    if (!v.is_object())
        return detail::invalid("tool declaration must be object");
    const std::set<std::string> allowed{"name",     "description", "parameters",
                                        "readOnly", "effects",     "timeoutMs"};
    for (const auto &p : v.as_object())
        if (!allowed.count(p.first))
            return detail::invalid("unknown declaration key");
    auto n = v.find("name"), d = v.find("description");
    if (!n || !n->is_string() || n->as_string().empty() || n->as_string().size() > 64)
        return detail::invalid("tool name");
    auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    if (!alpha(n->as_string().front()))
        return detail::invalid("tool name");
    for (char c : n->as_string())
        if (!alpha(c) && !(c >= '0' && c <= '9') && c != '_')
            return detail::invalid("tool name");
    if (!d || !d->is_string() || d->as_string().empty() || utf16_length(d->as_string()) > 2048)
        return detail::invalid("tool description");
    if (auto p = v.find("readOnly"); p && !p->is_bool())
        return detail::invalid("declaration readOnly");
    if (auto p = v.find("timeoutMs")) {
        if (!p->is_number())
            return detail::invalid("timeoutMs");
        auto num = p->as_u64();
        if (num < 1000 || num > 600000)
            return detail::invalid("timeoutMs range");
    }
    if (auto p = v.find("effects")) {
        if (!p->is_array() || p->as_array().size() > 4)
            return detail::invalid("effects");
        std::set<std::string> seen;
        for (const auto &e : p->as_array())
            if (!e.is_string() ||
                !std::set<std::string>{"irreversible", "financial", "external", "affects-others"}
                     .count(e.as_string()) ||
                !seen.insert(e.as_string()).second)
                return detail::invalid("effects");
    }
    if (auto p = v.find("parameters")) {
        if (!p->is_object())
            return detail::invalid("parameters");
        auto bytes = legacy_json(*p);
        if (!bytes)
            return bytes.error();
        if (bytes.value().size() > 32768)
            return detail::invalid("parameters byte limit");
        for (const auto &field : p->as_object()) {
            auto r = parameter(field.second, 1);
            if (!r)
                return r.error();
        }
    }
    auto bytes = legacy_json(v);
    if (!bytes)
        return bytes.error();
    return canonical::digest_bytes("tansr.sdk2.client-tool.v1", bytes.value());
}
} // namespace tansr::executor
