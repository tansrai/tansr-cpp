#include "tansr/crypto.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <openssl/rand.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace tansr::crypto {
namespace {

// 每次调用拥有独立上下文，不加载配置、不修改宿主默认 provider 或属性。
class OpenSslContext {
  public:
    OpenSslContext()
        : context_(OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free),
          provider_(context_ ? OSSL_PROVIDER_load(context_.get(), "default") : nullptr,
                    OSSL_PROVIDER_unload) {}

    ~OpenSslContext() {
        if (context_) {
            OPENSSL_thread_stop_ex(context_.get());
        }
    }

    OpenSslContext(const OpenSslContext &) = delete;
    OpenSslContext &operator=(const OpenSslContext &) = delete;
    explicit operator bool() const noexcept { return context_ && provider_; }
    OSSL_LIB_CTX *get() const noexcept { return context_.get(); }

  private:
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> context_;
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> provider_;
};

class SensitiveBytes {
  public:
    explicit SensitiveBytes(std::size_t size) : bytes(size) {}
    ~SensitiveBytes() {
        if (!bytes.empty()) {
            OPENSSL_cleanse(bytes.data(), bytes.size());
        }
    }
    SensitiveBytes(const SensitiveBytes &) = delete;
    SensitiveBytes &operator=(const SensitiveBytes &) = delete;
    std::vector<std::uint8_t> bytes;
};

// 流式对象可在串行的不同线程上调用，离开每次调用时释放该线程的私有上下文资源。
class CryptoThreadScope {
  public:
    explicit CryptoThreadScope(OSSL_LIB_CTX *context) : context_(context) {}
    ~CryptoThreadScope() {
        if (context_) {
            OPENSSL_thread_stop_ex(context_);
        }
    }
    CryptoThreadScope(const CryptoThreadScope &) = delete;
    CryptoThreadScope &operator=(const CryptoThreadScope &) = delete;

  private:
    OSSL_LIB_CTX *context_;
};

Error crypto_error() { return {ErrorCode::crypto, "Cryptographic operation failed"}; }
Error authentication_error() { return {ErrorCode::crypto, "Ciphertext authentication failed"}; }
Error base64_error() { return {ErrorCode::invalid_input, "Invalid canonical Base64"}; }

bool valid_gcm_lengths(std::size_t bytes, std::size_t aad_bytes) noexcept {
    // SP 800-38D: 明文最多 2^39 - 256 bit，AAD 最多 2^64 - 1 bit。
    constexpr std::uint64_t max_plaintext = (std::uint64_t{1} << 36) - 32;
    constexpr std::uint64_t max_aad = (std::numeric_limits<std::uint64_t>::max)() / 8;
    return bytes <= max_plaintext && aad_bytes <= max_aad &&
           bytes <= (std::numeric_limits<std::size_t>::max)() - EVP_MAX_BLOCK_LENGTH;
}

bool initialize_gcm(EVP_CIPHER_CTX *context, const EVP_CIPHER *cipher, const Aes256Key &key,
                    const GcmNonce &nonce, bool encrypt) {
    return EVP_CipherInit_ex2(context, cipher, nullptr, nullptr, encrypt ? 1 : 0, nullptr) == 1 &&
           EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_AEAD_SET_IVLEN, static_cast<int>(nonce.size()),
                               nullptr) == 1 &&
           EVP_CipherInit_ex2(context, nullptr, key.data(), nonce.data(), -1, nullptr) == 1;
}

bool update_gcm(EVP_CIPHER_CTX *context, std::string_view input, std::uint8_t *output) {
    constexpr std::size_t chunk_limit = 1024 * 1024;
    std::size_t offset = 0;
    while (offset < input.size()) {
        const auto chunk = static_cast<int>((std::min)(input.size() - offset, chunk_limit));
        int written = 0;
        if (EVP_CipherUpdate(context, output ? output + offset : nullptr, &written,
                             reinterpret_cast<const unsigned char *>(input.data() + offset),
                             chunk) != 1 ||
            (output && written != chunk)) {
            return false;
        }
        offset += static_cast<std::size_t>(chunk);
    }
    return true;
}

int base64_value(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }
    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }
    if (value == '+') {
        return 62;
    }
    return value == '/' ? 63 : -1;
}

} // namespace

struct Sha256::Impl {
    Impl()
        : algorithm(library ? EVP_MD_fetch(library.get(), "SHA256", "provider=default") : nullptr,
                    EVP_MD_free),
          context(EVP_MD_CTX_new(), EVP_MD_CTX_free) {}

