#include "tansr/crypto.hpp"
#include <cstdlib>
#include <iostream>
#include <openssl/evp.h>
#include <string>
#include <string_view>

namespace {
int checks = 0;
void check(bool condition, const char *name) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        std::exit(1);
    }
}
template <class Bytes> std::string hex(const Bytes &bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto value : bytes) {
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 15]);
    }
    return result;
}
std::string_view view(const std::vector<std::uint8_t> &bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
} // namespace

int main() {
    using namespace tansr::crypto;
    auto empty_hash = sha256_hex({});
    check(empty_hash && empty_hash.value() ==
                            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "empty SHA-256");
    auto abc_hash = sha256_hex("abc");
    check(abc_hash && abc_hash.value() ==
                          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 abc");

    Aes256Key key{};
    GcmNonce nonce{};
    auto empty = aes256_gcm_encrypt(key, nonce, {});
    check(empty && empty.value().ciphertext.empty() &&
              hex(empty.value().tag) == "530f8afbc74536b9a963b4f1c4cb738b",
          "empty AES-256-GCM vector");
    auto empty_plain = aes256_gcm_decrypt(key, nonce, {}, empty.value().tag);
    check(empty_plain && empty_plain.value().empty(), "empty authenticated plaintext");

    const std::string zeros(16, '\0');
    auto block = aes256_gcm_encrypt(key, nonce, zeros);
    check(block && hex(block.value().ciphertext) == "cea7403d4d606b6e074ec5d3baf39d18" &&
              hex(block.value().tag) == "d0d1c8a799996bf0265b98b5d48ab919",
          "AES-256-GCM block vector");
    auto plaintext =
        aes256_gcm_decrypt(key, nonce, view(block.value().ciphertext), block.value().tag);
    check(plaintext && view(plaintext.value()) == zeros, "GCM binary roundtrip");

    auto wrong_tag = block.value().tag;
    wrong_tag[0] ^= 1;
    check(!aes256_gcm_decrypt(key, nonce, view(block.value().ciphertext), wrong_tag),
          "wrong tag rejected");
    auto wrong_key = key;
    wrong_key[0] ^= 1;
    check(!aes256_gcm_decrypt(wrong_key, nonce, view(block.value().ciphertext), block.value().tag),
          "wrong key rejected");
    auto wrong_nonce = nonce;
    wrong_nonce[0] ^= 1;
    check(!aes256_gcm_decrypt(key, wrong_nonce, view(block.value().ciphertext), block.value().tag),
          "wrong nonce rejected");
    check(
        !aes256_gcm_decrypt(key, nonce, view(block.value().ciphertext), block.value().tag, "wrong"),
        "wrong AAD rejected");
    auto changed = block.value().ciphertext;
    changed[0] ^= 1;
    check(!aes256_gcm_decrypt(key, nonce, view(changed), block.value().tag),
          "tampered ciphertext rejected");
    changed = block.value().ciphertext;
    changed.pop_back();
    check(!aes256_gcm_decrypt(key, nonce, view(changed), block.value().tag),
          "truncated ciphertext rejected");

    const std::string binary("\0\xff\x80\n\r", 5);
    auto with_aad = aes256_gcm_encrypt(key, nonce, binary, "archive-v1");
    check(static_cast<bool>(with_aad), "AAD encryption");
    auto with_aad_plain = aes256_gcm_decrypt(key, nonce, view(with_aad.value().ciphertext),
                                             with_aad.value().tag, "archive-v1");
    check(with_aad_plain && view(with_aad_plain.value()) == binary, "AAD binary roundtrip");
    std::string large(2 * 1024 * 1024 + 17, 'x');
    auto chunked = aes256_gcm_encrypt(key, nonce, large, large);
    check(static_cast<bool>(chunked), "chunked GCM encryption");
    auto chunked_plain = aes256_gcm_decrypt(key, nonce, view(chunked.value().ciphertext),
                                            chunked.value().tag, large);
    check(chunked_plain && view(chunked_plain.value()) == large, "chunked GCM roundtrip");

    for (const auto &pair : {std::pair<const char *, const char *>{"", ""},
                             {"f", "Zg=="},
                             {"fo", "Zm8="},
                             {"foo", "Zm9v"},
                             {"foobar", "Zm9vYmFy"}}) {
        check(base64_encode(pair.first) == pair.second, "Base64 encode vector");
        auto decoded = base64_decode(pair.second);
        check(decoded && view(decoded.value()) == pair.first, "Base64 decode vector");
    }
    auto binary_base64 = base64_decode(base64_encode(binary));
    check(binary_base64 && view(binary_base64.value()) == binary, "Base64 binary roundtrip");
    for (const auto bad : {"Zg", "Zg=", "Zg===", "Zh==", "Zm9=", " Zg==", "Zg==\n", "====", "Z=00",
                           "Zm=v", "AA-A", "AA_A", "Zg==Zm9v"}) {
        check(!base64_decode(bad), "noncanonical Base64 rejected");
    }
    auto none = random_bytes(0);
    check(none && none.value().empty(), "zero random bytes");
    auto random_a = random_bytes(32);
    auto random_b = random_bytes(32);
    check(random_a && random_b && random_a.value().size() == 32 && random_b.value().size() == 32 &&
              random_a.value() != random_b.value(),
          "random output");
    auto random_large = random_bytes(128 * 1024);
    check(random_large && random_large.value().size() == 128 * 1024, "large random output");

    // 专用测试进程验证 SDK 调用没有改写宿主的默认 provider 属性。
    check(EVP_set_default_properties(nullptr, "provider=tansr-host-missing") == 1,
          "host policy set");
    auto isolated_hash = sha256_hex("abc");
    auto isolated_random = random_bytes(32);
    auto isolated_cipher = aes256_gcm_encrypt(key, nonce, binary, "archive-v1");
    check(isolated_hash && isolated_hash.value() == abc_hash.value() && isolated_random &&
              isolated_cipher,
          "private crypto context ignores host defaults");
    EVP_MD *host_digest = EVP_MD_fetch(nullptr, "SHA256", nullptr);
    check(host_digest == nullptr, "host policy preserved");
    EVP_MD_free(host_digest);
    check(EVP_set_default_properties(nullptr, nullptr) == 1, "host policy restored");
    std::cout << "crypto smoke: " << checks << " checks passed; 0 failed; 0 skipped\n";
}
