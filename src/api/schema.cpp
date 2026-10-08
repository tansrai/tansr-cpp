#include "tansr/api.hpp"
#include "tansr/operations.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace tansr {
namespace {
#include "operations.inc"
struct EmbeddedSchema {
    std::string_view family;
    const std::string_view *chunks;
    std::size_t count;
};
#include "schema_data.inc"

bool ascii_lower(char c) { return c >= 'a' && c <= 'z'; }
bool ascii_alpha(char c) { return ascii_lower(c) || (c >= 'A' && c <= 'Z'); }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool alnum(char c) { return ascii_alpha(c) || digit(c); }
bool hex(char c) { return digit(c) || (c >= 'a' && c <= 'f'); }
template <class Predicate> bool all(std::string_view s, Predicate predicate) {
    return std::all_of(s.begin(), s.end(), predicate);
}
bool starts(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}
bool decimal(std::string_view s) {
    return !s.empty() && all(s, digit) && (s.size() == 1 || s.front() != '0');
}
bool integer(std::string_view s) {
    if (!s.empty() && s.front() == '-')
        s.remove_prefix(1);
    return decimal(s);
}
int integer_compare(std::string_view a, std::string_view b) {
    bool negative_a = a.front() == '-';
    bool negative_b = b.front() == '-';
    if (negative_a)
        a.remove_prefix(1);
    if (negative_b)
        b.remove_prefix(1);
    if (a == "0")
        negative_a = false;
    if (b == "0")
        negative_b = false;
    if (negative_a != negative_b)
        return negative_a ? -1 : 1;
    int compared = a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : a.compare(b);
    compared = compared < 0 ? -1 : compared > 0 ? 1 : 0;
    return negative_a ? -compared : compared;
}
bool integer_type(std::string_view s) {
    return integer(s) && integer_compare(s, "-9223372036854775808") >= 0 &&
           integer_compare(s, "18446744073709551615") <= 0;
}
template <class Predicate> bool segments(std::string_view s, char delimiter, Predicate predicate) {
    for (;;) {
        const auto cut = s.find(delimiter);
        if (!predicate(s.substr(0, cut)))
            return false;
        if (cut == std::string_view::npos)
            return true;
        s.remove_prefix(cut + 1);
    }
}
bool token(std::string_view s) {
    return !s.empty() && alnum(s.front()) &&
           all(s, [](char c) { return alnum(c) || c == '.' || c == '_' || c == '~' || c == '-'; });
}
bool unicode_space(std::string_view s) {
    // 与冻结规则的 Unicode \s 一致，先由 JSON 边界保证 UTF-8。
    for (std::size_t i = 0; i < s.size();) {
        std::uint32_t code = static_cast<unsigned char>(s[i++]);
        unsigned tail = 0;
        if (code >= 0xf0) {
            code &= 7;
            tail = 3;
        } else if (code >= 0xe0) {
            code &= 15;
            tail = 2;
        } else if (code >= 0xc0) {
            code &= 31;
            tail = 1;
        }
        while (tail-- > 0 && i < s.size())
            code = (code << 6) | (static_cast<unsigned char>(s[i++]) & 63U);
        if ((code >= 9 && code <= 13) || code == 32 || code == 0x85 || code == 0xa0 ||
            code == 0x1680 || (code >= 0x2000 && code <= 0x200a) || code == 0x2028 ||
            code == 0x2029 || code == 0x202f || code == 0x205f || code == 0x3000)
            return true;
    }
    return false;
}
bool route(std::string_view s, bool legacy, bool wildcard, bool allow_root) {
    if (legacy) {
        if (s.size() < 3 || s[0] != '/' || s[1] != 'v' || s[2] < '1' || s[2] > '3')
            return false;
        s.remove_prefix(3);
    } else {
        if (!starts(s, "/api"))
            return false;
        s.remove_prefix(4);
    }
    if (s.empty())
        return allow_root;
    if (s.front() != '/')
        return false;
    s.remove_prefix(1);
    return segments(s, '/', [wildcard](std::string_view part) {
        if (part.empty())
            return false;
        if (wildcard && part == "**")
            return true;
        if (part.front() == ':')
            return part.size() > 1 && all(part.substr(1), ascii_alpha);
        return all(part,
                   [](char c) { return alnum(c) || c == '.' || c == '_' || c == '~' || c == '-'; });
    });
}
bool base64(std::string_view s) {
    if (s.size() % 4 != 0)
        return false;
    std::size_t padding = 0;
    if (!s.empty() && s.back() == '=') {
        padding = 1;
        if (s.size() > 1 && s[s.size() - 2] == '=')
            padding = 2;
    }
    return all(s.substr(0, s.size() - padding),
               [](char c) { return alnum(c) || c == '+' || c == '/'; });
}

// 只解释本冻结集合的模式，全部线性扫描，避免 std::regex 对长 base64/标识递归耗尽栈。
bool pattern_match(std::string_view p, std::string_view s) {
    if (p == "^/")
        return starts(s, "/");
    if (p == "^/v3/sdk2/")
        return starts(s, "/v3/sdk2/");
    if (p == "^/api(/(:[A-Za-z]+|[A-Za-z0-9._~-]+))+$")
        return route(s, false, false, false);
    if (p == "^/api(/(:[A-Za-z]+|[A-Za-z0-9._~-]+|\\*\\*))*$")
        return route(s, false, true, true);
    if (p == "^/v[123](/(:[A-Za-z]+|[A-Za-z0-9._~-]+|\\*\\*))+$")
        return route(s, true, true, false);
    if (p == "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$")
        return base64(s);
    if (p == "^(0|[1-9][0-9]*)$")
        return decimal(s);
    if (p == "^(0|[1-9][0-9]{0,18})$")
        return s.size() <= 19 && decimal(s);
    if (starts(p, "^(0|[1-9][0-9]{0,17}|"))
        return decimal(s) && integer_compare(s, "9223372036854775807") <= 0;
    if (p == "^(0|[1-9][0-9]*|-[1-9][0-9]*)$")
        return integer(s) && s != "-0";
    if (p == "^[1-9][0-9]*$")
        return decimal(s) && s != "0";
    if (p == "^(0|[1-9][0-9]*)(\\.[0-9]+)?$" || p == "^-?\\d+(?:\\.\\d+)?$") {
        const bool raw = p == "^-?\\d+(?:\\.\\d+)?$";
        if (raw && !s.empty() && s.front() == '-')
            s.remove_prefix(1);
        const auto dot = s.find('.');
        const auto whole = s.substr(0, dot);
        if (whole.empty() || !(raw ? all(whole, digit) : decimal(whole)))
            return false;
        return dot == std::string_view::npos ||
               (dot + 1 < s.size() && all(s.substr(dot + 1), digit));
    }
    if (p == "^[a-f0-9]{64}$")
        return s.size() == 64 && all(s, hex);
    if (p == "^sha256:[a-f0-9]{64}$")
        return starts(s, "sha256:") && s.size() == 71 && all(s.substr(7), hex);
    if (p == "^(sha256:[a-f0-9]{64}|none)$")
        return s == "none" || pattern_match("^sha256:[a-f0-9]{64}$", s);
    if (p == "^[A-Za-z0-9][A-Za-z0-9._~-]*$")
        return token(s);
    if (p == "^[A-Za-z0-9_-]+$" || p == "^[A-Za-z0-9_-]{43}$")
        return !s.empty() && (p != "^[A-Za-z0-9_-]{43}$" || s.size() == 43) &&
               all(s, [](char c) { return alnum(c) || c == '_' || c == '-'; });
    if (p == "^twp:[A-Za-z0-9_-]{8,40}$")
        return starts(s, "twp:") && s.size() >= 12 && s.size() <= 44 &&
               pattern_match("^[A-Za-z0-9_-]+$", s.substr(4));
    if (p == "^[A-Za-z0-9._-]{1,128}$")
        return !s.empty() && s.size() <= 128 &&
               all(s, [](char c) { return alnum(c) || c == '.' || c == '_' || c == '-'; });
    if (p == "^[A-Za-z][A-Za-z0-9]*$")
        return !s.empty() && ascii_alpha(s.front()) && all(s, alnum);
    if (p == "^[a-z][a-z0-9_]*$")
        return !s.empty() && ascii_lower(s.front()) &&
               all(s, [](char c) { return ascii_lower(c) || digit(c) || c == '_'; });
    if (p == "^[a-z][a-z0-9-]*$")
        return !s.empty() && ascii_lower(s.front()) &&
               all(s, [](char c) { return ascii_lower(c) || digit(c) || c == '-'; });
    if (p == "^[a-z0-9]+(-[a-z0-9]+)*$")
        return segments(s, '-', [](std::string_view v) {
            return !v.empty() && all(v, [](char c) { return ascii_lower(c) || digit(c); });
        });
    if (p == "^tansr-[a-z]+$")
        return starts(s, "tansr-") && s.size() > 6 && all(s.substr(6), ascii_lower);
    if (p == "^[a-z][a-z0-9_-]*(\\.[a-z][a-z0-9_-]*)*$")
        return segments(s, '.', [](std::string_view v) {
            return !v.empty() && ascii_lower(v.front()) && all(v, [](char c) {
                return ascii_lower(c) || digit(c) || c == '_' || c == '-';
            });
        });
    if (p == "^[a-z]+(\\.[a-z][a-z0-9]*)+$" || p == "^[a-z]+(\\.[A-Za-z0-9][A-Za-z0-9-]*)+$") {
        const auto dot = s.find('.');
        if (dot == std::string_view::npos || dot == 0 || !all(s.substr(0, dot), ascii_lower))
            return false;
        const bool lower = p == "^[a-z]+(\\.[a-z][a-z0-9]*)+$";
        return segments(s.substr(dot + 1), '.', [lower](std::string_view v) {
            return !v.empty() && (lower ? ascii_lower(v.front()) : alnum(v.front())) &&
                   all(v, [lower](char c) {
                       return lower ? ascii_lower(c) || digit(c) : alnum(c) || c == '-';
                   });
        });
    }
    if (p == "^[0-9]+\\.[A-Za-z0-9][A-Za-z0-9._~-]*$") {
        const auto dot = s.find('.');
        return dot != std::string_view::npos && dot > 0 && all(s.substr(0, dot), digit) &&
               token(s.substr(dot + 1));
    }
    if (p == "^[^#\\s]+#[A-Za-z_][A-Za-z0-9_]*$" || p == "^[a-z0-9-]+#[A-Za-z][A-Za-z0-9]*$") {
        const auto marker = s.find('#');
        if (marker == std::string_view::npos || marker == 0)
            return false;
        const auto head = s.substr(0, marker);
        const auto tail = s.substr(marker + 1);
        const bool permissive = p == "^[^#\\s]+#[A-Za-z_][A-Za-z0-9_]*$";
        if (tail.empty() || !(ascii_alpha(tail.front()) || (permissive && tail.front() == '_')))
            return false;
        if (!all(tail, [permissive](char c) { return alnum(c) || (permissive && c == '_'); }))
            return false;
        return permissive
                   ? !unicode_space(head)
                   : all(head, [](char c) { return ascii_lower(c) || digit(c) || c == '-'; });
    }
    if (p == "^[^\\s\\\\]+$")
        return !s.empty() && s.find('\\') == std::string_view::npos && !unicode_space(s);
    if (p == "^[^\\u0000-\\u001f\\u007f]+$")
        return !s.empty() && all(s, [](char c) {
            const auto b = static_cast<unsigned char>(c);
            return b > 31 && b != 127;
        });
    if (p == "^[\\x21-\\x7E]{1,128}$")
        return !s.empty() && s.size() <= 128 && all(s, [](char c) { return c >= 33 && c <= 126; });
    if (p == "^Bearer [\\x21-\\x7E]+$")
        return starts(s, "Bearer ") && s.size() > 7 &&
               all(s.substr(7), [](char c) { return c >= 33 && c <= 126; });
    if (p == "^(W/)?\"[\\x21\\x23-\\x7E]*\"$") {
        if (starts(s, "W/"))
            s.remove_prefix(2);
        return s.size() >= 2 && s.front() == '"' && s.back() == '"' &&
               all(s.substr(1, s.size() - 2),
                   [](char c) { return c == 33 || (c >= 35 && c <= 126); });
    }
    if (p == "^[A-Za-z0-9][A-Za-z0-9!#$&^_.+/-]*$")
        return !s.empty() && alnum(s.front()) && all(s, [](char c) {
            return alnum(c) || std::string_view("!#$&^_.+/-").find(c) != std::string_view::npos;
        });
    if (p == "^[0-9]{4}-[0-9]{2}-[0-9]{2}$")
        return s.size() == 10 && s[4] == '-' && s[7] == '-' && all(s.substr(0, 4), digit) &&
               all(s.substr(5, 2), digit) && all(s.substr(8, 2), digit);
    if (p == "^[0-9a-f]{2}-[0-9a-f]{32}-[0-9a-f]{16}-[0-9a-f]{2}$") {
        return s.size() == 55 && s[2] == '-' && s[35] == '-' && s[52] == '-' &&
               all(s.substr(0, 2), hex) && all(s.substr(3, 32), hex) &&
               all(s.substr(36, 16), hex) && all(s.substr(53, 2), hex);
    }
    throw std::logic_error("unsupported frozen pattern");
}

bool date_time(std::string_view s) {
    if (s.size() < 20 || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != 't') ||
        s[13] != ':' || s[16] != ':')
        return false;
    const auto number = [s](std::size_t position, std::size_t count) {
        int n = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const char c = s[position + i];
            if (!digit(c))
                return -1;
            n = n * 10 + c - '0';
        }
        return n;
    };
    const int year = number(0, 4), month = number(5, 2), day = number(8, 2);
    const int hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
    if (year < 0 || month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59 || second < 0 || second > 60)
        return false;
    constexpr std::array<int, 12> days{{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};
    const int maximum =
        days[static_cast<std::size_t>(month - 1)] +
        ((month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0);
    if (day > maximum)
        return false;
    std::size_t end = 19;
    if (s[end] == '.') {
        ++end;
        const auto begin = end;
        while (end < s.size() && digit(s[end]))
            ++end;
        if (begin == end)
            return false;
    }
    if (end == s.size())
        return false;
    if (s[end] == 'Z' || s[end] == 'z')
        return end + 1 == s.size();
    if ((s[end] != '+' && s[end] != '-') || end + 6 != s.size() || s[end + 3] != ':')
        return false;
    const int offset_hour = number(end + 1, 2), offset_minute = number(end + 4, 2);
    return offset_hour >= 0 && offset_hour <= 23 && offset_minute >= 0 && offset_minute <= 59;
}

bool equal(const Json &a, const Json &b) {
    if (a.is_object() && b.is_object()) {
        if (a.as_object().size() != b.as_object().size())
            return false;
        for (const auto &pair : a.as_object()) {
            const auto *v = b.find(pair.first);
            if (v == nullptr || !equal(pair.second, *v))
                return false;
        }
        return true;
    }
    if (a.is_array() && b.is_array()) {
        if (a.as_array().size() != b.as_array().size())
            return false;
        for (std::size_t i = 0; i < a.as_array().size(); ++i)
            if (!equal(a.at(i), b.at(i)))
                return false;
        return true;
    }
    return a.dump() == b.dump();
}
std::string sorted_key(const Json &value) {
    if (value.is_object()) {
        std::vector<std::pair<std::string, std::string>> pairs;
        for (const auto &pair : value.as_object())
            pairs.emplace_back(pair.first, sorted_key(pair.second));
        std::sort(pairs.begin(), pairs.end());
        std::string result = "{";
        for (const auto &pair : pairs)
            result += Json(pair.first).dump() + ":" + pair.second + ",";
        return result + "}";
    }
    if (value.is_array()) {
        std::string result = "[";
        for (const auto &item : value.as_array())
            result += sorted_key(item) + ",";
        return result + "]";
    }
    return value.dump();
}
const Json *pointer(const Json &root, std::string_view path) {
    const Json *current = &root;
    while (!path.empty()) {
        if (path.front() != '/')
            return nullptr;
        path.remove_prefix(1);
        const auto end = path.find('/');
        const auto part = path.substr(0, end);
        std::string key;
        for (std::size_t i = 0; i < part.size(); ++i) {
            if (part[i] == '~') {
                if (i + 1 == part.size() || (part[i + 1] != '0' && part[i + 1] != '1'))
                    return nullptr;
                key += part[++i] == '0' ? '~' : '/';
            } else
                key += part[i];
        }
        current = current->find(key);
        if (current == nullptr)
            return nullptr;
        if (end == std::string_view::npos)
            return current;
        path.remove_prefix(end);
    }
    return current;
}
bool tree_valid(const Json &value, unsigned depth, std::size_t &nodes) {
    if (depth > 64 || ++nodes > 200000)
        return false;
    if (value.is_string())
        return valid_utf8(value.as_string());
    if (value.is_object()) {
        std::set<std::string_view> keys;
        for (const auto &pair : value.as_object())
            if (!valid_utf8(pair.first) || !keys.insert(pair.first).second ||
                !tree_valid(pair.second, depth + 1, nodes))
                return false;
    }
    if (value.is_array())
        for (const auto &item : value.as_array())
            if (!tree_valid(item, depth + 1, nodes))
                return false;
    return true;
}
constexpr std::string_view keywords[]{"$comment",
                                      "$id",
                                      "$ref",
                                      "$schema",
                                      "additionalProperties",
                                      "allOf",
                                      "anyOf",
                                      "const",
                                      "contains",
                                      "default",
                                      "definitions",
                                      "description",
                                      "else",
                                      "enum",
                                      "format",
                                      "if",
                                      "items",
                                      "maximum",
                                      "maxItems",
                                      "maxLength",
                                      "maxProperties",
                                      "minimum",
                                      "minItems",
                                      "minLength",
                                      "minProperties",
                                      "multipleOf",
                                      "not",
                                      "oneOf",
                                      "pattern",
                                      "properties",
                                      "propertyNames",
                                      "required",
                                      "then",
                                      "title",
                                      "type",
                                      "uniqueItems",
                                      "x-wire-limits",
                                      "exclusiveMinimum",
                                      "exclusiveMaximum"};
void audit(const Json &rule) {
    if (rule.is_bool())
        return;
    if (!rule.is_object())
        throw std::logic_error("invalid frozen schema rule");
    for (const auto &pair : rule.as_object())
        if (std::find(std::begin(keywords), std::end(keywords), pair.first) == std::end(keywords))
            throw std::logic_error("unsupported frozen keyword");
    if (const auto *pattern = rule.find("pattern"))
        (void)pattern_match(pattern->as_string(), "");
    if (const auto *format = rule.find("format"))
        if (format->as_string() != "date-time")
            throw std::logic_error("unsupported frozen format");
    for (const auto name : {"definitions", "properties"})
        if (const auto *children = rule.find(name))
            for (const auto &pair : children->as_object())
                audit(pair.second);
    for (const auto name : {"allOf", "anyOf", "oneOf"})
        if (const auto *children = rule.find(name))
            for (const auto &child : children->as_array())
                audit(child);
    for (const auto name : {"additionalProperties", "contains", "items", "propertyNames", "if",
                            "then", "else", "not"})
        if (const auto *child = rule.find(name))
            audit(*child);
}
struct Schema {
    std::string_view family;
    Json root;
};
const std::vector<Schema> &schemas() {
    static const auto result = [] {
        std::vector<Schema> values;
        for (const auto &item : embedded_schemas) {
            std::string bytes;
            for (std::size_t i = 0; i < item.count; ++i)
                bytes.append(item.chunks[i]);
            auto parsed = Json::parse(bytes, JsonLimits{8U * 1024U * 1024U, 64, 200000});
            if (!parsed)
                throw std::logic_error("cannot decode frozen schema");
            audit(parsed.value());
            values.push_back({item.family, std::move(parsed).value()});
        }
        return values;
    }();
    return result;
}
struct Checker {
    const Json &root;
    std::size_t budget = 4000000;
    bool bound_size(const Json &rule, std::size_t size, std::string_view minimum,
                    std::string_view maximum) {
        const auto *low = rule.find(minimum);
        const auto *high = rule.find(maximum);
        return (!low || size >= low->as_u64()) && (!high || size <= high->as_u64());
    }
    bool type_matches(std::string_view kind, const Json &value) {
        return (kind == "null" && value.is_null()) || (kind == "boolean" && value.is_bool()) ||
               (kind == "object" && value.is_object()) || (kind == "array" && value.is_array()) ||
               (kind == "string" && value.is_string()) || (kind == "number" && value.is_number()) ||
               (kind == "integer" && value.is_number() && integer_type(value.number_token()));
    }
    bool check(const Json &rule, const Json &value, unsigned depth = 0) {
        if (depth > 128 || budget == 0)
            throw std::length_error("schema validation resource limit");
        --budget;
        if (rule.is_bool())
            return rule.as_bool();
        if (const auto *ref = rule.find("$ref")) {
            const auto &reference = ref->as_string();
            if (reference.empty() || reference.front() != '#')
                throw std::logic_error("external frozen schema reference");
            const auto *target = pointer(root, std::string_view(reference).substr(1));
            if (!target)
                throw std::logic_error("unresolved frozen schema reference");
            return check(*target, value, depth + 1);
        }
        if (const auto *constant = rule.find("const"))
            if (!equal(*constant, value))
                return false;
        if (const auto *choices = rule.find("enum"))
            if (std::none_of(choices->as_array().begin(), choices->as_array().end(),
                             [&value](const Json &v) { return equal(v, value); }))
                return false;
        if (const auto *kind = rule.find("type")) {
            if (kind->is_string()) {
                if (!type_matches(kind->as_string(), value))
                    return false;
            } else if (std::none_of(
                           kind->as_array().begin(), kind->as_array().end(),
                           [&](const Json &t) { return type_matches(t.as_string(), value); }))
                return false;
        }
        for (const auto name : {"allOf", "anyOf", "oneOf"})
            if (const auto *choices = rule.find(name)) {
                std::size_t count = 0;
                for (const auto &item : choices->as_array())
                    if (check(item, value, depth + 1))
                        ++count;
                if ((std::string_view(name) == "allOf" && count != choices->as_array().size()) ||
                    (std::string_view(name) == "anyOf" && count == 0) ||
                    (std::string_view(name) == "oneOf" && count != 1))
                    return false;
            }
        if (const auto *forbidden = rule.find("not"))
            if (check(*forbidden, value, depth + 1))
                return false;
        if (const auto *condition = rule.find("if"))
            if (const auto *branch =
                    rule.find(check(*condition, value, depth + 1) ? "then" : "else"))
                if (!check(*branch, value, depth + 1))
                    return false;
        if (value.is_string()) {
            const auto &text = value.as_string();
            const auto count =
                static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char c) {
                    return (static_cast<unsigned char>(c) & 0xc0U) != 0x80U;
                }));
            if (!bound_size(rule, count, "minLength", "maxLength"))
                return false;
            if (const auto *pattern = rule.find("pattern"))
                if (!pattern_match(pattern->as_string(), text))
                    return false;
            if (rule.contains("format") && !date_time(text))
                return false;
        }
        if (value.is_number()) {
            const auto n = value.number_token();
            if (const auto *multiple = rule.find("multipleOf")) {
                if (!integer(n))
                    return false;
                const auto m = multiple->as_u64();
                if (m == 0 || m > 9007199254740991ULL)
                    throw std::logic_error("invalid frozen multipleOf");
                std::uint64_t remainder = 0;
                for (const char c : n)
                    if (c != '-')
                        remainder = (remainder * 10 + static_cast<unsigned>(c - '0')) % m;
                if (remainder != 0)
                    return false;
            }
            for (const auto name : {"minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum"})
                if (const auto *bound = rule.find(name)) {
                    if (!integer(n) || !integer(bound->number_token()))
                        return false;
                    const int comparison = integer_compare(n, bound->number_token());
                    const std::string_view key(name);
                    if ((key == "minimum" && comparison < 0) ||
                        (key == "maximum" && comparison > 0) ||
                        (key == "exclusiveMinimum" && comparison <= 0) ||
                        (key == "exclusiveMaximum" && comparison >= 0))
                        return false;
                }
        }
        if (value.is_array()) {
            if (!bound_size(rule, value.as_array().size(), "minItems", "maxItems"))
                return false;
            if (const auto *unique = rule.find("uniqueItems"); unique && unique->as_bool()) {
                std::set<std::string> keys;
                for (const auto &item : value.as_array())
                    if (!keys.insert(sorted_key(item)).second)
                        return false;
            }
            if (const auto *items = rule.find("items"))
                for (const auto &item : value.as_array())
                    if (!check(*items, item, depth + 1))
                        return false;
            if (const auto *contains = rule.find("contains"))
                if (std::none_of(
                        value.as_array().begin(), value.as_array().end(),
                        [&](const Json &item) { return check(*contains, item, depth + 1); }))
                    return false;
        }
        if (value.is_object()) {
            if (!bound_size(rule, value.as_object().size(), "minProperties", "maxProperties"))
                return false;
            if (const auto *required = rule.find("required"))
                for (const auto &key : required->as_array())
                    if (!value.contains(key.as_string()))
                        return false;
            const auto *properties = rule.find("properties");
            for (const auto &pair : value.as_object()) {
                if (const auto *names = rule.find("propertyNames"))
                    if (!check(*names, Json(pair.first), depth + 1))
                        return false;
                const auto *member = properties ? properties->find(pair.first) : nullptr;
                if (!member)
                    member = rule.find("additionalProperties");
                if (member && !check(*member, pair.second, depth + 1))
                    return false;
            }
        }
        return true;
    }
};
} // namespace

