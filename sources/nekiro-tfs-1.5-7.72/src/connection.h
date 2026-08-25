/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2019  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef FS_CONNECTION_H_FC8E1B4392D24D27A2F129D8B93A6348
#define FS_CONNECTION_H_FC8E1B4392D24D27A2F129D8B93A6348

#include <atomic>
#include <unordered_set>

#include "networkmessage.h"

static constexpr int32_t CONNECTION_WRITE_TIMEOUT = 30;
static constexpr int32_t CONNECTION_READ_TIMEOUT = 30;

// Hard caps for the per-connection output queue (A11). A peer that reads too
// slowly — but still fast enough to stay under the write timeout — would
// otherwise accumulate up to ~24.5 KB messages without bound. Exceeding either
// cap force-closes the connection. The message cap also bounds the number of
// queued shared_ptr/asio operations when a peer stalls on small packets.
static constexpr size_t CONNECTION_OUTPUT_QUEUE_MAX_MESSAGES = 256;
static constexpr size_t CONNECTION_OUTPUT_QUEUE_MAX_BYTES = 1u << 20; // 1 MiB

class Protocol;
using Protocol_ptr = std::shared_ptr<Protocol>;
class OutputMessage;
using OutputMessage_ptr = std::shared_ptr<OutputMessage>;
class Connection;
using Connection_ptr = std::shared_ptr<Connection>;
using ConnectionWeak_ptr = std::weak_ptr<Connection>;
class ServiceBase;
using Service_ptr = std::shared_ptr<ServiceBase>;
class ServicePort;
using ServicePort_ptr = std::shared_ptr<ServicePort>;
using ConstServicePort_ptr = std::shared_ptr<const ServicePort>;

class ConnectionManager
{
	public:
		static ConnectionManager& getInstance() {
			static ConnectionManager instance;
			return instance;
		}

                Connection_ptr createConnection(boost::asio::io_context& io_service, ConstServicePort_ptr servicePort);
		void releaseConnection(const Connection_ptr& connection);
		void closeAll();

		// Output queue depth accounting (A11). Bytes are approximated by the
		// pre-encryption message length; the final frame adds a small header.
		// Updated by Connection::send/onWriteOperation under connectionLock.
		void addOutputQueueBytes(uint64_t bytes);
		void removeOutputQueueBytes(uint64_t bytes);
		void recordOutputQueueOverflowDisconnect();

		uint64_t getQueuedOutputBytes() const {
			return queuedOutputBytes.load(std::memory_order_relaxed);
		}
		uint64_t getPeakQueuedOutputBytes() const {
			return peakQueuedOutputBytes.load(std::memory_order_relaxed);
		}
		uint64_t getOutputQueueOverflowDisconnects() const {
			return outputQueueOverflowDisconnects.load(std::memory_order_relaxed);
		}

	private:
		ConnectionManager() = default;

		std::unordered_set<Connection_ptr> connections;
		std::mutex connectionManagerLock;

		std::atomic<uint64_t> queuedOutputBytes{0};
		std::atomic<uint64_t> peakQueuedOutputBytes{0};
		std::atomic<uint64_t> outputQueueOverflowDisconnects{0};
};

class Connection : public std::enable_shared_from_this<Connection>
{
	public:
		// non-copyable
		Connection(const Connection&) = delete;
		Connection& operator=(const Connection&) = delete;

		enum { FORCE_CLOSE = true };

                Connection(boost::asio::io_context& io_service,
		ConstServicePort_ptr service_port) :
			readTimer(io_service),
			writeTimer(io_service),
			service_port(std::move(service_port)),
			socket(io_service),
			timeConnected(time(nullptr)) {}
		~Connection();

		friend class ConnectionManager;

		void close(bool force = false);
		// Used by protocols that require server to send first
		void accept(Protocol_ptr protocol);
		void accept();

		void send(const OutputMessage_ptr& msg);

		uint32_t getIP();

		// Held across every io-thread parse handler (parseHeader/parsePacket)
		// and by close()/send(). Protocol::release() on the Dispatcher acquires
		// this once before destroying the protocol's player, which proves no
		// io-thread parse is currently reading protocol/player state (and none
		// can start anymore, because close() sets closed=true under this same
		// lock before posting the release task).
		//
		// WARNING: code running while holding this lock must never block waiting
		// for the Dispatcher — parse handlers only enqueue tasks. Violating this
		// would deadlock Protocol::release() (and therefore the Dispatcher).
		std::recursive_mutex& getConnectionLock() {
			return connectionLock;
		}

	private:
		void parseHeader(const boost::system::error_code& error);
		void parsePacket(const boost::system::error_code& error);

		void onWriteOperation(const boost::system::error_code& error);

		static void handleTimeout(ConnectionWeak_ptr connectionWeak, const boost::system::error_code& error);

		void closeSocket();
		void internalSend(const OutputMessage_ptr& msg);

		boost::asio::ip::tcp::socket& getSocket() {
			return socket;
		}
		friend class ServicePort;

		NetworkMessage msg;

		boost::asio::steady_timer readTimer;
		boost::asio::steady_timer writeTimer;

		std::recursive_mutex connectionLock;

		struct QueuedOutputMessage {
			OutputMessage_ptr message;
			// getLength() captured at enqueue time. onSendMessage() grows the
			// frame during encryption before the write completes, so reading
			// the length at pop time would not match the value accounted at
			// enqueue and the byte counter would drift negative.
			uint64_t bytes;
		};

		std::list<QueuedOutputMessage> messageQueue;
		// Sum of the queued byte counts above, kept in sync under
		// connectionLock and mirrored into ConnectionManager's global counter.
		uint64_t messageQueueBytes = 0;

		ConstServicePort_ptr service_port;
		Protocol_ptr protocol;

		boost::asio::ip::tcp::socket socket;

		time_t timeConnected;
		uint32_t packetsSent = 0;

		bool closed = false;
		bool receivedFirst = false;
};

#endif