    OpenSslContext library;
    std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> algorithm;
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context;
    std::uint64_t bytes{0};
    bool failed{false};
};

Sha256::Sha256(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Sha256::~Sha256() = default;
Sha256::Sha256(Sha256 &&) noexcept = default;
Sha256 &Sha256::operator=(Sha256 &&) noexcept = default;

Result<Sha256> Sha256::create() {
    auto impl = std::make_unique<Impl>();
    CryptoThreadScope thread_scope(impl->library.get());
    if (!impl->library || !impl->algorithm || !impl->context ||
        EVP_DigestInit_ex2(impl->context.get(), impl->algorithm.get(), nullptr) != 1) {
        return crypto_error();
    }
    return Sha256(std::move(impl));
}

Result<void> Sha256::update(std::string_view bytes) {
    if (!impl_) {
        return Error{ErrorCode::closed, "SHA-256 state has been moved"};
    }
    CryptoThreadScope thread_scope(impl_->library.get());
    if (impl_->failed) {
        return crypto_error();
    }
    constexpr auto max_bytes = (std::numeric_limits<std::uint64_t>::max)() / 8;
    if (bytes.size() > max_bytes - impl_->bytes) {
        return Error{ErrorCode::capacity, "SHA-256 input exceeds its length limit"};
    }
    if (!bytes.empty() && EVP_DigestUpdate(impl_->context.get(), bytes.data(), bytes.size()) != 1) {
        impl_->failed = true;
        return crypto_error();
    }
    impl_->bytes += bytes.size();
    return {};
}

Result<std::string> Sha256::hex() const {
    if (!impl_) {
        return Error{ErrorCode::closed, "SHA-256 state has been moved"};
    }
    CryptoThreadScope thread_scope(impl_->library.get());
    if (impl_->failed) {
        return crypto_error();
    }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> snapshot(EVP_MD_CTX_new(),
                                                                     EVP_MD_CTX_free);
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (!snapshot || EVP_MD_CTX_copy_ex(snapshot.get(), impl_->context.get()) != 1 ||
        EVP_DigestFinal_ex(snapshot.get(), digest.data(), &length) != 1 || length != 32) {
        return crypto_error();
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t index = 0; index < 32; ++index) {
        result[index * 2] = hex[digest[index] >> 4];
        result[index * 2 + 1] = hex[digest[index] & 15];
    }
    return result;
}

Result<std::string> sha256_hex(std::string_view bytes) {
    auto state = Sha256::create();
    if (!state) {
        return state.error();
    }
    auto updated = state.value().update(bytes);
    if (!updated) {
        return updated.error();
    }
    return state.value().hex();
}

Result<std::vector<std::uint8_t>> random_bytes(std::size_t size) {
    if (size == 0) {
        return std::vector<std::uint8_t>{};
    }
    OpenSslContext library;
    if (!library) {
        return crypto_error();
    }
    // 直接消费私有 context 的 DRBG，避免旧式全局 RAND_METHOD 覆盖 RAND_bytes_ex。
    auto *generator = RAND_get0_private(library.get());
    if (!generator) {
        return crypto_error();
    }
    SensitiveBytes result(size);
    if (EVP_RAND_generate(generator, result.bytes.data(), result.bytes.size(), 256, 0, nullptr,
                          0) != 1) {
        return crypto_error();
    }
    return std::move(result.bytes);
}

Result<AesGcmCiphertext> aes256_gcm_encrypt(const Aes256Key &key, const GcmNonce &nonce,
                                            std::string_view plaintext, std::string_view aad) {
    if (!valid_gcm_lengths(plaintext.size(), aad.size())) {
        return Error{ErrorCode::capacity, "AES-GCM input exceeds its length limit"};
    }
    OpenSslContext library;
    if (!library) {
        return crypto_error();
    }
    std::unique_ptr<EVP_CIPHER, decltype(&EVP_CIPHER_free)> cipher(
        EVP_CIPHER_fetch(library.get(), "AES-256-GCM", "provider=default"), EVP_CIPHER_free);
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(),
                                                                            EVP_CIPHER_CTX_free);
    if (!cipher || !context || !initialize_gcm(context.get(), cipher.get(), key, nonce, true)) {
        return crypto_error();
    }
    AesGcmCiphertext result;
    result.ciphertext.resize(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int final_length = 0;
    if (!update_gcm(context.get(), aad, nullptr) ||
        !update_gcm(context.get(), plaintext, result.ciphertext.data()) ||
        EVP_CipherFinal_ex(context.get(), result.ciphertext.data() + plaintext.size(),
                           &final_length) != 1 ||
        final_length != 0 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_GET_TAG,
                            static_cast<int>(result.tag.size()), result.tag.data()) != 1) {
        return crypto_error();
    }
    result.ciphertext.resize(plaintext.size());
    return result;
}

