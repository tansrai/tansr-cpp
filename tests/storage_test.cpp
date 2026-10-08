#include "tansr/crypto.hpp"
#include "tansr/storage.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <sddl.h>
#include <winioctl.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace tansr;
namespace {
int checks{};
void check(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
template <class T> T must(Result<T> value) {
    if (!value)
        throw std::runtime_error(value.error().message);
    return std::move(value).value();
}
void must(Result<void> value) {
    if (!value)
        throw std::runtime_error(value.error().message);
}
auto allowed = []() -> Result<void> { return {}; };
std::filesystem::path fresh_root() {
    auto random = must(crypto::random_bytes(12));
    auto root = std::filesystem::canonical(std::filesystem::temp_directory_path());
    std::string name = "tansr-cpp-storage-";
    const char *hex = "0123456789abcdef";
    for (auto byte : random) {
        name += hex[byte >> 4];
        name += hex[byte & 15];
    }
    root /= name;
    must(storage::create_private_directory(root));
    return root;
}
#ifdef _WIN32
bool child_lock(const std::filesystem::path &program, const std::filesystem::path &root) {
    std::wstring command =
        L"\"" + program.wstring() + L"\" --lock-child \"" + root.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                         nullptr, nullptr, &startup, &process) != 0,
          "spawn lock child");
    const auto waited = WaitForSingleObject(process.hProcess, 10000);
    DWORD code = 99;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return waited == WAIT_OBJECT_0 && code == 0;
}
void junction(const std::filesystem::path &link, const std::filesystem::path &target) {
    check(CreateDirectoryW(link.c_str(), nullptr) != 0, "create junction leaf");
    HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    check(handle != INVALID_HANDLE_VALUE, "open junction leaf");
    const auto substitute = L"\\??\\" + target.wstring(), print = target.wstring();
    struct Header {
        DWORD tag;
        WORD size, reserved, sub_offset, sub_length, print_offset, print_length;
    };
    const auto path_bytes = (substitute.size() + 1 + print.size() + 1) * sizeof(wchar_t);
    std::vector<unsigned char> buffer(sizeof(Header) + path_bytes);
    auto *header = reinterpret_cast<Header *>(buffer.data());
    header->tag = IO_REPARSE_TAG_MOUNT_POINT;
    header->size = static_cast<WORD>(8 + path_bytes);
    header->sub_offset = 0;
    header->sub_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    header->print_offset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
    header->print_length = static_cast<WORD>(print.size() * sizeof(wchar_t));
    auto *paths = reinterpret_cast<wchar_t *>(buffer.data() + sizeof(Header));
    std::copy(substitute.begin(), substitute.end(), paths);
    std::copy(print.begin(), print.end(), paths + substitute.size() + 1);
    DWORD returned{};
    const auto set =
        DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
                        static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(handle);
    check(set != 0, "set real junction");
}
#else
bool child_lock(const std::filesystem::path &program, const std::filesystem::path &root) {
    const auto child = fork();
    check(child >= 0, "fork lock child");
    if (child == 0) {
        execl(program.c_str(), program.c_str(), "--lock-child", root.c_str(), nullptr);
        _exit(99);
    }
    int status{};
    check(waitpid(child, &status, 0) == child, "wait lock child");
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif
} // namespace
int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--lock-child") {
        auto opened = storage::PrivateDirectory::open(std::filesystem::u8path(argv[2]), allowed);
        return !opened && opened.error().code == ErrorCode::conflict ? 0 : 3;
    }
    std::filesystem::path root;
    try {
        root = fresh_root();
        bool authorized = true;
        auto access = [&]() -> Result<void> {
            if (!authorized)
                return Error{ErrorCode::permission, "revoked"};
            return {};
        };
        auto directory = must(storage::PrivateDirectory::open(root, access));
        check(!must(directory->read("absent", 100)).has_value(), "absent is optional");
        check(!storage::PrivateDirectory::open(root, allowed), "same-process lock rejected");
        check(child_lock(std::filesystem::absolute(argv[0]), root), "cross-process lock rejected");
        for (const auto *invalid : {"../escape", "a/b", "a\\b", ".", "..", "a:", ".tansr.lock"})
            check(!directory->write_atomic(invalid, "x"), "invalid leaf rejected");
        must(directory->write_atomic("claim", std::string("a\0b", 3), false));
        check(must(directory->read("claim", 3)).value() == std::string("a\0b", 3),
              "owned raw bytes preserved");
        check(must(storage::read_private_file(root / "claim", 3, allowed)).value() ==
                  std::string("a\0b", 3),
              "private readonly read does not conflict with journal lock");
        auto collision = directory->write_atomic("claim", "changed", false);
        check(!collision && collision.error().code == ErrorCode::conflict,
              "exclusive create conflict");
        check(!directory->read("claim", 2), "read allocation bound");
        authorized = false;
        check(!directory->read("claim", 3) && !directory->write_atomic("other", "x"),
              "current revocation prevents access");
        authorized = true;
        for (auto stage : {storage::CommitStage::written, storage::CommitStage::file_synced,
                           storage::CommitStage::before_replace}) {
            auto failed = directory->write_atomic(
                "claim", "new", true, [stage](storage::CommitStage now) -> Result<void> {
                    if (now == stage)
                        return Error{ErrorCode::io, "injected"};
                    return {};
                });
            check(!failed, "precommit failure reports error");
            check(must(directory->read("claim", 3)).value() == std::string("a\0b", 3),
                  "precommit failure preserves old file");
        }
        auto uncertain = directory->write_atomic("claim", "new", true,
                                                 [](storage::CommitStage stage) -> Result<void> {
                                                     if (stage == storage::CommitStage::replaced)
                                                         return Error{ErrorCode::io, "injected"};
                                                     return {};
                                                 });
        check(!uncertain && !directory->read("claim", 3), "postreplace uncertainty freezes access");
        directory.reset();
        directory = must(storage::PrivateDirectory::open(root, access));
        check(must(directory->read("claim", 3)).value() == "new",
              "cold open sees complete new file");
        const auto unicode = std::filesystem::u8path(u8"档案-α");
        must(directory->write_atomic(unicode.u8string(), "utf8", false));
        check(must(directory->read(unicode.u8string(), 4)).value() == "utf8",
              "Unicode filename survives");
        const auto hard = root / "hard";
#ifdef _WIN32
        check(CreateHardLinkW(hard.c_str(), (root / "claim").c_str(), nullptr) != 0,
              "create hardlink");
#else
        check(::link((root / "claim").c_str(), hard.c_str()) == 0, "create hardlink");
#endif
        check(!directory->read("hard", 10) && !directory->write_atomic("claim", "bad"),
              "hardlink inode rejected");
        std::filesystem::remove(hard);
        directory.reset();
        const auto outside = root / "outside";
        must(storage::create_private_directory(outside));
        const auto link = root / "link";
#ifdef _WIN32
        junction(link, outside);
        check(!storage::PrivateDirectory::open(link, allowed), "Windows junction rejected");
        check(RemoveDirectoryW(link.c_str()) != 0, "remove junction itself");
        PSECURITY_DESCRIPTOR descriptor{};
        check(ConvertStringSecurityDescriptorToSecurityDescriptorW(
                  L"D:P(A;;FA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr) != 0,
              "build broad ACL");
        check(SetFileSecurityW(outside.c_str(),
                               DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                               descriptor) != 0,
              "set broad ACL");
        LocalFree(descriptor);
        check(!storage::PrivateDirectory::open(outside, allowed), "Windows broad ACL rejected");
#else
        check(::symlink(outside.c_str(), link.c_str()) == 0, "create symlink");
        check(!storage::PrivateDirectory::open(link, allowed), "Unix symlink rejected");
        std::filesystem::remove(link);
        check(::chmod(outside.c_str(), 0755) == 0, "set broad mode");
        check(!storage::PrivateDirectory::open(outside, allowed), "Unix broad mode rejected");
#endif
        directory = must(storage::PrivateDirectory::open(root, access));
        must(directory->remove("claim"));
        check(!must(directory->read("claim", 5)), "explicit remove durable");
        directory.reset();
        std::filesystem::remove_all(root);
        std::cout << "storage checks=" << checks << " passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "storage test failed: " << e.what() << "; retained root=" << root.u8string()
                  << "\n";
        return 1;
    }
}
