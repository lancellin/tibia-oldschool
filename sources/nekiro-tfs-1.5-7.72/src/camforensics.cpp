#include "otpch.h"

#include "camforensics.h"

#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include <cryptopp/osrng.h>
#include <cryptopp/sha.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <stdexcept>

namespace {
	constexpr const char* PRIVATE_KEY_HEADER = "-----BEGIN PRIVATE KEY-----";
	constexpr const char* PRIVATE_KEY_FOOTER = "-----END PRIVATE KEY-----";

	CryptoPP::AutoSeededRandomPool camForensicRandom;

	std::string bytesToHex(const CryptoPP::byte* bytes, size_t size)
	{
		static constexpr char HEX_DIGITS[] = "0123456789abcdef";
		std::string encoded(size * 2, '\0');
		for (size_t index = 0; index < size; ++index) {
			encoded[index * 2] = HEX_DIGITS[bytes[index] >> 4];
			encoded[(index * 2) + 1] = HEX_DIGITS[bytes[index] & 0x0F];
		}
		return encoded;
	}
}

void CamForensicSigner::loadPEM(const std::string& filename)
{
	std::ifstream file(filename);
	if (!file.is_open()) {
		throw std::runtime_error("Missing CAM forensic signing key " + filename + ".");
	}

	std::string encoded;
	bool insideKey = false;
	bool foundFooter = false;
	for (std::string line; std::getline(file, line);) {
		if (line == PRIVATE_KEY_HEADER) {
			if (insideKey) {
				throw std::runtime_error("Invalid CAM forensic signing key header.");
			}
			insideKey = true;
			continue;
		}

		if (line == PRIVATE_KEY_FOOTER) {
			foundFooter = insideKey;
			break;
		}

		if (insideKey) {
			encoded.append(line);
		}
	}

	if (!insideKey || !foundFooter || encoded.empty()) {
		throw std::runtime_error("Invalid CAM forensic PKCS#8 private key.");
	}

	CryptoPP::ByteQueue queue;
	CryptoPP::Base64Decoder decoder;
	decoder.Attach(new CryptoPP::Redirector(queue));
	decoder.Put(reinterpret_cast<const CryptoPP::byte*>(encoded.data()), encoded.size());
	decoder.MessageEnd();

	try {
		auto loadedSigner = std::make_unique<CryptoPP::ed25519Signer>(queue);
		const auto& key = dynamic_cast<const CryptoPP::ed25519PrivateKey&>(
			loadedSigner->GetPrivateKey());
		if (!key.Validate(camForensicRandom, 3)) {
			throw std::runtime_error("CAM forensic Ed25519 private key failed validation.");
		}

		const std::string publicKeyHex =
			bytesToHex(key.GetPublicKeyBytePtr(), CryptoPP::ed25519Signer::PUBLIC_KEYLENGTH);
		if (publicKeyHex != CAM_FORENSIC_PUBLIC_KEY_HEX) {
			throw std::runtime_error(
				"CAM forensic private key does not match the public key compiled into the client.");
		}

		signer = std::move(loadedSigner);

		static constexpr char TEST_PACKET[] = "abc";
		static constexpr char EXPECTED_TRANSCRIPT_DIGEST[] =
			"986b588f592d7c857c840eda259461870a4b4bc73ea595d0eab5eca3ffb0ab54";
		if (sha256TranscriptPacket(
		        "-", reinterpret_cast<const uint8_t*>(TEST_PACKET), sizeof(TEST_PACKET) - 1) !=
		    EXPECTED_TRANSCRIPT_DIGEST) {
			throw std::runtime_error("CAM forensic incremental transcript hash self-test failed.");
		}
	} catch (const CryptoPP::Exception& exception) {
		throw std::runtime_error(
			"Unable to load CAM forensic Ed25519 private key: " + std::string(exception.what()));
	}
}

std::string CamForensicSigner::sign(const std::string& payload) const
{
	std::lock_guard<std::mutex> lock(signingMutex);
	if (!signer) {
		throw std::runtime_error("CAM forensic signer is not initialized.");
	}

	std::string signature(signer->MaxSignatureLength(), '\0');
	const size_t signatureSize = signer->SignMessage(
		camForensicRandom,
		reinterpret_cast<const CryptoPP::byte*>(payload.data()),
		payload.size(),
		reinterpret_cast<CryptoPP::byte*>(signature.data()));
	signature.resize(signatureSize);
	return base64Encode(signature);
}

std::string CamForensicSigner::base64Encode(const std::string& value)
{
	std::string encoded;
	CryptoPP::StringSource source(
		value,
		true,
		new CryptoPP::Base64Encoder(new CryptoPP::StringSink(encoded), false));
	return encoded;
}

std::string CamForensicSigner::sha256TranscriptPacket(
	std::string_view previousDigest, const uint8_t* packetBody, size_t packetSize)
{
	static constexpr char PREFIX[] = "OTCAM-PACKET|1|";
	static constexpr CryptoPP::byte SEPARATOR = '|';
	static constexpr CryptoPP::byte NEWLINE = '\n';

	char packetSizeText[32];
	const auto sizeResult =
		std::to_chars(packetSizeText, packetSizeText + sizeof(packetSizeText), packetSize);
	if (sizeResult.ec != std::errc()) {
		throw std::runtime_error("Unable to encode CAM forensic transcript packet size.");
	}

	CryptoPP::SHA256 hash;
	hash.Update(
		reinterpret_cast<const CryptoPP::byte*>(PREFIX),
		sizeof(PREFIX) - 1);
	hash.Update(
		reinterpret_cast<const CryptoPP::byte*>(previousDigest.data()),
		previousDigest.size());
	hash.Update(&SEPARATOR, 1);
	hash.Update(
		reinterpret_cast<const CryptoPP::byte*>(packetSizeText),
		static_cast<size_t>(sizeResult.ptr - packetSizeText));
	hash.Update(&NEWLINE, 1);
	if (packetSize != 0) {
		hash.Update(packetBody, packetSize);
	}

	std::array<CryptoPP::byte, CryptoPP::SHA256::DIGESTSIZE> digest {};
	hash.Final(digest.data());
	return bytesToHex(digest.data(), digest.size());
}
