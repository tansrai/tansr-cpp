#include "tansr/canonical.hpp"
#include "tansr/operations.hpp"
#include "tansr/terminal_persistence.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace tansr;
namespace tp = tansr::terminal_persistence;
namespace ex = tansr::executor;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
    return std::move(r).value();
}
void take(Result<void> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
}
std::string hash(std::string_view s) { return take(crypto::sha256_hex(s)); }
std::string canonical_bytes(const Json &v) { return take(canonical::encode(v)); }
Json number(std::size_t value) { return Json(static_cast<std::uint64_t>(value)); }
Json request(const char *action) {
    return Json::object({{"contract", "terminal-persistence-v1"},
                         {"sourceId", "source"},
                         {"sourceGeneration", "1"},
                         {"domainKey", "domain"},
                         {"action", action}});
}
ex::Operation operation(const Json &input, std::string id = "operation") {
    ex::Operation op{std::move(id),
                     "session",
                     {"app", "user", "1"},
                     {"binding", "1", {"device", "connection", "1", "workspace", "1", {}}},
                     "MemoryPublication",
                     {"tool.invoke", Json::object({{"name", tp::tool_name},
                                                   {"definitionDigest", tp::tool_digest},
                                                   {"argsJson", input.dump()}})},
                     "",
                     "2099-01-01T00:00:00Z"};
    op.digest = take(ex::operation_digest(op));
    return op;
}
Json owner() {
    auto op = ex::to_json(operation(request("head")));
    return Json::object({{"scope", op.at("scope")},
                         {"sessionId", op.at("sessionId")},
                         {"binding", op.at("binding")}});
}
Json expected(const Json &root) {
    if (root.is_null())
        return {};
    return Json::object({{"commitRoot", root.at("commitRoot")},
                         {"generation", root.at("generation")},
                         {"bodyEtag", root.at("body").at("sha256")},
                         {"indexRoot", root.at("index").at("root")},
                         {"indexCount", root.at("index").at("count")}});
}
struct Plan {
    Json begin;
    std::vector<Json> puts;
    Plan(std::string body, std::string id, Json root = {},
         std::vector<std::pair<Json, std::string>> entries = {}, std::size_t added = 0) {
        Json refs = Json::array(), body_hashes = Json::array(), index_hashes = Json::array();
        std::vector<std::pair<std::string, std::string>> objects;
        for (std::size_t at = 0; at < body.size(); at += 12288) {
            const auto bytes = body.substr(at, 12288);
            refs.as_array().push_back(
                Json::object({{"sha256", hash(bytes)}, {"byteLength", number(bytes.size())}}));
            objects.emplace_back("body-block", bytes);
        }
        std::vector<std::pair<std::string, std::string>> pages;
        for (std::size_t at = 0; at < refs.as_array().size(); at += 64) {
            Json items = Json::array();
            for (std::size_t i = at; i < std::min(at + 64, refs.as_array().size()); ++i)
                items.as_array().push_back(refs.at(i));
            auto bytes = canonical_bytes(Json::object({{"version", 1},
                                                       {"kind", "body-page"},
                                                       {"index", number(at / 64)},
                                                       {"refs", items}}));
            body_hashes.as_array().emplace_back(hash(bytes));
            pages.emplace_back("body-page", bytes);
        }
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
            return a.first.at("primaryKey").as_string() < b.first.at("primaryKey").as_string();
        });
        for (std::size_t at = 0; at < entries.size(); at += 32) {
            Json items = Json::array();
            for (std::size_t i = at; i < std::min(at + 32, entries.size()); ++i)
                items.as_array().push_back(entries[i].first);
            auto bytes = canonical_bytes(Json::object({{"version", 1},
                                                       {"kind", "index-page"},
                                                       {"index", number(at / 32)},
                                                       {"entries", items}}));
            index_hashes.as_array().emplace_back(hash(bytes));
            pages.emplace_back("index-page", bytes);
        }
        for (const auto &entry : entries)
            objects.emplace_back("receipt-value", entry.second);
        pages.insert(pages.end(), objects.begin(), objects.end());
        std::vector<std::string> keys;
        std::size_t total = 0;
        begin = request("begin");
        begin.set("transferId", id);
        begin.set("expected", expected(root));
        begin.set("body", Json::object({{"byteLength", number(body.size())},
                                        {"sha256", hash(body)},
                                        {"blockCount", number(refs.as_array().size())},
                                        {"pageHashes", body_hashes}}));
        begin.set("index", Json::object({{"entryCount", number(entries.size())},
                                         {"addedCount", number(added)},
                                         {"pageHashes", index_hashes}}));
        for (const auto &obj : pages) {
            const auto digest = hash(obj.second), key = obj.first + digest;
            if (std::find(keys.begin(), keys.end(), key) != keys.end())
                continue;
            keys.push_back(key);
            total += obj.second.size();
            auto put = request("put");
            put.set("transferId", id);
            put.set("kind", obj.first);
            put.set("sha256", digest);
            put.set("byteLength", number(obj.second.size()));
            put.set("base64", crypto::base64_encode(obj.second));
            puts.push_back(put);
        }
        begin.set("declared",
                  Json::object({{"objects", number(keys.size())}, {"bytes", number(total)}}));
        begin.set("intentSha256", hash(canonical_bytes(begin)));
        for (auto &put : puts)
            put.set("intentSha256", begin.at("intentSha256"));
    }
    Json action(const char *name) const {
        auto value = request(name);
        value.set("transferId", begin.at("transferId"));
        value.set("intentSha256", begin.at("intentSha256"));
        return value;
    }
};
std::pair<Json, std::string> entry(std::size_t n) {
    const auto value = "opaque result " + std::to_string(n);
    return {Json::object({{"primaryKey", hash("operation" + std::to_string(n))},
                          {"secondaryKey", hash("request" + std::to_string(n))},
                          {"value", Json::object({{"sha256", hash(value)},
                                                  {"byteLength", number(value.size())}})}}),
            value};
}
std::string status(const Json &r) { return r.at("transfer").at("status").as_string(); }
Json publish(tp::FileStore &store, const Plan &plan) {
    check(status(take(store.execute(plan.begin, owner()))) == "staging", "begin stages");
    for (const auto &put : plan.puts)
        check(status(take(store.execute(put, owner()))) == "staging", "put stages");
    auto done = take(store.execute(plan.action("commit"), owner()));
    check(status(done) == "committed", "commit publishes");
    return done.at("transfer").at("result");
}
std::string bytes(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
Json plaintext(const tp::Options &opts) {
    const auto blob = bytes(opts.path);
    const std::string magic = "Tansr-Cpp-TerminalPersistence/1\n";
    crypto::GcmNonce nonce{};
    crypto::GcmTag tag{};
    std::memcpy(nonce.data(), blob.data() + magic.size(), nonce.size());
    std::memcpy(tag.data(), blob.data() + blob.size() - tag.size(), tag.size());
    const auto plain = take(crypto::aes256_gcm_decrypt(
        take(opts.read_key()), nonce,
        std::string_view(blob).substr(magic.size() + nonce.size(),
                                      blob.size() - magic.size() - nonce.size() - tag.size()),
        tag, magic + canonical_bytes(opts.identity) + "\n" + opts.key_id));
    return take(Json::parse(std::string(reinterpret_cast<const char *>(plain.data()), plain.size()),
                            JsonLimits{64U << 20, 48, 1000000}));
}
std::string fixture_hmac(const crypto::Aes256Key &key, const std::string &input) {
    std::string inner(64, '\x36'), outer(64, '\x5c');
    for (std::size_t n = 0; n < key.size(); ++n) {
        inner[n] ^= static_cast<char>(key[n]);
        outer[n] ^= static_cast<char>(key[n]);
    }
    const auto digest = hash(inner + input);
    for (std::size_t n = 0; n < digest.size(); n += 2)
        outer.push_back(static_cast<char>(std::stoul(digest.substr(n, 2), nullptr, 16)));
    return hash(outer);
}
void fixture_bytes(const std::filesystem::path &path, const std::string &value) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    out.close();
    check(static_cast<bool>(out), "fixture wrote exact private bytes");
}
void fixture_budget(const tp::Options &opts, const Json &state) {
    auto budget = Json::object({{"version", 1},
                                {"storeId", state.at("budgetId")},
                                {"identitySha256", hash(canonical_bytes(opts.identity))},
                                {"keyId", opts.key_id},
                                {"writes", state.at("writes")}});
    budget.set("tag", fixture_hmac(take(opts.read_key()), canonical_bytes(budget)));
    fixture_bytes(opts.path.u8string() + ".writes", canonical_bytes(budget));
}
void fixture_snapshot(const tp::Options &opts, const Json &state) {
    const std::string magic = "Tansr-Cpp-TerminalPersistence/1\n";
    crypto::GcmNonce nonce{};
    const auto random = take(crypto::random_bytes(12));
    std::copy(random.begin(), random.end(), nonce.begin());
    const auto cipher = take(
        crypto::aes256_gcm_encrypt(take(opts.read_key()), nonce, state.dump(),
                                   magic + canonical_bytes(opts.identity) + "\n" + opts.key_id));
    std::string blob(magic);
    blob.append(reinterpret_cast<const char *>(nonce.data()), nonce.size());
    blob.append(reinterpret_cast<const char *>(cipher.ciphertext.data()), cipher.ciphertext.size());
    blob.append(reinterpret_cast<const char *>(cipher.tag.data()), cipher.tag.size());
    std::ofstream out(opts.path, std::ios::binary | std::ios::trunc);
    out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    out.close();
    check(static_cast<bool>(out), "test fixture wrote private snapshot");
    fixture_budget(opts, state);
}
Json execute_journal(tp::FileStore &store, const Json &request_value, const std::string &id) {
    auto op = operation(request_value, id);
    check(take(store.claim(op)).state == ex::ClaimState::claimed, "original execution claim");
    auto result = take(store.execute(request_value, owner()));
    auto tool = Json::object(
        {{"status", "ok"},
         {"content", Json::array({Json::object({{"t", "text"}, {"text", result.dump()}})})}});
    ex::Receipt receipt{
        "device",    "connection",
        id,          op.digest,
        "completed", ex::Resource{"tool.invoke", Json::object({{"resultJson", tool.dump()}})},
        std::nullopt};
    take(store.complete(op, receipt));
    check(take(store.claim(op)).state == ex::ClaimState::receipt, "permanent original receipt");
    return result;
}

