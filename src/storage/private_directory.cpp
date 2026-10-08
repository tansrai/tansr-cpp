#include "tansr/crypto.hpp"
#include "tansr/storage.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <aclapi.h>
#include <sddl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tansr::storage {
namespace {
Error io() { return {ErrorCode::io, "private storage operation failed"}; }
Error unsafe() { return {ErrorCode::permission, "private storage ownership or path rejected"}; }
bool leaf_ok(std::string_view name) {
    return !name.empty() && name != "." && name != ".." && name.size() <= 200 &&
           name.find_first_of("/\\:\0", 0, 4) == std::string_view::npos && name.back() != '.' &&
           name.back() != ' ';
}
bool path_ok(const std::filesystem::path &path) {
    if (!path.is_absolute() || path.filename().empty())
        return false;
    for (const auto &part : path)
        if (part == "." || part == "..")
            return false;
    return true;
}
Result<void> check_callback(const AccessCheck &access) {
    if (!access)
        return unsafe();
    try {
        return access();
    } catch (...) {
        return unsafe();
    }
}
#ifdef _WIN32
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&h) noexcept : value(h.value) { h.value = INVALID_HANDLE_VALUE; }
    Handle &operator=(Handle &&h) noexcept {
        if (this != &h) {
            close();
            value = h.value;
            h.value = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    ~Handle() { close(); }
    void close() {
        if (value != INVALID_HANDLE_VALUE && value != nullptr)
            CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
    explicit operator bool() const { return value != INVALID_HANDLE_VALUE && value != nullptr; }
};
struct Local {
    void *value{};
    ~Local() {
        if (value)
            LocalFree(value);
    }
};
Result<std::vector<unsigned char>> current_user() {
    HANDLE raw{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return io();
    Handle token(raw);
    DWORD size{};
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    if (!size)
        return io();
    std::vector<unsigned char> bytes(size);
    if (!GetTokenInformation(token.value, TokenUser, bytes.data(), size, &size))
        return io();
    return bytes;
}
Result<void> private_acl(HANDLE h) {
    PSID owner{};
    PACL acl{};
    PSECURITY_DESCRIPTOR sd{};
    if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                        &owner, nullptr, &acl, nullptr, &sd) != ERROR_SUCCESS)
        return io();
    Local guard{sd};
    auto user = current_user();
    if (!user)
        return user.error();
    auto sid = reinterpret_cast<TOKEN_USER *>(user.value().data())->User.Sid;
    if (!owner || !acl || !EqualSid(owner, sid))
        return unsafe();
    alignas(DWORD) unsigned char rights[] = {1, 1, 0, 0, 0, 0, 0, 3, 4, 0, 0, 0};
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void *raw{};
        if (!GetAce(acl, i, &raw))
            return io();
        auto ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);
        if (ace->Header.AceType == ACCESS_DENIED_ACE_TYPE)
            continue;
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE)
            return unsafe();
        auto allowed = static_cast<PSID>(&ace->SidStart);
        if (!EqualSid(allowed, sid) && !EqualSid(allowed, rights))
            return unsafe();
    }
    return {};
}
Result<void> check_handle(HANDLE h, bool directory, bool check_acl) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info))
        return io();
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        (((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory) ||
        (!directory && info.nNumberOfLinks != 1))
        return unsafe();
    return check_acl ? private_acl(h) : Result<void>{};
}
Result<void> descriptor(Local &sd, bool directory) {
    auto user = current_user();
    if (!user)
        return user.error();
    LPWSTR text{};
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(user.value().data())->User.Sid,
                                &text))
        return io();
    Local owner{text};
    std::wstring sddl =
        L"O:" + std::wstring(text) + L"D:P(A;" + (directory ? L"OICI" : L"") + L";FA;;;OW)";
    PSECURITY_DESCRIPTOR raw{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &raw,
                                                              nullptr))
        return io();
    sd.value = raw;
    return {};
}
Result<std::vector<Handle>> ancestors(const std::filesystem::path &path) {
    std::vector<Handle> held;
    auto current = path.root_path();
    for (const auto &part : path.relative_path()) {
        current /= part;
        Handle h(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!h)
            return io();
        auto checked = check_handle(h.value, true, current == path);
        if (!checked)
            return checked.error();
        held.push_back(std::move(h));
    }
    return held;
}
Result<Handle> open_file(const std::filesystem::path &path, DWORD disposition,
                         bool writable = true) {
    Local sd;
    auto made = descriptor(sd, false);
    if (!made)
        return made.error();
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), sd.value, FALSE};
    Handle h(CreateFileW(path.c_str(), GENERIC_READ | (writable ? GENERIC_WRITE : 0) | READ_CONTROL,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, disposition,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!h)
        return io();
    auto checked = check_handle(h.value, false, true);
    if (!checked)
        return checked.error();
    return h;
}
Result<std::pair<std::uint64_t, std::uint64_t>> file_identity(const Handle &handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle.value, &info))
        return io();
    return std::make_pair(static_cast<std::uint64_t>(info.dwVolumeSerialNumber),
                          (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) |
                              info.nFileIndexLow);
}
#else
struct Handle {
    int value{-1};
    Handle() = default;
    explicit Handle(int fd) : value(fd) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&h) noexcept : value(h.value) { h.value = -1; }
    Handle &operator=(Handle &&h) noexcept {
        if (this != &h) {
            close();
            value = h.value;
            h.value = -1;
        }
        return *this;
    }
    ~Handle() { close(); }
    void close() {
        if (value >= 0)
            ::close(value);
        value = -1;
    }
    explicit operator bool() const { return value >= 0; }
};
Result<void> check_handle(int fd, bool directory, bool check_owner) {
    struct stat st{};
    if (fstat(fd, &st))
        return io();
    if ((directory ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) ||
        (!directory && st.st_nlink != 1))
        return unsafe();
    if (check_owner && (st.st_uid != geteuid() || (st.st_mode & 0077) != 0))
        return unsafe();
    return {};
}
Result<Handle> open_directory(const std::filesystem::path &path) {
    Handle current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!current)
        return io();
    for (const auto &part : path.relative_path()) {
        Handle next(
            ::openat(current.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (!next)
            return io();
        current = std::move(next);
    }
    auto checked = check_handle(current.value, true, true);
    if (!checked)
        return checked.error();
    return current;
}
Result<Handle> open_file(int directory, const std::string &name, int flags, bool writable = true) {
    Handle h(::openat(directory, name.c_str(),
                      (writable ? O_RDWR : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC | flags, 0600));
    if (!h)
        return io();
    auto checked = check_handle(h.value, false, true);
    if (!checked)
        return checked.error();
    return h;
}
Result<std::pair<std::uint64_t, std::uint64_t>> file_identity(const Handle &handle) {
    struct stat info{};
    if (::fstat(handle.value, &info))
        return io();
    return std::make_pair(static_cast<std::uint64_t>(info.st_dev),
                          static_cast<std::uint64_t>(info.st_ino));
}
#endif
} // namespace

struct PrivateDirectory::Impl {
    std::filesystem::path path;
    AccessCheck access;
    std::recursive_mutex mutex;
    bool entered{false};
    bool uncertain{false};
    Handle lock;
#ifdef _WIN32
    std::vector<Handle> parents;
#else
    Handle directory;
#endif
    Result<void> verify() {
        if (uncertain)
            return Error{ErrorCode::unknown, "storage commit uncertain; close and reopen"};
        auto permitted = check_callback(access);
        if (!permitted)
            return permitted;
#ifdef _WIN32
        auto now = ancestors(path);
        if (!now)
            return now.error();
        BY_HANDLE_FILE_INFORMATION old_info{}, new_info{};
        if (!GetFileInformationByHandle(parents.back().value, &old_info) ||
            !GetFileInformationByHandle(now.value().back().value, &new_info))
            return io();
        if (old_info.dwVolumeSerialNumber != new_info.dwVolumeSerialNumber ||
            old_info.nFileIndexHigh != new_info.nFileIndexHigh ||
            old_info.nFileIndexLow != new_info.nFileIndexLow)
            return unsafe();
#else
        auto now = open_directory(path);
        if (!now)
            return now.error();
        struct stat old_info{}, new_info{};
        if (fstat(directory.value, &old_info) || fstat(now.value().value, &new_info))
            return io();
        if (old_info.st_dev != new_info.st_dev || old_info.st_ino != new_info.st_ino)
            return unsafe();
#endif
        return {};
    }
};
namespace {
struct Entry {
    bool &flag;
    explicit Entry(bool &value) : flag(value) { flag = true; }
    ~Entry() { flag = false; }
};
Result<void> hook_call(const CommitHook &hook, CommitStage stage) {
    if (!hook)
        return {};
    try {
        return hook(stage);
    } catch (...) {
        return io();
    }
}
} // namespace

Result<void> create_private_directory(const std::filesystem::path &path) {
    if (!path_ok(path))
        return unsafe();
#ifdef _WIN32
    // 父目录只拒绝链接；用户临时根本身不必采用档案的私有 ACL。
    auto parent = path.parent_path();
    auto current = parent.root_path();
    std::vector<Handle> held;
    for (const auto &part : parent.relative_path()) {
        current /= part;
        Handle h(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!h)
            return io();
        auto checked = check_handle(h.value, true, false);
        if (!checked)
            return checked;
        held.push_back(std::move(h));
    }
    Local sd;
    auto made = descriptor(sd, true);
    if (!made)
        return made;
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), sd.value, FALSE};
    if (!CreateDirectoryW(path.c_str(), &sa) && GetLastError() != ERROR_ALREADY_EXISTS)
        return io();
    auto checked = ancestors(path);
    if (!checked)
        return checked.error();
