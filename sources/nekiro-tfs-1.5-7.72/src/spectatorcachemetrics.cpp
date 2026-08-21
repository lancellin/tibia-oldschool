#include "otpch.h"

#include "spectatorcachemetrics.h"

#include "configmanager.h"
#include "tools.h"

#include <fmt/format.h>

extern ConfigManager g_config;

SpectatorCacheMetrics g_spectatorCacheMetrics;

static constexpr int64_t EMIT_INTERVAL_MS = 10000;

void SpectatorCacheMetrics::loadConfig()
{
	metricsEnabled = g_config.getBoolean(ConfigManager::SPECTATOR_CACHE_METRICS);
	regional = g_config.getBoolean(ConfigManager::SPECTATOR_CACHE_REGIONAL_INVALIDATION);
	shadowValidate = g_config.getBoolean(ConfigManager::SPECTATOR_CACHE_SHADOW_VALIDATE);
	variantLabel = regional ? "0.2.7.y" : "0.2.7.x";
	initialized = true;

	if (metricsEnabled) {
		std::cout << "[SpectatorCacheMetrics] enabled, variant=" << variantLabel
		          << (shadowValidate ? ", shadow validation ON" : "") << std::endl;
	}
}

void SpectatorCacheMetrics::countCall(bool cacheableShape)
{
	if (!metricsEnabled) {
		return;
	}
	++calls;
	if (cacheableShape) {
		++cacheable;
	}
}

void SpectatorCacheMetrics::countHit()
{
	if (!metricsEnabled) {
		return;
	}
	++hits;
}

void SpectatorCacheMetrics::countMiss()
{
	if (!metricsEnabled) {
		return;
	}
	++misses;
}

void SpectatorCacheMetrics::addScanTimeNs(uint64_t nanoseconds)
{
	if (!metricsEnabled) {
		return;
	}
	scanTimeNs += nanoseconds;
}

void SpectatorCacheMetrics::countInvalidation(uint64_t removed, uint64_t survived)
{
	if (!metricsEnabled) {
		return;
	}
	++invalidations;
	entriesRemoved += removed;
	entriesSurvived += survived;
}

void SpectatorCacheMetrics::countShadowCheck()
{
	if (!metricsEnabled) {
		return;
	}
	++shadowChecks;
}

void SpectatorCacheMetrics::countShadowMismatch(const Position& centerPos)
{
	if (!metricsEnabled) {
		return;
	}
	++shadowMismatches;
	std::cout << "[SpectatorCacheMetrics] SHADOW MISMATCH at ("
	          << centerPos.getX() << ", " << centerPos.getY() << ", "
	          << static_cast<int32_t>(centerPos.getZ()) << ")" << std::endl;
}

void SpectatorCacheMetrics::noteCacheSize(uint64_t totalEntries)
{
	if (!metricsEnabled) {
		return;
	}
	if (totalEntries > peakCacheSize) {
		peakCacheSize = totalEntries;
	}
}

void SpectatorCacheMetrics::maybeEmit()
{
	if (!metricsEnabled || !initialized) {
		return;
	}
	const int64_t now = OTSYS_TIME();
	if (windowStartMs == 0) {
		windowStartMs = now;
		return;
	}
	if (now - windowStartMs >= EMIT_INTERVAL_MS) {
		emit();
	}
}

void SpectatorCacheMetrics::openFile()
{
	if (file) {
		return;
	}
	const std::string path = fmt::format("spectator_cache_metrics_{:s}.csv", variantLabel);
	file = fopen(path.c_str(), "a");
	if (!file) {
		std::cout << "[SpectatorCacheMetrics] failed to open " << path << std::endl;
		return;
	}
	if (ftell(file) == 0) {
		fmt::print(file, "window_start,calls,cacheable,hits,misses,scans_ms,invalidations,entries_removed,entries_survived,peak_cache_size,shadow_checks,shadow_mismatches\n");
	}
}

void SpectatorCacheMetrics::emit()
{
	openFile();
	if (file) {
		fmt::print(file, "{:d},{:d},{:d},{:d},{:d},{:.3f},{:d},{:d},{:d},{:d},{:d},{:d}\n",
			windowStartMs, calls, cacheable, hits, misses,
			static_cast<double>(scanTimeNs) / 1000000.0,
			invalidations, entriesRemoved, entriesSurvived, peakCacheSize,
			shadowChecks, shadowMismatches);
		fflush(file);
	} else {
		std::cout << "[SpectatorCacheMetrics] window calls=" << calls << " hits=" << hits
		          << " misses=" << misses << " scans=" << (scanTimeNs / 1000000) << "ms invalidations="
		          << invalidations << " removed=" << entriesRemoved << " survived=" << entriesSurvived
		          << " peakCache=" << peakCacheSize << " shadowChecks=" << shadowChecks
		          << " MISMATCHES=" << shadowMismatches << std::endl;
	}

	windowStartMs = OTSYS_TIME();
	calls = 0;
	cacheable = 0;
	hits = 0;
	misses = 0;
	scanTimeNs = 0;
	invalidations = 0;
	entriesRemoved = 0;
	entriesSurvived = 0;
	peakCacheSize = 0;
	shadowChecks = 0;
	shadowMismatches = 0;
}
