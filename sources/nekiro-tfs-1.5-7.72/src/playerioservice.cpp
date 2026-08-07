#include "playerioprotocol.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#endif

#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#include <boost/asio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using boost::asio::ip::tcp;

constexpr uint8_t JOB_PENDING = 1;
constexpr uint8_t JOB_APPLYING = 2;
constexpr uint8_t JOB_COMMITTED = 3;
constexpr uint8_t JOB_FAILED = 4;
constexpr std::chrono::milliseconds CLIENT_SOCKET_TIMEOUT{10000};
std::atomic<bool> serviceStopRequested{false};
// Armed by a shutting-down TFS that left durable work behind: once set, the
// service stops itself as soon as the durable queue drains and no client is
// connected, instead of running forever with nobody to request a shutdown.
std::atomic<bool> serviceStopWhenIdle{false};
std::atomic<int> activeClientConnections{0};

void configureClientSocketTimeouts(tcp::socket& socket)
{
#ifdef _WIN32
	const DWORD timeout = static_cast<DWORD>(CLIENT_SOCKET_TIMEOUT.count());
	const char* value = reinterpret_cast<const char*>(&timeout);
#else
	const timeval timeout{
		static_cast<time_t>(CLIENT_SOCKET_TIMEOUT.count() / 1000),
		static_cast<suseconds_t>((CLIENT_SOCKET_TIMEOUT.count() % 1000) * 1000)};
	const void* value = &timeout;
#endif
	if (::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, value,
			static_cast<int>(sizeof(timeout))) != 0 ||
			::setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, value,
				static_cast<int>(sizeof(timeout))) != 0) {
		throw std::runtime_error("could not configure player I/O client socket timeouts");
	}
}

bool isTransientMariaDBError(unsigned int error)
{
	return error == CR_SERVER_LOST ||
		error == CR_SERVER_GONE_ERROR ||
		error == CR_CONN_HOST_ERROR ||
		error == CR_CONNECTION_ERROR ||
		error == 1053 || // ER_SERVER_SHUTDOWN
		error == 1205 || // ER_LOCK_WAIT_TIMEOUT
		error == 1213;   // ER_LOCK_DEADLOCK
}

class ServiceDatabaseError : public std::runtime_error {
public:
	ServiceDatabaseError(unsigned int code, const std::string& message) :
		std::runtime_error(message), errorCode(code), transient(isTransientMariaDBError(code))
	{
	}

	bool isTransient() const {
		return transient;
	}

	unsigned int code() const {
		return errorCode;
	}

private:
	unsigned int errorCode;
	bool transient;
};

// MariaDB resolves lock cycles by aborting one transaction (1213) and reports
// lock wait timeouts as 1205. Both clear as soon as the conflicting
// transaction finishes, usually within milliseconds. Retrying immediately with
// a short backoff keeps a contended login snapshot or durable PREPARE from
// surfacing as a user-visible failure during heavy login/logout bursts.
constexpr uint32_t LOCK_RETRY_ATTEMPTS = 3;

bool isRetryableLockError(const std::exception& exception)
{
	const auto* databaseError = dynamic_cast<const ServiceDatabaseError*>(&exception);
	return databaseError &&
		(databaseError->code() == 1205 || databaseError->code() == 1213);
}

void sleepBeforeLockRetry(uint32_t attempt)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(50 * (attempt + 1)));
}

struct ServiceConfig {
	std::string listenHost = "127.0.0.1";
	uint16_t listenPort = 7180;
	std::string mysqlHost = "127.0.0.1";
	uint16_t mysqlPort = 3306;
	std::string mysqlUser;
	std::string mysqlPassword;
	std::string mysqlDatabase;
	std::string mysqlSocket;
};

std::string trim(std::string value)
{
	auto notSpace = [](unsigned char c) { return !std::isspace(c); };
	value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
	value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
	return value;
}

ServiceConfig loadConfig(const std::string& path)
{
	std::ifstream input(path);
	if (!input) {
		throw std::runtime_error("cannot open configuration file: " + path);
	}

	std::map<std::string, std::string> values;
	std::string line;
	while (std::getline(input, line)) {
		line = trim(line);
		if (line.empty() || line[0] == '#' || line[0] == ';') {
			continue;
		}
		const size_t separator = line.find('=');
		if (separator == std::string::npos) {
			throw std::runtime_error("invalid configuration line: " + line);
		}
		values[trim(line.substr(0, separator))] = trim(line.substr(separator + 1));
	}

	ServiceConfig config;
	auto setString = [&](const char* key, std::string& target, bool required = false) {
		auto it = values.find(key);
		if (it != values.end()) {
			target = it->second;
		} else if (required) {
			throw std::runtime_error(std::string("missing configuration key: ") + key);
		}
	};
	auto setPort = [&](const char* key, uint16_t& target) {
		auto it = values.find(key);
		if (it != values.end()) {
			const int value = std::stoi(it->second);
			if (value < 1 || value > 65535) {
				throw std::runtime_error(std::string("invalid port in configuration key: ") + key);
			}
			target = static_cast<uint16_t>(value);
		}
	};

	setString("listen_host", config.listenHost);
	setPort("listen_port", config.listenPort);
	setString("mysql_host", config.mysqlHost);
	setPort("mysql_port", config.mysqlPort);
	setString("mysql_user", config.mysqlUser, true);
	setString("mysql_password", config.mysqlPassword);
	setString("mysql_database", config.mysqlDatabase, true);
	setString("mysql_socket", config.mysqlSocket);
	return config;
}

void wakeServiceListener(const ServiceConfig& config)
{
	try {
		boost::asio::io_context ioContext;
		tcp::socket socket(ioContext);
		socket.connect(tcp::endpoint(
			boost::asio::ip::make_address(config.listenHost), config.listenPort));
	} catch (const std::exception&) {
		// The listener may already have stopped. This connection exists only to
		// wake the synchronous accept() after a successful shutdown response.
	}
}

