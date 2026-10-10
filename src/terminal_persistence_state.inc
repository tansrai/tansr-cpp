
namespace {
constexpr std::size_t block_bytes = 12288, terminal_reserve = 262144;
constexpr std::uint64_t key_write_limit = 1U << 20;
void erase(Json &value, std::string_view key) {
    auto &items = value.as_object();
    items.erase(
        std::remove_if(items.begin(), items.end(), [&](const auto &p) { return p.first == key; }),
        items.end());
}
std::size_t count(const Json &value) { return value.as_array().size(); }
std::size_t size_of(const Json &value) { return encode(value).size(); }
std::string object_key(std::string_view kind, std::string_view digest) {
    return std::string(kind) + ":" + std::string(digest);
}
std::string object_key(const Json &value) {
    return object_key(text(value, "kind"), text(value, "sha256"));
}
Json object_meta(const Json &value) {
    return Json::object({{"kind", value.at("kind")},
                         {"sha256", value.at("sha256")},
                         {"byteLength", value.at("byteLength")}});
}
bool has(const Json &values, std::string_view key) {
    return std::any_of(values.as_array().begin(), values.as_array().end(),
                       [&](const Json &v) { return v.as_string() == key; });
}
void add(Json &values, const std::string &key) {
    if (!has(values, key))
        values.as_array().emplace_back(key);
}
std::uint64_t sequence(const Json &v) {
    validate("Sequence", v);
    return std::stoull(v.as_string());
}
Json stored_identity(const Json &identity) {
    auto result = identity.at("scope");
    for (const char *field : {"sourceId", "sourceGeneration", "domainKey"})
        result.set(field, identity.at(field));
    return result;
}
Json expected_of(const Json &root) {
    if (root.is_null())
        return {};
    return Json::object({{"commitRoot", root.at("commitRoot")},
                         {"generation", root.at("generation")},
                         {"bodyEtag", root.at("body").at("sha256")},
                         {"indexRoot", root.at("index").at("root")},
                         {"indexCount", root.at("index").at("count")}});
}
void valid_plan(const Json &begin) {
    const auto &body = begin.at("body"), &index = begin.at("index");
    const auto blocks = body.at("blockCount").as_u64();
    need(blocks == (body.at("byteLength").as_u64() + block_bytes - 1) / block_bytes,
         "invalid_request");
    need(count(body.at("pageHashes")) == (blocks + 63) / 64 &&
             count(index.at("pageHashes")) == (index.at("entryCount").as_u64() + 31) / 32 &&
             index.at("addedCount").as_u64() <= index.at("entryCount").as_u64(),
         "invalid_request");
    if (!blocks)
        need(text(body, "sha256") == hash(""), "invalid_request");
    auto intent = begin;
    erase(intent, "intentSha256");
    need(hash(encode(intent)) == text(begin, "intentSha256"), "request_conflict");
}
Json unknown(const Json &request) {
    return Json::object({{"transferId", request.at("transferId")},
                         {"intentSha256", request.at("intentSha256")},
                         {"status", "unknown"},
                         {"progress", Json()},
                         {"result", Json()},
                         {"rejection", Json()}});
}
Json row_material(const Json &row) {
    return Json::object({{"begin", row.at("begin")},
                         {"owner", row.at("owner")},
                         {"baseRoot", row.at("baseRoot")},
                         {"transfer", row.at("transfer")}});
}
std::string object_bytes(const Json &state, std::string_view key) {
    const auto *object = state.at("objects").find(key);
    need(object != nullptr, "integrity_mismatch");
    auto bytes = decode(object->at("base64"));
    need(bytes.size() == object->at("byteLength").as_u64() &&
             hash(bytes) == text(*object, "sha256"),
         "integrity_mismatch");
    return bytes;
}
Json page(const Json &state, std::string_view kind, const Json &digest) {
    auto bytes = object_bytes(state, object_key(kind, digest.as_string()));
    auto result = take(Json::parse(bytes, JsonLimits{block_bytes, 8, 4096}));
    need(encode(result) == bytes && text(result, "kind") == kind &&
             result.at("version").as_u64() == 1 && result.as_object().size() == 4,
         "integrity_mismatch");
    return result;
}
Json body_refs(const Json &state, const Json &body) {
    Json refs = Json::array();
    const auto &hashes = body.at("pageHashes").as_array();
    for (std::size_t i = 0; i < hashes.size(); ++i) {
        auto p = page(state, "body-page", hashes[i]);
        need(p.at("index").as_u64() == i, "integrity_mismatch");
        need(count(p.at("refs")) ==
                 std::min<std::uint64_t>(64, body.at("blockCount").as_u64() - i * 64),
             "integrity_mismatch");
        for (const auto &ref : p.at("refs").as_array()) {
            validate("Ref", ref);
            need(ref.at("byteLength").as_u64() ==
                     std::min<std::uint64_t>(block_bytes, body.at("byteLength").as_u64() -
                                                              count(refs) * block_bytes),
                 "integrity_mismatch");
            refs.as_array().push_back(ref);
        }
    }
    return refs;
}
std::string full_body(const Json &state, const Json &body) {
    std::string result;
    result.reserve(static_cast<std::size_t>(body.at("byteLength").as_u64()));
    const auto refs = body_refs(state, body);
    for (const auto &ref : refs.as_array()) {
        auto bytes = object_bytes(state, object_key("body-block", text(ref, "sha256")));
        need(bytes.size() == ref.at("byteLength").as_u64(), "integrity_mismatch");
        result += bytes;
    }
    need(result.size() == body.at("byteLength").as_u64() && hash(result) == text(body, "sha256"),
         "integrity_mismatch");
    return result;
}
const Json *entry_for(const Json &state, std::string_view field, std::string_view key) {
    for (const auto &row : state.at("entries").as_array())
        if (text(row.at("entry"), field) == key)
            return &row.at("entry");
    return nullptr;
}
Json index_hash(const Json &state, const Json &identity) {
    std::string previous = hash(encode(Json::array({"TPV1-INDEX", stored_identity(identity)})));
    std::uint64_t ordinal = 0;
    for (const auto &row : state.at("entries").as_array()) {
        need(sequence(row.at("ordinal")) == ++ordinal, "integrity_mismatch");
        previous =
            hash(encode(Json::array({"TPV1-ENTRY", previous, row.at("ordinal"), row.at("entry")})));
    }
    return Json::object({{"root", previous}, {"count", std::to_string(ordinal)}});
}
Json make_root(const Json &identity, const Json &body, const Json &index,
               std::uint64_t generation) {
    need(generation > 0 && generation <= 9223372036854775807ULL, "capacity_exceeded");
    auto root = Json::object(
        {{"generation", std::to_string(generation)}, {"body", body}, {"index", index}});
    const auto digest = hash(encode(Json::array({"TPV1-ROOT", stored_identity(identity), root})));
    root.set("commitRoot", digest);
    return root;
}
bool available(const Json &row, const std::string &key) {
    return has(row.at("received"), key) || has(row.at("reused"), key);
}
Json planned_pages(const Json &row) {
    Json keys = Json::array();
    for (const char *kind : {"body", "index"})
        for (const auto &digest : row.at("begin").at(kind).at("pageHashes").as_array())
            add(keys, object_key(std::string(kind) + "-page", digest.as_string()));
    return keys;
}
Json planned_refs(const Json &state, const Json &row, const char *kind) {
    Json result = Json::array();
    const auto &plan = row.at("begin").at(kind);
    const std::string page_kind = std::string(kind) + "-page";
    for (const auto &digest : plan.at("pageHashes").as_array()) {
        if (!available(row, object_key(page_kind, digest.as_string())))
            continue;
        const auto p = page(state, page_kind, digest);
        for (const auto &item : p.at(std::string(kind) == "body" ? "refs" : "entries").as_array())
            result.as_array().push_back(item);
    }
    return result;
}
std::string bitset(const std::vector<bool> &bits) {
    std::string bytes((bits.size() + 7) / 8, '\0');
    for (std::size_t i = 0; i < bits.size(); ++i)
        if (bits[i])
            bytes[i / 8] =
                static_cast<char>(static_cast<unsigned char>(bytes[i / 8]) | (1U << (i % 8)));
    return crypto::base64_encode(bytes);
}
Json progress(const Json &state, const Json &row) {
    std::vector<bool> pages, bodies, values;
    const auto &begin = row.at("begin");
    for (const char *kind : {"body", "index"}) {
        const auto &plan = begin.at(kind);
        const std::string page_kind = std::string(kind) + "-page";
        const bool body = std::string(kind) == "body";
        auto &bits = body ? bodies : values;
        bits.resize(static_cast<std::size_t>(plan.at(body ? "blockCount" : "entryCount").as_u64()),
                    false);
        const auto &hashes = plan.at("pageHashes").as_array();
        for (std::size_t i = 0; i < hashes.size(); ++i) {
            const auto ready = available(row, object_key(page_kind, hashes[i].as_string()));
            pages.push_back(ready);
            if (!ready)
                continue;
            const auto p = page(state, page_kind, hashes[i]);
            std::size_t n = i * (body ? 64 : 32);
            for (const auto &item : p.at(body ? "refs" : "entries").as_array()) {
                const auto &ref = body ? item : item.at("value");
                need(n < bits.size(), "integrity_mismatch");
                bits[n++] = available(
                    row, object_key(body ? "body-block" : "receipt-value", text(ref, "sha256")));
            }
        }
    }
    std::uint64_t received = 0;
    for (const auto &key : row.at("received").as_array())
        received += state.at("objects").at(key.as_string()).at("byteLength").as_u64();
    return Json::object({{"pagesReady", bitset(pages)},
                         {"bodyReady", bitset(bodies)},
                         {"valuesReady", bitset(values)},
                         {"receivedBytes", Json(received)}});
}
void rejected(Json &row, const char *code, const Json &root) {
    auto &transfer = row.at("transfer");
    transfer.set("status", "rejected");
    transfer.set("progress", Json());
    transfer.set("rejection", Json::object({{"code", code}, {"observedRoot", root}}));
    row.set("baseRoot", Json());
}
Json described_objects(const Json &state, const Json &row) {
    Json objects = Json::object();
    const auto put = [&](const std::string &key, std::uint64_t length) {
        if (const auto *old = objects.find(key))
            need(old->as_u64() == length, "invalid_request");
        else
            objects.set(key, Json(length));
    };
    const auto pages = planned_pages(row), bodies = planned_refs(state, row, "body"),
               entries = planned_refs(state, row, "index");
    for (const auto &key : pages.as_array()) {
        need(available(row, key.as_string()), "invalid_request");
        put(key.as_string(), state.at("objects").at(key.as_string()).at("byteLength").as_u64());
    }
    for (const auto &ref : bodies.as_array())
        put(object_key("body-block", text(ref, "sha256")), ref.at("byteLength").as_u64());
    for (const auto &entry : entries.as_array())
        put(object_key("receipt-value", text(entry.at("value"), "sha256")),
            entry.at("value").at("byteLength").as_u64());
    return objects;
}
void verify_index_plan(const Json &state, const Json &row) {
    auto entries = planned_refs(state, row, "index");
    std::string previous;
    std::vector<std::string> secondary;
    std::uint64_t added = 0;
    for (const auto &entry : entries.as_array()) {
        validate("Entry", entry);
        const auto primary = text(entry, "primaryKey"), second = text(entry, "secondaryKey");
        need(previous.empty() || previous < primary, "request_conflict");
        need(std::find(secondary.begin(), secondary.end(), second) == secondary.end(),
             "request_conflict");
        previous = primary;
        secondary.push_back(second);
        const auto *a = entry_for(state, "primaryKey", primary),
                   *b = entry_for(state, "secondaryKey", second);
        if (a || b)
            need(a && b && equal(*a, entry) && equal(*b, entry), "request_conflict");
        else
            ++added;
    }
    need(added == row.at("begin").at("index").at("addedCount").as_u64(), "invalid_request");
}
void reuse_values(const Json &state, Json &row) {
    const auto entries = planned_refs(state, row, "index");
    for (const auto &entry : entries.as_array()) {
        const auto *old = entry_for(state, "primaryKey", text(entry, "primaryKey"));
        if (old && equal(*old, entry)) {
            const auto key = object_key("receipt-value", text(entry.at("value"), "sha256"));
            need(object_bytes(state, key).size() == entry.at("value").at("byteLength").as_u64(),
                 "integrity_mismatch");
            if (!has(row.at("received"), key))
                add(row.at("reused"), key);
        }
    }
}
void reuse_body(const Json &state, Json &row) {
    if (row.at("baseRoot").is_null())
        return;
    const auto &base = row.at("baseRoot").at("body");
    auto old_refs = body_refs(state, base);
    const auto &hashes = row.at("begin").at("body").at("pageHashes").as_array();
    for (std::size_t i = 0; i < hashes.size(); ++i) {
        const auto key = object_key("body-page", hashes[i].as_string());
        if (i < count(base.at("pageHashes")) && equal(hashes[i], base.at("pageHashes").at(i)))
            if (!has(row.at("received"), key))
                add(row.at("reused"), key);
    }
    const auto refs = planned_refs(state, row, "body");
    for (const auto &ref : refs.as_array())
        for (const auto &old : old_refs.as_array())
            if (equal(ref, old)) {
                const auto key = object_key("body-block", text(ref, "sha256"));
                need(object_bytes(state, key).size() == ref.at("byteLength").as_u64(),
                     "integrity_mismatch");
                if (!has(row.at("received"), key))
                    add(row.at("reused"), key);
                break;
            }
}
} // namespace