#else
    Handle parent(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!parent)
        return io();
    for (const auto &part : path.parent_path().relative_path()) {
        Handle next(
            ::openat(parent.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (!next)
            return io();
        parent = std::move(next);
    }
    if (::mkdirat(parent.value, path.filename().c_str(), 0700) && errno != EEXIST)
        return io();
    auto checked = open_directory(path);
    if (!checked)
        return checked.error();
    if (::fsync(parent.value))
        return io();
#endif
    return {};
}

PrivateDirectory::PrivateDirectory(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PrivateDirectory::~PrivateDirectory() = default;
Result<std::optional<std::string>> read_private_file(const std::filesystem::path &path,
                                                     std::size_t max_bytes, AccessCheck access) {
    if (!path_ok(path) || !path_ok(path.parent_path()))
        return unsafe();
    auto permitted = check_callback(access);
    if (!permitted)
        return permitted.error();
    auto impl = std::make_unique<PrivateDirectory::Impl>();
    impl->path = path.parent_path();
    impl->access = std::move(access);
#ifdef _WIN32
    auto parents = ancestors(impl->path);
    if (!parents)
        return parents.error();
    impl->parents = std::move(parents.value());
#else
    auto directory = open_directory(impl->path);
    if (!directory)
        return directory.error();
    impl->directory = std::move(directory.value());
#endif
    PrivateDirectory reader(std::move(impl));
    return reader.read(path.filename().u8string(), max_bytes);
}
Result<std::unique_ptr<PrivateDirectory>> PrivateDirectory::open(std::filesystem::path path,
                                                                 AccessCheck access) {
    if (!path_ok(path))
        return unsafe();
    auto permitted = check_callback(access);
    if (!permitted)
        return permitted.error();
    auto impl = std::make_unique<Impl>();
    impl->path = std::move(path);
    impl->access = std::move(access);
#ifdef _WIN32
    auto parents = ancestors(impl->path);
    if (!parents)
        return parents.error();
    impl->parents = std::move(parents.value());
    auto lock = open_file(impl->path / L".tansr.lock", OPEN_ALWAYS);
    if (!lock)
        return lock.error();
    OVERLAPPED overlapped{};
    if (!LockFileEx(lock.value().value, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                    MAXDWORD, MAXDWORD, &overlapped))
        return Error{ErrorCode::conflict, "storage already locked"};
#else
    auto directory = open_directory(impl->path);
    if (!directory)
        return directory.error();
    impl->directory = std::move(directory.value());
    auto lock = open_file(impl->directory.value, ".tansr.lock", O_CREAT);
    if (!lock)
        return lock.error();
    if (::flock(lock.value().value, LOCK_EX | LOCK_NB))
        return Error{ErrorCode::conflict, "storage already locked"};
#endif
    impl->lock = std::move(lock.value());
    return std::unique_ptr<PrivateDirectory>(new PrivateDirectory(std::move(impl)));
}
Result<void> PrivateDirectory::check_access() {
    std::lock_guard<std::recursive_mutex> held(impl_->mutex);
    if (impl_->entered)
        return Error{ErrorCode::reentrant, "storage callback reentry rejected"};
    Entry entry(impl_->entered);
    return impl_->verify();
}
Result<std::optional<std::string>> PrivateDirectory::read(std::string_view leaf,
                                                          std::size_t max_bytes) {
    if (!leaf_ok(leaf) || leaf == ".tansr.lock")
        return unsafe();
    std::lock_guard<std::recursive_mutex> held(impl_->mutex);
    if (impl_->entered)
        return Error{ErrorCode::reentrant, "storage callback reentry rejected"};
    Entry entry(impl_->entered);
    auto verified = impl_->verify();
    if (!verified)
        return verified.error();
    std::string name(leaf);
#ifdef _WIN32
    auto path = impl_->path / std::filesystem::u8path(name);
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetLastError() == ERROR_FILE_NOT_FOUND)
        return std::optional<std::string>{};
    auto opened = open_file(path, OPEN_EXISTING, false);
    if (!opened)
        return opened.error();
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(opened.value().value, &length) || length.QuadPart < 0)
        return io();
    if (static_cast<std::uint64_t>(length.QuadPart) > max_bytes)
        return Error{ErrorCode::capacity, "storage read limit exceeded"};
    std::string bytes(static_cast<std::size_t>(length.QuadPart), '\0');
    std::size_t offset{};
    while (offset < bytes.size()) {
        DWORD read{};
        auto count = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1U << 20));
        if (!ReadFile(opened.value().value, bytes.data() + offset, count, &read, nullptr) || !read)
            return io();
        offset += read;
    }
