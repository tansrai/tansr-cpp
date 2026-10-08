#pragma once

#include "tansr/api.hpp"
#include "tansr/crypto.hpp"
#include "tansr/storage.hpp"
#include <map>

namespace tansr::archive {
inline constexpr const char *protocol = "sdk2-ext-v1";
using Bodies = std::map<std::string, std::string>;
using AccessCheck = std::function<Result<void>(const Json &identity)>;

struct StoreLimits {
    std::size_t max_records{4096};
    std::size_t max_artifacts{16384};
    std::size_t max_stored_bytes{64U << 20};
    std::size_t max_batch_bytes{8U << 20};
};
struct StoreOptions {
    std::filesystem::path path;
    crypto::Aes256Key key{};
    std::string key_id;
    Json identity;
    StoreLimits limits;
    AccessCheck check_access;
    // 一把密钥仅供一个档案使用。达到上限须显式轮换，不自动换密钥打开旧文件。
    std::uint64_t max_encryptions{1U << 20};
    storage::CommitHook commit_hook;
};
TANSR_API Result<Json> identity_from_binding(const Json &binding, const Json &status);

class ArchiveStore {
  public:
    virtual ~ArchiveStore() = default;
    virtual const Json &identity() const noexcept = 0;
    virtual StoreLimits limits() const noexcept = 0;
    virtual Result<void> check_access() = 0;
    virtual Result<std::optional<Json>> head() = 0;
    virtual Result<std::optional<Json>> coverage() = 0;
    virtual Result<std::optional<Json>> pending() = 0;
    virtual Result<std::optional<std::int64_t>> pending_deadline() = 0;
    virtual Result<Json> receive(const Json &binding, const Json &status, const Json &page,
                                 Bodies bodies, const Json &request, std::int64_t deadline_ms) = 0;
    virtual Result<void> confirm(const Json &receipt) = 0;
    virtual Result<std::vector<Json>> records_by_id(const std::vector<std::string> &ids) = 0;
    virtual Result<std::string> body(const Json &reference) = 0;
    virtual Result<std::optional<Json>> pending_rebase() = 0;
    virtual Result<Json> prepare_rebase(const Json &request, std::int64_t deadline_ms) = 0;
    virtual Result<void> confirm_rebase(const Json &result) = 0;
};

class FileStore final : public ArchiveStore {
  public:
    TANSR_API static Result<std::unique_ptr<FileStore>> open(StoreOptions);
    TANSR_API ~FileStore() override;
    TANSR_API const Json &identity() const noexcept override;
    TANSR_API StoreLimits limits() const noexcept override;
    TANSR_API Result<void> check_access() override;
    TANSR_API Result<std::optional<Json>> head() override;
    TANSR_API Result<std::optional<Json>> coverage() override;
    TANSR_API Result<std::optional<Json>> pending() override;
    TANSR_API Result<std::optional<std::int64_t>> pending_deadline() override;
    TANSR_API Result<Json> receive(const Json &, const Json &, const Json &, Bodies, const Json &,
                                   std::int64_t) override;
    TANSR_API Result<void> confirm(const Json &) override;
    TANSR_API Result<std::vector<Json>> records_by_id(const std::vector<std::string> &) override;
    TANSR_API Result<std::string> body(const Json &) override;
    TANSR_API Result<std::optional<Json>> pending_rebase() override;
    TANSR_API Result<Json> prepare_rebase(const Json &, std::int64_t) override;
    TANSR_API Result<void> confirm_rebase(const Json &) override;
    TANSR_API Result<void> rotate_key(crypto::Aes256Key key, std::string key_id);

