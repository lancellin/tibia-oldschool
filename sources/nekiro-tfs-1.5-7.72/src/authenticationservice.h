#ifndef FS_AUTHENTICATIONSERVICE_H_4EF09A72AF61456D8B9412F875134988
#define FS_AUTHENTICATIONSERVICE_H_4EF09A72AF61456D8B9412F875134988

#include "passwordhash.h"

#include <cstdint>
#include <string>
#include <vector>

enum class AuthenticationFlow : uint8_t {
	CHARACTER_LIST,
	GAME_WORLD
};

enum class AuthenticationStatus : uint8_t {
	AUTHENTICATED,
	REJECTED,
	IP_BANNED,
	UNAVAILABLE
};

struct AuthenticationRequest {
	AuthenticationFlow flow = AuthenticationFlow::CHARACTER_LIST;
	std::string accountName;
	std::string password;
	std::string characterName;
	uint32_t clientIp = 0;
};

struct AuthenticationBanInfo {
	bool banned = false;
	std::string bannedBy;
	std::string reason;
	int64_t expiresAt = 0;
};

struct AuthenticatedPrincipal {
	uint32_t accountId = 0;
	std::string accountName;
	uint16_t accountType = 0;
	int64_t premiumEndsAt = 0;
	uint32_t characterId = 0;
	std::string characterName;
	std::vector<std::string> characters;
	bool characterNamelocked = false;
	AuthenticationBanInfo accountBan;
};

struct AuthenticationResult {
	AuthenticationStatus status = AuthenticationStatus::UNAVAILABLE;
	AuthenticatedPrincipal principal;
	AuthenticationBanInfo ipBan;
	bool migratedLegacyPassword = false;
	bool rehashedPassword = false;
};

enum class AuthenticationRepositoryStatus : uint8_t {
	OK,
	NOT_FOUND,
	FAILURE
};

struct AuthenticationAccountRecord {
	uint32_t id = 0;
	std::string name;
	std::string passwordHash;
	uint16_t accountType = 0;
	int64_t premiumEndsAt = 0;
};

class AuthenticationRepository
{
	public:
		virtual ~AuthenticationRepository() = default;

		virtual AuthenticationRepositoryStatus loadAccountByName(const std::string& name,
			AuthenticationAccountRecord& account) = 0;
		virtual AuthenticationRepositoryStatus loadPasswordById(uint32_t accountId,
			std::string& passwordHash) = 0;
		virtual AuthenticationRepositoryStatus compareAndSwapPassword(uint32_t accountId,
			const std::string& expectedHash, const std::string& replacementHash,
			bool& swapped) = 0;
		virtual AuthenticationRepositoryStatus loadCharacterList(uint32_t accountId,
			std::vector<std::string>& characters) = 0;
		virtual AuthenticationRepositoryStatus resolveOwnedCharacter(uint32_t accountId,
			const std::string& requestedName, uint32_t& characterId,
			std::string& canonicalName) = 0;
};

class AuthenticationService
{
	public:
		AuthenticationService(AuthenticationRepository& repository, const PasswordHasher& passwordHasher);

		AuthenticationResult authenticate(const AuthenticationRequest& request) const;

	private:
		AuthenticationRepository& repository;
		const PasswordHasher& passwordHasher;
};

#endif
