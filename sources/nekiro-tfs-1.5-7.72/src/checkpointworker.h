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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FS_CHECKPOINTWORKER_H_8B2E4C1D9A734F6E8C5D2A1B7E9F0C34
#define FS_CHECKPOINTWORKER_H_8B2E4C1D9A734F6E8C5D2A1B7E9F0C34

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "database.h"
#include "game.h"
#include "thread_holder_base.h"

// Immutable unit of work handed from the Dispatcher to the checkpoint worker.
// It carries only captured SQL statements and value-type bookkeeping; no live
// Player*, Item*, Tile* or House* pointers may ever reach the worker thread.
struct CheckpointJob {
	uint64_t groupId = 0;
	uint64_t groupVersion = 0;

	std::vector<std::string> playerStatements;
	std::vector<std::string> houseStatements;
	std::vector<std::string> floorStatements;
	std::string markerStatement;

	// Deferred logout saves: a synchronous player save captured on the
	// Dispatcher and queued behind in-flight floor checkpoints so the newer
	// state commits after every older captured state, without blocking the
	// Dispatcher. They carry no floor/house work, skip the checkpoint marker
	// and are settled without floor checkpoint group bookkeeping.
	bool playerSaveOnly = false;
	uint32_t deferredPlayerGuid = 0;
	std::string deferredPlayerName;
	uint32_t deferredLegacyReservationId = 0;
	uint32_t deferredAttempts = 0;

	// Retained for Dispatcher-side completion bookkeeping (clearing dirty
	// tiles whose captured tileVersion still matches). Returned untouched.
	std::vector<PreparedFloorSnapshot> snapshots;

	std::set<uint32_t> playerGuids;
	std::set<uint32_t> houseIds;

	std::chrono::steady_clock::time_point enqueuedAt;
};

struct CheckpointResult {
	std::unique_ptr<CheckpointJob> job;
	bool success = false;
	std::string error;

	uint64_t beginNanoseconds = 0;
	uint64_t sqlNanoseconds = 0;
	uint64_t commitNanoseconds = 0;
	uint64_t totalNanoseconds = 0;
};

// Work reclaimed from a worker thread that died unexpectedly. The Dispatcher
// uses this to release reservations and mark groups for retry so nothing stays
// stuck forever.
struct AbortedCheckpointWork {
	std::vector<std::unique_ptr<CheckpointJob>> queuedJobs;
	// The job being executed when the thread died (nullptr when none was in
	// flight). Its transaction rolled back with the worker's connection; the
	// Dispatcher replays deferred logout saves and retries checkpoint groups.
	std::unique_ptr<CheckpointJob> inFlightJob;
	// Player GUIDs held by the single in-flight job at the moment the thread
	// died (empty when no job was in flight).
	std::set<uint32_t> inFlightPlayerGuids;
};

class CheckpointWorker : public ThreadHolder<CheckpointWorker> {
	public:
		CheckpointWorker() = default;

		CheckpointWorker(const CheckpointWorker&) = delete;
		CheckpointWorker& operator=(const CheckpointWorker&) = delete;

		// Connects the worker's dedicated database connection and starts the
		// worker thread. Returns false when the connection cannot be created.
		bool start();
		// Signals the worker to finish the remaining queued jobs and stop.
		void shutdown();

		// Dispatcher -> worker. Returns false when the worker is unhealthy or
		// the pending queue is full (backpressure); the group stays dirty for
		// retry via the synchronous fallback.
		bool enqueue(std::unique_ptr<CheckpointJob> job);

		// Worker -> Dispatcher. Moves all pending results into out.
		size_t popResults(std::vector<CheckpointResult>& out);

		// Reclaims every queued job (and the in-flight job's player GUIDs) after
		// the worker thread died. Only safe to call once the thread is confirmed
		// dead; the Dispatcher then releases reservations and retries the groups.
		AbortedCheckpointWork abortAllPending();

		size_t queuedCount() const;
		size_t inFlightCount() const;
		size_t pendingCount() const;

		bool isRunning() const {
			return getState() == THREAD_STATE_RUNNING;
		}

		// True only while the worker thread is alive. Becomes false as soon as
		// the thread returns, including an unexpected exit.
		bool isThreadAlive() const {
			return threadActive.load(std::memory_order_relaxed);
		}

		// True while the worker can accept and process jobs: running, not
		// stopping, thread alive, and its heartbeat is fresh. When false the
		// Dispatcher must use the synchronous fallback instead of enqueueing.
		bool isHealthy() const;

		// Blocks up to timeoutMs waiting for at least one job to finish. Used by
		// the Dispatcher drain loop; safe because the worker signals progress
		// without needing the Dispatcher.
		void waitProgress(uint32_t timeoutMs);

		void simulateFailures(uint32_t count) {
			simulatedFailureCount.store(count, std::memory_order_relaxed);
		}

	private:
		friend class ThreadHolder<CheckpointWorker>;
		void threadMain();
		void executeJob(const CheckpointJob& job, CheckpointResult& result);
		void heartbeat();

		static int64_t nowMilliseconds();

		Database db;

		mutable std::mutex mutex;
		std::condition_variable jobSignal;
		std::condition_variable progressSignal;
		std::deque<std::unique_ptr<CheckpointJob>> jobs;
		std::deque<CheckpointResult> results;
		// The job currently being executed. Kept under the mutex so
		// abortAllPending can reclaim it (with its reservation metadata) when
		// the thread dies mid-job.
		std::unique_ptr<CheckpointJob> currentJob;
		// Player GUIDs of the job currently being executed; needed to release
		// reservations if the thread dies mid-job. Guarded by mutex.
		std::set<uint32_t> inFlightPlayerGuids;
		size_t inFlight = 0;

		std::atomic<bool> stopping{false};
		std::atomic<bool> threadActive{false};
		std::atomic<int64_t> lastProgressAt{0};

		std::atomic<uint32_t> simulatedFailureCount{0};

		static constexpr size_t MAX_PENDING_JOBS = 16;
		// A worker whose heartbeat is older than this is considered unhealthy.
		// Generous on purpose: a single slow statement (bounded by MariaDB lock
		// wait timeouts) must not be mistaken for a dead thread.
		static constexpr int64_t WORKER_HEALTH_THRESHOLD_MS = 60000;
};

extern CheckpointWorker g_checkpointWorker;

#endif
