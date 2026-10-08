#include "tansr/crypto.hpp"
#include "tansr/storage.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace tansr;
namespace {
int checks{};
void check(bool value, const char *message) {
    ++checks;
    if (!value)
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
std::filesystem::path child(const std::filesystem::path &root, const std::string &name) {
    auto path = root / name;
    must(storage::create_private_directory(path));
    return path;
}
std::string raw(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void make_file(const std::filesystem::path &root, const char *leaf, const char *body) {
    auto directory = must(storage::PrivateDirectory::open(root, allowed));
    must(directory->write_atomic(leaf, body, false));
}
void directory_replacement(const std::filesystem::path &root) {
    for (int stage = 0; stage < 5; ++stage) {
        const auto active = child(root, "active-" + std::to_string(stage));
        const auto moved = root / ("moved-" + std::to_string(stage));
        auto directory = must(storage::PrivateDirectory::open(active, allowed));
        must(directory->write_atomic("claim", "old"));
        bool attempted{}, renamed{};
        auto result = directory->write_atomic(
            "claim", "new", true, [&](storage::CommitStage now) -> Result<void> {
                if (static_cast<int>(now) != stage)
                    return {};
                attempted = true;
                std::exception_ptr failure;
                std::thread replacer([&] {
                    try {
                        std::error_code error;
                        std::filesystem::rename(active, moved, error);
                        renamed = !error;
                        if (renamed) {
                            must(storage::create_private_directory(active));
                            make_file(active, "claim", "foreign");
                        }
                    } catch (...) {
                        failure = std::current_exception();
                    }
                });
                replacer.join();
                if (failure)
                    std::rethrow_exception(failure);
                return {};
            });
        check(attempted, "actual replacement thread reached selected commit stage");
#ifdef _WIN32
        check(!renamed && result && raw(active / "claim") == "new",
              "Windows held no-share-delete directory handles block rename race");
#else
        check(renamed && !result && raw(active / "claim") == "foreign",
              "Unix path replacement cannot report success or overwrite replacement scope");
        check(raw(moved / "claim") == (stage >= 3 ? "new" : "old"),
              "Unix detached directory contains only complete old or new file");
        check(!directory->read("claim", 100) && !directory->write_atomic("after", "forbidden"),
              "old directory object does not follow replacement path");
#endif
    }
}
void leaf_replacement(const std::filesystem::path &root) {
    const auto foreign = child(root, "foreign");
    make_file(foreign, "sentinel", "foreign-original");
    for (int stage : {2, 4}) {
        const auto active = child(root, "leaf-" + std::to_string(stage));
        auto directory = must(storage::PrivateDirectory::open(active, allowed));
        must(directory->write_atomic("claim", "old"));
        bool attempted{}, renamed{}, linked{};
        auto result = directory->write_atomic(
            "claim", "new", true, [&](storage::CommitStage now) -> Result<void> {
                if (static_cast<int>(now) != stage)
                    return {};
                std::filesystem::path target = active / "claim";
                if (stage == 2)
                    for (const auto &entry : std::filesystem::directory_iterator(active))
                        if (entry.path().filename().u8string().rfind(".tansr-tmp-", 0) == 0)
                            target = entry.path();
                attempted = true;
                std::thread replacer([&] {
                    std::error_code error;
                    std::filesystem::rename(target, active / "held-original", error);
                    renamed = !error;
                    if (renamed) {
                        std::filesystem::create_hard_link(foreign / "sentinel", target, error);
                        linked = !error;
                    }
                });
                replacer.join();
                return {};
            });
        check(attempted && raw(foreign / "sentinel") == "foreign-original",
              "leaf race leaves external source bytes unchanged");
#ifdef _WIN32
        if (stage == 2) {
            check(!renamed && result && raw(active / "claim") == "new",
                  "Windows held temporary handle prevents precommit substitution");
            continue;
        }
#endif
        check(renamed && linked && !result,
              "substituted temporary or committed inode never reports durable success");
        if (stage == 2)
            check(raw(active / "claim") == "old",
                  "temporary substitution preserves original target");
        else
            check(!directory->read("claim", 100), "postcommit substitution freezes unsafe access");
    }
}
void authorization_boundaries(const std::filesystem::path &root) {
    const auto active = child(root, "authorization");
    make_file(active, "claim", "private-original");
    int checks_before_delivery{};
    auto read = storage::read_private_file(active / "claim", 100, [&]() -> Result<void> {
        if (++checks_before_delivery == 3)
            return Error{ErrorCode::permission, "revoked before returning bytes"};
        return {};
    });
    check(!read && read.error().code == ErrorCode::permission && checks_before_delivery == 3,
          "read-only handle rechecks current authorization before byte delivery");
    bool permitted = true;
    auto directory = must(storage::PrivateDirectory::open(active, [&]() -> Result<void> {
        return permitted ? Result<void>{} : Result<void>{Error{ErrorCode::permission, "revoked"}};
    }));
    auto written = directory->write_atomic("claim", "must-not-commit", true,
                                           [&](storage::CommitStage stage) -> Result<void> {
                                               if (stage == storage::CommitStage::before_replace)
                                                   permitted = false;
                                               return {};
                                           });
    check(!written && raw(active / "claim") == "private-original",
          "authorization revoked at commit barrier leaves original target intact");
}
} // namespace
int main() {
    std::filesystem::path root;
    try {
        auto random = must(crypto::random_bytes(8));
        std::string name = "tansr-storage-race-";
        for (auto byte : random)
            name += "0123456789abcdef"[byte & 15];
        root = child(std::filesystem::canonical(std::filesystem::temp_directory_path()), name);
        directory_replacement(root);
        leaf_replacement(root);
        authorization_boundaries(root);
        std::filesystem::remove_all(root);
        std::cout << "CPP_A31 directoryStages=5 leafStages=2 authorityBoundaries=2 checks="
                  << checks << " passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "storage race failed: " << error.what() << "; checks=" << checks
                  << "; retained=" << root.u8string() << '\n';
        return 1;
    }
}