  private:
    struct Impl;
    explicit FileStore(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

// 原意图包括精确正文与原绝对截止；落盘必须成功后才能发送变更。
class SavedIntent {
  public:
    TANSR_API static Result<SavedIntent> save(storage::PrivateDirectory &, std::string_view leaf,
                                              std::string kind, Json body,
                                              std::int64_t deadline_ms);
    TANSR_API static Result<SavedIntent> load(storage::PrivateDirectory &, std::string_view leaf);
    const Json &body() const noexcept { return body_; }
    const std::string &kind() const noexcept { return kind_; }
    std::int64_t deadline_ms() const noexcept { return deadline_ms_; }

  private:
    SavedIntent(std::string kind, Json body, std::int64_t deadline)
        : kind_(std::move(kind)), body_(std::move(body)), deadline_ms_(deadline) {}
    std::string kind_;
    Json body_;
    std::int64_t deadline_ms_{};
};

class ArchiveEventStream {
  public:
    TANSR_API ArchiveEventStream(EventStream stream, Json binding);
    TANSR_API Result<std::optional<SseEvent>> next(CancellationToken cancel = {});
    TANSR_API void cancel() noexcept;

  private:
    EventStream stream_;
    Json binding_;
};

class ArchiveClient {
  public:
    TANSR_API explicit ArchiveClient(std::shared_ptr<ApiClient> api);
    TANSR_API std::int64_t default_deadline_ms() const;
    TANSR_API Result<Json> capabilities(CallOptions context = {}) const;
    TANSR_API Result<Json> binding_target(std::string_view session, CallOptions context = {}) const;
    TANSR_API Result<Json> prepare_create(std::string_view session, std::string_view source,
                                          std::string_view request_id,
                                          CallOptions context = {}) const;
    TANSR_API Result<Json> create_binding(const SavedIntent &, CallOptions context = {}) const;
    TANSR_API Result<Json> close_binding(const Json &input, CallOptions context = {}) const;
    TANSR_API Result<Json> binding(std::string_view id, CallOptions context = {}) const;
    TANSR_API Result<Json> status(std::string_view id, CallOptions context = {}) const;
    TANSR_API Result<Json> records(const Json &binding, std::optional<std::string> after = {},
                                   CallOptions context = {}) const;
    TANSR_API Result<std::string> artifact(const Json &binding, const Json &reference,
                                           CallOptions context = {}) const;
    TANSR_API Result<Json> acknowledge(const Json &ack, CallOptions context = {}) const;
    TANSR_API Result<Json> operation(std::string_view binding, std::string_view operation,
                                     const Json &request, CallOptions context = {}) const;
    TANSR_API Result<Json> creation_operation(std::string_view session, const Json &request,
                                              CallOptions context = {}) const;
    TANSR_API Result<Json> rebase_acknowledgement(const Json &, CallOptions context = {}) const;
    TANSR_API Result<ArchiveEventStream> events(const Json &binding,
                                                std::optional<std::string> cursor = {},
                                                CallOptions context = {}) const;
    TANSR_API Result<Json> upload_material_chunk(const Json &, CallOptions context = {}) const;
    TANSR_API Result<Json> material_upload_status(std::string_view binding,
                                                  std::string_view request,
                                                  std::string_view artifact,
                                                  CallOptions context = {}) const;
    TANSR_API Result<Json> prepare_materials_before(ArchiveStore &, const Json &request,
                                                    const Json &identity,
                                                    std::int64_t original_deadline_ms,
                                                    CallOptions context = {}) const;
    TANSR_API Result<Json> submit_materials(const SavedIntent &, CallOptions context = {}) const;
    TANSR_API Result<Json> material_status(std::string_view binding, std::string_view request,
                                           CallOptions context = {}) const;

  private:
    CallOptions context(CallOptions) const;
    Result<Json> call(std::string_view operation, std::string_view definition, CallOptions context,
                      int status = 200) const;
    std::shared_ptr<ApiClient> api_;
};
struct SyncResult {
    std::size_t records{};
    bool complete{};
    bool recovered{};
    std::optional<Json> receipt;
};
TANSR_API Result<SyncResult> sync_once(const ArchiveClient &, ArchiveStore &,
                                       std::string_view request_id, CallOptions context = {});
TANSR_API Result<SyncResult> recover_pending(const ArchiveClient &, ArchiveStore &,
                                             std::string_view request_id, CallOptions context = {});
} // namespace tansr::archive
