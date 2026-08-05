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

#include "otpch.h"

#include "outputmessage.h"
#include "protocol.h"
#include "lockfree.h"
#include "scheduler.h"
#include "tasks.h"

#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>

extern Scheduler g_scheduler;
extern Dispatcher g_dispatcher;

namespace {

const uint16_t OUTPUTMESSAGE_FREE_LIST_CAPACITY = 2048;
const std::chrono::milliseconds OUTPUTMESSAGE_AUTOSEND_DELAY {10};

struct AutosendMetricsRow {
	std::chrono::system_clock::time_point timestamp;
	uint64_t intervalMilliseconds = 0;
	uint64_t calls = 0;
	double averageMicroseconds = 0;
	double p95Microseconds = 0;
	double p99Microseconds = 0;
	double maximumMicroseconds = 0;
	uint64_t protocolsExamined = 0;
	uint64_t protocolsPending = 0;
	double pendingPercent = 0;
	uint64_t dispatcherBusyMicroseconds = 0;
	uint64_t autosendMicroseconds = 0;
	double dispatcherPercent = 0;
};

class AutosendMetrics {
	public:
		AutosendMetrics()
		{
			const char* configuredPath = std::getenv("TFS_AUTOSEND_METRICS_PATH");
			if (!configuredPath || configuredPath[0] == '\0') {
				return;
			}

			path = configuredPath;
			interval = std::chrono::milliseconds(5000);
			if (const char* configuredInterval = std::getenv("TFS_AUTOSEND_METRICS_INTERVAL_MS")) {
				try {
					const uint64_t value = std::stoull(configuredInterval);
					interval = std::chrono::milliseconds(std::max<uint64_t>(1000, value));
				} catch (const std::exception&) {
					std::cout << "Autosend metrics: invalid TFS_AUTOSEND_METRICS_INTERVAL_MS; using 5000 ms."
					          << std::endl;
				}
			}

			enabled = true;
			windowStartedAt = std::chrono::steady_clock::now();
			lastDispatcherNanoseconds = g_dispatcher.getExecutedNanoseconds();
			writerThread = std::thread(&AutosendMetrics::writerLoop, this);
			std::cout << "Autosend metrics enabled: path=" << path
			          << " interval_ms=" << interval.count()
			          << ". Current autosend scan logic is unchanged." << std::endl;
		}

		~AutosendMetrics()
		{
			if (!enabled) {
				return;
			}

			{
				std::lock_guard<std::mutex> lock(queueMutex);
				stopping = true;
			}
			queueSignal.notify_one();
			if (writerThread.joinable()) {
				writerThread.join();
			}
		}

		void record(uint64_t durationNanoseconds, uint64_t examined, uint64_t pending)
		{
			if (!enabled) {
				return;
			}

			durations.push_back(durationNanoseconds);
			totalDurationNanoseconds += durationNanoseconds;
			protocolsExamined += examined;
			protocolsPending += pending;

			const auto now = std::chrono::steady_clock::now();
			if (now - windowStartedAt < interval) {
				return;
			}

			std::sort(durations.begin(), durations.end());
			const auto percentile = [&](double fraction) {
				const size_t index = std::min(
					durations.size() - 1,
					static_cast<size_t>(std::ceil(fraction * static_cast<double>(durations.size()))) - 1);
				return static_cast<double>(durations[index]) / 1000.0;
			};

			const uint64_t dispatcherNanoseconds = g_dispatcher.getExecutedNanoseconds();
			// Dispatcher accounts for the current task immediately after sendAll returns.
			// Add the measured sendAll body now, then carry that estimate as the next baseline.
			const uint64_t effectiveDispatcherNanoseconds = dispatcherNanoseconds + durationNanoseconds;
			const uint64_t dispatcherDelta = effectiveDispatcherNanoseconds >= lastDispatcherNanoseconds
				? effectiveDispatcherNanoseconds - lastDispatcherNanoseconds
				: 0;

			AutosendMetricsRow row;
			row.timestamp = std::chrono::system_clock::now();
			row.intervalMilliseconds = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(now - windowStartedAt).count());
			row.calls = durations.size();
			row.averageMicroseconds =
				(static_cast<double>(totalDurationNanoseconds) / static_cast<double>(durations.size())) / 1000.0;
			row.p95Microseconds = percentile(0.95);
			row.p99Microseconds = percentile(0.99);
			row.maximumMicroseconds = static_cast<double>(durations.back()) / 1000.0;
			row.protocolsExamined = protocolsExamined;
			row.protocolsPending = protocolsPending;
			row.pendingPercent = protocolsExamined == 0
				? 0
				: (static_cast<double>(protocolsPending) * 100.0) / static_cast<double>(protocolsExamined);
			row.dispatcherBusyMicroseconds = dispatcherDelta / 1000;
			row.autosendMicroseconds = totalDurationNanoseconds / 1000;
			row.dispatcherPercent = dispatcherDelta == 0
				? 0
				: (static_cast<double>(totalDurationNanoseconds) * 100.0) / static_cast<double>(dispatcherDelta);