void budget_checks(tp::Options opts) {
    opts.path = opts.path.parent_path() / "attempts";
    opts.create = true;
    bool armed = false;
    opts.commit_hook = [&](storage::CommitStage stage) -> Result<void> {
        if (armed && stage == storage::CommitStage::before_replace)
            return Error{ErrorCode::io, "synthetic replace refusal"};
        return {};
    };
    auto store = take(tp::FileStore::open(opts));
    Plan plan("attempt remains charged", "attempts");
    take(store->execute(plan.begin, owner()));
    const auto original = bytes(opts.path);
    const auto before = plaintext(opts).at("writes").as_u64();
    const auto budget_path = std::filesystem::path(opts.path.u8string() + ".writes");
    const auto old_budget = bytes(budget_path);
    armed = true;
    for (std::uint64_t attempt = 1; attempt <= 3; ++attempt) {
        const auto failed = store->execute(plan.puts.front(), owner());
        check(!failed && failed.error().code == ErrorCode::unknown,
              "failed encrypted replacement retains unknown");
        take(store->close());
        store.reset();
        check(bytes(opts.path) == original, "failed attempt preserves original root bytes");
        check(std::filesystem::exists(budget_path), "failed encryption attempt has durable budget");
        const auto budget = take(Json::parse(bytes(budget_path)));
        check(budget.at("writes").as_u64() == before + attempt,
              "each failed encryption attempt burns one count before snapshot replace");
        opts.create = false;
        store = take(tp::FileStore::open(opts));
        check(status(take(store->execute(plan.action("query"), owner()))) == "staging",
              "same ticket remains staged after failed attempts");
    }
    armed = false;
    for (const auto &put : plan.puts)
        take(store->execute(put, owner()));
    check(status(take(store->execute(plan.action("commit"), owner()))) == "committed",
          "same original plan settles after bounded failures");
    take(store->close());
    store.reset();
    const auto current_budget = bytes(budget_path);
    const auto current_snapshot = bytes(opts.path);
    check(take(Json::parse(current_budget)).at("writes").as_u64() ==
              before + 3 + plan.puts.size() + 1,
          "successful attempt continues the durable counter");
    fixture_bytes(budget_path, old_budget);
    check(!tp::FileStore::open(opts), "authenticated budget rollback below snapshot rejected");
    fixture_bytes(budget_path, current_budget);
    auto corrupt = take(Json::parse(current_budget));
    corrupt.set("writes", Json(corrupt.at("writes").as_u64() + 1));
    fixture_bytes(budget_path, canonical_bytes(corrupt));
    check(!tp::FileStore::open(opts), "unauthenticated budget refused");
    fixture_bytes(budget_path, current_budget);
    std::filesystem::rename(budget_path, budget_path.u8string() + ".held");
    check(!tp::FileStore::open(opts), "missing original counter fails closed");
    std::filesystem::rename(budget_path.u8string() + ".held", budget_path);
    check(bytes(opts.path) == current_snapshot,
          "counter failures never rewrite authoritative snapshot");
    store = take(tp::FileStore::open(opts));
    check(status(take(store->execute(plan.action("query"), owner()))) == "committed",
          "restoring exact counter permits original receipt recovery");
    take(store->close());
    store.reset();

    // 首次根发布失败也保留预算；不把残留侧文件视为可以自动清理的空目标。
    opts.path = opts.path.parent_path() / "failed-create";
    opts.create = true;
    armed = true;
    check(!tp::FileStore::open(opts), "first root replace can fail after budget burn");
    const auto stranded = bytes(opts.path.u8string() + ".writes");
    check(!stranded.empty() && !std::filesystem::exists(opts.path),
          "first failure retains charged counter only");
    armed = false;
    check(!tp::FileStore::open(opts), "create cannot reset stranded original budget");
    check(bytes(opts.path.u8string() + ".writes") == stranded,
          "failed create retains exact counter");
}

} // namespace
int main(int argc, char **argv) {
    std::filesystem::path dir;
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        const auto golden = take(Json::parse(
            bytes(std::filesystem::path(TANSR_CONTRACT_DIR).parent_path() /
                  "profiles/terminal-persistence-v1/terminal-persistence-v1.golden.json")));
        for (const char *kind : {"positive", "negative"})
            for (const auto &vector : golden.at(kind).as_array())
                check(static_cast<bool>(validate_wire(
                          "terminal-persistence-v1", vector.at("definition").as_string(),
                          vector.at("value"))) == (std::string(kind) == "positive"),
                      vector.at("id").as_string().c_str());
        dir = std::filesystem::temp_directory_path() /
              ("tansr-pst-v1-" +
               hash(std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
                   .substr(0, 12));
        take(storage::create_private_directory(dir));
        tp::Options opts;
        opts.path = dir / "store";
        opts.create = true;
        opts.key_id = "key-v1";
        opts.identity = Json::object(
            {{"scope", Json::object({{"applicationScopeId", "app"}, {"endUserId", "user"}})},
             {"sourceId", "source"},
             {"sourceGeneration", "1"},
             {"domainKey", "domain"}});
        crypto::Aes256Key key{};
        key[0] = 91;
        opts.read_key = [&]() -> Result<crypto::Aes256Key> { return key; };
        opts.read_context = []() -> Result<ex::Scope> { return ex::Scope{"app", "user", "1"}; };
        if (mode == "--budget") {
            budget_checks(opts);
            std::filesystem::remove_all(dir);
            std::cout << "durable encryption budget: " << checks << " checks passed\n";
            return 0;
        }
        auto store = take(tp::FileStore::open(opts));
        check(take(store->execute(request("head"), owner())).at("root").is_null(),
              "new empty root");
        if (mode == "--receipts") {
            Json root;
            for (std::size_t batch = 0; batch < 3; ++batch) {
                std::vector<std::pair<Json, std::string>> entries;
                const auto total = batch == 2 ? 1U : 256U;
                for (std::size_t n = 0; n < total; ++n)
                    entries.push_back(entry(batch * 256 + n + 1));
                Plan plan("bounded body", "batch-" + std::to_string(batch), root, entries,
                          entries.size());
                root = publish(*store, plan);
                check(root.at("index").at("count").as_string() ==
                          std::to_string(batch * 256 + total),
                      "256/512/513 append count");
            }
            for (std::size_t n : {1U, 256U, 257U, 512U, 513U})
                for (const char *kind : {"primary", "secondary"}) {
                    auto lookup = request("lookup");
                    lookup.set("commitRoot", root.at("commitRoot"));
                    lookup.set(
                        "key",
                        Json::object({{"kind", kind},
                                      {"digest", entry(n).first.at(std::string(kind) + "Key")}}));
                    check(take(store->execute(lookup, owner()))
                                  .at("entry")
                                  .at("base64")
                                  .as_string() == crypto::base64_encode(entry(n).second),
                          "permanent double-key exact result");
                }
            Plan duplicate("bounded body", "duplicate", root, {entry(1)}, 0);
            root = publish(*store, duplicate);
            check(root.at("index").at("count").as_string() == "513",
                  "duplicate does not assign ordinal");
            auto bad = entry(1);
            bad.first.set("secondaryKey", hash("different"));
            Plan conflict("bounded body", "conflict", root, {bad}, 0);
            take(store->execute(conflict.begin, owner()));
            for (const auto &put : conflict.puts)
                take(store->execute(put, owner()));
            check(status(take(store->execute(conflict.action("query"), owner()))) == "rejected",
                  "dual-key alias conflict durable");
            take(store->close());
            store.reset();
            opts.create = false;
            store = take(tp::FileStore::open(opts));
            check(take(store->execute(request("head"), owner()))
                          .at("root")
                          .at("index")
                          .at("count")
                          .as_string() == "513",
                  "513 cold reopen");
            take(store->close());
            store.reset();
            std::filesystem::remove_all(dir);
            std::cout << "permanent receipt 513: " << checks << " checks passed\n";
            return 0;
        }
        if (mode == "--delta") {
            std::string body(4194304, '\0');
            for (std::size_t n = 0; n < body.size(); ++n)
                body[n] = static_cast<char>(n % 251);
            check(hash(body) == golden.at("semantic").at("deltaRecipe").at("sha256").as_string(),
                  "shared full body golden");
            Plan full(body, "full");
            std::uint64_t full_bytes = 0, delta_bytes = 0, full_calls = 0, delta_calls = 0;
            const auto invoke = [&](const Json &req, bool delta) {
                auto response = take(store->execute(req, owner()));
                (delta ? delta_bytes : full_bytes) += req.dump().size() + response.dump().size();
                ++(delta ? delta_calls : full_calls);
                return response;
            };
            invoke(request("head"), false);
            invoke(full.begin, false);
            for (const auto &put : full.puts)
                invoke(put, false);
            auto root = invoke(full.action("commit"), false).at("transfer").at("result");
            invoke(full.action("query"), false);
            check(root.at("body").at("blockCount").as_u64() == 342, "342 logical blocks");
            const auto unchanged = take(store->execute(request("head"), owner()));
            check(unchanged.at("root").at("body").at("sha256").as_string() == hash(body),
                  "zero change head witness");
            const auto unchanged_bytes = request("head").dump().size() + unchanged.dump().size();
            for (std::size_t n = 12288; n < 24576; ++n)
                body[n] = static_cast<char>(static_cast<unsigned char>(body[n]) ^ 1U);
            check(hash(body) ==
                      golden.at("semantic").at("deltaRecipe").at("change").at("sha256").as_string(),
                  "shared delta body golden");
            Plan delta(body, "delta", root);
            invoke(request("head"), true);
            invoke(delta.begin, true);
            // 描述页由原固定根恢复；只发送页0及被改变的块，其他base页/块由存储证明复用。
            for (const auto &put : delta.puts) {
                if ((put.at("kind").as_string() == "body-page" &&
                     put.at("sha256").as_string() ==
                         delta.begin.at("body").at("pageHashes").at(0).as_string()) ||
                    (put.at("kind").as_string() == "body-block" &&
                     put.at("sha256").as_string() == hash(body.substr(12288, 12288))))
                    invoke(put, true);
            }
            const auto next_root = invoke(delta.action("commit"), true).at("transfer").at("result");
            invoke(delta.action("query"), true);
            check(next_root.at("body").at("sha256").as_string() == hash(body),
                  "one block change final body exact");
            check(delta_bytes < full_bytes && delta_calls < full_calls,
                  "actual control JSON delta smaller");
            auto read = request("read");
            read.set("part", "body");
            read.set("commitRoot", next_root.at("commitRoot"));
            read.set("offset", 12288);
            read.set("length", 12288);
            check(take(store->execute(read, owner())).at("base64").as_string() ==
                      crypto::base64_encode(body.substr(12288, 12288)),
                  "fixed root read byte exact");
            take(store->close());
            store.reset();
            opts.create = false;
            store = take(tp::FileStore::open(opts));
            check(take(store->execute(request("head"), owner()))
                          .at("root")
                          .at("commitRoot")
                          .as_string() == next_root.at("commitRoot").as_string(),
                  "4MiB cold reopen");
            take(store->close());
            store.reset();
            std::filesystem::remove_all(dir);
            std::cout << Json::object(
                             {{"fullControlBytes", Json(full_bytes)},
                              {"fullInvokes", Json(full_calls)},
                              {"unchangedControlBytes", number(unchanged_bytes)},
                              {"unchangedInvokes", 1},
                              {"deltaControlBytes", Json(delta_bytes)},
                              {"deltaInvokes", Json(delta_calls)},
                              {"checks", checks},
                              {"scope", "local storage request+response JSON; not HTTP/Serve SLA"}})
                             .dump()
                      << "\n";
            return 0;
        }

        Plan first("hello", "first", {}, {entry(1)}, 1);
        auto first_root = publish(*store, first);
        auto lookup = request("lookup");
        lookup.set("commitRoot", first_root.at("commitRoot"));
        lookup.set("key", Json::object(
                              {{"kind", "primary"}, {"digest", entry(1).first.at("primaryKey")}}));
        check(take(store->execute(lookup, owner())).at("entry").at("base64").as_string() ==
                  crypto::base64_encode(entry(1).second),
              "opaque lookup exact");
        auto wrong = owner();
        wrong.set("sessionId", "other");
        check(!store->execute(first.action("query"), wrong), "wrong owner rejected");
        Plan second("world", "second", first_root, {entry(2)}, 1);
        auto second_root = publish(*store, second);
        check(second_root.at("index").at("count").as_string() == "2", "index append");
        check(!store->execute(lookup, owner()), "stale fixed root refused");
        check(
            canonical_bytes(
                take(store->execute(first.action("query"), owner())).at("transfer").at("result")) ==
                canonical_bytes(first_root),
            "historical result retained");
        Plan pending("pending body", "pending", second_root);
        take(store->execute(pending.begin, owner()));
        take(store->execute(pending.puts.front(), owner()));
        auto op = operation(pending.action("commit"), "pending-execution");
        check(take(store->claim(op)).state == ex::ClaimState::claimed, "journal claimed");
        take(store->close());
        store.reset();
        opts.create = false;
        store = take(tp::FileStore::open(opts));
        check(take(store->claim(op)).state == ex::ClaimState::pending,
              "unknown journal never reexecutes");
        check(status(take(store->execute(pending.action("query"), owner()))) == "staging",
              "restart staging preserved");
        for (const auto &put : pending.puts)
            take(store->execute(put, owner()));
        check(status(take(store->execute(pending.action("commit"), owner()))) == "committed",
              "same plan resume");
        auto host = take(tp::create_host(
            {store, store, [](const auto &, auto) -> Result<void> { return {}; }, true}));
        check(host.tools.count(tp::tool_name) == 1, "explicit trusted host");
        Plan host_rejection("rejected body", "host-rejected");
        for (const auto &write :
             {host_rejection.begin, host_rejection.puts.front(), host_rejection.action("commit")}) {
            auto result = host.tools.at(tp::tool_name).handler({{}, {}, operation(write)}, write);
            const auto *tool = std::get_if<Json>(&result);
            check(tool && tool->at("status").as_string() == "error" &&
                      tool->at("message").as_string() == "revision_conflict",
                  "durable rejected write must use tool error envelope");
        }
        const auto rejected_query = host_rejection.action("query");
        auto queried = host.tools.at(tp::tool_name)
                           .handler({{}, {}, operation(rejected_query)}, rejected_query);
        const auto *query_tool = std::get_if<Json>(&queried);
        check(query_tool && query_tool->at("status").as_string() == "ok" &&
                  status(take(Json::parse(
                      query_tool->at("content").at(0).at("text").as_string()))) == "rejected",
              "query returns durable rejected fact in successful envelope");
        // 已调用后的无效/异请求响应与撤权均保unknown，不误映射成确定拒绝。
        struct ReplyStore final : tp::Store {
            Json id, reply;
            const Json &identity() const noexcept override { return id; }
            bool atomic_durable_publication() const noexcept override { return true; }
            bool encrypted_at_rest() const noexcept override { return true; }
            Result<Json> execute(const Json &, const Json &, tp::Guard) override { return reply; }
        };
        auto replies = std::make_shared<ReplyStore>();
        replies->id = store->identity();
        const auto rejected_response = take(store->execute(host_rejection.begin, owner()));
        for (int malformed = 0; malformed != 4; ++malformed) {
            replies->reply = rejected_response;
            if (malformed == 0)
                replies->reply.set("action", "query");
            else if (malformed == 1)
                replies->reply.at("transfer").set("transferId", "other-transfer");
            else if (malformed == 2)
                replies->reply.at("transfer").at("rejection").set("code", "invalid-code");
            int authorizations = 0;
            auto controlled = take(tp::create_host({replies, store,
                                                    [&](const auto &, auto) -> Result<void> {
                                                        if (++authorizations > 1 && malformed == 3)
                                                            return Error{ErrorCode::permission,
                                                                         "revoked after execute"};
                                                        return {};
                                                    },
                                                    true}));
            const auto invalid =
                controlled.tools.at(tp::tool_name)
                    .handler({{}, {}, operation(host_rejection.begin)}, host_rejection.begin);
            const auto *failure = std::get_if<ex::ToolFailure>(&invalid);
            check(failure && failure->kind == ex::ToolFailure::Kind::unknown,
                  "post-execute invalid response or revocation remains unknown");
        }
        check(bytes(opts.path).find("pending body") == std::string::npos &&
                  bytes(opts.path).find("pending-execution") == std::string::npos,
              "all material and journal encrypted");
        take(store->close());
        store.reset();
        host = {};

        store = take(tp::FileStore::open(opts));
        auto foreign = request("head");
        foreign.set("domainKey", "foreign");
        check(!store->claim(operation(foreign, "foreign-claim")),
              "foreign source cannot consume journal reservation");
        take(store->close());
        store.reset();
        // Query恢复证明不授予新的写owner，未知query不创建票据。
        auto recovery_options = opts;
        int recoveries = 0;
        recovery_options.authorize_recovery = [&](const Json &, const Json &,
                                                  std::string_view) -> Result<void> {
            ++recoveries;
            return {};
        };
        store = take(tp::FileStore::open(recovery_options));
        auto recovered_owner = owner();
        recovered_owner.at("binding").at("target").set("connectionId", "replacement");
        check(status(take(store->execute(first.action("query"), recovered_owner))) == "committed" &&
                  recoveries == 1,
              "trusted query-only recovery");
        check(!store->execute(pending.action("commit"), recovered_owner),
              "query proof never grants commit");
        auto absent = first.action("query");
        absent.set("transferId", "missing");
        auto before_count = take(store->capacity()).at("used").at("transferFacts").as_u64();
        check(status(take(store->execute(absent, owner()))) == "unknown" &&
                  take(store->capacity()).at("used").at("transferFacts").as_u64() == before_count,
              "unknown query no empty record");
        take(store->close());
        store.reset();
        // 原子接纳的帽等值可用，加1保留原文件与旧根。
        auto capped = opts;
        capped.create = true;
        capped.path = dir / "capacity";
        Plan small("hello", "capacity");
        capped.limits.retained_bytes =
            262144 + static_cast<std::size_t>(small.begin.at("declared").at("bytes").as_u64());
        store = take(tp::FileStore::open(capped));
        take(store->execute(small.begin, owner()));
        const auto capacity = take(store->capacity());
        check(capacity.at("used").at("retainedBytes").as_u64() +
                      capacity.at("used").at("reservedBytes").as_u64() ==
                  capped.limits.retained_bytes,
              "exact logical reserve boundary");
        const auto capped_hash = hash(bytes(capped.path));
        Plan plus("x", "over");
        check(!store->execute(plus.begin, owner()) && hash(bytes(capped.path)) == capped_hash,
              "capacity+1 preserves original");
        for (const auto &put : small.puts)
            take(store->execute(put, owner()));
        check(status(take(store->execute(small.action("commit"), owner()))) == "committed",
              "accepted terminal reserve usable");
        take(store->close());
        store.reset();
        capped.create = false;
        --capped.limits.retained_bytes;
        check(!tp::FileStore::open(capped), "reopen cannot withdraw quota promises");
        auto active = opts;
        active.create = true;
        active.path = dir / "active";
        store = take(tp::FileStore::open(active));
        for (std::size_t n = 0; n < 32; ++n) {
            Plan empty("", "active-" + std::to_string(n));
            take(store->execute(empty.begin, owner()));
        }
        Plan excess("", "active-over");
        check(!store->execute(excess.begin, owner()) &&
                  take(store->capacity()).at("used").at("activeTransfers").as_u64() == 32,
              "active32 exact and +1 refused");
        take(store->close());
        store.reset();
        // 五个原写阶段均只能重开旧完整根或新完整根，同 transfer 继续结算。
        std::size_t stage_index = 0;
        for (auto stage : {storage::CommitStage::written, storage::CommitStage::file_synced,
                           storage::CommitStage::before_replace, storage::CommitStage::replaced,
                           storage::CommitStage::directory_synced}) {
            auto fault_options = opts;
            bool armed = false;
            fault_options.commit_hook = [&](storage::CommitStage actual) -> Result<void> {
                if (armed && actual == stage)
                    return Error{ErrorCode::io, "injected commit reply loss"};
                return {};
            };
            store = take(tp::FileStore::open(fault_options));
            const auto before = take(store->execute(request("head"), owner())).at("root");
            const auto current_stage = stage_index++;
            Plan plan("failure boundary", "fault-" + std::to_string(current_stage), before,
                      {entry(100 + current_stage)}, 1);
            take(store->execute(plan.begin, owner()));
            for (const auto &put : plan.puts)
                take(store->execute(put, owner()));
            armed = true;
            auto failed = store->execute(plan.action("commit"), owner());
            check(!failed && failed.error().code == ErrorCode::unknown,
                  "commit failure stays unknown");
            take(store->close());
            store.reset();
            armed = false;
            store = take(tp::FileStore::open(opts));
            const bool published = stage == storage::CommitStage::replaced ||
                                   stage == storage::CommitStage::directory_synced;
            auto observed = take(store->execute(plan.action("query"), owner()));
            check(status(observed) == (published ? "committed" : "staging"),
                  "exact same ticket state after fault");
            auto head = take(store->execute(request("head"), owner())).at("root");
            check(head.at("index").at("count").as_string() ==
                      std::to_string(std::stoull(before.at("index").at("count").as_string()) +
                                     (published ? 1 : 0)),
                  "index/root/result one atomic point");
            check(status(take(store->execute(plan.action("commit"), owner()))) == "committed",
                  "original ticket converges");
            take(store->close());
            store.reset();
        }
        // 当前 host guard 在原 IO 提交前后均重读；撤权不能提前成功 ACK。
        for (bool after : {false, true}) {
            bool armed = false, allowed = true;
            auto fault_options = opts;
            fault_options.commit_hook = [&](storage::CommitStage stage) -> Result<void> {
                if (armed && stage == (after ? storage::CommitStage::replaced
                                             : storage::CommitStage::before_replace))
                    allowed = false;
                return {};
            };
            store = take(tp::FileStore::open(fault_options));
            auto root = take(store->execute(request("head"), owner())).at("root");
            Plan plan("guard body", after ? "guard-after" : "guard-before", root);
            take(store->execute(plan.begin, owner()));
            for (const auto &put : plan.puts)
                take(store->execute(put, owner()));
            armed = true;
            auto failure = store->execute(plan.action("commit"), owner(), [&]() -> Result<void> {
                if (!allowed)
                    return Error{ErrorCode::permission, "revoked"};
                return {};
            });
            check(!failure, "revoked guard withholds result");
            take(store->close());
            store.reset();
            allowed = true;
            armed = false;
            store = take(tp::FileStore::open(opts));
            check(status(take(store->execute(plan.action("query"), owner()))) ==
                      (after ? "committed" : "staging"),
                  "guard loss exact old/new fact");
            take(store->close());
            store.reset();
        }
        // 同一把钥的永久写次数不能被 journal/query 重试吞掉已受理计划的余量。
        auto budget_options = opts;
        budget_options.path = dir / "budget";
        budget_options.create = true;
        store = take(tp::FileStore::open(budget_options));
        take(store->close());
        store.reset();
        auto budget_state = plaintext(budget_options);
        budget_state.set("writes", Json(std::uint64_t{(1U << 20) - 18}));
        fixture_snapshot(budget_options, budget_state);
        budget_options.create = false;
        store = take(tp::FileStore::open(budget_options));
        Plan budget("hello", "budget");
        execute_journal(*store, budget.begin, "begin-budget");
        std::size_t queries = 0;
        for (; queries < 16; ++queries) {
            auto probe = operation(request("head"), "probe-" + std::to_string(queries));
            auto claimed = store->claim(probe);
            if (!claimed) {
                check(claimed.error().code == ErrorCode::capacity,
                      "free budget exhaustion explicit");
                break;
            }
            auto result = take(store->execute(request("head"), owner()));
            auto tool = Json::object(
                {{"status", "ok"},
                 {"content",
                  Json::array({Json::object({{"t", "text"}, {"text", result.dump()}})})}});
            ex::Receipt receipt{
                "device",
                "connection",
                probe.operation_id,
                probe.digest,
                "completed",
                ex::Resource{"tool.invoke", Json::object({{"resultJson", tool.dump()}})},
                std::nullopt};
            take(store->complete(probe, receipt));
        }
        check(queries < 16, "unrelated queries cannot steal reserve");
        for (std::size_t n = 0; n < budget.puts.size(); ++n)
            execute_journal(*store, budget.puts[n], "put-budget-" + std::to_string(n));
        check(status(execute_journal(*store, budget.action("commit"), "commit-budget")) ==
                  "committed",
              "accepted plan settles at key limit");
        take(store->close());
        store.reset();
        // 显式迁移完整首次发布；原件/未知执行和永久结果都保留。
        store = take(tp::FileStore::open(opts));
        execute_journal(*store, request("head"), "copy-original-head");
        take(store->close());
        store.reset();
        auto migrated = opts;
        migrated.create = true;
        migrated.path = dir / "next" / "store";
        migrated.key_id = "new-key";
        crypto::Aes256Key next_key{};
        next_key[0] = 55;
        migrated.read_key = [&]() -> Result<crypto::Aes256Key> { return next_key; };
        take(storage::create_private_directory(migrated.path.parent_path()));
        const auto original_hash = hash(bytes(opts.path));
        const auto original_counter = bytes(opts.path.u8string() + ".writes");
        auto migration = take(tp::FileStore::migrate(opts, migrated));
        check(migration.source_sha256 == original_hash && hash(bytes(opts.path)) == original_hash,
              "migration preserves exact original");
        check(!tp::FileStore::migrate(opts, migrated), "existing migration target refused");
        migrated.create = false;
        const auto migrated_budget_path =
            std::filesystem::path(migrated.path.u8string() + ".writes");
        const auto migrated_budget = bytes(migrated_budget_path);
        fixture_bytes(migrated_budget_path, bytes(opts.path.u8string() + ".writes"));
        check(!tp::FileStore::open(migrated),
              "source key/store counter cannot replace migrated counter");
        fixture_bytes(migrated_budget_path, migrated_budget);
        store = take(tp::FileStore::open(migrated));
        check(take(store->claim(op)).state == ex::ClaimState::pending,
              "migration retains unknown original execution");
        check(status(take(store->execute(first.action("query"), owner()))) == "committed",
              "migration retains first historical result");
        // 没有共同源围栏的复制件只作核验，不能因冷开重新成为第二个 writer。
        const auto copied_bytes = bytes(migrated.path);
        const auto copied_counter = bytes(migrated_budget_path);
        const auto copied_root = take(store->execute(request("head"), owner())).at("root");
        auto copied_read = request("read");
        copied_read.set("part", "body");
        copied_read.set("commitRoot", copied_root.at("commitRoot"));
        copied_read.set("offset", Json(0));
        copied_read.set("length", Json(1));
        check(take(store->execute(copied_read, owner())).at("byteLength").as_u64() == 1,
              "readonly copy permits original body read");
        auto copied_lookup = request("lookup");
        copied_lookup.set("commitRoot", copied_root.at("commitRoot"));
        copied_lookup.set("key", Json::object({{"kind", "primary"},
                                               {"digest", entry(1).first.at("primaryKey")}}));
        check(!take(store->execute(copied_lookup, owner())).at("entry").is_null(),
              "readonly copy permits permanent index lookup");
        for (const auto &write : {first.begin, first.puts.front(), first.action("commit")}) {
            const auto rejected = store->execute(write, owner());
            check(!rejected && rejected.error().code == ErrorCode::permission,
                  "cold copied target refuses begin/put/commit without activation");
        }
        const auto fresh = operation(request("head"), "copy-new-read");
        const auto new_claim = store->claim(fresh);
        check(!new_claim && new_claim.error().code == ErrorCode::permission,
              "readonly combined copy cannot create execution claims even for reads");
        ex::Receipt unknown{"device",
                            "connection",
                            op.operation_id,
                            op.digest,
                            "unknown",
                            {},
                            "execution_outcome_unknown"};
        const auto completed_pending = store->complete(op, unknown);
        check(!completed_pending && completed_pending.error().code == ErrorCode::permission,
              "readonly copy preserves original pending claim without completion");
        const auto known = operation(request("head"), "copy-original-head");
        const auto known_claim = take(store->claim(known));
        check(known_claim.state == ex::ClaimState::receipt && known_claim.receipt.has_value(),
              "readonly copy returns original completed journal receipt");
        take(store->complete(known, *known_claim.receipt));
        check(bytes(migrated.path) == copied_bytes && bytes(migrated_budget_path) == copied_counter,
              "readonly calls preserve copy snapshot and dedicated key budget");
        take(store->close());
        store = take(tp::FileStore::open(migrated));
        check(!store->execute(first.begin, owner()) &&
                  status(take(store->execute(first.action("query"), owner()))) == "committed" &&
                  take(store->claim(op)).state == ex::ClaimState::pending,
              "readonly state and original facts survive another cold open");
        take(store->close());
        store = take(tp::FileStore::open(opts));
        check(status(take(store->execute(first.begin, owner()))) == "committed" &&
                  hash(bytes(opts.path)) == original_hash &&
                  bytes(opts.path.u8string() + ".writes") == original_counter,
              "copy never fences or rewrites source or resets its key budget");

        take(store->close());
        store.reset();

        std::filesystem::remove_all(dir);
        std::cout << "terminal persistence: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "terminal persistence failure after " << checks << ": " << e.what() << " at "
                  << dir.u8string() << "\n";
        return 1;
    }
}