#else
    struct stat found{};
    if (::fstatat(impl_->directory.value, name.c_str(), &found, AT_SYMLINK_NOFOLLOW) &&
        errno == ENOENT)
        return std::optional<std::string>{};
    auto opened = open_file(impl_->directory.value, name, 0, false);
    if (!opened)
        return opened.error();
    struct stat length{};
    if (::fstat(opened.value().value, &length) || length.st_size < 0)
        return io();
    if (static_cast<std::uint64_t>(length.st_size) > max_bytes)
        return Error{ErrorCode::capacity, "storage read limit exceeded"};
    std::string bytes(static_cast<std::size_t>(length.st_size), '\0');
    std::size_t offset{};
    while (offset < bytes.size()) {
        auto count = ::read(opened.value().value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return io();
        offset += static_cast<std::size_t>(count);
    }
#endif
    auto checked = check_handle(opened.value().value, false, true);
    if (!checked)
        return checked.error();
    verified = impl_->verify();
    if (!verified)
        return verified.error();
    return std::optional<std::string>{std::move(bytes)};
}
Result<void> PrivateDirectory::write_atomic(std::string_view leaf, std::string_view bytes,
                                            bool replace, CommitHook hook) {
    if (!leaf_ok(leaf) || leaf == ".tansr.lock")
        return unsafe();
    std::lock_guard<std::recursive_mutex> held(impl_->mutex);
    if (impl_->entered)
        return Error{ErrorCode::reentrant, "storage callback reentry rejected"};
    Entry entry(impl_->entered);
    auto verified = impl_->verify();
    if (!verified)
        return verified;
    auto random = crypto::random_bytes(16);
    if (!random)
        return random.error();
    static constexpr char hex[] = "0123456789abcdef";
    std::string temp = ".tansr-tmp-";
    for (auto byte : random.value()) {
        temp += hex[byte >> 4];
        temp += hex[byte & 15];
    }
    const std::string name(leaf);
#ifdef _WIN32
    const auto target = impl_->path / std::filesystem::u8path(name);
    const auto temporary = impl_->path / std::filesystem::u8path(temp);
    const auto attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (!replace)
            return Error{ErrorCode::conflict, "storage target already exists"};
        auto checked = open_file(target, OPEN_EXISTING);
        if (!checked)
            return checked.error();
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND)
        return io();
    auto file = open_file(temporary, CREATE_NEW);
    if (!file)
        return file.error();
