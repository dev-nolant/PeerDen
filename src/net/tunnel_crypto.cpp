#include "tunnel_crypto.h"

#ifdef TUNNGLE_COORD_TLS
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#endif

#include <cstring>

namespace tunngle {
namespace net {

namespace {
constexpr uint8_t kCipherVersion = 1;
constexpr size_t kNonceLen = 12;
constexpr size_t kTagLen = 16;
constexpr size_t kHandshakeProofLen = 16;
constexpr size_t kCipherHeaderLen = 1 + kNonceLen;
}

bool InitTunnelCryptoKey(const std::string& token, uint8_t out_key[32]) {
#ifdef TUNNGLE_COORD_TLS
    if (token.empty() || !out_key) return false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1;
    if (ok) ok = EVP_DigestUpdate(ctx, token.data(), token.size()) == 1;
    unsigned int out_len = 0;
    if (ok) ok = EVP_DigestFinal_ex(ctx, out_key, &out_len) == 1;
    EVP_MD_CTX_free(ctx);
    return ok && out_len == 32;
#else
    (void)token;
    (void)out_key;
    return false;
#endif
}

bool MakeHandshakeProof(const uint8_t key[32], uint8_t msg_type, uint64_t nonce, uint8_t out_proof[16]) {
#ifdef TUNNGLE_COORD_TLS
    if (!key || !out_proof) return false;
    uint8_t msg[16];
    std::memcpy(msg, "TUNNGLE", 7);
    msg[7] = msg_type;
    msg[8] = static_cast<uint8_t>((nonce >> 56) & 0xFF);
    msg[9] = static_cast<uint8_t>((nonce >> 48) & 0xFF);
    msg[10] = static_cast<uint8_t>((nonce >> 40) & 0xFF);
    msg[11] = static_cast<uint8_t>((nonce >> 32) & 0xFF);
    msg[12] = static_cast<uint8_t>((nonce >> 24) & 0xFF);
    msg[13] = static_cast<uint8_t>((nonce >> 16) & 0xFF);
    msg[14] = static_cast<uint8_t>((nonce >> 8) & 0xFF);
    msg[15] = static_cast<uint8_t>(nonce & 0xFF);
    unsigned int mac_len = 0;
    uint8_t mac[EVP_MAX_MD_SIZE];
    unsigned char* mac_ptr = HMAC(EVP_sha256(), key, 32, msg, sizeof(msg), mac, &mac_len);
    if (!mac_ptr || mac_len < kHandshakeProofLen) return false;
    std::memcpy(out_proof, mac, kHandshakeProofLen);
    return true;
#else
    (void)key;
    (void)msg_type;
    (void)nonce;
    (void)out_proof;
    return false;
#endif
}

bool VerifyHandshakeProof(const uint8_t key[32], uint8_t msg_type, uint64_t nonce, const uint8_t proof[16]) {
    uint8_t expected[kHandshakeProofLen];
    if (!MakeHandshakeProof(key, msg_type, nonce, expected)) return false;
    return std::memcmp(expected, proof, kHandshakeProofLen) == 0;
}

bool EncryptTunnelPayload(const uint8_t key[32], uint32_t nonce_prefix, uint64_t counter,
                          const uint8_t* plaintext, size_t plaintext_len,
                          std::vector<uint8_t>& out_ciphertext) {
#ifdef TUNNGLE_COORD_TLS
    if (!key || !plaintext || plaintext_len == 0) return false;
    uint8_t nonce[kNonceLen];
    nonce[0] = static_cast<uint8_t>((nonce_prefix >> 24) & 0xFF);
    nonce[1] = static_cast<uint8_t>((nonce_prefix >> 16) & 0xFF);
    nonce[2] = static_cast<uint8_t>((nonce_prefix >> 8) & 0xFF);
    nonce[3] = static_cast<uint8_t>(nonce_prefix & 0xFF);
    nonce[4] = static_cast<uint8_t>((counter >> 56) & 0xFF);
    nonce[5] = static_cast<uint8_t>((counter >> 48) & 0xFF);
    nonce[6] = static_cast<uint8_t>((counter >> 40) & 0xFF);
    nonce[7] = static_cast<uint8_t>((counter >> 32) & 0xFF);
    nonce[8] = static_cast<uint8_t>((counter >> 24) & 0xFF);
    nonce[9] = static_cast<uint8_t>((counter >> 16) & 0xFF);
    nonce[10] = static_cast<uint8_t>((counter >> 8) & 0xFF);
    nonce[11] = static_cast<uint8_t>(counter & 0xFF);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1;
    if (ok) ok = EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce) == 1;

