#pragma once
#include "tansr/export.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace tansr {
namespace detail {
struct ReplayIdentity;
}
enum class ErrorCode {
    invalid_input,
    contract,
    network,
    tls,
    cancelled,
    timeout,
    http,
    not_found,
    permission,
    conflict,
    unknown,
    io,
    crypto,
    capacity,
    closed,
    reentrant,
    internal
};

// message 只包含固定诊断，不拼入令牌、URL 查询或服务端原始正文。
struct Error {
    ErrorCode code{ErrorCode::internal};
    std::string message;
    int http_status{0};
    std::string wire_code;
    std::string retry_action;
    std::string request_id;
    std::optional<std::uint64_t> retry_after_ms;
    // 原始诊断需宿主显式访问，不进入默认日志或 what()。
    std::string detail;
    Error() = default;
    Error(ErrorCode c, std::string text) : code(c), message(std::move(text)) {}

  private:
    std::shared_ptr<const detail::ReplayIdentity> replay_;
    friend class ApiClient;
};

template <class T> class Result {
  public:
    Result(T value) : data_(std::move(value)) {}
    Result(Error error) : data_(std::in_place_type<Error>) {
        // 显式建立已初始化的错误分支，再转移所有字段；保持原variant布局与异常语义。
        std::get<Error>(data_) = std::move(error);
    }
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(Error error) { return Result(std::move(error)); }
    bool has_value() const noexcept { return std::holds_alternative<T>(data_); }
    explicit operator bool() const noexcept { return has_value(); }
    T &value() & { return std::get<T>(data_); }
    const T &value() const & { return std::get<T>(data_); }
    T &&value() && { return std::get<T>(std::move(data_)); }
    Error &error() & { return std::get<Error>(data_); }
    const Error &error() const & { return std::get<Error>(data_); }

  private:
    std::variant<T, Error> data_;
};

template <> class Result<void> {
  public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}
    static Result success() { return {}; }
    static Result failure(Error error) { return Result(std::move(error)); }
    bool has_value() const noexcept { return !error_; }
    explicit operator bool() const noexcept { return has_value(); }
    void value() const {
        if (error_)
            throw std::logic_error("Result has an error");
    }
    Error &error() { return error_.value(); }
    const Error &error() const { return error_.value(); }

  private:
    std::optional<Error> error_;
};
} // namespace tansr
