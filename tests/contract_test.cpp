#include "tansr/api.hpp"
#include "tansr/operations.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using tansr::Json;
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    require(file.good(), "missing frozen fixture: " + path.filename().string());
    const std::string bytes(std::istreambuf_iterator<char>(file), {});
    auto value = Json::parse(bytes, tansr::JsonLimits{8U * 1024U * 1024U, 64, 200000});
    require(static_cast<bool>(value), "invalid frozen JSON: " + path.filename().string());
    return std::move(value).value();
}
std::string unescape(std::string_view text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '~') {
            require(i + 1 < text.size() && (text[i + 1] == '0' || text[i + 1] == '1'),
                    "bad frozen patch pointer");
            result += text[++i] == '0' ? '~' : '/';
        } else
            result += text[i];
    }
    return result;
}
Json *locate(Json &root, std::string_view path) {
    Json *current = &root;
    while (!path.empty()) {
        require(path.front() == '/', "bad patch pointer");
        path.remove_prefix(1);
        const auto cut = path.find('/');
        const auto key = unescape(path.substr(0, cut));
        current = current->is_array() ? &current->at(static_cast<std::size_t>(std::stoull(key)))
                                      : &current->at(key);
        if (cut == std::string_view::npos)
            return current;
        path.remove_prefix(cut);
    }
    return current;
}
void patch(Json &root, const Json &edit) {
    const auto &path = edit.at("path").as_string();
    const auto &action = edit.at("op").as_string();
    require(action == "add" || action == "replace" || action == "remove",
            "unknown frozen patch operation");
    if (path.empty()) {
        require(action != "remove", "cannot remove frozen root");
        root = edit.at("value");
        return;
    }
    const auto cut = path.rfind('/');
    require(cut != std::string::npos, "invalid frozen patch path");
    Json *parent = locate(root, std::string_view(path).substr(0, cut));
    const auto key = unescape(std::string_view(path).substr(cut + 1));
    if (parent->is_array()) {
        auto &items = parent->as_array();
        const auto index = key == "-" ? items.size() : static_cast<std::size_t>(std::stoull(key));
        require(index <= items.size() && (action == "add" || index < items.size()),
                "frozen patch index out of range");
        if (action == "remove")
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
        else if (action == "replace")
            items[index] = edit.at("value");
        else
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), edit.at("value"));
    } else {
        auto &members = parent->as_object();
        const auto found = std::find_if(members.begin(), members.end(),
                                        [&key](const auto &pair) { return pair.first == key; });
        require(action == "add" || found != members.end(), "frozen patch property missing");
        if (action == "remove")
            members.erase(found);
        else
            parent->set(key, edit.at("value"));
    }
}
Json materialize(const Json &vector, const Json::Array &vectors,
                 const std::filesystem::path &directory, unsigned depth = 0) {
    require(depth < 32, "cyclic frozen golden base");
    if (const auto *file = vector.find("valueFile")) {
        require(file->as_string() == "packages/server/contract/api-manifest.json",
                "unsupported frozen valueFile");
        return read(directory / "api-manifest.json");
    }
    if (const auto *base = vector.find("base")) {
        const auto found =
            std::find_if(vectors.begin(), vectors.end(), [base](const Json &candidate) {
                return candidate.at("name").as_string() == base->as_string();
            });
        require(found != vectors.end(), "missing frozen base");
        Json value = materialize(*found, vectors, directory, depth + 1);
        for (const auto &edit : vector.at("patch").as_array())
            patch(value, edit);
        return value;
    }
    return vector.at("value");
}
void compare_path(const std::vector<std::string_view> &actual, const Json &expected) {
    if (expected.is_null()) {
        require(actual.empty(), "unexpected generated path");
        return;
    }
    require(actual.size() == expected.as_array().size(), "generated path size changed");
    for (std::size_t i = 0; i < actual.size(); ++i)
        require(actual[i] == expected.at(i).as_string(), "generated path changed");
}
void operations(const std::filesystem::path &directory) {
    const auto manifest = read(directory / "api-manifest.json");
    require(tansr::all_operations().size() == 81, "expected 81 operations");
    require(manifest.at("revision").as_u64() == tansr::manifest_revision &&
                manifest.at("schemaHash").as_string() == tansr::schema_hash,
            "public frozen fingerprint differs");
    std::size_t fenced = 0;
    for (std::size_t i = 0; i < 81; ++i) {
        const auto &original = manifest.at("operations").at(i);
        const auto &generated = tansr::all_operations()[i];
        const bool expected_fenced = generated.name != "discovery.manifest" &&
                                     generated.name != "discovery.capabilities" &&
                                     generated.name != "discovery.session.capabilities" &&
                                     generated.name != "session.capabilities";
        require(generated.fenced == expected_fenced, "generated operation fence differs");
        require(generated.name == original.at("name").as_string(), "operation identity changed");
        require(tansr::find_operation(generated.name) == &generated, "operation lookup differs");
        require(generated.domain == original.at("domain").as_string() &&
                    generated.method == original.at("method").as_string() &&
                    generated.path == original.at("apiPath").as_string() &&
                    generated.kind == original.at("kind").as_string() &&
                    generated.sse == original.at("sse").as_bool(),
                "generated route metadata differs");
        for (const auto &item : {std::pair{"family", &generated.family},
                                 {"request", &generated.request_schema},
                                 {"response", &generated.response_schema}}) {
            const auto &expected = original.at(item.first);
            require(expected.is_null()
                        ? !item.second->has_value()
                        : item.second->has_value() && item.second->value() == expected.as_string(),
                    "generated optional schema differs");
        }
        compare_path(generated.query, original.at("query"));
        compare_path(generated.etag_path, original.at("etagPath"));
        if (generated.family) {
            const auto &families = manifest.at("families").as_array();
            const auto family =
                std::find_if(families.begin(), families.end(), [&](const Json &item) {
                    return item.at("id").as_string() == *generated.family;
                });
            require(family != families.end(), "unknown generated family");
            compare_path(generated.request_id_path, family->at("requestIdPath"));
        } else
            require(generated.request_id_path.empty(), "discovery gained write identity");
        const auto &revision = original.at("expectedRevision");
        require(revision.is_null() == !generated.expected_revision.has_value(),
                "generated revision presence differs");
        if (generated.expected_revision) {
            compare_path(generated.expected_revision->path, revision.at("path"));
            require(generated.expected_revision->kind == revision.at("kind").as_string(),
                    "generated revision kind differs");
        }
        fenced += generated.fenced ? 1U : 0U;
    }
    require(fenced == 77 && !tansr::find_operation("not.an.operation"),
            "closure/unknown operation boundary changed");
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 2, "usage: contract_test <contract-directory>");
        const auto directory = std::filesystem::u8path(argv[1]);
        operations(directory);
        const auto golden = read(directory / "unified-v1.golden.json");
        const auto &vectors = golden.at("vectors").as_array();
        require(vectors.size() == 165, "expected all 165 unified vectors");
        std::size_t positives = 0;
        for (const auto &vector : vectors) {
            const bool expected = vector.at("expect").as_string() == "valid";
            const auto result =
                tansr::validate_wire("unified-v1", vector.at("definition").as_string(),
                                     materialize(vector, vectors, directory));
            require(static_cast<bool>(result) == expected,
                    "unified golden mismatch: " + vector.at("name").as_string() +
                        (result ? " accepted" : " rejected: " + result.error().message));
            positives += expected ? 1U : 0U;
        }
        require(positives == 41, "golden positive count differs");
        std::size_t terminal_count = 0;
        for (const std::string family :
             {"terminal-services-v1", "terminal-observation-v1", "terminal-profile-v1"}) {
            const auto terminal = read(directory / (family + ".golden.json"));
            for (const auto name : {"positive", "negative"})
                for (const auto &vector : terminal.at(name).as_array()) {
                    const auto result = tansr::validate_wire(
                        family, vector.at("definition").as_string(), vector.at("value"));
                    require(static_cast<bool>(result) == (std::string_view(name) == "positive"),
                            "terminal golden mismatch: " + family + "/" +
                                vector.at("id").as_string());
                    ++terminal_count;
                }
        }
        require(terminal_count == 92, "terminal frozen golden count differs");
        for (const auto family :
             {"unified-v1", "sdk2-ext-v1", "sdk2-archive-recovery-v1", "archive-sync-v1",
              "sdk2-cache-v1", "sdk2-cache-core-v1", "terminal-services-v1",
              "terminal-observation-v1", "terminal-profile-v1", "terminal-shell-sandbox-v1"}) {
            const auto result = tansr::validate_wire(family, "DoesNotExist", Json{});
            require(!result && result.error().code == tansr::ErrorCode::invalid_input,
                    "frozen schema cannot load or unknown definition did not fail closed");
        }
        require(!tansr::validate_wire("agent-session-v1", "Id", Json("id")),
                "unsupported schema family bypassed validation");
        std::cout << "contract: 81 operation mappings, 165 unified vectors (41 positive), 92 "
                     "terminal vectors passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "contract: " << error.what() << '\n';
        return 1;
    }
}
