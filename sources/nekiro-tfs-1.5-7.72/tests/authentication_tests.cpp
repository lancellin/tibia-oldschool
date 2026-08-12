#include "authenticationservice.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr const char* INTEROPERABILITY_PASSWORD = "Interoperability-Test-2026!";

class FakeRepository final : public AuthenticationRepository
{
	public:
		AuthenticationRepositoryStatus loadAccountByName(const std::string& name,
			AuthenticationAccountRecord& output) override
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (!available) {
				return AuthenticationRepositoryStatus::FAILURE;
			}
			if (!accountExists || name != account.name) {
				return AuthenticationRepositoryStatus::NOT_FOUND;
			}
			output = account;
			if (loadBarrierTarget > 0) {
				++loadBarrierCount;
				loadBarrier.notify_all();
				loadBarrier.wait(lock, [&] { return loadBarrierCount >= loadBarrierTarget; });
			}
			return AuthenticationRepositoryStatus::OK;
		}

		AuthenticationRepositoryStatus loadPasswordById(uint32_t accountId,
			std::string& output) override
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (!available) {
				return AuthenticationRepositoryStatus::FAILURE;
			}
			if (!accountExists || accountId != account.id) {
				return AuthenticationRepositoryStatus::NOT_FOUND;
			}
			output = account.passwordHash;
			return AuthenticationRepositoryStatus::OK;
		}

		AuthenticationRepositoryStatus compareAndSwapPassword(uint32_t accountId,
			const std::string& expectedHash, const std::string& replacementHash,
			bool& swapped) override
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (!available) {
				return AuthenticationRepositoryStatus::FAILURE;
			}
			++compareAndSwapCalls;
			if (!externalReplacement.empty()) {
				account.passwordHash = externalReplacement;
				externalReplacement.clear();
			}
			swapped = accountExists && accountId == account.id &&
				account.passwordHash == expectedHash;
			if (swapped) {
				account.passwordHash = replacementHash;
				++successfulSwaps;
			}
			return AuthenticationRepositoryStatus::OK;
		}

		AuthenticationRepositoryStatus loadCharacterList(uint32_t accountId,
			std::vector<std::string>& output) override
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (!available) {
				return AuthenticationRepositoryStatus::FAILURE;
			}
			if (!accountExists || accountId != account.id) {
				return AuthenticationRepositoryStatus::NOT_FOUND;
			}
			output = characters;
			return AuthenticationRepositoryStatus::OK;
		}

		AuthenticationRepositoryStatus resolveOwnedCharacter(uint32_t accountId,
			const std::string& requestedName, uint32_t& characterId,
			std::string& canonicalName) override
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (!available) {
				return AuthenticationRepositoryStatus::FAILURE;
			}
			if (!accountExists || accountId != account.id) {
				return AuthenticationRepositoryStatus::NOT_FOUND;
			}
			for (const std::string& character : characters) {
				if (character == requestedName) {
					characterId = character == "Alice" ? 101 : 102;
					canonicalName = character;
					return AuthenticationRepositoryStatus::OK;
				}
			}
			return AuthenticationRepositoryStatus::NOT_FOUND;
		}

		std::string currentPasswordHash()
		{
			std::lock_guard<std::mutex> lock(mutex);
			return account.passwordHash;
		}

		std::mutex mutex;
		std::condition_variable loadBarrier;
		AuthenticationAccountRecord account {7, "testaccount", "", 1, 0};
		std::vector<std::string> characters {"Alice", "Bob"};
		std::string externalReplacement;
		size_t loadBarrierTarget = 0;
		size_t loadBarrierCount = 0;
		size_t compareAndSwapCalls = 0;
		size_t successfulSwaps = 0;
		bool available = true;
		bool accountExists = true;
};

struct TestContext {
	int failures = 0;

	void expect(bool condition, const std::string& name)
	{
		if (!condition) {
			++failures;
			std::cerr << "FAILED: " << name << '\n';
		}
	}
};

Argon2Policy fastPolicy()
{
	Argon2Policy policy;
	policy.memoryCostKiB = 8192;
	policy.timeCost = 1;
	policy.parallelism = 1;
	return policy;
}

AuthenticationRequest characterListRequest(const std::string& password)
{
	AuthenticationRequest request;
	request.flow = AuthenticationFlow::CHARACTER_LIST;
	request.accountName = "testaccount";
	request.password = password;
	return request;
}

AuthenticationRequest gameWorldRequest(const std::string& password,
	const std::string& character = "Alice")
{
	AuthenticationRequest request;
	request.flow = AuthenticationFlow::GAME_WORLD;
	request.accountName = "testaccount";
	request.password = password;
	request.characterName = character;
	return request;
}

