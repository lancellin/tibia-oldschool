/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2019  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "otpch.h"

#include "dispatchermetrics.h"

#include <array>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>

namespace {

constexpr size_t DISPATCHER_PHASE_COUNT = static_cast<size_t>(DispatcherMetricsPhase::COUNT);

constexpr std::array<const char*, DISPATCHER_PHASE_COUNT> DISPATCHER_PHASE_NAMES = {
	"login_preload",
	"login_policy",
	"login_full_load",
	"login_player_row_query",
	"login_load_core",
	"login_load_social",
	"login_load_inventory",
	"login_inventory_query",
	"login_inventory_decode",
	"login_inventory_attach",
	"login_load_locker",
	"login_load_depot",
	"login_load_storage",
	"login_load_charms",
	"login_load_vip",
	"login_load_finalize",
	"login_place_creature",
	"login_post_place",
	"login_vip_notify",
	"logout_accepted_total",
	"logout_remove_creature_total",
	"logout_map_remove_notify",
	"logout_callbacks",
	"logout_final_detach",
	"logout_cleanup",
	"logout_online_status",
	"logout_async_snapshot_build",
	"logout_async_statements_finalize",
	"logout_async_prepare_handoff",
	"logout_save_total",
	"logout_save_checkpoint",
	"logout_save_transaction_begin",
	"logout_save_core",
	"logout_save_spells",
	"logout_save_inventory",
	"logout_inventory_prepare",
	"logout_inventory_delete",
	"logout_inventory_serialize",
	"logout_inventory_build_rows",
	"logout_inventory_insert",
	"logout_save_depot",
	"logout_save_storage",
	"logout_save_commit",
	"logout_vip_notify",
	"item_move_total",
	"item_move_persistence",
	"floor_snapshot_tick",
	"floor_snapshot_prepare",
	"floor_checkpoint_group",
	"floor_checkpoint_player_save",
	"floor_checkpoint_tx_begin",
	"floor_checkpoint_house_save",
	"floor_checkpoint_tile_sql",
	"floor_checkpoint_marker_sql",
	"floor_checkpoint_clean_save_sql",
	"floor_checkpoint_tx_commit",
	"floor_checkpoint_db_lock_wait",
	"item_actor_attribution",
	"item_move_stamp",
	"item_move_identify",
	"item_move_attr_endpoint",
	"item_move_attr_path",
	"item_move_checkpoint_reg",
};

thread_local bool logoutMetricsContextActive = false;
thread_local bool dispatcherMetricsSuppressed = false;
thread_local bool databaseLockWaitRecordingActive = false;
thread_local bool checkpointSaveMetricsContextActive = false;

constexpr std::array<uint64_t, 25> DURATION_BUCKETS_NANOSECONDS = {
	1'000ULL,
	2'000ULL,
	5'000ULL,
	10'000ULL,
	20'000ULL,
	50'000ULL,
	100'000ULL,
	200'000ULL,
	500'000ULL,
	1'000'000ULL,
	2'000'000ULL,
	5'000'000ULL,
	10'000'000ULL,
	20'000'000ULL,
	50'000'000ULL,
	100'000'000ULL,
	250'000'000ULL,
	500'000'000ULL,
	1'000'000'000ULL,
	2'000'000'000ULL,
	5'000'000'000ULL,
	10'000'000'000ULL,
	30'000'000'000ULL,
	60'000'000'000ULL,
	std::numeric_limits<uint64_t>::max(),
};

struct DurationSummary {
	uint64_t count = 0;
	double totalMicroseconds = 0;
	double averageMicroseconds = 0;
	double p95Microseconds = 0;
	double p99Microseconds = 0;
	double p999Microseconds = 0;
	double maximumMicroseconds = 0;
	uint64_t over10Milliseconds = 0;
	uint64_t over25Milliseconds = 0;
	uint64_t over50Milliseconds = 0;
	uint64_t over100Milliseconds = 0;
};

class DurationStats {
	public:
		void record(uint64_t nanoseconds)
		{
			++count;
			totalNanoseconds += nanoseconds;
			maximumNanoseconds = std::max(maximumNanoseconds, nanoseconds);

			const auto it = std::lower_bound(
				DURATION_BUCKETS_NANOSECONDS.begin(), DURATION_BUCKETS_NANOSECONDS.end(), nanoseconds);
			const size_t index = static_cast<size_t>(std::distance(DURATION_BUCKETS_NANOSECONDS.begin(), it));
			++buckets[std::min(index, buckets.size() - 1)];

			over10Milliseconds += nanoseconds > 10'000'000ULL;
			over25Milliseconds += nanoseconds > 25'000'000ULL;
			over50Milliseconds += nanoseconds > 50'000'000ULL;
			over100Milliseconds += nanoseconds > 100'000'000ULL;
		}

