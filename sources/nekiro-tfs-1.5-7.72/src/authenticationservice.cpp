#include "authenticationservice.h"

namespace {

AuthenticationResult invalidCredentials()
{
	AuthenticationResult result;
	result.status = AuthenticationStatus::REJECTED;
	return result;
}

AuthenticationResult serviceUnavailable()
{
	AuthenticationResult result;
	result.status = AuthenticationStatus::UNAVAILABLE;
	return result;
}

} // namespace

AuthenticationService::AuthenticationService(AuthenticationRepository& repository,
	const PasswordHasher& passwordHasher) :
	repository(repository), passwordHasher(passwordHasher)
{
}

AuthenticationResult AuthenticationService::authenticate(const AuthenticationRequest& request) const
{
	if (request.accountName.empty() || request.password.empty() ||
			request.password.size() > PasswordHasher::MAX_PASSWORD_BYTES ||
			(request.flow == AuthenticationFlow::GAME_WORLD && request.characterName.empty())) {
		return invalidCredentials();
	}

	AuthenticationAccountRecord account;
	const AuthenticationRepositoryStatus loadStatus =
		repository.loadAccountByName(request.accountName, account);
	if (loadStatus == AuthenticationRepositoryStatus::NOT_FOUND) {
		return invalidCredentials();
	}
	if (loadStatus != AuthenticationRepositoryStatus::OK || account.id == 0) {
		return serviceUnavailable();
	}

	const PasswordVerification verification =
		passwordHasher.verify(request.password, account.passwordHash);
	if (verification.status == PasswordVerificationStatus::MISMATCH ||
			verification.status == PasswordVerificationStatus::INVALID_FORMAT) {
		return invalidCredentials();
	}
	if (verification.status != PasswordVerificationStatus::MATCH) {
		return serviceUnavailable();
	}

	AuthenticationResult result;
	result.status = AuthenticationStatus::AUTHENTICATED;
	result.principal.accountId = account.id;
	result.principal.accountName = account.name;
	result.principal.accountType = account.accountType;
	result.principal.premiumEndsAt = account.premiumEndsAt;

	if (verification.needsRehash) {
		std::string replacementHash;
		if (!passwordHasher.hash(request.password, replacementHash)) {
			return serviceUnavailable();
		}

		bool swapped = false;
		const AuthenticationRepositoryStatus updateStatus = repository.compareAndSwapPassword(
			account.id, account.passwordHash, replacementHash, swapped);
		if (updateStatus != AuthenticationRepositoryStatus::OK) {
			return serviceUnavailable();
		}

		if (swapped) {
			result.migratedLegacyPassword =
				verification.format == StoredPasswordFormat::LEGACY_SHA1;
			result.rehashedPassword =
				verification.format == StoredPasswordFormat::ARGON2ID;
		} else {
			std::string currentHash;
			const AuthenticationRepositoryStatus reloadStatus =
				repository.loadPasswordById(account.id, currentHash);
			if (reloadStatus == AuthenticationRepositoryStatus::NOT_FOUND) {
				return invalidCredentials();
			}
			if (reloadStatus != AuthenticationRepositoryStatus::OK) {
				return serviceUnavailable();
			}

			const PasswordVerification currentVerification =
				passwordHasher.verify(request.password, currentHash);
			if (currentVerification.status != PasswordVerificationStatus::MATCH) {
				return invalidCredentials();
			}
		}
	}

	if (request.flow == AuthenticationFlow::CHARACTER_LIST) {
		const AuthenticationRepositoryStatus charactersStatus =
			repository.loadCharacterList(account.id, result.principal.characters);
		if (charactersStatus != AuthenticationRepositoryStatus::OK) {
			return serviceUnavailable();
		}
	} else {
		const AuthenticationRepositoryStatus characterStatus = repository.resolveOwnedCharacter(
			account.id, request.characterName, result.principal.characterId,
			result.principal.characterName);
		if (characterStatus == AuthenticationRepositoryStatus::NOT_FOUND) {
			return invalidCredentials();
		}
		if (characterStatus != AuthenticationRepositoryStatus::OK) {
			return serviceUnavailable();
		}
	}

	return result;
}
