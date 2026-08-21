#ifndef FS_SPECTATORCACHEMETRICS_H_
#define FS_SPECTATORCACHEMETRICS_H_

#include "position.h"

#include <cstdint>
#include <cstdio>
#include <string>

// A/B instrumentation for the spectator cache (audit item A2).
// Entirely dispatcher-thread bound; counters are plain values.
// Output goes to its own CSV file, separate from the dispatcher/autosend
// metrics, one line per 10-second window.
class SpectatorCacheMetrics
{
	public:
		// Reads the three config flags; call once after ConfigManager::load.
		void loadConfig();

		bool enabled() const {
			return metricsEnabled;
		}
		bool regionalInvalidation() const {
			return regional;
		}
		bool shadowValidation() const {
			return shadowValidate;
		}

		// Variant label written into the CSV file name:
		//   "0.2.7.x" = legacy global-clear invalidation
		//   "0.2.7.y" = regional invalidation (current behavior)
		const std::string& variant() const {
			return variantLabel;
		}

		void countCall(bool cacheableShape);
		void countHit();
		void countMiss();
		void addScanTimeNs(uint64_t nanoseconds);
		void countInvalidation(uint64_t entriesRemoved, uint64_t entriesSurvived);
		void countShadowCheck();
		void countShadowMismatch(const Position& centerPos);
		void noteCacheSize(uint64_t totalEntries);

		// Writes and resets the window when 10 s have elapsed.
		void maybeEmit();

	private:
		void emit();
		void openFile();

		std::FILE* file = nullptr;
		std::string variantLabel = "0.2.7.y";

		bool initialized = false;
		bool metricsEnabled = false;
		bool regional = true;
		bool shadowValidate = false;

		int64_t windowStartMs = 0;
		uint64_t calls = 0;
		uint64_t cacheable = 0;
		uint64_t hits = 0;
		uint64_t misses = 0;
		uint64_t scanTimeNs = 0;
		uint64_t invalidations = 0;
		uint64_t entriesRemoved = 0;
		uint64_t entriesSurvived = 0;
		uint64_t peakCacheSize = 0;
		uint64_t shadowChecks = 0;
		uint64_t shadowMismatches = 0;
};

extern SpectatorCacheMetrics g_spectatorCacheMetrics;

#endif