		DurationSummary summarize() const
		{
			DurationSummary summary;
			summary.count = count;
			if (count == 0) {
				return summary;
			}

			summary.totalMicroseconds = static_cast<double>(totalNanoseconds) / 1000.0;
			summary.averageMicroseconds =
				(static_cast<double>(totalNanoseconds) / static_cast<double>(count)) / 1000.0;
			summary.p95Microseconds = percentileMicroseconds(0.95);
			summary.p99Microseconds = percentileMicroseconds(0.99);
			summary.p999Microseconds = percentileMicroseconds(0.999);
			summary.maximumMicroseconds = static_cast<double>(maximumNanoseconds) / 1000.0;
			summary.over10Milliseconds = over10Milliseconds;
			summary.over25Milliseconds = over25Milliseconds;
			summary.over50Milliseconds = over50Milliseconds;
			summary.over100Milliseconds = over100Milliseconds;
			return summary;
		}

		void reset()
		{
			count = 0;
			totalNanoseconds = 0;
			maximumNanoseconds = 0;
			over10Milliseconds = 0;
			over25Milliseconds = 0;
			over50Milliseconds = 0;
			over100Milliseconds = 0;
			buckets.fill(0);
		}

	private:
		double percentileMicroseconds(double fraction) const
		{
			const uint64_t target = std::max<uint64_t>(
				1, static_cast<uint64_t>(std::ceil(fraction * static_cast<double>(count))));
			uint64_t cumulative = 0;
			for (size_t i = 0; i < buckets.size(); ++i) {
				cumulative += buckets[i];
				if (cumulative >= target) {
					const uint64_t upperBound = DURATION_BUCKETS_NANOSECONDS[i];
					const uint64_t value = upperBound == std::numeric_limits<uint64_t>::max()
						? maximumNanoseconds
						: std::min(upperBound, maximumNanoseconds);
					return static_cast<double>(value) / 1000.0;
				}
			}
			return static_cast<double>(maximumNanoseconds) / 1000.0;
		}

		uint64_t count = 0;
		uint64_t totalNanoseconds = 0;
		uint64_t maximumNanoseconds = 0;
		uint64_t over10Milliseconds = 0;
		uint64_t over25Milliseconds = 0;
		uint64_t over50Milliseconds = 0;
		uint64_t over100Milliseconds = 0;
		std::array<uint64_t, DURATION_BUCKETS_NANOSECONDS.size()> buckets{};
};

struct DispatcherMetricsRow {
	std::chrono::system_clock::time_point timestamp;
	uint64_t intervalMilliseconds = 0;
	uint64_t maximumQueueDepth = 0;
	uint64_t expiredTasks = 0;

	DurationSummary taskExecution;
	DurationSummary queueWait;
	DurationSummary batchExecution;
	uint64_t batchCount = 0;
	uint64_t batchTasks = 0;
	uint64_t maximumBatchTasks = 0;
	uint64_t dispatcherBusyMicroseconds = 0;
	double dispatcherBusyPercent = 0;

