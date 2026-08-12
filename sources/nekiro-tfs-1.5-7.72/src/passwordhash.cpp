#include "passwordhash.h"

#include <argon2.h>
#include <cryptopp/osrng.h>
#include <cryptopp/secblock.h>
#include <cryptopp/sha.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <regex>
#include <vector>

namespace {

constexpr uint32_t ARGON2_VERSION_19 = 19;
constexpr uint32_t MAX_ACCEPTED_MEMORY_COST_KIB = 262144;
constexpr uint32_t MAX_ACCEPTED_TIME_COST = 10;
constexpr uint32_t MAX_ACCEPTED_PARALLELISM = 8;
constexpr uint32_t MIN_ACCEPTED_SALT_BYTES = 8;
constexpr uint32_t MAX_ACCEPTED_SALT_BYTES = 64;
constexpr uint32_t MIN_ACCEPTED_HASH_BYTES = 16;
constexpr uint32_t MAX_ACCEPTED_HASH_BYTES = 64;

struct ParsedArgon2id {
	uint32_t version = 0;
	uint32_t memoryCostKiB = 0;
	uint32_t timeCost = 0;
	uint32_t parallelism = 0;
	uint32_t saltLength = 0;
	uint32_t hashLength = 0;
};

bool parseUint32(const std::string& value, uint32_t& result)
{
	if (value.empty() || value.size() > 10 ||
			!std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
		return false;
	}

	try {
		const unsigned long parsed = std::stoul(value);
		if (parsed > std::numeric_limits<uint32_t>::max()) {
			return false;
		}
		result = static_cast<uint32_t>(parsed);
		return true;
	} catch (...) {
		return false;
	}
}

bool decodedBase64Length(const std::string& value, uint32_t& length)
{
	if (value.empty() || value.size() % 4 == 1 ||
			!std::all_of(value.begin(), value.end(), [](unsigned char ch) {
				return std::isalnum(ch) != 0 || ch == '+' || ch == '/';
			})) {
		return false;
	}

	const size_t decoded = (value.size() * 6) / 8;
	if (decoded > std::numeric_limits<uint32_t>::max()) {
		return false;
	}
	length = static_cast<uint32_t>(decoded);
	return true;
}

bool parseArgon2id(const std::string& encoded, ParsedArgon2id& parsed)
{
	static const std::regex pattern(
		R"(^\$argon2id\$v=([0-9]+)\$m=([0-9]+),t=([0-9]+),p=([0-9]+)\$([A-Za-z0-9+/]+)\$([A-Za-z0-9+/]+)$)",
		std::regex_constants::ECMAScript);
	std::smatch match;
	if (!std::regex_match(encoded, match, pattern)) {
		return false;
	}

	if (!parseUint32(match[1].str(), parsed.version) ||
			!parseUint32(match[2].str(), parsed.memoryCostKiB) ||
			!parseUint32(match[3].str(), parsed.timeCost) ||
			!parseUint32(match[4].str(), parsed.parallelism) ||
			!decodedBase64Length(match[5].str(), parsed.saltLength) ||
			!decodedBase64Length(match[6].str(), parsed.hashLength)) {
		return false;
	}

	return parsed.version == ARGON2_VERSION_19 &&
		parsed.parallelism >= 1 && parsed.parallelism <= MAX_ACCEPTED_PARALLELISM &&
		parsed.timeCost >= 1 && parsed.timeCost <= MAX_ACCEPTED_TIME_COST &&
		parsed.memoryCostKiB >= 8 * parsed.parallelism &&
		parsed.memoryCostKiB <= MAX_ACCEPTED_MEMORY_COST_KIB &&
		parsed.saltLength >= MIN_ACCEPTED_SALT_BYTES &&
		parsed.saltLength <= MAX_ACCEPTED_SALT_BYTES &&
		parsed.hashLength >= MIN_ACCEPTED_HASH_BYTES &&
		parsed.hashLength <= MAX_ACCEPTED_HASH_BYTES;
}

bool isLegacySha1(const std::string& value)
{
	return value.size() == 40 &&
		std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}

std::string sha1Hex(const std::string& value)
{
	std::array<CryptoPP::byte, CryptoPP::SHA1::DIGESTSIZE> digest {};
	CryptoPP::SHA1 sha1;
	sha1.CalculateDigest(digest.data(),
		reinterpret_cast<const CryptoPP::byte*>(value.data()), value.size());

	static constexpr char hex[] = "0123456789abcdef";
	std::string result(digest.size() * 2, '\0');
	for (size_t i = 0; i < digest.size(); ++i) {
		result[2 * i] = hex[digest[i] >> 4];
		result[2 * i + 1] = hex[digest[i] & 0x0F];
	}
	return result;
}

bool constantTimeEqualsIgnoreCase(const std::string& left, const std::string& right)
{
	if (left.size() != right.size()) {
		return false;
	}

	unsigned char difference = 0;
	for (size_t i = 0; i < left.size(); ++i) {
		difference |= static_cast<unsigned char>(
			std::tolower(static_cast<unsigned char>(left[i])) ^
			std::tolower(static_cast<unsigned char>(right[i])));
	}
	return difference == 0;
}

} // namespace

