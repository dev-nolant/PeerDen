#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tunngle {
namespace net {

bool InitTunnelCryptoKey(const std::string& token, uint8_t out_key[32]);
bool MakeHandshakeProof(const uint8_t key[32], uint8_t msg_type, uint64_t nonce, uint8_t out_proof[16]);
bool VerifyHandshakeProof(const uint8_t key[32], uint8_t msg_type, uint64_t nonce, const uint8_t proof[16]);
bool EncryptTunnelPayload(const uint8_t key[32], uint32_t nonce_prefix, uint64_t counter,
                          const uint8_t* plaintext, size_t plaintext_len,
                          std::vector<uint8_t>& out_ciphertext);
bool DecryptTunnelPayload(const uint8_t key[32], const uint8_t* ciphertext, size_t ciphertext_len,
                          std::vector<uint8_t>& out_plaintext);
bool ExtractTunnelNonce(const uint8_t* ciphertext, size_t ciphertext_len, uint32_t* out_prefix, uint64_t* out_counter);
uint32_t RandomNoncePrefix();

}  // namespace net
}  // namespace tunngle

