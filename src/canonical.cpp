#include <tansr/canonical.hpp>
#include <tansr/crypto.hpp>

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <vector>

namespace tansr::canonical {
namespace {

struct Failure {
    Error error;
};
[[noreturn]] void fail(const char *code, std::size_t offset) {
    throw Failure{Error{ErrorCode::contract,
                        "JSON " + std::string(code) + " at byte " + std::to_string(offset)}};
}
bool valid_key(std::string_view key) {
    return !key.empty() && std::all_of(key.begin(), key.end(), [](unsigned char byte) {
        return byte >= 0x21 && byte <= 0x7e;
    });
}
void validate_number(std::string_view token, std::size_t offset) {
    if (token.empty() || (token != "0" && (token.front() < '1' || token.front() > '9')) ||
        !std::all_of(token.begin(), token.end(),
                     [](char byte) { return byte >= '0' && byte <= '9'; }))
        fail("invalid_number", offset);
    std::uint64_t value = 0;
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    if (token.size() > 16 || result.ec != std::errc{} ||
        result.ptr != token.data() + token.size() || value > MAX_SAFE_INTEGER)
        fail("unsafe_integer", offset);
}

class Writer {
  public:
    explicit Writer(std::size_t maximum) : maximum_(maximum) {}
    std::string run(const Json &input) {
        value(input, 0);
        return std::move(output_);
    }

  private:
    std::string output_;
    std::size_t maximum_, nodes_ = 0;
    void append(std::string_view bytes) {
        if (bytes.size() > maximum_ - output_.size())
            fail("bytes_exceeded", output_.size());
        output_.append(bytes);
    }
    void string(std::string_view text) {
        if (!valid_utf8(text))
            fail("invalid_utf8", output_.size());
        append("\"");
        constexpr char hex[] = "0123456789abcdef";
        std::size_t start = 0;
        for (std::size_t at = 0; at < text.size(); ++at) {
            const auto byte = static_cast<unsigned char>(text[at]);
            if (byte >= 0x20 && byte != '"' && byte != '\\')
                continue;
            append(text.substr(start, at - start));
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
            case '\t':
                append("\\t");
                break;
            case '\n':
                append("\\n");
                break;
            case '\f':
                append("\\f");
                break;
            case '\r':
                append("\\r");
                break;
            default: {
                const char escaped[] = {'\\', 'u', '0', '0', hex[byte >> 4], hex[byte & 15]};
                append(std::string_view(escaped, sizeof(escaped)));
            }
            }
            start = at + 1;
        }
        append(text.substr(start));
        append("\"");
    }
    void value(const Json &input, std::size_t depth) {
        if (depth > MAX_DEPTH)
            fail("depth_exceeded", output_.size());
        if (nodes_ == MAX_NODES)
            fail("nodes_exceeded", output_.size());
        ++nodes_;
        if (input.is_null())
            append("null");
        else if (input.is_bool())
            append(input.as_bool() ? "true" : "false");
        else if (input.is_number()) {
            validate_number(input.number_token(), output_.size());
            append(input.number_token());
        } else if (input.is_string())
            string(input.as_string());
        else if (input.is_array()) {
            const auto &items = input.as_array();
            if (items.size() > MAX_NODES - nodes_)
                fail("nodes_exceeded", output_.size());
            append("[");
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i != 0)
                    append(",");
                value(items[i], depth + 1);
            }
            append("]");
        } else {
            const auto &items = input.as_object();
            if (items.size() > MAX_NODES - nodes_)
                fail("nodes_exceeded", output_.size());
            std::vector<const Json::Object::value_type *> sorted;
            sorted.reserve(items.size());
            for (const auto &item : items) {
                if (!valid_key(item.first))
                    fail("invalid_key", output_.size());
                sorted.push_back(&item);
            }
            std::sort(sorted.begin(), sorted.end(), [](const auto *left, const auto *right) {
                return left->first < right->first;
            });
            append("{");
            for (std::size_t i = 0; i < sorted.size(); ++i) {
                if (i != 0) {
                    if (sorted[i - 1]->first == sorted[i]->first)
                        fail("duplicate_key", output_.size());
                    append(",");
                }
                string(sorted[i]->first);
                append(":");
                value(sorted[i]->second, depth + 1);
            }
            append("}");
        }
    }
};

} // namespace

Result<Json> decode(std::string_view bytes, std::size_t max_bytes) {
    auto decoded = Json::parse(bytes, JsonLimits{max_bytes, MAX_DEPTH, MAX_NODES});
    if (!decoded)
        return decoded.error();
    // owned AST 尚保有所有数字词法/键顺序；控制规则验证不借助有损 DOM。
    auto encoded = encode_limited(decoded.value(), max_bytes);
    if (!encoded)
        return encoded.error();
    return std::move(decoded).value();
}
Result<Json> parse_strict(std::string_view bytes, std::size_t max_bytes) {
    auto decoded = decode(bytes, max_bytes);
    if (!decoded)
        return decoded.error();
    auto encoded = encode_limited(decoded.value(), max_bytes);
    if (!encoded)
        return encoded.error();
    if (encoded.value() != bytes)
        return Error{ErrorCode::contract, "JSON not_canonical at byte 0"};
    return std::move(decoded).value();
}
Result<std::string> encode(const Json &value) { return encode_limited(value, DEFAULT_MAX_BYTES); }
Result<std::string> encode_limited(const Json &value, std::size_t max_bytes) {
    if (max_bytes == 0)
        return Error{ErrorCode::invalid_input, "JSON max_bytes must be positive"};
    try {
        return Writer(max_bytes).run(value);
    } catch (const Failure &failure) {
        return failure.error;
    }
}
Result<std::string> digest(std::string_view domain, const Json &value) {
    auto encoded = encode(value);
    if (!encoded)
        return encoded.error();
    return digest_bytes(domain, encoded.value());
}
Result<std::string> digest_bytes(std::string_view domain, std::string_view bytes) {
    if (domain.empty() || domain.find('\0') != std::string_view::npos || !valid_utf8(domain))
        return Error{ErrorCode::invalid_input,
                     "digest domain must be UTF-8, nonempty and without NUL"};
    auto state = crypto::Sha256::create();
    if (!state)
        return state.error();
    for (const auto segment : {domain, std::string_view("\0", 1), bytes}) {
        auto updated = state.value().update(segment);
        if (!updated)
            return updated.error();
    }
    return state.value().hex();
}
std::string encode_path_segment(std::string_view value) {
    if (!valid_utf8(value))
        throw std::invalid_argument("path segment invalid UTF-8");
    constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') ||
            std::string_view("-_.!~*'()").find(static_cast<char>(byte)) != std::string_view::npos)
            output.push_back(static_cast<char>(byte));
        else {
            output.push_back('%');
            output.push_back(hex[byte >> 4]);
            output.push_back(hex[byte & 15]);
        }
    }
    return output;
}

} // namespace tansr::canonical
