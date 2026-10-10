#pragma once
#include "tansr/crypto.hpp"
#include "tansr/executor.hpp"
#include "tansr/storage.hpp"

namespace tansr::memory_publication {
inline constexpr const char *tool_name = "TansrTerminalMemoryPublication";
inline constexpr const char *tool_digest =
    "8532a582d40d2d8993a80db59412a89671670ed7f994eaf9bf972bd544a111b0";
// owner 必须来自已验证的原操作；存储不产生授权、记忆或接管决定。
class Store {
  public:
    virtual ~Store() = default;
    virtual const Json &identity() const noexcept = 0;
    virtual bool atomic_durable_publication() const noexcept = 0;
    virtual bool encrypted_at_rest() const noexcept = 0;
    virtual Result<Json> execute(const Json &request, const Json &owner) = 0;
};
class Journal : public executor::Journal {
  public:
    virtual bool encrypted_at_rest() const noexcept = 0;
};
struct Limits {
    std::size_t max_transfers{4096};
    std::size_t max_staging_bytes{8U << 20};
    std::size_t max_journal_entries{8192};
    std::size_t max_file_bytes{64U << 20};
};
struct Options {
    std::filesystem::path path;
    bool create{false}; // 明确新建或重开；缺原件不自动建空域。
    Json identity;      // scope: app/user，sourceId/sourceGeneration/domainKey。
    std::string key_id;
    std::function<Result<crypto::Aes256Key>()> read_key;
    std::function<Result<executor::Scope>()> read_context;
    Limits limits;
    // 仅只读 query：宿主核原 Serve 恢复证明、旧授权撤销和当前连接许可。
    // 适配器先限制同 app/user/session/executor/workspace，再调用；不能用于写。
    std::function<Result<void>(const Json &original_owner, const Json &current_owner,
                               std::string_view transfer_id)>
        authorize_recovery;
    storage::CommitHook commit_hook;
};
// 仅覆盖此 publication 与共存 journal；不包含外部会话/预算/密钥。
struct MigrationReceipt {
    std::string format;
    Json identity;
    std::string source_sha256, destination_sha256;
    std::size_t source_bytes{0}, destination_bytes{0}, transfers{0}, journal_entries{0};
};
class FileStore final : public Store, public Journal {
  public:
    TANSR_API static Result<std::shared_ptr<FileStore>> open(Options);
    // 先停原 Runner。源必须 reopen，目标必须 create 且在另一私有目录。
    // 目标使用从未用于其他介质的新 key_id/密钥；完整快照首次原子发布。
    // 保留源原件和全部防重事实，不切换 Serve owner；成功返回前释放两端锁。
    TANSR_API static Result<MigrationReceipt> migrate(Options source, Options destination);
    TANSR_API ~FileStore() override;
    TANSR_API const Json &identity() const noexcept override;
    bool atomic_durable_publication() const noexcept override { return true; }
    bool encrypted_at_rest() const noexcept override { return true; }
    TANSR_API Result<Json> execute(const Json &, const Json &) override;
    TANSR_API Result<Json> capacity();
    TANSR_API Result<executor::ClaimResult> claim(const executor::Operation &) override;
    TANSR_API Result<void> complete(const executor::Operation &,
                                    const executor::Receipt &) override;
    TANSR_API Result<void> close();

  private:
    struct Impl;
    explicit FileStore(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
struct HostOptions {
    std::shared_ptr<Store> store;
    std::shared_ptr<Journal> journal;
    executor::Authorizer authorize; // 每次直接/Runner调用均重新验证原操作。
    bool require_encryption{true};
};
struct Host {
    std::map<std::string, executor::Tool> tools;
    std::shared_ptr<executor::Journal> journal;
};
TANSR_API Result<Host> create_host(HostOptions);
} // namespace tansr::memory_publication