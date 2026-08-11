/**
 * The Forgotten Server - Professions domain
 */

#include "professions.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace professions {

namespace {
constexpr double ALCHEMY_SKILL_BASE = 50.0;
constexpr double ALCHEMY_SKILL_MULTIPLIER = 1.1;
constexpr double ALCHEMY_SKILL_RATE = 2.5;
}

uint64_t getAlchemyRequiredTries(uint32_t level)
{
	const uint32_t normalizedLevel = std::max(level, ALCHEMY_INITIAL_LEVEL);
	const double required =
		(ALCHEMY_SKILL_BASE *
			std::pow(ALCHEMY_SKILL_MULTIPLIER,
				static_cast<double>(normalizedLevel - ALCHEMY_INITIAL_LEVEL))) /
		ALCHEMY_SKILL_RATE;
	const uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
	if (!std::isfinite(required) || required >= static_cast<double>(maximum)) {
		return maximum;
	}
	return std::max<uint64_t>(1, static_cast<uint64_t>(required));
}

uint8_t getAlchemyPercent(const Progress& progress)
{
	const uint64_t required = getAlchemyRequiredTries(progress.level);
	if (required == 0 || progress.tries >= required) {
		return 0;
	}

	const long double percent =
		(static_cast<long double>(progress.tries) * 100.0L) /
		static_cast<long double>(required);
	return static_cast<uint8_t>(std::min<long double>(99.0L, percent));
}

bool addAlchemyTries(Progress& progress, uint64_t count)
{
	if (count == 0) {
		return false;
	}

	sanitizeAlchemyProgress(progress);
	while (count > 0) {
		const uint64_t required = getAlchemyRequiredTries(progress.level);
		const uint64_t remaining = required - progress.tries;
		if (count < remaining) {
			progress.tries += count;
			break;
		}

		count -= remaining;
		progress.tries = 0;
		if (progress.level == (std::numeric_limits<uint32_t>::max)()) {
			break;
		}
		++progress.level;
	}
	return true;
}

void sanitizeAlchemyProgress(Progress& progress)
{
	progress.level = std::max(progress.level, ALCHEMY_INITIAL_LEVEL);
	if (progress.tries >= getAlchemyRequiredTries(progress.level)) {
		progress.tries = ALCHEMY_INITIAL_TRIES;
	}
}

} // namespace professions
