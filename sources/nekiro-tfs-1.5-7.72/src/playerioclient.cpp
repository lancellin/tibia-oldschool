#include "otpch.h"

#include "playerioclient.h"

#include <array>
#include <chrono>

using boost::asio::ip::tcp;

namespace {

template <typename StartOperation>
bool runSocketOperation(boost::asio::io_context& ioContext, tcp::socket& socket,
	const char* operation, std::chrono::milliseconds timeout,
	StartOperation&& startOperation, std::string& error)
{
	boost::asio::steady_timer timer(ioContext);
	boost::system::error_code operationError;
	bool completed = false;
	bool timedOut = false;

	ioContext.restart();
	timer.expires_after(timeout);
	timer.async_wait([&](const boost::system::error_code& timerError) {
		if (!timerError && !completed) {
			timedOut = true;
			boost::system::error_code ignored;
			socket.cancel(ignored);
		}
	});
	startOperation([&](const boost::system::error_code& result, std::size_t) {
		operationError = result;
		completed = true;
		(void)timer.cancel();
	});
	ioContext.run();

	if (timedOut) {
		error = std::string("player I/O ") + operation + " timed out";
		return false;
	}
	if (!completed || operationError) {
		error = std::string("player I/O ") + operation + " failed: " +
			(operationError ? operationError.message() : "operation was not completed");
		return false;
	}
	return true;
}

bool resolveWithTimeout(boost::asio::io_context& ioContext, const std::string& host, uint16_t port,
	tcp::resolver::results_type& endpoints, std::chrono::milliseconds timeout,
	std::string& error)
{
	tcp::resolver resolver(ioContext);
	boost::asio::steady_timer timer(ioContext);
	boost::system::error_code resolveError;
	bool completed = false;
	bool timedOut = false;

	ioContext.restart();
	timer.expires_after(timeout);
	timer.async_wait([&](const boost::system::error_code& timerError) {
		if (!timerError && !completed) {
			timedOut = true;
			resolver.cancel();
		}
	});
	resolver.async_resolve(host, std::to_string(port), [&](const boost::system::error_code& result,
		const tcp::resolver::results_type& value) {
		resolveError = result;
		if (!result) {
			endpoints = value;
		}
		completed = true;
		(void)timer.cancel();
	});
	ioContext.run();

	if (timedOut) {
		error = "player I/O resolve timed out";
		return false;
	}
	if (!completed || resolveError) {
		error = "player I/O resolve failed: " +
			(resolveError ? resolveError.message() : "operation was not completed");
		return false;
	}
	return true;
}

std::chrono::milliseconds remainingTimeout(
	std::chrono::steady_clock::time_point deadline, std::string& error)
{
	const auto now = std::chrono::steady_clock::now();
	if (now >= deadline) {
		error = "player I/O operation timed out";
		return std::chrono::milliseconds::zero();
	}
	return std::max(std::chrono::milliseconds(1),
		std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
}

} // namespace

PlayerIOClient::PlayerIOClient(std::string host, uint16_t port,
	std::chrono::milliseconds operationTimeout) :
	host(std::move(host)), port(port), operationTimeout(operationTimeout)
{
}

uint64_t PlayerIOClient::nextRequestId()
{
	return requestSequence.fetch_add(1, std::memory_order_relaxed);
}

void PlayerIOClient::close()
{
	std::lock_guard<std::recursive_mutex> lock(operationMutex);
	if (!socket) {
		return;
	}
	boost::system::error_code error;
	socket->shutdown(tcp::socket::shutdown_both, error);
	socket->close(error);
	socket.reset();
}

bool PlayerIOClient::ensureConnected(OperationDeadline deadline, std::string& error)
{
	if (socket && socket->is_open()) {
		return true;
	}

	try {
		tcp::resolver::results_type endpoints;
		const std::chrono::milliseconds resolveTimeout = remainingTimeout(deadline, error);
		if (resolveTimeout == std::chrono::milliseconds::zero() ||
				!resolveWithTimeout(ioContext, host, port, endpoints, resolveTimeout, error)) {
			return false;
		}

		socket = std::make_unique<tcp::socket>(ioContext);
		boost::asio::steady_timer timer(ioContext);
		boost::system::error_code connectError;
		bool completed = false;
		bool timedOut = false;
		ioContext.restart();
		const std::chrono::milliseconds connectTimeout = remainingTimeout(deadline, error);
		if (connectTimeout == std::chrono::milliseconds::zero()) {
			close();
			return false;
		}
		timer.expires_after(connectTimeout);
		timer.async_wait([&](const boost::system::error_code& timerError) {
			if (!timerError && !completed) {
				timedOut = true;
				boost::system::error_code ignored;
				socket->cancel(ignored);
			}
		});
		boost::asio::async_connect(*socket, endpoints,
			[&](const boost::system::error_code& result, const tcp::endpoint&) {
				connectError = result;
				completed = true;
				(void)timer.cancel();
			});
		ioContext.run();
		if (timedOut) {
			error = "player I/O connect timed out";
			close();
			return false;
		}
		if (!completed || connectError) {
			error = "player I/O connect failed: " +
				(connectError ? connectError.message() : "operation was not completed");
			close();
			return false;
		}
		socket->set_option(tcp::no_delay(true));
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		close();
		return false;
	}
}

bool PlayerIOClient::writeFrameTimed(const std::vector<uint8_t>& payload,
	OperationDeadline deadline, std::string& error)
{
	if (!socket || payload.size() > playerio::PLAYER_IO_MAX_FRAME_SIZE) {
		error = "invalid player I/O outbound frame";
		return false;
	}
	const std::array<uint8_t, 4> header{{
		static_cast<uint8_t>((payload.size() >> 24) & 0xFF),
		static_cast<uint8_t>((payload.size() >> 16) & 0xFF),
		static_cast<uint8_t>((payload.size() >> 8) & 0xFF),
		static_cast<uint8_t>(payload.size() & 0xFF),
	}};
	std::array<boost::asio::const_buffer, 2> buffers{{
		boost::asio::buffer(header), boost::asio::buffer(payload),
	}};
	const std::chrono::milliseconds timeout = remainingTimeout(deadline, error);
	if (timeout == std::chrono::milliseconds::zero()) {
		return false;
	}
	return runSocketOperation(ioContext, *socket, "write", timeout, [&](auto&& handler) {
		boost::asio::async_write(*socket, buffers, std::forward<decltype(handler)>(handler));
	}, error);
}

bool PlayerIOClient::readFrameTimed(std::vector<uint8_t>& payload,
	OperationDeadline deadline, std::string& error)
{
	if (!socket) {
		error = "player I/O socket is not connected";
		return false;
	}
	std::array<uint8_t, 4> header{};
	std::chrono::milliseconds timeout = remainingTimeout(deadline, error);
	if (timeout == std::chrono::milliseconds::zero() ||
			!runSocketOperation(ioContext, *socket, "header read", timeout, [&](auto&& handler) {
		boost::asio::async_read(*socket, boost::asio::buffer(header), std::forward<decltype(handler)>(handler));
	}, error)) {
		return false;
	}
	const uint32_t size = static_cast<uint32_t>(header[0]) << 24 |
		static_cast<uint32_t>(header[1]) << 16 |
		static_cast<uint32_t>(header[2]) << 8 |
		static_cast<uint32_t>(header[3]);
	if (size > playerio::PLAYER_IO_MAX_FRAME_SIZE) {
		error = "player I/O inbound frame exceeds maximum size";
		return false;
	}
	payload.assign(size, 0);
	if (size == 0) {
		return true;
	}
	timeout = remainingTimeout(deadline, error);
	if (timeout == std::chrono::milliseconds::zero()) {
		return false;
	}
	return runSocketOperation(ioContext, *socket, "body read", timeout, [&](auto&& handler) {
		boost::asio::async_read(*socket, boost::asio::buffer(payload), std::forward<decltype(handler)>(handler));
	}, error);
}

bool PlayerIOClient::exchange(playerio::Opcode expectedOpcode, uint64_t expectedRequestId,
	const playerio::Writer& request, std::vector<uint8_t>& response, bool& operationSuccess,
	OperationDeadline deadline, std::string& error)
{
	try {
		if (!writeFrameTimed(request.data(), deadline, error) ||
				!readFrameTimed(response, deadline, error)) {
			close();
			return false;
		}
		playerio::Reader reader(response);
		playerio::Opcode opcode;
		uint64_t requestId;
		playerio::readResponseEnvelope(reader, opcode, requestId, operationSuccess, error);
		if (opcode != expectedOpcode || requestId != expectedRequestId) {
			error = "mismatched player I/O response";
			close();
			return false;
		}
		// The service handles each frame independently. Do not leave a local TCP
		// channel idle between operations: on Windows an idle loopback channel can
		// later time out server-side and leave the TFS socket in CLOSE_WAIT. Closing
		// after the complete response gives both peers an ordinary EOF boundary; the
		// next operation reconnects through ensureConnected().
		close();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		close();
		return false;
	}
}

bool PlayerIOClient::ping(std::string& error)
{
	std::lock_guard<std::recursive_mutex> lock(operationMutex);
	const OperationDeadline deadline =
		std::chrono::steady_clock::now() + operationTimeout;
	const uint64_t requestId = nextRequestId();
	playerio::Writer request;
	playerio::addEnvelope(request, playerio::Opcode::PING, requestId);
	for (uint32_t attempt = 0; attempt < 2; ++attempt) {
		if (!ensureConnected(deadline, error)) {
			continue;
		}
		std::vector<uint8_t> payload;
		bool success = false;
		if (!exchange(playerio::Opcode::PING, requestId, request, payload, success,
				deadline, error) || !success) {
			continue;
		}
		try {
			playerio::Reader reader(payload);
			playerio::Opcode opcode;
			uint64_t responseId;
			bool responseSuccess;
			std::string responseError;
			playerio::readResponseEnvelope(reader, opcode, responseId, responseSuccess, responseError);
			(void)reader.getString();
			return reader.empty();
		} catch (const std::exception& exception) {
			error = exception.what();
			close();
		}
	}
	return false;
}

bool PlayerIOClient::query(const std::string& sql, playerio::ResultSet& result, std::string& error)
{
	std::lock_guard<std::recursive_mutex> lock(operationMutex);
	const OperationDeadline deadline =
		std::chrono::steady_clock::now() + operationTimeout;
	const uint64_t requestId = nextRequestId();
	playerio::Writer request;
	playerio::addEnvelope(request, playerio::Opcode::QUERY, requestId);
	request.addString(sql);
	for (uint32_t attempt = 0; attempt < 2; ++attempt) {
		if (!ensureConnected(deadline, error)) {
			continue;
		}
		std::vector<uint8_t> payload;
		bool success = false;
		if (!exchange(playerio::Opcode::QUERY, requestId, request, payload, success,
				deadline, error) || !success) {
			continue;
		}
		try {
			playerio::Reader reader(payload);
			playerio::Opcode opcode;
			uint64_t responseId;
			bool responseSuccess;
			std::string responseError;
			playerio::readResponseEnvelope(reader, opcode, responseId, responseSuccess, responseError);
			result = playerio::readResultSet(reader);
			if (!reader.empty()) {
				throw std::runtime_error("unexpected bytes after player I/O query response");
			}
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			close();
		}
	}
	return false;
}

bool PlayerIOClient::queryPlayerSnapshot(uint32_t playerId, const std::vector<std::string>& queries,
	std::vector<playerio::ResultSet>& results, uint64_t& committedRevision, std::string& error)
{
	std::lock_guard<std::recursive_mutex> lock(operationMutex);
	const OperationDeadline deadline =
		std::chrono::steady_clock::now() + operationTimeout;
	if (playerId == 0 || queries.empty() || queries.size() > 64) {
		error = "invalid player I/O snapshot request";
		return false;
	}
	const uint64_t requestId = nextRequestId();
	playerio::Writer request;
	playerio::addEnvelope(request, playerio::Opcode::QUERY_BATCH, requestId);
	request.addU32(playerId);
	request.addU32(static_cast<uint32_t>(queries.size()));
	for (const std::string& query : queries) {
		request.addString(query);
	}
	for (uint32_t attempt = 0; attempt < 2; ++attempt) {
		if (!ensureConnected(deadline, error)) {
			continue;
		}
		std::vector<uint8_t> payload;
		bool success = false;
		if (!exchange(playerio::Opcode::QUERY_BATCH, requestId, request, payload, success,
				deadline, error) || !success) {
			continue;
		}
		try {
			playerio::Reader reader(payload);
			playerio::Opcode opcode;
			uint64_t responseId;
			bool responseSuccess;
			std::string responseError;
			playerio::readResponseEnvelope(reader, opcode, responseId, responseSuccess, responseError);
			committedRevision = reader.getU64();
			const uint32_t count = reader.getU32();
			if (count != queries.size()) {
				throw std::runtime_error("player I/O snapshot result count mismatch");
			}
			results.clear();
			results.reserve(count);
			for (uint32_t i = 0; i < count; ++i) {
				results.emplace_back(playerio::readResultSet(reader));
			}
			if (!reader.empty()) {
				throw std::runtime_error("unexpected bytes after player I/O snapshot response");
			}
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			close();
		}
	}
	return false;
}

bool PlayerIOClient::checkPlayerReady(uint32_t playerId, bool& ready, playerio::JobState& state,
	uint64_t& committedRevision, std::string& error)
{
	std::lock_guard<std::recursive_mutex> lock(operationMutex);
	const OperationDeadline deadline =
		std::chrono::steady_clock::now() + operationTimeout;
	const uint64_t requestId = nextRequestId();
	playerio::Writer request;
	playerio::addEnvelope(request, playerio::Opcode::CHECK_PLAYER_READY, requestId);
	request.addU32(playerId);
	for (uint32_t attempt = 0; attempt < 2; ++attempt) {
		if (!ensureConnected(deadline, error)) {
			continue;
		}
		std::vector<uint8_t> payload;
		bool success = false;
		if (!exchange(playerio::Opcode::CHECK_PLAYER_READY, requestId, request, payload,
				success, deadline, error) || !success) {
			continue;
		}
		try {
			playerio::Reader reader(payload);
			playerio::Opcode opcode;
			uint64_t responseId;
			bool responseSuccess;
			std::string responseError;
			playerio::readResponseEnvelope(reader, opcode, responseId, responseSuccess, responseError);
			ready = reader.getBool();
			state = static_cast<playerio::JobState>(reader.getU8());
			const std::string stateError = reader.getString();
			if (!stateError.empty()) {
				error = stateError;
			}
			committedRevision = reader.getU64();
			if (!reader.empty()) {
				throw std::runtime_error("unexpected bytes after player I/O readiness response");
			}
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			close();
		}
	}
	return false;
}

bool PlayerIOClient::prepareSaveJob(const std::string& jobId, uint32_t playerId,
	const std::vector<std::string>& statements, playerio::JobState& state,
	uint64_t& revision, bool& outcomeAmbiguous, bool& serviceUnavailable,
	std::string& error)
{
	std::lock_guard<std::recursive_mutex> lock(operationMutex);
	outcomeAmbiguous = false;
	serviceUnavailable = false;
	const OperationDeadline deadline =
		std::chrono::steady_clock::now() + operationTimeout;
	const uint64_t requestId = nextRequestId();
	playerio::Writer request;
	playerio::addEnvelope(request, playerio::Opcode::PREPARE_SAVE_JOB, requestId);
	request.addString(jobId);
	request.addU32(playerId);
	request.addU32(static_cast<uint32_t>(statements.size()));
	for (const std::string& statement : statements) {
		request.addString(statement);
	}
	if (request.data().size() > playerio::PLAYER_IO_MAX_FRAME_SIZE) {
		error = "invalid player I/O outbound frame";
		return false;
	}
	bool requestAttempted = false;
	for (uint32_t attempt = 0; attempt < 2; ++attempt) {
		if (!ensureConnected(deadline, error)) {
			continue;
		}
		requestAttempted = true;
		std::vector<uint8_t> payload;
		bool success = false;
		if (!exchange(playerio::Opcode::PREPARE_SAVE_JOB, requestId, request, payload,
				success, deadline, error) || !success) {
			continue;
		}
		try {
			playerio::Reader reader(payload);
			playerio::Opcode opcode;
			uint64_t responseId;
			bool responseSuccess;
			std::string responseError;
			playerio::readResponseEnvelope(reader, opcode, responseId, responseSuccess, responseError);
			state = static_cast<playerio::JobState>(reader.getU8());
			revision = reader.getU64();
			if (!reader.empty()) {
				throw std::runtime_error("unexpected bytes after player I/O prepare response");
			}
			outcomeAmbiguous = false;
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			close();
		}
	}
	outcomeAmbiguous = requestAttempted;
	serviceUnavailable = !requestAttempted;
	return false;
}

bool PlayerIOClient::getSaveJobStatus(const std::string& jobId,
	playerio::JobState& state, uint64_t& committedRevision,
	std::string& jobError, std::string& error)
{
	std::lock_guard<std::recursive_mutex> lock(operationMutex);
	const OperationDeadline deadline =
		std::chrono::steady_clock::now() + operationTimeout;
	const uint64_t requestId = nextRequestId();
	playerio::Writer request;
	playerio::addEnvelope(request, playerio::Opcode::JOB_STATUS, requestId);
	request.addString(jobId);
	for (uint32_t attempt = 0; attempt < 2; ++attempt) {
		if (!ensureConnected(deadline, error)) {
			continue;
		}
		std::vector<uint8_t> payload;
		bool success = false;
		if (!exchange(playerio::Opcode::JOB_STATUS, requestId, request, payload,
				success, deadline, error) || !success) {
			continue;
		}
		try {
			playerio::Reader reader(payload);
			playerio::Opcode opcode;
			uint64_t responseId;
			bool responseSuccess;
			std::string responseError;
			playerio::readResponseEnvelope(
				reader, opcode, responseId, responseSuccess, responseError);
			state = static_cast<playerio::JobState>(reader.getU8());
			jobError = reader.getString();
			committedRevision = reader.getU64();
			if (!reader.empty()) {
				throw std::runtime_error("unexpected bytes after player I/O job status response");
			}
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			close();
		}
	}
	return false;
}

bool PlayerIOClient::applySaveJob(const std::string& jobId, playerio::JobState& state,
	uint64_t& revision, std::string& error)
{
	std::lock_guard<std::recursive_mutex> lock(operationMutex);
	const OperationDeadline deadline =
		std::chrono::steady_clock::now() + operationTimeout;
	const uint64_t requestId = nextRequestId();
	playerio::Writer request;
	playerio::addEnvelope(request, playerio::Opcode::APPLY_SAVE_JOB, requestId);
	request.addString(jobId);
	for (uint32_t attempt = 0; attempt < 2; ++attempt) {
		if (!ensureConnected(deadline, error)) {
			continue;
		}
		std::vector<uint8_t> payload;
		bool success = false;
		if (!exchange(playerio::Opcode::APPLY_SAVE_JOB, requestId, request, payload,
				success, deadline, error)) {
			continue;
		}
		try {
			playerio::Reader reader(payload);
			playerio::Opcode opcode;
			uint64_t responseId;
			bool responseSuccess;
			std::string responseError;
			playerio::readResponseEnvelope(reader, opcode, responseId, responseSuccess, responseError);
			state = static_cast<playerio::JobState>(reader.getU8());
			revision = reader.getU64();
			if (!reader.empty()) {
				throw std::runtime_error("unexpected bytes after player I/O apply response");
			}
			return success && state == playerio::JobState::COMMITTED;
		} catch (const std::exception& exception) {
			error = exception.what();
			close();
		}
	}
	return false;
}

bool PlayerIOClient::submitSaveJob(const std::string& jobId, uint32_t playerId, uint64_t revision,
	const std::vector<std::string>& statements, playerio::JobState& state,
	uint64_t& committedRevision, std::string& error)
{
	std::lock_guard<std::recursive_mutex> lock(operationMutex);
	(void)revision;
	bool prepareAmbiguous = false;
	bool serviceUnavailable = false;
	if (!prepareSaveJob(jobId, playerId, statements, state, committedRevision,
			prepareAmbiguous, serviceUnavailable, error)) {
		return false;
	}
	if (state == playerio::JobState::COMMITTED) {
		return true;
	}
	return applySaveJob(jobId, state, committedRevision, error);
}

bool PlayerIOClient::shutdownIfIdle(bool& accepted, uint32_t& pendingJobs,
	std::string& error)
{
	std::lock_guard<std::recursive_mutex> lock(operationMutex);
	accepted = false;
	pendingJobs = 0;
	const OperationDeadline deadline =
		std::chrono::steady_clock::now() + operationTimeout;
	const uint64_t requestId = nextRequestId();
	playerio::Writer request;
	playerio::addEnvelope(request, playerio::Opcode::SHUTDOWN_IF_IDLE, requestId);

	if (!ensureConnected(deadline, error)) {
		return false;
	}

	std::vector<uint8_t> payload;
	bool success = false;
	if (!exchange(playerio::Opcode::SHUTDOWN_IF_IDLE, requestId, request, payload,
			success, deadline, error) || !success) {
		return false;
	}

	try {
		playerio::Reader reader(payload);
		playerio::Opcode opcode;
		uint64_t responseId;
		bool responseSuccess;
		std::string responseError;
		playerio::readResponseEnvelope(
			reader, opcode, responseId, responseSuccess, responseError);
		accepted = reader.getBool();
		pendingJobs = reader.getU32();
		if (!reader.empty()) {
			throw std::runtime_error(
				"unexpected bytes after player I/O shutdown response");
		}
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		close();
		return false;
	}
}

bool PlayerIOClient::armShutdownWhenIdle(std::string& error)
{
	std::lock_guard<std::recursive_mutex> lock(operationMutex);
	const OperationDeadline deadline =
		std::chrono::steady_clock::now() + operationTimeout;
	const uint64_t requestId = nextRequestId();
	playerio::Writer request;
	playerio::addEnvelope(request, playerio::Opcode::SHUTDOWN_WHEN_IDLE, requestId);

	if (!ensureConnected(deadline, error)) {
		return false;
	}

	std::vector<uint8_t> payload;
	bool success = false;
	if (!exchange(playerio::Opcode::SHUTDOWN_WHEN_IDLE, requestId, request, payload,
			success, deadline, error) || !success) {
		return false;
	}
	return true;
}