	DurationSummary loginExecution;
	uint64_t successfulLogins = 0;
	uint64_t failedLogins = 0;
	std::array<DurationSummary, DISPATCHER_PHASE_COUNT> phases;
	uint64_t floorDirtyMarks = 0;
	uint64_t floorDirtyTilesMax = 0;
	uint64_t floorCheckpointGroupsSaved = 0;
	uint64_t floorCheckpointMaxTiles = 0;
	uint64_t floorCheckpointMaxPlayers = 0;
	uint64_t floorCheckpointTileQueries = 0;
	uint64_t actorAttributionsPendingMax = 0;
	uint64_t actorAttributionsResolved = 0;
};

class DispatcherMetrics {
	public:
		DispatcherMetrics()
		{
			const char* configuredPath = std::getenv("TFS_DISPATCHER_METRICS_PATH");
			if (!configuredPath || configuredPath[0] == '\0') {
				return;
			}

			path = configuredPath;
			interval = std::chrono::milliseconds(5000);
			if (const char* configuredInterval = std::getenv("TFS_DISPATCHER_METRICS_INTERVAL_MS")) {
				try {
					const uint64_t value = std::stoull(configuredInterval);
					interval = std::chrono::milliseconds(std::max<uint64_t>(1000, value));
				} catch (const std::exception&) {
					std::cout << "Dispatcher metrics: invalid TFS_DISPATCHER_METRICS_INTERVAL_MS; using 5000 ms."
					          << std::endl;
				}
			}

			enabled = true;
			windowStartedAt = std::chrono::steady_clock::now();
			writerThread = std::thread(&DispatcherMetrics::writerLoop, this);
			std::cout << "Dispatcher metrics enabled: path=" << path
			          << " interval_ms=" << interval.count()
			          << ". Metrics are aggregated; gameplay logic is unchanged." << std::endl;
		}

		~DispatcherMetrics()
		{
			if (!enabled) {
				return;
			}

			flushWindow(std::chrono::steady_clock::now(), true);
			{
				std::lock_guard<std::mutex> lock(queueMutex);
				stopping = true;
			}
			queueSignal.notify_one();
			if (writerThread.joinable()) {
				writerThread.join();
			}
		}

		bool isEnabled() const {
			return enabled;
		}

		void recordQueueDepth(size_t depth)
		{
			if (!enabled) {
				return;
			}

			uint64_t current = maximumQueueDepth.load(std::memory_order_relaxed);
			while (depth > current && !maximumQueueDepth.compare_exchange_weak(
				current, static_cast<uint64_t>(depth), std::memory_order_relaxed)) {
			}
		}

		void recordTask(uint64_t queueWaitNanoseconds, uint64_t executionNanoseconds)
		{
			if (!enabled) {
				return;
			}

			queueWait.record(queueWaitNanoseconds);
			taskExecution.record(executionNanoseconds);
			dispatcherBusyNanoseconds += executionNanoseconds;
		}

		void recordBatch(uint64_t executionNanoseconds, size_t tasks, size_t, size_t expired)
		{
			if (!enabled) {
				return;
			}

			batchExecution.record(executionNanoseconds);
			++batchCount;
			batchTasks += tasks;
			maximumBatchTasks = std::max<uint64_t>(maximumBatchTasks, tasks);
			expiredTasks += expired;

			const auto now = std::chrono::steady_clock::now();
			if (now - windowStartedAt >= interval) {
				flushWindow(now, false);
			}
		}

		void recordLogin(uint64_t executionNanoseconds, bool successful)
		{
			if (!enabled) {
				return;
			}

			loginExecution.record(executionNanoseconds);
			if (successful) {
				++successfulLogins;
			} else {
				++failedLogins;
			}
		}

		void recordPhase(DispatcherMetricsPhase phase, uint64_t executionNanoseconds)
		{
			if (!enabled) {
				return;
			}

			phaseExecution[static_cast<size_t>(phase)].record(executionNanoseconds);
		}

		void recordFloorDirtyEvent(size_t currentDirtyTiles)
		{
			if (!enabled) {
				return;
			}

			++floorDirtyMarks;
			floorDirtyTilesMax = std::max<uint64_t>(floorDirtyTilesMax, currentDirtyTiles);
		}

