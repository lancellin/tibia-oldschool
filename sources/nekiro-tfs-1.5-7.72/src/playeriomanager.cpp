#include "otpch.h"

#include "configmanager.h"
#include "dispatchermetrics.h"
#include "iologindata.h"
#include "player.h"
#include "playeriomanager.h"
#include "tasks.h"
#include "tools.h"

#include <fmt/format.h>

extern ConfigManager g_config;
extern Dispatcher g_dispatcher;

PlayerIOManager g_playerIOManager;

namespace {
std::string normalizeName(const std::string& name)
{
	return asLowerCaseString(name);
}

const std::optional<std::string>* resultValue(
	const playerio::ResultSet& result, const std::string& column)
{
	const auto it = std::find(result.columns.begin(), result.columns.end(), column);
	if (it == result.columns.end() || result.rows.empty()) {
		return nullptr;
	}
	const size_t index = static_cast<size_t>(std::distance(result.columns.begin(), it));
	if (index >= result.rows.front().size()) {
		return nullptr;
	}
	return &result.rows.front()[index];
}

uint32_t resultNumber(const playerio::ResultSet& result, const std::string& column)
{
	const std::optional<std::string>* value = resultValue(result, column);
	if (!value || !value->has_value()) {
		return 0;
	}
	try {
		return static_cast<uint32_t>(std::stoul(value->value()));
	} catch (const std::exception&) {
		return 0;
	}
}

std::string resultString(const playerio::ResultSet& result, const std::string& column)
{
	const std::optional<std::string>* value = resultValue(result, column);
	if (!value || !value->has_value()) {
		return {};
	}
	return value->value();
}
}

bool PlayerIOManager::start()
{
	if (!g_config.getBoolean(ConfigManager::PLAYER_IO_SERVICE_ENABLED)) {
		std::cout << "Player I/O service disabled; login/logout database operations remain synchronous." << std::endl;
		return true;
	}

	host = g_config.getString(ConfigManager::PLAYER_IO_SERVICE_HOST);
	port = static_cast<uint16_t>(g_config.getNumber(ConfigManager::PLAYER_IO_SERVICE_PORT));
	loginClient = std::make_unique<PlayerIOClient>(host, port);
	logoutClient = std::make_unique<PlayerIOClient>(host, port);
	handoffClient = std::make_unique<PlayerIOClient>(
		host, port, std::chrono::seconds(1));
	healthClient = std::make_unique<PlayerIOClient>(
		host, port, std::chrono::seconds(1));

	std::string error;
	if (!loginClient->ping(error)) {
		std::cerr << "Player I/O service login channel failed: " << error << std::endl;
		return false;
	}
	error.clear();
	if (!logoutClient->ping(error)) {
		std::cerr << "Player I/O service logout channel failed: " << error << std::endl;
		loginClient->close();
		return false;
	}
	error.clear();
	if (!handoffClient->ping(error)) {
		std::cerr << "Player I/O service durable handoff channel failed: " << error << std::endl;
		loginClient->close();
		logoutClient->close();
		return false;
	}

	stopping = false;
	serviceAvailable.store(true, std::memory_order_release);
	enabled = true;
	loginThread = std::thread(&PlayerIOManager::loginThreadMain, this);
	logoutThread = std::thread(&PlayerIOManager::logoutThreadMain, this);
	healthThread = std::thread(&PlayerIOManager::healthThreadMain, this);
	std::cout << "Player I/O service enabled: host=" << host << " port=" << port
	          << " login_workers=1 logout_workers=1 durable_handoff=enabled"
	          << " health_monitor_ms=2000." << std::endl;
	return true;
}

void PlayerIOManager::shutdown()
{
	shutdown(false);
}

