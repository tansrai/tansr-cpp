#pragma once

#include "tansr/api.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace tansr::executor {
inline constexpr std::string_view protocol = "sdk2-ext-v1";
struct Scope {
    std::string application_scope_id, end_user_id, authorization_revision;
};
struct Platform {
    std::string platform, arch, language, runtime_version, adapter_version;
    TANSR_API static Platform current();
};
struct Workspace {
    std::string workspace_id, revision;
};
struct ToolDefinition {
    std::string name, definition_digest;
};
struct Interpreter {
    std::string id, revision, host_shell;
};
struct Registration {
    std::string executor_id;
    Platform platform = Platform::current();
    std::vector<Workspace> workspaces;
    std::vector<std::string> operations;
    std::vector<ToolDefinition> tools;
    std::optional<Interpreter> interpreter;
};
struct Connection {
    std::string executor_id, connection_id, connection_revision, expires_at;
    std::uint64_t heartbeat_after_ms{0};
};
struct Target {
    std::string executor_id, connection_id, connection_revision, workspace_id, workspace_revision;
    std::optional<Interpreter> interpreter;
};
struct Binding {
    std::string binding_id, revision;
    Target target;
};
struct EffectiveTool {
    std::string name, execution_kind;
    bool available{false};
    std::optional<std::string> unavailable_reason;
};
struct Capabilities {
    std::string session_id;
    std::optional<Platform> platform;
    std::string capability_revision;
    std::vector<EffectiveTool> effective_tools;
    std::optional<Binding> binding;
};
struct Resource {
    std::string operation;
    Json args = Json::object();
};
struct Operation {
    std::string operation_id, session_id;
    Scope scope;
    Binding binding;
    std::string tool_name;
    Resource request;
    std::string digest, expires_at;
};
struct Batch {
    std::string executor_id, connection_id;
    std::vector<Operation> operations;
};
struct Receipt {
    std::string executor_id, connection_id, operation_id, digest, status;
    std::optional<Resource> result;
    std::optional<std::string> error_code;
};
struct Status {
    Operation operation;
    std::string status;
    std::optional<Receipt> receipt;
};
struct TerminalSessionReference {
    std::string session_contract, session_id;
};
struct OutputOperationReference {
    std::string operation_id, request_digest;
};
struct OutputLimits {
    std::size_t max_control_bytes{0}, max_block_bytes{0}, max_batch_bytes{0}, max_pending_bytes{0},
        max_retained_bytes{0};
};
struct OutputSeal {
    std::optional<std::string> last_seq;
    std::string total_bytes, payload_digest;
    bool truncated{false};
};
struct OutputStatus {
    OutputOperationReference operation;
    std::string state;
    std::optional<std::string> accepted_through, durable_through, retained_from, next_byte_offset;
    std::optional<OutputSeal> seal;
};
struct TerminalOptions {
    std::string session_contract;
    OutputLimits limits;
};
struct OutputOptions {
    TerminalSessionReference session;
    OutputOperationReference operation;
    std::string executor_id, connection_id;
    OutputLimits limits;
    std::string encoding{"binary"};
    CancellationToken cancellation;
    std::optional<std::int64_t> deadline_ms;
};
struct OutputSnapshot {
    std::size_t pending_bytes{0}, pending_blocks{0};
    std::uint64_t captured_bytes{0}, dropped_bytes{0};
    bool truncated{false}, sealed{false}, failed{false};
};

// 控制合同、声明摘要和普通业务数字是三个不同的编码域。
TANSR_API Result<std::string> definition_digest(const Json &declaration);
TANSR_API Result<Json> parse_tool_arguments(std::string_view text);
TANSR_API Result<void> verify_tool_result(const Json &result);
TANSR_API Result<std::string> operation_digest(const Operation &operation);
TANSR_API Result<void> validate_operation(const Operation &operation);
TANSR_API Result<void> validate_receipt(const Operation &operation, const Receipt &receipt);
TANSR_API Json to_json(const Operation &operation);
TANSR_API Json to_json(const Receipt &receipt);

class Client {
  public:
    TANSR_API static Result<std::shared_ptr<Client>> create(std::shared_ptr<ApiClient>, Scope);
    TANSR_API Result<Connection> register_executor(const Registration &,
                                                   CancellationToken = {}) const;
    TANSR_API Result<Connection> heartbeat(const Connection &, CancellationToken = {}) const;
    TANSR_API Result<Batch> poll(const Connection &, CancellationToken = {}) const;
    TANSR_API Result<Capabilities> initialize(const std::string &session, const Platform &,
                                              std::optional<std::vector<std::string>> tools,
                                              const std::string &closure,
                                              CancellationToken = {}) const;
    TANSR_API Result<Capabilities> execution_capabilities(const std::string &session,
                                                          CancellationToken = {}) const;
    TANSR_API Result<Capabilities> bind(const std::string &session, const Connection &,
                                        const Workspace &, const std::string &revision,
                                        const std::string &closure, CancellationToken = {}) const;
    TANSR_API Result<TerminalOptions> negotiate_output(const TerminalSessionReference &,
                                                       const Binding &,
                                                       const std::string &request_id,
                                                       CancellationToken = {}) const;
    TANSR_API Result<Status> status(const std::string &session, const std::string &operation,
                                    CancellationToken = {},
                                    std::optional<std::int64_t> deadline = {}) const;
    TANSR_API Result<Status> executor_status(const TerminalSessionReference &, const Connection &,
                                             const Operation &, CancellationToken = {}) const;
    TANSR_API Result<Status> submit(const Operation &, const Receipt &,
                                    CancellationToken = {}) const;
    TANSR_API Result<void> check_identity(const Connection &, const Operation &) const;
    TANSR_API const Scope &scope() const noexcept;
    TANSR_API const std::shared_ptr<ApiClient> &api() const noexcept;

