// Two-factor helpers for the login protocol. Reads the same data used by the
// website (accounts.totp_secret, myaac_trusted_devices) so both surfaces share
// secrets and trusted devices.

#ifndef FS_TWOFACTOR_H
#define FS_TWOFACTOR_H

#include <cstdint>
#include <string>

namespace twofactor {

// Decrypted TOTP secret for the account, or "" when 2FA is not enabled.
std::string getSecret(uint32_t accountId);

// Validates a raw trusted-device token (cookie/client side) against the
// sha256 hashes stored in myaac_trusted_devices. Updates last_used_at.
bool verifyTrustedDevice(uint32_t accountId, const std::string& token);

// Creates a new trusted device (30 days) and returns the raw token.
std::string issueTrustedDevice(uint32_t accountId);

} // namespace twofactor

#endif
