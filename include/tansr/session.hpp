#pragma once

#include "tansr/api.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tansr::session {

inline constexpr std::uint64_t max_safe_integer = 9007199254740991ULL;

// 对账时保留原键和绝对截止时间；不会自动重试副作用。
struct WriteOptions {
    std::optional<std::string> request_key;
    std::optional<std::int64_t> deadline_ms;
    CancellationToken cancel;
};
struct ResumeReference {
    std::string session_id;
};
struct ForkReference {
    std::string session_id;
    std::string checkpoint_id;
};
struct Budget {
    std::optional<double> max_usd;
    std::optional<std::uint64_t> max_tokens;
};
struct CreateOptions {
    std::optional<std::string> request_id, model, prompt, profile;
    std::optional<Budget> budget;
    std::optional<std::vector<std::string>> tools;
    std::optional<std::vector<Json>> client_tools;
    std::optional<std::string> capabilities_profile, cwd;
    std::optional<ResumeReference> resume;
    std::optional<ForkReference> fork;
    WriteOptions write;
};
struct Created {
    std::string session_id;
    bool resumed{false};
    std::uint64_t last_seq{0};
};
struct Meta {
    std::string session_id, status;
    bool live{false};
    std::uint64_t last_seq{0};
    Json raw;
};
struct SessionList {
    std::vector<Meta> sessions;
    std::uint64_t total{0};
};
// 接收成功不等于当前轮完成，也不证明模型已经消费输入。
struct Accepted {
    bool accepted{false};
    std::optional<std::string> session_id;
};
struct TextBlock {
    std::string text;
};
struct ImageBlock {
    std::string mime, data;
};
using Block = std::variant<TextBlock, ImageBlock>;
struct Answer {
    std::string question_id;
    std::vector<std::string> selected_option_ids;
    std::optional<std::string> free_text;
};
struct InputTarget {
    std::string history_epoch, turn_id;
};
struct InputContent {
    std::optional<std::string> text;
    std::optional<std::vector<Block>> blocks;
};
struct Input {
    std::string input_id;
    InputTarget target;
    InputContent content;
    std::optional<std::string> ack;
};
struct CapabilityClosure {
    std::string closure_id;
    std::map<std::string, std::string> operations;
    Json raw;
};
struct Checkpoint {
    std::string checkpoint_id, session_id;
    std::uint64_t message_count{0};
    Json raw;
};
struct LabeledCheckpoint {
    std::optional<std::string> label;
};
using CheckpointOption = std::variant<bool, LabeledCheckpoint>;
struct CompactOptions {
    std::optional<std::string> instructions;
    std::optional<CheckpointOption> checkpoint;
};
struct TranscriptionRequest {
    std::optional<std::string> model;
    std::string audio;
    std::optional<std::string> language;
    std::optional<bool> diarize;
    std::optional<std::string> prompt;
};
struct SpeechRequest {
    std::optional<std::string> model;
    std::string input;
    std::optional<std::string> voice, format;
    std::optional<double> speed;
};

class Session;
class SessionEventStream;
class SessionClient {
  public:
    TANSR_API static Result<SessionClient> create(std::shared_ptr<ApiClient>,
                                                  CancellationToken = {});
    TANSR_API Result<Session> create(CreateOptions = {}) const;
    TANSR_API Result<Session> attach(std::string session_id) const;
    TANSR_API Result<Session> resume(std::string session_id, WriteOptions = {}) const;
    TANSR_API Result<SessionList> list(std::uint64_t offset, std::uint64_t limit) const;
    TANSR_API const std::shared_ptr<ApiClient> &api() const noexcept;

  private:
    TANSR_API SessionClient(std::shared_ptr<ApiClient>, CancellationToken);
    TANSR_API Result<ApiResponse> call(std::string_view, CallOptions = {}) const;
    TANSR_API Result<void> discover_family(const WriteOptions &) const;
    std::shared_ptr<ApiClient> api_;
    CancellationToken cancel_;
    friend class Session;
};