void PlayerIOManager::shutdown(bool stopServiceIfIdle)
{
	if (!enabled) {
		return;
	}

	bool serviceStopEligible = false;
	if (stopServiceIfIdle) {
		std::string drainError;
		serviceStopEligible = drain(SHUTDOWN_DRAIN_TIMEOUT, drainError);
		if (!serviceStopEligible) {
			std::cerr << "Player I/O service will remain running because the TFS "
			             "could not prove that all player saves were committed: "
			          << drainError << std::endl;
		}
	}

	stopping = true;
	healthSignal.notify_all();
	{
		std::lock_guard<std::mutex> lock(loginMutex);
		loginTasks.clear();
	}
	{
		std::lock_guard<std::mutex> lock(logoutMutex);
		// A normal controlled shutdown drains this queue before stopping the
		// workers. This clear only handles startup/abnormal shutdown paths.
		logoutTasks.clear();
	}
	loginSignal.notify_all();
	logoutSignal.notify_all();

	// Client operations have bounded timeouts. close() serializes with an active
	// request, so join cannot wait indefinitely; queued logout jobs were durably
	// journaled before entering the worker queue.
	if (loginClient) {
		loginClient->close();
	}
	if (logoutClient) {
		logoutClient->close();
	}
	if (handoffClient) {
		handoffClient->close();
	}
	if (healthClient) {
		healthClient->close();
	}

	if (loginThread.joinable()) {
		loginThread.join();
	}
	if (logoutThread.joinable()) {
		logoutThread.join();
	}
	if (healthThread.joinable()) {
		healthThread.join();
	}
	serviceAvailable.store(false, std::memory_order_release);
	enabled = false;

	if (!serviceStopEligible) {
		return;
	}

	// The safe-shutdown request also performs bounded offline journal pruning.
	// Give it the same maintenance window as the local queue drain.
	PlayerIOClient shutdownClient(host, port, SHUTDOWN_DRAIN_TIMEOUT);
	bool accepted = false;
	uint32_t pendingJobs = 0;
	std::string error;
	if (!shutdownClient.shutdownIfIdle(accepted, pendingJobs, error)) {
		std::cerr << "Player I/O service safe shutdown request failed: "
		          << error << ". The service will remain running." << std::endl;
		return;
	}
	if (!accepted) {
		std::cerr << "Player I/O service refused safe shutdown because it still has "
		          << pendingJobs << " pending job(s). The service will remain running."
		          << std::endl;
		return;
	}
	std::cout << "Player I/O service confirmed an empty durable queue and is shutting down."
	          << std::endl;
}

bool PlayerIOManager::reserveLogin(const std::string& name)
{
	if (!enabled) {
		return true;
	}
	std::lock_guard<std::mutex> lock(loginMutex);
	if (stopping) {
		return false;
	}
	return loadingNames.emplace(normalizeName(name)).second;
}

void PlayerIOManager::releaseLogin(const std::string& name, uint32_t playerId)
{
	if (!enabled) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(loginMutex);
		loadingNames.erase(normalizeName(name));
	}
	if (playerId != 0) {
		std::lock_guard<std::mutex> lock(activityMutex);
		loadingPlayerIds.erase(playerId);
	}
}

bool PlayerIOManager::reserveLoadingPlayerId(uint32_t playerId)
{
	std::lock_guard<std::mutex> lock(activityMutex);
	if (savingPlayers.count(playerId) != 0 || legacyPlayers.count(playerId) != 0) {
		return false;
	}
	return loadingPlayerIds.emplace(playerId).second;
}

bool PlayerIOManager::isSaving(uint32_t playerId) const
{
	if (!enabled) {
		return false;
	}
	std::lock_guard<std::mutex> lock(activityMutex);
	return savingPlayers.count(playerId) != 0;
}

bool PlayerIOManager::reserveLegacyOperation(uint32_t playerId)
{
	if (!enabled || playerId == 0) {
		return true;
	}
	{
		std::lock_guard<std::mutex> lock(activityMutex);
		if (savingPlayers.count(playerId) != 0 ||
				loadingPlayerIds.count(playerId) != 0 ||
				legacyPlayers.count(playerId) != 0) {
			return false;
		}
	}

	bool ready = false;
	playerio::JobState state = playerio::JobState::UNKNOWN;
	uint64_t revision = 0;
	std::string error;
	{
		std::lock_guard<std::mutex> handoffLock(handoffMutex);
		if (!handoffClient->checkPlayerReady(
				playerId, ready, state, revision, error) || !ready) {
			return false;
		}
	}

	std::lock_guard<std::mutex> lock(activityMutex);
	if (savingPlayers.count(playerId) != 0 ||
			loadingPlayerIds.count(playerId) != 0 ||
			legacyPlayers.count(playerId) != 0) {
		return false;
	}
	legacyPlayers.emplace(playerId);
	return true;
}

