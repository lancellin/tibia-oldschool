// TOTP (RFC 6238) support for the in-game two-factor authentication.
// SHA-1, 30s period, 6 digits, window +-1, constant-time comparison.
// Mirrors the website implementation (system/src/Totp.php) so the same
// authenticator code is valid on both the site and the game.

#ifndef FS_TOTP_H
#define FS_TOTP_H

#include <cstdint>
#include <ctime>
#include <string>

namespace totp {

std::string base32Decode(const std::string& input);

// blob = base64(nonce[12] | tag[16] | ciphertext), key = 32 raw bytes.
// Returns "" when the blob or the key is invalid.
std::string decryptSecret(const std::string& blob, const std::string& key);

bool verify(const std::string& base32Secret, const std::string& code, time_t now = 0);

} // namespace totp

#endif