void testLegacyMigration(TestContext& test)
{
	FakeRepository repository;
	repository.account.passwordHash = "5baa61e4c9b93f3f0682250b6cf8331b7ee68fd8";
	PasswordHasher hasher(fastPolicy());
	AuthenticationService service(repository, hasher);

	AuthenticationResult result = service.authenticate(characterListRequest("password"));
	test.expect(result.status == AuthenticationStatus::AUTHENTICATED,
		"correct legacy SHA-1 authenticates");
	test.expect(result.migratedLegacyPassword, "legacy SHA-1 is reported as migrated");
	test.expect(hasher.classify(repository.currentPasswordHash()) == StoredPasswordFormat::ARGON2ID,
		"legacy SHA-1 is replaced with Argon2id");
	test.expect(result.principal.characters.size() == 2,
		"character-list flow returns owned characters");
}

void testLegacyMismatchDoesNotWrite(TestContext& test)
{
	FakeRepository repository;
	const std::string original = "5baa61e4c9b93f3f0682250b6cf8331b7ee68fd8";
	repository.account.passwordHash = original;
	PasswordHasher hasher(fastPolicy());
	AuthenticationService service(repository, hasher);

	AuthenticationResult result = service.authenticate(characterListRequest("incorrect"));
	test.expect(result.status == AuthenticationStatus::REJECTED,
		"incorrect legacy SHA-1 is rejected");
	test.expect(repository.currentPasswordHash() == original,
		"incorrect legacy SHA-1 does not change storage");
	test.expect(repository.compareAndSwapCalls == 0,
		"incorrect legacy SHA-1 does not attempt CAS");
}

void testModernPasswordAndOwnership(TestContext& test)
{
	FakeRepository repository;
	PasswordHasher hasher(fastPolicy());
	test.expect(hasher.hash("modern-password", repository.account.passwordHash),
		"modern test hash generated");
	AuthenticationService service(repository, hasher);

	AuthenticationResult correct = service.authenticate(
		gameWorldRequest("modern-password", "Alice"));
	test.expect(correct.status == AuthenticationStatus::AUTHENTICATED,
		"correct modern password authenticates");
	test.expect(correct.principal.characterName == "Alice",
		"world-login flow resolves canonical owned character");
	test.expect(service.authenticate(gameWorldRequest("wrong-password", "Alice")).status ==
		AuthenticationStatus::REJECTED,
		"incorrect modern password is rejected");
	test.expect(service.authenticate(gameWorldRequest("modern-password", "Mallory")).status ==
		AuthenticationStatus::REJECTED,
		"character not owned by account is rejected");
}

void testInvalidAndUnavailable(TestContext& test)
{
	PasswordHasher hasher(fastPolicy());

	FakeRepository invalidRepository;
	invalidRepository.account.passwordHash = "$argon2id$malformed";
	AuthenticationService invalidService(invalidRepository, hasher);
	test.expect(invalidService.authenticate(characterListRequest("anything")).status ==
		AuthenticationStatus::REJECTED,
		"invalid stored format fails closed");

	FakeRepository missingRepository;
	missingRepository.accountExists = false;
	AuthenticationService missingService(missingRepository, hasher);
	test.expect(missingService.authenticate(characterListRequest("anything")).status ==
		AuthenticationStatus::REJECTED,
		"nonexistent account is rejected generically");

	FakeRepository unavailableRepository;
	unavailableRepository.available = false;
	AuthenticationService unavailableService(unavailableRepository, hasher);
	test.expect(unavailableService.authenticate(characterListRequest("anything")).status ==
		AuthenticationStatus::UNAVAILABLE,
		"repository failure reports temporary unavailability");
}

void testPasswordTransportLimit(TestContext& test)
{
	PasswordHasher hasher(fastPolicy());
	std::string encodedHash;
	test.expect(hasher.hash(std::string(PasswordHasher::MAX_PASSWORD_BYTES, 'x'), encodedHash),
		"77-byte protocol-safe password is accepted");
	PasswordHasher::wipe(encodedHash);
	test.expect(!hasher.hash(std::string(PasswordHasher::MAX_PASSWORD_BYTES + 1, 'x'), encodedHash),
		"password beyond the shared 7.72 RSA limit is rejected");
}

void testPolicyRehash(TestContext& test)
{
	Argon2Policy oldPolicy = fastPolicy();
	oldPolicy.memoryCostKiB = 4096;
	PasswordHasher oldHasher(oldPolicy);
	FakeRepository repository;
	test.expect(oldHasher.hash("rehash-password", repository.account.passwordHash),
		"old-policy hash generated");

	PasswordHasher currentHasher(fastPolicy());
	AuthenticationService service(repository, currentHasher);
	AuthenticationResult result = service.authenticate(characterListRequest("rehash-password"));
	test.expect(result.status == AuthenticationStatus::AUTHENTICATED,
		"old-policy Argon2id authenticates");
	test.expect(result.rehashedPassword, "old-policy Argon2id is reported as rehashed");
	test.expect(!currentHasher.verify("rehash-password", repository.currentPasswordHash()).needsRehash,
		"rehash stores current parameters");
}