const std::vector<Operation> &all_operations() { return operation_table; }
const Operation *find_operation(std::string_view name) {
    const auto it =
        std::find_if(operation_table.begin(), operation_table.end(),
                     [name](const Operation &operation) { return operation.name == name; });
    return it == operation_table.end() ? nullptr : &*it;
}
Result<void> validate_wire(std::string_view family, std::string_view definition,
                           const Json &value) {
    try {
        const auto &available = schemas();
        const auto schema = std::find_if(available.begin(), available.end(),
                                         [family](const Schema &s) { return s.family == family; });
        if (schema == available.end())
            return Error{ErrorCode::invalid_input, "unsupported schema family"};
        const auto *definitions = schema->root.find("definitions");
        const auto *rule = definitions ? definitions->find(definition) : nullptr;
        if (!rule)
            return Error{ErrorCode::invalid_input, "unknown schema definition"};
        std::size_t nodes = 0;
        if (!tree_valid(value, 0, nodes))
            return Error{ErrorCode::contract, "wire JSON exceeds bounds or contains invalid text"};
        Checker checker{schema->root};
        if (!checker.check(*rule, value))
            return Error{ErrorCode::contract, "wire value does not match frozen schema"};
        return {};
    } catch (const std::exception &) {
        return Error{ErrorCode::contract, "frozen schema validation failed"};
    }
}
} // namespace tansr