Result<std::vector<std::uint8_t>> aes256_gcm_decrypt(const Aes256Key &key, const GcmNonce &nonce,
                                                     std::string_view ciphertext, const GcmTag &tag,
                                                     std::string_view aad) {
    if (!valid_gcm_lengths(ciphertext.size(), aad.size())) {
        return Error{ErrorCode::capacity, "AES-GCM input exceeds its length limit"};
    }
    OpenSslContext library;
    if (!library) {
        return crypto_error();
    }
    std::unique_ptr<EVP_CIPHER, decltype(&EVP_CIPHER_free)> cipher(
        EVP_CIPHER_fetch(library.get(), "AES-256-GCM", "provider=default"), EVP_CIPHER_free);
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(),
                                                                            EVP_CIPHER_CTX_free);
    if (!cipher || !context || !initialize_gcm(context.get(), cipher.get(), key, nonce, false)) {
        return crypto_error();
    }
    auto expected_tag = tag;
    if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_TAG,
                            static_cast<int>(expected_tag.size()), expected_tag.data()) != 1) {
        return crypto_error();
    }
    SensitiveBytes plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int final_length = 0;
    if (!update_gcm(context.get(), aad, nullptr) ||
        !update_gcm(context.get(), ciphertext, plaintext.bytes.data()) ||
        EVP_CipherFinal_ex(context.get(), plaintext.bytes.data() + ciphertext.size(),
                           &final_length) != 1 ||
        final_length != 0) {
        return authentication_error();
    }
    plaintext.bytes.resize(ciphertext.size());
    return std::move(plaintext.bytes);
}

std::string base64_encode(std::string_view bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto groups = bytes.size() / 3 + (bytes.size() % 3 != 0 ? 1 : 0);
    std::string result;
    if (groups > result.max_size() / 4) {
        throw std::length_error("Base64 output exceeds its length limit");
    }
    result.reserve(groups * 4);
    for (std::size_t offset = 0; offset < bytes.size();) {
        const auto remaining = bytes.size() - offset;
        const auto first = static_cast<unsigned char>(bytes[offset]);
        const auto second = remaining > 1 ? static_cast<unsigned char>(bytes[offset + 1]) : 0U;
        const auto third = remaining > 2 ? static_cast<unsigned char>(bytes[offset + 2]) : 0U;
        result.push_back(alphabet[first >> 2]);
        result.push_back(alphabet[((first & 3U) << 4) | (second >> 4)]);
        result.push_back(remaining > 1 ? alphabet[((second & 15U) << 2) | (third >> 6)] : '=');
        result.push_back(remaining > 2 ? alphabet[third & 63U] : '=');
        offset += (std::min)(remaining, std::size_t{3});
    }
    return result;
}

Result<std::vector<std::uint8_t>> base64_decode(std::string_view encoded) {
    if (encoded.size() % 4 != 0) {
        return base64_error();
    }
    std::vector<std::uint8_t> result;
    result.reserve(encoded.size() / 4 * 3);
    for (std::size_t offset = 0; offset < encoded.size(); offset += 4) {
        const auto first = base64_value(encoded[offset]);
        const auto second = base64_value(encoded[offset + 1]);
        const auto third = base64_value(encoded[offset + 2]);
        const auto fourth = base64_value(encoded[offset + 3]);
        const bool pad_two = encoded[offset + 2] == '=';
        const bool pad_one = encoded[offset + 3] == '=';
        const bool last = encoded.size() - offset == 4;
        if (first < 0 || second < 0 || (third < 0 && !pad_two) || (fourth < 0 && !pad_one) ||
            (pad_two && (!pad_one || !last || (second & 15) != 0)) ||
            (pad_one && (!last || (!pad_two && (third & 3) != 0)))) {
            return base64_error();
        }
        result.push_back(static_cast<std::uint8_t>((first << 2) | (second >> 4)));
        if (!pad_two) {
            result.push_back(static_cast<std::uint8_t>((second << 4) | (third >> 2)));
        }
        if (!pad_one) {
            result.push_back(static_cast<std::uint8_t>((third << 6) | fourth));
        }
    }
    return result;
}

} // namespace tansr::crypto