		void recordCheckpointGroupSaved(size_t tiles, size_t players)
		{
			if (!enabled) {
				return;
			}

			++floorCheckpointGroupsSaved;
			floorCheckpointMaxTiles = std::max<uint64_t>(floorCheckpointMaxTiles, tiles);
			floorCheckpointMaxPlayers = std::max<uint64_t>(floorCheckpointMaxPlayers, players);
		}

		void recordCheckpointTileQueries(size_t count)
		{
			if (!enabled) {
				return;
			}

			floorCheckpointTileQueries += count;
		}

		void recordActorAttributionQueued(size_t pendingCount)
		{
			if (!enabled) {
				return;
			}

			actorAttributionsPendingMax = std::max<uint64_t>(actorAttributionsPendingMax, pendingCount);
		}

		void recordActorAttributionsResolved(uint32_t count)
		{
			if (!enabled) {
				return;
			}

			actorAttributionsResolved += count;
		}

	private:
		void flushWindow(std::chrono::steady_clock::time_point now, bool force)
		{
			if (!enabled || (!force && taskExecution.summarize().count == 0)) {
				return;
			}

			DispatcherMetricsRow row;
			row.timestamp = std::chrono::system_clock::now();
			row.intervalMilliseconds = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(now - windowStartedAt).count());
			row.maximumQueueDepth = maximumQueueDepth.exchange(0, std::memory_order_relaxed);
			row.expiredTasks = expiredTasks;
			row.taskExecution = taskExecution.summarize();
			row.queueWait = queueWait.summarize();
			row.batchExecution = batchExecution.summarize();
			row.batchCount = batchCount;
			row.batchTasks = batchTasks;
			row.maximumBatchTasks = maximumBatchTasks;
			row.dispatcherBusyMicroseconds = dispatcherBusyNanoseconds / 1000;
			const uint64_t intervalNanoseconds = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(now - windowStartedAt).count());
			row.dispatcherBusyPercent = intervalNanoseconds == 0
				? 0
				: (static_cast<double>(dispatcherBusyNanoseconds) * 100.0) /
					static_cast<double>(intervalNanoseconds);

			row.loginExecution = loginExecution.summarize();
			row.successfulLogins = successfulLogins;
			row.failedLogins = failedLogins;
			for (size_t i = 0; i < DISPATCHER_PHASE_COUNT; ++i) {
				row.phases[i] = phaseExecution[i].summarize();
			}
			row.floorDirtyMarks = floorDirtyMarks;
			row.floorDirtyTilesMax = floorDirtyTilesMax;
			row.floorCheckpointGroupsSaved = floorCheckpointGroupsSaved;
			row.floorCheckpointMaxTiles = floorCheckpointMaxTiles;
			row.floorCheckpointMaxPlayers = floorCheckpointMaxPlayers;
			row.floorCheckpointTileQueries = floorCheckpointTileQueries;
			row.actorAttributionsPendingMax = actorAttributionsPendingMax;
			row.actorAttributionsResolved = actorAttributionsResolved;

			{
				std::lock_guard<std::mutex> lock(queueMutex);
				rows.emplace_back(std::move(row));
			}
			queueSignal.notify_one();