Json FileStore::Impl::capacity_of(const Json &next) {
    std::uint64_t active = 0, staging_bytes = 0, retained = 0, reserved = 0, objects = 0,
                  entries = 0;
    for (const auto &item : next.at("objects").as_object())
        retained += item.second.at("byteLength").as_u64() + size_of(object_meta(item.second));
    for (const auto &entry : next.at("entries").as_array())
        retained += size_of(entry);
    for (const auto &item : next.at("transfers").as_object()) {
        const auto &row = item.second;
        const auto meta = size_of(row_material(row));
        retained += meta;
        if (text(row.at("transfer"), "status") != "staging")
            continue;
        ++active;
        std::uint64_t ready_bytes = 0, reused_bytes = 0, descriptors = 0;
        for (const char *set : {"received", "reused"})
            for (const auto &key : row.at(set).as_array()) {
                const auto bytes = next.at("objects").at(key.as_string()).at("byteLength").as_u64();
                ready_bytes += bytes;
                if (std::string_view(set) == "reused")
                    reused_bytes += bytes;
            }
        for (const auto &key : row.at("created").as_array())
            descriptors += size_of(object_meta(next.at("objects").at(key.as_string())));
        const auto &declared = row.at("begin").at("declared");
        need(ready_bytes <= declared.at("bytes").as_u64() &&
                 count(row.at("received")) + count(row.at("reused")) <=
                     declared.at("objects").as_u64() &&
                 meta + descriptors <= terminal_reserve,
             "capacity_exceeded");
        objects +=
            declared.at("objects").as_u64() - count(row.at("received")) - count(row.at("reused"));
        entries += row.at("begin").at("index").at("addedCount").as_u64();
        reserved +=
            declared.at("bytes").as_u64() - ready_bytes + terminal_reserve - meta - descriptors;
        staging_bytes += declared.at("bytes").as_u64() - reused_bytes;
    }
    if (!next.at("root").is_null())
        retained += size_of(next.at("root"));
    return Json::object(
        {{"limits", limits_json(options.limits)},
         {"used", Json::object({{"activeTransfers", Json(active)},
                                {"stagingBytes", Json(staging_bytes)},
                                {"receiptEntries", number(count(next.at("entries")))},
                                {"transferFacts", number(next.at("transfers").as_object().size())},
                                {"objects", number(next.at("objects").as_object().size())},
                                {"retainedBytes", Json(retained)},
                                {"reservedBytes", Json(reserved)},
                                {"reservedObjects", Json(objects)},
                                {"reservedReceiptEntries", Json(entries)}})}});
}
std::uint64_t FileStore::Impl::write_reserve(const Json &next) {
    std::uint64_t reserve = 0;
    for (const auto &item : next.at("transfers").as_object()) {
        const auto &row = item.second;
        if (text(row.at("transfer"), "status") == "staging")
            reserve += 3 * (row.at("begin").at("declared").at("objects").as_u64() -
                            count(row.at("received")) - count(row.at("reused"))) +
                       4;
    }
    for (const auto &item : next.at("journal").as_object())
        if (item.second.at("receipt").is_null()) {
            const auto &credit = item.second.at("credit");
            const auto *row =
                credit.is_null() ? nullptr : next.at("transfers").find(text(credit, "transferId"));
            if (row && text(row->at("transfer"), "status") == "staging" &&
                (text(credit, "target") == "commit" || !available(*row, text(credit, "target"))))
                --reserve;
            else
                ++reserve;
        }
    return reserve;
}
void FileStore::Impl::physical_capacity(const Json &next) {
    std::uint64_t pending_objects = 0, pending_bytes = 0, pending_journal = 0;
    for (const auto &item : next.at("transfers").as_object()) {
        const auto &row = item.second;
        if (text(row.at("transfer"), "status") != "staging")
            continue;
        pending_objects += row.at("begin").at("declared").at("objects").as_u64() -
                           count(row.at("received")) - count(row.at("reused")) + 1;
        pending_bytes += terminal_reserve + row.at("begin").at("declared").at("bytes").as_u64();
        for (const char *set : {"received", "reused"})
            for (const auto &key : row.at(set).as_array())
                pending_bytes -= next.at("objects").at(key.as_string()).at("byteLength").as_u64();
    }
    for (const auto &item : next.at("journal").as_object())
        if (item.second.at("receipt").is_null()) {
            ++pending_journal;
            const auto &credit = item.second.at("credit");
            const auto *row =
                credit.is_null() ? nullptr : next.at("transfers").find(text(credit, "transferId"));
            if (row && text(row->at("transfer"), "status") == "staging" &&
                (text(credit, "target") == "commit" || !available(*row, text(credit, "target"))))
                --pending_objects;
        }
    if (next.at("writes").as_u64() + write_reserve(next) >= key_write_limit ||
        next.dump().size() + pending_bytes * 2 + pending_objects * 70000 + pending_journal * 40000 +
                std::strlen(magic) + 28 >
            options.limits.max_file_bytes ||
        next.at("journal").as_object().size() + pending_objects >
            options.limits.max_journal_entries)
        fail(ErrorCode::capacity, "capacity_exceeded");
}

