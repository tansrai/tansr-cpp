#pragma once

#include "tansr/error.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace tansr::storage {
using AccessCheck = std::function<Result<void>()>;
enum class CommitStage { written, file_synced, before_replace, replaced, directory_synced };
using CommitHook = std::function<Result<void>(CommitStage)>;

// 只创建最后一级。已有目录只核验，不修改用户的权限或所有者。
TANSR_API Result<void> create_private_directory(const std::filesystem::path &path);
// 凭据等只读消费者不取得目录事务锁；仍核对私有路径、当前授权和对象身份。
TANSR_API Result<std::optional<std::string>>
read_private_file(const std::filesystem::path &path, std::size_t max_bytes, AccessCheck access);

// 持有目录身份和跨进程排他锁。路径必须绝对且无链接；叶文件名不能含路径。
// access 在打开及每次操作时检查当前宿主授权，不能回调本对象。
class PrivateDirectory {
  public:
    TANSR_API static Result<std::unique_ptr<PrivateDirectory>> open(std::filesystem::path path,
                                                                    AccessCheck access);
    TANSR_API ~PrivateDirectory();
    PrivateDirectory(const PrivateDirectory &) = delete;
    PrivateDirectory &operator=(const PrivateDirectory &) = delete;
    TANSR_API Result<void> check_access();
    TANSR_API Result<std::optional<std::string>> read(std::string_view leaf, std::size_t max_bytes);
    // 成功意味着文件与目录提交完成；替换后失回进入 uncertain，必须关闭后冷开。
    TANSR_API Result<void> write_atomic(std::string_view leaf, std::string_view bytes,
                                        bool replace = true, CommitHook hook = {});
    TANSR_API Result<void> remove(std::string_view leaf);

  private:
    friend TANSR_API Result<std::optional<std::string>>
    read_private_file(const std::filesystem::path &, std::size_t, AccessCheck);
    struct Impl;
    explicit PrivateDirectory(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
} // namespace tansr::storage
