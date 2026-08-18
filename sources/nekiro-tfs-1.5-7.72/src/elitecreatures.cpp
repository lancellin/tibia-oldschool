/**
 * Elite Creatures - see elitecreatures.h for the system overview.
 */

#include "otpch.h"

#include "elitecreatures.h"

#include "tools.h"

namespace EliteCreatures {

uint8_t forcedTier = 0;
uint32_t forcedTierRolls = 0;

EliteTier roll()
{
	// Deterministic test hook (see forcedTier in elitecreatures.h).
	if (forcedTier > 0 && forcedTierRolls > 0) {
		const EliteTier tier = static_cast<EliteTier>(std::min<uint8_t>(forcedTier, 3));
		if (--forcedTierRolls == 0) {
			forcedTier = 0;
		}
		return tier;
	}

	// Single roll over the combined 0.75% budget so the total elite chance
	// never exceeds 0.5% + 0.2% + 0.05% and at most one tier is produced.
	const uint32_t value = uniform_random(1, ROLL_BASE);
	if (value <= CHANCE_TIER_1) {
		return EliteTier::One;
	}
	if (value <= CHANCE_TIER_1 + CHANCE_TIER_2) {
		return EliteTier::Two;
	}
	if (value <= CHANCE_TIER_1 + CHANCE_TIER_2 + CHANCE_TIER_3) {
		return EliteTier::Three;
	}
	return EliteTier::None;
}

double healthMultiplier(EliteTier tier)
{
	switch (tier) {
		case EliteTier::One:
			return 1.50;
		case EliteTier::Two:
			return 2.00;
		case EliteTier::Three:
			return 6.00;
		default:
			return 1.00;
	}
}

double damageMultiplier(EliteTier tier)
{
	switch (tier) {
		case EliteTier::One:
			return 1.20;
		case EliteTier::Two:
			return 1.50;
		case EliteTier::Three:
			return 2.00;
		default:
			return 1.00;
	}
}

double speedMultiplier(EliteTier tier)
{
	switch (tier) {
		case EliteTier::One:
			return 1.25;
		case EliteTier::Two:
			return 1.50;
		case EliteTier::Three:
			return 2.00;
		default:
			return 1.00;
	}
}

double lootChanceMultiplier(EliteTier tier)
{
	switch (tier) {
		case EliteTier::One:
			return 1.25;
		case EliteTier::Two:
			return 1.50;
		case EliteTier::Three:
			// Kept in sync with ELITE_LOOT_CHANCE_MULTIPLIER in
			// default_onDropLoot.lua (the table Lua actually applies).
			return 4.00;
		default:
			return 1.00;
	}
}

uint32_t experienceMultiplier(EliteTier tier)
{
	switch (tier) {
		case EliteTier::One:
			return 3;
		case EliteTier::Two:
			return 5;
		case EliteTier::Three:
			return 10;
		default:
			return 1;
	}
}

uint32_t bestiaryKillMultiplier(EliteTier tier)
{
	// Elite kills count 3x/5x/10x towards the base creature's bestiary.
	return experienceMultiplier(tier);
}

}
