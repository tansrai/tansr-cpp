#include <tansr/json.hpp>

#include <charconv>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <variant>

namespace tansr {
namespace {

std::size_t invalid_utf8_offset(std::string_view text) noexcept {
    for (std::size_t at = 0; at < text.size();) {
        const auto start = at;
        const auto first = static_cast<unsigned char>(text[at++]);
        if (first < 0x80)
            continue;
        std::size_t count = 0;
        std::uint32_t code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1;
            code = first & 0x1f;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2;
            code = first & 0x0f;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3;
            code = first & 0x07;
            minimum = 0x10000;
        } else
            return start;
        if (count > text.size() - at)
            return start;
        for (std::size_t i = 0; i < count; ++i) {
            const auto next = static_cast<unsigned char>(text[at++]);
            if ((next & 0xc0) != 0x80)
                return start;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return start;
    }
    return text.size();
}

bool digit(char value) noexcept { return value >= '0' && value <= '9'; }
bool whitespace(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

// 只检查 JSON 数字文法，不使用浮点转换；1e400 在普通 AST 中仍为合法词法。
std::size_t number_end(std::string_view text, std::size_t start) noexcept {
    auto at = start;
    if (at < text.size() && text[at] == '-')
        ++at;
    if (at == text.size())
        return start;
    if (text[at] == '0')
        ++at;
    else if (text[at] >= '1' && text[at] <= '9')
        while (at < text.size() && digit(text[at]))
            ++at;
    else
        return start;
    if (at < text.size() && text[at] == '.') {
        const auto begin = ++at;
        while (at < text.size() && digit(text[at]))
            ++at;
        if (at == begin)
            return start;
    }
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
        ++at;
        if (at < text.size() && (text[at] == '+' || text[at] == '-'))
            ++at;
        const auto begin = at;
        while (at < text.size() && digit(text[at]))
            ++at;
        if (at == begin)
            return start;
    }
    return at;
}

struct ParseFailure {
    Error error;
};
[[noreturn]] void fail(const char *code, std::size_t at) {
    // 固定诊断与偏移；不回显原正文、键名或凭据。
    throw ParseFailure{
        Error{ErrorCode::contract, "JSON " + std::string(code) + " at byte " + std::to_string(at)}};
}

void append_codepoint(std::string &output, std::uint32_t value) {
    if (value <= 0x7f)
        output.push_back(static_cast<char>(value));
    else if (value <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (value >> 18)));
        output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

class Parser {
  public:
    Parser(std::string_view bytes, JsonLimits limits) : bytes_(bytes), limits_(limits) {}
    Json parse() {
        auto result = value(0);
        space();
        if (at_ != bytes_.size())
            fail("trailing_data", at_);
        return result;
    }