    out_ciphertext.clear();
    out_ciphertext.resize(kCipherHeaderLen + plaintext_len + kTagLen);
    out_ciphertext[0] = kCipherVersion;
    std::memcpy(out_ciphertext.data() + 1, nonce, kNonceLen);

    int out_len = 0;
    if (ok) {
        ok = EVP_EncryptUpdate(ctx,
                               out_ciphertext.data() + kCipherHeaderLen,
                               &out_len,
                               plaintext,
                               static_cast<int>(plaintext_len)) == 1;
    }
    int final_len = 0;
    if (ok) ok = EVP_EncryptFinal_ex(ctx, out_ciphertext.data() + kCipherHeaderLen + out_len, &final_len) == 1;
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(ctx,
                                 EVP_CTRL_GCM_GET_TAG,
                                 kTagLen,
                                 out_ciphertext.data() + kCipherHeaderLen + out_len + final_len) == 1;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        out_ciphertext.clear();
        return false;
    }
    out_ciphertext.resize(kCipherHeaderLen + out_len + final_len + kTagLen);
    return true;
#else
    (void)key;
    (void)nonce_prefix;
    (void)counter;
    (void)plaintext;
    (void)plaintext_len;
    (void)out_ciphertext;
    return false;
#endif
}

bool DecryptTunnelPayload(const uint8_t key[32], const uint8_t* ciphertext, size_t ciphertext_len,
                          std::vector<uint8_t>& out_plaintext) {
#ifdef TUNNGLE_COORD_TLS
    if (!key || !ciphertext) return false;
    if (ciphertext_len < kCipherHeaderLen + kTagLen + 1) return false;
    if (ciphertext[0] != kCipherVersion) return false;
    const uint8_t* nonce = ciphertext + 1;
    const uint8_t* ct = ciphertext + kCipherHeaderLen;
    const size_t ct_len = ciphertext_len - kCipherHeaderLen - kTagLen;
    const uint8_t* tag = ciphertext + ciphertext_len - kTagLen;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1;
    if (ok) ok = EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce) == 1;

    out_plaintext.clear();
    out_plaintext.resize(ct_len);
    int out_len = 0;
    if (ok) ok = EVP_DecryptUpdate(ctx, out_plaintext.data(), &out_len, ct, static_cast<int>(ct_len)) == 1;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen, const_cast<uint8_t*>(tag)) == 1;
    int final_len = 0;
    if (ok) ok = EVP_DecryptFinal_ex(ctx, out_plaintext.data() + out_len, &final_len) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        out_plaintext.clear();
        return false;
    }
    out_plaintext.resize(static_cast<size_t>(out_len + final_len));
    return true;
#else
    (void)key;
    (void)ciphertext;
    (void)ciphertext_len;
    (void)out_plaintext;
    return false;
#endif
}

bool ExtractTunnelNonce(const uint8_t* ciphertext, size_t ciphertext_len, uint32_t* out_prefix, uint64_t* out_counter) {
    if (!ciphertext || !out_prefix || !out_counter) return false;
    if (ciphertext_len < kCipherHeaderLen + kTagLen + 1) return false;
    if (ciphertext[0] != kCipherVersion) return false;
    const uint8_t* nonce = ciphertext + 1;
    *out_prefix = (static_cast<uint32_t>(nonce[0]) << 24) |
                  (static_cast<uint32_t>(nonce[1]) << 16) |
                  (static_cast<uint32_t>(nonce[2]) << 8) |
                  static_cast<uint32_t>(nonce[3]);
    *out_counter = (static_cast<uint64_t>(nonce[4]) << 56) |
                   (static_cast<uint64_t>(nonce[5]) << 48) |
                   (static_cast<uint64_t>(nonce[6]) << 40) |
                   (static_cast<uint64_t>(nonce[7]) << 32) |
                   (static_cast<uint64_t>(nonce[8]) << 24) |
                   (static_cast<uint64_t>(nonce[9]) << 16) |
                   (static_cast<uint64_t>(nonce[10]) << 8) |
                   static_cast<uint64_t>(nonce[11]);
    return true;
}

uint32_t RandomNoncePrefix() {
#ifdef TUNNGLE_COORD_TLS
    uint32_t out = 0;
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&out), sizeof(out)) != 1) return 0;
    return out;
#else
    return 0;
#endif
}

}  // namespace net
}  // namespace tunngle

