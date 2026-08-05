/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2019  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef FS_DISPATCHERMETRICS_H_7D43C8D8392A43AE9D6D9484489518AD
#define FS_DISPATCHERMETRICS_H_7D43C8D8392A43AE9D6D9484489518AD

#include <chrono>
#include <cstddef>
#include <cstdint>

enum class DispatcherMetricsPhase : uint8_t {
	LOGIN_PRELOAD,
	LOGIN_POLICY,
	LOGIN_FULL_LOAD,
	LOGIN_PLAYER_ROW_QUERY,
	LOGIN_LOAD_CORE,
	LOGIN_LOAD_SOCIAL,
	LOGIN_LOAD_INVENTORY,
	LOGIN_INVENTORY_QUERY,
	LOGIN_INVENTORY_DECODE,
	LOGIN_INVENTORY_ATTACH,
	LOGIN_LOAD_LOCKER,
	LOGIN_LOAD_DEPOT,
	LOGIN_LOAD_STORAGE,
	LOGIN_LOAD_CHARMS,
	LOGIN_LOAD_VIP,
	LOGIN_LOAD_FINALIZE,
	LOGIN_PLACE_CREATURE,
	LOGIN_POST_PLACE,
	LOGIN_VIP_NOTIFY,
	LOGOUT_ACCEPTED_TOTAL,
	LOGOUT_REMOVE_CREATURE_TOTAL,
	LOGOUT_MAP_REMOVE_NOTIFY,
	LOGOUT_CALLBACKS,
	LOGOUT_FINAL_DETACH,
	LOGOUT_CLEANUP,
	LOGOUT_ONLINE_STATUS,
	LOGOUT_ASYNC_SNAPSHOT_BUILD,
	LOGOUT_ASYNC_STATEMENTS_FINALIZE,
	LOGOUT_ASYNC_PREPARE_HANDOFF,
	LOGOUT_SAVE_TOTAL,
	LOGOUT_SAVE_CHECKPOINT,
	LOGOUT_SAVE_TRANSACTION_BEGIN,
	LOGOUT_SAVE_CORE,
	LOGOUT_SAVE_SPELLS,
	LOGOUT_SAVE_INVENTORY,
	LOGOUT_INVENTORY_PREPARE,
	LOGOUT_INVENTORY_DELETE,
	LOGOUT_INVENTORY_SERIALIZE,
	LOGOUT_INVENTORY_BUILD_ROWS,
	LOGOUT_INVENTORY_INSERT,
	LOGOUT_SAVE_DEPOT,
	LOGOUT_SAVE_STORAGE,
	LOGOUT_SAVE_COMMIT,
	LOGOUT_VIP_NOTIFY,
	COUNT
};

bool dispatcherMetricsEnabled();
void recordDispatcherQueueDepth(size_t depth);
void recordDispatcherTask(uint64_t queueWaitNanoseconds, uint64_t executionNanoseconds);
void recordDispatcherBatch(uint64_t executionNanoseconds, size_t tasks, size_t executed, size_t expired);
void recordDispatcherLogin(uint64_t executionNanoseconds, bool successful);
void recordDispatcherPhase(DispatcherMetricsPhase phase, uint64_t executionNanoseconds);
bool dispatcherLogoutMetricsContextActive();

class DispatcherMetricsSuppressionScope {
	public:
		DispatcherMetricsSuppressionScope();
		~DispatcherMetricsSuppressionScope();

		DispatcherMetricsSuppressionScope(const DispatcherMetricsSuppressionScope&) = delete;
		DispatcherMetricsSuppressionScope& operator=(const DispatcherMetricsSuppressionScope&) = delete;

	private:
		bool previous = false;
};

class DispatcherPhaseMetricsTimer {
	public:
		explicit DispatcherPhaseMetricsTimer(DispatcherMetricsPhase phase, bool measure = true);
		~DispatcherPhaseMetricsTimer();

		DispatcherPhaseMetricsTimer(const DispatcherPhaseMetricsTimer&) = delete;
		DispatcherPhaseMetricsTimer& operator=(const DispatcherPhaseMetricsTimer&) = delete;

		void stop();

	private:
		DispatcherMetricsPhase phase;
		std::chrono::steady_clock::time_point startedAt;
		bool enabled = false;
};

class DispatcherLogoutMetricsContext {
	public:
		DispatcherLogoutMetricsContext();
		~DispatcherLogoutMetricsContext();

		DispatcherLogoutMetricsContext(const DispatcherLogoutMetricsContext&) = delete;
		DispatcherLogoutMetricsContext& operator=(const DispatcherLogoutMetricsContext&) = delete;

	private:
		bool previous = false;
};

class DispatcherLoginMetricsTimer {
	public:
		DispatcherLoginMetricsTimer();
		~DispatcherLoginMetricsTimer();

		DispatcherLoginMetricsTimer(const DispatcherLoginMetricsTimer&) = delete;
		DispatcherLoginMetricsTimer& operator=(const DispatcherLoginMetricsTimer&) = delete;

		void markSuccessful() {
			successful = true;
		}

	private:
		std::chrono::steady_clock::time_point startedAt;
		bool enabled = false;
		bool successful = false;
};

#endif
