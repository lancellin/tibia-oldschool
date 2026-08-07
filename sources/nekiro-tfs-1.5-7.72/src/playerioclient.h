#ifndef FS_PLAYER_IO_CLIENT_H
#define FS_PLAYER_IO_CLIENT_H

#include "playerioprotocol.h"

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class PlayerIOClient {
public:
	PlayerIOClient(std::string host, uint16_t port,
		std::chrono::milliseconds operationTimeout = std::chrono::seconds(5));

	bool ping(std::string& error);
	bool query(const std::string& sql, playerio::ResultSet& result, std::string& error);
	bool queryPlayerSnapshot(uint32_t playerId, const std::vector<std::string>& queries,
		std::vector<playerio::ResultSet>& results, uint64_t& committedRevision, std::string& error);
	bool checkPlayerReady(uint32_t playerId, bool& ready, playerio::JobState& state,
		uint64_t& committedRevision, std::string& error);
	bool getSaveJobStatus(const std::string& jobId, playerio::JobState& state,
		uint64_t& committedRevision, std::string& jobError, std::string& error);
	bool prepareSaveJob(const std::string& jobId, uint32_t playerId,
		const std::vector<std::string>& statements, playerio::JobState& state,
		uint64_t& revision, bool& outcomeAmbiguous, bool& serviceUnavailable,
		std::string& error);
	bool applySaveJob(const std::string& jobId, playerio::JobState& state,
		uint64_t& revision, std::string& error);
	bool submitSaveJob(const std::string& jobId, uint32_t playerId, uint64_t revision,
		const std::vector<std::string>& statements, playerio::JobState& state,
		uint64_t& committedRevision, std::string& error);
	bool shutdownIfIdle(bool& accepted, uint32_t& pendingJobs, std::string& error);
	bool armShutdownWhenIdle(std::string& error);

	void close();

private:
	using OperationDeadline = std::chrono::steady_clock::time_point;

	bool ensureConnected(OperationDeadline deadline, std::string& error);
	bool writeFrameTimed(const std::vector<uint8_t>& payload,
		OperationDeadline deadline, std::string& error);
	bool readFrameTimed(std::vector<uint8_t>& payload,
		OperationDeadline deadline, std::string& error);
	bool exchange(playerio::Opcode expectedOpcode, uint64_t expectedRequestId,
		const playerio::Writer& request, std::vector<uint8_t>& response,
		bool& operationSuccess, OperationDeadline deadline, std::string& error);
	uint64_t nextRequestId();

	std::string host;
	uint16_t port;
	std::chrono::milliseconds operationTimeout;
	boost::asio::io_context ioContext;
	std::unique_ptr<boost::asio::ip::tcp::socket> socket;
	std::atomic<uint64_t> requestSequence{1};
	std::recursive_mutex operationMutex;
};

#endif
