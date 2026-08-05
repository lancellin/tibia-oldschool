#ifndef FS_PLAYER_IO_MANAGER_H
#define FS_PLAYER_IO_MANAGER_H

#include "playerioclient.h"
#include "playeriodatabase.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;

struct PlayerIOLoginSnapshot {
	uint32_t playerId = 0;
	uint32_t accountId = 0;
	uint64_t committedRevision = 0;
	std::vector<PlayerIOReadSnapshotEntry> entries;
};

class PlayerIOManager {
public:
	static constexpr std::chrono::seconds SHUTDOWN_DRAIN_TIMEOUT{330};

	using Completion = std::function<void(bool, const std::string&)>;
	using LoginCompletion = std::function<void(
		std::shared_ptr<const PlayerIOLoginSnapshot>, const std::string&)>;

	bool start();
	void shutdown();
	void shutdown(bool stopServiceIfIdle);

	bool isEnabled() const {
		return enabled;
	}
	bool isServiceAvailable() const {
		return serviceAvailable.load(std::memory_order_acquire);
	}

	bool enqueueLogin(const std::string& name, LoginCompletion completion);
	bool enqueueLogout(Player* player, Completion completion);

	bool reserveLogin(const std::string& name);
	void releaseLogin(const std::string& name, uint32_t playerId = 0);
	bool isSaving(uint32_t playerId) const;

	bool reserveLegacyOperation(uint32_t playerId);
	void releaseLegacyOperation(uint32_t playerId);
	bool drain(std::chrono::milliseconds timeout, std::string& error);

	size_t pendingLoginCount() const;
	size_t pendingLogoutCount() const;

private:
	struct LoginTask {
		std::string name;
		LoginCompletion completion;
	};

	struct LogoutTask {
		uint32_t playerId = 0;
		std::string playerName;
		std::string jobId;
		std::vector<std::string> statements;
		uint64_t revision = 0;
		uint32_t retryCount = 0;
		Completion completion;
	};

	void loginThreadMain();
	void logoutThreadMain();
	void healthThreadMain();
	std::string makeJobId(uint32_t playerId) const;
	bool reserveLoadingPlayerId(uint32_t playerId);
	void updateServiceAvailability(bool available, const std::string& error = {});

	bool enabled = false;
	std::atomic<bool> stopping{false};
	std::atomic<bool> serviceAvailable{false};
	std::string host;
	uint16_t port = 0;

	std::unique_ptr<PlayerIOClient> loginClient;
	std::unique_ptr<PlayerIOClient> logoutClient;
	std::unique_ptr<PlayerIOClient> handoffClient;
	std::unique_ptr<PlayerIOClient> healthClient;
	mutable std::mutex handoffMutex;
	std::thread loginThread;
	std::thread logoutThread;
	std::thread healthThread;
	std::mutex healthMutex;
	std::condition_variable healthSignal;

	mutable std::mutex loginMutex;
	std::condition_variable loginSignal;
	std::deque<LoginTask> loginTasks;
	std::unordered_set<std::string> loadingNames;

	mutable std::mutex logoutMutex;
	std::condition_variable logoutSignal;
	std::condition_variable logoutDrainedSignal;
	std::deque<LogoutTask> logoutTasks;
	size_t activeLogoutJobs = 0;
	std::unordered_map<uint32_t, std::string> unresolvedLogoutJobs;

	mutable std::mutex activityMutex;
	std::unordered_set<uint32_t> loadingPlayerIds;
	std::unordered_set<uint32_t> savingPlayers;
	std::unordered_set<uint32_t> legacyPlayers;
};

extern PlayerIOManager g_playerIOManager;

#endif
