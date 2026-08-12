#ifndef FS_PASSWORDHASH_H_E514DF73330F4D0AB2355FDE31B88910
#define FS_PASSWORDHASH_H_E514DF73330F4D0AB2355FDE31B88910

#include <cstddef>
#include <cstdint>
#include <string>

enum class StoredPasswordFormat : uint8_t {
	ARGON2ID,
	LEGACY_SHA1,
	INVALID
};

enum class PasswordVerificationStatus : uint8_t {
	MATCH,
	MISMATCH,
	INVALID_FORMAT,
	INTERNAL_FAILURE
};

struct Argon2Policy {
	uint32_t memoryCostKiB = 65536;
	uint32_t timeCost = 3;
	uint32_t parallelism = 1;
	uint32_t saltLength = 16;
	uint32_t hashLength = 32;
};

struct PasswordVerification {
	PasswordVerificationStatus status = PasswordVerificationStatus::INTERNAL_FAILURE;
	StoredPasswordFormat format = StoredPasswordFormat::INVALID;
	bool needsRehash = false;
};

class PasswordHasher
{
	public:
		explicit PasswordHasher(Argon2Policy policy);

		bool isPolicyValid() const;
		const Argon2Policy& getPolicy() const;

		StoredPasswordFormat classify(const std::string& storedPassword) const;
		PasswordVerification verify(const std::string& password,
			const std::string& storedPassword) const;
		bool hash(const std::string& password, std::string& encodedHash) const;

		// ProtocolGame encrypts credentials inside one 128-byte RSA block. With
		// the server's 25-byte character-name limit, 77 password bytes is the
		// largest value that both 7.72 authentication flows can carry losslessly.
		static constexpr size_t MAX_PASSWORD_BYTES = 77;
		static void wipe(std::string& secret);

	private:
		Argon2Policy policy;
};

#endif