			taskExecution.reset();
			queueWait.reset();
			batchExecution.reset();
			loginExecution.reset();
			for (DurationStats& phase : phaseExecution) {
				phase.reset();
			}
			expiredTasks = 0;
			batchCount = 0;
			batchTasks = 0;
			maximumBatchTasks = 0;
			dispatcherBusyNanoseconds = 0;
			successfulLogins = 0;
			failedLogins = 0;
			floorDirtyMarks = 0;
			floorDirtyTilesMax = 0;
			floorCheckpointGroupsSaved = 0;
			floorCheckpointMaxTiles = 0;
			floorCheckpointMaxPlayers = 0;
			floorCheckpointTileQueries = 0;
			actorAttributionsPendingMax = 0;
			actorAttributionsResolved = 0;
			windowStartedAt = now;
		}

		void writerLoop()
		{
			std::ofstream output(path, std::ios::out | std::ios::app);
			if (!output) {
				std::cerr << "Dispatcher metrics: unable to open " << path << std::endl;
				return;
			}

			output.seekp(0, std::ios::end);
			if (output.tellp() == 0) {
				output
					<< "timestamp,interval_ms,tasks,expired_tasks,queue_depth_max,"
					   "task_avg_us,task_p95_us,task_p99_us,task_p999_us,task_max_us,"
					   "task_over_10ms,task_over_25ms,task_over_50ms,task_over_100ms,"
					   "queue_wait_avg_us,queue_wait_p95_us,queue_wait_p99_us,queue_wait_p999_us,"
					   "queue_wait_max_us,queue_wait_over_10ms,queue_wait_over_25ms,"
					   "queue_wait_over_50ms,queue_wait_over_100ms,"
					   "batches,batch_tasks_avg,batch_tasks_max,batch_avg_us,batch_p95_us,"
					   "batch_p99_us,batch_max_us,dispatcher_busy_us,dispatcher_busy_pct,"
					   "login_attempts,login_success,login_failed,login_avg_us,login_p95_us,"
					   "login_p99_us,login_p999_us,login_max_us";
				for (const char* phaseName : DISPATCHER_PHASE_NAMES) {
					output << ',' << phaseName << "_count"
					       << ',' << phaseName << "_total_us"
					       << ',' << phaseName << "_avg_us"
					       << ',' << phaseName << "_p95_us"
					       << ',' << phaseName << "_p99_us"
					       << ',' << phaseName << "_max_us";
				}
				output << ",floor_dirty_marks,floor_dirty_tiles_max,"
				          "floor_checkpoint_groups_saved,floor_checkpoint_max_tiles,"
				          "floor_checkpoint_max_players,floor_checkpoint_tile_queries,"
				          "actor_attributions_pending_max,"
				          "actor_attributions_resolved";
				output << '\n';
				output.flush();
			}

			while (true) {
				std::deque<DispatcherMetricsRow> pendingRows;
				{
					std::unique_lock<std::mutex> lock(queueMutex);
					queueSignal.wait(lock, [&]() { return stopping || !rows.empty(); });
					pendingRows.swap(rows);
					if (stopping && pendingRows.empty()) {
						break;
					}
				}

				for (const DispatcherMetricsRow& row : pendingRows) {
					const std::time_t timestamp = std::chrono::system_clock::to_time_t(row.timestamp);
					std::tm localTime{};
#ifdef _WIN32
					localtime_s(&localTime, &timestamp);
#else
					localtime_r(&timestamp, &localTime);
#endif

					const double batchTasksAverage = row.batchCount == 0
						? 0
						: static_cast<double>(row.batchTasks) / static_cast<double>(row.batchCount);
					output << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S") << ','
					       << row.intervalMilliseconds << ','
					       << row.taskExecution.count << ','
					       << row.expiredTasks << ','
					       << row.maximumQueueDepth << ','
					       << std::fixed << std::setprecision(3)
					       << row.taskExecution.averageMicroseconds << ','
					       << row.taskExecution.p95Microseconds << ','
					       << row.taskExecution.p99Microseconds << ','
					       << row.taskExecution.p999Microseconds << ','
					       << row.taskExecution.maximumMicroseconds << ','
					       << row.taskExecution.over10Milliseconds << ','
					       << row.taskExecution.over25Milliseconds << ','
					       << row.taskExecution.over50Milliseconds << ','
					       << row.taskExecution.over100Milliseconds << ','
					       << row.queueWait.averageMicroseconds << ','
					       << row.queueWait.p95Microseconds << ','
					       << row.queueWait.p99Microseconds << ','
					       << row.queueWait.p999Microseconds << ','
					       << row.queueWait.maximumMicroseconds << ','
					       << row.queueWait.over10Milliseconds << ','
					       << row.queueWait.over25Milliseconds << ','
					       << row.queueWait.over50Milliseconds << ','
					       << row.queueWait.over100Milliseconds << ','
					       << row.batchCount << ','
					       << batchTasksAverage << ','
					       << row.maximumBatchTasks << ','
					       << row.batchExecution.averageMicroseconds << ','
					       << row.batchExecution.p95Microseconds << ','
					       << row.batchExecution.p99Microseconds << ','
					       << row.batchExecution.maximumMicroseconds << ','
					       << row.dispatcherBusyMicroseconds << ','
					       << row.dispatcherBusyPercent << ','
					       << row.loginExecution.count << ','
					       << row.successfulLogins << ','
					       << row.failedLogins << ','
					       << row.loginExecution.averageMicroseconds << ','
					       << row.loginExecution.p95Microseconds << ','
					       << row.loginExecution.p99Microseconds << ','
					       << row.loginExecution.p999Microseconds << ','
					       << row.loginExecution.maximumMicroseconds;
					for (const DurationSummary& phase : row.phases) {
						output << ',' << phase.count
						       << ',' << phase.totalMicroseconds
						       << ',' << phase.averageMicroseconds
						       << ',' << phase.p95Microseconds
						       << ',' << phase.p99Microseconds
						       << ',' << phase.maximumMicroseconds;
					}
					output << ',' << row.floorDirtyMarks
					       << ',' << row.floorDirtyTilesMax
					       << ',' << row.floorCheckpointGroupsSaved
					       << ',' << row.floorCheckpointMaxTiles
					       << ',' << row.floorCheckpointMaxPlayers
					       << ',' << row.floorCheckpointTileQueries
					       << ',' << row.actorAttributionsPendingMax
					       << ',' << row.actorAttributionsResolved;
					output << '\n';
				}
				output.flush();
			}
		}

		bool enabled = false;
		bool stopping = false;
		std::string path;
		std::chrono::milliseconds interval{5000};
		std::chrono::steady_clock::time_point windowStartedAt;

		DurationStats taskExecution;
		DurationStats queueWait;
		DurationStats batchExecution;
		DurationStats loginExecution;
		std::array<DurationStats, DISPATCHER_PHASE_COUNT> phaseExecution;
		std::atomic<uint64_t> maximumQueueDepth{0};
		uint64_t expiredTasks = 0;
		uint64_t batchCount = 0;
		uint64_t batchTasks = 0;
		uint64_t maximumBatchTasks = 0;
		uint64_t dispatcherBusyNanoseconds = 0;
		uint64_t successfulLogins = 0;
		uint64_t failedLogins = 0;
		uint64_t floorDirtyMarks = 0;
		uint64_t floorDirtyTilesMax = 0;
		uint64_t floorCheckpointGroupsSaved = 0;
		uint64_t floorCheckpointMaxTiles = 0;
		uint64_t floorCheckpointMaxPlayers = 0;
		uint64_t floorCheckpointTileQueries = 0;
		uint64_t actorAttributionsPendingMax = 0;
		uint64_t actorAttributionsResolved = 0;

		std::mutex queueMutex;
		std::condition_variable queueSignal;
		std::deque<DispatcherMetricsRow> rows;
		std::thread writerThread;
};