#else
    struct stat target{};
    if (::fstatat(impl_->directory.value, name.c_str(), &target, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!replace)
            return Error{ErrorCode::conflict, "storage target already exists"};
        auto checked = open_file(impl_->directory.value, name, 0);
        if (!checked)
            return checked.error();
    } else if (errno != ENOENT)
        return io();
    auto file = open_file(impl_->directory.value, temp, O_CREAT | O_EXCL);
    if (!file)
        return file.error();
#endif
    auto cleanup = [&] {
        file.value().close();
#ifdef _WIN32
        DeleteFileW(temporary.c_str());
#else
        ::unlinkat(impl_->directory.value, temp.c_str(), 0);
#endif
    };
    const auto expected_file = file_identity(file.value());
    if (!expected_file) {
        cleanup();
        return expected_file.error();
    }
    auto verify_leaf = [&](bool pending) -> Result<void> {
#ifdef _WIN32
        auto current = open_file(pending ? temporary : target, OPEN_EXISTING, false);
#else
        auto current = open_file(impl_->directory.value, pending ? temp : name, 0, false);
#endif
        if (!current)
            return current.error();
        auto identity = file_identity(current.value());
        if (!identity)
            return identity.error();
        if (identity.value() != expected_file.value())
            return unsafe();
        return {};
    };
    std::size_t offset{};
    while (offset < bytes.size()) {
#ifdef _WIN32
        DWORD count{};
        auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1U << 20));
        if (!WriteFile(file.value().value, bytes.data() + offset, chunk, &count, nullptr) ||
            !count) {
            cleanup();
            return io();
        }