  private:
    std::string_view bytes_;
    JsonLimits limits_;
    std::size_t at_ = 0, nodes_ = 0;
    void space() {
        while (at_ < bytes_.size() && whitespace(bytes_[at_]))
            ++at_;
    }
    bool take(char expected) {
        if (at_ == bytes_.size() || bytes_[at_] != expected)
            return false;
        ++at_;
        return true;
    }
    Json literal(std::string_view word, Json result) {
        if (bytes_.substr(at_, word.size()) != word)
            fail("invalid_literal", at_);
        at_ += word.size();
        return result;
    }
    std::uint32_t hex4() {
        if (bytes_.size() - at_ < 4)
            fail("invalid_unicode_escape", at_);
        std::uint32_t value = 0;
        for (unsigned int i = 0; i < 4; ++i) {
            const char next = bytes_[at_++];
            value <<= 4;
            if (next >= '0' && next <= '9')
                value |= static_cast<std::uint32_t>(next - '0');
            else if (next >= 'a' && next <= 'f')
                value |= static_cast<std::uint32_t>(next - 'a' + 10);
            else if (next >= 'A' && next <= 'F')
                value |= static_cast<std::uint32_t>(next - 'A' + 10);
            else
                fail("invalid_unicode_escape", at_ - 1);
        }
        return value;
    }
    std::string string() {
        const auto start = at_++;
        std::string output;
        while (at_ < bytes_.size()) {
            const char next = bytes_[at_++];
            if (next == '"')
                return output;
            if (static_cast<unsigned char>(next) < 0x20)
                fail("invalid_string", at_ - 1);
            if (next != '\\') {
                output.push_back(next);
                continue;
            }
            if (at_ == bytes_.size())
                fail("unterminated_string", start);
            switch (bytes_[at_++]) {
            case '"':
                output.push_back('"');
                break;
            case '\\':
                output.push_back('\\');
                break;
            case '/':
                output.push_back('/');
                break;
            case 'b':
                output.push_back('\b');
                break;
            case 'f':
                output.push_back('\f');
                break;
            case 'n':
                output.push_back('\n');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 't':
                output.push_back('\t');
                break;
            case 'u': {
                auto code = hex4();
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (!take('\\') || !take('u'))
                        fail("invalid_surrogate_pair", at_);
                    const auto low = hex4();
                    if (low < 0xdc00 || low > 0xdfff)
                        fail("invalid_surrogate_pair", at_ - 4);
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                } else if (code >= 0xdc00 && code <= 0xdfff)
                    fail("invalid_surrogate_pair", at_ - 4);
                append_codepoint(output, code);
                break;
            }
            default:
                fail("invalid_escape", at_ - 1);
            }
        }
        fail("unterminated_string", start);
    }
    Json number() {
        const auto start = at_, end = number_end(bytes_, at_);
        if (end == start || (end < bytes_.size() && !whitespace(bytes_[end]) &&
                             bytes_[end] != ',' && bytes_[end] != ']' && bytes_[end] != '}'))
            fail("invalid_number", start);
        at_ = end;
        auto result = Json::number(bytes_.substr(start, end - start));
        if (!result)
            fail("invalid_number", start);
        return std::move(result).value();
    }
    Json object(std::size_t depth) {
        ++at_;
        space();
        Json::Object output;
        std::unordered_set<std::string> keys;
        if (take('}'))
            return Json(std::move(output));
        for (;;) {
            space();
            if (at_ == bytes_.size() || bytes_[at_] != '"')
                fail("expected_key", at_);
            const auto key_offset = at_;
            auto key = string();
            if (!keys.insert(key).second)
                fail("duplicate_key", key_offset);
            space();
            if (!take(':'))
                fail("expected_colon", at_);
            output.emplace_back(std::move(key), value(depth + 1));
            space();
            if (take('}'))
                return Json(std::move(output));
            if (!take(','))
                fail("expected_object_separator", at_);
        }
    }
    Json array(std::size_t depth) {
        ++at_;
        space();
        Json::Array output;
        if (take(']'))
            return Json(std::move(output));
        for (;;) {
            output.push_back(value(depth + 1));
            space();
            if (take(']'))
                return Json(std::move(output));
            if (!take(','))
                fail("expected_array_separator", at_);
        }
    }
    Json value(std::size_t depth) {
        space();
        if (depth > limits_.max_depth)
            fail("depth_exceeded", at_);
        if (nodes_ == limits_.max_nodes)
            fail("nodes_exceeded", at_);
        ++nodes_;
        if (at_ == bytes_.size())
            fail("unexpected_end", at_);
        switch (bytes_[at_]) {
        case '{':
            return object(depth);
        case '[':
            return array(depth);
        case '"':
            return Json(string());
        case 't':
            return literal("true", Json(true));
        case 'f':
            return literal("false", Json(false));
        case 'n':
            return literal("null", Json());
        default:
            if (bytes_[at_] == '-' || digit(bytes_[at_]))
                return number();
            fail("unexpected_token", at_);
        }
    }
};

// 普通再编码保持键序和 NumberToken；手动变更造成的非法 AST 不静默修复。
class Dumper {
  public:
    std::string run(const Json &value) {
        write(value, 0);
        return std::move(output_);
    }

