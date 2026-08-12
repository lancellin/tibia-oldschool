#include "otpch.h"

#include "authenticationmanager.h"

#include "ban.h"
#include "configmanager.h"
#include "database.h"
#include "tasks.h"

#include <fmt/format.h>

#include <algorithm>
#include <iostream>
#include <utility>

extern ConfigManager g_config;
extern Dispatcher g_dispatcher;

AuthenticationManager g_authenticationManager;

namespace {

class DatabaseAuthenticationRepository final : public AuthenticationRepository
{
	public:
		AuthenticationRepositoryStatus loadAccountByName(const std::string& name,
			AuthenticationAccountRecord& account) override
		{
			Database& database = Database::getInstance();
			bool querySucceeded = false;
			DBResult_ptr result = database.storeQuery(fmt::format(
				"SELECT `id`, `name`, `password`, `type`, `premium_ends_at` FROM `accounts` "
				"WHERE `name` = {:s}", database.escapeString(name)), querySucceeded);
			if (!querySucceeded) {
				return AuthenticationRepositoryStatus::FAILURE;
			}
			if (!result) {
				return AuthenticationRepositoryStatus::NOT_FOUND;
			}

			account.id = result->getNumber<uint32_t>("id");
			account.name = result->getString("name");
			account.passwordHash = result->getString("password");
			account.accountType = result->getNumber<uint16_t>("type");
			account.premiumEndsAt = result->getNumber<int64_t>("premium_ends_at");
			return AuthenticationRepositoryStatus::OK;
		}

		AuthenticationRepositoryStatus loadPasswordById(uint32_t accountId,
			std::string& passwordHash) override
		{
			bool querySucceeded = false;
			DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
				"SELECT `password` FROM `accounts` WHERE `id` = {:d}", accountId),
				querySucceeded);
			if (!querySucceeded) {
				return AuthenticationRepositoryStatus::FAILURE;
			}
			if (!result) {
				return AuthenticationRepositoryStatus::NOT_FOUND;
			}
			passwordHash = result->getString("password");
			return AuthenticationRepositoryStatus::OK;
		}

		AuthenticationRepositoryStatus compareAndSwapPassword(uint32_t accountId,
			const std::string& expectedHash, const std::string& replacementHash,
			bool& swapped) override
		{
			Database& database = Database::getInstance();
			uint64_t affectedRows = 0;
			const std::string query = fmt::format(
				"UPDATE `accounts` SET `password` = {:s} WHERE `id` = {:d} AND `password` = {:s}",
				database.escapeString(replacementHash), accountId,
				database.escapeString(expectedHash));
			const std::string redactedQuery = fmt::format(
				"UPDATE `accounts` SET `password` = <redacted> WHERE `id` = {:d} "
				"AND `password` = <redacted>", accountId);
			if (!database.executeQueryWithAffectedRows(query, affectedRows, redactedQuery)) {
				return AuthenticationRepositoryStatus::FAILURE;
			}
			swapped = affectedRows == 1;
			return AuthenticationRepositoryStatus::OK;
		}

		AuthenticationRepositoryStatus loadCharacterList(uint32_t accountId,
			std::vector<std::string>& characters) override
		{
			bool querySucceeded = false;
			DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
				"SELECT `name` FROM `players` WHERE `account_id` = {:d} AND `deletion` = 0 "
				"ORDER BY `name` ASC", accountId), querySucceeded);
			if (!querySucceeded) {
				return AuthenticationRepositoryStatus::FAILURE;
			}
			if (!result) {
				return AuthenticationRepositoryStatus::OK;
			}

			do {
				characters.push_back(result->getString("name"));
			} while (result->next());
			return AuthenticationRepositoryStatus::OK;
		}

		AuthenticationRepositoryStatus resolveOwnedCharacter(uint32_t accountId,
			const std::string& requestedName, uint32_t& characterId,
			std::string& canonicalName) override
		{
			Database& database = Database::getInstance();
			bool querySucceeded = false;
			DBResult_ptr result = database.storeQuery(fmt::format(
				"SELECT `id`, `name` FROM `players` WHERE `name` = {:s} AND `account_id` = {:d} "
				"AND `deletion` = 0", database.escapeString(requestedName), accountId),
				querySucceeded);
			if (!querySucceeded) {
				return AuthenticationRepositoryStatus::FAILURE;
			}
			if (!result) {
				return AuthenticationRepositoryStatus::NOT_FOUND;
			}
			characterId = result->getNumber<uint32_t>("id");
			canonicalName = result->getString("name");
			return AuthenticationRepositoryStatus::OK;
		}
};

} // namespace

