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

#include "otpch.h"

#include "checkpointworker.h"

CheckpointWorker g_checkpointWorker;

int64_t CheckpointWorker::nowMilliseconds()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

void CheckpointWorker::heartbeat()
{
	lastProgressAt.store(nowMilliseconds(), std::memory_order_relaxed);
}

bool CheckpointWorker::isHealthy() const
{
	if (stopping.load(std::memory_order_relaxed)) {
		return false;
	}
	if (getState() != THREAD_STATE_RUNNING) {
		return false;
	}
	if (!threadActive.load(std::memory_order_relaxed)) {
		return false;
	}
	const int64_t last = lastProgressAt.load(std::memory_order_relaxed);
	if (last == 0) {
		// The thread has not reported a heartbeat yet; treat it as healthy
		// briefly so startup is not mistaken for death.
		return true;
	}
	return (nowMilliseconds() - last) < WORKER_HEALTH_THRESHOLD_MS;
}

bool CheckpointWorker::start()
{
	if (!db.connect()) {
		std::cerr << "[CheckpointWorker] could not open a dedicated database connection." << std::endl;
		return false;
	}
	stopping.store(false, std::memory_order_relaxed);
	threadActive.store(false, std::memory_order_relaxed);
	heartbeat();
	ThreadHolder::start();
	return true;
}

void CheckpointWorker::shutdown()
{
	stopping.store(true, std::memory_order_relaxed);
	// Flip the thread state so isRunning() reports false immediately, even
	// while the worker finishes draining the remaining queued jobs.
	stop();
	jobSignal.notify_all();
	progressSignal.notify_all();
}

bool CheckpointWorker::enqueue(std::unique_ptr<CheckpointJob> job)
{
	// Reject work unless the worker is actually able to process it; the caller
	// then falls back to the synchronous path. Checking isHealthy() here (not
	// just `stopping`) prevents handing jobs to a worker that died or stalled.
	if (!isHealthy()) {
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (stopping.load(std::memory_order_relaxed)) {
			return false;
		}
		if (jobs.size() + inFlight >= MAX_PENDING_JOBS) {
			return false;
		}
		job->enqueuedAt = std::chrono::steady_clock::now();
		jobs.push_back(std::move(job));
	}
	jobSignal.notify_one();
	return true;
}

size_t CheckpointWorker::popResults(std::vector<CheckpointResult>& out)
{
	std::lock_guard<std::mutex> lock(mutex);
	const size_t count = results.size();
	while (!results.empty()) {
		out.push_back(std::move(results.front()));
		results.pop_front();
	}
	return count;
}

AbortedCheckpointWork CheckpointWorker::abortAllPending()
{
	AbortedCheckpointWork aborted;
	std::lock_guard<std::mutex> lock(mutex);
	while (!jobs.empty()) {
		aborted.queuedJobs.push_back(std::move(jobs.front()));
		jobs.pop_front();
	}
	// If the thread died while executing a job, that job's transaction was
	// rolled back when its connection was closed, so its reservation must also
	// be released. inFlight is reset because no job is executing anymore.
	aborted.inFlightPlayerGuids = std::move(inFlightPlayerGuids);
	inFlightPlayerGuids.clear();
	inFlight = 0;
	return aborted;
}

size_t CheckpointWorker::queuedCount() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return jobs.size();
}

size_t CheckpointWorker::inFlightCount() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return inFlight;
}

size_t CheckpointWorker::pendingCount() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return jobs.size() + inFlight;
}

void CheckpointWorker::waitProgress(uint32_t timeoutMs)
{
	std::unique_lock<std::mutex> lock(mutex);
	progressSignal.wait_for(lock, std::chrono::milliseconds(timeoutMs));
}