void FileStore::Impl::validate_state(const Json &next) {
    need(next.is_object());
    const auto *readonly = next.find("readOnlyCopy");
    need(next.as_object().size() == (readonly ? 13U : 12U) &&
         (!readonly || equal(*readonly, Json(true))));
    need(text(next, "format") == magic && equal(next.at("identity"), options.identity) &&
         equal(next.at("limits"), limits_json(options.limits)) &&
         text(next, "keyId") == options.key_id && next.at("writes").as_u64() <= key_write_limit);
    validate("Digest", next.at("budgetId"));
    need(next.at("physicalLimits").as_object().size() == 2 &&
         next.at("physicalLimits").at("fileBytes").as_u64() == options.limits.max_file_bytes &&
         next.at("physicalLimits").at("journalEntries").as_u64() ==
             options.limits.max_journal_entries);
    need(next.at("objects").is_object() && next.at("entries").is_array() &&
         next.at("transfers").is_object() && next.at("journal").is_object());
    for (const auto &item : next.at("objects").as_object()) {
        const auto &obj = item.second;
        need(obj.is_object() && obj.as_object().size() == 4 && object_key(obj) == item.first);
        need(text(obj, "kind") == "body-page" || text(obj, "kind") == "index-page" ||
             text(obj, "kind") == "body-block" || text(obj, "kind") == "receipt-value");
        need(obj.at("byteLength").as_u64() > 0 && obj.at("byteLength").as_u64() <= block_bytes);
        object_bytes(next, item.first);
    }
    std::vector<std::string> primary, secondary;
    for (const auto &row : next.at("entries").as_array()) {
        need(row.as_object().size() == 2);
        const auto &entry = row.at("entry");
        validate("Entry", entry);
        for (const auto &pair :
             {std::make_pair("primaryKey", &primary), std::make_pair("secondaryKey", &secondary)}) {
            const auto key = text(entry, pair.first);
            need(std::find(pair.second->begin(), pair.second->end(), key) == pair.second->end());
            pair.second->push_back(key);
        }
        need(object_bytes(next, object_key("receipt-value", text(entry.at("value"), "sha256")))
                 .size() == entry.at("value").at("byteLength").as_u64());
    }
    const auto index = index_hash(next, options.identity);
    if (!next.at("root").is_null()) {
        const auto &root = next.at("root");
        validate("Root", root);
        need(equal(root.at("index"), index));
        need(equal(root, make_root(options.identity, root.at("body"), index,
                                   sequence(root.at("generation")))));
        full_body(next, root.at("body"));
    } else
        need(count(next.at("entries")) == 0);
    for (const auto &item : next.at("transfers").as_object()) {
        const auto &row = item.second, &begin = row.at("begin"), &transfer = row.at("transfer");
        need(row.as_object().size() == 7);
        validate("BeginRequest", begin);
        valid_plan(begin);
        validate("Transfer", transfer);
        need(text(begin, "transferId") == item.first &&
             text(transfer, "transferId") == item.first &&
             equal(begin.at("intentSha256"), transfer.at("intentSha256")));
        owner_valid(row.at("owner"));
        for (const char *field : {"applicationScopeId", "endUserId"})
            need(equal(row.at("owner").at("scope").at(field),
                       options.identity.at("scope").at(field)));
        for (const char *field : {"sourceId", "sourceGeneration", "domainKey"})
            need(equal(begin.at(field), options.identity.at(field)));
        const auto status = text(transfer, "status");
        need(status != "unknown");
        for (const char *set : {"received", "reused", "created"}) {
            std::vector<std::string> seen;
            for (const auto &key : row.at(set).as_array()) {
                need(next.at("objects").contains(key.as_string()) &&
                     std::find(seen.begin(), seen.end(), key.as_string()) == seen.end());
                seen.push_back(key.as_string());
            }
        }
        for (const auto &key : row.at("received").as_array())
            need(!has(row.at("reused"), key.as_string()));
        if (status == "staging") {
            need(equal(begin.at("expected"), expected_of(row.at("baseRoot"))));
            if (!row.at("baseRoot").is_null())
                full_body(next, row.at("baseRoot").at("body"));
            need(equal(transfer.at("progress"), progress(next, row)));
        } else {
            need(row.at("baseRoot").is_null());
            if (status == "committed") {
                const auto &root = transfer.at("result");
                need(equal(root.at("body"), begin.at("body")) &&
                     equal(root, make_root(options.identity, root.at("body"), root.at("index"),
                                           sequence(root.at("generation")))));
            }
        }
    }
    for (const auto &item : next.at("journal").as_object()) {
        const auto &row = item.second;
        need(row.as_object().size() == 4);
        auto op = take(executor::detail::operation(row.at("operation")));
        take(executor::validate_operation(op));
        const auto &credit = row.at("credit");
        if (!credit.is_null()) {
            need(credit.as_object().size() == 2 && op.request.operation == "tool.invoke" &&
                 text(op.request.args, "name") == tool_name);
            const auto request =
                take(executor::parse_tool_arguments(text(op.request.args, "argsJson")));
            need(equal(credit.at("transferId"), request.at("transferId")) &&
                 next.at("transfers").contains(text(credit, "transferId")));
            const auto &plan = next.at("transfers").at(text(credit, "transferId"));
            need(equal(plan.at("owner"), row.at("owner")) &&
                 equal(plan.at("begin").at("intentSha256"), request.at("intentSha256")));
            need((text(request, "action") == "commit" && text(credit, "target") == "commit") ||
                 (text(request, "action") == "put" &&
                  text(credit, "target") == object_key(request)));
            if (row.at("receipt").is_null())
                for (const auto &other : next.at("journal").as_object()) {
                    if (other.first == item.first || !other.second.at("receipt").is_null())
                        continue;
                    need(!equal(credit, other.second.at("credit")));
                }
        }
        need(journal_key(op) == item.first && equal(owner_of(op), row.at("owner")));
        for (const char *field : {"applicationScopeId", "endUserId"})
            need(equal(row.at("owner").at("scope").at(field),
                       options.identity.at("scope").at(field)));
        if (!row.at("receipt").is_null())
            take(
                executor::validate_receipt(op, take(executor::detail::receipt(row.at("receipt")))));
    }
    const auto capacity = capacity_of(next);
    const auto &used = capacity.at("used"), &limits = capacity.at("limits");
    for (const char *field : {"activeTransfers", "stagingBytes", "transferFacts"})
        need(used.at(field).as_u64() <= limits.at(field).as_u64(), "capacity_exceeded");
    for (const auto &pair : {std::make_pair("objects", "reservedObjects"),
                             std::make_pair("receiptEntries", "reservedReceiptEntries"),
                             std::make_pair("retainedBytes", "reservedBytes")})
        need(used.at(pair.first).as_u64() + used.at(pair.second).as_u64() <=
                 limits.at(pair.first).as_u64(),
             "capacity_exceeded");
    need(next.at("journal").as_object().size() <= options.limits.max_journal_entries,
         "capacity_exceeded");
}