struct QueryResult {
	std::vector<std::string> columns;
	std::vector<std::vector<std::optional<std::string>>> rows;
};

class ServiceDatabase {
public:
	explicit ServiceDatabase(const ServiceConfig& config) : config(config)
	{
		connect();
	}

	~ServiceDatabase()
	{
		if (handle) {
			mysql_close(handle);
		}
	}

	ServiceDatabase(const ServiceDatabase&) = delete;
	ServiceDatabase& operator=(const ServiceDatabase&) = delete;

	void connect()
	{
		if (transactionActive) {
			throw std::runtime_error("cannot reconnect MariaDB while a player save transaction is active");
		}
		if (handle) {
			mysql_close(handle);
			handle = nullptr;
		}

		handle = mysql_init(nullptr);
		if (!handle) {
			throw ServiceDatabaseError(CR_CONNECTION_ERROR, "mysql_init failed");
		}

		// Never let the client library reconnect transparently.  A reconnect in the
		// middle of START TRANSACTION/COMMIT silently moves later statements to a
		// different autocommit session and can turn one player save into a partial
		// save.  We reconnect only before beginning a new operation.
		bool reconnect = false;
		mysql_options(handle, MYSQL_OPT_RECONNECT, &reconnect);
		const unsigned int networkTimeoutSeconds = 5;
		mysql_options(handle, MYSQL_OPT_CONNECT_TIMEOUT, &networkTimeoutSeconds);
		mysql_options(handle, MYSQL_OPT_READ_TIMEOUT, &networkTimeoutSeconds);
		mysql_options(handle, MYSQL_OPT_WRITE_TIMEOUT, &networkTimeoutSeconds);
		my_bool disableSsl = 0;
		mysql_options(handle, MYSQL_OPT_SSL_ENFORCE, &disableSsl);
		mysql_options(handle, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &disableSsl);

		if (!mysql_real_connect(handle, config.mysqlHost.c_str(), config.mysqlUser.c_str(),
				config.mysqlPassword.c_str(), config.mysqlDatabase.c_str(), config.mysqlPort,
				config.mysqlSocket.empty() ? nullptr : config.mysqlSocket.c_str(), 0)) {
			const unsigned int errorCode = mysql_errno(handle);
			const std::string error = mysql_error(handle);
			mysql_close(handle);
			handle = nullptr;
			throw ServiceDatabaseError(errorCode == 0 ? CR_CONNECTION_ERROR : errorCode,
				"MariaDB connection failed: " + error);
		}
	}

	void ensureConnected()
	{
		if (transactionActive) {
			if (!handle) {
				throw ServiceDatabaseError(CR_SERVER_GONE_ERROR,
					"MariaDB handle disappeared during player save transaction");
			}
			return;
		}
		if (!handle || mysql_ping(handle) != 0) {
			connect();
		}
	}

	void beginTransaction(bool repeatableRead = false)
	{
		if (transactionActive) {
			throw std::runtime_error("nested player I/O transaction");
		}
		ensureConnected();
		if (repeatableRead &&
				mysql_real_query(handle,
					"SET TRANSACTION ISOLATION LEVEL REPEATABLE READ",
					sizeof("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ") - 1) != 0) {
			throw ServiceDatabaseError(mysql_errno(handle), mysql_error(handle));
		}
		if (mysql_real_query(handle, "START TRANSACTION", sizeof("START TRANSACTION") - 1) != 0) {
			throw ServiceDatabaseError(mysql_errno(handle), mysql_error(handle));
		}
		transactionActive = true;
	}

	void commitTransaction()
	{
		if (!transactionActive) {
			throw std::runtime_error("player I/O transaction commit without begin");
		}
		const int result = mysql_real_query(handle, "COMMIT", sizeof("COMMIT") - 1);
		transactionActive = false;
		if (result != 0) {
			throw ServiceDatabaseError(mysql_errno(handle), mysql_error(handle));
		}
	}

	void rollbackTransaction() noexcept
	{
		if (!transactionActive) {
			return;
		}
		// Do not reconnect here.  If the connection was lost, MariaDB has already
		// rolled the transaction back; reconnecting would conceal that boundary.
		if (handle) {
			(void)mysql_rollback(handle);
		}
		transactionActive = false;
	}

	void execute(const std::string& sql)
	{
		ensureConnected();
		if (mysql_real_query(handle, sql.data(), static_cast<unsigned long>(sql.size())) != 0) {
			throw ServiceDatabaseError(mysql_errno(handle), mysql_error(handle));
		}
		MYSQL_RES* result = mysql_store_result(handle);
		if (result) {
			mysql_free_result(result);
		}
	}

	QueryResult query(const std::string& sql)
	{
		ensureConnected();
		if (mysql_real_query(handle, sql.data(), static_cast<unsigned long>(sql.size())) != 0) {
			throw ServiceDatabaseError(mysql_errno(handle), mysql_error(handle));
		}

		MYSQL_RES* result = mysql_store_result(handle);
		if (!result) {
			const unsigned int errorCode = mysql_errno(handle);
			const std::string error = mysql_error(handle);
			throw ServiceDatabaseError(errorCode,
				error.empty() ? "player I/O query returned no result set" : error);
		}

		QueryResult output;
		const unsigned int fieldCount = mysql_num_fields(result);
		MYSQL_FIELD* fields = mysql_fetch_fields(result);
		output.columns.reserve(fieldCount);
		for (unsigned int i = 0; i < fieldCount; ++i) {
			output.columns.emplace_back(fields[i].name);
		}

		while (MYSQL_ROW row = mysql_fetch_row(result)) {
			unsigned long* lengths = mysql_fetch_lengths(result);
			std::vector<std::optional<std::string>> values;
			values.reserve(fieldCount);
			for (unsigned int i = 0; i < fieldCount; ++i) {
				if (!row[i]) {
					values.emplace_back(std::nullopt);
				} else {
					values.emplace_back(std::string(row[i], lengths[i]));
				}
			}
			output.rows.emplace_back(std::move(values));
		}
		mysql_free_result(result);
		return output;
	}

