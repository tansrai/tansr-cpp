#pragma once

#include <tansr/error.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tansr {

// 根值深度为零；对象键不计节点。限制在建立 AST 前执行。
struct JsonLimits {
    std::size_t max_bytes = 8 * 1024 * 1024;
    std::size_t max_depth = 32;
    std::size_t max_nodes = 100000;
};

struct NumberToken {
    std::string token;
};

TANSR_API bool valid_utf8(std::string_view text) noexcept;

// 自有值语义 AST：复制拥有独立存储，数字保持原词法，对象保持插入顺序。
class Json {
  public:
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;

    TANSR_API Json();
    TANSR_API Json(std::nullptr_t);
    TANSR_API Json(bool value);
    TANSR_API Json(const char *value);
    TANSR_API Json(std::string value);
    TANSR_API Json(std::string_view value);
    TANSR_API Json(int value);
    TANSR_API Json(std::int64_t value);
    TANSR_API Json(std::uint64_t value);
    explicit TANSR_API Json(Array value);
    explicit TANSR_API Json(Object value);
    TANSR_API Json(const Json &other);
    TANSR_API Json(Json &&other) noexcept;
    TANSR_API Json &operator=(const Json &other);
    TANSR_API Json &operator=(Json &&other) noexcept;
    TANSR_API ~Json();

    static TANSR_API Result<Json> parse(std::string_view text, JsonLimits limits = {});
    // 仅验证 JSON 数字文法，允许普通业务的负数、小数、指数和任意精度词法。
    static TANSR_API Result<Json> number(std::string_view token);
    static TANSR_API Json object(std::initializer_list<std::pair<std::string, Json>> values = {});
    static TANSR_API Json array(std::initializer_list<Json> values = {});

    // 再编码普通 JSON，不替代承诺摘要的原始网络字节或控制 canonical。
    TANSR_API std::string dump() const;
    TANSR_API bool is_null() const noexcept;
    TANSR_API bool is_bool() const noexcept;
    TANSR_API bool is_number() const noexcept;
    TANSR_API bool is_string() const noexcept;
    TANSR_API bool is_array() const noexcept;
    TANSR_API bool is_object() const noexcept;

    TANSR_API bool as_bool() const;
    TANSR_API const NumberToken &as_number() const;
    TANSR_API std::string_view number_token() const;
    TANSR_API const std::string &as_string() const;
    TANSR_API std::string &as_string();
    TANSR_API const Array &as_array() const;
    TANSR_API Array &as_array();
    TANSR_API const Object &as_object() const;
    TANSR_API Object &as_object();
    // 仅接受整数词法并检查目标范围；1.0/1e0 不会被静默转换为整数。
    TANSR_API std::uint64_t as_u64() const;
    TANSR_API std::int64_t as_i64() const;

    TANSR_API const Json *find(std::string_view key) const noexcept;
    TANSR_API Json *find(std::string_view key) noexcept;
    TANSR_API bool contains(std::string_view key) const noexcept;
    TANSR_API const Json &at(std::string_view key) const;
    TANSR_API Json &at(std::string_view key);
    TANSR_API const Json &at(std::size_t index) const;
    TANSR_API Json &at(std::size_t index);
    TANSR_API void set(std::string key, Json value);
    TANSR_API const Json &operator[](std::string_view key) const;
    TANSR_API Json &operator[](std::string_view key);
    TANSR_API const Json &operator[](std::size_t index) const;
    TANSR_API Json &operator[](std::size_t index);

  private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
    explicit Json(NumberToken value);
};

} // namespace tansr
