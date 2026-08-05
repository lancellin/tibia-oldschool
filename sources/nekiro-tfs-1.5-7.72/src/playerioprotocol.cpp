#include "playerioprotocol.h"

#include <algorithm>
#include <array>
#include <limits>

namespace playerio {

void Writer::addU8(uint8_t value)
{
	buffer.push_back(value);
}

void Writer::addU16(uint16_t value)
{
	buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
	buffer.push_back(static_cast<uint8_t>(value & 0xFF));
}

void Writer::addU32(uint32_t value)
{
	for (int shift = 24; shift >= 0; shift -= 8) {
		buffer.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
	}
}

void Writer::addU64(uint64_t value)
{
	for (int shift = 56; shift >= 0; shift -= 8) {
		buffer.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
	}
}

void Writer::addBool(bool value)
{
	addU8(value ? 1 : 0);
}

void Writer::addString(const std::string& value)
{
	if (value.size() > std::numeric_limits<uint32_t>::max()) {
		throw std::length_error("player I/O string exceeds protocol limit");
	}
	addU32(static_cast<uint32_t>(value.size()));
	addBytes(value.data(), value.size());
}

void Writer::addBytes(const char* data, size_t size)
{
	buffer.insert(buffer.end(), data, data + size);
}

void Reader::require(size_t size) const
{
	if (size > buffer.size() - position) {
		throw std::runtime_error("truncated player I/O frame");
	}
}

uint8_t Reader::getU8()
{
	require(1);
	return buffer[position++];
}

uint16_t Reader::getU16()
{
	require(2);
	const uint16_t value = static_cast<uint16_t>(buffer[position]) << 8 |
		static_cast<uint16_t>(buffer[position + 1]);
	position += 2;
	return value;
}

uint32_t Reader::getU32()
{
	require(4);
	uint32_t value = 0;
	for (size_t i = 0; i < 4; ++i) {
		value = (value << 8) | buffer[position + i];
	}
	position += 4;
	return value;
}

uint64_t Reader::getU64()
{
	require(8);
	uint64_t value = 0;
	for (size_t i = 0; i < 8; ++i) {
		value = (value << 8) | buffer[position + i];
	}
	position += 8;
	return value;
}

bool Reader::getBool()
{
	return getU8() != 0;
}

std::string Reader::getString()
{
	const uint32_t size = getU32();
	require(size);
	std::string value(reinterpret_cast<const char*>(buffer.data() + position), size);
	position += size;
	return value;
}

void writeFrame(boost::asio::ip::tcp::socket& socket, const std::vector<uint8_t>& payload)
{
	if (payload.size() > PLAYER_IO_MAX_FRAME_SIZE) {
		throw std::length_error("player I/O frame exceeds maximum size");
	}

	std::array<uint8_t, 4> header{{
		static_cast<uint8_t>((payload.size() >> 24) & 0xFF),
		static_cast<uint8_t>((payload.size() >> 16) & 0xFF),
		static_cast<uint8_t>((payload.size() >> 8) & 0xFF),
		static_cast<uint8_t>(payload.size() & 0xFF),
	}};
	boost::asio::write(socket, boost::asio::buffer(header));
	if (!payload.empty()) {
		boost::asio::write(socket, boost::asio::buffer(payload));
	}
}

std::vector<uint8_t> readFrame(boost::asio::ip::tcp::socket& socket)
{
	std::array<uint8_t, 4> header{};
	boost::asio::read(socket, boost::asio::buffer(header));
	const uint32_t size = static_cast<uint32_t>(header[0]) << 24 |
		static_cast<uint32_t>(header[1]) << 16 |
		static_cast<uint32_t>(header[2]) << 8 |
		static_cast<uint32_t>(header[3]);
	if (size > PLAYER_IO_MAX_FRAME_SIZE) {
		throw std::length_error("player I/O frame exceeds maximum size");
	}

	std::vector<uint8_t> payload(size);
	if (size != 0) {
		boost::asio::read(socket, boost::asio::buffer(payload));
	}
	return payload;
}

void addEnvelope(Writer& writer, Opcode opcode, uint64_t requestId)
{
	writer.addU32(PLAYER_IO_PROTOCOL_MAGIC);
	writer.addU16(PLAYER_IO_PROTOCOL_VERSION);
	writer.addU8(static_cast<uint8_t>(opcode));
	writer.addU64(requestId);
}

void readEnvelope(Reader& reader, Opcode& opcode, uint64_t& requestId)
{
	if (reader.getU32() != PLAYER_IO_PROTOCOL_MAGIC) {
		throw std::runtime_error("invalid player I/O protocol magic");
	}
	if (reader.getU16() != PLAYER_IO_PROTOCOL_VERSION) {
		throw std::runtime_error("unsupported player I/O protocol version");
	}
	opcode = static_cast<Opcode>(reader.getU8());
	requestId = reader.getU64();
}

void addResponseEnvelope(Writer& writer, Opcode opcode, uint64_t requestId, bool success, const std::string& error)
{
	addEnvelope(writer, opcode, requestId);
	writer.addBool(success);
	writer.addString(error);
}

void readResponseEnvelope(Reader& reader, Opcode& opcode, uint64_t& requestId, bool& success, std::string& error)
{
	readEnvelope(reader, opcode, requestId);
	success = reader.getBool();
	error = reader.getString();
}

std::vector<uint8_t> serializeStatements(const std::vector<std::string>& statements)
{
	Writer writer;
	writer.addU32(static_cast<uint32_t>(statements.size()));
	for (const std::string& statement : statements) {
		writer.addString(statement);
	}
	return writer.data();
}

std::vector<std::string> deserializeStatements(const std::vector<uint8_t>& payload)
{
	Reader reader(payload);
	const uint32_t count = reader.getU32();
	std::vector<std::string> statements;
	statements.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		statements.push_back(reader.getString());
	}
	if (!reader.empty()) {
		throw std::runtime_error("unexpected bytes after player I/O statement batch");
	}
	return statements;
}

void writeResultSet(Writer& writer, const ResultSet& result)
{
	writer.addU32(static_cast<uint32_t>(result.columns.size()));
	for (const std::string& column : result.columns) {
		writer.addString(column);
	}
	writer.addU32(static_cast<uint32_t>(result.rows.size()));
	for (const auto& row : result.rows) {
		if (row.size() != result.columns.size()) {
			throw std::runtime_error("player I/O result row has an invalid column count");
		}
		for (const auto& cell : row) {
			writer.addBool(cell.has_value());
			if (cell) {
				writer.addString(*cell);
			}
		}
	}
}

ResultSet readResultSet(Reader& reader)
{
	ResultSet result;
	const uint32_t columnCount = reader.getU32();
	result.columns.reserve(columnCount);
	for (uint32_t i = 0; i < columnCount; ++i) {
		result.columns.emplace_back(reader.getString());
	}

	const uint32_t rowCount = reader.getU32();
	result.rows.reserve(rowCount);
	for (uint32_t rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
		std::vector<std::optional<std::string>> row;
		row.reserve(columnCount);
		for (uint32_t columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
			if (reader.getBool()) {
				row.emplace_back(reader.getString());
			} else {
				row.emplace_back(std::nullopt);
			}
		}
		result.rows.emplace_back(std::move(row));
	}
	return result;
}

} // namespace playerio
