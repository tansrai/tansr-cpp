#pragma once

#include "tansr/error.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tansr::crypto {

using Aes256Key = std::array<std::uint8_t, 32>;
using GcmNonce = std::array<std::uint8_t, 12>;
using GcmTag = std::array<std::uint8_t, 16>;

struct AesGcmCiphertext {
    std::vector<std::uint8_t> ciphertext;
    GcmTag tag{};
};

class Sha256 {
  public:
    [[nodiscard]] static TANSR_API Result<Sha256> create();
    TANSR_API ~Sha256();
    TANSR_API Sha256(Sha256 &&) noexcept;
    TANSR_API Sha256 &operator=(Sha256 &&) noexcept;
    Sha256(const Sha256 &) = delete;
    Sha256 &operator=(const Sha256 &) = delete;

    [[nodiscard]] TANSR_API Result<void> update(std::string_view bytes);
    // 对当前状态取快照，不终结原状态；调用者负责串行化同一实例的访问。
    [[nodiscard]] TANSR_API Result<std::string> hex() const;

  private:
    struct Impl;
    explicit Sha256(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// 所有 string_view 均按原始字节处理，包括嵌入的 NUL。
[[nodiscard]] TANSR_API Result<std::string> sha256_hex(std::string_view bytes);
[[nodiscard]] TANSR_API Result<std::vector<std::uint8_t>> random_bytes(std::size_t size);

// 同一密钥下 nonce 必须唯一；密钥归宿主所有，用量与轮换由档案层约束。
[[nodiscard]] TANSR_API Result<AesGcmCiphertext> aes256_gcm_encrypt(const Aes256Key &key,
                                                                    const GcmNonce &nonce,
                                                                    std::string_view plaintext,
                                                                    std::string_view aad = {});

// 仅在完整认证成功后交付明文；失败会清零内部暂态明文。
[[nodiscard]] TANSR_API Result<std::vector<std::uint8_t>>
aes256_gcm_decrypt(const Aes256Key &key, const GcmNonce &nonce, std::string_view ciphertext,
                   const GcmTag &tag, std::string_view aad = {});

[[nodiscard]] TANSR_API std::string base64_encode(std::string_view bytes);
// 只接受带必要填充的标准 canonical Base64，不接受空白、URL 字母表或非零尾位。
[[nodiscard]] TANSR_API Result<std::vector<std::uint8_t>> base64_decode(std::string_view encoded);

} // namespace tansr::crypto