	std::string escape(const char* data, size_t size)
	{
		// Escaping is local to MYSQL and performs no I/O.  During a transaction it
		// must use the existing connection instead of probing/reconnecting it.
		if (!transactionActive) {
			ensureConnected();
		}
		if (!handle) {
			throw std::runtime_error("MariaDB handle unavailable while escaping player I/O value");
		}
		std::string output;
		output.resize(size * 2 + 1);
		const unsigned long written = mysql_real_escape_string(handle, output.data(), data,
			static_cast<unsigned long>(size));
		output.resize(written);
		return output;
	}

	std::string escape(const std::string& value)
	{
		return escape(value.data(), value.size());
	}

private:
	ServiceConfig config;
	MYSQL* handle = nullptr;
	bool transactionActive = false;
};

void ensureSchema(ServiceDatabase& db)
{
	db.execute(
		"CREATE TABLE IF NOT EXISTS `player_io_jobs` ("
		"`job_id` varchar(64) NOT NULL,"
		"`player_id` int unsigned NOT NULL,"
		"`revision` bigint unsigned NOT NULL,"
		"`payload` longblob NOT NULL,"
		"`payload_hash` char(64) NOT NULL DEFAULT '',"
		"`status` tinyint unsigned NOT NULL DEFAULT 1,"
		"`attempts` int unsigned NOT NULL DEFAULT 0,"
		"`last_error` text NOT NULL,"
		"`created_at` timestamp(6) NOT NULL DEFAULT current_timestamp(6),"
		"`updated_at` timestamp(6) NOT NULL DEFAULT current_timestamp(6) ON UPDATE current_timestamp(6),"
		"PRIMARY KEY (`job_id`),"
		"UNIQUE KEY `player_revision` (`player_id`,`revision`),"
		"KEY `status_created` (`status`,`created_at`)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

	db.execute(
		"CREATE TABLE IF NOT EXISTS `player_io_state` ("
		"`player_id` int unsigned NOT NULL,"
		"`committed_revision` bigint unsigned NOT NULL DEFAULT 0,"
		"`committed_job_id` varchar(64) NOT NULL DEFAULT '',"
		"`next_revision` bigint unsigned NOT NULL DEFAULT 0,"
		"`updated_at` timestamp(6) NOT NULL DEFAULT current_timestamp(6) ON UPDATE current_timestamp(6),"
		"PRIMARY KEY (`player_id`)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

	// Existing development databases predate these two integrity columns.
	// IF NOT EXISTS keeps the migration idempotent across service restarts.
	db.execute("ALTER TABLE `player_io_jobs` ADD COLUMN IF NOT EXISTS `payload_hash` char(64) NOT NULL DEFAULT '' AFTER `payload`");
	db.execute("ALTER TABLE `player_io_state` ADD COLUMN IF NOT EXISTS `next_revision` bigint unsigned NOT NULL DEFAULT 0 AFTER `committed_job_id`");

	// Legacy data backfills are deliberately not performed during service startup.
	// Scanning the full durable journal here can exceed the database read timeout
	// and make the service unavailable. Existing installations must run those
	// one-time repairs as explicit offline maintenance before using the service.
}

playerio::JobState toProtocolState(uint8_t status)
{
	switch (status) {
		case JOB_PENDING: return playerio::JobState::PENDING;
		case JOB_APPLYING: return playerio::JobState::APPLYING;
		case JOB_COMMITTED: return playerio::JobState::COMMITTED;
		case JOB_FAILED: return playerio::JobState::FAILED;
		default: return playerio::JobState::UNKNOWN;
	}
}

struct JobRecord {
	std::string jobId;
	uint32_t playerId = 0;
	uint64_t revision = 0;
	std::vector<uint8_t> payload;
	uint8_t status = JOB_PENDING;
	uint32_t attempts = 0;
	std::string lastError;
	std::string payloadHash;
	bool payloadHashValid = false;
};

std::optional<JobRecord> loadJob(ServiceDatabase& db, const std::string& jobId, bool forUpdate = false)
{
	QueryResult result = db.query(
		"SELECT `job_id`,`player_id`,`revision`,`payload`,`status`,`attempts`,`last_error`,"
		"`payload_hash`,IF(`payload_hash`=SHA2(`payload`,256),1,0) AS `payload_hash_valid` "
		"FROM `player_io_jobs` WHERE `job_id`='" + db.escape(jobId) + "' LIMIT 1" +
		(forUpdate ? " FOR UPDATE" : ""));
	if (result.rows.empty()) {
		return std::nullopt;
	}

	const auto& row = result.rows.front();
	JobRecord job;
	job.jobId = row[0].value_or("");
	job.playerId = static_cast<uint32_t>(std::stoul(row[1].value_or("0")));
	job.revision = std::stoull(row[2].value_or("0"));
	const std::string payload = row[3].value_or("");
	job.payload.assign(payload.begin(), payload.end());
	job.status = static_cast<uint8_t>(std::stoul(row[4].value_or("0")));
	job.attempts = static_cast<uint32_t>(std::stoul(row[5].value_or("0")));
	job.lastError = row[6].value_or("");
	job.payloadHash = row[7].value_or("");
	job.payloadHashValid = row[8].value_or("0") == "1";
	return job;
}

struct PlayerState {
	uint64_t committedRevision = 0;
	std::string committedJobId;
	uint64_t nextRevision = 0;
};

PlayerState loadPlayerState(ServiceDatabase& db, uint32_t playerId, bool forUpdate)
{
	QueryResult result = db.query(
		"SELECT `committed_revision`,`committed_job_id`,`next_revision` FROM `player_io_state` WHERE `player_id`=" +
		std::to_string(playerId) + (forUpdate ? " FOR UPDATE" : ""));
	if (result.rows.empty()) {
		return {};
	}
	const auto& row = result.rows.front();
	PlayerState state;
	state.committedRevision = std::stoull(row[0].value_or("0"));
	state.committedJobId = row[1].value_or("");
	state.nextRevision = std::stoull(row[2].value_or("0"));
	return state;
}

uint64_t loadCommittedRevision(ServiceDatabase& db, uint32_t playerId, bool forUpdate)
{
	return loadPlayerState(db, playerId, forUpdate).committedRevision;
}

JobRecord prepareJob(ServiceDatabase& db, const std::string& jobId, uint32_t playerId,
	const std::vector<uint8_t>& payload)
{
	if (jobId.empty() || jobId.size() > 64 || playerId == 0) {
		throw std::runtime_error("invalid player I/O save job identity");
	}

	const std::string escapedJobId = db.escape(jobId);
	const std::string escapedPayload = db.escape(reinterpret_cast<const char*>(payload.data()), payload.size());
	try {
		db.beginTransaction();
		db.execute(
			"INSERT IGNORE INTO `player_io_state` (`player_id`,`committed_revision`,`committed_job_id`,`next_revision`) VALUES (" +
			std::to_string(playerId) + ",0,'',0)");
		const PlayerState state = loadPlayerState(db, playerId, true);

		if (auto existing = loadJob(db, jobId, true)) {
			if (existing->playerId != playerId || existing->payload != payload) {
				throw std::runtime_error("player I/O save job id was reused with a different payload");
			}
			if (!existing->payloadHashValid) {
				throw std::runtime_error("player I/O save job payload hash validation failed");
			}
			db.commitTransaction();
			return *existing;
		}
		// A newer immutable snapshot may never be assigned a revision behind an
		// already durable save for the same player.  Normally the manager keeps
		// this impossible; this guard also protects restart/migration recovery.
		QueryResult pending = db.query(
			"SELECT `job_id` FROM `player_io_jobs` WHERE `player_id`=" +
			std::to_string(playerId) + " AND `status`<>" + std::to_string(JOB_COMMITTED) +
			" ORDER BY `revision` DESC LIMIT 1 FOR UPDATE");
		if (!pending.rows.empty()) {
			throw std::runtime_error("another durable player save job is still pending");
		}

		const uint64_t revision = std::max(state.nextRevision, state.committedRevision) + 1;
		db.execute(
			"UPDATE `player_io_state` SET `next_revision`=" + std::to_string(revision) +
			" WHERE `player_id`=" + std::to_string(playerId));
		db.execute(
			"INSERT INTO `player_io_jobs` "
			"(`job_id`,`player_id`,`revision`,`payload`,`payload_hash`,`status`,`attempts`,`last_error`) VALUES ('" +
			escapedJobId + "'," + std::to_string(playerId) + "," + std::to_string(revision) + ",'" +
			escapedPayload + "',SHA2('" + escapedPayload + "',256)," +
			std::to_string(JOB_PENDING) + ",0,'')");
		db.commitTransaction();

		JobRecord job;
		job.jobId = jobId;
		job.playerId = playerId;
		job.revision = revision;
		job.payload = payload;
		job.status = JOB_PENDING;
		job.payloadHashValid = true;
		return job;
	} catch (...) {
		db.rollbackTransaction();
		throw;
	}
}

bool applyJob(ServiceDatabase& db, JobRecord& job, std::string& error)
{
	if (job.status == JOB_COMMITTED) {
		return true;
	}
	if (job.status != JOB_PENDING && job.status != JOB_APPLYING) {
		error = "player I/O save job is not in an applicable state";
		return false;
	}
	if (!job.payloadHashValid) {
		error = "durable player save payload hash validation failed";
		try {
			db.execute(
				"UPDATE `player_io_jobs` SET `status`=" + std::to_string(JOB_FAILED) +
				",`attempts`=`attempts`+1,`last_error`='" + db.escape(error) +
				"' WHERE `job_id`='" + db.escape(job.jobId) + "'");
			job.status = JOB_FAILED;
		} catch (const std::exception&) {
		}
		return false;
	}

	std::vector<std::string> statements;
	try {
		statements = playerio::deserializeStatements(job.payload);
	} catch (const std::exception& exception) {
		error = std::string("invalid durable player save payload: ") + exception.what();
		try {
			db.execute(
				"UPDATE `player_io_jobs` SET `status`=" + std::to_string(JOB_FAILED) +
				",`attempts`=`attempts`+1,`last_error`='" + db.escape(error) +
				"' WHERE `job_id`='" + db.escape(job.jobId) + "'");
			job.status = JOB_FAILED;
		} catch (const std::exception&) {
			// The original durable row remains PENDING and will be reported on the
			// next readiness check instead of being silently discarded.
		}
		return false;
	}

	try {
		db.beginTransaction();
		db.execute(
			"INSERT IGNORE INTO `player_io_state` (`player_id`,`committed_revision`,`committed_job_id`) VALUES (" +
			std::to_string(job.playerId) + ",0,'')");
		const PlayerState state = loadPlayerState(db, job.playerId, true);

		db.execute(
			"UPDATE `player_io_jobs` SET `status`=" + std::to_string(JOB_APPLYING) +
			",`attempts`=`attempts`+1,`last_error`='' WHERE `job_id`='" + db.escape(job.jobId) + "'");

		if (state.committedRevision < job.revision) {
			for (const std::string& statement : statements) {
				db.execute(statement);
			}
			db.execute(
				"UPDATE `player_io_state` SET `committed_revision`=" + std::to_string(job.revision) +
				",`committed_job_id`='" + db.escape(job.jobId) + "' WHERE `player_id`=" +
				std::to_string(job.playerId));
		} else if (state.committedRevision == job.revision && state.committedJobId != job.jobId) {
			throw std::runtime_error("player I/O revision belongs to another save job");
		} else if (state.committedRevision > job.revision) {
			throw std::runtime_error("player I/O save job revision is older than committed state");
		}

		db.execute(
			"UPDATE `player_io_jobs` SET `status`=" + std::to_string(JOB_COMMITTED) +
			",`last_error`='' WHERE `job_id`='" + db.escape(job.jobId) + "'");
		db.commitTransaction();
		job.status = JOB_COMMITTED;
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		db.rollbackTransaction();
		const auto* databaseError = dynamic_cast<const ServiceDatabaseError*>(&exception);
		const bool transient = databaseError && databaseError->isTransient();
		const uint8_t retryStatus = transient ? JOB_PENDING : JOB_FAILED;
		try {
			db.execute(
				"UPDATE `player_io_jobs` SET `status`=" + std::to_string(retryStatus) +
				",`attempts`=`attempts`+1,`last_error`='" + db.escape(error) +
				"' WHERE `job_id`='" + db.escape(job.jobId) + "'");
			job.status = retryStatus;
		} catch (const std::exception&) {
			// The durable pending row remains available for startup recovery.
		}
		return false;
	}
}

void recoverPendingJobs(ServiceDatabase& db, bool includeFreshJobs)
{
	std::string query =
		"SELECT `job_id` FROM `player_io_jobs` WHERE `status` IN (1,2)";
	if (!includeFreshJobs) {
		// A freshly prepared job is owned by the live TFS logout worker. Giving it
		// a short exclusive window prevents the background recovery thread from
		// applying the same payload concurrently and contending on the player's
		// rows. Startup recovery still includes every pending job immediately.
		query += " AND `updated_at` < NOW(6) - INTERVAL 30 SECOND";
	}
	query += " ORDER BY `created_at`,`job_id`";
	QueryResult result = db.query(query);
	for (const auto& row : result.rows) {
		const std::string jobId = row.front().value_or("");
		auto job = loadJob(db, jobId);
		if (!job) {
			continue;
		}
		std::string error;
		if (applyJob(db, *job, error)) {
			std::cout << "Recovered pending player save job " << jobId << "." << std::endl;
		} else {
			std::cerr << "Failed to recover player save job " << jobId << ": " << error << std::endl;
		}
	}
}

void recoverPendingJobs(const ServiceConfig& config, bool initializeSchema)
{
	ServiceDatabase db(config);
	if (initializeSchema) {
		ensureSchema(db);
	}
	recoverPendingJobs(db, true);
}

uint64_t pruneCommittedJournal(ServiceDatabase& db)
{
	// Keep the committed job referenced by player_io_state and every job that is
	// not COMMITTED. Old committed payloads are maintenance history only. Select
	// small batches by indexed identifiers so deleting large payloads cannot turn
	// the controlled shutdown into one enormous transaction.
	constexpr uint32_t PRUNE_BATCH_SIZE = 100;
	uint64_t removed = 0;
	for (;;) {
		QueryResult candidates = db.query(
			"SELECT `jobs`.`job_id` FROM `player_io_jobs` AS `jobs` "
			"INNER JOIN `player_io_state` AS `state` "
			"ON `state`.`player_id`=`jobs`.`player_id` "
			"WHERE `jobs`.`status`=" + std::to_string(JOB_COMMITTED) +
			" AND `jobs`.`revision`<`state`.`committed_revision` "
			"ORDER BY `jobs`.`created_at`,`jobs`.`job_id` LIMIT " +
			std::to_string(PRUNE_BATCH_SIZE));
		if (candidates.rows.empty()) {
			break;
		}

		std::string query = "DELETE FROM `player_io_jobs` WHERE `status`=" +
			std::to_string(JOB_COMMITTED) + " AND `job_id` IN (";
		bool first = true;
		uint64_t batchCount = 0;
		for (const auto& row : candidates.rows) {
			if (row.empty() || !row.front().has_value()) {
				continue;
			}
			if (!first) {
				query += ',';
			}
			first = false;
			query += '\'' + db.escape(row.front().value()) + '\'';
			++batchCount;
		}
		if (batchCount == 0) {
			throw std::runtime_error(
				"player I/O journal cleanup selected no valid job identifiers");
		}
		query += ')';
		db.execute(query);
		removed += batchCount;
	}
	return removed;
}

uint32_t countPendingJobs(ServiceDatabase& db)
{
	QueryResult result = db.query(
		"SELECT COUNT(*) AS `pending_jobs` FROM `player_io_jobs` WHERE `status` IN (1,2)");
	if (result.rows.empty() || result.rows.front().empty()) {
		throw std::runtime_error("could not count pending player I/O jobs");
	}
	return static_cast<uint32_t>(std::stoul(result.rows.front().front().value_or("0")));
}

bool inspectJob(const ServiceConfig& config, const std::string& jobId)
{
	ServiceDatabase db(config);
	ensureSchema(db);
	const std::optional<JobRecord> job = loadJob(db, jobId);
	if (!job) {
		std::cerr << "Player I/O job not found: " << jobId << std::endl;
		return false;
	}
	std::cout << "job_id=" << job->jobId
	          << " player_id=" << job->playerId
	          << " revision=" << job->revision
	          << " status=" << static_cast<uint32_t>(job->status)
	          << " attempts=" << job->attempts
	          << " payload_hash=" << job->payloadHash
	          << " payload_hash_valid=" << (job->payloadHashValid ? "yes" : "no")
	          << " last_error=" << job->lastError << std::endl;
	return true;
}

bool retryFailedJob(const ServiceConfig& config, const std::string& jobId,
	uint64_t expectedRevision)
{
	ServiceDatabase db(config);
	ensureSchema(db);
	const std::optional<JobRecord> discovery = loadJob(db, jobId);
	if (!discovery) {
		throw std::runtime_error("player I/O job not found");
	}

	try {
		db.beginTransaction();
		db.execute(
			"INSERT IGNORE INTO `player_io_state` "
			"(`player_id`,`committed_revision`,`committed_job_id`,`next_revision`) VALUES (" +
			std::to_string(discovery->playerId) + ",0,'',0)");
		const PlayerState state = loadPlayerState(db, discovery->playerId, true);
		const std::optional<JobRecord> job = loadJob(db, jobId, true);
		if (!job || job->playerId != discovery->playerId ||
				job->revision != expectedRevision) {
			throw std::runtime_error(
				"player I/O failed job identity or revision changed");
		}
		if (job->status == JOB_COMMITTED) {
			db.commitTransaction();
			return true;
		}
		if (job->status != JOB_FAILED) {
			throw std::runtime_error("player I/O job is not FAILED");
		}
		if (!job->payloadHashValid) {
			throw std::runtime_error(
				"player I/O failed job payload hash is invalid; retry refused");
		}
		(void)playerio::deserializeStatements(job->payload);

		if (state.committedRevision == job->revision &&
				state.committedJobId == job->jobId) {
			db.execute(
				"UPDATE `player_io_jobs` SET `status`=" +
				std::to_string(JOB_COMMITTED) +
				",`last_error`='' WHERE `job_id`='" + db.escape(job->jobId) + "'");
			db.commitTransaction();
			return true;
		}
		if (state.committedRevision >= job->revision) {
			throw std::runtime_error(
				"player I/O failed job revision conflicts with committed state");
		}

		QueryResult conflict = db.query(
			"SELECT `job_id` FROM `player_io_jobs` WHERE `player_id`=" +
			std::to_string(job->playerId) + " AND `job_id`<>'" +
			db.escape(job->jobId) + "' AND `status`<>" +
			std::to_string(JOB_COMMITTED) + " LIMIT 1 FOR UPDATE");
		if (!conflict.rows.empty()) {
			throw std::runtime_error(
				"another non-committed player I/O job blocks this retry");
		}

		db.execute(
			"UPDATE `player_io_jobs` SET `status`=" + std::to_string(JOB_PENDING) +
			",`last_error`='administrative retry requested' WHERE `job_id`='" +
			db.escape(job->jobId) + "'");
		db.commitTransaction();
		return true;
	} catch (...) {
		db.rollbackTransaction();
		throw;
	}
}

playerio::ResultSet toProtocolResult(QueryResult&& result)
{
	playerio::ResultSet output;
	output.columns = std::move(result.columns);
	output.rows = std::move(result.rows);
	return output;
}

namespace {
struct ConnectionGuard {
	explicit ConnectionGuard(std::atomic<int>& counter) : counter(counter) {
		counter.fetch_add(1);
	}
	~ConnectionGuard() {
		counter.fetch_sub(1);
	}
	std::atomic<int>& counter;
};
}

void handleClient(tcp::socket socket, ServiceConfig config)
{
	ConnectionGuard connectionGuard(activeClientConnections);
	try {
		configureClientSocketTimeouts(socket);
		std::vector<uint8_t> frame = playerio::readFrame(socket);
		std::unique_ptr<ServiceDatabase> database;

		for (;;) {
			playerio::Reader reader(frame);
			playerio::Opcode opcode;
			uint64_t requestId;
			playerio::readEnvelope(reader, opcode, requestId);
			// PING is deliberately process-only. The TFS health monitor must be
			// able to distinguish a stopped Player I/O process without turning the
			// heartbeat into a continuous MariaDB health check.
			if (opcode != playerio::Opcode::PING && !database) {
				database = std::make_unique<ServiceDatabase>(config);
			}

			playerio::Writer response;
			bool stopAfterResponse = false;
			try {
				switch (opcode) {
					case playerio::Opcode::PING: {
						playerio::addResponseEnvelope(response, opcode, requestId, true, "");
						response.addString("player-io-service");
						break;
					}

					case playerio::Opcode::QUERY: {
						const std::string sql = reader.getString();
						QueryResult result;
						for (uint32_t attempt = 0;; ++attempt) {
							try {
								result = database->query(sql);
								break;
							} catch (const std::exception& exception) {
								if (attempt + 1 >= LOCK_RETRY_ATTEMPTS ||
										!isRetryableLockError(exception)) {
									throw;
								}
								sleepBeforeLockRetry(attempt);
							}
						}
						playerio::addResponseEnvelope(response, opcode, requestId, true, "");
						playerio::writeResultSet(response, toProtocolResult(std::move(result)));
						break;
					}

					case playerio::Opcode::QUERY_BATCH: {
						const uint32_t playerId = reader.getU32();
						const uint32_t queryCount = reader.getU32();
						if (playerId == 0 || queryCount == 0 || queryCount > 64) {
							throw std::runtime_error("invalid player I/O login snapshot request");
						}
						std::vector<std::string> queries;
						queries.reserve(queryCount);
						for (uint32_t i = 0; i < queryCount; ++i) {
							queries.emplace_back(reader.getString());
						}

						std::vector<QueryResult> results;
						uint64_t committedRevision = 0;
						for (uint32_t attempt = 0;; ++attempt) {
							try {
								database->beginTransaction(true);
								database->execute(
									"INSERT IGNORE INTO `player_io_state` (`player_id`,`committed_revision`,`committed_job_id`,`next_revision`) VALUES (" +
									std::to_string(playerId) + ",0,'',0)");
								const PlayerState state = loadPlayerState(*database, playerId, true);
								QueryResult pending = database->query(
									"SELECT `status`,`last_error` FROM `player_io_jobs` WHERE `player_id`=" +
									std::to_string(playerId) + " AND `status`<>" + std::to_string(JOB_COMMITTED) +
									" ORDER BY `revision` DESC LIMIT 1 FOR UPDATE");
								if (!pending.rows.empty()) {
									throw std::runtime_error("player save is still pending: " + pending.rows.front()[1].value_or(""));
								}
								results.clear();
								results.reserve(queries.size());
								for (const std::string& query : queries) {
									results.emplace_back(database->query(query));
								}
								database->commitTransaction();
								committedRevision = state.committedRevision;
								break;
							} catch (const std::exception& exception) {
								database->rollbackTransaction();
								if (attempt + 1 >= LOCK_RETRY_ATTEMPTS ||
										!isRetryableLockError(exception)) {
									throw;
								}
								sleepBeforeLockRetry(attempt);
							} catch (...) {
								database->rollbackTransaction();
								throw;
							}
						}

						playerio::addResponseEnvelope(response, opcode, requestId, true, "");
						response.addU64(committedRevision);
						response.addU32(static_cast<uint32_t>(results.size()));
						for (QueryResult& result : results) {
							playerio::writeResultSet(response, toProtocolResult(std::move(result)));
						}
						break;
					}

					case playerio::Opcode::CHECK_PLAYER_READY: {
						const uint32_t playerId = reader.getU32();
						QueryResult result = database->query(
							"SELECT `status`,`last_error` FROM `player_io_jobs` WHERE `player_id`=" +
							std::to_string(playerId) + " AND `status`<>" + std::to_string(JOB_COMMITTED) +
							" ORDER BY `revision` DESC LIMIT 1");
						const bool ready = result.rows.empty();
						playerio::addResponseEnvelope(response, opcode, requestId, true, "");
						response.addBool(ready);
						if (ready) {
							response.addU8(static_cast<uint8_t>(playerio::JobState::COMMITTED));
							response.addString("");
						} else {
							response.addU8(static_cast<uint8_t>(toProtocolState(
								static_cast<uint8_t>(std::stoul(result.rows.front()[0].value_or("0"))))));
							response.addString(result.rows.front()[1].value_or(""));
						}
						response.addU64(loadCommittedRevision(*database, playerId, false));
						break;
					}

					case playerio::Opcode::PREPARE_SAVE_JOB: {
						const std::string jobId = reader.getString();
						const uint32_t playerId = reader.getU32();
						const uint32_t statementCount = reader.getU32();
						if (statementCount > 100000) {
							throw std::runtime_error("player I/O save job contains too many statements");
						}
						std::vector<std::string> statements;
						statements.reserve(statementCount);
						for (uint32_t i = 0; i < statementCount; ++i) {
							statements.emplace_back(reader.getString());
						}

						JobRecord job;
						for (uint32_t attempt = 0;; ++attempt) {
							try {
								job = prepareJob(*database, jobId, playerId, playerio::serializeStatements(statements));
								break;
							} catch (const std::exception& exception) {
								if (attempt + 1 >= LOCK_RETRY_ATTEMPTS ||
										!isRetryableLockError(exception)) {
									throw;
								}
								sleepBeforeLockRetry(attempt);
							}
						}
						playerio::addResponseEnvelope(response, opcode, requestId, true, job.lastError);
						response.addU8(static_cast<uint8_t>(toProtocolState(job.status)));
						response.addU64(job.revision);
						break;
					}

					case playerio::Opcode::APPLY_SAVE_JOB: {
						const std::string jobId = reader.getString();
						auto job = loadJob(*database, jobId);
						if (!job) {
							throw std::runtime_error("unknown player I/O save job");
						}
						std::string applyError;
						const bool committed = job->status == JOB_COMMITTED || applyJob(*database, *job, applyError);
						job = loadJob(*database, jobId);
						const playerio::JobState state = job ? toProtocolState(job->status) : playerio::JobState::UNKNOWN;
						playerio::addResponseEnvelope(response, opcode, requestId,
							committed && state == playerio::JobState::COMMITTED,
							job ? job->lastError : applyError);
						response.addU8(static_cast<uint8_t>(state));
						response.addU64(job ? job->revision : 0);
						break;
					}

					case playerio::Opcode::SUBMIT_SAVE_JOB: {
						// Protocol v1 compatibility: prepare durably, then attempt one apply.
						const std::string jobId = reader.getString();
						const uint32_t playerId = reader.getU32();
						(void)reader.getU64();
						const uint32_t statementCount = reader.getU32();
						if (statementCount > 100000) {
							throw std::runtime_error("player I/O save job contains too many statements");
						}
						std::vector<std::string> statements;
						statements.reserve(statementCount);
						for (uint32_t i = 0; i < statementCount; ++i) {
							statements.emplace_back(reader.getString());
						}
						auto prepared = prepareJob(*database, jobId, playerId, playerio::serializeStatements(statements));
						std::string applyError;
						const bool committed = prepared.status == JOB_COMMITTED || applyJob(*database, prepared, applyError);
						auto job = loadJob(*database, jobId);
						const playerio::JobState state = job ? toProtocolState(job->status) : playerio::JobState::UNKNOWN;
						playerio::addResponseEnvelope(response, opcode, requestId,
							committed && state == playerio::JobState::COMMITTED,
							job ? job->lastError : applyError);
						response.addU8(static_cast<uint8_t>(state));
						response.addU64(job ? job->revision : 0);
						break;
					}

					case playerio::Opcode::JOB_STATUS: {
						const std::string jobId = reader.getString();
						auto job = loadJob(*database, jobId);
						playerio::addResponseEnvelope(response, opcode, requestId, true, "");
						response.addU8(static_cast<uint8_t>(job ? toProtocolState(job->status) :
							playerio::JobState::UNKNOWN));
						response.addString(job ? job->lastError : "");
						response.addU64(job ? loadCommittedRevision(*database, job->playerId, false) : 0);
						break;
					}

					case playerio::Opcode::SHUTDOWN_IF_IDLE: {
						QueryResult result = database->query(
							"SELECT COUNT(*) AS `pending_jobs` FROM `player_io_jobs` "
							"WHERE `status` IN (1,2)");
						if (result.rows.empty() || result.rows.front().empty()) {
							throw std::runtime_error(
								"could not verify the durable player I/O queue");
						}
						const uint32_t pendingJobs = static_cast<uint32_t>(
							std::stoul(result.rows.front().front().value_or("0")));
						const bool accepted = pendingJobs == 0;
						if (accepted) {
							const uint64_t removedJobs = pruneCommittedJournal(*database);
							std::cout << "Player I/O journal cleanup completed: removed="
							          << removedJobs
							          << " old committed job(s); latest committed job per player retained."
							          << std::endl;
						}
						playerio::addResponseEnvelope(response, opcode, requestId, true, "");
						response.addBool(accepted);
						response.addU32(pendingJobs);
						stopAfterResponse = accepted;
						break;
					}

					case playerio::Opcode::SHUTDOWN_WHEN_IDLE: {
						// A shutting-down TFS that left durable work behind arms this so
						// the service stops itself once the queue drains, instead of
						// running forever with nobody left to request a shutdown.
						serviceStopWhenIdle.store(true);
						std::cout << "Player I/O service armed stop-when-idle: it will exit "
						             "after the durable queue drains."
						          << std::endl;
						playerio::addResponseEnvelope(response, opcode, requestId, true, "");
						break;
					}

					default:
						throw std::runtime_error("unsupported player I/O opcode");
				}
				if (!reader.empty()) {
					throw std::runtime_error("unexpected bytes after player I/O request");
				}
			} catch (const std::exception& exception) {
				response = playerio::Writer{};
				playerio::addResponseEnvelope(response, opcode, requestId, false, exception.what());
			}

			try {
				playerio::writeFrame(socket, response.data());
			} catch (const std::exception&) {
				// The requester may have disconnected before reading the reply.
				// A failed response write must not cancel an already-accepted
				// safe shutdown, otherwise the service would stay alive forever.
			}
			if (stopAfterResponse) {
				serviceStopRequested = true;
				std::cout << "Player I/O service safe shutdown accepted: durable queue is empty."
				          << std::endl;
				wakeServiceListener(config);
				return;
			}
			frame = playerio::readFrame(socket);
		}
	} catch (const boost::system::system_error& exception) {
		if (exception.code() != boost::asio::error::eof &&
				exception.code() != boost::asio::error::connection_reset) {
			std::cerr << "Player I/O client error: " << exception.what() << std::endl;
		}
	} catch (const std::exception& exception) {
		std::cerr << "Player I/O client error: " << exception.what() << std::endl;
	}
}

} // namespace

int main(int argc, char** argv)
{
	try {
		const std::string configPath = argc >= 2 ? argv[1] : "player-io-service.conf";
		ServiceConfig config = loadConfig(configPath);
		if (argc >= 3) {
			const std::string action = argv[2];
			if (action == "inspect-job" && argc == 4) {
				return inspectJob(config, argv[3]) ? 0 : 1;
			}
			if (action == "retry-failed" && argc == 5) {
				const uint64_t revision = std::stoull(argv[4]);
				if (!retryFailedJob(config, argv[3], revision)) {
					return 1;
				}
				std::cout << "Player I/O FAILED job returned to the durable recovery queue: "
				          << argv[3] << " revision=" << revision << "." << std::endl;
				return 0;
			}
			throw std::runtime_error(
				"usage: player_io_service <config> [inspect-job <job_id> | "
				"retry-failed <job_id> <revision>]");
		}
		recoverPendingJobs(config, true);

		boost::asio::io_context ioContext;
		const auto address = boost::asio::ip::make_address(config.listenHost);
		if (!address.is_loopback()) {
			throw std::runtime_error(
				"player I/O service must listen on a loopback address");
		}
		tcp::acceptor acceptor(ioContext, tcp::endpoint(address, config.listenPort));
		std::cout << "Player I/O service listening on " << config.listenHost << ':' <<
			config.listenPort << " using database " << config.mysqlDatabase << "." << std::endl;

		// A durable PENDING job must not depend on a service restart to be retried.
		// This worker never touches TFS objects; the per-player SQL state lock in
		// applyJob serializes it with normal APPLY requests.
		std::thread recoveryThread([config] {
			std::unique_ptr<ServiceDatabase> recoveryDatabase;
			while (!serviceStopRequested) {
				try {
					if (!recoveryDatabase) {
						recoveryDatabase = std::make_unique<ServiceDatabase>(config);
					}
					recoverPendingJobs(*recoveryDatabase, false);
					// A shutting-down TFS armed stop-when-idle: once the durable queue
					// is empty and no client is connected, stop ourselves so the service
					// does not run forever after finishing recovered saves.
					if (serviceStopWhenIdle.load() &&
					    activeClientConnections.load() == 0 &&
					    countPendingJobs(*recoveryDatabase) == 0) {
						serviceStopRequested = true;
						std::cout << "Player I/O service durable queue drained after armed "
						             "stop-when-idle; shutting down."
						          << std::endl;
						wakeServiceListener(config);
						break;
					}
					// Journal retention is deliberately excluded from the live service
					// loop. Even a bounded DELETE can scan and lock a large payload table;
					// maintenance must run only in an explicit offline window.
				} catch (const std::exception& exception) {
					std::cerr << "Player I/O pending-save recovery error: " << exception.what() << std::endl;
					recoveryDatabase.reset();
				}
				for (uint8_t wait = 0; wait < 10 && !serviceStopRequested; ++wait) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
			}
		});

		try {
			while (!serviceStopRequested) {
				tcp::socket socket(ioContext);
				acceptor.accept(socket);
				if (serviceStopRequested) {
					boost::system::error_code ignored;
					socket.close(ignored);
					break;
				}
				std::thread(handleClient, std::move(socket), config).detach();
			}
		} catch (...) {
			serviceStopRequested = true;
			if (recoveryThread.joinable()) {
				recoveryThread.join();
			}
			throw;
		}
		if (recoveryThread.joinable()) {
			recoveryThread.join();
		}
		std::cout << "Player I/O service stopped cleanly." << std::endl;
	} catch (const std::exception& exception) {
		std::cerr << "Player I/O service fatal error: " << exception.what() << std::endl;
		return 1;
	}
}
