#include "otpch.h"

#include "playeriodatabase.h"

namespace {
thread_local PlayerIORemoteDatabaseScope* remoteDatabaseScope = nullptr;
}

std::string makePlayerIOSqlLiteral(std::string_view value)
{
	static constexpr char hex[] = "0123456789ABCDEF";
	std::string literal;
	literal.reserve(value.size() * 2 + 3);
	literal.append("X'");
	for (const char character : value) {
		const uint8_t byte = static_cast<uint8_t>(character);
		literal.push_back(hex[byte >> 4]);
		literal.push_back(hex[byte & 0x0F]);
	}
	literal.push_back('\'');
	return literal;
}

PlayerIORemoteDatabaseScope::PlayerIORemoteDatabaseScope(PlayerIOClient& client,
	bool collectWrites, std::string jobId, uint32_t playerId, uint64_t revision) :
	client(&client),
	previous(remoteDatabaseScope),
	collectWrites(collectWrites),
	jobId(std::move(jobId)),
	playerId(playerId),
	revision(revision)
{
	remoteDatabaseScope = this;
}

PlayerIORemoteDatabaseScope::PlayerIORemoteDatabaseScope(
	std::vector<std::string>& collectedStatements) :
	previous(remoteDatabaseScope),
	mode(Mode::COLLECT_WRITES),
	collectWrites(true),
	collectedStatements(&collectedStatements)
{
	remoteDatabaseScope = this;
}

PlayerIORemoteDatabaseScope::PlayerIORemoteDatabaseScope(
	const std::vector<PlayerIOReadSnapshotEntry>& replayEntries) :
	previous(remoteDatabaseScope),
	mode(Mode::REPLAY_READS),
	replayEntries(&replayEntries)
{
	remoteDatabaseScope = this;
}

PlayerIORemoteDatabaseScope::~PlayerIORemoteDatabaseScope()
{
	remoteDatabaseScope = previous;
}

PlayerIORemoteDatabaseScope* PlayerIORemoteDatabaseScope::current()
{
	return remoteDatabaseScope;
}

DBResult_ptr PlayerIORemoteDatabaseScope::storeQuery(const std::string& query)
{
	if (mode == Mode::COLLECT_WRITES) {
		error = "immutable player save snapshot attempted a read query";
		return nullptr;
	}

	if (mode == Mode::REPLAY_READS) {
		if (!replayEntries || replayIndex >= replayEntries->size()) {
			error = "player login snapshot did not contain the requested query";
			return nullptr;
		}

		const PlayerIOReadSnapshotEntry& entry = (*replayEntries)[replayIndex++];
		if (entry.query != query) {
			error = "player login snapshot query order mismatch";
			return nullptr;
		}
		if (entry.result.rows.empty()) {
			return nullptr;
		}
		return std::make_shared<DBResult>(entry.result.columns, entry.result.rows);
	}

	playerio::ResultSet result;
	if (!client || !client->query(query, result, error)) {
		return nullptr;
	}
	if (result.rows.empty()) {
		return nullptr;
	}
	return std::make_shared<DBResult>(std::move(result.columns), std::move(result.rows));
}

bool PlayerIORemoteDatabaseScope::executeQuery(const std::string& query)
{
	if (!collectWrites) {
		error = "remote player I/O read scope rejected a write query";
		return false;
	}
	statements.push_back(query);
	return true;
}

bool PlayerIORemoteDatabaseScope::beginTransaction()
{
	if (!collectWrites || transactionStarted) {
		error = "invalid remote player save transaction begin";
		return false;
	}
	transactionStarted = true;
	return true;
}

bool PlayerIORemoteDatabaseScope::commit()
{
	if (!collectWrites || !transactionStarted) {
		error = "invalid remote player save transaction commit";
		return false;
	}

	if (mode == Mode::COLLECT_WRITES) {
		if (!collectedStatements) {
			error = "immutable player save snapshot has no output";
			return false;
		}
		*collectedStatements = statements;
		transactionStarted = false;
		return true;
	}

	if (!client || jobId.empty() || playerId == 0) {
		error = "invalid remote player save job";
		return false;
	}

	uint64_t committedRevision = 0;
	const bool success = client->submitSaveJob(jobId, playerId, revision, statements,
		jobState, committedRevision, error);
	transactionStarted = false;
	return success && jobState == playerio::JobState::COMMITTED;
}

bool PlayerIORemoteDatabaseScope::rollback()
{
	statements.clear();
	transactionStarted = false;
	return true;
}

std::string PlayerIORemoteDatabaseScope::escapeBlob(const char* data, uint32_t length) const
{
	return makePlayerIOSqlLiteral(std::string_view(data, length));
}