void PlayerIOManager::releaseLegacyOperation(uint32_t playerId)
{
	if (!enabled || playerId == 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(activityMutex);
	legacyPlayers.erase(playerId);
}

bool PlayerIOManager::enqueueLogin(const std::string& name, LoginCompletion completion)
{
	if (!enabled) {
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(loginMutex);
		if (stopping) {
			return false;
		}
		loginTasks.push_back(LoginTask{name, std::move(completion)});
	}
	loginSignal.notify_one();
	return true;
}

std::string PlayerIOManager::makeJobId(uint32_t playerId) const
{
	std::ostringstream id;
	id << "player-" << playerId << '-';
	for (uint8_t i = 0; i < 4; ++i) {
		id << std::hex << std::setw(8) << std::setfill('0') << getRandomGenerator()();
	}
	return id.str();
}

bool PlayerIOManager::enqueueLogout(Player* player, Completion completion)
{
	if (!enabled || !player || stopping || !isServiceAvailable()) {
		return false;
	}

	const uint32_t playerId = player->getGUID();
	{
		std::lock_guard<std::mutex> lock(activityMutex);
		if (loadingPlayerIds.count(playerId) != 0 ||
				legacyPlayers.count(playerId) != 0 ||
				!savingPlayers.emplace(playerId).second) {
			return false;
		}
	}

	std::vector<std::string> statements;
	std::string error;
	bool snapshotBuilt = false;
	{
		DispatcherLogoutMetricsContext logoutMetricsContext;
		DispatcherPhaseMetricsTimer snapshotTimer(
			DispatcherMetricsPhase::LOGOUT_ASYNC_SNAPSHOT_BUILD);
		snapshotBuilt = IOLoginData::buildPlayerSaveSnapshot(
			player, statements, error);
	}
	if (!snapshotBuilt) {
		std::lock_guard<std::mutex> lock(activityMutex);
		savingPlayers.erase(playerId);
		return false;
	}
	// The monitor may have observed a failure while the immutable snapshot was
	// being built. Refuse the handoff while the Player is still owned by the
	// Dispatcher so Player::onRemoveCreature can use its synchronous fallback.
	if (!isServiceAvailable()) {
		std::lock_guard<std::mutex> lock(activityMutex);
		savingPlayers.erase(playerId);
		return false;
	}

	const std::string jobId = makeJobId(playerId);
	const std::string playerName = player->getName();
	{
		std::lock_guard<std::mutex> lock(logoutMutex);
		if (stopping) {
			std::lock_guard<std::mutex> activityLock(activityMutex);
			savingPlayers.erase(playerId);
			return false;
		}
		logoutTasks.push_back(LogoutTask{
			playerId, playerName, jobId, std::move(statements), 0, 0,
			std::move(completion)});
	}
	logoutSignal.notify_one();
	return true;
}

size_t PlayerIOManager::pendingLoginCount() const
{
	std::lock_guard<std::mutex> lock(loginMutex);
	return loginTasks.size();
}

size_t PlayerIOManager::pendingLogoutCount() const
{
	std::lock_guard<std::mutex> lock(logoutMutex);
	return logoutTasks.size() + activeLogoutJobs;
}

bool PlayerIOManager::drain(std::chrono::milliseconds timeout, std::string& error)
{
	if (!enabled) {
		return true;
	}
	std::unique_lock<std::mutex> lock(logoutMutex);
	const bool drained = logoutDrainedSignal.wait_for(lock, timeout, [&] {
		return logoutTasks.empty() && activeLogoutJobs == 0;
	});
	if (!drained) {
		error = "timed out waiting for asynchronous player saves to drain";
		return false;
	}
	const std::vector<std::pair<uint32_t, std::string>> unresolved(
		unresolvedLogoutJobs.begin(), unresolvedLogoutJobs.end());
	lock.unlock();

	for (const auto& unresolvedJob : unresolved) {
		const uint32_t playerId = unresolvedJob.first;
		playerio::JobState state = playerio::JobState::UNKNOWN;
		uint64_t committedRevision = 0;
		std::string jobError;
		std::string checkError;
		{
			std::lock_guard<std::mutex> handoffLock(handoffMutex);
			if (!handoffClient->getSaveJobStatus(unresolvedJob.second, state,
					committedRevision, jobError, checkError)) {
				error = checkError.empty() ?
					"could not verify a previously failed player save" : checkError;
				return false;
			}
		}
		if (state != playerio::JobState::COMMITTED) {
			error = fmt::format(
				"player {:d} job {:s} is not committed (state {:d}): {:s}",
				playerId, unresolvedJob.second, static_cast<uint8_t>(state), jobError);
			return false;
		}
		{
			std::lock_guard<std::mutex> activityLock(activityMutex);
			savingPlayers.erase(playerId);
		}
		std::lock_guard<std::mutex> resolvedLock(logoutMutex);
		unresolvedLogoutJobs.erase(playerId);
	}
	return true;
}

void PlayerIOManager::updateServiceAvailability(bool available, const std::string& error)
{
	const bool previous = serviceAvailable.exchange(available, std::memory_order_acq_rel);
	if (previous == available || stopping) {
		return;
	}

	if (available) {
		std::cout << "Player I/O service health monitor: service recovered."
		          << std::endl;
	} else {
		std::cerr << "Player I/O service health monitor: service unavailable";
		if (!error.empty()) {
			std::cerr << " (" << error << ')';
		}
		std::cerr << ". New logouts will use the synchronous fallback until recovery."
		          << std::endl;
	}
	healthSignal.notify_all();
}

void PlayerIOManager::healthThreadMain()
{
	while (!stopping) {
		std::string error;
		const bool available = healthClient->ping(error);
		if (!stopping) {
			updateServiceAvailability(available, error);
		}

		std::unique_lock<std::mutex> lock(healthMutex);
		healthSignal.wait_for(lock, std::chrono::seconds(2), [&] {
			return stopping.load();
		});
	}
}

void PlayerIOManager::loginThreadMain()
{
	for (;;) {
		LoginTask task;
		{
			std::unique_lock<std::mutex> lock(loginMutex);
			loginSignal.wait(lock, [&] { return stopping || !loginTasks.empty(); });
			if (stopping) {
				return;
			}
			if (loginTasks.empty()) {
				continue;
			}
			task = std::move(loginTasks.front());
			loginTasks.pop_front();
		}

		auto snapshot = std::make_shared<PlayerIOLoginSnapshot>();
		std::string error;
		bool playerIdReserved = false;
		try {
			DispatcherMetricsSuppressionScope metricsSuppression;
			playerio::ResultSet preload;
			const std::string preloadQuery = IOLoginData::buildPlayerPreloadQuery(task.name);
			if (!loginClient->query(preloadQuery, preload, error)) {
				if (error.empty()) {
					error = "character preload query failed";
				}
			} else if (preload.rows.empty()) {
				error = "character does not exist";
			} else {
				snapshot->playerId = resultNumber(preload, "id");
				snapshot->accountId = resultNumber(preload, "account_id");
				if (preload.rows.size() != 1 ||
						snapshot->playerId == 0 || snapshot->accountId == 0) {
					error = "character preload returned invalid identifiers";
				} else if (!reserveLoadingPlayerId(snapshot->playerId)) {
					error = "character is already loading, saving, or being changed";
				} else {
					playerIdReserved = true;
					std::vector<std::string> queries;
					queries.emplace_back(preloadQuery);
					std::vector<std::string> loadQueries =
						IOLoginData::buildPlayerLoadQueries(
							snapshot->playerId, snapshot->accountId);
					queries.insert(queries.end(),
						std::make_move_iterator(loadQueries.begin()),
						std::make_move_iterator(loadQueries.end()));
					std::vector<playerio::ResultSet> results;
					if (!loginClient->queryPlayerSnapshot(
							snapshot->playerId, queries, results,
							snapshot->committedRevision, error)) {
						if (error.empty()) {
							error = "could not read a consistent character snapshot";
						}
					} else if (results.size() != queries.size()) {
						error = "character snapshot returned an incomplete result set";
					} else if (results.size() < 3 ||
							results[0].rows.size() != 1 ||
							results[1].rows.size() != 1 ||
							results[2].rows.size() != 1) {
						error = "character snapshot is missing its required player or account row";
					} else if (
							resultNumber(results[0], "id") != snapshot->playerId ||
							resultNumber(results[0], "account_id") != snapshot->accountId ||
							normalizeName(resultString(results[0], "name")) !=
								normalizeName(task.name)) {
						error = "character identity changed while its login snapshot was being acquired";
					} else if (
							resultNumber(results[1], "id") != snapshot->playerId ||
							resultNumber(results[1], "account_id") != snapshot->accountId ||
							normalizeName(resultString(results[1], "name")) !=
								normalizeName(task.name)) {
						error = "character core row does not match the requested login identity";
					} else if (
							resultNumber(results[2], "id") != snapshot->accountId) {
						error = "character account row does not match the requested login identity";
					} else {
						snapshot->entries.reserve(results.size());
						for (size_t i = 0; i < queries.size(); ++i) {
							snapshot->entries.push_back(
								PlayerIOReadSnapshotEntry{queries[i], std::move(results[i])});
						}
					}
				}
			}
		} catch (const std::exception& exception) {
			error = fmt::format("player login worker exception: {:s}", exception.what());
		} catch (...) {
			error = "unknown player login worker exception";
		}

		const bool success = error.empty() && !snapshot->entries.empty();
		if (!success && playerIdReserved) {
			std::lock_guard<std::mutex> lock(activityMutex);
			loadingPlayerIds.erase(snapshot->playerId);
			snapshot->playerId = 0;
		}
		g_dispatcher.addTask(createTask(
			[task = std::move(task),
			 snapshot = success ? std::shared_ptr<const PlayerIOLoginSnapshot>(snapshot) : nullptr,
			 error]() mutable {
				task.completion(std::move(snapshot), error);
			}));
	}
}

void PlayerIOManager::logoutThreadMain()
{
	for (;;) {
		LogoutTask task;
		{
			std::unique_lock<std::mutex> lock(logoutMutex);
			logoutSignal.wait(lock, [&] { return stopping || !logoutTasks.empty(); });
			if (stopping) {
				return;
			}
			if (logoutTasks.empty()) {
				continue;
			}
			task = std::move(logoutTasks.front());
			logoutTasks.pop_front();
			++activeLogoutJobs;
		}

		bool success = false;
		std::string error;
		playerio::JobState state = playerio::JobState::UNKNOWN;
		uint64_t revision = task.revision;
		bool prepared = false;
		bool prepareResponseReceived = false;
		bool prepareAmbiguous = false;
		bool prepareServiceUnavailable = false;
		try {
			{
				DispatcherPhaseMetricsTimer prepareTimer(
					DispatcherMetricsPhase::LOGOUT_ASYNC_PREPARE_HANDOFF);
				for (uint8_t attempt = 0; attempt < 5 && !stopping; ++attempt) {
					error.clear();
					prepareResponseReceived = false;
					prepareAmbiguous = false;
					prepareServiceUnavailable = false;
					{
						std::lock_guard<std::mutex> handoffLock(handoffMutex);
						prepareResponseReceived = handoffClient->prepareSaveJob(
							task.jobId, task.playerId, task.statements, state,
							revision, prepareAmbiguous, prepareServiceUnavailable,
							error);
						prepared = prepareResponseReceived &&
							(state == playerio::JobState::PENDING ||
							 state == playerio::JobState::APPLYING ||
							 state == playerio::JobState::COMMITTED);

						// A lost PREPARE response is resolved by the exact idempotent
						// job id. It must never enable another writer.
						if (!prepared && prepareAmbiguous) {
							std::string jobError;
							std::string statusError;
							playerio::JobState resolvedState = playerio::JobState::UNKNOWN;
							uint64_t committedRevision = 0;
							if (handoffClient->getSaveJobStatus(task.jobId,
									resolvedState, committedRevision, jobError,
									statusError) &&
									resolvedState != playerio::JobState::UNKNOWN) {
								state = resolvedState;
								revision = committedRevision;
								error = jobError;
								prepared = state == playerio::JobState::PENDING ||
									state == playerio::JobState::APPLYING ||
									state == playerio::JobState::COMMITTED;
							} else if (!statusError.empty()) {
								if (error.empty()) {
									error = statusError;
								} else {
									error += "; status resolution failed: " +
										statusError;
								}
							}
						}
					}

					if (prepared || prepareAmbiguous || prepareResponseReceived) {
						break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
				}
			}

			if (prepared && state == playerio::JobState::COMMITTED) {
				success = true;
			} else if (prepared) {
				for (uint8_t attempt = 0; attempt < 5 && !stopping; ++attempt) {
					error.clear();
					if (logoutClient->applySaveJob(task.jobId, state, revision,
							error) && state == playerio::JobState::COMMITTED) {
						success = true;
						break;
					}
					if (state == playerio::JobState::FAILED) {
						break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
				}
			} else if (prepareAmbiguous || prepareResponseReceived) {
				if (error.empty()) {
					error = "durable PREPARE outcome is unresolved";
				}
			} else if (error.empty()) {
				error = "durable PREPARE did not reach the Player I/O service";
			}
		} catch (const std::exception& exception) {
			error = fmt::format("player logout worker exception: {:s}", exception.what());
		} catch (...) {
			error = "unknown player logout worker exception";
		}

		const bool retryable = !success && !stopping &&
			state != playerio::JobState::FAILED &&
			(prepared || prepareAmbiguous || prepareResponseReceived ||
			 prepareServiceUnavailable);
		if (retryable) {
			if (prepareServiceUnavailable) {
				updateServiceAvailability(false, error);
			}
			task.revision = revision;
			++task.retryCount;
			if (task.retryCount == 1 || task.retryCount % 30 == 0) {
				std::cerr << "Player I/O logout save remains pending for "
				          << task.playerName << " (job " << task.jobId
				          << ", retry " << task.retryCount << "): " << error
				          << ". Relog remains blocked; the same idempotent job will be retried."
				          << std::endl;
			}
			{
				std::lock_guard<std::mutex> lock(logoutMutex);
				// A retrying job must keep its per-player reservation, but it must not
				// monopolize the single logout worker. Move it behind jobs that have
				// not had their first attempt yet; cross-player save order is independent.
				logoutTasks.push_back(std::move(task));
				--activeLogoutJobs;
			}
			logoutDrainedSignal.notify_all();

			// This wait belongs only to the logout worker. It prevents a failed
			// service from causing a busy reconnect loop and never occupies the
			// Dispatcher. A health transition wakes it early.
			std::unique_lock<std::mutex> healthLock(healthMutex);
			healthSignal.wait_for(healthLock, std::chrono::seconds(2));
			continue;
		}

		if (!success && !stopping) {
			std::cerr << "Player I/O asynchronous save failed for " << task.playerName
			          << " (job " << task.jobId << "): " << error
			          << (prepared || prepareAmbiguous || prepareResponseReceived ?
				". The durable handoff may require recovery." :
				". No durable handoff was confirmed.")
			          << std::endl;
		}

		{
			std::lock_guard<std::mutex> lock(activityMutex);
			savingPlayers.erase(task.playerId);
		}
		{
			std::lock_guard<std::mutex> lock(logoutMutex);
			if (!success && !stopping &&
					(prepared || prepareAmbiguous || prepareResponseReceived)) {
				unresolvedLogoutJobs[task.playerId] = task.jobId;
			} else if (success) {
				unresolvedLogoutJobs.erase(task.playerId);
			}
			--activeLogoutJobs;
		}
		logoutDrainedSignal.notify_all();

		g_dispatcher.addTask(createTask(
			[task = std::move(task), success, error]() mutable {
				task.completion(success, error);
			}));
	}
}