void testConcurrentLazyMigration(TestContext& test)
{
	FakeRepository repository;
	repository.account.passwordHash = "5baa61e4c9b93f3f0682250b6cf8331b7ee68fd8";
	repository.loadBarrierTarget = 2;
	PasswordHasher hasher(fastPolicy());
	AuthenticationService service(repository, hasher);
	std::atomic<int> successes {0};

	auto login = [&] {
		if (service.authenticate(characterListRequest("password")).status ==
				AuthenticationStatus::AUTHENTICATED) {
			++successes;
		}
	};
	std::thread first(login);
	std::thread second(login);
	first.join();
	second.join();

	test.expect(successes == 2, "two concurrent migrations both authenticate safely");
	test.expect(repository.successfulSwaps == 1,
		"two concurrent migrations perform exactly one successful CAS");
	test.expect(hasher.classify(repository.currentPasswordHash()) == StoredPasswordFormat::ARGON2ID,
		"concurrent migration leaves one valid Argon2id hash");
}

void testExternalPasswordChangeWins(TestContext& test)
{
	FakeRepository repository;
	repository.account.passwordHash = "5baa61e4c9b93f3f0682250b6cf8331b7ee68fd8";
	PasswordHasher hasher(fastPolicy());
	test.expect(hasher.hash("externally-changed", repository.externalReplacement),
		"external replacement hash generated");
	AuthenticationService service(repository, hasher);

	AuthenticationResult result = service.authenticate(characterListRequest("password"));
	test.expect(result.status == AuthenticationStatus::REJECTED,
		"external password change invalidates stale authentication");
	test.expect(repository.successfulSwaps == 0,
		"stale lazy migration cannot overwrite external password change");
	test.expect(hasher.verify("externally-changed", repository.currentPasswordHash()).status ==
		PasswordVerificationStatus::MATCH,
		"external password remains stored");
}

int runTests()
{
	TestContext test;
	testLegacyMigration(test);
	testLegacyMismatchDoesNotWrite(test);
	testModernPasswordAndOwnership(test);
	testInvalidAndUnavailable(test);
	testPasswordTransportLimit(test);
	testPolicyRehash(test);
	testConcurrentLazyMigration(test);
	testExternalPasswordChangeWins(test);

	if (test.failures != 0) {
		std::cerr << test.failures << " authentication test(s) failed.\n";
		return 1;
	}
	std::cout << "All authentication tests passed.\n";
	return 0;
}

int writeInteroperabilityHash(const std::string& path)
{
	PasswordHasher hasher(Argon2Policy {});
	std::string encodedHash;
	if (!hasher.hash(INTEROPERABILITY_PASSWORD, encodedHash)) {
		return 1;
	}
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output) {
		PasswordHasher::wipe(encodedHash);
		return 1;
	}
	output << encodedHash;
	PasswordHasher::wipe(encodedHash);
	return output ? 0 : 1;
}

int verifyInteroperabilityHash(const std::string& path)
{
	std::ifstream input(path, std::ios::binary);
	std::string encodedHash;
	std::getline(input, encodedHash);
	if (!input && encodedHash.empty()) {
		return 1;
	}
	PasswordHasher hasher(Argon2Policy {});
	const PasswordVerification verification = hasher.verify(
		INTEROPERABILITY_PASSWORD, encodedHash);
	PasswordHasher::wipe(encodedHash);
	return verification.status == PasswordVerificationStatus::MATCH ? 0 : 1;
}

int runBenchmark()
{
	const Argon2Policy policy;
	for (size_t concurrency : {size_t {1}, size_t {2}, size_t {4}}) {
		std::atomic<bool> failed {false};
		const auto started = std::chrono::steady_clock::now();
		std::vector<std::thread> threads;
		threads.reserve(concurrency);
		for (size_t i = 0; i < concurrency; ++i) {
			threads.emplace_back([&] {
				PasswordHasher hasher(policy);
				std::string encodedHash;
				if (!hasher.hash(INTEROPERABILITY_PASSWORD, encodedHash)) {
					failed = true;
				}
				PasswordHasher::wipe(encodedHash);
			});
		}
		for (std::thread& thread : threads) {
			thread.join();
		}
		const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count();
		if (failed) {
			return 1;
		}
		std::cout << "concurrency=" << concurrency << " elapsed_ms=" << milliseconds
			<< " peak_argon2_memory_mib=" << (policy.memoryCostKiB * concurrency / 1024)
			<< '\n';
	}
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	if (argc == 1) {
		return runTests();
	}
	if (argc == 3 && std::string(argv[1]) == "--write-interoperability-hash") {
		return writeInteroperabilityHash(argv[2]);
	}
	if (argc == 3 && std::string(argv[1]) == "--verify-interoperability-hash") {
		return verifyInteroperabilityHash(argv[2]);
	}
	if (argc == 2 && std::string(argv[1]) == "--benchmark") {
		return runBenchmark();
	}
	std::cerr << "Unsupported test mode.\n";
	return 2;
}