DispatcherMetrics& getDispatcherMetrics()
{
	static DispatcherMetrics metrics;
	return metrics;
}

}

bool dispatcherMetricsEnabled()
{
	return getDispatcherMetrics().isEnabled();
}

void recordDispatcherQueueDepth(size_t depth)
{
	getDispatcherMetrics().recordQueueDepth(depth);
}

void recordDispatcherTask(uint64_t queueWaitNanoseconds, uint64_t executionNanoseconds)
{
	getDispatcherMetrics().recordTask(queueWaitNanoseconds, executionNanoseconds);
}

void recordDispatcherBatch(uint64_t executionNanoseconds, size_t tasks, size_t executed, size_t expired)
{
	getDispatcherMetrics().recordBatch(executionNanoseconds, tasks, executed, expired);
}

void recordDispatcherLogin(uint64_t executionNanoseconds, bool successful)
{
	getDispatcherMetrics().recordLogin(executionNanoseconds, successful);
}

void recordDispatcherPhase(DispatcherMetricsPhase phase, uint64_t executionNanoseconds)
{
	getDispatcherMetrics().recordPhase(phase, executionNanoseconds);
}

void recordDispatcherFloorDirtyEvent(size_t currentDirtyTiles)
{
	getDispatcherMetrics().recordFloorDirtyEvent(currentDirtyTiles);
}

