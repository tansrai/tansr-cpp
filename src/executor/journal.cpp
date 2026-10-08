#include "internal.hpp"
#include "tansr/storage.hpp"
#include <mutex>

namespace tansr::executor {
struct FileJournal::Impl {
    std::unique_ptr<storage::PrivateDirectory> directory;
    std::mutex gate;
};
namespace {
thread_local const void *active_journal = nullptr;
struct JournalGuard {
    const void *previous = active_journal;
    explicit JournalGuard(const void *owner) { active_journal = owner; }
    ~JournalGuard() { active_journal = previous; }
};
Result<std::string> journal_key(const Operation &op) {
    auto valid = validate_operation(op);
    if (!valid)
        return valid.error();
    auto encoded =
        canonical::encode(Json::array({op.scope.application_scope_id, op.scope.end_user_id,
                                       op.binding.target.executor_id, op.operation_id}));
    if (!encoded)
        return encoded.error();
    return crypto::sha256_hex(encoded.value());
}
Result<std::optional<Json>> read_entry(storage::PrivateDirectory &dir, const std::string &name) {
    auto bytes = dir.read(name, detail::control_bytes);
    if (!bytes)
        return bytes.error();
    if (!bytes.value())
        return std::optional<Json>{};
    auto parsed = canonical::parse_strict(*bytes.value(), detail::control_bytes);
    if (!parsed)
        return detail::unknown("journal entry corrupt or incomplete");
    return std::optional<Json>(std::move(parsed.value()));
}
} // namespace
FileJournal::FileJournal(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FileJournal::~FileJournal() = default;
Result<std::shared_ptr<FileJournal>> FileJournal::open(const std::filesystem::path &path,
                                                       std::function<Result<void>()> access) {
    if (!access)
        return Error{ErrorCode::permission, "journal current authorization callback is required"};
    if (!path.is_absolute())
        return detail::invalid("journal requires absolute directory");
    auto created = storage::create_private_directory(path);
    if (!created)
        return created.error();
    auto dir = storage::PrivateDirectory::open(path, std::move(access));
    if (!dir)
        return dir.error();
    auto impl = std::make_unique<Impl>();
    impl->directory = std::move(dir.value());
    return std::shared_ptr<FileJournal>(new FileJournal(std::move(impl)));
}
Result<ClaimResult> FileJournal::claim(const Operation &op) {
    if (active_journal == impl_.get())
        return Error{ErrorCode::reentrant, "journal access callback cannot reenter journal"};
    JournalGuard guard(impl_.get());
    auto key = journal_key(op);
    if (!key)
        return key.error();
    std::lock_guard<std::mutex> lock(impl_->gate);
    const auto name = key.value() + ".claim";
    auto expected = Json::object({{"digest", op.digest}});
    auto encoded = canonical::encode(expected);
    if (!encoded)
        return encoded.error();
    auto existing = read_entry(*impl_->directory, name);
    if (!existing)
        return existing.error();
    if (!existing.value()) {
        auto orphan = read_entry(*impl_->directory, key.value() + ".receipt");
        if (!orphan)
            return orphan.error();
        if (orphan.value())
            return detail::unknown("journal receipt without claim; original fact retained");
        auto write = impl_->directory->write_atomic(name, encoded.value(), false);
        // write/flush/sync 失回不能被本次调用解释为“可以执行”。保留已有事实。
        if (!write)
            return write.error();
        return ClaimResult{ClaimState::claimed, {}};
    }
    if (!detail::equal(*existing.value(), expected))
        return detail::invalid("journal digest conflict");
    auto stored = read_entry(*impl_->directory, key.value() + ".receipt");
    if (!stored)
        return stored.error();
    if (!stored.value())
        return ClaimResult{ClaimState::pending, {}};
    auto r = detail::receipt(*stored.value());
    if (!r)
        return r.error();
    auto valid = validate_receipt(op, r.value());
    if (!valid)
        return valid.error();
    return ClaimResult{ClaimState::receipt, std::move(r.value())};
}
Result<void> FileJournal::complete(const Operation &op, const Receipt &r) {
    if (active_journal == impl_.get())
        return Error{ErrorCode::reentrant, "journal access callback cannot reenter journal"};
    JournalGuard guard(impl_.get());
    auto key = journal_key(op);
    if (!key)
        return key.error();
    auto valid = validate_receipt(op, r);
    if (!valid)
        return valid;
    std::lock_guard<std::mutex> lock(impl_->gate);
    auto claim = read_entry(*impl_->directory, key.value() + ".claim");
    if (!claim)
        return claim.error();
    if (!claim.value() || !detail::equal(*claim.value(), Json::object({{"digest", op.digest}})))
        return detail::invalid("journal missing or conflicting claim");
    auto v = to_json(r);
    auto bytes = canonical::encode_limited(v, detail::control_bytes);
    if (!bytes)
        return bytes.error();
    auto old = read_entry(*impl_->directory, key.value() + ".receipt");
    if (!old)
        return old.error();
    if (old.value()) {
        if (!detail::equal(*old.value(), v))
            return detail::invalid("journal receipt conflict");
        return {};
    }
    return impl_->directory->write_atomic(key.value() + ".receipt", bytes.value(), false);
}
} // namespace tansr::executor