			{
				std::lock_guard<std::mutex> lock(queueMutex);
				rows.emplace_back(std::move(row));
			}
			queueSignal.notify_one();

			durations.clear();
			totalDurationNanoseconds = 0;
			protocolsExamined = 0;
			protocolsPending = 0;
			windowStartedAt = now;
			lastDispatcherNanoseconds = effectiveDispatcherNanoseconds;
		}

	private:
		void writerLoop()
		{
			std::ofstream output(path, std::ios::out | std::ios::app);
			if (!output) {
				std::cerr << "Autosend metrics: unable to open " << path << std::endl;
				return;
			}

			output.seekp(0, std::ios::end);
			if (output.tellp() == 0) {
				output << "timestamp,interval_ms,calls,avg_us,p95_us,p99_us,max_us,"
				          "protocols_examined,protocols_pending,pending_pct,dispatcher_busy_us,"
				          "autosend_total_us,autosend_dispatcher_pct\n";
				output.flush();
			}

			while (true) {
				std::deque<AutosendMetricsRow> pendingRows;
				{
					std::unique_lock<std::mutex> lock(queueMutex);
					queueSignal.wait(lock, [&]() { return stopping || !rows.empty(); });
					pendingRows.swap(rows);
					if (stopping && pendingRows.empty()) {
						break;
					}
				}

				for (const AutosendMetricsRow& row : pendingRows) {
					const std::time_t timestamp = std::chrono::system_clock::to_time_t(row.timestamp);
					std::tm localTime{};
#ifdef _WIN32
					localtime_s(&localTime, &timestamp);
#else
					localtime_r(&timestamp, &localTime);
#endif
					output << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S") << ','
					       << row.intervalMilliseconds << ','
					       << row.calls << ','
					       << std::fixed << std::setprecision(3)
					       << row.averageMicroseconds << ','
					       << row.p95Microseconds << ','
					       << row.p99Microseconds << ','
					       << row.maximumMicroseconds << ','
					       << row.protocolsExamined << ','
					       << row.protocolsPending << ','
					       << row.pendingPercent << ','
					       << row.dispatcherBusyMicroseconds << ','
					       << row.autosendMicroseconds << ','
					       << row.dispatcherPercent << '\n';
				}
				output.flush();
			}
		}

		bool enabled = false;
		bool stopping = false;
		std::string path;
		std::chrono::milliseconds interval{5000};
		std::chrono::steady_clock::time_point windowStartedAt;
		uint64_t lastDispatcherNanoseconds = 0;
		std::vector<uint64_t> durations;
		uint64_t totalDurationNanoseconds = 0;
		uint64_t protocolsExamined = 0;
		uint64_t protocolsPending = 0;
		std::mutex queueMutex;
		std::condition_variable queueSignal;
		std::deque<AutosendMetricsRow> rows;
		std::thread writerThread;
};

AutosendMetrics& getAutosendMetrics()
{
	static AutosendMetrics metrics;
	return metrics;
}

void sendAll(const std::vector<Protocol_ptr>& bufferedProtocols);

void scheduleSendAll(const std::vector<Protocol_ptr>& bufferedProtocols)
{
	g_scheduler.addEvent(createSchedulerTask(OUTPUTMESSAGE_AUTOSEND_DELAY.count(), [&]() { sendAll(bufferedProtocols); }));
}

void sendAll(const std::vector<Protocol_ptr>& bufferedProtocols)
{
	//dispatcher thread
	const auto startedAt = std::chrono::steady_clock::now();
	uint64_t pendingProtocols = 0;
	for (auto& protocol : bufferedProtocols) {
		auto& msg = protocol->getCurrentBuffer();
		if (msg) {
			++pendingProtocols;
			protocol->send(std::move(msg));
		}
	}

	if (!bufferedProtocols.empty()) {
		scheduleSendAll(bufferedProtocols);
	}

	const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now() - startedAt);
	getAutosendMetrics().record(
		static_cast<uint64_t>(elapsed.count()),
		static_cast<uint64_t>(bufferedProtocols.size()),
		pendingProtocols);
}

}

void OutputMessagePool::addProtocolToAutosend(Protocol_ptr protocol)
{
	//dispatcher thread
	if (bufferedProtocols.empty()) {
		scheduleSendAll(bufferedProtocols);
	}
	bufferedProtocols.emplace_back(protocol);
}

void OutputMessagePool::removeProtocolFromAutosend(const Protocol_ptr& protocol)
{
	//dispatcher thread
	auto it = std::find(bufferedProtocols.begin(), bufferedProtocols.end(), protocol);
	if (it != bufferedProtocols.end()) {
		std::swap(*it, bufferedProtocols.back());
		bufferedProtocols.pop_back();
	}
}

OutputMessage_ptr OutputMessagePool::getOutputMessage()
{
	// LockfreePoolingAllocator<void,...> will leave (void* allocate) ill-formed because
	// of sizeof(T), so this guarantees that only one list will be initialized
	return std::allocate_shared<OutputMessage>(LockfreePoolingAllocator<void, OUTPUTMESSAGE_FREE_LIST_CAPACITY>());
}