Json FileStore::Impl::dispatch(Json &next, const Json &request, const Json &owner, bool &changed) {
    auto response = Json::object({{"contract", protocol},
                                  {"sourceId", request.at("sourceId")},
                                  {"sourceGeneration", request.at("sourceGeneration")},
                                  {"domainKey", request.at("domainKey")},
                                  {"action", request.at("action")}});
    const auto action = text(request, "action");
    if (action == "head") {
        response.set("root", next.at("root"));
        response.set("capacity", capacity_of(next));
        return response;
    }
    if (action == "read" || action == "lookup") {
        need(!next.at("root").is_null() &&
                 equal(request.at("commitRoot"), next.at("root").at("commitRoot")),
             "revision_conflict");
        const auto &root = next.at("root");
        response.set("commitRoot", root.at("commitRoot"));
        if (action == "lookup") {
            response.set("indexRoot", root.at("index").at("root"));
            response.set("indexCount", root.at("index").at("count"));
            const auto *entry = entry_for(
                next, text(request.at("key"), "kind") == "primary" ? "primaryKey" : "secondaryKey",
                text(request.at("key"), "digest"));
            Json result;
            if (entry) {
                result = *entry;
                result.set("base64", crypto::base64_encode(object_bytes(
                                         next, object_key("receipt-value",
                                                          text(entry->at("value"), "sha256")))));
            }
            response.set("entry", result);
            return response;
        }
        const auto part = text(request, "part");
        response.set("part", part);
        std::string bytes;
        if (part == "body-page") {
            const auto index = request.at("pageIndex").as_u64();
            need(index < count(root.at("body").at("pageHashes")), "invalid_request");
            bytes =
                object_bytes(next, object_key("body-page", root.at("body")
                                                               .at("pageHashes")
                                                               .at(static_cast<std::size_t>(index))
                                                               .as_string()));
            response.set("pageIndex", request.at("pageIndex"));
        } else {
            const auto full = full_body(next, root.at("body"));
            const auto offset = request.at("offset").as_u64();
            need(offset <= full.size(), "invalid_request");
            bytes = full.substr(static_cast<std::size_t>(offset),
                                static_cast<std::size_t>(request.at("length").as_u64()));
            response.set("offset", request.at("offset"));
            response.set("bodyEtag", root.at("body").at("sha256"));
            response.set("nextOffset", Json(offset + bytes.size()));
            response.set("complete", offset + bytes.size() == full.size());
        }
        response.set("byteLength", number(bytes.size()));
        response.set("base64", crypto::base64_encode(bytes));
        response.set("payloadDigest", hash(bytes));
        return response;
    }
    const auto id = text(request, "transferId");
    auto *row = next.at("transfers").find(id);
    if (row) {
        need(equal(request.at("intentSha256"), row->at("begin").at("intentSha256")),
             "request_conflict");
        if (!equal(row->at("owner"), owner)) {
            need(action == "query" && options.authorize_recovery &&
                     same_recovery_domain(row->at("owner"), owner),
                 "stale_generation");
            take(options.authorize_recovery(row->at("owner"), owner, id));
            check();
        }
        if (action == "begin")
            need(equal(request, row->at("begin")), "request_conflict");
        if (action == "query" || action == "begin" ||
            text(row->at("transfer"), "status") != "staging") {
            response.set("transfer", row->at("transfer"));
            return response;
        }
    } else if (action != "begin") {
        response.set("transfer", unknown(request));
        return response;
    }
    if (action == "begin") {
        valid_plan(request);
        auto transfer = unknown(request);
        transfer.set("status", "staging");
        auto value = Json::object({{"begin", request},
                                   {"owner", owner},
                                   {"baseRoot", next.at("root")},
                                   {"transfer", transfer},
                                   {"received", Json::array()},
                                   {"reused", Json::array()},
                                   {"created", Json::array()}});
        if (!equal(request.at("expected"), expected_of(next.at("root"))))
            rejected(value, "revision_conflict", next.at("root"));
        else {
            reuse_body(next, value);
            value.at("transfer").set("progress", progress(next, value));
        }
        next.at("transfers").set(id, value);
        row = &next.at("transfers").at(id);
        validate_state(next);
        physical_capacity(next);
        changed = true;
    } else if (action == "put") {
        const auto kind = text(request, "kind"), key = object_key(request);
        auto bytes = decode(request.at("base64"));
        need(bytes.size() == request.at("byteLength").as_u64() &&
                 hash(bytes) == text(request, "sha256"),
             "integrity_mismatch");
        if (available(*row, key)) {
            need(object_bytes(next, key) == bytes, "integrity_mismatch");
            response.set("transfer", row->at("transfer"));
            return response;
        }
        if (kind == "body-page" || kind == "index-page") {
            const bool body = kind == "body-page";
            const auto &plan = row->at("begin").at(body ? "body" : "index");
            const auto p = take(Json::parse(bytes, JsonLimits{block_bytes, 8, 4096}));
            need(p.is_object() && p.as_object().size() == 4 && encode(p) == bytes &&
                     p.at("version").as_u64() == 1 && text(p, "kind") == kind,
                 "invalid_request");
            const auto index = p.at("index").as_u64();
            need(index < count(plan.at("pageHashes")) &&
                     equal(request.at("sha256"),
                           plan.at("pageHashes").at(static_cast<std::size_t>(index))),
                 "invalid_request");
            const auto &items = p.at(body ? "refs" : "entries");
            const auto per_page = body ? 64U : 32U;
            need(count(items) ==
                     std::min<std::uint64_t>(per_page,
                                             plan.at(body ? "blockCount" : "entryCount").as_u64() -
                                                 index * per_page),
                 "invalid_request");
            std::size_t n = static_cast<std::size_t>(index) * per_page;
            for (const auto &item : items.as_array()) {
                validate(body ? "Ref" : "Entry", item);
                if (body)
                    need(item.at("byteLength").as_u64() ==
                             std::min<std::uint64_t>(block_bytes, plan.at("byteLength").as_u64() -
                                                                      n * block_bytes),
                         "invalid_request");
                ++n;
            }
        } else {
            bool listed = false;
            const auto refs = planned_refs(next, *row, kind == "body-block" ? "body" : "index");
            for (const auto &item : refs.as_array()) {
                const auto &ref = kind == "body-block" ? item : item.at("value");
                if (equal(ref.at("sha256"), request.at("sha256"))) {
                    need(equal(ref.at("byteLength"), request.at("byteLength")), "invalid_request");
                    listed = true;
                }
            }
            need(listed, "invalid_request");
        }
        if (next.at("objects").contains(key))
            need(object_bytes(next, key) == bytes, "integrity_mismatch");
        else {
            auto object = object_meta(request);
            object.set("base64", request.at("base64"));
            next.at("objects").set(key, object);
            add(row->at("created"), key);
        }
        add(row->at("received"), key);
        try {
            reuse_body(next, *row);
            reuse_values(next, *row);
            bool complete_pages = true;
            const auto pages = planned_pages(*row);
            for (const auto &page_key : pages.as_array())
                complete_pages = complete_pages && available(*row, page_key.as_string());
            if (complete_pages) {
                const auto objects = described_objects(next, *row);
                std::uint64_t total = 0;
                for (const auto &item : objects.as_object())
                    total += item.second.as_u64();
                need(objects.as_object().size() ==
                             row->at("begin").at("declared").at("objects").as_u64() &&
                         total == row->at("begin").at("declared").at("bytes").as_u64(),
                     "invalid_request");
                verify_index_plan(next, *row);
            }
            row->at("transfer").set("progress", progress(next, *row));
            capacity_of(next);
        } catch (const Failure &failure) {
            if (failure.error.code != ErrorCode::conflict)
                throw;
            rejected(*row,
                     failure.error.message == "request_conflict" ? "request_conflict"
                                                                 : "invalid_request",
                     next.at("root"));
        }
        changed = true;
    } else if (action == "commit") {
        if (!equal(row->at("begin").at("expected"), expected_of(next.at("root"))))
            rejected(*row, "revision_conflict", next.at("root"));
        else {
            const auto objects = described_objects(next, *row);
            for (const auto &item : objects.as_object()) {
                need(available(*row, item.first), "invalid_request");
                need(object_bytes(next, item.first).size() == item.second.as_u64(),
                     "integrity_mismatch");
            }
            std::uint64_t declared_bytes = 0;
            for (const auto &item : objects.as_object())
                declared_bytes += item.second.as_u64();
            try {
                need(objects.as_object().size() ==
                             row->at("begin").at("declared").at("objects").as_u64() &&
                         declared_bytes == row->at("begin").at("declared").at("bytes").as_u64(),
                     "invalid_request");
                full_body(next, row->at("begin").at("body"));
                verify_index_plan(next, *row);
            } catch (const Failure &failure) {
                if (failure.error.code != ErrorCode::conflict)
                    throw;
                const auto code = failure.error.message == "request_conflict" ? "request_conflict"
                                  : failure.error.message == "integrity_mismatch"
                                      ? "integrity_mismatch"
                                      : "invalid_request";
                rejected(*row, code, next.at("root"));
                changed = true;
                response.set("transfer", row->at("transfer"));
                return response;
            }
            const auto entries = planned_refs(next, *row, "index");
            auto metadata = size_of(row_material(*row));
            for (const auto &key : row->at("created").as_array())
                metadata += size_of(object_meta(next.at("objects").at(key.as_string())));
            for (const auto &entry : entries.as_array())
                if (!entry_for(next, "primaryKey", text(entry, "primaryKey"))) {
                    auto item =
                        Json::object({{"ordinal", std::to_string(count(next.at("entries")) + 1)},
                                      {"entry", entry}});
                    metadata += size_of(item);
                    next.at("entries").as_array().push_back(item);
                }
            const auto generation =
                next.at("root").is_null() ? 1 : sequence(next.at("root").at("generation")) + 1;
            const auto root = make_root(options.identity, row->at("begin").at("body"),
                                        index_hash(next, options.identity), generation);
            next.set("root", root);
            row->set("baseRoot", Json());
            row->at("transfer").set("status", "committed");
            row->at("transfer").set("progress", Json());
            row->at("transfer").set("result", root);
            need(metadata + 2 * size_of(root) <= terminal_reserve, "capacity_exceeded");
        }
        changed = true;
    }
    response.set("transfer", row->at("transfer"));
    return response;
}
