/**
 * Elite Creatures - a new server system for TFS 1.5 (7.72 downgrade).
 *
 * A regular monster death has a small, mutually exclusive chance of
 * summoning an elite variant of the same species after a 10 second
 * portal delay. Elites keep the base MonsterType and receive stat
 * multipliers (health, damage, speed, experience, loot chance) plus
 * special Evolution Crystal drops.
 */

#ifndef FS_ELITECREATURES_9C1F2B7A6D404E8C8F2E1C7B5A9D0E31
#define FS_ELITECREATURES_9C1F2B7A6D404E8C8F2E1C7B5A9D0E31

#include <cstdint>

enum class EliteTier : uint8_t {
	None = 0,
	One = 1,
	Two = 2,
	Three = 3,
};

namespace EliteCreatures {
	// A single death rolls once against the whole 0.75% budget:
	// Tier 1 = 0.5%, Tier 2 = 0.2%, Tier 3 = 0.05% (mutually exclusive).
	constexpr uint32_t ROLL_BASE = 100000;
	constexpr uint32_t CHANCE_TIER_1 = 500; // 0.5%
	constexpr uint32_t CHANCE_TIER_2 = 200; // 0.2%
	constexpr uint32_t CHANCE_TIER_3 = 50; // 0.05%

	// Portal items shown on the corpse while the elite is being summoned.
	// Tier 1 reuses the vanilla magic forcefield; tiers 2/3 use dedicated
	// animated portals (onTop in the DAT, above corpses and creatures).
	constexpr uint16_t PORTAL_TIER1_ID = 1387; // magic forcefield (blue)
	constexpr uint16_t PORTAL_TIER2_ID = 26402; // lightning portal
	constexpr uint16_t PORTAL_TIER3_ID = 26403; // infernal portal

	constexpr uint16_t portalItemId(EliteTier tier)
	{
		switch (tier) {
			case EliteTier::Two:
				return PORTAL_TIER2_ID;
			case EliteTier::Three:
				return PORTAL_TIER3_ID;
			default:
				return PORTAL_TIER1_ID;
		}
	}
	constexpr uint32_t PORTAL_DURATION_MS = 10000;
	// The portal ItemType has no decay configured, so the duration attribute
	// alone never removes it; a second scheduler task at this delay acts as
	// the cleanup safety net for a leftover portal.
	constexpr uint32_t PORTAL_DECAY_MS = PORTAL_DURATION_MS + 5000;

	// An elite nobody kills simply vanishes after this delay, so a failed
	// fight never leaves an overpowered monster roaming the map.
	constexpr uint32_t DESPAWN_TIER12_MS = 15 * 60 * 1000;
	constexpr uint32_t DESPAWN_TIER3_MS = 20 * 60 * 1000;

	constexpr uint32_t despawnDelay(EliteTier tier)
	{
		return tier == EliteTier::Three ? DESPAWN_TIER3_MS : DESPAWN_TIER12_MS;
	}

	// Evolution crystals dropped by elites (see tools evolution_crystal.lua).
	constexpr uint16_t SPARK_CRYSTAL_ID = 26399;
	constexpr uint16_t LIGHTNING_CRYSTAL_ID = 26400;
	constexpr uint16_t INFERNAL_CRYSTAL_ID = 26401;

	// Crystal drops only apply to creatures with at least this base HP.
	constexpr int32_t CRYSTAL_MIN_BASE_HP = 200;

	// Test hook (GM tooling): while forcedTier is non-zero, roll() returns
	// that tier for the next forcedTierRolls rolls, making elite spawns
	// deterministic (both values reset to zero when the budget is spent).
	extern uint8_t forcedTier;
	extern uint32_t forcedTierRolls;

	// Single mutually exclusive roll: returns None or exactly one tier.
	EliteTier roll();

	double healthMultiplier(EliteTier tier);
	double damageMultiplier(EliteTier tier);
	double speedMultiplier(EliteTier tier);
	double lootChanceMultiplier(EliteTier tier);
	uint32_t experienceMultiplier(EliteTier tier);
	uint32_t bestiaryKillMultiplier(EliteTier tier);
}

#endif
