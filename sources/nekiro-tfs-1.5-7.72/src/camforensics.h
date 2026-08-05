/**
 * Authenticated item evidence embedded in OTClient CAM recordings.
 *
 * The private key exists only in the server runtime. The matching public key
 * is compiled into the trusted client and is safe to distribute.
 */

#ifndef FS_CAMFORENSICS_H
#define FS_CAMFORENSICS_H

#include <cryptopp/xed25519.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

inline constexpr const char* CAM_FORENSIC_PUBLIC_KEY_HEX =
	"51cd8dc1a893657c1b13ce53abf22292f68b1735d71b2ff719cb88158556bc46";

class CamForensicSigner
{
	public:
		CamForensicSigner() = default;

		CamForensicSigner(const CamForensicSigner&) = delete;
		CamForensicSigner& operator=(const CamForensicSigner&) = delete;

		void loadPEM(const std::string& filename);
		std::string sign(const std::string& payload) const;

		static std::string base64Encode(const std::string& value);
		static std::string sha256TranscriptPacket(
			std::string_view previousDigest, const uint8_t* packetBody, size_t packetSize);

	private:
		std::unique_ptr<CryptoPP::ed25519Signer> signer;
		mutable std::mutex signingMutex;
};

extern CamForensicSigner g_camForensicSigner;

#endif