class Session {
  public:
    TANSR_API const std::string &id() const noexcept;
    TANSR_API const Created &created() const noexcept;
    TANSR_API const std::shared_ptr<ApiClient> &api() const noexcept;
    TANSR_API Result<CapabilityClosure> capabilities() const;
    TANSR_API Result<Meta> meta() const;
    TANSR_API Result<Meta> application_prompt_meta() const;
    TANSR_API Result<Accepted> send(std::string prompt, WriteOptions = {}) const;
    TANSR_API Result<Accepted> send_blocks(std::vector<Block>, WriteOptions = {}) const;
    // 显式远端中断；流取消、析构和运行时关闭从不调用本方法。
    TANSR_API Result<Accepted> interrupt(WriteOptions = {}) const;
    TANSR_API Result<Accepted> close(WriteOptions = {}) const;
    TANSR_API Result<Accepted> permission(std::string ticket, std::string digest,
                                          std::string verdict, WriteOptions = {}) const;
    TANSR_API Result<Accepted> answer(std::string ticket, std::vector<Answer>,
                                      WriteOptions = {}) const;
    TANSR_API Result<Accepted> tool_result(std::string call_id, Json receipt,
                                           WriteOptions = {}) const;
    TANSR_API Result<Json> history(std::uint64_t offset, std::uint64_t limit) const;
    TANSR_API Result<Json> input_capabilities() const;
    TANSR_API Result<Json> submit_input(Input, WriteOptions = {}) const;
    TANSR_API Result<Json> input_status(std::string input_id, InputTarget) const;
    TANSR_API Result<Json> compact(CompactOptions = {}, WriteOptions = {}) const;
    TANSR_API Result<std::vector<Checkpoint>> checkpoints() const;
    TANSR_API Result<Checkpoint> checkpoint(std::string label = {}, WriteOptions = {}) const;
    TANSR_API Result<Json> restore(std::string checkpoint_id, bool checkpoint = true,
                                   WriteOptions = {}) const;
    TANSR_API Result<void> delete_checkpoint(std::string checkpoint_id, WriteOptions = {}) const;
    TANSR_API Result<std::string> export_checkpoint(std::string checkpoint_id) const;
    TANSR_API Result<Checkpoint> import_checkpoint(std::string bytes, std::string label = {},
                                                   WriteOptions = {}) const;
    TANSR_API Result<Json> set_cwd(std::string cwd, WriteOptions = {}) const;
    TANSR_API Result<Json> transcribe(TranscriptionRequest, WriteOptions = {}) const;
    TANSR_API Result<Json> speak(SpeechRequest, WriteOptions = {}) const;
    TANSR_API Result<SessionEventStream> events(std::optional<std::string> last_event_id = {},
                                                CancellationToken = {}) const;

  private:
    TANSR_API Session(SessionClient, Created);
    TANSR_API Result<CapabilityClosure> capabilities_for(const CallOptions &) const;
    TANSR_API Result<Meta> meta_query(std::map<std::string, std::string>) const;
    TANSR_API Result<ApiResponse> read(std::string_view,
                                       std::map<std::string, std::string> = {}) const;
    TANSR_API Result<ApiResponse>
        write(std::string_view, std::optional<Json>, WriteOptions,
              std::optional<std::pair<std::string, std::string>> = {}) const;
    TANSR_API Result<ApiResponse>
        write_call(std::string_view, CallOptions,
                   std::optional<std::pair<std::string, std::string>> = {}) const;
    TANSR_API Result<Accepted>
    accepted(std::string_view, Json, WriteOptions, bool,
             std::optional<std::pair<std::string, std::string>> = {}) const;
    TANSR_API Result<Checkpoint> read_checkpoint(Json) const;
    SessionClient client_;
    Created created_;
    friend class SessionClient;
};

// 完整七键信封保留原始 additive 事件。解码器须经统一 schema 校验。
enum class OutcomeStatus { completed, aborted, failed, session_ended, unknown };
struct Outcome {
    OutcomeStatus status;
    std::optional<std::string> turn_id, reason;
};
struct SessionEvent {
    Json envelope;
    TANSR_API std::string kind() const;
    TANSR_API const Json &raw() const;
    TANSR_API std::optional<Outcome> turn_outcome() const;
};
class TurnTracker {
  public:
    TANSR_API static Result<TurnTracker> create(std::uint64_t after_seq);
    TANSR_API static Result<TurnTracker> resume(std::uint64_t after_seq, std::string turn_id);
    TANSR_API static Result<TurnTracker> from_replay(std::uint64_t after_seq);
    TANSR_API const std::optional<std::string> &active_turn_id() const noexcept;
    TANSR_API bool needs_reconciliation() const noexcept;
    TANSR_API std::optional<Outcome> observe(const SessionEvent &);

  private:
    TANSR_API explicit TurnTracker(std::uint64_t);
    std::uint64_t after_seq_;
    std::optional<std::string> active_turn_id_;
    bool replaying_prefix_{false}, invalidated_{false}, finished_{false};
};

// 单消费者、无自动重连。游标只在交付时前移，持久化处理水位由宿主管理。
class SessionEventStream {
  public:
    TANSR_API ~SessionEventStream();
    TANSR_API SessionEventStream(SessionEventStream &&) noexcept;
    TANSR_API SessionEventStream &operator=(SessionEventStream &&) noexcept;
    SessionEventStream(const SessionEventStream &) = delete;
    SessionEventStream &operator=(const SessionEventStream &) = delete;
    TANSR_API Result<std::optional<SessionEvent>> next(CancellationToken = {});
    TANSR_API const std::optional<std::string> &last_event_id() const noexcept;
    TANSR_API void close() noexcept;
    TANSR_API void shutdown() noexcept;

  private:
    TANSR_API SessionEventStream(EventStream, std::string, std::optional<std::string>,
                                 CancellationSource, CancellationToken);
    TANSR_API Result<std::optional<SessionEvent>> decode(Json);
    std::optional<EventStream> inner_;
    std::string session_id_;
    std::optional<std::string> last_event_id_;
    std::optional<std::uint64_t> last_seq_;
    CancellationSource local_;
    CancellationToken cancel_;
    friend class Session;
};

} // namespace tansr::session