PasswordHasher::PasswordHasher(Argon2Policy policy) : policy(policy)
{
}

bool PasswordHasher::isPolicyValid() const
{
	return policy.parallelism >= 1 && policy.parallelism <= MAX_ACCEPTED_PARALLELISM &&
		policy.timeCost >= 1 && policy.timeCost <= MAX_ACCEPTED_TIME_COST &&
		policy.memoryCostKiB >= 8 * policy.parallelism &&
		policy.memoryCostKiB <= MAX_ACCEPTED_MEMORY_COST_KIB &&
		policy.saltLength >= MIN_ACCEPTED_SALT_BYTES &&
		policy.saltLength <= MAX_ACCEPTED_SALT_BYTES &&
		policy.hashLength >= MIN_ACCEPTED_HASH_BYTES &&
		policy.hashLength <= MAX_ACCEPTED_HASH_BYTES;
}

const Argon2Policy& PasswordHasher::getPolicy() const
{
	return policy;
}

StoredPasswordFormat PasswordHasher::classify(const std::string& storedPassword) const
{
	ParsedArgon2id parsed;
	if (parseArgon2id(storedPassword, parsed)) {
		return StoredPasswordFormat::ARGON2ID;
	}
	if (isLegacySha1(storedPassword)) {
		return StoredPasswordFormat::LEGACY_SHA1;
	}
	return StoredPasswordFormat::INVALID;
}

PasswordVerification PasswordHasher::verify(const std::string& password,
	const std::string& storedPassword) const
{
	PasswordVerification verification;
	verification.format = classify(storedPassword);

	if (password.empty() || password.size() > MAX_PASSWORD_BYTES) {
		verification.status = PasswordVerificationStatus::MISMATCH;
		return verification;
	}

	if (verification.format == StoredPasswordFormat::LEGACY_SHA1) {
		verification.status = constantTimeEqualsIgnoreCase(sha1Hex(password), storedPassword) ?
			PasswordVerificationStatus::MATCH : PasswordVerificationStatus::MISMATCH;
		verification.needsRehash = verification.status == PasswordVerificationStatus::MATCH;
		return verification;
	}

	if (verification.format != StoredPasswordFormat::ARGON2ID) {
		verification.status = PasswordVerificationStatus::INVALID_FORMAT;
		return verification;
	}

	ParsedArgon2id parsed;
	if (!parseArgon2id(storedPassword, parsed)) {
		verification.status = PasswordVerificationStatus::INVALID_FORMAT;
		return verification;
	}

	const int result = argon2id_verify(storedPassword.c_str(), password.data(), password.size());
	if (result == ARGON2_VERIFY_MISMATCH) {
		verification.status = PasswordVerificationStatus::MISMATCH;
		return verification;
	}
	if (result != ARGON2_OK) {
		verification.status = PasswordVerificationStatus::INVALID_FORMAT;
		return verification;
	}

	verification.status = PasswordVerificationStatus::MATCH;
	verification.needsRehash = parsed.memoryCostKiB != policy.memoryCostKiB ||
		parsed.timeCost != policy.timeCost || parsed.parallelism != policy.parallelism ||
		parsed.saltLength != policy.saltLength || parsed.hashLength != policy.hashLength;
	return verification;
}

bool PasswordHasher::hash(const std::string& password, std::string& encodedHash) const
{
	encodedHash.clear();
	if (!isPolicyValid() || password.empty() || password.size() > MAX_PASSWORD_BYTES) {
		return false;
	}

	std::vector<uint8_t> salt(policy.saltLength);
	CryptoPP::AutoSeededRandomPool random;
	random.GenerateBlock(salt.data(), salt.size());

	const size_t encodedLength = argon2_encodedlen(policy.timeCost, policy.memoryCostKiB,
		policy.parallelism, policy.saltLength, policy.hashLength, Argon2_id);
	std::vector<char> buffer(encodedLength, '\0');
	const int result = argon2id_hash_encoded(policy.timeCost, policy.memoryCostKiB,
		policy.parallelism, password.data(), password.size(), salt.data(), salt.size(),
		policy.hashLength, buffer.data(), buffer.size());
	CryptoPP::SecureWipeBuffer(salt.data(), salt.size());
	if (result != ARGON2_OK) {
		return false;
	}

	encodedHash.assign(buffer.data());
	CryptoPP::SecureWipeBuffer(reinterpret_cast<CryptoPP::byte*>(buffer.data()), buffer.size());
	return true;
}

void PasswordHasher::wipe(std::string& secret)
{
	if (!secret.empty()) {
		CryptoPP::SecureWipeBuffer(reinterpret_cast<CryptoPP::byte*>(&secret[0]), secret.size());
		secret.clear();
	}
}
