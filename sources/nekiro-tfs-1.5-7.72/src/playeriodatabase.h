#ifndef FS_PLAYER_IO_DATABASE_H
#define FS_PLAYER_IO_DATABASE_H

#include "database.h"
#include "playerioclient.h"

#include <string>
#include <string_view>
#include <vector>

std::string makePlayerIOSqlLiteral(std::string_view value);

struct PlayerIOReadSnapshotEntry {
	std::string query;
	playerio::ResultSet result;
};

class PlayerIORemoteDatabaseScope {
public:
	PlayerIORemoteDatabaseScope(PlayerIOClient& client, bool collectWrites,
		std::string jobId = {}, uint32_t playerId = 0, uint64_t revision = 0);
	explicit PlayerIORemoteDatabaseScope(std::vector<std::string>& collectedStatements);
	explicit PlayerIORemoteDatabaseScope(const std::vector<PlayerIOReadSnapshotEntry>& replayEntries);
	~PlayerIORemoteDatabaseScope();

	PlayerIORemoteDatabaseScope(const PlayerIORemoteDatabaseScope&) = delete;
	PlayerIORemoteDatabaseScope& operator=(const PlayerIORemoteDatabaseScope&) = delete;

	DBResult_ptr storeQuery(const std::string& query);
	bool executeQuery(const std::string& query);
	bool beginTransaction();
	bool commit();
	bool rollback();
	std::string escapeBlob(const char* data, uint32_t length) const;
	bool replayComplete() const {
		return mode != Mode::REPLAY_READS ||
			(replayEntries && replayIndex == replayEntries->size());
	}
	bool isReplayRead() const {
		return mode == Mode::REPLAY_READS;
	}

	const std::string& getError() const {
		return error;
	}

	playerio::JobState getJobState() const {
		return jobState;
	}

	static PlayerIORemoteDatabaseScope* current();

private:
	enum class Mode : uint8_t {
		REMOTE,
		COLLECT_WRITES,
		REPLAY_READS,
	};

	PlayerIOClient* client = nullptr;
	PlayerIORemoteDatabaseScope* previous = nullptr;
	Mode mode = Mode::REMOTE;
	bool collectWrites = false;
	bool transactionStarted = false;
	std::string jobId;
	uint32_t playerId = 0;
	uint64_t revision = 0;
	std::vector<std::string> statements;
	std::vector<std::string>* collectedStatements = nullptr;
	const std::vector<PlayerIOReadSnapshotEntry>* replayEntries = nullptr;
	size_t replayIndex = 0;
	std::string error;
	playerio::JobState jobState = playerio::JobState::UNKNOWN;
};

#endif
