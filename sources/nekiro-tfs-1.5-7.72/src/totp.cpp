#include "otpch.h"

#include "totp.h"

#include <cryptopp/aes.h>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include <cryptopp/gcm.h>
#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>

#include <cctype>

namespace {

constexpr int32_t PERIOD = 30;
constexpr int32_t DIGITS = 6;
constexpr int32_t WINDOW = 1;

std::string hotp(const std::string& key, uint64_t counter)
{
	uint8_t buf[8];
	for (int i = 7; i >= 0; --i) {
		buf[i] = counter & 0xFF;
		counter >>= 8;
	}

	uint8_t digest[CryptoPP::SHA1::DIGESTSIZE];
	CryptoPP::HMAC<CryptoPP::SHA1> hmac(reinterpret_cast<const uint8_t*>(key.data()), key.size());
	hmac.CalculateDigest(digest, buf, 8);

	const int offset = digest[19] & 0x0F;
	const uint32_t value = ((digest[offset] & 0x7F) << 24) | ((digest[offset + 1] & 0xFF) << 16) | ((digest[offset + 2] & 0xFF) << 8) | (digest[offset + 3] & 0xFF);

	uint32_t mod = 1;
	for (int i = 0; i < DIGITS; ++i) {
		mod *= 10;
	}

	char out[8];
	std::snprintf(out, sizeof(out), "%06u", value % mod);
	return out;
}

bool constantTimeEquals(const std::string& a, const std::string& b)
{
	if (a.size() != b.size()) {
		return false;
	}

	uint8_t diff = 0;
	for (size_t i = 0; i < a.size(); ++i) {
		diff |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
	}
	return diff == 0;
}

} // namespace

namespace totp {

std::string base32Decode(const std::string& input)
{
	static const std::string B32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

	std::string bits;
	bits.reserve(input.size() * 5);
	for (const char c : input) {
		const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		if (upper == '=' || upper == ' ') {
			continue;
		}

		const auto pos = B32.find(upper);
		if (pos == std::string::npos) {
			return {};
		}

		for (int i = 4; i >= 0; --i) {
			bits += ((pos >> i) & 1) != 0 ? '1' : '0';
		}
	}

	std::string out;
	for (size_t i = 0; i + 8 <= bits.size(); i += 8) {
		out += static_cast<char>(std::stoi(bits.substr(i, 8), nullptr, 2));
	}
	return out;
}

std::string decryptSecret(const std::string& blob, const std::string& key)
{
	if (key.size() != 32) {
		return {};
	}

	std::string raw;
	try {
		CryptoPP::StringSource source(blob, true, new CryptoPP::Base64Decoder(new CryptoPP::StringSink(raw)));
	} catch (const CryptoPP::Exception&) {
		return {};
	}

	// nonce[12] | tag[16] | ciphertext
	if (raw.size() < 12 + 16 + 1) {
		return {};
	}

	// Crypto++ expects ciphertext followed by the MAC
	std::string ctTag = raw.substr(28);
	ctTag += raw.substr(12, 16);

	try {
		CryptoPP::GCM<CryptoPP::AES>::Decryption decryption;
		decryption.SetKeyWithIV(reinterpret_cast<const uint8_t*>(key.data()), key.size(), reinterpret_cast<const uint8_t*>(raw.data()), 12);

		std::string plain;
		CryptoPP::AuthenticatedDecryptionFilter filter(decryption, new CryptoPP::StringSink(plain));
		filter.Put(reinterpret_cast<const uint8_t*>(ctTag.data()), ctTag.size());
		filter.MessageEnd();
		if (!filter.GetLastResult()) {
			return {};
		}
		return plain;
	} catch (const CryptoPP::Exception&) {
		return {};
	}
}

bool verify(const std::string& base32Secret, const std::string& code, time_t now)
{
	if (code.size() != DIGITS) {
		return false;
	}

	for (const char c : code) {
		if (c < '0' || c > '9') {
			return false;
		}
	}

	const std::string key = base32Decode(base32Secret);
	if (key.empty()) {
		return false;
	}

	if (now == 0) {
		now = time(nullptr);
	}
	const int64_t counter = now / PERIOD;

	for (int i = -WINDOW; i <= WINDOW; ++i) {
		if (constantTimeEquals(hotp(key, counter + i), code)) {
			return true;
		}
	}
	return false;
}

} // namespace totp