void CheckpointWorker::threadMain()
{
	// Marks the thread alive for as long as this scope is active. The guard
	// runs even when the loop exits through an exception, so an unexpected
	// death is observable via isThreadAlive() instead of hanging reservations.
	struct ActiveGuard {
		std::atomic<bool>& flag;
		explicit ActiveGuard(std::atomic<bool>& activeFlag) : flag(activeFlag) {
			flag.store(true, std::memory_order_relaxed);
		}
		~ActiveGuard() {
			flag.store(false, std::memory_order_relaxed);
		}
	} activeGuard(threadActive);

	try {
		while (true) {
			heartbeat();

			std::unique_ptr<CheckpointJob> job;
			{
				std::unique_lock<std::mutex> lock(mutex);
				jobSignal.wait(lock, [this] { return stopping.load(std::memory_order_relaxed) || !jobs.empty(); });
				if (jobs.empty()) {
					if (stopping.load(std::memory_order_relaxed)) {
						return;
					}
					continue;
				}
				job = std::move(jobs.front());
				jobs.pop_front();
				++inFlight;
				inFlightPlayerGuids = job->playerGuids;
			}

			CheckpointResult result;
			result.job = std::move(job);
			executeJob(*result.job, result);
			heartbeat();

			{
				std::lock_guard<std::mutex> lock(mutex);
				--inFlight;
				inFlightPlayerGuids.clear();
				results.push_back(std::move(result));
			}
			progressSignal.notify_all();
		}
	} catch (const std::exception& exception) {
		std::cerr << "[CheckpointWorker] worker thread aborted: " << exception.what()
		          << ". Checkpoints fall back to the synchronous path." << std::endl;
	} catch (...) {
		std::cerr << "[CheckpointWorker] worker thread aborted by an unknown exception."
		          << " Checkpoints fall back to the synchronous path." << std::endl;
	}
}

void CheckpointWorker::executeJob(const CheckpointJob& job, CheckpointResult& result)
{
	const auto totalStartedAt = std::chrono::steady_clock::now();

	if (simulatedFailureCount.load(std::memory_order_relaxed) != 0) {
		--simulatedFailureCount;
		result.error = "simulated coordinated checkpoint database failure";
		result.success = false;
		result.totalNanoseconds = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - totalStartedAt).count());
		return;
	}

	auto rollback = [this] {
		db.executeQuery("ROLLBACK");
	};

	const auto beginStartedAt = std::chrono::steady_clock::now();
	if (!db.executeQuery("BEGIN")) {
		result.error = "could not begin coordinated checkpoint transaction";
		result.success = false;
		result.totalNanoseconds = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - totalStartedAt).count());
		return;
	}
	result.beginNanoseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - beginStartedAt).count());

	const auto sqlStartedAt = std::chrono::steady_clock::now();
	bool ok = true;
	for (const std::string& statement : job.playerStatements) {
		if (!db.executeQuery(statement)) {
			result.error = "could not save every player in the coordinated checkpoint";
			ok = false;
			break;
		}
	}
	if (ok) {
		for (const std::string& statement : job.houseStatements) {
			if (!db.executeQuery(statement)) {
				result.error = "could not save every house in the coordinated checkpoint";
				ok = false;
				break;
			}
		}
	}
	if (ok) {
		for (const std::string& statement : job.floorStatements) {
			if (!db.executeQuery(statement)) {
				result.error = "could not save every tile in the coordinated checkpoint";
				ok = false;
				break;
			}
		}
	}
	if (ok && !db.executeQuery(job.markerStatement)) {
		result.error = "could not register the coordinated checkpoint commit";
		ok = false;
	}
	result.sqlNanoseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - sqlStartedAt).count());

	if (!ok) {
		rollback();
		result.success = false;
		result.totalNanoseconds = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - totalStartedAt).count());
		return;
	}

	const auto commitStartedAt = std::chrono::steady_clock::now();
	if (!db.executeQuery("COMMIT")) {
		rollback();
		result.error = "could not commit the coordinated checkpoint transaction";
		result.success = false;
	} else {
		result.success = true;
	}
	result.commitNanoseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - commitStartedAt).count());
	result.totalNanoseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - totalStartedAt).count());
}
