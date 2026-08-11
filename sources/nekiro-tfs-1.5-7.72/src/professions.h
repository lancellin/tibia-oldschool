/**
 * The Forgotten Server - Professions domain
 */

#ifndef FS_PROFESSIONS_H_9D0E967785944B1F8D15EE86A7D2A847
#define FS_PROFESSIONS_H_9D0E967785944B1F8D15EE86A7D2A847

#include <cstdint>

namespace professions {

inline constexpr uint32_t ALCHEMY_INITIAL_LEVEL = 10;
inline constexpr uint64_t ALCHEMY_INITIAL_TRIES = 0;

struct Progress {
	uint32_t level = ALCHEMY_INITIAL_LEVEL;
	uint64_t tries = ALCHEMY_INITIAL_TRIES;
};

uint64_t getAlchemyRequiredTries(uint32_t level);
uint8_t getAlchemyPercent(const Progress& progress);
bool addAlchemyTries(Progress& progress, uint64_t count);
void sanitizeAlchemyProgress(Progress& progress);

} // namespace professions

#endif
