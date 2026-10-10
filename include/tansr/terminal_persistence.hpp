#pragma once
#include "tansr/memory_publication.hpp"

namespace tansr::terminal_persistence {
inline constexpr const char *tool_name = "TansrTerminalPersistenceV1";
// 由统一机器合同消费生成，不能用 RFC 文档摘要替代工具定义摘要。
#include "tansr/detail/terminal_persistence_digest.inc"
// 宿主提供原操作的当前授权/取消检查，贯穿每个介质边界。
using Guard = std::function<Result<void>()>;
class Store {
  public:
    virtual ~Store() = default;
    virtual const Json &identity() const noexcept = 0;
    virtual bool atomic_durable_publication() const noexcept = 0;
    virtual bool encrypted_at_rest() const noexcept = 0;
    virtual Result<Json> execute(const Json &, const Json &, Guard = {}) = 0;
};
using Journal = memory_publication::Journal;
using MigrationReceipt = memory_publication::MigrationReceipt;
using Host = memory_publication::Host;
struct HostOptions {
    std::shared_ptr<Store> store;
    std::shared_ptr<Journal> journal;
    executor::Authorizer authorize;
    bool require_encryption{true};
};
struct Limits {
    std::size_t active_transfers{32};
    std::size_t staging_bytes{8U << 20};
    std::size_t receipt_entries{4096};
    std::size_t transfer_facts{4096};
    std::size_t objects{4096};
    std::size_t retained_bytes{32U << 20};
    std::size_t max_journal_entries{8192};
    std::size_t max_file_bytes{64U << 20};
};
struct Options {
    std::filesystem::path path;
    bool create{false};
    Json identity;
    std::string key_id;
    std::function<Result<crypto::Aes256Key>()> read_key;
    std::function<Result<executor::Scope>()> read_context;
    Limits limits;
    // 精确原 owner 恢复证明只许可 query，不授予新 owner put/commit。
    std::function<Result<void>(const Json &, const Json &, std::string_view)> authorize_recovery;
    storage::CommitHook commit_hook;
};
// 独立 v1 布局；单一密文快照的原子替换共同提交根、索引、票据与日志。
// 每次写入/重开均有整快照成本，head 返回实际较低配额，不宣称常量 IO。
// 相邻 .writes 经 HMAC 认证并在每次 GCM 尝试前耐久扣额；关闭备份须成对保留。
class FileStore final : public Store, public Journal {
  public:
    TANSR_API static Result<std::shared_ptr<FileStore>> open(Options);
    // 新钥保源复制件持久只读：可核验原根/索引/票据，不接管执行，无激活开关。
    // 同文件 journal 仅可查询既有原键；新 claim 与 pending 的完成均拒写。
    TANSR_API static Result<MigrationReceipt> migrate(Options source, Options destination);
    TANSR_API ~FileStore() override;
    TANSR_API const Json &identity() const noexcept override;
    bool atomic_durable_publication() const noexcept override { return true; }
    bool encrypted_at_rest() const noexcept override { return true; }
    TANSR_API Result<Json> execute(const Json &, const Json &, Guard = {}) override;
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
TANSR_API Result<Host> create_host(HostOptions);
} // namespace tansr::terminal_persistence