bool AuthenticationManager::start()
{
	const int32_t configuredWorkers = g_config.getNumber(ConfigManager::AUTH_WORKER_THREADS);
	const int32_t configuredCapacity = g_config.getNumber(ConfigManager::AUTH_QUEUE_CAPACITY);
	policy.memoryCostKiB = static_cast<uint32_t>(
		std::max<int32_t>(0, g_config.getNumber(ConfigManager::ARGON2_MEMORY_COST_KIB)));
	policy.timeCost = static_cast<uint32_t>(
		std::max<int32_t>(0, g_config.getNumber(ConfigManager::ARGON2_TIME_COST)));
	policy.parallelism = static_cast<uint32_t>(
		std::max<int32_t>(0, g_config.getNumber(ConfigManager::ARGON2_PARALLELISM)));

	PasswordHasher passwordHasher(policy);
	if (configuredWorkers < 1 || configuredWorkers > 8 ||
			configuredCapacity < 1 || configuredCapacity > 4096 ||
			!passwordHasher.isPolicyValid()) {
		std::cerr << "[Error - AuthenticationManager::start] invalid authentication "
			"worker, queue, or Argon2 policy configuration." << std::endl;
		return false;
	}

	std::lock_guard<std::mutex> lock(mutex);
	if (running) {
		return true;
	}

	queueCapacity = static_cast<size_t>(configuredCapacity);
	stopping = false;
	running = true;
	workers.reserve(static_cast<size_t>(configuredWorkers));
	for (int32_t i = 0; i < configuredWorkers; ++i) {
		workers.emplace_back(&AuthenticationManager::workerMain, this);
	}

	std::cout << ">> Authentication workers online: " << configuredWorkers
		<< " (queue " << queueCapacity << ", Argon2id m=" << policy.memoryCostKiB
		<< " KiB, t=" << policy.timeCost << ", p=" << policy.parallelism << ')'
		<< std::endl;
	return true;
}

void AuthenticationManager::shutdown()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!running) {
			return;
		}
		stopping = true;
		for (Task& task : tasks) {
			PasswordHasher::wipe(task.request.password);
		}
		tasks.clear();
	}
	signal.notify_all();

	for (std::thread& worker : workers) {
		if (worker.joinable()) {
			worker.join();
		}
	}

	std::lock_guard<std::mutex> lock(mutex);
	workers.clear();
	running = false;
}

bool AuthenticationManager::enqueue(AuthenticationRequest request, Completion completion)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (!running || stopping || !completion || tasks.size() >= queueCapacity) {
		PasswordHasher::wipe(request.password);
		return false;
	}

	tasks.push_back(Task{std::move(request), std::move(completion)});
	signal.notify_one();
	return true;
}

bool AuthenticationManager::isRunning() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return running && !stopping;
}

size_t AuthenticationManager::pendingCount() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return tasks.size();
}

void AuthenticationManager::workerMain()
{
	DatabaseAuthenticationRepository repository;
	PasswordHasher passwordHasher(policy);
	AuthenticationService service(repository, passwordHasher);
	// Auth must fail in bounded time when MariaDB is unavailable. The generic
	// database layer otherwise retries transient disconnects indefinitely.
	DatabaseRetryLimitScope databaseRetryLimit(1);

	for (;;) {
		Task task;
		{
			std::unique_lock<std::mutex> lock(mutex);
			signal.wait(lock, [&] { return stopping || !tasks.empty(); });
			if (stopping) {
				return;
			}
			task = std::move(tasks.front());
			tasks.pop_front();
		}

		AuthenticationResult result;
		try {
			BanInfo ipBan;
			bool ipBanned = false;
			if (!IOBan::lookupIpBan(task.request.clientIp, ipBan, ipBanned)) {
				result.status = AuthenticationStatus::UNAVAILABLE;
			} else if (ipBanned) {
				result.status = AuthenticationStatus::IP_BANNED;
				result.ipBan.banned = true;
				result.ipBan.bannedBy = std::move(ipBan.bannedBy);
				result.ipBan.reason = std::move(ipBan.reason);
				result.ipBan.expiresAt = ipBan.expiresAt;
			} else {
				result = service.authenticate(task.request);
				if (result.status == AuthenticationStatus::AUTHENTICATED &&
						task.request.flow == AuthenticationFlow::GAME_WORLD) {
					BanInfo accountBan;
					bool accountBanned = false;
					bool namelocked = false;
					if (!IOBan::lookupAccountBan(result.principal.accountId,
							accountBan, accountBanned) ||
							!IOBan::lookupPlayerNamelock(result.principal.characterId,
							namelocked)) {
						result.status = AuthenticationStatus::UNAVAILABLE;
					} else {
						result.principal.characterNamelocked = namelocked;
						result.principal.accountBan.banned = accountBanned;
						result.principal.accountBan.bannedBy = std::move(accountBan.bannedBy);
						result.principal.accountBan.reason = std::move(accountBan.reason);
						result.principal.accountBan.expiresAt = accountBan.expiresAt;
					}
				}
			}
		} catch (...) {
			// Keep the pool alive and return only the generic unavailable state.
			// Exception text is deliberately not logged because dependencies may
			// include credential or verifier material in diagnostic messages.
			result.status = AuthenticationStatus::UNAVAILABLE;
		}
		PasswordHasher::wipe(task.request.password);

		std::lock_guard<std::mutex> lock(mutex);
		if (!stopping) {
			g_dispatcher.addTask(createTask(
				[completion = std::move(task.completion), result = std::move(result)]() mutable {
					completion(std::move(result));
				}));
		}
	}
}