  private:
    Client(std::shared_ptr<ApiClient>, Scope);
    Result<void> validate_status(const Status &) const;
    std::shared_ptr<ApiClient> api_;
    Scope scope_;
};

class OutputWriter {
  public:
    TANSR_API static Result<std::shared_ptr<OutputWriter>> create(std::shared_ptr<ApiClient>,
                                                                  OutputOptions);
    TANSR_API ~OutputWriter();
    OutputWriter(const OutputWriter &) = delete;
    OutputWriter &operator=(const OutputWriter &) = delete;
    // 非阻塞采集，返回保留字节；满队列只截断连续前缀，之后继续排空丢弃。
    TANSR_API Result<std::size_t> capture(std::string_view channel, std::string_view bytes);
    TANSR_API OutputSnapshot snapshot() const;
    TANSR_API Result<OutputStatus> finish();
    TANSR_API void abort() noexcept;
    TANSR_API Result<void> shutdown();

  private:
    struct Impl;
    explicit OutputWriter(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

enum class ClaimState { claimed, pending, receipt };
struct ClaimResult {
    ClaimState state;
    std::optional<Receipt> receipt;
};
class Journal {
  public:
    virtual ~Journal() = default;
    virtual Result<ClaimResult> claim(const Operation &) = 0;
    virtual Result<void> complete(const Operation &, const Receipt &) = 0;
};
// 私有目录中的不可变明文事实账本；加密/备份政策属于宿主。
class FileJournal final : public Journal {
  public:
    TANSR_API static Result<std::shared_ptr<FileJournal>>
    open(const std::filesystem::path &, std::function<Result<void>()> access_check);
    TANSR_API ~FileJournal() override;
    TANSR_API Result<ClaimResult> claim(const Operation &) override;
    TANSR_API Result<void> complete(const Operation &, const Receipt &) override;

  private:
    struct Impl;
    explicit FileJournal(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

struct ToolContext {
    CancellationToken cancellation;
    std::shared_ptr<OutputWriter> output;
};
struct ToolFailure {
    enum class Kind { rejected, unknown } kind{Kind::unknown};
    std::string code;
    // rejected 仅供宿主能证明零副作用时使用。
    TANSR_API static ToolFailure rejected(std::string code);
    TANSR_API static ToolFailure unknown(std::string code = "execution_outcome_unknown");
};
using ToolResult = std::variant<Json, ToolFailure>;
using ToolHandler = std::function<ToolResult(ToolContext, Json)>;
struct Tool {
    std::string definition_digest;
    ToolHandler handler;
};
using Authorizer = std::function<Result<void>(const Operation &, CancellationToken)>;
struct OutputOutcome {
    bool sealed{false};
    std::optional<OutputStatus> last_known;
};
struct ExecutionOutcome {
    Receipt receipt;
    std::optional<OutputOutcome> output;
    bool output_confirmed() const noexcept { return !output || output->sealed; }
};
struct RunnerOptions {
    std::shared_ptr<Client> client;
    Registration registration;
    std::shared_ptr<Journal> journal;
    std::map<std::string, Tool> tools;
    Authorizer authorize;
    std::chrono::milliseconds poll_interval{100};
    std::optional<TerminalOptions> terminal;
    bool require_output{false}, restricted_status{false};
};
class Runner {
  public:
    TANSR_API static Result<std::shared_ptr<Runner>>
    create(RunnerOptions, std::optional<Connection> connection = {});
    TANSR_API ~Runner();
    TANSR_API Result<Connection> connect(CancellationToken = {});
    TANSR_API std::optional<Connection> connection() const;
    // 由宿主工作线程调用；run 自己续租，直接 execute 不延长原租约。
    TANSR_API Result<void> run(CancellationToken);
    TANSR_API Result<ExecutionOutcome> execute_with_output(Operation, CancellationToken = {});
    TANSR_API Result<Receipt> execute(Operation, CancellationToken = {});

  private:
    struct Impl;
    explicit Runner(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
} // namespace tansr::executor

namespace tansr {
using ExecutorClient = executor::Client;
using ExecutorRunner = executor::Runner;
} // namespace tansr
