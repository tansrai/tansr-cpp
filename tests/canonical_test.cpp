#include <tansr/canonical.hpp>
#include <tansr/crypto.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using tansr::Json;
namespace canonical = tansr::canonical;
namespace crypto = tansr::crypto;
std::size_t checks = 0;
void check(bool condition, const std::string &label) {
    ++checks;
    if (!condition)
        throw std::runtime_error(label);
}
template <class T> T required(tansr::Result<T> result, const std::string &label) {
    if (!result)
        throw std::runtime_error(label + ": " + result.error().message);
    return std::move(result).value();
}
Json fixture(const char *name) {
#ifdef TANSR_CONTRACT_DIR
    const std::filesystem::path directory(TANSR_CONTRACT_DIR);
#elif defined(TANSR_SOURCE_DIR)
    const std::filesystem::path root(TANSR_SOURCE_DIR);
    const auto directory = root / "contract";
#else
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto directory = root / "contract";
#endif
    std::ifstream input(directory / name, std::ios::binary);
    if (!input)
        throw std::runtime_error(std::string("missing frozen fixture: ") + name);
    const std::string bytes{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
    return required(Json::parse(bytes), name);
}
std::string hex(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const char character : bytes) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}
std::string vector_input(const Json &vector) {
    if (const auto *generator = vector.find("generator")) {
        check(generator->at("kind").as_string() == "flat-array", "known generator");
        const auto &element = generator->at("element").as_string();
        const auto count = generator->at("count").as_u64();
        std::string result = "[";
        for (std::uint64_t index = 0; index < count; ++index) {
            if (index != 0)
                result.push_back(',');
            result += element;
        }
        if (const auto *tail = generator->find("tail"); tail && !tail->as_string().empty()) {
            if (count != 0)
                result.push_back(',');
            result += tail->as_string();
        }
        result.push_back(']');
        return result;
    }
    const auto &input = vector.at("input").as_string();
    const auto &kind = vector.at("inputKind").as_string();
    if (kind == "utf8-text")
        return input;
    check(kind == "bytes", "known input kind");
    const auto bytes = required(crypto::base64_decode(input), "vector base64");
    return std::string(bytes.begin(), bytes.end());
}
void frozen_cross_vectors() {
    const auto data = fixture("canonical-cross-vectors.json");
    const auto &vectors = data.at("vectors").as_array();
    check(vectors.size() == 127, "frozen matrix must contain exactly 127 vectors");
    for (const auto &vector : vectors) {
        const auto &id = vector.at("id").as_string();
        const auto bytes = vector_input(vector);
        const auto *limit = vector.find("maxBytes");
        const auto maximum =
            limit ? static_cast<std::size_t>(limit->as_u64()) : std::size_t{262144};
        auto decoded = canonical::decode(bytes, maximum);
        const auto strict = canonical::parse_strict(bytes, maximum);
        if (vector.at("expect").as_string() == "reject") {
            check(!decoded, "decode accepted reject vector " + id);
            check(!strict, "strict accepted reject vector " + id);
            continue;
        }
        const auto value = required(std::move(decoded), "decode " + id);
        const auto encoded = required(canonical::encode_limited(value, maximum), "encode " + id);
        if (const auto *expected = vector.find("canonicalHex"))
            check(hex(encoded) == expected->as_string(), "canonical bytes " + id);
        else
            check(required(crypto::sha256_hex(encoded), "hash " + id) ==
                      vector.at("canonicalSha256").as_string(),
                  "canonical hash " + id);
        check(static_cast<bool>(strict) == (encoded == bytes), "canonical exactness " + id);
        if (strict)
            check(required(canonical::encode_limited(strict.value(), maximum), "roundtrip " + id) ==
                      encoded,
                  "strict roundtrip " + id);
    }
    std::cout << "frozen canonical vectors: " << vectors.size() << " passed\n";
}
void frozen_wire() {
    const auto data = fixture("sdk2-wire-v1.json");
    for (const auto &sample : data.at("metadata").as_array()) {
        const auto &expected = sample.at("utf8").as_string();
        check(required(canonical::encode(sample.at("value")), "metadata") == expected,
              "wire metadata bytes");
        const auto parsed = required(canonical::parse_strict(expected, 262144), "wire strict");
        check(required(canonical::encode(parsed), "wire roundtrip") == expected,
              "wire strict roundtrip");
    }
    for (const auto &sample : data.at("invalidMetadata").as_array()) {
        check(!canonical::decode(sample.as_string(), 262144), "invalid metadata decode");
        check(!canonical::parse_strict(sample.as_string(), 262144), "invalid metadata strict");
    }
    for (const auto &sample : data.at("pathIds").as_array())
        check(canonical::encode_path_segment(sample.at("id").as_string()) ==
                  sample.at("segment").as_string(),
              "wire path id");
    check(canonical::encode_path_segment("!~*'()-_.AZ09") == "!~*'()-_.AZ09",
          "path safe characters");
    check(canonical::encode_path_segment("?&#= +/%") == "%3F%26%23%3D%20%2B%2F%25",
          "path escaped characters");
    const auto &frame = data.at("sse").at("utf8").as_string();
    const auto start = frame.find("data: ");
    check(start != std::string::npos, "SSE data marker");
    auto body = frame.substr(start + 6);
    while (!body.empty() && body.back() == '\n')
        body.pop_back();
    check(required(canonical::encode(data.at("sse").at("frame")), "wire SSE") == body,
          "SSE exact canonical bytes");
}
void frozen_closure_and_domains() {
    const auto data = fixture("unified-v1.golden.json");
    std::size_t closures = 0;
    for (const auto &vector : data.at("vectors").as_array()) {
        if (vector.at("definition").as_string() != "CapabilityClosure" ||
            vector.at("expect").as_string() != "valid")
            continue;
        const auto &value = vector.at("value");
        const auto body =
            Json::object({{"authorizationRevision", value.at("authorizationRevision")},
                          {"domains", value.at("domains")},
                          {"operations", value.at("operations")}});
        check(required(canonical::digest(canonical::DOMAIN_CLOSURE, body), "closure digest") ==
                  value.at("closureId").as_string(),
              "frozen closure digest");
        ++closures;
    }
    check(closures >= 2, "closure golden count");
    const auto empty = Json::object();
    check(required(canonical::digest("a", empty), "domain a") !=
              required(canonical::digest("b", empty), "domain b"),
          "domain separation");
    check(!canonical::digest("", empty), "empty digest domain rejected");
    check(!canonical::digest(std::string("a\0b", 3), empty), "NUL digest domain rejected");
    check(required(canonical::digest_bytes("archive", R"({"a":1})"), "raw a") !=
              required(canonical::digest_bytes("archive", R"({ "a":1})"), "raw b"),
          "raw digest preserves whitespace");
}
void control_boundaries() {
    for (const auto *token : {"-0", "-1", "1.0", "1e0", "1E+02", "9007199254740992", "1e400"})
        check(!canonical::encode(required(Json::number(token), "ordinary number")),
              "control numeric rejection");
    check(!canonical::encode(Json::object({{"", 1}})), "empty control key");
    check(!canonical::encode(Json::object({{"汉", 1}})), "non-ASCII control key");
    check(!canonical::encode(Json::object({{"a b", 1}})), "space control key");
    check(!canonical::encode_limited(Json(), 3), "writer byte cap");
    check(required(canonical::encode_limited(Json(), 4), "exact null") == "null",
          "writer exact byte cap");
    const auto unicode = Json::object({{"a", "日本"}});
    check(!canonical::encode_limited(unicode, 13), "Unicode byte cap");
    check(required(canonical::encode_limited(unicode, 14), "exact Unicode").size() == 14,
          "Unicode cap counts UTF-8 bytes");
    auto deep = Json(0);
    for (std::size_t depth = 0; depth < 32; ++depth)
        deep = Json::array({std::move(deep)});
    check(static_cast<bool>(canonical::encode(deep)), "writer depth 32");
    deep = Json::array({std::move(deep)});
    check(!canonical::encode(deep), "writer depth 33");
    const auto exact = Json(Json::Array(canonical::MAX_NODES - 1, Json(0)));
    const auto bytes = required(canonical::encode(exact), "writer exact nodes");
    check(static_cast<bool>(Json::parse(bytes)), "parser exact nodes");
    const auto over = Json(Json::Array(canonical::MAX_NODES, Json(0)));
    check(!canonical::encode(over), "writer node overflow");
    check(!Json::parse(bytes.substr(0, bytes.size() - 1) + ",0]"), "parser node overflow");
    auto changed = Json::object({{"a", 1}});
    changed.as_object().emplace_back("a", 2);
    check(!canonical::encode(changed), "mutated duplicate object");
    auto text = Json("valid");
    text.as_string() = "\xff";
    check(!canonical::encode(text), "mutated invalid UTF-8");
}
void streaming_crypto() {
    const std::string empty_sha =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const std::string abc_sha = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    check(required(crypto::sha256_hex(""), "empty SHA") == empty_sha, "SHA empty vector");
    check(required(crypto::sha256_hex("abc"), "abc SHA") == abc_sha, "SHA abc vector");
    auto stream = required(crypto::Sha256::create(), "SHA create");
    check(required(stream.hex(), "empty snapshot") == empty_sha, "empty stream snapshot");
    check(static_cast<bool>(stream.update("a")), "stream update a");
    check(required(stream.hex(), "a snapshot") == required(crypto::sha256_hex("a"), "a SHA"),
          "intermediate snapshot");
    check(static_cast<bool>(stream.update("bc")), "stream update bc");
    check(required(stream.hex(), "abc snapshot") == abc_sha,
          "snapshot leaves original state usable");
    auto moved = std::move(stream);
    check(!stream.update("x") && !stream.hex(), "moved stream reports closed");
    check(required(moved.hex(), "moved snapshot") == abc_sha, "moved stream retains state");
    tansr::Result<std::string> threaded(tansr::Error{tansr::ErrorCode::internal, "not run"});
    std::thread worker([&] {
        const auto update = moved.update("d");
        threaded = update ? moved.hex() : tansr::Result<std::string>(update.error());
    });
    worker.join();
    check(required(std::move(threaded), "threaded SHA") ==
              required(crypto::sha256_hex("abcd"), "abcd SHA"),
          "stream can migrate serially across threads");
    check(required(moved.hex(), "owner snapshot") ==
              required(crypto::sha256_hex("abcd"), "abcd SHA"),
          "stream returns to owner after thread cleanup");
    const std::string binary("a\0\xff", 3);
    const auto roundtrip =
        required(crypto::base64_decode(crypto::base64_encode(binary)), "base64 roundtrip");
    check(std::string(roundtrip.begin(), roundtrip.end()) == binary, "binary base64 roundtrip");
    for (const auto *invalid : {"Zg", "Zh==", "Zm9=", "Zg==\n", "-w==", "===="})
        check(!crypto::base64_decode(invalid), "strict base64 rejects ambiguity");
    const crypto::Aes256Key key{};
    const crypto::GcmNonce nonce{};
    const auto encrypted =
        required(crypto::aes256_gcm_encrypt(key, nonce, "", ""), "AES-GCM empty");
    check(encrypted.ciphertext.empty(), "empty GCM ciphertext");
    const auto tag_bytes =
        std::string(reinterpret_cast<const char *>(encrypted.tag.data()), encrypted.tag.size());
    check(hex(tag_bytes) == "530f8afbc74536b9a963b4f1c4cb738b", "AES-256-GCM empty known vector");
    check(
        required(crypto::aes256_gcm_decrypt(key, nonce, "", encrypted.tag), "AES-GCM empty decrypt")
            .empty(),
        "authenticated empty plaintext");
    auto corrupt = encrypted.tag;
    corrupt[0] ^= 1;
    check(!crypto::aes256_gcm_decrypt(key, nonce, "", corrupt), "bad tag rejected");
}
} // namespace

int main() {
    try {
        frozen_cross_vectors();
        frozen_wire();
        frozen_closure_and_domains();
        control_boundaries();
        streaming_crypto();
        std::cout << "canonical/crypto: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "canonical failed: " << error.what() << '\n';
        return 1;
    }
}