  private:
    std::string output_;
    std::size_t nodes_ = 0;
    void append(std::string_view text) {
        if (text.size() > JsonLimits{}.max_bytes - output_.size())
            throw std::length_error("JSON bytes_exceeded");
        output_.append(text);
    }
    void string(std::string_view text) {
        if (!valid_utf8(text))
            throw std::invalid_argument("JSON invalid_utf8");
        append("\"");
        constexpr char hex[] = "0123456789abcdef";
        for (const char byte : text) {
            switch (byte) {
            case '"':
                append("\\\"");
                break;
            case '\\':
                append("\\\\");
                break;
            case '\b':
                append("\\b");
                break;
            case '\f':
                append("\\f");
                break;
            case '\n':
                append("\\n");
                break;
            case '\r':
                append("\\r");
                break;
            case '\t':
                append("\\t");
                break;
            default: {
                const auto value = static_cast<unsigned char>(byte);
                if (value < 0x20) {
                    const char escaped[] = {'\\', 'u', '0', '0', hex[value >> 4], hex[value & 15]};
                    append(std::string_view(escaped, sizeof(escaped)));
                } else
                    append(std::string_view(&byte, 1));
            }
            }
        }
        append("\"");
    }
    void write(const Json &value, std::size_t depth) {
        if (depth > JsonLimits{}.max_depth)
            throw std::length_error("JSON depth_exceeded");
        if (++nodes_ > JsonLimits{}.max_nodes)
            throw std::length_error("JSON nodes_exceeded");
        if (value.is_null())
            append("null");
        else if (value.is_bool())
            append(value.as_bool() ? "true" : "false");
        else if (value.is_number())
            append(value.number_token());
        else if (value.is_string())
            string(value.as_string());
        else if (value.is_array()) {
            append("[");
            bool first = true;
            for (const auto &item : value.as_array()) {
                if (!first)
                    append(",");
                first = false;
                write(item, depth + 1);
            }
            append("]");
        } else {
            append("{");
            std::unordered_set<std::string_view> keys;
            bool first = true;
            for (const auto &item : value.as_object()) {
                if (!keys.insert(item.first).second)
                    throw std::invalid_argument("JSON duplicate_key");
                if (!first)
                    append(",");
                first = false;
                string(item.first);
                append(":");
                write(item.second, depth + 1);
            }
            append("}");
        }
    }
};

} // namespace

struct Json::Storage {
    using Value = std::variant<std::nullptr_t, bool, NumberToken, std::string, Array, Object>;
    Value value;
    template <class T> explicit Storage(T input) : value(std::move(input)) {}
};

bool valid_utf8(std::string_view text) noexcept { return invalid_utf8_offset(text) == text.size(); }
Json::Json() = default;
Json::Json(std::nullptr_t) : Json() {}
Json::Json(bool value) : storage_(std::make_unique<Storage>(value)) {}
Json::Json(const char *value)
    : Json(value ? std::string(value) : throw std::invalid_argument("JSON null string pointer")) {}
Json::Json(std::string value) : storage_(std::make_unique<Storage>(std::move(value))) {
    if (!valid_utf8(as_string()))
        throw std::invalid_argument("JSON invalid_utf8");
}
Json::Json(std::string_view value) : Json(std::string(value)) {}
Json::Json(int value) : Json(static_cast<std::int64_t>(value)) {}
Json::Json(std::int64_t value) : Json(NumberToken{std::to_string(value)}) {}
Json::Json(std::uint64_t value) : Json(NumberToken{std::to_string(value)}) {}
Json::Json(NumberToken value) : storage_(std::make_unique<Storage>(std::move(value))) {}
Json::Json(Array value) : storage_(std::make_unique<Storage>(std::move(value))) {}
Json::Json(Object value) : storage_(std::make_unique<Storage>(std::move(value))) {
    std::unordered_set<std::string_view> keys;
    for (const auto &item : as_object()) {
        if (!valid_utf8(item.first))
            throw std::invalid_argument("JSON invalid_utf8");
        if (!keys.insert(item.first).second)
            throw std::invalid_argument("JSON duplicate_key");
    }
}
Json::Json(const Json &other)
    : storage_(other.storage_ ? std::make_unique<Storage>(*other.storage_) : nullptr) {}
