#ifndef FS_PLAYER_IO_PROTOCOL_H
#define FS_PLAYER_IO_PROTOCOL_H

#include <boost/asio.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace playerio {

constexpr uint32_t PLAYER_IO_PROTOCOL_MAGIC = 0x50494F31; // PIO1
constexpr uint16_t PLAYER_IO_PROTOCOL_VERSION = 2;
constexpr uint32_t PLAYER_IO_MAX_FRAME_SIZE = 64 * 1024 * 1024;

enum class Opcode : uint8_t {
	PING = 1,
	QUERY = 2,
	CHECK_PLAYER_READY = 3,
	SUBMIT_SAVE_JOB = 4,
	JOB_STATUS = 5,
	QUERY_BATCH = 6,
	PREPARE_SAVE_JOB = 7,
	APPLY_SAVE_JOB = 8,
	SHUTDOWN_IF_IDLE = 9,
	SHUTDOWN_WHEN_IDLE = 10,
};

enum class JobState : uint8_t {
	UNKNOWN = 0,
	PENDING = 1,
	APPLYING = 2,
	COMMITTED = 3,
	FAILED = 4,
};

struct ResultSet {
	std::vector<std::string> columns;
	std::vector<std::vector<std::optional<std::string>>> rows;
};

struct Response {
	bool success = false;
	std::string error;
	ResultSet result;
	JobState jobState = JobState::UNKNOWN;
	uint64_t committedRevision = 0;
};

class Writer {
public:
	void addU8(uint8_t value);
	void addU16(uint16_t value);
	void addU32(uint32_t value);
	void addU64(uint64_t value);
	void addBool(bool value);
	void addString(const std::string& value);
	void addBytes(const char* data, size_t size);

	const std::vector<uint8_t>& data() const {
		return buffer;
	}

private:
	std::vector<uint8_t> buffer;
};

class Reader {
public:
	explicit Reader(const std::vector<uint8_t>& input) : buffer(input) {}

	uint8_t getU8();
	uint16_t getU16();
	uint32_t getU32();
	uint64_t getU64();
	bool getBool();
	std::string getString();

	bool empty() const {
		return position == buffer.size();
	}

private:
	void require(size_t size) const;

	const std::vector<uint8_t>& buffer;
	size_t position = 0;
};

void writeFrame(boost::asio::ip::tcp::socket& socket, const std::vector<uint8_t>& payload);
std::vector<uint8_t> readFrame(boost::asio::ip::tcp::socket& socket);

void addEnvelope(Writer& writer, Opcode opcode, uint64_t requestId);
void readEnvelope(Reader& reader, Opcode& opcode, uint64_t& requestId);

void addResponseEnvelope(Writer& writer, Opcode opcode, uint64_t requestId, bool success, const std::string& error);
void readResponseEnvelope(Reader& reader, Opcode& opcode, uint64_t& requestId, bool& success, std::string& error);

std::vector<uint8_t> serializeStatements(const std::vector<std::string>& statements);
std::vector<std::string> deserializeStatements(const std::vector<uint8_t>& payload);

void writeResultSet(Writer& writer, const ResultSet& result);
ResultSet readResultSet(Reader& reader);

} // namespace playerio

#endif
