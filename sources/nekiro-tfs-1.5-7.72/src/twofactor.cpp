#include "otpch.h"

#include "twofactor.h"
#include "totp.h"

#include "configmanager.h"
#include "database.h"

#include <cryptopp/osrng.h>
#include <cryptopp/sha.h>

#include <fmt/format.h>

extern ConfigManager g_config;

namespace {

constexpr int64_t DEVICE_TTL = 30 * 24 * 3600; // 30 days

std::string sha256Hex(const std::string& data)
{
	CryptoPP::SHA256 hash;
	std::array<uint8_t, CryptoPP::SHA256::DIGESTSIZE> digest {};
	hash.CalculateDigest(digest.data(), reinterpret_cast<const uint8_t*>(data.data()), data.size());

	static const char* hex = "0123456789abcdef";
	std::string out;
	out.reserve(digest.size() * 2);
	for (const uint8_t b : digest) {
		out += hex[b >> 4];
		out += hex[b & 0x0F];
	}
	return out;
}

std::string hexKey()
{
	const std::string& hex = g_config.getString(ConfigManager::TOTP_KEY);
	std::string raw;
	raw.reserve(hex.size() / 2);
	for (size_t i = 0; i + 1 < hex.size(); i += 2) {
		const auto hi = hex.find(hex[i]);
		static const std::string H = "0123456789abcdefABCDEF";
		if (H.find(hex[i]) == std::string::npos || H.find(hex[i + 1]) == std::string::npos) {
			return {};
		}
		raw += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
	}
	return raw;
}

} // namespace

namespace twofactor {

std::string getSecret(uint32_t accountId)
{
	Database& db = Database::getInstance();
	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `totp_secret` FROM `accounts` WHERE `id` = {:d}", accountId));
	if (!result) {
		return {};
	}

	const std::string blob = result->getString("totp_secret");
	if (blob.empty()) {
		return {};
	}

	return totp::decryptSecret(blob, hexKey());
}

bool verifyTrustedDevice(uint32_t accountId, const std::string& token)
{
	if (token.empty()) {
		return false;
	}

	Database& db = Database::getInstance();
	const std::string hash = sha256Hex(token);
	DBResult_ptr result = db.storeQuery(fmt::format(
		"SELECT `id`, `expires_at` FROM `myaac_trusted_devices` WHERE `token_hash` = {:s} AND `account_id` = {:d}",
		db.escapeString(hash), accountId));
	if (!result) {
		return false;
	}

	const uint32_t deviceId = result->getNumber<uint32_t>("id");
	const int64_t expiresAt = result->getNumber<int64_t>("expires_at");
	if (expiresAt < time(nullptr)) {
		db.executeQuery(fmt::format("DELETE FROM `myaac_trusted_devices` WHERE `id` = {:d} AND `account_id` = {:d}", deviceId, accountId));
		return false;
	}

	db.executeQuery(fmt::format("UPDATE `myaac_trusted_devices` SET `last_used_at` = {:d} WHERE `id` = {:d}", time(nullptr), deviceId));
	return true;
}

std::string issueTrustedDevice(uint32_t accountId)
{
	CryptoPP::AutoSeededRandomPool prng;
	uint8_t raw[32];
	prng.GenerateBlock(raw, sizeof(raw));

	static const char* hex = "0123456789abcdef";
	std::string token;
	token.reserve(64);
	for (const uint8_t b : raw) {
		token += hex[b >> 4];
		token += hex[b & 0x0F];
	}

	Database& db = Database::getInstance();
	const int64_t now = time(nullptr);
	db.executeQuery(fmt::format(
		"INSERT INTO `myaac_trusted_devices` (`account_id`, `token_hash`, `label`, `created_at`, `expires_at`, `last_used_at`) "
		"VALUES ({:d}, {:s}, {:s}, {:d}, {:d}, {:d})",
		accountId, db.escapeString(sha256Hex(token)), db.escapeString("game client"), now, now + DEVICE_TTL, now));
	return token;
}

} // namespace twofactor