Json::Json(Json &&other) noexcept = default;
Json &Json::operator=(const Json &other) {
    if (this != &other)
        storage_ = other.storage_ ? std::make_unique<Storage>(*other.storage_) : nullptr;
    return *this;
}
Json &Json::operator=(Json &&other) noexcept = default;
Json::~Json() = default;
Result<Json> Json::parse(std::string_view text, JsonLimits limits) {
    if (limits.max_bytes == 0 || limits.max_nodes == 0)
        return Error{ErrorCode::invalid_input, "JSON byte and node limits must be positive"};
    try {
        if (text.size() > limits.max_bytes)
            fail("bytes_exceeded", 0);
        const auto invalid = invalid_utf8_offset(text);
        if (invalid != text.size())
            fail("invalid_utf8", invalid);
        return Parser(text, limits).parse();
    } catch (const ParseFailure &error) {
        return error.error;
    }
}
Result<Json> Json::number(std::string_view token) {
    if (token.empty() || number_end(token, 0) != token.size())
        return Error{ErrorCode::contract, "JSON invalid_number at byte 0"};
    return Json(NumberToken{std::string(token)});
}
Json Json::object(std::initializer_list<std::pair<std::string, Json>> values) {
    return Json(Object(values));
}
Json Json::array(std::initializer_list<Json> values) { return Json(Array(values)); }
std::string Json::dump() const { return Dumper{}.run(*this); }
bool Json::is_null() const noexcept {
    return !storage_ || std::holds_alternative<std::nullptr_t>(storage_->value);
}
bool Json::is_bool() const noexcept {
    return storage_ && std::holds_alternative<bool>(storage_->value);
}
bool Json::is_number() const noexcept {
    return storage_ && std::holds_alternative<NumberToken>(storage_->value);
}
bool Json::is_string() const noexcept {
    return storage_ && std::holds_alternative<std::string>(storage_->value);
}
bool Json::is_array() const noexcept {
    return storage_ && std::holds_alternative<Array>(storage_->value);
}
bool Json::is_object() const noexcept {
    return storage_ && std::holds_alternative<Object>(storage_->value);
}
bool Json::as_bool() const {
    if (!is_bool())
        throw std::invalid_argument("JSON type is not bool");
    return std::get<bool>(storage_->value);
}
const NumberToken &Json::as_number() const {
    if (!is_number())
        throw std::invalid_argument("JSON type is not number");
    return std::get<NumberToken>(storage_->value);
}
std::string_view Json::number_token() const { return as_number().token; }
const std::string &Json::as_string() const {
    if (!is_string())
        throw std::invalid_argument("JSON type is not string");
    return std::get<std::string>(storage_->value);
}
std::string &Json::as_string() {
    if (!is_string())
        throw std::invalid_argument("JSON type is not string");
    return std::get<std::string>(storage_->value);
}
const Json::Array &Json::as_array() const {
    if (!is_array())
        throw std::invalid_argument("JSON type is not array");
    return std::get<Array>(storage_->value);
}
Json::Array &Json::as_array() {
    if (!is_array())
        throw std::invalid_argument("JSON type is not array");
    return std::get<Array>(storage_->value);
}
const Json::Object &Json::as_object() const {
    if (!is_object())
        throw std::invalid_argument("JSON type is not object");
    return std::get<Object>(storage_->value);
}
Json::Object &Json::as_object() {
    if (!is_object())
        throw std::invalid_argument("JSON type is not object");
    return std::get<Object>(storage_->value);
}
std::uint64_t Json::as_u64() const {
    const auto text = number_token();
    if (text.empty() || text.front() == '-' ||
        text.find_first_not_of("0123456789") != std::string_view::npos)
        throw std::invalid_argument("JSON number is not an unsigned integer token");
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::out_of_range("JSON unsigned integer out of range");
    return result;
}
std::int64_t Json::as_i64() const {
    const auto text = number_token();
    const auto digits = text.substr(!text.empty() && text.front() == '-' ? 1 : 0);
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string_view::npos)
        throw std::invalid_argument("JSON number is not a signed integer token");
    std::int64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::out_of_range("JSON signed integer out of range");
    return result;
}
const Json *Json::find(std::string_view key) const noexcept {
    if (!is_object())
        return nullptr;
    for (const auto &item : std::get<Object>(storage_->value))
        if (item.first == key)
            return &item.second;
    return nullptr;
}
Json *Json::find(std::string_view key) noexcept {
    if (!is_object())
        return nullptr;
    for (auto &item : std::get<Object>(storage_->value))
        if (item.first == key)
            return &item.second;
    return nullptr;
}
bool Json::contains(std::string_view key) const noexcept { return find(key) != nullptr; }
const Json &Json::at(std::string_view key) const {
    const auto *result = find(key);
    if (!result)
        throw std::out_of_range("JSON key absent");
    return *result;
}
Json &Json::at(std::string_view key) {
    auto *result = find(key);
    if (!result)
        throw std::out_of_range("JSON key absent");
    return *result;
}
const Json &Json::at(std::size_t index) const { return as_array().at(index); }
Json &Json::at(std::size_t index) { return as_array().at(index); }
void Json::set(std::string key, Json value) {
    if (!valid_utf8(key))
        throw std::invalid_argument("JSON invalid_utf8");
    if (is_null())
        storage_ = std::make_unique<Storage>(Object{});
    if (!is_object())
        throw std::invalid_argument("JSON type is not object");
    if (auto *existing = find(key))
        *existing = std::move(value);
    else
        as_object().emplace_back(std::move(key), std::move(value));
}
const Json &Json::operator[](std::string_view key) const { return at(key); }
Json &Json::operator[](std::string_view key) {
    if (!contains(key))
        set(std::string(key), Json());
    return at(key);
}
const Json &Json::operator[](std::size_t index) const { return at(index); }
Json &Json::operator[](std::size_t index) { return at(index); }

} // namespace tansr