void recordDispatcherCheckpointGroupSaved(size_t tiles, size_t players)
{
	getDispatcherMetrics().recordCheckpointGroupSaved(tiles, players);
}

void recordDispatcherCheckpointTileQueries(size_t count)
{
	getDispatcherMetrics().recordCheckpointTileQueries(count);
}

void recordDispatcherActorAttributionQueued(size_t pendingCount)
{
	getDispatcherMetrics().recordActorAttributionQueued(pendingCount);
}

void recordDispatcherActorAttributionsResolved(uint32_t count)
{
	getDispatcherMetrics().recordActorAttributionsResolved(count);
}

void recordDispatcherDatabaseLockWait(uint64_t waitNanoseconds)
{
	recordDispatcherPhase(DispatcherMetricsPhase::FLOOR_CHECKPOINT_DB_LOCK_WAIT, waitNanoseconds);
}

bool dispatcherDatabaseLockWaitRecordingActive()
{
	return databaseLockWaitRecordingActive && dispatcherMetricsEnabled();
}

DispatcherDatabaseLockWaitScope::DispatcherDatabaseLockWaitScope()
	: previous(databaseLockWaitRecordingActive)
{
	databaseLockWaitRecordingActive = true;
}

DispatcherDatabaseLockWaitScope::~DispatcherDatabaseLockWaitScope()
{
	databaseLockWaitRecordingActive = previous;
}

bool dispatcherLogoutMetricsContextActive()
{
	return logoutMetricsContextActive && !dispatcherMetricsSuppressed;
}

bool dispatcherPlayerSaveMetricsContextActive()
{
	return (logoutMetricsContextActive || checkpointSaveMetricsContextActive) &&
		!dispatcherMetricsSuppressed;
}

DispatcherCheckpointSaveMetricsContext::DispatcherCheckpointSaveMetricsContext()
	: previous(checkpointSaveMetricsContextActive)
{
	checkpointSaveMetricsContextActive = true;
}

DispatcherCheckpointSaveMetricsContext::~DispatcherCheckpointSaveMetricsContext()
{
	checkpointSaveMetricsContextActive = previous;
}

DispatcherMetricsSuppressionScope::DispatcherMetricsSuppressionScope()
	: previous(dispatcherMetricsSuppressed)
{
	dispatcherMetricsSuppressed = true;
}

DispatcherMetricsSuppressionScope::~DispatcherMetricsSuppressionScope()
{
	dispatcherMetricsSuppressed = previous;
}

DispatcherPhaseMetricsTimer::DispatcherPhaseMetricsTimer(DispatcherMetricsPhase phase, bool measure)
	: phase(phase), enabled(measure && dispatcherMetricsEnabled() && !dispatcherMetricsSuppressed)
{
	if (enabled) {
		startedAt = std::chrono::steady_clock::now();
	}
}

DispatcherPhaseMetricsTimer::~DispatcherPhaseMetricsTimer()
{
	stop();
}

void DispatcherPhaseMetricsTimer::stop()
{
	if (!enabled) {
		return;
	}

	const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now() - startedAt);
	recordDispatcherPhase(phase, static_cast<uint64_t>(elapsed.count()));
	enabled = false;
}

DispatcherLogoutMetricsContext::DispatcherLogoutMetricsContext()
	: previous(logoutMetricsContextActive)
{
	logoutMetricsContextActive = true;
}

DispatcherLogoutMetricsContext::~DispatcherLogoutMetricsContext()
{
	logoutMetricsContextActive = previous;
}

DispatcherLoginMetricsTimer::DispatcherLoginMetricsTimer()
	: enabled(dispatcherMetricsEnabled())
{
	if (enabled) {
		startedAt = std::chrono::steady_clock::now();
	}
}

DispatcherLoginMetricsTimer::~DispatcherLoginMetricsTimer()
{
	if (!enabled) {
		return;
	}

	const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now() - startedAt);
	recordDispatcherLogin(static_cast<uint64_t>(elapsed.count()), successful);
}