#else
        auto count = ::write(file.value().value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            cleanup();
            return io();
        }
#endif
        offset += static_cast<std::size_t>(count);
    }
    auto stage = hook_call(hook, CommitStage::written);
    if (!stage) {
        cleanup();
        return stage;
    }
#ifdef _WIN32
    if (!FlushFileBuffers(file.value().value)) {
        cleanup();
        return io();
    }
#else
    if (::fsync(file.value().value)) {
        cleanup();
        return io();
    }
#endif
    stage = hook_call(hook, CommitStage::file_synced);
    if (!stage) {
        cleanup();
        return stage;
    }
    verified = impl_->verify();
    if (!verified) {
        cleanup();
        return verified;
    }
    stage = hook_call(hook, CommitStage::before_replace);
    if (!stage) {
        cleanup();
        return stage;
    }
    verified = impl_->verify();
    if (!verified) {
        cleanup();
        return verified;
    }
    verified = verify_leaf(true);
    if (!verified) {
        cleanup();
        return verified;
    }
    file.value().close();
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) {
        cleanup();
        return io();
    }
#else
    if (!replace) {
        if (::linkat(impl_->directory.value, temp.c_str(), impl_->directory.value, name.c_str(),
                     0)) {
            cleanup();
            return io();
        }
        if (::unlinkat(impl_->directory.value, temp.c_str(), 0)) {
            impl_->uncertain = true;
            return io();
        }
    } else if (::renameat(impl_->directory.value, temp.c_str(), impl_->directory.value,
                          name.c_str())) {
        cleanup();
        return io();
    }
#endif
    impl_->uncertain = true;
    stage = hook_call(hook, CommitStage::replaced);
    if (!stage)
        return stage;
#ifndef _WIN32
    if (::fsync(impl_->directory.value))
        return io();
#endif
    stage = hook_call(hook, CommitStage::directory_synced);
    if (!stage)
        return stage;
    verified = verify_leaf(false);
    if (!verified)
        return verified;
    impl_->uncertain = false;
    verified = impl_->verify();
    if (!verified) {
        impl_->uncertain = true;
        return verified;
    }
    return {};
}
Result<void> PrivateDirectory::remove(std::string_view leaf) {
    if (!leaf_ok(leaf) || leaf == ".tansr.lock")
        return unsafe();
    std::lock_guard<std::recursive_mutex> held(impl_->mutex);
    if (impl_->entered)
        return Error{ErrorCode::reentrant, "storage callback reentry rejected"};
    Entry entry(impl_->entered);
    auto verified = impl_->verify();
    if (!verified)
        return verified;
#ifdef _WIN32
    auto path = impl_->path / std::filesystem::u8path(leaf);
    auto checked = open_file(path, OPEN_EXISTING);
    if (!checked)
        return checked.error();
    checked.value().close();
    if (!DeleteFileW(path.c_str()))
        return io();
#else
    auto checked = open_file(impl_->directory.value, std::string(leaf), 0);
    if (!checked)
        return checked.error();
    if (::unlinkat(impl_->directory.value, std::string(leaf).c_str(), 0))
        return io();
    impl_->uncertain = true;
    if (::fsync(impl_->directory.value))
        return io();
    impl_->uncertain = false;
#endif
    return {};
}
} // namespace tansr::storage
