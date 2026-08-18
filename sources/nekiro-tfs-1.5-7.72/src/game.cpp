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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include "pugicast.h"

#include "actions.h"
#include "bed.h"
#include "checkpointworker.h"
#include "configmanager.h"
#include "creature.h"
#include "creatureevent.h"
#include "databasetasks.h"
#include "dispatchermetrics.h"
#include "events.h"
#include "game.h"
#include "globalevent.h"
#include "housetile.h"
#include "iologindata.h"
#include "iomapserialize.h"
#include "iomarket.h"
#include "items.h"
#include "mailbox.h"
#include "monster.h"
#include "movement.h"
#include "playeriodatabase.h"
#include "scheduler.h"
#include "server.h"
#include "spells.h"
#include "talkaction.h"
#include "tools.h"
#include "weapons.h"
#include "script.h"
#include "playershop.h"
#include "playeriomanager.h"

#include <fmt/format.h>

#include <array>

extern ConfigManager g_config;
extern Actions* g_actions;
extern Chat* g_chat;
extern TalkActions* g_talkActions;
extern Spells* g_spells;
extern Vocations g_vocations;
extern GlobalEvents* g_globalEvents;
extern CreatureEvents* g_creatureEvents;
extern Events* g_events;
extern Monsters g_monsters;
extern MoveEvents* g_moveEvents;
extern Weapons* g_weapons;
extern Scripts* g_scripts;

static constexpr uint32_t RUNE_EXHAUST_TOLERANCE = 70;
// Upper bound for how long a synchronous checkpoint/save path may block the
// Dispatcher waiting for in-flight background checkpoint jobs to commit.
static constexpr uint32_t CHECKPOINT_SYNC_DRAIN_TIMEOUT_MS = 30000;
// A checkpoint group that keeps failing past this many attempts is considered
// stuck (e.g. a participant that can never be saved again) and escalates to a
// loud [ALERT] log; detection/telemetry only, retry behavior is unchanged.
static constexpr uint32_t CHECKPOINT_STUCK_ALERT_RETRY_THRESHOLD = 3;

namespace {
	constexpr uint8_t BESTIARY_UNLOCK_EXTENDED_OPCODE = 9;
	constexpr int32_t PLAYER_CORPSE_CRASH_RECOVERY_DECAY_BONUS_MS = 50 * 60 * 1000;

	const char* checkpointGroupFailureKindLabel(CheckpointGroupFailureKind kind)
	{
		switch (kind) {
			case CheckpointGroupFailureKind::PARTICIPANT_UNAVAILABLE: return "participant unavailable";
			case CheckpointGroupFailureKind::HOUSE_UNAVAILABLE: return "house unavailable";
			case CheckpointGroupFailureKind::CAPTURE_FAILED: return "save capture failed";
			case CheckpointGroupFailureKind::SERIALIZATION: return "tile serialization failed";
			case CheckpointGroupFailureKind::TRANSACTION: return "transaction failed";
			case CheckpointGroupFailureKind::WORKER: return "worker execution failed";
			case CheckpointGroupFailureKind::WORKER_ABORTED: return "worker died before execution";
			default: return "unknown";
		}
	}

	bool isCreatureStack(const Item* item)
	{
		if (!item || item->getID() != ITEM_GOLD_COIN) {
			return false;
		}

		const ItemAttributes::CustomAttribute* attribute =
			item->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_CREATURE_STACK);
		if (!attribute) {
			return false;
		}

		const bool* value = boost::get<bool>(&attribute->value);
		return value && *value;
	}

	void clearCreatureStackAfterMixedMerge(Item* destination, bool sourceCreatureStack,
			bool destinationCreatureStack, uint32_t mergedCount)
	{
		if (!destination || mergedCount == 0 ||
				sourceCreatureStack == destinationCreatureStack) {
			return;
		}

		if (destination->removeCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_CREATURE_STACK)) {
			destination->markFloorPersistenceAttributeDirty();
		}
	}

	struct FloorRecoveryDecayState {
		std::vector<Item*> heldItems;
		uint64_t resumedItemCount = 0;
		uint64_t extendedPlayerCorpseCount = 0;
		uint64_t removedHeldItemCount = 0;
	};

	FloorRecoveryDecayState floorRecoveryDecayState;

	constexpr uint16_t calculateSpawnPlayerBucket(size_t playersOnline)
	{
		constexpr size_t playerCap = 600;
		constexpr size_t playerStep = 50;
		const size_t cappedOnline = playersOnline < playerCap ? playersOnline : playerCap;
		return static_cast<uint16_t>((cappedOnline / playerStep) * playerStep);
	}

	static_assert(calculateSpawnPlayerBucket(0) == 0);
	static_assert(calculateSpawnPlayerBucket(49) == 0);
	static_assert(calculateSpawnPlayerBucket(50) == 50);
	static_assert(calculateSpawnPlayerBucket(599) == 550);
	static_assert(calculateSpawnPlayerBucket(600) == 600);
	static_assert(calculateSpawnPlayerBucket(601) == 600);

	class FloorDirtyPlayerMutationScope final
	{
		public:
			FloorDirtyPlayerMutationScope(Game& game, bool active) : game(game), active(active) {
				if (active) {
					game.beginFloorDirtyPlayerMutation();
				}
			}

			~FloorDirtyPlayerMutationScope() {
				if (active) {
					game.endFloorDirtyPlayerMutation();
				}
			}

		private:
			Game& game;
			bool active;
	};

	class ItemMovePersistenceMetricsScope final
	{
		public:
			ItemMovePersistenceMetricsScope() : enabled(dispatcherMetricsEnabled()) {}

			~ItemMovePersistenceMetricsScope() {
				if (enabled && accumulatedNanoseconds != 0) {
					recordDispatcherPhase(DispatcherMetricsPhase::ITEM_MOVE_PERSISTENCE,
						accumulatedNanoseconds);
				}
			}

			void begin() {
				if (enabled) {
					segmentStartedAt = std::chrono::steady_clock::now();
				}
			}

			void end() {
				if (enabled) {
					accumulatedNanoseconds += static_cast<uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - segmentStartedAt).count());
				}
			}

		private:
			bool enabled = false;
			uint64_t accumulatedNanoseconds = 0;
			std::chrono::steady_clock::time_point segmentStartedAt;
	};

	struct FloorCheckpointEndpoint {
		Player* player = nullptr;
		Tile* tile = nullptr;
		House* house = nullptr;
	};

	FloorCheckpointEndpoint resolveFloorCheckpointEndpoint(Cylinder* cylinder, Player* actorPlayer)
	{
		FloorCheckpointEndpoint endpoint;
		for (Cylinder* current = cylinder; current; current = current->getParent()) {
			if (Creature* creature = current->getCreature()) {
				endpoint.player = creature->getPlayer();
				return endpoint;
			}

			// A player's virtual depot locker is parented to the physical depot
			// tile while it is open. Stop at the virtual locker so its contents
			// remain player storage rather than being classified as floor state.
			// A loose item dropped on top of the depot still reaches this function
			// as a Tile cylinder and is intentionally treated as normal floor state.
			Item* currentItem = current->getItem();
			Container* currentContainer = currentItem ? currentItem->getContainer() : nullptr;
			if (currentContainer && currentContainer->getDepotLocker()) {
				endpoint.player = actorPlayer;
				return endpoint;
			}

			if (Tile* tile = dynamic_cast<Tile*>(current)) {
				if (HouseTile* houseTile = dynamic_cast<HouseTile*>(tile)) {
					endpoint.house = houseTile->getHouse();
				} else {
					endpoint.tile = tile;
				}
				return endpoint;
			}
		}
		return endpoint;
	}
}

static bool usesRuneActionExhaust(const Item* item)
{
	if (!item) {
		return false;
	}

	const ItemType& it = Item::items[item->getID()];
	return it.isRune() && g_spells->getRuneSpell(item->getID());
}

static bool canQueueShortRuneRetry(const Item* item, uint32_t delay)
{
	if (!usesRuneActionExhaust(item) || delay > RUNE_EXHAUST_TOLERANCE) {
		return false;
	}

	if (RuneSpell* runeSpell = g_spells->getRuneSpell(item->getID())) {
		if (runeSpell->getAggressive()) {
			return true;
		}
	}

	switch (item->getID()) {
		case 2265: // Intense Healing Rune
		case 2273: // Ultimate Healing Rune
			return true;
		default:
			return false;
	}
}

Game::Game()
{
	offlineTrainingWindow.defaultEnterButton = 1;
	offlineTrainingWindow.defaultEscapeButton = 0;
	offlineTrainingWindow.choices.emplace_back("Sword Fighting and Shielding", SKILL_SWORD);
	offlineTrainingWindow.choices.emplace_back("Axe Fighting and Shielding", SKILL_AXE);
	offlineTrainingWindow.choices.emplace_back("Club Fighting and Shielding", SKILL_CLUB);
	offlineTrainingWindow.choices.emplace_back("Distance Fighting and Shielding", SKILL_DISTANCE);
	offlineTrainingWindow.choices.emplace_back("Magic Level and Shielding", SKILL_MAGLEVEL);
	offlineTrainingWindow.buttons.emplace_back("Okay", offlineTrainingWindow.defaultEnterButton);
	offlineTrainingWindow.buttons.emplace_back("Cancel", offlineTrainingWindow.defaultEscapeButton);
	offlineTrainingWindow.priority = true;
}

Game::~Game()
{
	for (const auto& it : guilds) {
		delete it.second;
	}
}

void Game::setFloorPersistenceCityPosition(const Position& position, bool excluded)
{
	if (excluded) {
		floorPersistenceCityPositions.insert(position);
	} else {
		floorPersistenceCityPositions.erase(position);
	}
}

void Game::clearFloorPersistenceCityPositions()
{
	floorPersistenceCityPositions.clear();
}

bool Game::isFloorPersistenceCityPosition(const Position& position) const
{
	return floorPersistenceCityPositions.find(position) != floorPersistenceCityPositions.end();
}

void Game::markFloorTileDirty(const Tile& tile, FloorDirtyReason_t reason, FloorDirtyOrigin_t origin)
{
	if (!floorDirtyTrackingEnabled || reason == FLOOR_DIRTY_NONE) {
		return;
	}

	FloorDirtyOrigin_t effectiveOrigin = origin;
	if (effectiveOrigin == FLOOR_DIRTY_ORIGIN_SYSTEM && floorDirtyPlayerMutationDepth > 0) {
		effectiveOrigin = FLOOR_DIRTY_ORIGIN_PLAYER_MOVE;
	}

	if (effectiveOrigin == FLOOR_DIRTY_ORIGIN_SYSTEM) {
		++floorDirtyIgnoredSystemEvents;
		return;
	}

	if (dynamic_cast<const HouseTile*>(&tile)) {
		return;
	}

	const Position& position = tile.getPosition();
	const int64_t modifiedAt = static_cast<int64_t>(time(nullptr));
	const int64_t modifiedMonotonic = OTSYS_TIME();
	const uint64_t wallClockVersion = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
	floorSnapshotVersionClock = std::max(floorSnapshotVersionClock + 1, wallClockVersion);
	const uint64_t sequence = ++floorDirtySequence;
	++floorDirtyTotalEvents;

	FloorDirtyTileRecord& record = floorDirtyTiles[position];
	if (record.eventCount == 0) {
		record.firstSequence = sequence;
		record.firstModifiedAt = modifiedAt;
		record.firstModifiedMonotonic = modifiedMonotonic;
	}

	record.lastSequence = sequence;
	record.tileVersion = floorSnapshotVersionClock;
	record.lastModifiedAt = modifiedAt;
	record.lastModifiedMonotonic = modifiedMonotonic;
	++record.eventCount;
	record.reasonMask |= static_cast<uint32_t>(reason);
	record.lastReason = reason;
	record.originMask |= static_cast<uint32_t>(effectiveOrigin);
	record.lastOrigin = effectiveOrigin;

	auto groupIt = floorCheckpointTileGroups.find(position);
	if (groupIt != floorCheckpointTileGroups.end()) {
		auto inFlightGroupIt = floorCheckpointGroups.find(groupIt->second);
		if (inFlightGroupIt == floorCheckpointGroups.end() || !inFlightGroupIt->second.workerInFlight) {
			touchFloorCheckpointGroup(groupIt->second);
		}
	}

	recordDispatcherFloorDirtyEvent(floorDirtyTiles.size());
}

bool Game::hasFloorCheckpointForPlayer(uint32_t playerGuid) const
{
	return floorSnapshotShadowEnabled && floorCheckpointPlayerGroups.find(playerGuid) != floorCheckpointPlayerGroups.end();
}

void Game::touchFloorCheckpointGroup(uint64_t groupId)
{
	auto groupIt = floorCheckpointGroups.find(groupId);
	if (groupIt == floorCheckpointGroups.end()) {
		return;
	}

	FloorCheckpointGroup& group = groupIt->second;
	const int64_t now = OTSYS_TIME();
	if (group.firstModifiedMonotonic == 0) {
		group.firstModifiedMonotonic = now;
	}
	group.lastModifiedMonotonic = now;
	++group.version;
}

uint64_t Game::mergeFloorCheckpointGroups(const std::set<uint64_t>& groupIds)
{
	uint64_t targetId = 0;
	for (uint64_t groupId : groupIds) {
		auto it = floorCheckpointGroups.find(groupId);
		if (it != floorCheckpointGroups.end() && !it->second.workerInFlight) {
			targetId = groupId;
			break;
		}
	}

	if (targetId == 0) {
		targetId = ++floorCheckpointGroupClock;
		FloorCheckpointGroup group;
		group.id = targetId;
		group.version = 1;
		group.firstModifiedMonotonic = OTSYS_TIME();
		group.lastModifiedMonotonic = group.firstModifiedMonotonic;
		floorCheckpointGroups.emplace(targetId, std::move(group));
		++floorSnapshotStats.checkpointGroupsCreated;
	}

	FloorCheckpointGroup& target = floorCheckpointGroups[targetId];
	for (uint64_t groupId : groupIds) {
		if (groupId == targetId) {
			continue;
		}

		auto sourceIt = floorCheckpointGroups.find(groupId);
		if (sourceIt == floorCheckpointGroups.end()) {
			continue;
		}
		if (sourceIt->second.workerInFlight) {
			// An in-flight background job owns this group; it must not be merged.
			continue;
		}

		FloorCheckpointGroup& source = sourceIt->second;
		target.positions.insert(source.positions.begin(), source.positions.end());
		target.playerGuids.insert(source.playerGuids.begin(), source.playerGuids.end());
		target.houseIds.insert(source.houseIds.begin(), source.houseIds.end());
		target.itemInstanceIds.insert(source.itemInstanceIds.begin(), source.itemInstanceIds.end());
		if (target.firstModifiedMonotonic == 0 || (source.firstModifiedMonotonic != 0 &&
		    source.firstModifiedMonotonic < target.firstModifiedMonotonic)) {
			target.firstModifiedMonotonic = source.firstModifiedMonotonic;
		}
		target.lastModifiedMonotonic = std::max(target.lastModifiedMonotonic, source.lastModifiedMonotonic);
		target.version = std::max(target.version, source.version) + 1;
		floorCheckpointGroups.erase(sourceIt);
		++floorSnapshotStats.checkpointGroupsMerged;
	}

	for (const Position& position : target.positions) {
		floorCheckpointTileGroups[position] = targetId;
	}
	for (uint32_t playerGuid : target.playerGuids) {
		floorCheckpointPlayerGroups[playerGuid] = targetId;
	}
	for (uint32_t houseId : target.houseIds) {
		floorCheckpointHouseGroups[houseId] = targetId;
	}
	for (const std::string& instanceId : target.itemInstanceIds) {
		floorCheckpointItemGroups[instanceId] = targetId;
	}
	return targetId;
}

void Game::identifyFloorPersistenceMovableContainerAfterPlayerMutation(Cylinder* cylinder,
	Player* actorPlayer)
{
	DispatcherPhaseMetricsTimer identifyTimer(DispatcherMetricsPhase::ITEM_MOVE_IDENTIFY);

	if (!cylinder || !actorPlayer || !isFloorDirtyPlayerMutationActive()) {
		return;
	}

	const FloorCheckpointEndpoint endpoint = resolveFloorCheckpointEndpoint(cylinder, actorPlayer);
	if (!endpoint.player && !endpoint.tile) {
		// House floors and unsupported roots keep their independent persistence.
		return;
	}

	Item* outermostMovableContainer = nullptr;
	for (Cylinder* current = cylinder; current; current = current->getParent()) {
		Item* currentItem = current->getItem();
		Container* currentContainer = currentItem ? currentItem->getContainer() : nullptr;
		if (!currentContainer) {
			continue;
		}
		if (currentContainer->getDepotLocker()) {
			break;
		}
		if (currentItem->isFloorPersistenceCreatureCorpse()) {
			// Changes inside a creature corpse can identify the item that was
			// actually moved, but must never identify or persist the corpse
			// wrapper and the items that remain inside it.
			currentContainer->setFloorPersistenceIdentifiedSubtree(true);
			return;
		}

		if (!currentItem->isMoveable() || currentItem->isStackable() ||
		    currentItem->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
			// A fixed player-storage wrapper (DepotChest/DepotLocker) is only a
			// boundary. A fixed floor container remains deliberately unsupported
			// until its independent persistence policy is implemented.
			if (endpoint.tile) {
				return;
			}
			break;
		}

		outermostMovableContainer = currentItem;
	}

	if (!outermostMovableContainer) {
		return;
	}

	Container* outermostContainer = outermostMovableContainer->getContainer();
	if (!outermostContainer) {
		return;
	}

	// Player mutations identify the moved subtree before insertion
	// (markAsPlayerMovedForFloorPersistence runs on the moved item prior to
	// addThing in every insertion path), and removals cannot introduce
	// unidentified items. A subtree proof that was valid before the mutation
	// therefore stays valid, so it must not be invalidated here: doing so
	// forced a full re-identification scan of the whole movable container on
	// every single move. markAsPlayerMovedForFloorPersistence() early-exits
	// while the proof holds and otherwise performs the full identification
	// scan and rebuilds the proof exactly as before.
	outermostMovableContainer->markAsPlayerMovedForFloorPersistence();
	// Do not stamp the investigative actor here. The independent actor
	// attribution pass must observe the previous GUID/consistency proof so it
	// can decide whether the complete subtree needs its debounced normalization.
}

void Game::stampFloorPersistenceActorAfterPlayerMutation(Cylinder* cylinder, Item* item,
	Player* actorPlayer)
{
	DispatcherPhaseMetricsTimer stampTimer(DispatcherMetricsPhase::ITEM_MOVE_STAMP);

	if (!cylinder || !item || !actorPlayer || !isFloorDirtyPlayerMutationActive()) {
		return;
	}
	if (item->isFloorPersistenceCreatureCorpse()) {
		return;
	}

	const FloorCheckpointEndpoint endpoint = resolveFloorCheckpointEndpoint(cylinder, actorPlayer);
	if (!endpoint.tile) {
		// Player storage, depot lockers, houses and unsupported roots do not need
		// floor-stack attribution.
		return;
	}

	for (Cylinder* current = cylinder; current; current = current->getParent()) {
		Item* currentItem = current->getItem();
		Container* currentContainer = currentItem ? currentItem->getContainer() : nullptr;
		if (!currentContainer) {
			continue;
		}
		if (currentContainer->getDepotLocker() || currentItem->isFloorPersistenceCreatureCorpse()) {
			return;
		}
		if (currentItem->isMoveable() && !currentItem->isStackable() &&
		    !currentItem->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
			// The outermost movable container is stamped by
			// identifyFloorPersistenceMovableContainerAfterPlayerMutation().
			return;
		}
		return;
	}

	if (item->isStackable()) {
		item->setFloorPersistenceLastActorGuid(actorPlayer->getGUID());
	}
}

bool Game::isInsideCreatureCorpse(Cylinder* cylinder) const
{
	for (Cylinder* current = cylinder; current; current = current->getParent()) {
		Item* currentItem = current->getItem();
		if (currentItem && currentItem->isFloorPersistenceCreatureCorpse()) {
			return true;
		}
	}
	return false;
}

Player* Game::findPlayerStorageOwner(Cylinder* cylinder) const
{
	for (Cylinder* current = cylinder; current; current = current->getParent()) {
		if (Creature* creature = current->getCreature()) {
			return creature->getPlayer();
		}

		Item* currentItem = current->getItem();
		Container* currentContainer = currentItem ? currentItem->getContainer() : nullptr;
		if (currentContainer && currentContainer->getDepotLocker()) {
			// Open depots are virtual player storage, but the locker itself does
			// not retain an owning Player pointer. Player moves use their actor;
			// mail supplies the explicit recipient after delivery.
			return nullptr;
		}
	}
	return nullptr;
}

Item* Game::findOutermostMovableActorContainer(Cylinder* cylinder) const
{
	Item* outermost = nullptr;
	for (Cylinder* current = cylinder; current; current = current->getParent()) {
		Item* currentItem = current->getItem();
		Container* currentContainer = currentItem ? currentItem->getContainer() : nullptr;
		if (!currentContainer) {
			continue;
		}
		if (currentItem->isFloorPersistenceCreatureCorpse() ||
		    currentContainer->getDepotLocker()) {
			if (currentContainer->getDepotLocker()) {
				break;
			}
			return nullptr;
		}
		if (!currentItem->isMoveable()) {
			break;
		}
		outermost = currentItem;
	}
	return outermost;
}

void Game::queueItemActorAttribution(Item* root, uint32_t playerGuid)
{
	if (!root || playerGuid == 0 || root->isRemoved() || !root->isMoveable() ||
	    !root->getContainer() || root->isFloorPersistenceCreatureCorpse()) {
		return;
	}

	root->setFloorPersistenceLastActorGuid(playerGuid);
	root->setFloorPersistenceLastActorConsistentSubtree(false);

	const int64_t now = OTSYS_TIME();
	for (auto& entry : pendingItemActorAttributions) {
		Item* pendingRoot = entry.first;
		Container* pendingContainer = pendingRoot ? pendingRoot->getContainer() : nullptr;
		if (pendingRoot == root ||
		    (pendingContainer && pendingContainer->isHoldingItem(root))) {
			pendingRoot->setFloorPersistenceLastActorGuid(playerGuid);
			pendingRoot->setFloorPersistenceLastActorConsistentSubtree(false);
			entry.second.playerGuid = playerGuid;
			entry.second.lastModifiedMonotonic = now;
			return;
		}
	}

	std::vector<Item*> coveredPendingRoots;
	Container* rootContainer = root->getContainer();
	if (rootContainer) {
		for (const auto& entry : pendingItemActorAttributions) {
			if (rootContainer->isHoldingItem(entry.first)) {
				coveredPendingRoots.push_back(entry.first);
			}
		}
	}
	for (Item* coveredRoot : coveredPendingRoots) {
		auto coveredIt = pendingItemActorAttributions.find(coveredRoot);
		if (coveredIt != pendingItemActorAttributions.end()) {
			pendingItemActorAttributions.erase(coveredIt);
			coveredRoot->decrementReferenceCounter();
		}
	}

	auto it = pendingItemActorAttributions.find(root);
	if (it != pendingItemActorAttributions.end()) {
		it->second.playerGuid = playerGuid;
		it->second.lastModifiedMonotonic = now;
		return;
	}

	root->incrementReferenceCounter();
	PendingItemActorAttribution pending;
	pending.root = root;
	pending.playerGuid = playerGuid;
	pending.firstModifiedMonotonic = now;
	pending.lastModifiedMonotonic = now;
	pendingItemActorAttributions.emplace(root, pending);
	recordDispatcherActorAttributionQueued(pendingItemActorAttributions.size());
}

void Game::normalizeItemActorSubtree(Item* root, uint32_t playerGuid,
	bool allowVirtualStorage)
{
	if (!root || playerGuid == 0 ||
	    (!allowVirtualStorage && root->isRemoved()) ||
	    root->isFloorPersistenceCreatureCorpse()) {
		return;
	}

	std::vector<Item*> pendingItems {root};
	std::vector<Item*> visitedContainers;
	while (!pendingItems.empty()) {
		Item* current = pendingItems.back();
		pendingItems.pop_back();
		if (!current || (!allowVirtualStorage && current->isRemoved()) ||
		    current->isFloorPersistenceCreatureCorpse()) {
			continue;
		}

		if (current->isMoveable()) {
			current->setFloorPersistenceLastActorGuid(playerGuid);
		}

		Container* container = current->getContainer();
		if (!container) {
			continue;
		}
		if (current->isMoveable()) {
			visitedContainers.push_back(current);
		}
		for (Item* child : container->getItemList()) {
			pendingItems.push_back(child);
		}
	}

	for (Item* containerItem : visitedContainers) {
		containerItem->setFloorPersistenceLastActorConsistentSubtree(true);
	}
}

void Game::certifyItemActorAncestorPath(Item* item, uint32_t playerGuid)
{
	if (!item || playerGuid == 0 || item->isRemoved()) {
		return;
	}

	for (Cylinder* current = item->getParent(); current; current = current->getParent()) {
		Item* containerItem = current->getItem();
		Container* container = containerItem ? containerItem->getContainer() : nullptr;
		if (!container) {
			continue;
		}
		if (containerItem->isFloorPersistenceCreatureCorpse() ||
		    container->getDepotLocker() || !containerItem->isMoveable() ||
		    containerItem->getFloorPersistenceLastActorGuid() != playerGuid) {
			break;
		}

		bool directChildrenConsistent = true;
		for (Item* child : container->getItemList()) {
			if (!child->isMoveable() || child->isFloorPersistenceCreatureCorpse()) {
				continue;
			}
			if (child->getFloorPersistenceLastActorGuid() != playerGuid) {
				directChildrenConsistent = false;
				break;
			}
			if (child->getContainer() &&
			    !child->hasFloorPersistenceLastActorConsistentSubtree()) {
				directChildrenConsistent = false;
				break;
			}
		}

		containerItem->setFloorPersistenceLastActorConsistentSubtree(
			directChildrenConsistent);
		if (!directChildrenConsistent) {
			break;
		}
	}
}

bool Game::hasPendingItemActorAttributionWithin(Item* containerItem,
	uint32_t playerGuid, bool includeContainer) const
{
	Container* container = containerItem ? containerItem->getContainer() : nullptr;
	if (!container || playerGuid == 0) {
		return false;
	}

	for (const auto& entry : pendingItemActorAttributions) {
		if (entry.second.playerGuid != playerGuid) {
			continue;
		}
		if ((includeContainer && entry.first == containerItem) ||
		    container->isHoldingItem(entry.first)) {
			return true;
		}
	}
	return false;
}

void Game::attributeContainerPathAfterMutation(Cylinder* cylinder, uint32_t playerGuid)
{
	DispatcherPhaseMetricsTimer pathTimer(DispatcherMetricsPhase::ITEM_MOVE_ATTR_PATH);

	if (!cylinder || playerGuid == 0 || isInsideCreatureCorpse(cylinder)) {
		return;
	}

	Item* outermost = nullptr;
	bool needsNormalization = false;
	for (Cylinder* current = cylinder; current; current = current->getParent()) {
		Item* currentItem = current->getItem();
		Container* currentContainer = currentItem ? currentItem->getContainer() : nullptr;
		if (!currentContainer) {
			continue;
		}
		if (currentItem->isFloorPersistenceCreatureCorpse() ||
		    currentContainer->getDepotLocker()) {
			if (currentContainer->getDepotLocker()) {
				break;
			}
			return;
		}
		if (!currentItem->isMoveable()) {
			break;
		}

		const bool actorChanged =
			currentItem->getFloorPersistenceLastActorGuid() != playerGuid;
		const bool unresolvedWithoutCoveredChild =
			!currentItem->hasFloorPersistenceLastActorConsistentSubtree() &&
			!hasPendingItemActorAttributionWithin(
				currentItem, playerGuid, false);
		if (actorChanged || unresolvedWithoutCoveredChild) {
			needsNormalization = true;
			currentItem->setFloorPersistenceLastActorGuid(playerGuid);
			currentItem->setFloorPersistenceLastActorConsistentSubtree(false);
		}
		outermost = currentItem;
	}

	if (needsNormalization && outermost) {
		queueItemActorAttribution(outermost, playerGuid);
	}
}

void Game::attributeSuccessfulItemEndpoint(Cylinder* expectedCylinder, Item* item,
	uint32_t playerGuid)
{
	DispatcherPhaseMetricsTimer endpointTimer(DispatcherMetricsPhase::ITEM_MOVE_ATTR_ENDPOINT);

	if (!expectedCylinder || !item || playerGuid == 0 || item->isRemoved() ||
	    item->getParent() != expectedCylinder ||
	    item->isFloorPersistenceCreatureCorpse() ||
	    isInsideCreatureCorpse(item->getParent()) || !item->isMoveable()) {
		return;
	}

	Container* movedContainer = item->getContainer();
	bool movedSubtreeNeedsNormalization = movedContainer &&
		(item->getFloorPersistenceLastActorGuid() != playerGuid ||
		 !item->hasFloorPersistenceLastActorConsistentSubtree());
	item->setFloorPersistenceLastActorGuid(playerGuid);

	Item* outermost = nullptr;
	bool parentPathNeedsNormalization = false;
	if (movedSubtreeNeedsNormalization) {
		item->setFloorPersistenceLastActorConsistentSubtree(false);
	}

	for (Cylinder* current = item->getParent(); current; current = current->getParent()) {
		Item* currentItem = current->getItem();
		Container* currentContainer = currentItem ? currentItem->getContainer() : nullptr;
		if (!currentContainer) {
			continue;
		}
		if (currentItem->isFloorPersistenceCreatureCorpse() ||
		    currentContainer->getDepotLocker()) {
			if (currentContainer->getDepotLocker()) {
				break;
			}
			return;
		}
		if (!currentItem->isMoveable()) {
			break;
		}

		const bool actorChanged =
			currentItem->getFloorPersistenceLastActorGuid() != playerGuid;
		const bool unresolvedWithoutCoveredChild =
			!currentItem->hasFloorPersistenceLastActorConsistentSubtree() &&
			!hasPendingItemActorAttributionWithin(
				currentItem, playerGuid, false);
		if (actorChanged || unresolvedWithoutCoveredChild) {
			parentPathNeedsNormalization = true;
			currentItem->setFloorPersistenceLastActorGuid(playerGuid);
		}
		outermost = currentItem;
	}

	if (parentPathNeedsNormalization && outermost) {
		for (Cylinder* current = item->getParent(); current; current = current->getParent()) {
			Item* currentItem = current->getItem();
			Container* currentContainer = currentItem ? currentItem->getContainer() : nullptr;
			if (!currentContainer || !currentItem->isMoveable()) {
				continue;
			}
			if (currentItem->isFloorPersistenceCreatureCorpse() ||
			    currentContainer->getDepotLocker()) {
				break;
			}
			currentItem->setFloorPersistenceLastActorConsistentSubtree(false);
		}
		queueItemActorAttribution(outermost, playerGuid);
		return;
	}

	if (movedSubtreeNeedsNormalization) {
		if (outermost) {
			// The receiving tree is already consistent for this player. Only
			// the newly inserted container can violate that proof. Invalidate
			// the ancestor proof and debounce only the inserted subtree rather
			// than scanning every sibling in the mother bag.
			for (Cylinder* current = item->getParent(); current; current = current->getParent()) {
				Item* currentItem = current->getItem();
				Container* currentContainer = currentItem ? currentItem->getContainer() : nullptr;
				if (!currentContainer || !currentItem->isMoveable()) {
					continue;
				}
				if (currentItem->isFloorPersistenceCreatureCorpse() ||
				    currentContainer->getDepotLocker()) {
					break;
				}
				currentItem->setFloorPersistenceLastActorConsistentSubtree(false);
			}
			queueItemActorAttribution(item, playerGuid);
		} else {
			queueItemActorAttribution(item, playerGuid);
		}
	}
}

void Game::attributeDeliveredItem(Item* item, uint32_t recipientGuid,
	bool normalizeImmediately)
{
	if (!item || recipientGuid == 0 ||
	    (!normalizeImmediately && item->isRemoved()) ||
	    item->isFloorPersistenceCreatureCorpse() || !item->isMoveable()) {
		return;
	}

	if (normalizeImmediately) {
		// Depot lockers are virtual storage roots without a parent. Items held
		// by them report isRemoved() even though they are valid and will be
		// serialized immediately for an offline mail recipient.
		normalizeItemActorSubtree(item, recipientGuid, true);
		return;
	}

	attributeSuccessfulItemEndpoint(item->getParent(), item, recipientGuid);
}

void Game::attributeContainerMutation(Cylinder* cylinder, uint32_t playerGuid)
{
	attributeContainerPathAfterMutation(cylinder, playerGuid);
}

void Game::processItemActorAttributions(bool force)
{
	DispatcherPhaseMetricsTimer attributionTimer(DispatcherMetricsPhase::ITEM_ACTOR_ATTRIBUTION,
		!pendingItemActorAttributions.empty());

	const int64_t now = OTSYS_TIME();
	std::vector<Item*> ready;
	ready.reserve(pendingItemActorAttributions.size());
	for (const auto& entry : pendingItemActorAttributions) {
		const PendingItemActorAttribution& pending = entry.second;
		if (force ||
		    now - pending.lastModifiedMonotonic >= ITEM_ACTOR_DEBOUNCE_MS ||
		    now - pending.firstModifiedMonotonic >= ITEM_ACTOR_MAX_DELAY_MS) {
			ready.push_back(entry.first);
		}
	}

	uint32_t resolvedCount = 0;
	for (Item* root : ready) {
		auto it = pendingItemActorAttributions.find(root);
		if (it == pendingItemActorAttributions.end()) {
			continue;
		}
		const uint32_t playerGuid = it->second.playerGuid;
		pendingItemActorAttributions.erase(it);
		++resolvedCount;
		if (!root->isRemoved()) {
			normalizeItemActorSubtree(root, playerGuid);
			certifyItemActorAncestorPath(root, playerGuid);
		}
		root->decrementReferenceCounter();
	}

	if (resolvedCount != 0) {
		recordDispatcherActorAttributionsResolved(resolvedCount);
	}
}

void Game::flushItemActorAttributions()
{
	// Apply every pending subtree normalization immediately, ignoring the
	// debounce window. Called right before a save/logout commit so the parent
	// container and all nested items are serialized with a consistent
	// lastActorGuid, even if the player relogs before the debounce fires.
	processItemActorAttributions(true);
}

void Game::checkItemActorAttributions()
{
	processItemActorAttributions();
	g_scheduler.addEvent(createSchedulerTask(
		1000, std::bind(&Game::checkItemActorAttributions, this)));
}

void Game::registerFloorCheckpointTransfer(Cylinder* fromCylinder, Cylinder* toCylinder, Item* movedItem,
	Player* actorPlayer)
{
	DispatcherPhaseMetricsTimer checkpointRegTimer(DispatcherMetricsPhase::ITEM_MOVE_CHECKPOINT_REG);

	if (!floorSnapshotShadowEnabled || !floorDirtyTrackingEnabled) {
		return;
	}

	const FloorCheckpointEndpoint from = resolveFloorCheckpointEndpoint(fromCylinder, actorPlayer);
	const FloorCheckpointEndpoint to = resolveFloorCheckpointEndpoint(toCylinder, actorPlayer);
	const std::string instanceId = movedItem ? movedItem->getFloorPersistenceInstanceId() : std::string();

	std::set<uint64_t> relatedGroups;
	auto addTileGroup = [&](Tile* tile) {
		if (!tile) {
			return;
		}
		auto it = floorCheckpointTileGroups.find(tile->getPosition());
		if (it != floorCheckpointTileGroups.end()) {
			relatedGroups.insert(it->second);
		}
	};
	auto addPlayerGroup = [&](Player* player) {
		if (!player) {
			return;
		}
		auto it = floorCheckpointPlayerGroups.find(player->getGUID());
		if (it != floorCheckpointPlayerGroups.end()) {
			relatedGroups.insert(it->second);
		}
	};
	auto addHouseGroup = [&](House* house) {
		if (!house) {
			return;
		}
		auto it = floorCheckpointHouseGroups.find(house->getId());
		if (it != floorCheckpointHouseGroups.end()) {
			relatedGroups.insert(it->second);
		}
	};
	addTileGroup(from.tile);
	addTileGroup(to.tile);
	addPlayerGroup(from.player);
	addPlayerGroup(to.player);
	addHouseGroup(from.house);
	addHouseGroup(to.house);
	if (!instanceId.empty()) {
		auto it = floorCheckpointItemGroups.find(instanceId);
		if (it != floorCheckpointItemGroups.end()) {
			relatedGroups.insert(it->second);
		}
	}

	const bool crossesHouseBoundary =
		from.house != to.house && (from.house != nullptr || to.house != nullptr);
	if (!from.tile && !to.tile && !crossesHouseBoundary && relatedGroups.empty()) {
		return;
	}

	const uint64_t groupId = mergeFloorCheckpointGroups(relatedGroups);
	FloorCheckpointGroup& group = floorCheckpointGroups[groupId];
	if (from.tile) {
		group.positions.insert(from.tile->getPosition());
		floorCheckpointTileGroups[from.tile->getPosition()] = groupId;
	}
	if (to.tile) {
		group.positions.insert(to.tile->getPosition());
		floorCheckpointTileGroups[to.tile->getPosition()] = groupId;
	}
	if (from.player) {
		group.playerGuids.insert(from.player->getGUID());
		floorCheckpointPlayerGroups[from.player->getGUID()] = groupId;
	}
	if (to.player) {
		group.playerGuids.insert(to.player->getGUID());
		floorCheckpointPlayerGroups[to.player->getGUID()] = groupId;
	}
	if (from.house) {
		group.houseIds.insert(from.house->getId());
		floorCheckpointHouseGroups[from.house->getId()] = groupId;
	}
	if (to.house) {
		group.houseIds.insert(to.house->getId());
		floorCheckpointHouseGroups[to.house->getId()] = groupId;
	}
	if (!instanceId.empty()) {
		group.itemInstanceIds.insert(instanceId);
		floorCheckpointItemGroups[instanceId] = groupId;
	}
	touchFloorCheckpointGroup(groupId);
}

uint64_t Game::registerTradeCheckpoint(Player* player, Player* tradePartner,
	Cylinder* playerTradeSource, Cylinder* partnerTradeSource,
	Item* playerTradeItem, Item* partnerTradeItem)
{
	if (!floorSnapshotShadowEnabled || !floorDirtyTrackingEnabled ||
	    floorPersistenceSessionId == 0 || !player || !tradePartner) {
		return 0;
	}

	const FloorCheckpointEndpoint playerSource =
		resolveFloorCheckpointEndpoint(playerTradeSource, player);
	const FloorCheckpointEndpoint partnerSource =
		resolveFloorCheckpointEndpoint(partnerTradeSource, tradePartner);
	const std::string playerItemInstanceId = playerTradeItem ?
		playerTradeItem->getFloorPersistenceInstanceId() : std::string();
	const std::string partnerItemInstanceId = partnerTradeItem ?
		partnerTradeItem->getFloorPersistenceInstanceId() : std::string();

	std::set<uint64_t> relatedGroups;
	auto addTileGroup = [&](Tile* tile) {
		if (!tile) {
			return;
		}
		auto it = floorCheckpointTileGroups.find(tile->getPosition());
		if (it != floorCheckpointTileGroups.end()) {
			relatedGroups.insert(it->second);
		}
	};
	auto addPlayerGroup = [&](uint32_t playerGuid) {
		auto it = floorCheckpointPlayerGroups.find(playerGuid);
		if (it != floorCheckpointPlayerGroups.end()) {
			relatedGroups.insert(it->second);
		}
	};
	auto addHouseGroup = [&](House* house) {
		if (!house) {
			return;
		}
		auto it = floorCheckpointHouseGroups.find(house->getId());
		if (it != floorCheckpointHouseGroups.end()) {
			relatedGroups.insert(it->second);
		}
	};
	auto addItemGroup = [&](const std::string& instanceId) {
		if (instanceId.empty()) {
			return;
		}
		auto it = floorCheckpointItemGroups.find(instanceId);
		if (it != floorCheckpointItemGroups.end()) {
			relatedGroups.insert(it->second);
		}
	};

	addTileGroup(playerSource.tile);
	addTileGroup(partnerSource.tile);
	addPlayerGroup(player->getGUID());
	addPlayerGroup(tradePartner->getGUID());
	addHouseGroup(playerSource.house);
	addHouseGroup(partnerSource.house);
	addItemGroup(playerItemInstanceId);
	addItemGroup(partnerItemInstanceId);

	const uint64_t groupId = mergeFloorCheckpointGroups(relatedGroups);
	FloorCheckpointGroup& group = floorCheckpointGroups[groupId];
	auto includeSource = [&](const FloorCheckpointEndpoint& source) {
		if (source.tile) {
			group.positions.insert(source.tile->getPosition());
			floorCheckpointTileGroups[source.tile->getPosition()] = groupId;
		}
		if (source.house) {
			group.houseIds.insert(source.house->getId());
			floorCheckpointHouseGroups[source.house->getId()] = groupId;
		}
	};
	auto includeItem = [&](const std::string& instanceId) {
		if (!instanceId.empty()) {
			group.itemInstanceIds.insert(instanceId);
			floorCheckpointItemGroups[instanceId] = groupId;
		}
	};

	includeSource(playerSource);
	includeSource(partnerSource);
	group.playerGuids.insert(player->getGUID());
	group.playerGuids.insert(tradePartner->getGUID());
	floorCheckpointPlayerGroups[player->getGUID()] = groupId;
	floorCheckpointPlayerGroups[tradePartner->getGUID()] = groupId;
	includeItem(playerItemInstanceId);
	includeItem(partnerItemInstanceId);
	touchFloorCheckpointGroup(groupId);
	return groupId;
}

bool Game::commitActiveMailTransferCheckpoint(Player* recipient, Item* deliveredItem)
{
	if (!activeMailTransferCheckpoint.active || !activeMailTransferCheckpoint.sender ||
	    !recipient || !deliveredItem) {
		return false;
	}

	if (!floorSnapshotShadowEnabled || !floorDirtyTrackingEnabled || floorPersistenceSessionId == 0) {
		activeMailTransferCheckpoint.error =
			"atomic mail checkpoint requires active floor persistence";
		return false;
	}

	Player* sender = activeMailTransferCheckpoint.sender;
	const FloorCheckpointEndpoint source =
		resolveFloorCheckpointEndpoint(activeMailTransferCheckpoint.sourceCylinder, sender);
	if (source.tile) {
		// Mailbox::sendItem runs inside the destination addThing call. The
		// ordinary postRemoveNotification (and therefore the dirty event) has
		// not run yet, but the parcel has already left the live source. Mark the
		// tile now so the transaction serializes that exact empty/new state.
		markFloorTileDirty(*source.tile, FLOOR_DIRTY_ITEM_REMOVE,
			FLOOR_DIRTY_ORIGIN_PLAYER_MOVE);
	}
	Tile* mailboxTile = activeMailTransferCheckpoint.mailboxTile;

	const std::string instanceId = deliveredItem->getFloorPersistenceInstanceId();
	std::set<uint64_t> relatedGroups;
	auto addTileGroup = [&](Tile* tile) {
		if (!tile) {
			return;
		}
		auto it = floorCheckpointTileGroups.find(tile->getPosition());
		if (it != floorCheckpointTileGroups.end()) {
			relatedGroups.insert(it->second);
		}
	};
	auto addPlayerGroup = [&](uint32_t playerGuid) {
		auto it = floorCheckpointPlayerGroups.find(playerGuid);
		if (it != floorCheckpointPlayerGroups.end()) {
			relatedGroups.insert(it->second);
		}
	};
	auto addHouseGroup = [&](House* house) {
		if (!house) {
			return;
		}
		auto it = floorCheckpointHouseGroups.find(house->getId());
		if (it != floorCheckpointHouseGroups.end()) {
			relatedGroups.insert(it->second);
		}
	};
	addTileGroup(source.tile);
	addTileGroup(mailboxTile);
	addPlayerGroup(sender->getGUID());
	addPlayerGroup(recipient->getGUID());
	addHouseGroup(source.house);
	if (!instanceId.empty()) {
		auto it = floorCheckpointItemGroups.find(instanceId);
		if (it != floorCheckpointItemGroups.end()) {
			relatedGroups.insert(it->second);
		}
	}

	const uint64_t groupId = mergeFloorCheckpointGroups(relatedGroups);
	FloorCheckpointGroup& group = floorCheckpointGroups[groupId];
	if (source.tile) {
		group.positions.insert(source.tile->getPosition());
		floorCheckpointTileGroups[source.tile->getPosition()] = groupId;
	}
	if (mailboxTile) {
		// A normal client drop first inserts the parcel into the physical
		// mailbox tile. Tile::postAddNotification then forwards it to the
		// Mailbox cylinder, which removes it again. Both dirty events have
		// already happened when sendItem reaches this checkpoint.
		group.positions.insert(mailboxTile->getPosition());
		floorCheckpointTileGroups[mailboxTile->getPosition()] = groupId;
	}
	group.playerGuids.insert(sender->getGUID());
	group.playerGuids.insert(recipient->getGUID());
	floorCheckpointPlayerGroups[sender->getGUID()] = groupId;
	floorCheckpointPlayerGroups[recipient->getGUID()] = groupId;
	if (source.house) {
		group.houseIds.insert(source.house->getId());
		floorCheckpointHouseGroups[source.house->getId()] = groupId;
	}
	if (!instanceId.empty()) {
		group.itemInstanceIds.insert(instanceId);
		floorCheckpointItemGroups[instanceId] = groupId;
	}
	touchFloorCheckpointGroup(groupId);

	if (!executeFloorCheckpointGroup(groupId, recipient)) {
		auto failedGroupIt = floorCheckpointGroups.find(groupId);
		if (failedGroupIt != floorCheckpointGroups.end()) {
			activeMailTransferCheckpoint.error = failedGroupIt->second.lastError;

			// The temporary offline recipient is about to leave memory. It
			// cannot remain in a retryable runtime group. The source and sender
			// stay grouped and will be serialized again after rollback.
			if (recipient->getGUID() != sender->getGUID()) {
				failedGroupIt->second.playerGuids.erase(recipient->getGUID());
				auto playerGroupIt = floorCheckpointPlayerGroups.find(recipient->getGUID());
				if (playerGroupIt != floorCheckpointPlayerGroups.end() &&
				    playerGroupIt->second == groupId) {
					floorCheckpointPlayerGroups.erase(playerGroupIt);
				}
			}
		}
		if (activeMailTransferCheckpoint.error.empty()) {
			activeMailTransferCheckpoint.error = "atomic mail checkpoint failed";
		}
		return false;
	}

	activeMailTransferCheckpoint.committed = true;
	activeMailTransferCheckpoint.error.clear();
	return true;
}

bool Game::rollbackActiveMailTransfer(Item* deliveredItem)
{
	if (!activeMailTransferCheckpoint.active || activeMailTransferCheckpoint.committed ||
	    activeMailTransferCheckpoint.rolledBack || !deliveredItem ||
	    !activeMailTransferCheckpoint.sourceCylinder) {
		return activeMailTransferCheckpoint.rolledBack;
	}

	if (deliveredItem->getID() != activeMailTransferCheckpoint.originalItemId) {
		deliveredItem = transformItem(
			deliveredItem, activeMailTransferCheckpoint.originalItemId);
		if (!deliveredItem) {
			activeMailTransferCheckpoint.error +=
				"; could not restore original parcel type";
			return false;
		}
	}

	Cylinder* currentParent = deliveredItem->getParent();
	ReturnValue rollbackResult = RETURNVALUE_NOERROR;
	if (currentParent && currentParent != activeMailTransferCheckpoint.sourceCylinder) {
		rollbackResult = internalMoveItem(
			currentParent, activeMailTransferCheckpoint.sourceCylinder,
			activeMailTransferCheckpoint.sourceIndex, deliveredItem,
			deliveredItem->getItemCount(), nullptr, FLAG_NOLIMIT);
	} else if (activeMailTransferCheckpoint.sourceCylinder->getThingIndex(deliveredItem) == -1) {
		activeMailTransferCheckpoint.sourceCylinder->addThing(
			activeMailTransferCheckpoint.sourceIndex, deliveredItem);
		const int32_t restoredIndex =
			activeMailTransferCheckpoint.sourceCylinder->getThingIndex(deliveredItem);
		if (restoredIndex != -1) {
			activeMailTransferCheckpoint.sourceCylinder->postAddNotification(
				deliveredItem, currentParent, restoredIndex);
		} else {
			rollbackResult = RETURNVALUE_NOTPOSSIBLE;
		}
	}

	if (rollbackResult != RETURNVALUE_NOERROR) {
		activeMailTransferCheckpoint.error +=
			"; could not return the parcel to its source";
		return false;
	}

	activeMailTransferCheckpoint.rolledBack = true;
	return true;
}

void Game::removeFloorCheckpointGroup(uint64_t groupId)
{
	auto groupIt = floorCheckpointGroups.find(groupId);
	if (groupIt == floorCheckpointGroups.end()) {
		return;
	}

	for (const Position& position : groupIt->second.positions) {
		auto it = floorCheckpointTileGroups.find(position);
		if (it != floorCheckpointTileGroups.end() && it->second == groupId) {
			floorCheckpointTileGroups.erase(it);
		}
	}
	for (uint32_t playerGuid : groupIt->second.playerGuids) {
		auto it = floorCheckpointPlayerGroups.find(playerGuid);
		if (it != floorCheckpointPlayerGroups.end() && it->second == groupId) {
			floorCheckpointPlayerGroups.erase(it);
		}
	}
	for (uint32_t houseId : groupIt->second.houseIds) {
		auto it = floorCheckpointHouseGroups.find(houseId);
		if (it != floorCheckpointHouseGroups.end() && it->second == groupId) {
			floorCheckpointHouseGroups.erase(it);
		}
	}
	for (const std::string& instanceId : groupIt->second.itemInstanceIds) {
		auto it = floorCheckpointItemGroups.find(instanceId);
		if (it != floorCheckpointItemGroups.end() && it->second == groupId) {
			floorCheckpointItemGroups.erase(it);
		}
	}
	floorCheckpointGroups.erase(groupIt);
}

uint32_t Game::getFloorSnapshotInFlightCount() const
{
	uint32_t count = 0;
	for (const auto& entry : floorDirtyTiles) {
		if (entry.second.snapshotInFlight) {
			++count;
		}
	}
	return count;
}

void Game::checkFloorSnapshots()
{
	if (floorSnapshotShadowEnabled) {
		processFloorSnapshots(false);
		g_scheduler.addEvent(createSchedulerTask(1000, std::bind(&Game::checkFloorSnapshots, this)));
	}
}

uint32_t Game::processFloorSnapshots(bool force)
{
	if (!floorSnapshotShadowEnabled) {
		return 0;
	}

	DispatcherPhaseMetricsTimer snapshotTickTimer(DispatcherMetricsPhase::FLOOR_SNAPSHOT_TICK);

	// Reclaim any work left behind by a dead worker, then apply the results of
	// background checkpoint jobs that finished since the last pass.
	recoverDeadCheckpointWorker();
	processCheckpointResults();

	const int64_t now = OTSYS_TIME();
	uint32_t queued = 0;
	size_t stuckGroups = 0;
	std::vector<uint64_t> readyGroups;
	readyGroups.reserve(floorCheckpointGroups.size());
	for (const auto& entry : floorCheckpointGroups) {
		const FloorCheckpointGroup& group = entry.second;
		if (group.retryCount >= CHECKPOINT_STUCK_ALERT_RETRY_THRESHOLD) {
			++stuckGroups;
		}
		if (group.workerInFlight) {
			continue;
		}
		if (now < group.retryNotBefore) {
			continue;
		}

		bool snapshotInFlight = false;
		for (const Position& position : group.positions) {
			auto dirtyIt = floorDirtyTiles.find(position);
			if (dirtyIt != floorDirtyTiles.end() && dirtyIt->second.snapshotInFlight) {
				snapshotInFlight = true;
				break;
			}
		}
		if (snapshotInFlight) {
			continue;
		}

		const bool debounceElapsed = now - group.lastModifiedMonotonic >= floorSnapshotDebounceMs;
		const bool maxDelayElapsed = now - group.firstModifiedMonotonic >= floorSnapshotMaxDelayMs;
		if (force || debounceElapsed || maxDelayElapsed) {
			readyGroups.push_back(entry.first);
		}
	}
	recordDispatcherCheckpointStuckGroups(stuckGroups);

	for (uint64_t groupId : readyGroups) {
		auto groupIt = floorCheckpointGroups.find(groupId);
		if (groupIt == floorCheckpointGroups.end()) {
			continue;
		}
		const uint32_t groupTiles = static_cast<uint32_t>(groupIt->second.positions.size());
		if (queued != 0 && queued + groupTiles > floorSnapshotBatchSize) {
			break;
		}

		bool handled;
		if (g_checkpointWorker.isHealthy()) {
			handled = enqueueFloorCheckpointGroup(groupId);
		} else {
			handled = executeFloorCheckpointGroup(groupId);
		}

		if (handled) {
			queued += groupTiles;
		} else {
			break;
		}
		if (queued >= floorSnapshotBatchSize) {
			break;
		}
	}

	for (auto& entry : floorDirtyTiles) {
		if (queued >= floorSnapshotBatchSize) {
			break;
		}
		if (floorCheckpointTileGroups.find(entry.first) != floorCheckpointTileGroups.end()) {
			continue;
		}

		FloorDirtyTileRecord& record = entry.second;
		if (record.snapshotInFlight || now < record.snapshotRetryNotBefore) {
			continue;
		}

		const bool debounceElapsed = now - record.lastModifiedMonotonic >= floorSnapshotDebounceMs;
		const bool maxDelayElapsed = now - record.firstModifiedMonotonic >= floorSnapshotMaxDelayMs;
		if (!force && !debounceElapsed && !maxDelayElapsed) {
			continue;
		}

		if (queueFloorSnapshot(entry.first, record)) {
			++queued;
		}
	}

	if (force && g_checkpointWorker.isHealthy()) {
		// Forced flushes (server save / manual flush) must leave every captured
		// checkpoint committed before returning.
		if (!drainCheckpointWorker(CHECKPOINT_SYNC_DRAIN_TIMEOUT_MS)) {
			std::cout << "[Warning - Game::processFloorSnapshots] forced flush could not "
			          << "commit every background checkpoint within the limit." << std::endl;
		}
	}
	return queued;
}

bool Game::queueFloorSnapshot(const Position& position, FloorDirtyTileRecord& record)
{
	DispatcherPhaseMetricsTimer prepareTimer(DispatcherMetricsPhase::FLOOR_SNAPSHOT_PREPARE);

	Tile* tile = map.getTile(position);
	// City tiles are captured during normal runtime as crash-recovery data.
	// They are filtered only after a clean, coordinated server save.
	const bool cityExcluded = false;
	FloorSnapshotData snapshot;
	std::string error;
	const auto serializationStarted = std::chrono::steady_clock::now();
	const bool serialized = FloorPersistenceSerializer::serializeTile(position, tile, cityExcluded, snapshot, error);
	const uint64_t serializationMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - serializationStarted).count());

	floorSnapshotStats.lastSerializationMicros = serializationMicros;
	floorSnapshotStats.totalSerializationMicros += serializationMicros;
	if (!serialized) {
		++floorSnapshotStats.serializationFailed;
		floorSnapshotStats.lastError = error;
		record.lastSnapshotError = error;
		++record.snapshotRetryCount;
		record.snapshotRetryNotBefore = OTSYS_TIME() + static_cast<int64_t>(floorSnapshotRetryMs) *
			std::min<uint32_t>(record.snapshotRetryCount, 6);
		return false;
	}

	FloorSnapshotRuntimeRecord runtimeRecord;
	runtimeRecord.tileVersion = record.tileVersion;
	runtimeRecord.itemCount = snapshot.itemCount;
	runtimeRecord.topItemCount = snapshot.topItemCount;
	runtimeRecord.serializedBytes = static_cast<uint32_t>(snapshot.serializedData.size());
	runtimeRecord.persistAlwaysCount = snapshot.persistAlwaysCount;
	runtimeRecord.persistCleanOnlyCount = snapshot.persistCleanOnlyCount;
	runtimeRecord.persistFoodCount = snapshot.persistFoodCount;
	runtimeRecord.deathBundleCount = snapshot.deathBundleCount;
	runtimeRecord.excludedItemCount = snapshot.excludedItemCount;
	runtimeRecord.identityMissingCount = snapshot.identityMissingCount;
	runtimeRecord.identityInvalidCount = snapshot.identityInvalidCount;
	runtimeRecord.playerCorpseCount = snapshot.playerCorpseCount;
	runtimeRecord.serializationMicros = serializationMicros;
	runtimeRecord.checksum = snapshot.checksum;

	Database& database = Database::getInstance();
	const std::string escapedData = database.escapeBlob(snapshot.serializedData.data(),
		static_cast<uint32_t>(snapshot.serializedData.size()));
	const std::string query = fmt::format(
		"INSERT INTO `floor_persistence_snapshots` "
		"(`world_id`,`generation_id`,`tile_x`,`tile_y`,`tile_z`,`tile_version`,`format_version`,`policy_version`,"
		"`item_count`,`top_item_count`,`serialized_bytes`,`persist_always_count`,`persist_clean_only_count`,"
		"`persist_food_count`,`death_bundle_count`,`excluded_item_count`,`identity_missing_count`,"
		"`identity_invalid_count`,`player_corpse_count`,`checksum`,`serialized_data`,`dirty_reason_mask`,"
		"`dirty_origin_mask`,`serialization_duration_us`,`checkpoint_group_id`,`checkpoint_group_version`,"
		"`save_session_id`,`city_cleanup_filtered`) VALUES "
		"({:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},"
		"'{:s}',{:s},{:d},{:d},{:d},0,0,{:d},0) ON DUPLICATE KEY UPDATE "
		"`format_version`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`format_version`),`format_version`),"
		"`policy_version`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`policy_version`),`policy_version`),"
		"`item_count`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`item_count`),`item_count`),"
		"`top_item_count`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`top_item_count`),`top_item_count`),"
		"`serialized_bytes`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`serialized_bytes`),`serialized_bytes`),"
		"`persist_always_count`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`persist_always_count`),`persist_always_count`),"
		"`persist_clean_only_count`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`persist_clean_only_count`),`persist_clean_only_count`),"
		"`persist_food_count`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`persist_food_count`),`persist_food_count`),"
		"`death_bundle_count`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`death_bundle_count`),`death_bundle_count`),"
		"`excluded_item_count`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`excluded_item_count`),`excluded_item_count`),"
		"`identity_missing_count`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`identity_missing_count`),`identity_missing_count`),"
		"`identity_invalid_count`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`identity_invalid_count`),`identity_invalid_count`),"
		"`player_corpse_count`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`player_corpse_count`),`player_corpse_count`),"
		"`checksum`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`checksum`),`checksum`),"
		"`serialized_data`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`serialized_data`),`serialized_data`),"
		"`dirty_reason_mask`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`dirty_reason_mask`),`dirty_reason_mask`),"
		"`dirty_origin_mask`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`dirty_origin_mask`),`dirty_origin_mask`),"
		"`serialization_duration_us`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`serialization_duration_us`),`serialization_duration_us`),"
		"`checkpoint_group_id`=IF(VALUES(`tile_version`)>=`tile_version`,0,`checkpoint_group_id`),"
		"`checkpoint_group_version`=IF(VALUES(`tile_version`)>=`tile_version`,0,`checkpoint_group_version`),"
		"`save_session_id`=IF(VALUES(`tile_version`)>=`tile_version`,VALUES(`save_session_id`),`save_session_id`),"
		"`city_cleanup_filtered`=IF(VALUES(`tile_version`)>=`tile_version`,0,`city_cleanup_filtered`),"
		"`updated_at`=IF(VALUES(`tile_version`)>=`tile_version`,CURRENT_TIMESTAMP(6),`updated_at`),"
		"`tile_version`=GREATEST(`tile_version`,VALUES(`tile_version`))",
		floorSnapshotWorldId, floorSnapshotGenerationId, position.x, position.y, position.z,
		runtimeRecord.tileVersion, FLOOR_SNAPSHOT_FORMAT_VERSION, FLOOR_SNAPSHOT_POLICY_VERSION,
		runtimeRecord.itemCount, runtimeRecord.topItemCount, runtimeRecord.serializedBytes,
		runtimeRecord.persistAlwaysCount, runtimeRecord.persistCleanOnlyCount, runtimeRecord.persistFoodCount,
		runtimeRecord.deathBundleCount, runtimeRecord.excludedItemCount, runtimeRecord.identityMissingCount,
		runtimeRecord.identityInvalidCount, runtimeRecord.playerCorpseCount, runtimeRecord.checksum, escapedData,
		record.reasonMask, record.originMask, serializationMicros, floorPersistenceSessionId);

	if (query.size() >= database.getMaxPacketSize()) {
		error = "snapshot UPSERT exceeds max_allowed_packet";
		++floorSnapshotStats.serializationFailed;
		floorSnapshotStats.lastError = error;
		record.lastSnapshotError = error;
		++record.snapshotRetryCount;
		record.snapshotRetryNotBefore = OTSYS_TIME() + static_cast<int64_t>(floorSnapshotRetryMs) *
			std::min<uint32_t>(record.snapshotRetryCount, 6);
		return false;
	}

	record.snapshotInFlight = true;
	record.snapshotVersionInFlight = runtimeRecord.tileVersion;
	record.lastSnapshotError.clear();
	++floorSnapshotStats.queued;
	floorSnapshotStats.totalSerializedBytes += runtimeRecord.serializedBytes;

	if (floorSnapshotSimulatedFailures != 0) {
		--floorSnapshotSimulatedFailures;
		completeFloorSnapshot(position, runtimeRecord.tileVersion, false, std::move(runtimeRecord),
			"simulated stage 3 database failure");
		return true;
	}

	g_databaseTasks.addTask(query,
		[this, position, tileVersion = runtimeRecord.tileVersion, runtimeRecord = std::move(runtimeRecord)]
		(DBResult_ptr, bool success) mutable {
			completeFloorSnapshot(position, tileVersion, success, std::move(runtimeRecord),
				success ? std::string() : "database UPSERT failed");
		}, false);
	return true;
}

bool Game::prepareFloorSnapshot(const Position& position, const FloorDirtyTileRecord& record,
	bool cityCleanupFiltered, uint64_t groupId, uint64_t groupVersion,
	PreparedFloorSnapshot& prepared, std::string& error)
{
	DispatcherPhaseMetricsTimer prepareTimer(DispatcherMetricsPhase::FLOOR_SNAPSHOT_PREPARE);

	FloorSnapshotData snapshot;
	const auto serializationStarted = std::chrono::steady_clock::now();
	if (!FloorPersistenceSerializer::serializeTile(position, map.getTile(position), cityCleanupFiltered, snapshot, error)) {
		return false;
	}

	const uint64_t serializationMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - serializationStarted).count());
	prepared = {};
	prepared.position = position;
	prepared.tileVersion = record.tileVersion;
	prepared.reasonMask = record.reasonMask;
	prepared.originMask = record.originMask;
	prepared.cityCleanupFiltered = cityCleanupFiltered;
	prepared.runtimeRecord.tileVersion = record.tileVersion;
	prepared.runtimeRecord.itemCount = snapshot.itemCount;
	prepared.runtimeRecord.topItemCount = snapshot.topItemCount;
	prepared.runtimeRecord.serializedBytes = static_cast<uint32_t>(snapshot.serializedData.size());
	prepared.runtimeRecord.persistAlwaysCount = snapshot.persistAlwaysCount;
	prepared.runtimeRecord.persistCleanOnlyCount = snapshot.persistCleanOnlyCount;
	prepared.runtimeRecord.persistFoodCount = snapshot.persistFoodCount;
	prepared.runtimeRecord.deathBundleCount = snapshot.deathBundleCount;
	prepared.runtimeRecord.excludedItemCount = snapshot.excludedItemCount;
	prepared.runtimeRecord.identityMissingCount = snapshot.identityMissingCount;
	prepared.runtimeRecord.identityInvalidCount = snapshot.identityInvalidCount;
	prepared.runtimeRecord.playerCorpseCount = snapshot.playerCorpseCount;
	prepared.runtimeRecord.serializationMicros = serializationMicros;
	prepared.runtimeRecord.checksum = snapshot.checksum;

	Database& database = Database::getInstance();
	const std::string escapedData = database.escapeBlob(snapshot.serializedData.data(),
		static_cast<uint32_t>(snapshot.serializedData.size()));
	prepared.query = fmt::format(
		"INSERT INTO `floor_persistence_snapshots` "
		"(`world_id`,`generation_id`,`tile_x`,`tile_y`,`tile_z`,`tile_version`,`format_version`,`policy_version`,"
		"`item_count`,`top_item_count`,`serialized_bytes`,`persist_always_count`,`persist_clean_only_count`,"
		"`persist_food_count`,`death_bundle_count`,`excluded_item_count`,`identity_missing_count`,"
		"`identity_invalid_count`,`player_corpse_count`,`checksum`,`serialized_data`,`dirty_reason_mask`,"
		"`dirty_origin_mask`,`serialization_duration_us`,`checkpoint_group_id`,`checkpoint_group_version`,"
		"`save_session_id`,`city_cleanup_filtered`) VALUES "
		"({:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},"
		"'{:s}',{:s},{:d},{:d},{:d},{:d},{:d},{:d},{:d}) ON DUPLICATE KEY UPDATE "
		"`tile_version`=VALUES(`tile_version`),`format_version`=VALUES(`format_version`),"
		"`policy_version`=VALUES(`policy_version`),`item_count`=VALUES(`item_count`),"
		"`top_item_count`=VALUES(`top_item_count`),`serialized_bytes`=VALUES(`serialized_bytes`),"
		"`persist_always_count`=VALUES(`persist_always_count`),"
		"`persist_clean_only_count`=VALUES(`persist_clean_only_count`),`persist_food_count`=VALUES(`persist_food_count`),"
		"`death_bundle_count`=VALUES(`death_bundle_count`),`excluded_item_count`=VALUES(`excluded_item_count`),"
		"`identity_missing_count`=VALUES(`identity_missing_count`),"
		"`identity_invalid_count`=VALUES(`identity_invalid_count`),`player_corpse_count`=VALUES(`player_corpse_count`),"
		"`checksum`=VALUES(`checksum`),`serialized_data`=VALUES(`serialized_data`),"
		"`dirty_reason_mask`=VALUES(`dirty_reason_mask`),`dirty_origin_mask`=VALUES(`dirty_origin_mask`),"
		"`serialization_duration_us`=VALUES(`serialization_duration_us`),"
		"`checkpoint_group_id`=VALUES(`checkpoint_group_id`),"
		"`checkpoint_group_version`=VALUES(`checkpoint_group_version`),"
		"`save_session_id`=VALUES(`save_session_id`),`city_cleanup_filtered`=VALUES(`city_cleanup_filtered`),"
		"`updated_at`=CURRENT_TIMESTAMP(6)",
		floorSnapshotWorldId, floorSnapshotGenerationId, position.x, position.y, position.z,
		prepared.tileVersion, FLOOR_SNAPSHOT_FORMAT_VERSION, FLOOR_SNAPSHOT_POLICY_VERSION,
		prepared.runtimeRecord.itemCount, prepared.runtimeRecord.topItemCount,
		prepared.runtimeRecord.serializedBytes, prepared.runtimeRecord.persistAlwaysCount,
		prepared.runtimeRecord.persistCleanOnlyCount, prepared.runtimeRecord.persistFoodCount,
		prepared.runtimeRecord.deathBundleCount, prepared.runtimeRecord.excludedItemCount,
		prepared.runtimeRecord.identityMissingCount, prepared.runtimeRecord.identityInvalidCount,
		prepared.runtimeRecord.playerCorpseCount, prepared.runtimeRecord.checksum, escapedData,
		prepared.reasonMask, prepared.originMask, serializationMicros, groupId, groupVersion,
		floorPersistenceSessionId, cityCleanupFiltered ? 1 : 0);

	if (prepared.query.size() >= database.getMaxPacketSize()) {
		error = "snapshot UPSERT exceeds max_allowed_packet";
		return false;
	}
	return true;
}

bool Game::executeFloorSnapshotsTransaction(const std::vector<PreparedFloorSnapshot>& snapshots,
	const std::vector<Player*>& checkpointPlayers, const std::vector<House*>& checkpointHouses,
	uint64_t groupId, uint64_t groupVersion, std::string& error,
	bool commitCleanSave, bool resetFloorSnapshots)
{
	if (floorSnapshotSimulatedFailures != 0) {
		--floorSnapshotSimulatedFailures;
		error = "simulated coordinated checkpoint database failure";
		return false;
	}
	if (resetFloorSnapshots && !commitCleanSave) {
		error = "floor snapshot reset requires a clean save transaction";
		return false;
	}
	if (resetFloorSnapshots && !snapshots.empty()) {
		error = "floor snapshot reset must commit an empty checkpoint";
		return false;
	}

	DBTransaction transaction;
	{
		DispatcherPhaseMetricsTimer beginTimer(DispatcherMetricsPhase::FLOOR_CHECKPOINT_TX_BEGIN);
		if (!transaction.begin()) {
			error = "could not begin coordinated checkpoint transaction";
			return false;
		}
	}

	for (Player* checkpointPlayer : checkpointPlayers) {
		DispatcherPhaseMetricsTimer playerSaveTimer(
			DispatcherMetricsPhase::FLOOR_CHECKPOINT_PLAYER_SAVE, checkpointPlayer != nullptr);
		if (!checkpointPlayer) {
			error = "could not save every player in the coordinated checkpoint";
			return false;
		}

		bool playerSaved;
		{
			DispatcherCheckpointSaveMetricsContext checkpointSaveContext;
			playerSaved = IOLoginData::savePlayerData(checkpointPlayer);
		}
		if (!playerSaved) {
			error = "could not save every player in the coordinated checkpoint";
			return false;
		}
	}
	for (House* checkpointHouse : checkpointHouses) {
		DispatcherPhaseMetricsTimer houseSaveTimer(
			DispatcherMetricsPhase::FLOOR_CHECKPOINT_HOUSE_SAVE, checkpointHouse != nullptr);
		if (!checkpointHouse || !IOMapSerialize::saveHouseData(checkpointHouse)) {
			error = "could not save every house in the coordinated checkpoint";
			return false;
		}
	}

	Database& database = Database::getInstance();
	size_t executedTileQueries = 0;
	{
		DispatcherPhaseMetricsTimer tileSqlTimer(DispatcherMetricsPhase::FLOOR_CHECKPOINT_TILE_SQL);
		if (resetFloorSnapshots && !database.executeQuery(fmt::format(
			"DELETE FROM `floor_persistence_snapshots` WHERE `world_id`={:d} AND `generation_id`={:d}",
			floorSnapshotWorldId, floorSnapshotGenerationId))) {
			error = "could not atomically remove materialized snapshots for the weekly floor reset";
			return false;
		}
		for (const PreparedFloorSnapshot& snapshot : snapshots) {
			if (!database.executeQuery(snapshot.query)) {
				error = "could not save every tile in the coordinated checkpoint";
				return false;
			}
			++executedTileQueries;
		}
	}
	recordDispatcherCheckpointTileQueries(executedTileQueries);

	{
		DispatcherPhaseMetricsTimer markerTimer(DispatcherMetricsPhase::FLOOR_CHECKPOINT_MARKER_SQL);
		if (!database.executeQuery(fmt::format(
			"INSERT INTO `floor_persistence_checkpoints` (`world_id`,`generation_id`,`save_session_id`,"
			"`checkpoint_group_id`,`checkpoint_group_version`,`tile_count`,`player_count`,`house_count`,`state`) VALUES "
			"({:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},'COMMITTED')",
			floorSnapshotWorldId, floorSnapshotGenerationId, floorPersistenceSessionId, groupId, groupVersion,
			snapshots.size(), checkpointPlayers.size(), checkpointHouses.size()))) {
			error = "could not register the coordinated checkpoint commit";
			return false;
		}
	}

	// The filtered city snapshots and CLEAN_COMMITTED must become durable
	// together. If the process or database fails before this transaction
	// commits, MariaDB rolls both changes back and the last unfiltered runtime
	// snapshots remain available for crash recovery.
	if (commitCleanSave) {
		if (floorPersistenceSessionId == 0) {
			error = "clean save has no active persistence session";
			return false;
		}

		DispatcherPhaseMetricsTimer cleanSaveTimer(DispatcherMetricsPhase::FLOOR_CHECKPOINT_CLEAN_SAVE_SQL);
		if (!database.executeQuery(fmt::format(
			"UPDATE `floor_persistence_save_sessions` SET `state`='CLEAN_COMMITTED',"
			"`player_count`={:d},`tile_count`={:d},`error`='',`updated_at`=CURRENT_TIMESTAMP(6),"
			"`committed_at`=CURRENT_TIMESTAMP(6) WHERE `id`={:d}",
			floorCleanSavePlayerCount, snapshots.size(), floorPersistenceSessionId))) {
			error = "could not atomically commit the clean save session";
			return false;
		}
	}

	{
		DispatcherPhaseMetricsTimer commitTimer(DispatcherMetricsPhase::FLOOR_CHECKPOINT_TX_COMMIT);
		if (!transaction.commit()) {
			error = "could not commit the coordinated checkpoint transaction";
			return false;
		}
	}
	if (commitCleanSave) {
		floorPersistenceSessionState = "CLEAN_COMMITTED";
	}
	return true;
}

void Game::completePreparedFloorSnapshots(const std::vector<PreparedFloorSnapshot>& snapshots)
{
	const int64_t persistedAt = static_cast<int64_t>(time(nullptr));
	for (const PreparedFloorSnapshot& prepared : snapshots) {
		FloorSnapshotRuntimeRecord runtimeRecord = prepared.runtimeRecord;
		runtimeRecord.persistedAt = persistedAt;
		auto runtimeIt = floorSnapshotRuntimeRecords.find(prepared.position);
		if (runtimeIt == floorSnapshotRuntimeRecords.end() ||
		    runtimeIt->second.tileVersion <= runtimeRecord.tileVersion) {
			floorSnapshotRuntimeRecords[prepared.position] = std::move(runtimeRecord);
		}

		auto dirtyIt = floorDirtyTiles.find(prepared.position);
		if (dirtyIt != floorDirtyTiles.end() && dirtyIt->second.tileVersion == prepared.tileVersion) {
			floorDirtyTiles.erase(dirtyIt);
		}
		++floorSnapshotStats.succeeded;
		floorSnapshotStats.totalSerializedBytes += prepared.runtimeRecord.serializedBytes;
		floorSnapshotStats.totalSerializationMicros += prepared.runtimeRecord.serializationMicros;
		floorSnapshotStats.lastSerializationMicros = prepared.runtimeRecord.serializationMicros;
	}
	floorSnapshotStats.lastSuccessAt = persistedAt;
	floorSnapshotStats.lastError.clear();
}

bool Game::executeFloorCheckpointGroup(uint64_t groupId, Player* requiredPlayer)
{
	// Synchronous checkpoints must never race an in-flight background job.
	if (!drainCheckpointWorker(CHECKPOINT_SYNC_DRAIN_TIMEOUT_MS)) {
		return false;
	}

	auto groupIt = floorCheckpointGroups.find(groupId);
	if (groupIt == floorCheckpointGroups.end()) {
		return true;
	}

	DispatcherPhaseMetricsTimer checkpointTimer(DispatcherMetricsPhase::FLOOR_CHECKPOINT_GROUP);
	DispatcherDatabaseLockWaitScope databaseLockWaitScope;

	FloorCheckpointGroup& group = groupIt->second;
	std::vector<Player*> checkpointPlayers;
	checkpointPlayers.reserve(group.playerGuids.size());
	for (uint32_t playerGuid : group.playerGuids) {
		Player* checkpointPlayer = requiredPlayer && requiredPlayer->getGUID() == playerGuid ?
			requiredPlayer : getPlayerByGUID(playerGuid);
		if (!checkpointPlayer) {
			failFloorCheckpointGroup(&group, "a checkpoint participant is no longer available in memory",
				CheckpointGroupFailureKind::PARTICIPANT_UNAVAILABLE);
			return false;
		}
		checkpointPlayers.push_back(checkpointPlayer);
	}
	std::vector<House*> checkpointHouses;
	checkpointHouses.reserve(group.houseIds.size());
	for (uint32_t houseId : group.houseIds) {
		House* checkpointHouse = map.houses.getHouse(houseId);
		if (!checkpointHouse) {
			failFloorCheckpointGroup(&group, "a checkpoint house is no longer available in memory",
				CheckpointGroupFailureKind::HOUSE_UNAVAILABLE);
			return false;
		}
		checkpointHouses.push_back(checkpointHouse);
	}

	std::vector<PreparedFloorSnapshot> snapshots;
	snapshots.reserve(group.positions.size());
	std::string error;
	for (const Position& position : group.positions) {
		auto dirtyIt = floorDirtyTiles.find(position);
		if (dirtyIt == floorDirtyTiles.end()) {
			continue;
		}
		PreparedFloorSnapshot prepared;
		if (!prepareFloorSnapshot(position, dirtyIt->second, false, group.id, group.version, prepared, error)) {
			failFloorCheckpointGroup(&group, error, CheckpointGroupFailureKind::SERIALIZATION);
			return false;
		}
		snapshots.push_back(std::move(prepared));
	}

	if (!executeFloorSnapshotsTransaction(
	        snapshots, checkpointPlayers, checkpointHouses, group.id, group.version, error)) {
		failFloorCheckpointGroup(&group, error, CheckpointGroupFailureKind::TRANSACTION);
		return false;
	}

	floorSnapshotStats.queued += snapshots.size();
	completePreparedFloorSnapshots(snapshots);
	floorSnapshotStats.checkpointTilesSaved += snapshots.size();
	floorSnapshotStats.checkpointPlayersSaved += checkpointPlayers.size();
	floorSnapshotStats.checkpointHousesSaved += checkpointHouses.size();
	++floorSnapshotStats.checkpointGroupsSucceeded;
	recordDispatcherCheckpointGroupSaved(snapshots.size(), checkpointPlayers.size());
	removeFloorCheckpointGroup(groupId);
	return true;
}

bool Game::enqueueFloorCheckpointGroup(uint64_t groupId)
{
	auto groupIt = floorCheckpointGroups.find(groupId);
	if (groupIt == floorCheckpointGroups.end()) {
		return true;
	}
	FloorCheckpointGroup& group = groupIt->second;
	if (group.workerInFlight) {
		return true;
	}

	DispatcherPhaseMetricsTimer captureTimer(DispatcherMetricsPhase::FLOOR_CHECKPOINT_CAPTURE);

	auto job = std::make_unique<CheckpointJob>();
	job->groupId = group.id;
	job->groupVersion = group.version;

	// Capture every player save as immutable SQL statements. Reading live Player
	// state happens here, on the Dispatcher; only strings cross to the worker.
	for (uint32_t playerGuid : group.playerGuids) {
		Player* checkpointPlayer = getPlayerByGUID(playerGuid);
		if (!checkpointPlayer) {
			failFloorCheckpointGroup(&group, "a checkpoint participant is no longer available in memory",
				CheckpointGroupFailureKind::PARTICIPANT_UNAVAILABLE);
			return false;
		}

		std::vector<std::string> statements;
		bool captured;
		{
			DispatcherCheckpointSaveMetricsContext checkpointSaveContext;
			PlayerIORemoteDatabaseScope collector(statements);
			DBTransaction transaction;
			captured = transaction.begin() && IOLoginData::savePlayerData(checkpointPlayer) &&
			           transaction.commit();
		}
		if (!captured || statements.empty()) {
			failFloorCheckpointGroup(&group, "could not capture a player save for the coordinated checkpoint",
				CheckpointGroupFailureKind::CAPTURE_FAILED);
			return false;
		}
		job->playerStatements.insert(job->playerStatements.end(),
			std::make_move_iterator(statements.begin()), std::make_move_iterator(statements.end()));
		job->playerGuids.insert(playerGuid);
	}

	// Capture every house save as immutable SQL statements.
	for (uint32_t houseId : group.houseIds) {
		House* checkpointHouse = map.houses.getHouse(houseId);
		if (!checkpointHouse) {
			failFloorCheckpointGroup(&group, "a checkpoint house is no longer available in memory",
				CheckpointGroupFailureKind::HOUSE_UNAVAILABLE);
			return false;
		}

		std::vector<std::string> statements;
		bool captured;
		{
			PlayerIORemoteDatabaseScope collector(statements);
			DBTransaction transaction;
			captured = transaction.begin() && IOMapSerialize::saveHouseData(checkpointHouse) &&
			           transaction.commit();
		}
		if (!captured) {
			failFloorCheckpointGroup(&group, "could not capture a house save for the coordinated checkpoint",
				CheckpointGroupFailureKind::CAPTURE_FAILED);
			return false;
		}
		job->houseStatements.insert(job->houseStatements.end(),
			std::make_move_iterator(statements.begin()), std::make_move_iterator(statements.end()));
		job->houseIds.insert(houseId);
	}

	// Capture every dirty tile snapshot as an immutable UPSERT statement.
	std::string prepareError;
	for (const Position& position : group.positions) {
		auto dirtyIt = floorDirtyTiles.find(position);
		if (dirtyIt == floorDirtyTiles.end()) {
			continue;
		}
		PreparedFloorSnapshot prepared;
		if (!prepareFloorSnapshot(position, dirtyIt->second, false, group.id, group.version, prepared,
		                          prepareError)) {
			failFloorCheckpointGroup(&group, prepareError, CheckpointGroupFailureKind::SERIALIZATION);
			return false;
		}
		job->floorStatements.push_back(prepared.query);
		job->snapshots.push_back(std::move(prepared));
	}

	job->markerStatement = fmt::format(
		"INSERT INTO `floor_persistence_checkpoints` (`world_id`,`generation_id`,`save_session_id`,"
		"`checkpoint_group_id`,`checkpoint_group_version`,`tile_count`,`player_count`,`house_count`,`state`) VALUES "
		"({:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},'COMMITTED')",
		floorSnapshotWorldId, floorSnapshotGenerationId, floorPersistenceSessionId, group.id, group.version,
		job->snapshots.size(), job->playerGuids.size(), job->houseIds.size());

	const size_t pendingDepth = g_checkpointWorker.pendingCount() + 1;
	if (!g_checkpointWorker.enqueue(std::move(job))) {
		recordDispatcherCheckpointBackpressureSkip();
		// The worker is saturated; leave the group dirty and retry later.
		group.retryNotBefore = OTSYS_TIME() + static_cast<int64_t>(floorSnapshotRetryMs);
		return false;
	}

	group.workerInFlight = true;
	for (uint32_t playerGuid : group.playerGuids) {
		++floorCheckpointInFlightPlayers[playerGuid];
	}
	recordDispatcherCheckpointJobQueued(pendingDepth);
	return true;
}

bool Game::saveFloorCheckpointForPlayer(Player* player)
{
	if (!player) {
		return false;
	}
	auto groupIt = floorCheckpointPlayerGroups.find(player->getGUID());
	if (groupIt == floorCheckpointPlayerGroups.end()) {
		DBTransaction transaction;
		return transaction.begin() && IOLoginData::savePlayerData(player) && transaction.commit();
	}
	return executeFloorCheckpointGroup(groupIt->second, player);
}

bool Game::flushFloorCheckpointGroups()
{
	// Commit every pending background checkpoint before flushing the remainder
	// synchronously so no in-flight job races the flush.
	if (!drainCheckpointWorker(CHECKPOINT_SYNC_DRAIN_TIMEOUT_MS)) {
		return false;
	}
	while (!floorCheckpointGroups.empty()) {
		const uint64_t groupId = floorCheckpointGroups.begin()->first;
		if (!executeFloorCheckpointGroup(groupId)) {
			return false;
		}
	}
	return true;
}

bool Game::hasInFlightCheckpointForPlayer(uint32_t playerGuid) const
{
	return floorCheckpointInFlightPlayers.find(playerGuid) != floorCheckpointInFlightPlayers.end();
}

void Game::releaseInFlightCheckpointPlayers(const std::set<uint32_t>& playerGuids)
{
	for (uint32_t playerGuid : playerGuids) {
		auto it = floorCheckpointInFlightPlayers.find(playerGuid);
		if (it == floorCheckpointInFlightPlayers.end()) {
			continue;
		}
		if (it->second <= 1) {
			floorCheckpointInFlightPlayers.erase(it);
		} else {
			--it->second;
		}
	}
}

bool Game::drainCheckpointWorker(uint32_t timeoutMs)
{
	// Recover first in case the worker thread died and left work behind.
	recoverDeadCheckpointWorker();

	if (!g_checkpointWorker.isHealthy()) {
		// Unhealthy (stopped or dead). After recovery no pending work can ever
		// commit, so there is nothing left to drain.
		processCheckpointResults();
		return true;
	}
	if (g_checkpointWorker.pendingCount() == 0) {
		processCheckpointResults();
		return true;
	}

	recordDispatcherCheckpointDrain();
	DispatcherPhaseMetricsTimer drainTimer(DispatcherMetricsPhase::FLOOR_CHECKPOINT_DRAIN_WAIT);
	const int64_t deadline = OTSYS_TIME() + timeoutMs;
	while (true) {
		processCheckpointResults();
		// If the worker died while draining, reclaim its work and stop waiting.
		recoverDeadCheckpointWorker();
		if (!g_checkpointWorker.isHealthy()) {
			processCheckpointResults();
			return true;
		}
		if (g_checkpointWorker.pendingCount() == 0) {
			processCheckpointResults();
			return true;
		}
		if (OTSYS_TIME() >= deadline) {
			std::cout << "[Warning - Game::drainCheckpointWorker] timed out waiting for "
			          << g_checkpointWorker.pendingCount() << " checkpoint job(s)." << std::endl;
			return false;
		}
		g_checkpointWorker.waitProgress(50);
	}
}

void Game::recoverDeadCheckpointWorker()
{
	// Reclaim only once the thread is confirmed dead. While it is alive (even
	// if slow) its queued/in-flight jobs may still commit, so they must not be
	// reclaimed.
	if (g_checkpointWorker.isThreadAlive()) {
		return;
	}

	AbortedCheckpointWork aborted = g_checkpointWorker.abortAllPending();
	if (aborted.queuedJobs.empty() && aborted.inFlightPlayerGuids.empty()) {
		return;
	}

	std::cout << "[Warning - Game::recoverDeadCheckpointWorker] checkpoint worker thread "
	          << "died; recovering " << aborted.queuedJobs.size() << " queued checkpoint job(s); "
	          << "checkpoints fall back to the synchronous path." << std::endl;

	for (std::unique_ptr<CheckpointJob>& job : aborted.queuedJobs) {
		auto groupIt = floorCheckpointGroups.find(job->groupId);
		if (groupIt != floorCheckpointGroups.end()) {
			groupIt->second.workerInFlight = false;
		}
		failFloorCheckpointGroup(groupIt != floorCheckpointGroups.end() ? &groupIt->second : nullptr,
			"checkpoint worker thread died before the job executed",
			CheckpointGroupFailureKind::WORKER_ABORTED);
		releaseInFlightCheckpointPlayers(job->playerGuids);
	}

	// Release the reservation of the job that was executing when the thread died.
	releaseInFlightCheckpointPlayers(aborted.inFlightPlayerGuids);
}

void Game::processCheckpointResults()
{
	std::vector<CheckpointResult> results;
	if (g_checkpointWorker.popResults(results) == 0) {
		return;
	}

	for (CheckpointResult& result : results) {
		const CheckpointJob& job = *result.job;
		recordDispatcherCheckpointJobCompleted(result.success);
		recordDispatcherPhase(DispatcherMetricsPhase::FLOOR_CHECKPOINT_WORKER_TOTAL, result.totalNanoseconds);
		recordDispatcherPhase(DispatcherMetricsPhase::FLOOR_CHECKPOINT_WORKER_BEGIN, result.beginNanoseconds);
		recordDispatcherPhase(DispatcherMetricsPhase::FLOOR_CHECKPOINT_WORKER_SQL, result.sqlNanoseconds);
		recordDispatcherPhase(DispatcherMetricsPhase::FLOOR_CHECKPOINT_WORKER_COMMIT, result.commitNanoseconds);

		if (result.success) {
			floorSnapshotStats.queued += job.snapshots.size();
			completePreparedFloorSnapshots(job.snapshots);
			floorSnapshotStats.checkpointTilesSaved += job.snapshots.size();
			floorSnapshotStats.checkpointPlayersSaved += job.playerGuids.size();
			floorSnapshotStats.checkpointHousesSaved += job.houseIds.size();
			++floorSnapshotStats.checkpointGroupsSucceeded;
			recordDispatcherCheckpointGroupSaved(job.snapshots.size(), job.playerGuids.size());
			removeFloorCheckpointGroup(job.groupId);
		} else {
			auto groupIt = floorCheckpointGroups.find(job.groupId);
			failFloorCheckpointGroup(groupIt != floorCheckpointGroups.end() ? &groupIt->second : nullptr,
				result.error, CheckpointGroupFailureKind::WORKER);
		}

		auto releaseIt = floorCheckpointGroups.find(job.groupId);
		if (releaseIt != floorCheckpointGroups.end()) {
			releaseIt->second.workerInFlight = false;
		}
		releaseInFlightCheckpointPlayers(job.playerGuids);
	}
}

void Game::failFloorCheckpointGroup(FloorCheckpointGroup* group, const std::string& error,
                                    CheckpointGroupFailureKind kind)
{
	if (group) {
		group->lastError = error;
		++group->retryCount;
		group->retryNotBefore = OTSYS_TIME() + static_cast<int64_t>(floorSnapshotRetryMs) *
			std::min<uint32_t>(group->retryCount, 6);
	}

	if (kind == CheckpointGroupFailureKind::SERIALIZATION) {
		++floorSnapshotStats.serializationFailed;
	} else if (kind == CheckpointGroupFailureKind::TRANSACTION ||
	           kind == CheckpointGroupFailureKind::WORKER) {
		++floorSnapshotStats.failed;
	}
	++floorSnapshotStats.checkpointGroupsFailed;
	floorSnapshotStats.lastError = error;
	recordDispatcherCheckpointGroupFailure(kind);

	if (!group) {
		std::cout << "[Warning - Game::failFloorCheckpointGroup] checkpoint failed ("
		          << checkpointGroupFailureKindLabel(kind)
		          << ") for a group that no longer exists: " << error << std::endl;
		return;
	}

	// Escalate to a loud alert once the group is stuck, then re-alert every
	// fourth attempt so a permanently failing group keeps showing up.
	const bool stuckEscalation = group->retryCount >= CHECKPOINT_STUCK_ALERT_RETRY_THRESHOLD &&
		(group->retryCount - CHECKPOINT_STUCK_ALERT_RETRY_THRESHOLD) % 4 == 0;
	if (stuckEscalation) {
		++floorSnapshotStats.checkpointStuckAlerts;

		std::string participants = "none";
		if (!group->playerGuids.empty()) {
			participants.clear();
			for (const uint32_t playerGuid : group->playerGuids) {
				if (!participants.empty()) {
					participants += ", ";
				}
				const Player* participant = getPlayerByGUID(playerGuid);
				if (participant) {
					participants += participant->getName();
					participants += " (" + std::to_string(playerGuid) + ")";
				} else {
					participants += std::to_string(playerGuid);
				}
			}
		}

		std::cout << "[ALERT - Game::failFloorCheckpointGroup] checkpoint group " << group->id
		          << " is STUCK: " << group->retryCount << " consecutive failure(s) ("
		          << checkpointGroupFailureKindLabel(kind) << "), pending for "
		          << ((OTSYS_TIME() - group->firstModifiedMonotonic) / 1000)
		          << "s; participants=[" << participants << "] tiles=" << group->positions.size()
		          << " houses=" << group->houseIds.size()
		          << "; merged saves are held until this group commits. Last error: " << error
		          << std::endl;
		return;
	}

	std::cout << "[Warning - Game::failFloorCheckpointGroup] checkpoint group " << group->id
	          << " failed (attempt " << group->retryCount << ", "
	          << checkpointGroupFailureKindLabel(kind) << "): " << error << std::endl;
}

bool Game::buildFloorRecoveryPlan()
{
	floorRecoveryPlan = FloorRecoveryPlan {};
	floorRecoveryPlan.evaluated = true;
	const auto validationStarted = std::chrono::steady_clock::now();
	auto finishValidation = [&]() {
		floorRecoveryPlan.validationMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - validationStarted).count());
	};
	auto blockPlan = [&](const std::string& reason, bool databaseFailure) {
		floorRecoveryPlan.mode = "RECOVERY_BLOCKED";
		floorRecoveryPlan.reason = reason;
		if (floorRecoveryPlan.validationError.empty()) {
			floorRecoveryPlan.validationError = reason;
		}
		finishValidation();
		return !databaseFailure;
	};

	Database& database = Database::getInstance();
	DBResult_ptr newestSessionResult = database.storeQuery(fmt::format(
		"SELECT COALESCE(MAX(`id`),0) AS `newest_session_id` FROM `floor_persistence_save_sessions` "
		"WHERE `world_id`={:d} AND `generation_id`={:d}",
		floorSnapshotWorldId, floorSnapshotGenerationId));
	if (!newestSessionResult) {
		return blockPlan("could not query the newest floor persistence session", true);
	}
	floorRecoveryPlan.databaseAvailable = true;
	floorRecoveryPlan.newestSessionId = newestSessionResult->getNumber<uint64_t>("newest_session_id");

	DBResult_ptr sourceIdResult = database.storeQuery(fmt::format(
		"SELECT COALESCE(MAX(`s`.`id`),0) AS `source_session_id` "
		"FROM `floor_persistence_save_sessions` AS `s` "
		"LEFT JOIN (SELECT DISTINCT `save_session_id` FROM `floor_persistence_snapshots` "
		"WHERE `world_id`={0:d} AND `generation_id`={1:d}) AS `ss` ON `ss`.`save_session_id`=`s`.`id` "
		"LEFT JOIN (SELECT DISTINCT `save_session_id` FROM `floor_persistence_checkpoints` "
		"WHERE `world_id`={0:d} AND `generation_id`={1:d}) AS `cp` ON `cp`.`save_session_id`=`s`.`id` "
		"WHERE `s`.`world_id`={0:d} AND `s`.`generation_id`={1:d} "
		"AND (`s`.`state`<>'RUNNING' OR `ss`.`save_session_id` IS NOT NULL OR `cp`.`save_session_id` IS NOT NULL)",
		floorSnapshotWorldId, floorSnapshotGenerationId));
	if (!sourceIdResult) {
		return blockPlan("could not select a meaningful floor recovery session", true);
	}
	floorRecoveryPlan.sourceSessionId = sourceIdResult->getNumber<uint64_t>("source_session_id");

	if (floorRecoveryPlan.sourceSessionId != 0) {
		DBResult_ptr sourceResult = database.storeQuery(fmt::format(
			"SELECT `s`.`state`,`s`.`player_count`,`s`.`tile_count`,`s`.`error`,"
			"COALESCE(CAST(UNIX_TIMESTAMP(`s`.`started_at`) AS UNSIGNED),0) AS `started_at_epoch`,"
			"COALESCE(CAST(UNIX_TIMESTAMP(`s`.`updated_at`) AS UNSIGNED),0) AS `updated_at_epoch`,"
			"COALESCE(CAST(UNIX_TIMESTAMP(`s`.`committed_at`) AS UNSIGNED),0) AS `committed_at_epoch`,"
			"(SELECT COUNT(*) FROM `floor_persistence_snapshots` AS `ss` "
			"WHERE `ss`.`world_id`={0:d} AND `ss`.`generation_id`={1:d} AND `ss`.`save_session_id`=`s`.`id`) AS `snapshot_rows`,"
			"(SELECT COUNT(*) FROM `floor_persistence_checkpoints` AS `cp` "
			"WHERE `cp`.`world_id`={0:d} AND `cp`.`generation_id`={1:d} AND `cp`.`save_session_id`=`s`.`id`) AS `checkpoint_rows`,"
			"(SELECT COUNT(*) FROM `floor_persistence_checkpoints` AS `cp` "
			"WHERE `cp`.`world_id`={0:d} AND `cp`.`generation_id`={1:d} AND `cp`.`save_session_id`=`s`.`id` "
			"AND `cp`.`state`='COMMITTED') AS `committed_checkpoints`,"
			"(SELECT COUNT(*) FROM `floor_persistence_checkpoints` AS `cp` "
			"WHERE `cp`.`world_id`={0:d} AND `cp`.`generation_id`={1:d} AND `cp`.`save_session_id`=`s`.`id` "
			"AND `cp`.`checkpoint_group_id`=0 AND `cp`.`state`='COMMITTED') AS `clean_checkpoints`,"
			"COALESCE((SELECT MAX(`cp`.`tile_count`) FROM `floor_persistence_checkpoints` AS `cp` "
			"WHERE `cp`.`world_id`={0:d} AND `cp`.`generation_id`={1:d} AND `cp`.`save_session_id`=`s`.`id` "
			"AND `cp`.`checkpoint_group_id`=0 AND `cp`.`state`='COMMITTED'),0) AS `clean_checkpoint_tiles` "
			"FROM `floor_persistence_save_sessions` AS `s` WHERE `s`.`id`={2:d} "
			"AND `s`.`world_id`={0:d} AND `s`.`generation_id`={1:d} LIMIT 1",
			floorSnapshotWorldId, floorSnapshotGenerationId, floorRecoveryPlan.sourceSessionId));
		if (!sourceResult) {
			return blockPlan("the selected floor recovery session could not be read", true);
		}

		floorRecoveryPlan.sourceState = sourceResult->getString("state");
		floorRecoveryPlan.sourcePlayerCount = sourceResult->getNumber<uint64_t>("player_count");
		floorRecoveryPlan.sourceTileCount = sourceResult->getNumber<uint64_t>("tile_count");
		floorRecoveryPlan.sourceError = sourceResult->getString("error");
		floorRecoveryPlan.sourceStartedAt = sourceResult->getNumber<int64_t>("started_at_epoch");
		floorRecoveryPlan.sourceUpdatedAt = sourceResult->getNumber<int64_t>("updated_at_epoch");
		floorRecoveryPlan.sourceCommittedAt = sourceResult->getNumber<int64_t>("committed_at_epoch");
		floorRecoveryPlan.sourceSessionSnapshotRows = sourceResult->getNumber<uint64_t>("snapshot_rows");
		floorRecoveryPlan.sourceCheckpointCount = sourceResult->getNumber<uint64_t>("checkpoint_rows");
		floorRecoveryPlan.sourceCommittedCheckpointCount = sourceResult->getNumber<uint64_t>("committed_checkpoints");
		floorRecoveryPlan.sourceCleanCheckpointCount = sourceResult->getNumber<uint64_t>("clean_checkpoints");
		floorRecoveryPlan.sourceCleanCheckpointTileCount = sourceResult->getNumber<uint64_t>("clean_checkpoint_tiles");
	}

	if (floorRecoveryPlan.newestSessionId != 0 &&
	    floorRecoveryPlan.newestSessionId != floorRecoveryPlan.sourceSessionId) {
		DBResult_ptr ignoredResult = database.storeQuery(fmt::format(
			"SELECT COUNT(*) AS `ignored_sessions` FROM `floor_persistence_save_sessions` "
			"WHERE `world_id`={:d} AND `generation_id`={:d} AND `id`>{:d}",
			floorSnapshotWorldId, floorSnapshotGenerationId, floorRecoveryPlan.sourceSessionId));
		if (!ignoredResult) {
			return blockPlan("could not count empty sessions ignored by recovery selection", true);
		}
		floorRecoveryPlan.ignoredEmptySessions = ignoredResult->getNumber<uint64_t>("ignored_sessions");
	}

	DBResult_ptr snapshotCountResult = database.storeQuery(fmt::format(
		"SELECT COUNT(*) AS `snapshot_rows` FROM `floor_persistence_snapshots` "
		"WHERE `world_id`={:d} AND `generation_id`={:d}",
		floorSnapshotWorldId, floorSnapshotGenerationId));
	if (!snapshotCountResult) {
		return blockPlan("could not count materialized floor snapshots", true);
	}
	const uint64_t expectedSnapshotRows = snapshotCountResult->getNumber<uint64_t>("snapshot_rows");
	floorRecoveryPlan.snapshotRows = expectedSnapshotRows;

	if (expectedSnapshotRows != 0) {
		DBResult_ptr snapshotResult = database.storeQuery(fmt::format(
			"SELECT `tile_x`,`tile_y`,`tile_z`,`format_version`,`policy_version`,`item_count`,"
			"`top_item_count`,`serialized_bytes`,`death_bundle_count`,`identity_missing_count`,"
			"`identity_invalid_count`,`player_corpse_count`,`checksum`,`serialized_data`,"
			"`city_cleanup_filtered` FROM `floor_persistence_snapshots` "
			"WHERE `world_id`={:d} AND `generation_id`={:d} ORDER BY `tile_z`,`tile_y`,`tile_x`",
			floorSnapshotWorldId, floorSnapshotGenerationId));
		if (!snapshotResult) {
			return blockPlan("materialized floor snapshots could not be read", true);
		}

		uint64_t seenRows = 0;
		do {
			++seenRows;
			const Position position(
				snapshotResult->getNumber<uint16_t>("tile_x"),
				snapshotResult->getNumber<uint16_t>("tile_y"),
				static_cast<uint8_t>(snapshotResult->getNumber<uint16_t>("tile_z")));
			const uint16_t formatVersion = snapshotResult->getNumber<uint16_t>("format_version");
			const uint16_t policyVersion = snapshotResult->getNumber<uint16_t>("policy_version");
			const uint32_t storedItemCount = snapshotResult->getNumber<uint32_t>("item_count");
			const uint32_t storedTopItemCount = snapshotResult->getNumber<uint32_t>("top_item_count");
			const uint32_t storedBytes = snapshotResult->getNumber<uint32_t>("serialized_bytes");
			const uint32_t identityMissing = snapshotResult->getNumber<uint32_t>("identity_missing_count");
			const uint32_t identityInvalid = snapshotResult->getNumber<uint32_t>("identity_invalid_count");
			const std::string storedChecksum = snapshotResult->getString("checksum");
			unsigned long storedDataSize = 0;
			const char* storedDataPointer = snapshotResult->getStream("serialized_data", storedDataSize);
			const std::string storedData(storedDataPointer ? storedDataPointer : "", storedDataSize);

			floorRecoveryPlan.itemCount += storedItemCount;
			floorRecoveryPlan.topItemCount += storedTopItemCount;
			floorRecoveryPlan.serializedBytes += storedBytes;
			floorRecoveryPlan.deathBundleCount += snapshotResult->getNumber<uint32_t>("death_bundle_count");
			floorRecoveryPlan.playerCorpseCount += snapshotResult->getNumber<uint32_t>("player_corpse_count");
			floorRecoveryPlan.identityMissingCount += identityMissing;
			floorRecoveryPlan.identityInvalidCount += identityInvalid;
			if (snapshotResult->getNumber<uint16_t>("city_cleanup_filtered") != 0) {
				++floorRecoveryPlan.cityFilteredRows;
			}

			const bool formatValid = formatVersion == FLOOR_SNAPSHOT_FORMAT_VERSION;
			const bool policyValid = policyVersion == FLOOR_SNAPSHOT_POLICY_VERSION;
			const bool sizeValid = storedBytes == storedDataSize && storedDataSize <= FLOOR_SNAPSHOT_MAX_BYTES;
			const bool checksumValid = storedChecksum == FloorPersistenceSerializer::checksum(storedData);
			uint32_t decodedItems = 0;
			uint32_t decodedTopItems = 0;
			std::string blobError;
			const bool blobValid = FloorPersistenceSerializer::validateSnapshot(
				storedData, position, decodedItems, decodedTopItems, blobError);
			const bool countersValid = decodedItems == storedItemCount && decodedTopItems == storedTopItemCount;
			const bool discardableLegacyIdentity =
				identityMissing != 0 && identityInvalid == 0 &&
				formatValid && policyValid && sizeValid && checksumValid && blobValid && countersValid &&
				FloorPersistenceSerializer::hasOnlyDiscardableLegacyIdentityProblems(
					storedData, position, identityMissing, identityInvalid);
			const bool identityValid =
				(identityMissing == 0 && identityInvalid == 0) || discardableLegacyIdentity;
			if (discardableLegacyIdentity) {
				std::cout << "Floor recovery compatibility: terminal empty player corpse at "
				          << position.x << ',' << position.y << ',' << static_cast<uint16_t>(position.z)
				          << " will be discarded instead of blocking recovery." << std::endl;
			}

			if (!formatValid) {
				++floorRecoveryPlan.formatMismatchRows;
			}
			if (!policyValid) {
				++floorRecoveryPlan.policyMismatchRows;
			}
			if (!sizeValid) {
				++floorRecoveryPlan.sizeMismatchRows;
			}
			if (!checksumValid) {
				++floorRecoveryPlan.checksumMismatchRows;
			}
			if (!blobValid) {
				++floorRecoveryPlan.blobInvalidRows;
			}
			if (!countersValid) {
				++floorRecoveryPlan.counterMismatchRows;
			}
			if (!identityValid) {
				++floorRecoveryPlan.identityProblemRows;
			}

			const bool rowValid = formatValid && policyValid && sizeValid && checksumValid && blobValid &&
				countersValid && identityValid;
			if (rowValid) {
				++floorRecoveryPlan.validRows;
			} else {
				++floorRecoveryPlan.invalidRows;
				if (floorRecoveryPlan.validationError.empty()) {
					std::string rowError = blobError;
					if (rowError.empty()) {
						if (!formatValid) {
							rowError = "snapshot format version mismatch";
						} else if (!policyValid) {
							rowError = "snapshot policy version mismatch";
						} else if (!sizeValid) {
							rowError = "snapshot byte count mismatch";
						} else if (!checksumValid) {
							rowError = "snapshot checksum mismatch";
						} else if (!countersValid) {
							rowError = "snapshot item counters mismatch";
						} else {
							rowError = "snapshot contains missing or invalid item identities";
						}
					}
					floorRecoveryPlan.validationError = fmt::format("tile {:d},{:d},{:d}: {:s}",
						position.x, position.y, position.z, rowError);
				}
			}
		} while (snapshotResult->next());

		if (seenRows != expectedSnapshotRows) {
			return blockPlan("snapshot row count changed during recovery validation", false);
		}
	}

	if (floorRecoveryPlan.sourceSessionId == 0) {
		if (floorRecoveryPlan.snapshotRows == 0) {
			floorRecoveryPlan.mode = "NOTHING_TO_RECOVER";
			floorRecoveryPlan.reason = "no meaningful session or materialized snapshot exists for this generation";
		} else {
			return blockPlan("snapshot rows exist without a meaningful recovery session", false);
		}
	} else if (floorRecoveryPlan.sourceState == "CLEAN_COMMITTED") {
		const bool cleanCheckpointValid = floorRecoveryPlan.sourceCommittedAt != 0 &&
			floorRecoveryPlan.sourceError.empty() && floorRecoveryPlan.sourceCleanCheckpointCount == 1 &&
			floorRecoveryPlan.sourceCleanCheckpointTileCount == floorRecoveryPlan.sourceTileCount;
		if (!cleanCheckpointValid) {
			return blockPlan("CLEAN_COMMITTED session is missing its matching atomic clean checkpoint", false);
		}
		floorRecoveryPlan.mode = "CLEAN_RESTART";
		floorRecoveryPlan.reason = "previous clean save committed with a matching atomic clean checkpoint";
	} else if (floorRecoveryPlan.sourceState == "RUNNING") {
		floorRecoveryPlan.mode = "CRASH_RECOVERY";
		floorRecoveryPlan.reason = "previous meaningful runtime session ended without a clean commit";
	} else if (floorRecoveryPlan.sourceState == "CLEAN_PREPARING") {
		floorRecoveryPlan.mode = "CRASH_RECOVERY";
		floorRecoveryPlan.reason = "clean save was interrupted while preparing its atomic checkpoint";
	} else if (floorRecoveryPlan.sourceState == "CLEAN_FAILED") {
		floorRecoveryPlan.mode = "CRASH_RECOVERY";
		floorRecoveryPlan.reason = "previous clean save failed; use the last unfiltered durable snapshots";
	} else {
		return blockPlan(fmt::format("unsupported recovery session state: {:s}", floorRecoveryPlan.sourceState), false);
	}

	if (floorRecoveryPlan.invalidRows != 0) {
		const std::string firstError = floorRecoveryPlan.validationError;
		const bool blocked = blockPlan(fmt::format("{:d} materialized snapshot row(s) failed validation",
			floorRecoveryPlan.invalidRows), false);
		floorRecoveryPlan.validationError = firstError;
		return blocked;
	}

	finishValidation();
	const bool dryRunBuilt = buildFloorRecoveryDryRun();
	floorRecoveryPlan.applyEnabled = dryRunBuilt && floorRecoveryPlan.dryRunReady &&
		floorRecoveryPlan.reconciliationReady && floorRecoveryPlan.quarantineReady &&
		(floorRecoveryPlan.mode == "CLEAN_RESTART" || floorRecoveryPlan.mode == "CRASH_RECOVERY");
	std::cout << "Floor recovery selector stage 4: mode=" << floorRecoveryPlan.mode
	          << " source=" << floorRecoveryPlan.sourceSessionId << '/' << floorRecoveryPlan.sourceState
	          << " rows=" << floorRecoveryPlan.snapshotRows << " valid=" << floorRecoveryPlan.validRows
	          << " invalid=" << floorRecoveryPlan.invalidRows << '.' << std::endl;
	if (floorRecoveryPlan.dryRunEvaluated) {
		std::cout << "Floor recovery stage 5 dry-run: ready=" << (floorRecoveryPlan.dryRunReady ? "yes" : "no")
		          << " restore=" << floorRecoveryPlan.dryRunRestoreItemCount
		          << " quarantine=" << floorRecoveryPlan.dryRunQuarantineItemCount
		          << " rejected=" << floorRecoveryPlan.dryRunRejectedItemCount
		          << " duplicates=" << floorRecoveryPlan.dryRunDuplicateIdentityCount;
		if (floorRecoveryPlan.mode == "CLEAN_RESTART") {
			std::cout << ". Clean restart map apply will run automatically before login." << std::endl;
		} else {
			std::cout << ". Map apply is explicit and requires the selected source id." << std::endl;
		}
	}
	if (floorRecoveryPlan.reconciliationEvaluated) {
		std::cout << "Floor recovery stage 5.3 reconciliation: ready="
		          << (floorRecoveryPlan.reconciliationReady ? "yes" : "no")
		          << " player_identities=" << floorRecoveryPlan.playerIdentityCount
		          << " floor_only=" << floorRecoveryPlan.floorOnlyIdentityCount
		          << " player_matches=" << floorRecoveryPlan.floorPlayerIdentityMatchCount
		          << " duplicates=" << floorRecoveryPlan.playerDuplicateIdentityCount
		          << ". Storage scan is read-only." << std::endl;
	}
	if (floorRecoveryPlan.quarantineEvaluated) {
		std::cout << "Floor recovery stage 5.4 quarantine: ready="
		          << (floorRecoveryPlan.quarantineReady ? "yes" : "no")
		          << " rows=" << floorRecoveryPlan.quarantinePersistedRows
		          << " stackables=" << floorRecoveryPlan.quarantinePersistedStackableItems
		          << " player_matches=" << floorRecoveryPlan.quarantinePersistedPlayerMatches
		          << " bytes=" << floorRecoveryPlan.quarantinePersistedBytes
		          << ". Quarantine is pending; map apply is explicit." << std::endl;
	}
	return dryRunBuilt;
}

bool Game::buildFloorRecoveryDryRun()
{
	floorRecoveryPlan.dryRunEvaluated = true;
	floorRecoveryPlan.dryRunReady = false;
	floorRecoveryPlan.dryRunError.clear();
	floorRecoverySuppressedInstanceIds.clear();
	const auto dryRunStarted = std::chrono::steady_clock::now();
	auto finishDryRun = [&]() {
		floorRecoveryPlan.dryRunMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - dryRunStarted).count());
	};
	auto failDryRun = [&](const std::string& error) {
		if (floorRecoveryPlan.dryRunError.empty()) {
			floorRecoveryPlan.dryRunError = error;
		}
		finishDryRun();
		return false;
	};

	if (floorRecoveryPlan.mode == "NOTHING_TO_RECOVER") {
		floorRecoveryPlan.dryRunReady = true;
		finishDryRun();
		return true;
	}

	FloorRecoverySnapshotMode recoveryMode;
	if (floorRecoveryPlan.mode == "CLEAN_RESTART") {
		recoveryMode = FloorRecoverySnapshotMode::CLEAN_RESTART;
	} else if (floorRecoveryPlan.mode == "CRASH_RECOVERY") {
		recoveryMode = FloorRecoverySnapshotMode::CRASH_RECOVERY;
	} else {
		return failDryRun(fmt::format("stage 5 dry-run cannot analyze recovery mode {:s}", floorRecoveryPlan.mode));
	}

	if (floorRecoveryPlan.snapshotRows == 0) {
		if (floorRecoveryPlan.mode != "CLEAN_RESTART") {
			return failDryRun("selected crash recovery mode has no materialized snapshot rows");
		}

		const std::unordered_map<std::string, std::string> emptyFloorIdentities;
		if (!buildFloorRecoveryPlayerReconciliation(emptyFloorIdentities)) {
			return failDryRun(floorRecoveryPlan.reconciliationError.empty() ?
				"stage 5 empty clean restart player storage reconciliation failed" :
				floorRecoveryPlan.reconciliationError);
		}
		if (!materializeFloorRecoveryQuarantine()) {
			return failDryRun(floorRecoveryPlan.quarantineError.empty() ?
				"stage 5 empty clean restart quarantine preparation failed" :
				floorRecoveryPlan.quarantineError);
		}

		floorRecoveryPlan.dryRunReady = true;
		finishDryRun();
		return true;
	}

	Database& database = Database::getInstance();
	DBResult_ptr snapshotResult = database.storeQuery(fmt::format(
		"SELECT `tile_x`,`tile_y`,`tile_z`,`serialized_data` FROM `floor_persistence_snapshots` "
		"WHERE `world_id`={:d} AND `generation_id`={:d} ORDER BY `tile_z`,`tile_y`,`tile_x`",
		floorSnapshotWorldId, floorSnapshotGenerationId));
	if (!snapshotResult) {
		return failDryRun("stage 5 dry-run could not read materialized floor snapshots");
	}

	std::unordered_map<std::string, std::string> identityPositions;
	do {
		const Position position(
			snapshotResult->getNumber<uint16_t>("tile_x"),
			snapshotResult->getNumber<uint16_t>("tile_y"),
			static_cast<uint8_t>(snapshotResult->getNumber<uint16_t>("tile_z")));
		unsigned long storedDataSize = 0;
		const char* storedDataPointer = snapshotResult->getStream("serialized_data", storedDataSize);
		const std::string storedData(storedDataPointer ? storedDataPointer : "", storedDataSize);

		FloorRecoverySnapshotAnalysis analysis;
		std::string analysisError;
		if (!FloorPersistenceSerializer::analyzeRecoverySnapshot(
				storedData, position, recoveryMode, analysis, analysisError)) {
			return failDryRun(fmt::format("tile {:d},{:d},{:d}: {:s}", position.x, position.y, position.z,
				analysisError.empty() ? "recovery snapshot analysis failed" : analysisError));
		}

		++floorRecoveryPlan.dryRunRows;
		floorRecoveryPlan.dryRunItemCount += analysis.itemCount;
		floorRecoveryPlan.dryRunTopItemCount += analysis.topItemCount;
		floorRecoveryPlan.dryRunRestoreItemCount += analysis.restoreItemCount;
		floorRecoveryPlan.dryRunQuarantineItemCount += analysis.quarantineItemCount;
		floorRecoveryPlan.dryRunRejectedItemCount += analysis.rejectedItemCount;
		floorRecoveryPlan.dryRunRestoreTopItemCount += analysis.restoreTopItemCount;
		floorRecoveryPlan.dryRunQuarantineTopItemCount += analysis.quarantineTopItemCount;
		floorRecoveryPlan.dryRunRejectedTopItemCount += analysis.rejectedTopItemCount;
		floorRecoveryPlan.dryRunPersistAlwaysCount += analysis.persistAlwaysCount;
		floorRecoveryPlan.dryRunPersistCleanOnlyCount += analysis.persistCleanOnlyCount;
		floorRecoveryPlan.dryRunPersistFoodCount += analysis.persistFoodCount;
		floorRecoveryPlan.dryRunDeathBundleCount += analysis.deathBundleCount;
		floorRecoveryPlan.dryRunContainerCount += analysis.containerCount;
		floorRecoveryPlan.dryRunMaxDepth = std::max<uint64_t>(floorRecoveryPlan.dryRunMaxDepth, analysis.maxDepth);

		const std::string currentPosition = fmt::format("{:d},{:d},{:d}", position.x, position.y, position.z);
		for (const std::string& instanceId : analysis.instanceIds) {
			++floorRecoveryPlan.dryRunIdentityCount;
			auto result = identityPositions.emplace(instanceId, currentPosition);
			if (!result.second) {
				++floorRecoveryPlan.dryRunDuplicateIdentityCount;
				if (floorRecoveryPlan.dryRunError.empty()) {
					floorRecoveryPlan.dryRunError = fmt::format(
						"duplicate floor instance identity appears at tiles {:s} and {:s}",
						result.first->second, currentPosition);
				}
			}
		}
	} while (snapshotResult->next());

	if (floorRecoveryPlan.dryRunRows != floorRecoveryPlan.snapshotRows) {
		return failDryRun("materialized snapshot row count changed during stage 5 dry-run");
	}
	if (floorRecoveryPlan.dryRunItemCount != floorRecoveryPlan.itemCount ||
	    floorRecoveryPlan.dryRunTopItemCount != floorRecoveryPlan.topItemCount) {
		return failDryRun("stage 5 decoded item counters differ from stage 4 validation");
	}
	if (floorRecoveryPlan.dryRunRejectedItemCount != 0) {
		return failDryRun("stage 5 recovery policy rejected one or more decoded items");
	}
	if (floorRecoveryPlan.dryRunDuplicateIdentityCount != 0) {
		return failDryRun(floorRecoveryPlan.dryRunError.empty() ?
			"stage 5 found duplicate floor instance identities" : floorRecoveryPlan.dryRunError);
	}
	if (!buildFloorRecoveryPlayerReconciliation(identityPositions)) {
		return failDryRun(floorRecoveryPlan.reconciliationError.empty() ?
			"stage 5 player storage reconciliation failed" : floorRecoveryPlan.reconciliationError);
	}
	if (!materializeFloorRecoveryQuarantine()) {
		return failDryRun(floorRecoveryPlan.quarantineError.empty() ?
			"stage 5 quarantine materialization failed" : floorRecoveryPlan.quarantineError);
	}

	floorRecoveryPlan.dryRunReady = true;
	finishDryRun();
	return true;
}

bool Game::buildFloorRecoveryPlayerReconciliation(
	const std::unordered_map<std::string, std::string>& floorIdentityPositions)
{
	floorRecoveryPlan.reconciliationEvaluated = true;
	floorRecoveryPlan.reconciliationReady = false;
	floorRecoveryPlan.reconciliationError.clear();
	floorRecoveryPlan.reconciliationFirstMatch.clear();
	const auto reconciliationStarted = std::chrono::steady_clock::now();
	auto finishReconciliation = [&]() {
		floorRecoveryPlan.reconciliationMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - reconciliationStarted).count());
	};
	auto failReconciliation = [&](const std::string& error) {
		if (floorRecoveryPlan.reconciliationError.empty()) {
			floorRecoveryPlan.reconciliationError = error;
		}
		finishReconciliation();
		return false;
	};

	struct PlayerIdentityRecord {
		uint64_t occurrences = 0;
		std::string firstLocation;
	};
	struct StorageSource {
		const char* table;
		const char* label;
		uint64_t* identityCounter;
	};

	const std::array<StorageSource, 5> storageSources {{
		{"player_items", "inventory", &floorRecoveryPlan.inventoryIdentityCount},
		{"player_depotlockeritems", "depot_locker", &floorRecoveryPlan.depotLockerIdentityCount},
		{"player_depotitems", "depot", &floorRecoveryPlan.depotIdentityCount},
		{"player_inboxitems", "inbox", &floorRecoveryPlan.inboxIdentityCount},
		{"player_storeinboxitems", "store_inbox", &floorRecoveryPlan.storeInboxIdentityCount},
	}};

	Database& database = Database::getInstance();
	std::unordered_map<std::string, PlayerIdentityRecord> playerIdentities;
	for (const StorageSource& storage : storageSources) {
		DBResult_ptr result = database.storeQuery(fmt::format(
			"SELECT `player_id`,`pid`,`sid`,`itemtype`,`count`,`attributes`,0 AS `sentinel` "
			"FROM `{0:s}` WHERE LOCATE('{1:s}',`attributes`)>0 "
			"UNION ALL SELECT 0,0,0,0,0,'' AS `attributes`,1 AS `sentinel`",
			storage.table, ITEM_CUSTOM_ATTRIBUTE_FLOOR_INSTANCE_ID));
		if (!result) {
			return failReconciliation(fmt::format("could not scan {:s} for player item identities", storage.label));
		}

		do {
			if (result->getNumber<uint16_t>("sentinel") != 0) {
				continue;
			}

			++floorRecoveryPlan.reconciliationCandidateRows;
			const uint32_t playerId = result->getNumber<uint32_t>("player_id");
			const uint32_t pid = result->getNumber<uint32_t>("pid");
			const uint32_t sid = result->getNumber<uint32_t>("sid");
			const uint16_t itemType = result->getNumber<uint16_t>("itemtype");
			const uint16_t count = result->getNumber<uint16_t>("count");
			const std::string location = fmt::format(
				"{:s} player={:d} sid={:d} pid={:d} item={:d}",
				storage.label, playerId, sid, pid, itemType);

			if (itemType >= Item::items.size() || Item::items[itemType].id == 0) {
				++floorRecoveryPlan.reconciliationInvalidRows;
				if (floorRecoveryPlan.reconciliationError.empty()) {
					floorRecoveryPlan.reconciliationError = "unknown item type at " + location;
				}
				continue;
			}

			unsigned long attributesSize = 0;
			const char* attributes = result->getStream("attributes", attributesSize);
			PropStream propStream;
			propStream.init(attributes ? attributes : "", attributesSize);
			std::unique_ptr<Item> item(Item::CreateItem(itemType, count));
			if (!item || !item->unserializeAttr(propStream) || propStream.size() != 0) {
				++floorRecoveryPlan.reconciliationInvalidRows;
				if (floorRecoveryPlan.reconciliationError.empty()) {
					floorRecoveryPlan.reconciliationError = "invalid item attributes at " + location;
				}
				continue;
			}
			++floorRecoveryPlan.reconciliationDecodedRows;

			if (!item->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_FLOOR_INSTANCE_ID)) {
				++floorRecoveryPlan.reconciliationFalsePositiveRows;
				continue;
			}
			const std::string instanceId = item->getFloorPersistenceInstanceId();
			if (instanceId.empty()) {
				++floorRecoveryPlan.reconciliationInvalidRows;
				if (floorRecoveryPlan.reconciliationError.empty()) {
					floorRecoveryPlan.reconciliationError = "invalid floor instance identity at " + location;
				}
				continue;
			}

			++floorRecoveryPlan.playerIdentityCount;
			++(*storage.identityCounter);
			PlayerIdentityRecord& identity = playerIdentities[instanceId];
			if (identity.occurrences++ == 0) {
				identity.firstLocation = location;
			} else {
				++floorRecoveryPlan.playerDuplicateIdentityCount;
				if (floorRecoveryPlan.reconciliationError.empty()) {
					floorRecoveryPlan.reconciliationError = fmt::format(
						"duplicate player identity at {:s} and {:s}", identity.firstLocation, location);
				}
			}
		} while (result->next());
	}

	floorRecoveryPlan.playerUniqueIdentityCount = playerIdentities.size();
	for (const auto& floorIdentity : floorIdentityPositions) {
		auto playerIdentity = playerIdentities.find(floorIdentity.first);
		if (playerIdentity == playerIdentities.end()) {
			++floorRecoveryPlan.floorOnlyIdentityCount;
			continue;
		}

		if (playerIdentity->second.occurrences == 1) {
			++floorRecoveryPlan.floorPlayerIdentityMatchCount;
			floorRecoverySuppressedInstanceIds.insert(floorIdentity.first);
			if (floorRecoveryPlan.reconciliationFirstMatch.empty()) {
				floorRecoveryPlan.reconciliationFirstMatch = fmt::format(
					"floor tile {:s} matches {:s}", floorIdentity.second, playerIdentity->second.firstLocation);
			}
		} else {
			++floorRecoveryPlan.floorPlayerAmbiguousIdentityCount;
		}
	}

	if (floorRecoveryPlan.reconciliationInvalidRows != 0) {
		return failReconciliation(floorRecoveryPlan.reconciliationError.empty() ?
			"one or more player item identity rows are invalid" : floorRecoveryPlan.reconciliationError);
	}
	if (floorRecoveryPlan.playerDuplicateIdentityCount != 0 ||
	    floorRecoveryPlan.floorPlayerAmbiguousIdentityCount != 0) {
		return failReconciliation(floorRecoveryPlan.reconciliationError.empty() ?
			"duplicate player item identities require investigation" : floorRecoveryPlan.reconciliationError);
	}

	floorRecoveryPlan.reconciliationReady = true;
	finishReconciliation();
	return true;
}

bool Game::materializeFloorRecoveryQuarantine()
{
	floorRecoveryPlan.quarantineEvaluated = true;
	floorRecoveryPlan.quarantineReady = false;
	floorRecoveryPlan.quarantineError.clear();
	const auto quarantineStarted = std::chrono::steady_clock::now();
	auto finishQuarantine = [&]() {
		floorRecoveryPlan.quarantineMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - quarantineStarted).count());
	};
	auto failQuarantine = [&](const std::string& error) {
		if (floorRecoveryPlan.quarantineError.empty()) {
			floorRecoveryPlan.quarantineError = error;
		}
		finishQuarantine();
		return false;
	};

	if (floorRecoveryPlan.mode != "CRASH_RECOVERY") {
		floorRecoveryPlan.quarantineReady = true;
		finishQuarantine();
		return true;
	}

	static constexpr uint32_t QUARANTINE_REASON_CRASH_STACKABLE = 1 << 0;
	static constexpr uint32_t QUARANTINE_REASON_PLAYER_MATCH = 1 << 1;
	struct PreparedQuarantine {
		Position position;
		uint64_t tileVersion = 0;
		uint64_t snapshotSaveSessionId = 0;
		uint64_t checkpointGroupId = 0;
		uint16_t formatVersion = 0;
		uint16_t policyVersion = 0;
		uint32_t reasonMask = 0;
		uint32_t quarantineItemCount = 0;
		uint32_t playerMatchItemCount = 0;
		uint32_t snapshotItemCount = 0;
		uint32_t snapshotTopItemCount = 0;
		std::string snapshotUpdatedAt;
		std::string checksum;
		std::string serializedData;
		std::vector<FloorQuarantineManifestItem> manifestItems;
	};

	Database& database = Database::getInstance();
	DBResult_ptr snapshotResult = database.storeQuery(fmt::format(
		"SELECT `tile_x`,`tile_y`,`tile_z`,`tile_version`,`format_version`,`policy_version`,"
		"`item_count`,`top_item_count`,`serialized_bytes`,`checksum`,`serialized_data`,"
		"DATE_FORMAT(`updated_at`,'%Y-%m-%d %H:%i:%s.%f') AS `snapshot_updated_at`,"
		"`checkpoint_group_id`,`save_session_id` FROM `floor_persistence_snapshots` "
		"WHERE `world_id`={:d} AND `generation_id`={:d} ORDER BY `tile_z`,`tile_y`,`tile_x`",
		floorSnapshotWorldId, floorSnapshotGenerationId));
	if (!snapshotResult) {
		return failQuarantine("could not read materialized snapshots for quarantine planning");
	}

	std::vector<PreparedQuarantine> prepared;
	do {
		PreparedQuarantine entry;
		entry.position = Position(
			snapshotResult->getNumber<uint16_t>("tile_x"),
			snapshotResult->getNumber<uint16_t>("tile_y"),
			static_cast<uint8_t>(snapshotResult->getNumber<uint16_t>("tile_z")));
		entry.tileVersion = snapshotResult->getNumber<uint64_t>("tile_version");
		entry.snapshotSaveSessionId = snapshotResult->getNumber<uint64_t>("save_session_id");
		entry.checkpointGroupId = snapshotResult->getNumber<uint64_t>("checkpoint_group_id");
		entry.formatVersion = snapshotResult->getNumber<uint16_t>("format_version");
		entry.policyVersion = snapshotResult->getNumber<uint16_t>("policy_version");
		entry.snapshotItemCount = snapshotResult->getNumber<uint32_t>("item_count");
		entry.snapshotTopItemCount = snapshotResult->getNumber<uint32_t>("top_item_count");
		entry.snapshotUpdatedAt = snapshotResult->getString("snapshot_updated_at");
		const uint32_t storedBytes = snapshotResult->getNumber<uint32_t>("serialized_bytes");
		entry.checksum = snapshotResult->getString("checksum");
		unsigned long storedDataSize = 0;
		const char* storedDataPointer = snapshotResult->getStream("serialized_data", storedDataSize);
		entry.serializedData.assign(storedDataPointer ? storedDataPointer : "", storedDataSize);
		if (storedBytes != storedDataSize) {
			return failQuarantine(fmt::format("tile {:d},{:d},{:d}: quarantine byte count mismatch",
				entry.position.x, entry.position.y, entry.position.z));
		}

		FloorRecoverySnapshotAnalysis analysis;
		std::string analysisError;
		if (!FloorPersistenceSerializer::analyzeRecoverySnapshot(
				entry.serializedData, entry.position, FloorRecoverySnapshotMode::CRASH_RECOVERY,
				analysis, analysisError)) {
			return failQuarantine(fmt::format("tile {:d},{:d},{:d}: {:s}",
				entry.position.x, entry.position.y, entry.position.z,
				analysisError.empty() ? "quarantine analysis failed" : analysisError));
		}

		entry.quarantineItemCount = analysis.quarantineItemCount;
		for (const std::string& instanceId : analysis.instanceIds) {
			if (floorRecoverySuppressedInstanceIds.find(instanceId) != floorRecoverySuppressedInstanceIds.end()) {
				++entry.playerMatchItemCount;
			}
		}
		if (entry.quarantineItemCount != 0) {
			entry.reasonMask |= QUARANTINE_REASON_CRASH_STACKABLE;
		}
		if (entry.playerMatchItemCount != 0) {
			entry.reasonMask |= QUARANTINE_REASON_PLAYER_MATCH;
		}
		if (entry.reasonMask == 0) {
			continue;
		}

		std::string manifestError;
		if (!FloorPersistenceSerializer::buildQuarantineManifest(
				entry.serializedData, entry.position, floorRecoverySuppressedInstanceIds,
				entry.manifestItems, manifestError)) {
			return failQuarantine(fmt::format("tile {:d},{:d},{:d}: {:s}",
				entry.position.x, entry.position.y, entry.position.z,
				manifestError.empty() ? "quarantine manifest failed" : manifestError));
		}
		uint32_t manifestStackables = 0;
		uint32_t manifestPlayerMatches = 0;
		for (const FloorQuarantineManifestItem& manifestItem : entry.manifestItems) {
			if (!manifestItem.quarantined) {
				continue;
			}
			if ((manifestItem.reasonMask & QUARANTINE_REASON_CRASH_STACKABLE) != 0) {
				++manifestStackables;
			}
			if ((manifestItem.reasonMask & QUARANTINE_REASON_PLAYER_MATCH) != 0) {
				++manifestPlayerMatches;
			}
		}
		if (manifestStackables != entry.quarantineItemCount ||
		    manifestPlayerMatches != entry.playerMatchItemCount) {
			return failQuarantine(fmt::format(
				"tile {:d},{:d},{:d}: quarantine manifest counters do not match the recovery analysis",
				entry.position.x, entry.position.y, entry.position.z));
		}

		++floorRecoveryPlan.quarantinePlannedRows;
		floorRecoveryPlan.quarantineStackableItemCount += entry.quarantineItemCount;
		floorRecoveryPlan.quarantinePlayerMatchItemCount += entry.playerMatchItemCount;
		floorRecoveryPlan.quarantineSnapshotItemCount += entry.snapshotItemCount;
		floorRecoveryPlan.quarantineSerializedBytes += entry.serializedData.size();
		prepared.push_back(std::move(entry));
	} while (snapshotResult->next());

	DBTransaction transaction;
	if (!transaction.begin()) {
		return failQuarantine("could not start the quarantine materialization transaction");
	}

	DBResult_ptr resolvedResult = database.storeQuery(fmt::format(
		"SELECT COUNT(*) AS `resolved_rows` FROM `floor_persistence_quarantine` "
		"WHERE `world_id`={:d} AND `generation_id`={:d} AND `recovery_source_session_id`={:d} "
		"AND `state`<>'PENDING'",
		floorSnapshotWorldId, floorSnapshotGenerationId, floorRecoveryPlan.sourceSessionId));
	if (!resolvedResult) {
		return failQuarantine("could not verify existing quarantine resolutions");
	}
	if (resolvedResult->getNumber<uint64_t>("resolved_rows") != 0) {
		return failQuarantine("the selected recovery source already has resolved quarantine rows");
	}

	if (!database.executeQuery(fmt::format(
			"UPDATE `floor_persistence_quarantine` SET `active`=0 "
			"WHERE `world_id`={:d} AND `generation_id`={:d} AND `recovery_source_session_id`={:d} "
			"AND `state`='PENDING'",
			floorSnapshotWorldId, floorSnapshotGenerationId, floorRecoveryPlan.sourceSessionId))) {
		return failQuarantine("could not deactivate the previous pending quarantine plan");
	}

	for (const PreparedQuarantine& entry : prepared) {
		const std::string escapedChecksum = database.escapeString(entry.checksum);
		const std::string escapedData = database.escapeBlob(
			entry.serializedData.data(), static_cast<uint32_t>(entry.serializedData.size()));
		if (!database.executeQuery(fmt::format(
				"INSERT INTO `floor_persistence_quarantine` "
				"(`world_id`,`generation_id`,`recovery_source_session_id`,`snapshot_save_session_id`,"
				"`tile_x`,`tile_y`,`tile_z`,`source_tile_version`,`source_checkpoint_group_id`,"
				"`reason_mask`,`quarantine_item_count`,`player_match_item_count`,`snapshot_item_count`,"
				"`snapshot_top_item_count`,`format_version`,`policy_version`,`serialized_bytes`,"
				"`source_snapshot_updated_at`,`checksum`,`serialized_data`,`state`,`active`) VALUES "
				"({:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:s},{:s},{:s},'PENDING',1) "
				"ON DUPLICATE KEY UPDATE `snapshot_save_session_id`=VALUES(`snapshot_save_session_id`),"
				"`source_tile_version`=VALUES(`source_tile_version`),"
				"`source_checkpoint_group_id`=VALUES(`source_checkpoint_group_id`),"
				"`reason_mask`=VALUES(`reason_mask`),`quarantine_item_count`=VALUES(`quarantine_item_count`),"
				"`player_match_item_count`=VALUES(`player_match_item_count`),"
				"`snapshot_item_count`=VALUES(`snapshot_item_count`),"
				"`snapshot_top_item_count`=VALUES(`snapshot_top_item_count`),"
				"`format_version`=VALUES(`format_version`),`policy_version`=VALUES(`policy_version`),"
				"`serialized_bytes`=VALUES(`serialized_bytes`),"
				"`source_snapshot_updated_at`=VALUES(`source_snapshot_updated_at`),"
				"`checksum`=VALUES(`checksum`),"
				"`serialized_data`=VALUES(`serialized_data`),`active`=1,`id`=LAST_INSERT_ID(`id`)",
				floorSnapshotWorldId, floorSnapshotGenerationId, floorRecoveryPlan.sourceSessionId,
				entry.snapshotSaveSessionId, entry.position.x, entry.position.y, entry.position.z,
				entry.tileVersion, entry.checkpointGroupId, entry.reasonMask, entry.quarantineItemCount,
				entry.playerMatchItemCount, entry.snapshotItemCount, entry.snapshotTopItemCount,
				entry.formatVersion, entry.policyVersion, entry.serializedData.size(),
				database.escapeString(entry.snapshotUpdatedAt), escapedChecksum, escapedData))) {
			return failQuarantine(fmt::format("could not persist quarantine tile {:d},{:d},{:d}",
				entry.position.x, entry.position.y, entry.position.z));
		}

		const uint64_t quarantineId = database.getLastInsertId();
		if (quarantineId == 0) {
			return failQuarantine(fmt::format(
				"could not resolve quarantine id for tile {:d},{:d},{:d}",
				entry.position.x, entry.position.y, entry.position.z));
		}
		if (!database.executeQuery(fmt::format(
				"DELETE FROM `floor_persistence_quarantine_items` WHERE `quarantine_id`={:d}",
				quarantineId))) {
			return failQuarantine(fmt::format(
				"could not replace quarantine manifest for tile {:d},{:d},{:d}",
				entry.position.x, entry.position.y, entry.position.z));
		}

		for (const FloorQuarantineManifestItem& item : entry.manifestItems) {
			if (!database.executeQuery(fmt::format(
					"INSERT INTO `floor_persistence_quarantine_items` "
					"(`quarantine_id`,`recovery_source_session_id`,`source_item_index`,"
					"`parent_source_item_index`,`depth`,`item_id`,`item_name`,`item_count`,"
					"`item_subtype`,`is_container`,`container_capacity`,`instance_id`,"
					"`policy_state`,`reason_mask`,`is_quarantined`,`death_bundle`,`player_corpse`,"
					"`action_id`,`unique_id`,`duration_ms`,`last_actor_guid`,`description`,`special_description`,"
					"`written_text`,`writer`,`written_date`) VALUES "
					"({:d},{:d},{:d},{:d},{:d},{:d},{:s},{:d},{:d},{:d},{:d},{:s},{:s},"
					"{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:s},{:s},{:s},{:s},{:d})",
					quarantineId, floorRecoveryPlan.sourceSessionId, item.sourceIndex,
					item.parentSourceIndex, item.depth, item.itemId,
					database.escapeString(item.name), item.itemCount, item.itemSubtype,
					item.container ? 1 : 0, item.containerCapacity,
					database.escapeString(item.instanceId), database.escapeString(item.policy),
					item.reasonMask, item.quarantined ? 1 : 0, item.deathBundle ? 1 : 0,
					item.playerCorpse ? 1 : 0, item.actionId, item.uniqueId, item.durationMs,
					item.lastActorGuid,
					database.escapeString(item.description),
					database.escapeString(item.specialDescription),
					database.escapeString(item.writtenText), database.escapeString(item.writer),
					item.writtenDate))) {
				return failQuarantine(fmt::format(
					"could not persist quarantine item {:d} for tile {:d},{:d},{:d}",
					item.sourceIndex, entry.position.x, entry.position.y, entry.position.z));
			}
		}
	}

	DBResult_ptr aggregateResult = database.storeQuery(fmt::format(
		"SELECT COUNT(*) AS `rows`,COALESCE(SUM(`quarantine_item_count`),0) AS `stackable_items`,"
		"COALESCE(SUM(`player_match_item_count`),0) AS `player_matches`,"
		"COALESCE(SUM(`serialized_bytes`),0) AS `bytes` FROM `floor_persistence_quarantine` "
		"WHERE `world_id`={:d} AND `generation_id`={:d} AND `recovery_source_session_id`={:d} "
		"AND `state`='PENDING' AND `active`=1",
		floorSnapshotWorldId, floorSnapshotGenerationId, floorRecoveryPlan.sourceSessionId));
	if (!aggregateResult) {
		return failQuarantine("could not verify the materialized quarantine plan");
	}
	floorRecoveryPlan.quarantinePersistedRows = aggregateResult->getNumber<uint64_t>("rows");
	floorRecoveryPlan.quarantinePersistedStackableItems = aggregateResult->getNumber<uint64_t>("stackable_items");
	floorRecoveryPlan.quarantinePersistedPlayerMatches = aggregateResult->getNumber<uint64_t>("player_matches");
	floorRecoveryPlan.quarantinePersistedBytes = aggregateResult->getNumber<uint64_t>("bytes");
	if (floorRecoveryPlan.quarantinePersistedRows != floorRecoveryPlan.quarantinePlannedRows ||
	    floorRecoveryPlan.quarantinePersistedStackableItems != floorRecoveryPlan.quarantineStackableItemCount ||
	    floorRecoveryPlan.quarantinePersistedPlayerMatches != floorRecoveryPlan.quarantinePlayerMatchItemCount ||
	    floorRecoveryPlan.quarantinePersistedBytes != floorRecoveryPlan.quarantineSerializedBytes) {
		return failQuarantine("persisted quarantine counters differ from the recovery plan");
	}
	if (!transaction.commit()) {
		return failQuarantine("could not commit the quarantine materialization transaction");
	}

	floorRecoveryPlan.quarantineReady = true;
	finishQuarantine();
	return true;
}

bool Game::applyFloorRecovery(uint64_t expectedSourceSessionId)
{
	floorRecoveryPlan.applyEvaluated = true;
	floorRecoveryPlan.applyError.clear();

	if (floorRecoveryAppliedThisSession) {
		floorRecoveryPlan.applyReady = true;
		floorRecoveryPlan.applyCompleted = true;
		floorRecoveryPlan.applyError = fmt::format(
			"recovery source {:d} was already applied in this server process",
			floorRecoveryAppliedSourceSessionId);
		return false;
	}
	if (expectedSourceSessionId == 0 || expectedSourceSessionId != floorRecoveryPlan.sourceSessionId) {
		floorRecoveryPlan.applyReady = false;
		floorRecoveryPlan.applyCompleted = false;
		floorRecoveryPlan.applyError = fmt::format(
			"expected recovery source {:d}, but the selected source is {:d}",
			expectedSourceSessionId, floorRecoveryPlan.sourceSessionId);
		return false;
	}

	const std::string expectedMode = floorRecoveryPlan.mode;
	if (!buildFloorRecoveryPlan()) {
		floorRecoveryPlan.applyEvaluated = true;
		floorRecoveryPlan.applyReady = false;
		floorRecoveryPlan.applyCompleted = false;
		floorRecoveryPlan.applyError = "recovery validation refresh failed immediately before map apply";
		return false;
	}
	if (floorRecoveryPlan.sourceSessionId != expectedSourceSessionId || floorRecoveryPlan.mode != expectedMode) {
		floorRecoveryPlan.applyEvaluated = true;
		floorRecoveryPlan.applyReady = false;
		floorRecoveryPlan.applyCompleted = false;
		floorRecoveryPlan.applyError = fmt::format(
			"recovery selection changed during preflight: source {:d}/{:s} became {:d}/{:s}",
			expectedSourceSessionId, expectedMode,
			floorRecoveryPlan.sourceSessionId, floorRecoveryPlan.mode);
		return false;
	}

	return prepareAndApplyFloorRecovery();
}

bool Game::prepareAndApplyFloorRecovery()
{
	floorRecoveryPlan.applyEvaluated = true;
	floorRecoveryPlan.applyReady = false;
	floorRecoveryPlan.applyCompleted = false;
	floorRecoveryPlan.applyRows = 0;
	floorRecoveryPlan.applyTargetTiles = 0;
	floorRecoveryPlan.applyPolicyRestoreItemCount = 0;
	floorRecoveryPlan.applyPolicyRestoreTopItemCount = 0;
	floorRecoveryPlan.applyRestoredItemCount = 0;
	floorRecoveryPlan.applyRestoredTopItemCount = 0;
	floorRecoveryPlan.applyQuarantineItemCount = 0;
	floorRecoveryPlan.applySuppressedItemCount = 0;
	floorRecoveryPlan.applySuppressedTopItemCount = 0;
	floorRecoveryPlan.applyDirectSuppressedIdentityCount = 0;
	floorRecoveryPlan.applyError.clear();
	const auto applyStarted = std::chrono::steady_clock::now();
	auto finishApply = [&]() {
		floorRecoveryPlan.applyMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - applyStarted).count());
	};
	auto failApply = [&](const std::string& error) {
		if (floorRecoveryPlan.applyError.empty()) {
			floorRecoveryPlan.applyError = error;
		}
		finishApply();
		return false;
	};

	if (!floorRecoveryPlan.applyEnabled || !floorRecoveryPlan.dryRunReady ||
	    !floorRecoveryPlan.reconciliationReady || !floorRecoveryPlan.quarantineReady) {
		return failApply("recovery dry-run, reconciliation and quarantine must all be ready before map apply");
	}
	if (!floorRecoveryDecayState.heldItems.empty()) {
		return failApply("a previous crash recovery still owns paused decay items in this process");
	}

	FloorRecoverySnapshotMode recoveryMode;
	if (floorRecoveryPlan.mode == "CLEAN_RESTART") {
		recoveryMode = FloorRecoverySnapshotMode::CLEAN_RESTART;
	} else if (floorRecoveryPlan.mode == "CRASH_RECOVERY") {
		recoveryMode = FloorRecoverySnapshotMode::CRASH_RECOVERY;
	} else {
		return failApply(fmt::format("recovery mode {:s} cannot be applied", floorRecoveryPlan.mode));
	}

	if (floorRecoveryPlan.snapshotRows == 0) {
		if (recoveryMode != FloorRecoverySnapshotMode::CLEAN_RESTART ||
		    floorRecoveryPlan.itemCount != 0 || floorRecoveryPlan.topItemCount != 0 ||
		    floorRecoveryPlan.dryRunItemCount != 0 || floorRecoveryPlan.dryRunTopItemCount != 0 ||
		    floorRecoveryPlan.dryRunRestoreItemCount != 0 ||
		    floorRecoveryPlan.dryRunQuarantineItemCount != 0 ||
		    floorRecoveryPlan.dryRunRejectedItemCount != 0) {
			return failApply("empty clean restart recovery counters are inconsistent");
		}

		floorRecoveryAppliedThisSession = true;
		floorRecoveryAppliedSourceSessionId = floorRecoveryPlan.sourceSessionId;
		floorRecoveryPlan.applyReady = true;
		floorRecoveryPlan.applyCompleted = true;
		finishApply();
		return true;
	}

	struct PreparedRecoveryTile {
		Position position;
		Tile* tile = nullptr;
		std::vector<std::unique_ptr<Item>> topItems;
	};

	Database& database = Database::getInstance();
	DBResult_ptr snapshotResult = database.storeQuery(fmt::format(
		"SELECT `tile_x`,`tile_y`,`tile_z`,`serialized_bytes`,`checksum`,`serialized_data` "
		"FROM `floor_persistence_snapshots` WHERE `world_id`={:d} AND `generation_id`={:d} "
		"ORDER BY `tile_z`,`tile_y`,`tile_x`",
		floorSnapshotWorldId, floorSnapshotGenerationId));
	if (!snapshotResult) {
		return failApply("could not read materialized snapshots for map apply");
	}

	std::vector<PreparedRecoveryTile> preparedTiles;
	preparedTiles.reserve(floorRecoveryPlan.snapshotRows);
	uint64_t sourceItems = 0;
	uint64_t sourceTopItems = 0;
	do {
		PreparedRecoveryTile prepared;
		prepared.position = Position(
			snapshotResult->getNumber<uint16_t>("tile_x"),
			snapshotResult->getNumber<uint16_t>("tile_y"),
			static_cast<uint8_t>(snapshotResult->getNumber<uint16_t>("tile_z")));
		const uint32_t storedBytes = snapshotResult->getNumber<uint32_t>("serialized_bytes");
		const std::string storedChecksum = snapshotResult->getString("checksum");
		unsigned long storedDataSize = 0;
		const char* storedDataPointer = snapshotResult->getStream("serialized_data", storedDataSize);
		const std::string storedData(storedDataPointer ? storedDataPointer : "", storedDataSize);
		if (storedBytes != storedDataSize ||
		    storedChecksum != FloorPersistenceSerializer::checksum(storedData)) {
			return failApply(fmt::format("tile {:d},{:d},{:d}: snapshot changed or failed checksum during apply preflight",
				prepared.position.x, prepared.position.y, prepared.position.z));
		}

		FloorRecoverySnapshotRestore restore;
		std::string restoreError;
		if (!FloorPersistenceSerializer::prepareRecoverySnapshot(
				storedData, prepared.position, recoveryMode, floorRecoverySuppressedInstanceIds,
				prepared.topItems, restore, restoreError)) {
			return failApply(fmt::format("tile {:d},{:d},{:d}: {:s}",
				prepared.position.x, prepared.position.y, prepared.position.z,
				restoreError.empty() ? "recovery item preparation failed" : restoreError));
		}

		prepared.tile = map.getTile(prepared.position);
		if (!prepared.tile) {
			return failApply(fmt::format("tile {:d},{:d},{:d}: map tile does not exist",
				prepared.position.x, prepared.position.y, prepared.position.z));
		}
		if (dynamic_cast<HouseTile*>(prepared.tile)) {
			return failApply(fmt::format("tile {:d},{:d},{:d}: recovery cannot apply to a house tile",
				prepared.position.x, prepared.position.y, prepared.position.z));
		}
		const TileItemVector* existingItems = prepared.tile->getItemList();
		const size_t existingItemCount = existingItems ? existingItems->size() : 0;
		if (existingItemCount + prepared.topItems.size() > 0xFFFF) {
			return failApply(fmt::format("tile {:d},{:d},{:d}: applying recovery would exceed the tile item limit",
				prepared.position.x, prepared.position.y, prepared.position.z));
		}

		++floorRecoveryPlan.applyRows;
		sourceItems += restore.sourceItemCount;
		sourceTopItems += restore.sourceTopItemCount;
		floorRecoveryPlan.applyPolicyRestoreItemCount += restore.policyRestoreItemCount;
		floorRecoveryPlan.applyPolicyRestoreTopItemCount += restore.policyRestoreTopItemCount;
		floorRecoveryPlan.applyRestoredItemCount += restore.preparedItemCount;
		floorRecoveryPlan.applyRestoredTopItemCount += restore.preparedTopItemCount;
		floorRecoveryPlan.applyQuarantineItemCount += restore.quarantineItemCount;
		floorRecoveryPlan.applySuppressedItemCount += restore.suppressedItemCount;
		floorRecoveryPlan.applySuppressedTopItemCount += restore.suppressedTopItemCount;
		floorRecoveryPlan.applyDirectSuppressedIdentityCount += restore.directSuppressedIdentityCount;
		if (!prepared.topItems.empty()) {
			++floorRecoveryPlan.applyTargetTiles;
		}
		preparedTiles.emplace_back(std::move(prepared));
	} while (snapshotResult->next());

	if (floorRecoveryPlan.applyRows != floorRecoveryPlan.snapshotRows) {
		return failApply("snapshot row count changed during recovery apply preflight");
	}
	if (sourceItems != floorRecoveryPlan.dryRunItemCount ||
	    sourceTopItems != floorRecoveryPlan.dryRunTopItemCount) {
		return failApply("prepared recovery source counters differ from the validated dry-run");
	}
	if (floorRecoveryPlan.applyPolicyRestoreItemCount != floorRecoveryPlan.dryRunRestoreItemCount ||
	    floorRecoveryPlan.applyPolicyRestoreTopItemCount != floorRecoveryPlan.dryRunRestoreTopItemCount ||
	    floorRecoveryPlan.applyQuarantineItemCount != floorRecoveryPlan.dryRunQuarantineItemCount) {
		return failApply("prepared recovery policy counters differ from the validated dry-run");
	}
	if (floorRecoveryPlan.applyDirectSuppressedIdentityCount !=
	    floorRecoveryPlan.floorPlayerIdentityMatchCount) {
		return failApply("prepared recovery suppression count differs from player reconciliation");
	}
	if (floorRecoveryPlan.applyRestoredItemCount + floorRecoveryPlan.applySuppressedItemCount !=
	    floorRecoveryPlan.applyPolicyRestoreItemCount ||
	    floorRecoveryPlan.applyRestoredTopItemCount + floorRecoveryPlan.applySuppressedTopItemCount !=
	    floorRecoveryPlan.applyPolicyRestoreTopItemCount) {
		return failApply("prepared recovery item totals are internally inconsistent");
	}

	auto startOrHoldRecoveredDecay = [&](Item* rootItem) {
		std::vector<Item*> pendingItems {rootItem};
		while (!pendingItems.empty()) {
			Item* currentItem = pendingItems.back();
			pendingItems.pop_back();

			if (Container* container = currentItem->getContainer()) {
				for (Item* child : container->getItemList()) {
					pendingItems.push_back(child);
				}
			}

			if (!currentItem->canDecay()) {
				continue;
			}

			if (recoveryMode == FloorRecoverySnapshotMode::CRASH_RECOVERY) {
				// The map owns the normal item reference. Keep one additional
				// reference while decay is paused so a GM may move or remove
				// the item safely during the blocked inspection window.
				currentItem->incrementReferenceCounter();
				floorRecoveryDecayState.heldItems.push_back(currentItem);
			} else {
				// Call Game::startDecay directly for every node. The virtual
				// Container::startDecaying implementation only visits children
				// and would otherwise omit a decaying container root.
				startDecay(currentItem);
			}
		}
	};

	// All rows, policies, storage identities, target tiles and capacities were
	// validated above. Internal insertion is intentional: it preserves OTBM
	// items on the same tile and does not trigger movement scripts, trash,
	// teleport or a new player-origin dirty event.
	for (PreparedRecoveryTile& prepared : preparedTiles) {
		if (prepared.topItems.empty()) {
			continue;
		}
		for (std::unique_ptr<Item>& item : prepared.topItems) {
			Item* restoredItem = item.release();
			prepared.tile->internalAddThing(restoredItem);
			startOrHoldRecoveredDecay(restoredItem);
		}

		SpectatorVec spectators;
		map.getSpectators(spectators, prepared.position, true, true);
		for (Creature* spectator : spectators) {
			if (Player* player = spectator->getPlayer()) {
				player->sendUpdateTile(prepared.tile, prepared.position);
			}
		}
	}

	floorRecoveryAppliedThisSession = true;
	floorRecoveryAppliedSourceSessionId = floorRecoveryPlan.sourceSessionId;
	floorRecoveryPlan.applyReady = true;
	floorRecoveryPlan.applyCompleted = true;
	finishApply();
	return true;
}

bool Game::confirmFloorRecovery(uint64_t expectedSourceSessionId, uint32_t confirmerGuid,
	const std::string& confirmerName)
{
	floorRecoveryPlan.confirmationEvaluated = true;
	floorRecoveryPlan.confirmationReady = false;
	floorRecoveryPlan.confirmationCompleted = false;
	floorRecoveryPlan.confirmationRecordId = 0;
	floorRecoveryPlan.confirmationSourceSessionId = expectedSourceSessionId;
	floorRecoveryPlan.confirmationApplySessionId = floorPersistenceSessionId;
	floorRecoveryPlan.confirmationPendingQuarantineRows = 0;
	floorRecoveryPlan.confirmationPendingQuarantineItems = 0;
	floorRecoveryPlan.confirmationPendingPlayerMatches = 0;
	floorRecoveryDecayState.resumedItemCount = 0;
	floorRecoveryDecayState.extendedPlayerCorpseCount = 0;
	floorRecoveryDecayState.removedHeldItemCount = 0;
	floorRecoveryPlan.confirmationPlayerGuid = confirmerGuid;
	floorRecoveryPlan.confirmationPlayerName = confirmerName;
	floorRecoveryPlan.confirmationConfirmedAt = 0;
	floorRecoveryPlan.confirmationError.clear();
	const auto confirmationStarted = std::chrono::steady_clock::now();
	auto finishConfirmation = [&]() {
		floorRecoveryPlan.confirmationMicros = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - confirmationStarted).count());
	};
	auto failConfirmation = [&](const std::string& error) {
		if (floorRecoveryPlan.confirmationError.empty()) {
			floorRecoveryPlan.confirmationError = error;
		}
		finishConfirmation();
		return false;
	};

	if (floorRecoveryConfirmedThisSession) {
		floorRecoveryPlan.confirmationReady = true;
		floorRecoveryPlan.confirmationCompleted = true;
		floorRecoveryPlan.confirmationSourceSessionId = floorRecoveryConfirmedSourceSessionId;
		return failConfirmation(fmt::format(
			"recovery source {:d} was already confirmed in this server process",
			floorRecoveryConfirmedSourceSessionId));
	}
	if (!floorSnapshotShadowEnabled) {
		return failConfirmation("floor snapshot persistence is disabled");
	}
	if (floorRecoveryPlan.mode != "CRASH_RECOVERY") {
		return failConfirmation(fmt::format(
			"recovery mode {:s} does not require a stage 5.6 crash confirmation",
			floorRecoveryPlan.mode));
	}
	if (expectedSourceSessionId == 0 ||
	    expectedSourceSessionId != floorRecoveryPlan.sourceSessionId ||
	    expectedSourceSessionId != floorRecoveryAppliedSourceSessionId) {
		return failConfirmation(fmt::format(
			"expected source {:d}, selected source {:d}, applied source {:d}",
			expectedSourceSessionId, floorRecoveryPlan.sourceSessionId,
			floorRecoveryAppliedSourceSessionId));
	}
	if (!floorRecoveryAppliedThisSession || !floorRecoveryPlan.applyEvaluated ||
	    !floorRecoveryPlan.applyReady || !floorRecoveryPlan.applyCompleted) {
		return failConfirmation(
			"the selected recovery source has not completed stage 5.5 map apply in this server process");
	}
	if (floorPersistenceSessionId == 0 || floorPersistenceSessionState != "RUNNING") {
		return failConfirmation(fmt::format(
			"the applying floor persistence session is not RUNNING (session {:d}, state {:s})",
			floorPersistenceSessionId, floorPersistenceSessionState));
	}
	if (confirmerGuid == 0 || confirmerName.empty()) {
		return failConfirmation("a valid GOD character is required to confirm floor recovery");
	}

	const uint32_t inFlight = getFloorSnapshotInFlightCount();
	if (!floorDirtyTiles.empty() || inFlight != 0 || !floorCheckpointGroups.empty()) {
		return failConfirmation(fmt::format(
			"floor mutations are not settled: dirty={:d}, in_flight={:d}, groups={:d}; "
			"wait for the checkpoint or flush it before confirming",
			floorDirtyTiles.size(), inFlight, floorCheckpointGroups.size()));
	}
	if (floorRecoveryPlan.applyRows != floorRecoveryPlan.snapshotRows ||
	    floorRecoveryPlan.applyRestoredItemCount + floorRecoveryPlan.applySuppressedItemCount !=
		    floorRecoveryPlan.applyPolicyRestoreItemCount ||
	    floorRecoveryPlan.applyRestoredTopItemCount + floorRecoveryPlan.applySuppressedTopItemCount !=
		    floorRecoveryPlan.applyPolicyRestoreTopItemCount ||
	    floorRecoveryPlan.applyQuarantineItemCount != floorRecoveryPlan.dryRunQuarantineItemCount) {
		return failConfirmation("stage 5.5 apply counters are no longer internally consistent");
	}

	Database& database = Database::getInstance();
	DBResult_ptr quarantineResult = database.storeQuery(fmt::format(
		"SELECT COUNT(*) AS `rows`,COALESCE(SUM(`quarantine_item_count`),0) AS `items`,"
		"COALESCE(SUM(`player_match_item_count`),0) AS `player_matches` "
		"FROM `floor_persistence_quarantine` "
		"WHERE `world_id`={:d} AND `generation_id`={:d} "
		"AND `recovery_source_session_id`={:d} AND `state`='PENDING' AND `active`=1",
		floorSnapshotWorldId, floorSnapshotGenerationId, expectedSourceSessionId));
	if (!quarantineResult) {
		return failConfirmation("could not verify the pending quarantine before confirmation");
	}
	floorRecoveryPlan.confirmationPendingQuarantineRows =
		quarantineResult->getNumber<uint64_t>("rows");
	floorRecoveryPlan.confirmationPendingQuarantineItems =
		quarantineResult->getNumber<uint64_t>("items");
	floorRecoveryPlan.confirmationPendingPlayerMatches =
		quarantineResult->getNumber<uint64_t>("player_matches");
	if (floorRecoveryPlan.confirmationPendingQuarantineRows !=
		    floorRecoveryPlan.quarantinePersistedRows ||
	    floorRecoveryPlan.confirmationPendingQuarantineItems !=
		    floorRecoveryPlan.quarantinePersistedStackableItems ||
	    floorRecoveryPlan.confirmationPendingPlayerMatches !=
		    floorRecoveryPlan.quarantinePersistedPlayerMatches) {
		return failConfirmation(
			"the active pending quarantine no longer matches the applied recovery plan");
	}

	DBTransaction transaction;
	if (!transaction.begin()) {
		return failConfirmation("could not start the durable recovery confirmation transaction");
	}
	if (!database.executeQuery(fmt::format(
			"INSERT INTO `floor_persistence_recovery_confirmations` "
			"(`world_id`,`generation_id`,`recovery_source_session_id`,`apply_session_id`,"
			"`recovery_mode`,`source_state`,`snapshot_rows`,`applied_rows`,`target_tiles`,"
			"`restored_item_count`,`restored_top_item_count`,`quarantine_item_count`,"
			"`suppressed_item_count`,`suppressed_top_item_count`,`pending_quarantine_rows`,"
			"`pending_player_match_count`,`confirmed_by_player_id`,`confirmed_by_name`) VALUES "
			"({:d},{:d},{:d},{:d},{:s},{:s},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:d},{:s})",
			floorSnapshotWorldId, floorSnapshotGenerationId, expectedSourceSessionId,
			floorPersistenceSessionId, database.escapeString(floorRecoveryPlan.mode),
			database.escapeString(floorRecoveryPlan.sourceState), floorRecoveryPlan.snapshotRows,
			floorRecoveryPlan.applyRows, floorRecoveryPlan.applyTargetTiles,
			floorRecoveryPlan.applyRestoredItemCount, floorRecoveryPlan.applyRestoredTopItemCount,
			floorRecoveryPlan.applyQuarantineItemCount, floorRecoveryPlan.applySuppressedItemCount,
			floorRecoveryPlan.applySuppressedTopItemCount,
			floorRecoveryPlan.confirmationPendingQuarantineRows,
			floorRecoveryPlan.confirmationPendingPlayerMatches, confirmerGuid,
			database.escapeString(confirmerName)))) {
		return failConfirmation("could not persist the stage 5.6 recovery confirmation");
	}
	const uint64_t confirmationRecordId = database.getLastInsertId();
	if (confirmationRecordId == 0) {
		return failConfirmation("the durable recovery confirmation did not receive an id");
	}
	if (!transaction.commit()) {
		return failConfirmation("could not commit the durable recovery confirmation");
	}

	for (Item* item : floorRecoveryDecayState.heldItems) {
		if (item->isRemoved() || !item->canDecay()) {
			++floorRecoveryDecayState.removedHeldItemCount;
			ReleaseItem(item);
			continue;
		}

		if (item->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_FLOOR_PLAYER_CORPSE)) {
			const int64_t extendedDuration =
				static_cast<int64_t>(item->getDuration()) +
				PLAYER_CORPSE_CRASH_RECOVERY_DECAY_BONUS_MS;
			item->setDuration(static_cast<int32_t>(std::min<int64_t>(
				extendedDuration, std::numeric_limits<int32_t>::max())));
			++floorRecoveryDecayState.extendedPlayerCorpseCount;
		}

		++floorRecoveryDecayState.resumedItemCount;
		startDecay(item);
		ReleaseItem(item);
	}
	floorRecoveryDecayState.heldItems.clear();

	floorRecoveryConfirmedThisSession = true;
	floorRecoveryConfirmedSourceSessionId = expectedSourceSessionId;
	floorRecoveryPlan.confirmationReady = true;
	floorRecoveryPlan.confirmationCompleted = true;
	floorRecoveryPlan.confirmationRecordId = confirmationRecordId;
	floorRecoveryPlan.confirmationConfirmedAt = static_cast<int64_t>(time(nullptr));
	finishConfirmation();
	std::cout << "Floor recovery stage 5.6 confirmed: source=" << expectedSourceSessionId
	          << " apply_session=" << floorPersistenceSessionId
	          << " record=" << confirmationRecordId
	          << " restored=" << floorRecoveryPlan.applyRestoredItemCount
	          << " quarantine=" << floorRecoveryPlan.applyQuarantineItemCount
	          << " decay_resumed=" << floorRecoveryDecayState.resumedItemCount
	          << " player_corpses_extended=" << floorRecoveryDecayState.extendedPlayerCorpseCount
	          << " decay_removed=" << floorRecoveryDecayState.removedHeldItemCount
	          << " confirmed_by=" << confirmerName
	          << ". Common-player login and clean save are now allowed in this process." << std::endl;
	return true;
}

uint64_t Game::getFloorRecoveryHeldDecayItemCount() const
{
	return floorRecoveryDecayState.heldItems.size();
}

uint64_t Game::getFloorRecoveryResumedDecayItemCount() const
{
	return floorRecoveryDecayState.resumedItemCount;
}

uint64_t Game::getFloorRecoveryExtendedPlayerCorpseCount() const
{
	return floorRecoveryDecayState.extendedPlayerCorpseCount;
}

uint64_t Game::getFloorRecoveryRemovedHeldDecayItemCount() const
{
	return floorRecoveryDecayState.removedHeldItemCount;
}

bool Game::initializeFloorPersistenceSession()
{
	Database& database = Database::getInstance();
	if (!database.executeQuery(fmt::format(
		"INSERT INTO `floor_persistence_save_sessions` (`world_id`,`generation_id`,`state`) "
		"VALUES ({:d},{:d},'RUNNING')", floorSnapshotWorldId, floorSnapshotGenerationId))) {
		floorPersistenceSessionState = "SESSION_INIT_FAILED";
		return false;
	}
	floorPersistenceSessionId = database.getLastInsertId();
	floorPersistenceSessionState = "RUNNING";
	return floorPersistenceSessionId != 0;
}

bool Game::updateFloorPersistenceSession(const std::string& state, uint32_t playerCount,
	uint32_t tileCount, const std::string& error)
{
	if (floorPersistenceSessionId == 0) {
		return false;
	}
	Database& database = Database::getInstance();
	const bool success = database.executeQuery(fmt::format(
		"UPDATE `floor_persistence_save_sessions` SET `state`={:s},`player_count`={:d},"
		"`tile_count`={:d},`error`={:s},`updated_at`=CURRENT_TIMESTAMP(6),"
		"`committed_at`=IF({:s}='CLEAN_COMMITTED',CURRENT_TIMESTAMP(6),`committed_at`) "
		"WHERE `id`={:d}", database.escapeString(state), playerCount, tileCount,
		database.escapeString(error), database.escapeString(state), floorPersistenceSessionId));
	if (success) {
		floorPersistenceSessionState = state;
	}
	return success;
}

bool Game::saveAllFloorSnapshotsForCleanSave(uint32_t& savedTiles, std::string& error)
{
	savedTiles = 0;
	std::set<Position> positions;
	if (floorCleanSaveResetFloor) {
		Database& database = Database::getInstance();
		DBResult_ptr persistedCount = database.storeQuery(fmt::format(
			"SELECT COUNT(*) AS `snapshot_rows` FROM `floor_persistence_snapshots` "
			"WHERE `world_id`={:d} AND `generation_id`={:d}",
			floorSnapshotWorldId, floorSnapshotGenerationId));
		if (!persistedCount) {
			error = "could not count materialized snapshots for the weekly floor reset";
			return false;
		}
		floorCleanSaveResetSnapshotCount = persistedCount->getNumber<uint64_t>("snapshot_rows");
	}
	if (!floorCleanSaveResetFloor) {
		for (const auto& entry : floorDirtyTiles) {
			if (!dynamic_cast<HouseTile*>(map.getTile(entry.first))) {
				positions.insert(entry.first);
			}
		}
		positions.insert(floorPersistenceCityPositions.begin(), floorPersistenceCityPositions.end());
	}

	std::vector<PreparedFloorSnapshot> snapshots;
	snapshots.reserve(positions.size());
	for (const Position& position : positions) {
		FloorDirtyTileRecord record;
		auto dirtyIt = floorDirtyTiles.find(position);
		if (dirtyIt != floorDirtyTiles.end()) {
			record = dirtyIt->second;
		} else {
			const uint64_t wallClockVersion = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
			floorSnapshotVersionClock = std::max(floorSnapshotVersionClock + 1, wallClockVersion);
			record.tileVersion = floorSnapshotVersionClock;
			record.reasonMask = FLOOR_DIRTY_NONE;
			record.originMask = FLOOR_DIRTY_ORIGIN_EXPLICIT;
		}

		PreparedFloorSnapshot prepared;
		const bool cityCleanupFiltered = isFloorPersistenceCityPosition(position);
		if (!prepareFloorSnapshot(position, record, cityCleanupFiltered, 0, 0, prepared, error)) {
			++floorSnapshotStats.serializationFailed;
			floorSnapshotStats.lastError = error;
			return false;
		}
		snapshots.push_back(std::move(prepared));
	}

	if (!executeFloorSnapshotsTransaction(
	        snapshots, {}, {}, 0, 0, error, true, floorCleanSaveResetFloor)) {
		++floorSnapshotStats.failed;
		floorSnapshotStats.lastError = error;
		return false;
	}

	floorSnapshotStats.queued += snapshots.size();
	completePreparedFloorSnapshots(snapshots);
	savedTiles = static_cast<uint32_t>(snapshots.size());
	floorSnapshotStats.checkpointTilesSaved += savedTiles;
	if (floorCleanSaveResetFloor) {
		floorDirtyTiles.clear();
		floorSnapshotRuntimeRecords.clear();
	}
	floorCheckpointGroups.clear();
	floorCheckpointTileGroups.clear();
	floorCheckpointPlayerGroups.clear();
	floorCheckpointHouseGroups.clear();
	floorCheckpointItemGroups.clear();
	return true;
}

void Game::recordCleanSavePlayerResult(bool success)
{
	if (!floorCleanSaveInProgress) {
		return;
	}
	++floorCleanSavePlayerCount;
	if (!success) {
		++floorCleanSavePlayerFailures;
	}
}

bool Game::beginFloorPersistenceCleanSave(bool resetFloorSnapshots)
{
	if (emergencyActive) {
		std::cout << "[Emergency] Clean save refused while emergency mode is active. "
		          << "Use !emergency finish to authorize the coordinated save." << std::endl;
		return false;
	}
	if (isFloorRecoveryLoginRestricted()) {
		std::cout << "[Error - Game::beginFloorPersistenceCleanSave] Clean save refused while floor recovery mode="
		          << floorRecoveryPlan.mode << ". Recovery must be applied and confirmed first." << std::endl;
		return false;
	}
	if (!floorSnapshotShadowEnabled || floorCleanSaveWindowActive || floorCleanSaveInProgress) {
		return false;
	}

	floorCleanSaveInProgress = true;
	floorCleanSaveResetFloor = resetFloorSnapshots;
	floorCleanSaveResetSnapshotCount = 0;
	floorCleanSavePlayerCount = 0;
	floorCleanSavePlayerFailures = 0;
	floorCleanSaveTileCount = 0;
	if (!updateFloorPersistenceSession("CLEAN_PREPARING", 0, 0)) {
		floorCleanSaveInProgress = false;
		floorCleanSaveResetFloor = false;
		return false;
	}
	std::cout << "Floor persistence " << (floorCleanSaveResetFloor ? "weekly reset" : "clean save")
	          << ": login barrier active; disconnecting "
	          << players.size() << " player(s)." << std::endl;

	// CLOSING blocks ordinary logins before the first player is removed.
	gameState = GAME_STATE_CLOSING;
	while (!players.empty()) {
		players.begin()->second->kickPlayer(true);
	}

	std::string playerIODrainError;
	if (!g_playerIOManager.drain(
			PlayerIOManager::SHUTDOWN_DRAIN_TIMEOUT, playerIODrainError)) {
		recordCleanSavePlayerResult(false);
		std::cout << "[Error - Game::beginFloorPersistenceCleanSave] "
		             "Asynchronous player saves did not reach a safe terminal state: "
		          << playerIODrainError << std::endl;
	}

	const bool saved = saveGameState();
	floorCleanSaveInProgress = false;
	gameState = GAME_STATE_CLOSED;
	const bool success = saved && floorCleanSavePlayerFailures == 0;
	const bool floorResetCommitted = floorCleanSaveResetFloor && success;
	const uint64_t floorResetRemovedSnapshots = floorCleanSaveResetSnapshotCount;
	floorCleanSaveResetFloor = false;
	floorCleanSaveWindowActive = success;
	if (success && floorPersistenceSessionState != "CLEAN_COMMITTED") {
		floorCleanSaveWindowActive = false;
		std::cout << "[Error - Game::beginFloorPersistenceCleanSave] Clean save transaction did not commit its session state."
		          << std::endl;
		return false;
	}
	if (!success && !updateFloorPersistenceSession("CLEAN_FAILED", floorCleanSavePlayerCount,
		floorCleanSaveTileCount, "one or more player, tile, account or house saves failed")) {
		floorCleanSaveWindowActive = false;
		return false;
	}
	if (success) {
		std::cout << "Floor persistence "
		          << (floorResetCommitted ? "weekly reset" : "clean save")
		          << " committed: players=" << floorCleanSavePlayerCount
		          << " tiles=" << floorCleanSaveTileCount
		          << (floorResetCommitted ?
		              fmt::format(" snapshots_removed={:d}.", floorResetRemovedSnapshots) : ".")
		          << " Login remains blocked until restart." << std::endl;
	} else {
		std::cout << "[Error - Game::beginFloorPersistenceCleanSave] Clean save failed; automatic shutdown must not proceed."
		          << std::endl;
	}
	return success;
}

bool Game::activateEmergency(uint32_t activatorGuid, const std::string& activatorName)
{
	if (emergencyActive) {
		return false;
	}

	if (floorCleanSaveInProgress || floorCleanSaveWindowActive ||
	    isFloorRecoveryLoginRestricted() || gameState == GAME_STATE_SHUTDOWN) {
		return false;
	}

	emergencyActive = true;

	auto holdDecayItem = [this](Item* item) {
		if (item && emergencyHeldDecaySet.emplace(item).second) {
			emergencyHeldDecayItems.push_back(item);
		}
	};

	for (auto& bucket : decayItems) {
		for (Item* item : bucket) {
			holdDecayItem(item);
		}
		bucket.clear();
	}

	for (Item* item : toDecayItems) {
		holdDecayItem(item);
	}
	toDecayItems.clear();

	gameState = GAME_STATE_CLOSED;
	broadcastMessage(
		"Emergency mode activated. Ordinary access is closed until the server restarts.",
		MESSAGE_STATUS_WARNING);

	uint32_t disconnectedPlayers = 0;
	for (auto it = players.begin(); it != players.end();) {
		Player* player = (it++)->second;
		if (!player->hasFlag(PlayerFlag_CanAlwaysLogin)) {
			player->kickPlayer(true);
			++disconnectedPlayers;
		}
	}

	std::cout << "[Emergency] Activated by " << activatorName << '/' << activatorGuid
	          << ": decay_paused=" << emergencyHeldDecayItems.size()
	          << " ordinary_players_disconnected=" << disconnectedPlayers
	          << ". Automatic and manual clean saves are blocked until !emergency finish."
	          << std::endl;
	return true;
}

bool Game::finishEmergency(uint32_t finisherGuid, const std::string& finisherName)
{
	if (!emergencyActive) {
		return false;
	}

	uint64_t resumedItems = 0;
	uint64_t extendedPlayerCorpses = 0;
	uint64_t removedItems = 0;

	emergencyActive = false;
	for (Item* item : emergencyHeldDecayItems) {
		if (!item || item->isRemoved() || !item->canDecay()) {
			++removedItems;
			if (item) {
				ReleaseItem(item);
			}
			continue;
		}

		if (item->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_FLOOR_PLAYER_CORPSE)) {
			const int64_t extendedDuration =
				static_cast<int64_t>(item->getDuration()) +
				PLAYER_CORPSE_CRASH_RECOVERY_DECAY_BONUS_MS;
			item->setDuration(static_cast<int32_t>(std::min<int64_t>(
				extendedDuration, std::numeric_limits<int32_t>::max())));
			++extendedPlayerCorpses;
		}

		if (item->getDuration() > 0) {
			toDecayItems.push_front(item);
		} else {
			internalDecayItem(item);
			ReleaseItem(item);
		}
		++resumedItems;
	}

	emergencyHeldDecayItems.clear();
	emergencyHeldDecaySet.clear();

	std::cout << "[Emergency] Finish authorized by " << finisherName << '/' << finisherGuid
	          << ": decay_resumed=" << resumedItems
	          << " player_corpses_extended_50m=" << extendedPlayerCorpses
	          << " removed_while_paused=" << removedItems
	          << ". The coordinated clean save may now begin." << std::endl;
	return true;
}

void Game::completeFloorSnapshot(const Position& position, uint64_t tileVersion, bool success,
	FloorSnapshotRuntimeRecord runtimeRecord, const std::string& error)
{
	auto dirtyIt = floorDirtyTiles.find(position);
	if (success) {
		++floorSnapshotStats.succeeded;
		floorSnapshotStats.lastSuccessAt = static_cast<int64_t>(time(nullptr));
		floorSnapshotStats.lastError.clear();
		runtimeRecord.persistedAt = floorSnapshotStats.lastSuccessAt;
		auto runtimeIt = floorSnapshotRuntimeRecords.find(position);
		const bool newerRuntimeExists = runtimeIt != floorSnapshotRuntimeRecords.end() &&
			runtimeIt->second.tileVersion > tileVersion;
		if (!newerRuntimeExists) {
			floorSnapshotRuntimeRecords[position] = std::move(runtimeRecord);
		}

		if (dirtyIt != floorDirtyTiles.end() && dirtyIt->second.tileVersion == tileVersion) {
			floorDirtyTiles.erase(dirtyIt);
		} else {
			++floorSnapshotStats.staleCompletions;
			if (dirtyIt != floorDirtyTiles.end() && dirtyIt->second.snapshotVersionInFlight == tileVersion) {
				dirtyIt->second.snapshotInFlight = false;
				dirtyIt->second.snapshotVersionInFlight = 0;
			}
		}
		return;
	}

	++floorSnapshotStats.failed;
	floorSnapshotStats.lastError = error;
	if (dirtyIt != floorDirtyTiles.end() && dirtyIt->second.snapshotVersionInFlight == tileVersion) {
		FloorDirtyTileRecord& record = dirtyIt->second;
		record.snapshotInFlight = false;
		record.snapshotVersionInFlight = 0;
		record.lastSnapshotError = error;
		++record.snapshotRetryCount;
		record.snapshotRetryNotBefore = OTSYS_TIME() + static_cast<int64_t>(floorSnapshotRetryMs) *
			std::min<uint32_t>(record.snapshotRetryCount, 6);
	}
}

uint32_t Game::flushFloorSnapshots()
{
	return processFloorSnapshots(true);
}

void Game::simulateFloorSnapshotFailures(uint32_t count)
{
	floorSnapshotSimulatedFailures = count;
	// Mirror the hook into the checkpoint worker so forced failures also apply
	// to checkpoints executed in the background.
	g_checkpointWorker.simulateFailures(count);
}

FloorSnapshotDatabaseStats Game::getFloorSnapshotDatabaseStats() const
{
	FloorSnapshotDatabaseStats stats;
	DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
		"SELECT COUNT(*) AS `row_count`,COALESCE(SUM(`serialized_bytes`),0) AS `total_bytes`,"
		"COALESCE(MAX(`tile_version`),0) AS `max_tile_version`,"
		"COALESCE(CAST(UNIX_TIMESTAMP(MAX(`updated_at`)) AS UNSIGNED),0) AS `last_updated_at` "
		"FROM `floor_persistence_snapshots` WHERE `world_id`={:d} AND `generation_id`={:d}",
		floorSnapshotWorldId, floorSnapshotGenerationId));
	if (!result) {
		stats.error = "snapshot table query failed";
		return stats;
	}

	stats.available = true;
	stats.rowCount = result->getNumber<uint64_t>("row_count");
	stats.totalBytes = result->getNumber<uint64_t>("total_bytes");
	stats.maxTileVersion = result->getNumber<uint64_t>("max_tile_version");
	stats.lastUpdatedAt = result->getNumber<int64_t>("last_updated_at");
	return stats;
}

FloorSnapshotVerification Game::verifyFloorSnapshot(const Position& position) const
{
	FloorSnapshotVerification verification;
	auto dirtyIt = floorDirtyTiles.find(position);
	if (dirtyIt != floorDirtyTiles.end()) {
		verification.dirty = true;
		verification.inFlight = dirtyIt->second.snapshotInFlight;
		verification.dirtyTileVersion = dirtyIt->second.tileVersion;
	}

	DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
		"SELECT `tile_version`,`policy_version`,`item_count`,`top_item_count`,`serialized_bytes`,`checksum`,"
		"`serialized_data`,CAST(UNIX_TIMESTAMP(`updated_at`) AS UNSIGNED) AS `updated_at_epoch` "
		"FROM `floor_persistence_snapshots` WHERE `world_id`={:d} AND `generation_id`={:d} "
		"AND `tile_x`={:d} AND `tile_y`={:d} AND `tile_z`={:d} LIMIT 1",
		floorSnapshotWorldId, floorSnapshotGenerationId, position.x, position.y, position.z));
	if (!result) {
		verification.error = "snapshot row not found or database query failed";
		return verification;
	}

	verification.rowFound = true;
	verification.storedTileVersion = result->getNumber<uint64_t>("tile_version");
	verification.storedPolicyVersion = result->getNumber<uint16_t>("policy_version");
	verification.storedItemCount = result->getNumber<uint32_t>("item_count");
	verification.storedTopItemCount = result->getNumber<uint32_t>("top_item_count");
	verification.storedBytes = result->getNumber<uint32_t>("serialized_bytes");
	verification.storedChecksum = result->getString("checksum");
	verification.storedUpdatedAt = result->getNumber<int64_t>("updated_at_epoch");

	unsigned long storedSize = 0;
	const char* storedDataPointer = result->getStream("serialized_data", storedSize);
	const std::string storedData(storedDataPointer ? storedDataPointer : "", storedSize);
	verification.storedChecksumValid = verification.storedChecksum == FloorPersistenceSerializer::checksum(storedData) &&
		verification.storedBytes == storedSize;

	uint32_t decodedItems = 0;
	uint32_t decodedTopItems = 0;
	std::string validationError;
	verification.storedBlobValid = FloorPersistenceSerializer::validateSnapshot(
		storedData, position, decodedItems, decodedTopItems, validationError) &&
		decodedItems == verification.storedItemCount && decodedTopItems == verification.storedTopItemCount;
	if (!verification.storedBlobValid) {
		verification.error = validationError.empty() ? "stored snapshot counters do not match its blob" : validationError;
	}

	FloorSnapshotData liveSnapshot;
	std::string liveError;
	verification.liveSnapshotValid = FloorPersistenceSerializer::serializeTile(position, map.getTile(position),
		false, liveSnapshot, liveError);
	if (verification.liveSnapshotValid) {
		verification.liveItemCount = liveSnapshot.itemCount;
		verification.liveTopItemCount = liveSnapshot.topItemCount;
		verification.liveBytes = static_cast<uint32_t>(liveSnapshot.serializedData.size());
		verification.liveChecksum = liveSnapshot.checksum;
		verification.matchesLive = verification.storedChecksumValid && verification.storedBlobValid &&
			verification.liveChecksum == verification.storedChecksum;
	} else if (verification.error.empty()) {
		verification.error = liveError;
	}
	return verification;
}

FloorInstanceLookup Game::inspectFloorInstanceId(const std::string& instanceId)
{
	FloorInstanceLookup lookup;
	if (instanceId.size() != 32 ||
	    !std::all_of(instanceId.begin(), instanceId.end(), [](char character) {
		    return (character >= '0' && character <= '9') ||
		           (character >= 'a' && character <= 'f');
	    })) {
		lookup.error = "instance_id must contain exactly 32 lowercase hexadecimal characters";
		return lookup;
	}
	lookup.validFormat = true;

	Database& database = Database::getInstance();
	const std::string escapedInstanceId = database.escapeString(instanceId);
	DBResult_ptr result = database.storeQuery(fmt::format(
		"SELECT `source`,`location` FROM ("
		"SELECT 'inventory' AS `source`,CONCAT('player=',`player_id`,' sid=',`sid`) AS `location` "
			"FROM `player_items` WHERE LOCATE({0:s},`attributes`)>0 "
		"UNION ALL SELECT 'depot_locker',CONCAT('player=',`player_id`,' sid=',`sid`) "
			"FROM `player_depotlockeritems` WHERE LOCATE({0:s},`attributes`)>0 "
		"UNION ALL SELECT 'depot',CONCAT('player=',`player_id`,' sid=',`sid`) "
			"FROM `player_depotitems` WHERE LOCATE({0:s},`attributes`)>0 "
		"UNION ALL SELECT 'inbox',CONCAT('player=',`player_id`,' sid=',`sid`) "
			"FROM `player_inboxitems` WHERE LOCATE({0:s},`attributes`)>0 "
		"UNION ALL SELECT 'store_inbox',CONCAT('player=',`player_id`,' sid=',`sid`) "
			"FROM `player_storeinboxitems` WHERE LOCATE({0:s},`attributes`)>0 "
		"UNION ALL SELECT 'floor_snapshot',CONCAT(`tile_x`,',',`tile_y`,',',`tile_z`) "
			"FROM `floor_persistence_snapshots` WHERE `world_id`={1:d} AND `generation_id`={2:d} "
			"AND LOCATE({0:s},`serialized_data`)>0 "
		"UNION ALL SELECT 'quarantine',CONCAT('row=',`id`,' tile=',`tile_x`,',',`tile_y`,',',`tile_z`) "
			"FROM `floor_persistence_quarantine` WHERE `world_id`={1:d} AND `generation_id`={2:d} "
			"AND `active`=1 AND LOCATE({0:s},`serialized_data`)>0 "
		"UNION ALL SELECT 'house',CONCAT('house=',`house_id`) "
			"FROM `tile_store` WHERE LOCATE({0:s},`data`)>0"
		") AS `identity_matches` "
		"UNION ALL SELECT '__sentinel__',''",
		escapedInstanceId, floorSnapshotWorldId, floorSnapshotGenerationId));
	if (!result) {
		lookup.error = "database identity scan failed; absence cannot be certified";
		return lookup;
	}

	do {
		const std::string source = result->getString("source");
		if (source == "__sentinel__") {
			lookup.databaseAvailable = true;
			continue;
		}

		++lookup.databaseMatches;
		if (lookup.firstDatabaseLocation.empty()) {
			lookup.firstDatabaseLocation = source + " " + result->getString("location");
		}
	} while (result->next());

	if (!lookup.databaseAvailable) {
		lookup.error = "database identity scan did not complete";
		return lookup;
	}

	std::unordered_set<const Item*> visitedItems;
	auto recordLiveMatch = [&](const std::string& location) {
		++lookup.liveMatches;
		if (lookup.firstLiveLocation.empty()) {
			lookup.firstLiveLocation = location;
		}
	};

	std::function<void(const Item*, const std::string&)> inspectItem;
	inspectItem = [&](const Item* item, const std::string& location) {
		if (!item || !visitedItems.insert(item).second) {
			return;
		}

		if (item->getFloorPersistenceInstanceId() == instanceId) {
			recordLiveMatch(location);
		}

		const Container* container = item->getContainer();
		if (!container) {
			return;
		}

		uint32_t childIndex = 0;
		for (const Item* child : container->getItemList()) {
			inspectItem(child, fmt::format("{:s}.{:d}", location, ++childIndex));
		}
	};

	auto inspectTile = [&](const Tile* tile, const std::string& location) {
		if (!tile) {
			return;
		}

		inspectItem(tile->getGround(), location + " ground");
		const TileItemVector* items = tile->getItemList();
		if (!items) {
			return;
		}

		uint32_t itemIndex = 0;
		for (const Item* item : *items) {
			inspectItem(item, fmt::format("{:s} item={:d}", location, ++itemIndex));
		}
	};

	for (const auto& playerEntry : players) {
		const Player* onlinePlayer = playerEntry.second;
		const std::string playerLabel = fmt::format(
			"player={:s}/{:d}", onlinePlayer->getName(), onlinePlayer->getGUID());
		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			inspectItem(
				onlinePlayer->getInventoryItem(static_cast<slots_t>(slot)),
				fmt::format("{:s} inventory={:d}", playerLabel, slot));
		}

		inspectItem(onlinePlayer->getInbox(), playerLabel + " inbox");
		inspectItem(onlinePlayer->getStoreInbox(), playerLabel + " store_inbox");
		for (const auto& depotEntry : onlinePlayer->getLoadedDepotLockers()) {
			inspectItem(
				depotEntry.second.get(),
				fmt::format("{:s} depot_locker={:d}", playerLabel, depotEntry.first));
		}
		for (const auto& depotEntry : onlinePlayer->getLoadedDepotChests()) {
			inspectItem(
				depotEntry.second,
				fmt::format("{:s} depot={:d}", playerLabel, depotEntry.first));
		}
	}

	for (const auto& dirtyEntry : floorDirtyTiles) {
		const Position& position = dirtyEntry.first;
		inspectTile(
			map.getTile(position),
			fmt::format("dirty_floor={:d},{:d},{:d}", position.x, position.y, position.z));
	}

	for (const auto& houseEntry : map.houses.getHouses()) {
		const House* house = houseEntry.second;
		for (const HouseTile* tile : house->getTiles()) {
			const Position& position = tile->getPosition();
			inspectTile(
				tile,
				fmt::format("house={:d} tile={:d},{:d},{:d}",
					houseEntry.first, position.x, position.y, position.z));
		}
	}

	lookup.safeToRecreate = lookup.databaseMatches == 0 && lookup.liveMatches == 0;
	return lookup;
}

const FloorDirtyTileRecord* Game::getFloorDirtyTile(const Position& position) const
{
	auto iterator = floorDirtyTiles.find(position);
	return iterator != floorDirtyTiles.end() ? &iterator->second : nullptr;
}

bool Game::clearFloorDirtyTile(const Position& position)
{
	if (floorCheckpointTileGroups.find(position) != floorCheckpointTileGroups.end()) {
		return false;
	}
	return floorDirtyTiles.erase(position) != 0;
}

size_t Game::clearFloorDirtyTiles()
{
	const size_t count = floorDirtyTiles.size();
	floorDirtyTiles.clear();
	floorCheckpointGroups.clear();
	floorCheckpointTileGroups.clear();
	floorCheckpointPlayerGroups.clear();
	floorCheckpointHouseGroups.clear();
	floorCheckpointItemGroups.clear();
	return count;
}

void Game::start(ServiceManager* manager)
{
	serviceManager = manager;
	updateWorldTime();

	floorSnapshotShadowEnabled = g_config.getBoolean(ConfigManager::FLOOR_PERSISTENCE_SHADOW_ENABLED);
	floorSnapshotWorldId = static_cast<uint32_t>(g_config.getNumber(ConfigManager::FLOOR_PERSISTENCE_WORLD_ID));
	floorSnapshotGenerationId = static_cast<uint32_t>(g_config.getNumber(ConfigManager::FLOOR_PERSISTENCE_GENERATION_ID));
	floorSnapshotDebounceMs = static_cast<uint32_t>(g_config.getNumber(ConfigManager::FLOOR_PERSISTENCE_SNAPSHOT_DEBOUNCE_MS));
	floorSnapshotMaxDelayMs = static_cast<uint32_t>(g_config.getNumber(ConfigManager::FLOOR_PERSISTENCE_SNAPSHOT_MAX_DELAY_MS));
	floorSnapshotRetryMs = static_cast<uint32_t>(g_config.getNumber(ConfigManager::FLOOR_PERSISTENCE_SNAPSHOT_RETRY_MS));
	floorSnapshotBatchSize = static_cast<uint32_t>(g_config.getNumber(ConfigManager::FLOOR_PERSISTENCE_SNAPSHOT_BATCH_SIZE));
	if (floorSnapshotShadowEnabled) {
		std::cout << "Floor persistence stage 3 shadow snapshots enabled: world=" << floorSnapshotWorldId
		          << " generation=" << floorSnapshotGenerationId << " debounce_ms=" << floorSnapshotDebounceMs
		          << " max_delay_ms=" << floorSnapshotMaxDelayMs << " batch=" << floorSnapshotBatchSize
		          << ". Clean restart replay is automatic; crash recovery remains explicit." << std::endl;
		const bool recoveryPlanBuilt = buildFloorRecoveryPlan();
		if (!recoveryPlanBuilt || floorRecoveryPlan.mode == "RECOVERY_BLOCKED") {
			std::cout << "[Error - Game::start] Floor recovery plan is blocked: "
			          << (floorRecoveryPlan.validationError.empty() ? floorRecoveryPlan.reason : floorRecoveryPlan.validationError)
			          << ". Common-player login remains blocked." << std::endl;
		} else if (floorRecoveryPlan.mode == "CLEAN_RESTART") {
			const uint64_t cleanSourceSessionId = floorRecoveryPlan.sourceSessionId;
			std::cout << "Floor recovery stage 5.7 clean restart replay: applying source="
			          << cleanSourceSessionId << " before common-player login." << std::endl;
			if (!applyFloorRecovery(cleanSourceSessionId)) {
				std::cout << "[Error - Game::start] Automatic clean restart replay failed for source="
				          << cleanSourceSessionId << ": "
				          << (floorRecoveryPlan.applyError.empty() ?
				              "unknown apply failure" : floorRecoveryPlan.applyError)
				          << ". Common-player login remains blocked." << std::endl;
			} else {
				std::cout << "Floor recovery stage 5.7 clean restart replay completed: source="
				          << cleanSourceSessionId
				          << " restored=" << floorRecoveryPlan.applyRestoredItemCount
				          << " top=" << floorRecoveryPlan.applyRestoredTopItemCount
				          << " tiles=" << floorRecoveryPlan.applyTargetTiles
				          << " quarantine=" << floorRecoveryPlan.applyQuarantineItemCount
				          << " suppressed=" << floorRecoveryPlan.applySuppressedItemCount
				          << ". No recovery confirmation is required." << std::endl;
			}
		}
		if (!initializeFloorPersistenceSession()) {
			std::cout << "[Error - Game::start] Could not initialize the floor persistence save session." << std::endl;
		}
		g_scheduler.addEvent(createSchedulerTask(1000, std::bind(&Game::checkFloorSnapshots, this)));
	}

	if (g_config.getBoolean(ConfigManager::DEFAULT_WORLD_LIGHT)) {
		g_scheduler.addEvent(createSchedulerTask(EVENT_LIGHTINTERVAL, std::bind(&Game::checkLight, this)));
	}
	g_scheduler.addEvent(createSchedulerTask(EVENT_CREATURE_THINK_INTERVAL, std::bind(&Game::checkCreatures, this, 0)));
	g_scheduler.addEvent(createSchedulerTask(EVENT_DECAYINTERVAL, std::bind(&Game::checkDecay, this)));
	g_scheduler.addEvent(createSchedulerTask(1000, std::bind(&Game::checkItemActorAttributions, this)));
}

GameState_t Game::getGameState() const
{
	return gameState;
}

void Game::setWorldType(WorldType_t type)
{
	worldType = type;
}

void Game::setGameState(GameState_t newState)
{
	if (gameState == GAME_STATE_SHUTDOWN) {
		return; //this cannot be stopped
	}

	if (gameState == newState) {
		return;
	}

	gameState = newState;
	switch (newState) {
		case GAME_STATE_INIT: {
			groups.load();
			g_chat->load();
			loadSpawnRateBoost();

			map.spawns.startup();

			raids.loadFromXml();
			raids.startup();

			quests.loadFromXml();
			mounts.loadFromXml();

			loadMotdNum();
			loadPlayersRecord();
			loadAccountStorageValues();
			loadBestiaryMonsters();

			g_globalEvents->startup();
			break;
		}

		case GAME_STATE_SHUTDOWN: {
			g_globalEvents->execute(GLOBALEVENT_SHUTDOWN);

			bool controlledCleanSaveCommitted = false;
			if (floorSnapshotShadowEnabled && !floorCleanSaveWindowActive) {
				std::cout << "Controlled shutdown requested: committing player/floor clean save before exit..."
				          << std::endl;

				// setGameState assigned SHUTDOWN before entering this switch.
				// Temporarily move to CLOSING so the coordinated clean-save
				// routine can own the login barrier and the final player/tile
				// checkpoint.
				gameState = GAME_STATE_CLOSING;
				if (beginFloorPersistenceCleanSave()) {
					controlledCleanSaveCommitted = true;
					gameState = GAME_STATE_SHUTDOWN;
				} else {
					// Do not hang the process waiting on a clean save that was
					// refused or failed: under an OS-driven close (console X) the
					// process would be force-killed, losing player saves and
					// triggering crash recovery. Fall back to the legacy kick-all
					// + save so characters are persisted and the server exits;
					// floor state stays uncommitted for recovery to handle.
					std::cout << "[Error - Game::setGameState] Controlled clean save did not commit; "
					             "falling back to legacy player save and shutdown."
					          << std::endl;
					gameState = GAME_STATE_CLOSING;
				}
			}

			saveMotdNum();
			if (!controlledCleanSaveCommitted) {
				// Floor persistence may be disabled, or a coordinated clean
				// save may already have committed and left its login barrier
				// active. Preserve the legacy final save for those paths.
				auto it = players.begin();
				while (it != players.end()) {
					it->second->kickPlayer(true);
					it = players.begin();
				}
				saveGameState();
			}

			g_dispatcher.addTask(
				createTask(std::bind(&Game::shutdown, this)));

			g_scheduler.stop();
			g_databaseTasks.stop();
			g_dispatcher.stop();
			break;
		}

		case GAME_STATE_CLOSED: {
			/* kick all players without the CanAlwaysLogin flag */
			auto it = players.begin();
			while (it != players.end()) {
				if (!it->second->hasFlag(PlayerFlag_CanAlwaysLogin)) {
					it->second->kickPlayer(true);
					it = players.begin();
				} else {
					++it;
				}
			}

			saveGameState();
			break;
		}

		default:
			break;
	}
}

bool Game::saveGameState()
{
	bool success = true;
	if (gameState == GAME_STATE_NORMAL) {
		setGameState(GAME_STATE_MAINTAIN);
	}

	std::cout << "Saving server..." << std::endl;

	// A house boundary transfer may couple a player, a normal floor tile and
	// one or more houses. Commit every pending boundary group before any
	// independent player or full-house save can expose only one side.
	if (!flushFloorCheckpointGroups()) {
		std::cout << "[Error - Game::saveGameState] Failed to flush coordinated "
		             "player/floor/house checkpoints. Independent saves were not attempted."
		          << std::endl;
		if (gameState == GAME_STATE_MAINTAIN) {
			setGameState(GAME_STATE_NORMAL);
		}
		return false;
	}

	if (!saveAccountStorageValues()) {
		std::cout << "[Error - Game::saveGameState] Failed to save account-level storage values." << std::endl;
		success = false;
	}

	for (const auto& it : players) {
		it.second->loginPosition = it.second->getPosition();
		const bool playerSaved = IOLoginData::savePlayer(it.second);
		if (floorCleanSaveInProgress) {
			recordCleanSavePlayerResult(playerSaved);
		}
		if (!playerSaved) {
			success = false;
		}
	}

	if (!Map::save()) {
		success = false;
	}

	g_databaseTasks.flush();
	if (floorCleanSaveInProgress && success && floorCleanSavePlayerFailures == 0) {
		uint32_t savedTiles = 0;
		std::string error;
		if (!saveAllFloorSnapshotsForCleanSave(savedTiles, error)) {
			std::cout << "[Error - Game::saveGameState] Coordinated floor checkpoint failed: " << error << std::endl;
			success = false;
		} else {
			floorCleanSaveTileCount = savedTiles;
		}
	} else if (floorCleanSaveInProgress) {
		std::cout << "[Error - Game::saveGameState] Clean floor checkpoint was not attempted because a prerequisite save failed."
		          << std::endl;
		success = false;
	}

	if (gameState == GAME_STATE_MAINTAIN) {
		setGameState(GAME_STATE_NORMAL);
	}
	return success;
}

bool Game::loadMainMap(const std::string& filename)
{
	return map.loadMap("data/world/" + filename + ".otbm", true);
}

void Game::loadMap(const std::string& path)
{
	map.loadMap(path, false);
}

Cylinder* Game::internalGetCylinder(Player* player, const Position& pos) const
{
	if (pos.x != 0xFFFF) {
		return map.getTile(pos);
	}

	//container
	if (pos.y & 0x40) {
		uint8_t from_cid = pos.y & 0x0F;
		return player->getContainerByID(from_cid);
	}

	//inventory
	return player;
}

Thing* Game::internalGetThing(Player* player, const Position& pos, int32_t index, uint32_t spriteId, stackPosType_t type) const
{
	if (pos.x != 0xFFFF) {
		Tile* tile = map.getTile(pos);
		if (!tile) {
			return nullptr;
		}

		Thing* thing;
		switch (type) {
			case STACKPOS_LOOK: {
				return tile->getTopVisibleThing(player);
			}

			case STACKPOS_MOVE: {
				Item* item = tile->getTopDownItem();
				if (item && item->isMoveable()) {
					thing = item;
				} else {
					thing = tile->getTopVisibleCreature(player);
				}
				break;
			}

			case STACKPOS_USEITEM: {
				thing = tile->getUseItem(index);
				break;
			}

			case STACKPOS_TOPDOWN_ITEM: {
				thing = tile->getTopDownItem();
				break;
			}

			case STACKPOS_USETARGET: {
				thing = tile->getTopVisibleCreature(player);
				if (!thing) {
					thing = tile->getUseItem(index);
				}
				break;
			}

			default: {
				thing = nullptr;
				break;
			}
		}

		if (player && tile->hasFlag(TILESTATE_SUPPORTS_HANGABLE)) {
			//do extra checks here if the thing is accessible
			if (thing && thing->getItem()) {
				if (tile->hasProperty(CONST_PROP_ISVERTICAL)) {
					if (player->getPosition().x + 1 == tile->getPosition().x) {
						thing = nullptr;
					}
				} else { // horizontal
					if (player->getPosition().y + 1 == tile->getPosition().y) {
						thing = nullptr;
					}
				}
			}
		}
		return thing;
	}

	//container
	if (pos.y & 0x40) {
		uint8_t fromCid = pos.y & 0x0F;

		Container* parentContainer = player->getContainerByID(fromCid);
		if (!parentContainer) {
			return nullptr;
		}

		/*if (parentContainer->getID() == ITEM_BROWSEFIELD) {
			Tile* tile = parentContainer->getTile();
			if (tile && tile->hasFlag(TILESTATE_SUPPORTS_HANGABLE)) {
				if (tile->hasProperty(CONST_PROP_ISVERTICAL)) {
					if (player->getPosition().x + 1 == tile->getPosition().x) {
						return nullptr;
					}
				} else { // horizontal
					if (player->getPosition().y + 1 == tile->getPosition().y) {
						return nullptr;
					}
				}
			}
		}*/

		uint8_t slot = pos.z;
		return parentContainer->getItemByIndex(player->getContainerIndex(fromCid) + slot);
	} else if (pos.y == 0 && pos.z == 0) {
		const ItemType& it = Item::items.getItemIdByClientId(spriteId);
		if (it.id == 0) {
			return nullptr;
		}

		int32_t subType;
		if (it.isFluidContainer() && index < static_cast<int32_t>(sizeof(reverseFluidMap) / sizeof(uint8_t))) {
			subType = reverseFluidMap[index];
		} else {
			subType = -1;
		}

		return findItemOfType(player, it.id, true, subType);
	}

	//inventory
	slots_t slot = static_cast<slots_t>(pos.y);
	if (slot == CONST_SLOT_STORE_INBOX) {
		return player->getStoreInbox();
	}

	return player->getInventoryItem(slot);
}

void Game::internalGetPosition(Item* item, Position& pos, uint8_t& stackpos)
{
	pos.x = 0;
	pos.y = 0;
	pos.z = 0;
	stackpos = 0;

	Cylinder* topParent = item->getTopParent();
	if (topParent) {
		if (Player* player = dynamic_cast<Player*>(topParent)) {
			pos.x = 0xFFFF;

			Container* container = dynamic_cast<Container*>(item->getParent());
			if (container) {
				pos.y = static_cast<uint16_t>(0x40) | static_cast<uint16_t>(player->getContainerID(container));
				pos.z = container->getThingIndex(item);
				stackpos = pos.z;
			} else {
				pos.y = player->getThingIndex(item);
				stackpos = pos.y;
			}
		} else if (Tile* tile = topParent->getTile()) {
			pos = tile->getPosition();
			stackpos = tile->getThingIndex(item);
		}
	}
}

Creature* Game::getCreatureByID(uint32_t id)
{
	if (id <= Player::playerAutoID) {
		return getPlayerByID(id);
	} else if (id <= Monster::monsterAutoID) {
		return getMonsterByID(id);
	} else if (id <= Npc::npcAutoID) {
		return getNpcByID(id);
	}
	return nullptr;
}

Monster* Game::getMonsterByID(uint32_t id)
{
	if (id == 0) {
		return nullptr;
	}

	auto it = monsters.find(id);
	if (it == monsters.end()) {
		return nullptr;
	}
	return it->second;
}

Npc* Game::getNpcByID(uint32_t id)
{
	if (id == 0) {
		return nullptr;
	}

	auto it = npcs.find(id);
	if (it == npcs.end()) {
		return nullptr;
	}
	return it->second;
}

Player* Game::getPlayerByID(uint32_t id)
{
	if (id == 0) {
		return nullptr;
	}

	auto it = players.find(id);
	if (it == players.end()) {
		return nullptr;
	}
	return it->second;
}

Creature* Game::getCreatureByName(const std::string& s)
{
	if (s.empty()) {
		return nullptr;
	}

	const std::string& lowerCaseName = asLowerCaseString(s);

	{
		auto it = mappedPlayerNames.find(lowerCaseName);
		if (it != mappedPlayerNames.end()) {
			return it->second;
		}
	}

	auto equalCreatureName = [&](const std::pair<uint32_t, Creature*>& it) {
		auto name = it.second->getName();
		return lowerCaseName.size() == name.size() && std::equal(lowerCaseName.begin(), lowerCaseName.end(), name.begin(), [](char a, char b) {
			return a == std::tolower(b);
		});
	};

	{
		auto it = std::find_if(npcs.begin(), npcs.end(), equalCreatureName);
		if (it != npcs.end()) {
			return it->second;
		}
	}

	{
		auto it = std::find_if(monsters.begin(), monsters.end(), equalCreatureName);
		if (it != monsters.end()) {
			return it->second;
		}
	}

	return nullptr;
}

Npc* Game::getNpcByName(const std::string& s)
{
	if (s.empty()) {
		return nullptr;
	}

	const char* npcName = s.c_str();
	for (const auto& it : npcs) {
		if (strcasecmp(npcName, it.second->getName().c_str()) == 0) {
			return it.second;
		}
	}
	return nullptr;
}

Player* Game::getPlayerByName(const std::string& s)
{
	if (s.empty()) {
		return nullptr;
	}

	auto it = mappedPlayerNames.find(asLowerCaseString(s));
	if (it == mappedPlayerNames.end()) {
		return nullptr;
	}
	return it->second;
}

Player* Game::getPlayerByGUID(const uint32_t& guid)
{
	if (guid == 0) {
		return nullptr;
	}

	auto it = mappedPlayerGuids.find(guid);
	if (it == mappedPlayerGuids.end()) {
		return nullptr;
	}
	return it->second;
}

ReturnValue Game::getPlayerByNameWildcard(const std::string& s, Player*& player)
{
	size_t strlen = s.length();
	if (strlen == 0 || strlen > PLAYER_NAME_LENGTH) {
		return RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE;
	}

	if (s.back() == '~') {
		const std::string& query = asLowerCaseString(s.substr(0, strlen - 1));
		std::string result;
		ReturnValue ret = wildcardTree.findOne(query, result);
		if (ret != RETURNVALUE_NOERROR) {
			return ret;
		}

		player = getPlayerByName(result);
	} else {
		player = getPlayerByName(s);
	}

	if (!player) {
		return RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE;
	}

	return RETURNVALUE_NOERROR;
}

Player* Game::getPlayerByAccount(uint32_t acc)
{
	for (const auto& it : players) {
		if (it.second->getAccount() == acc) {
			return it.second;
		}
	}
	return nullptr;
}

bool Game::internalPlaceCreature(Creature* creature, const Position& pos, bool extendedPos /*=false*/, bool forced /*= false*/)
{
	if (creature->getParent() != nullptr) {
		return false;
	}

	if (!map.placeCreature(pos, creature, extendedPos, forced)) {
		return false;
	}

	creature->incrementReferenceCounter();
	creature->setID();
	creature->addList();
	return true;
}

bool Game::placeCreature(Creature* creature, const Position& pos, bool extendedPos /*=false*/, bool forced /*= false*/)
{
	if (!internalPlaceCreature(creature, pos, extendedPos, forced)) {
		return false;
	}

	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true);
	for (Creature* spectator : spectators) {
		if (Player* tmpPlayer = spectator->getPlayer()) {
			tmpPlayer->sendCreatureAppear(creature, creature->getPosition(), true);
		}
	}

	for (Creature* spectator : spectators) {
		spectator->onCreatureAppear(creature, true);
	}

	creature->getParent()->postAddNotification(creature, nullptr, 0);

	addCreatureCheck(creature);
	creature->onPlacedCreature();
	return true;
}

bool Game::removeCreature(Creature* creature, bool isLogout/* = true*/)
{
	if (creature->isRemoved()) {
		return false;
	}

	Player* removedPlayer = creature->getPlayer();
	DispatcherPhaseMetricsTimer removeCreatureTimer(
		DispatcherMetricsPhase::LOGOUT_REMOVE_CREATURE_TOTAL,
		removedPlayer != nullptr);

	if (Player* player = removedPlayer) {
		g_playerShop.onPlayerDisappear(player);
	}

	DispatcherPhaseMetricsTimer mapRemoveTimer(
		DispatcherMetricsPhase::LOGOUT_MAP_REMOVE_NOTIFY,
		removedPlayer != nullptr);
	Tile* tile = creature->getTile();

	std::vector<int32_t> oldStackPosVector;

	SpectatorVec spectators;
	map.getSpectators(spectators, tile->getPosition(), true);
	for (Creature* spectator : spectators) {
		if (Player* player = spectator->getPlayer()) {
			oldStackPosVector.push_back(player->canSeeCreature(creature) ? tile->getClientIndexOfCreature(player, creature) : -1);
		}
	}

	tile->removeCreature(creature);

	const Position& tilePosition = tile->getPosition();

	//send to client
	size_t i = 0;
	for (Creature* spectator : spectators) {
		if (Player* player = spectator->getPlayer()) {
			if (player->canSeeCreature(creature)) {
				player->sendRemoveTileCreature(creature, tilePosition, oldStackPosVector[i++]);
			}
		}
	}
	mapRemoveTimer.stop();

	//event method
	DispatcherPhaseMetricsTimer callbacksTimer(
		DispatcherMetricsPhase::LOGOUT_CALLBACKS,
		removedPlayer != nullptr);
	for (Creature* spectator : spectators) {
		spectator->onRemoveCreature(creature, isLogout);
	}
	callbacksTimer.stop();

	DispatcherPhaseMetricsTimer finalDetachTimer(
		DispatcherMetricsPhase::LOGOUT_FINAL_DETACH,
		removedPlayer != nullptr);
	creature->getParent()->postRemoveNotification(creature, nullptr, 0);

	creature->removeList();
	creature->setRemoved();
	ReleaseCreature(creature);

	removeCreatureCheck(creature);

	for (Creature* summon : creature->summons) {
		summon->setSkillLoss(false);
		removeCreature(summon);
	}
	return true;
}

void Game::executeDeath(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (creature && !creature->isRemoved()) {
		creature->onDeath();
	}
}

void Game::playerMoveThing(uint32_t playerId, const Position& fromPos,
                           uint16_t spriteId, uint8_t fromStackPos, const Position& toPos, uint8_t count)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	uint8_t fromIndex = 0;
	if (fromPos.x == 0xFFFF) {
		if (fromPos.y & 0x40) {
			fromIndex = fromPos.z;
		} else {
			fromIndex = static_cast<uint8_t>(fromPos.y);
		}
	} else {
		fromIndex = fromStackPos;
	}

	Thing* thing = internalGetThing(player, fromPos, fromIndex, 0, STACKPOS_MOVE);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (Creature* movingCreature = thing->getCreature()) {
		Tile* tile = map.getTile(toPos);
		if (!tile) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		if (Position::areInRange<1, 1, 0>(movingCreature->getPosition(), player->getPosition())) {
			SchedulerTask* task = createSchedulerTask(MOVE_CREATURE_INTERVAL,
			                      std::bind(&Game::playerMoveCreatureByID, this, player->getID(),
			                                  movingCreature->getID(), movingCreature->getPosition(), tile->getPosition()));
			player->setNextActionTask(task);
		} else {
			playerMoveCreature(player, movingCreature, movingCreature->getPosition(), tile);
		}
	} else if (thing->getItem()) {
		Cylinder* toCylinder = internalGetCylinder(player, toPos);
		if (!toCylinder) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		playerMoveItem(player, fromPos, spriteId, fromStackPos, toPos, count, thing->getItem(), toCylinder);
	}
}

void Game::playerMoveCreatureByID(uint32_t playerId, uint32_t movingCreatureId, const Position& movingCreatureOrigPos, const Position& toPos)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Creature* movingCreature = getCreatureByID(movingCreatureId);
	if (!movingCreature) {
		return;
	}

	Tile* toTile = map.getTile(toPos);
	if (!toTile) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	playerMoveCreature(player, movingCreature, movingCreatureOrigPos, toTile);
}

void Game::playerMoveCreature(Player* player, Creature* movingCreature, const Position& movingCreatureOrigPos, Tile* toTile)
{
	if (g_playerShop.shouldBlockMovement(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}
	if (Player* movingPlayer = movingCreature->getPlayer()) {
		if (g_playerShop.shouldBlockMovement(movingPlayer)) {
			player->sendCancelMessage(RETURNVALUE_NOTMOVEABLE);
			return;
		}
	}

	if (!player->canDoAction()) {
		uint32_t delay = player->getNextActionTime();
		SchedulerTask* task = createSchedulerTask(delay, std::bind(&Game::playerMoveCreatureByID,
			this, player->getID(), movingCreature->getID(), movingCreatureOrigPos, toTile->getPosition()));
		player->setNextActionTask(task);
		return;
	}

	if (movingCreature->isMovementBlocked()) {
		player->sendCancelMessage(RETURNVALUE_NOTMOVEABLE);
		return;
	}

	player->setNextActionTask(nullptr);

	if (!Position::areInRange<1, 1, 0>(movingCreatureOrigPos, player->getPosition())) {
		//need to walk to the creature first before moving it
		std::vector<Direction> listDir;
		if (player->getPathTo(movingCreatureOrigPos, listDir, 0, 1, true, true)) {
			g_dispatcher.addTask(createTask(std::bind(&Game::playerAutoWalk,
			                                this, player->getID(), std::move(listDir))));
			SchedulerTask* task = createSchedulerTask(RANGE_MOVE_CREATURE_INTERVAL, std::bind(&Game::playerMoveCreatureByID, this,
				player->getID(), movingCreature->getID(), movingCreatureOrigPos, toTile->getPosition()));
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	if ((!movingCreature->isPushable() && !player->hasFlag(PlayerFlag_CanPushAllCreatures)) ||
	        (movingCreature->isInGhostMode() && !player->canSeeGhostMode(movingCreature))) {
		player->sendCancelMessage(RETURNVALUE_NOTMOVEABLE);
		return;
	}

	//check throw distance
	const Position& movingCreaturePos = movingCreature->getPosition();
	const Position& toPos = toTile->getPosition();
	if ((Position::getDistanceX(movingCreaturePos, toPos) > movingCreature->getThrowRange()) || (Position::getDistanceY(movingCreaturePos, toPos) > movingCreature->getThrowRange()) || (Position::getDistanceZ(movingCreaturePos, toPos) * 4 > movingCreature->getThrowRange())) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		return;
	}

	if (player != movingCreature) {
		if (toTile->hasFlag(TILESTATE_BLOCKPATH)) {
			player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
			return;
		} else if ((movingCreature->getZone() == ZONE_PROTECTION && !toTile->hasFlag(TILESTATE_PROTECTIONZONE)) || (movingCreature->getZone() == ZONE_NOPVP && !toTile->hasFlag(TILESTATE_NOPVPZONE))) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		} else {
			if (CreatureVector* tileCreatures = toTile->getCreatures()) {
				for (Creature* tileCreature : *tileCreatures) {
					if (!tileCreature->isInGhostMode()) {
						player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
						return;
					}
				}
			}

			Npc* movingNpc = movingCreature->getNpc();
			if (movingNpc && !Spawns::isInZone(movingNpc->getMasterPos(), movingNpc->getMasterRadius(), toPos)) {
				player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
				return;
			}
		}
	}

	if (!g_events->eventPlayerOnMoveCreature(player, movingCreature, movingCreaturePos, toPos)) {
		return;
	}

	ReturnValue ret = internalMoveCreature(*movingCreature, *toTile);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
	}
}

ReturnValue Game::internalMoveCreature(Creature* creature, Direction direction, uint32_t flags /*= 0*/)
{
	creature->setLastPosition(creature->getPosition());
	const Position& currentPos = creature->getPosition();
	Position destPos = getNextPosition(direction, currentPos);
	Player* player = creature->getPlayer();

	bool diagonalMovement = (direction & DIRECTION_DIAGONAL_MASK) != 0;
	if (player && !diagonalMovement) {
		//try to go up
		if (currentPos.z != 8 && creature->getTile()->hasHeight(3)) {
			Tile* tmpTile = map.getTile(currentPos.x, currentPos.y, currentPos.getZ() - 1);
			if (tmpTile == nullptr || (tmpTile->getGround() == nullptr && !tmpTile->hasFlag(TILESTATE_BLOCKSOLID))) {
				tmpTile = map.getTile(destPos.x, destPos.y, destPos.getZ() - 1);
				if (tmpTile && tmpTile->getGround() && !tmpTile->hasFlag(TILESTATE_IMMOVABLEBLOCKSOLID)) {
					flags |= FLAG_IGNOREBLOCKITEM | FLAG_IGNOREBLOCKCREATURE;

					if (!tmpTile->hasFlag(TILESTATE_FLOORCHANGE)) {
						player->setDirection(direction);
						destPos.z--;
					}
				}
			}
		}

		//try to go down
		if (currentPos.z != 7 && currentPos.z == destPos.z) {
			Tile* tmpTile = map.getTile(destPos.x, destPos.y, destPos.z);
			if (tmpTile == nullptr || (tmpTile->getGround() == nullptr && !tmpTile->hasFlag(TILESTATE_BLOCKSOLID))) {
				tmpTile = map.getTile(destPos.x, destPos.y, destPos.z + 1);
				if (tmpTile && tmpTile->hasHeight(3) && !tmpTile->hasFlag(TILESTATE_IMMOVABLEBLOCKSOLID)) {
					flags |= FLAG_IGNOREBLOCKITEM | FLAG_IGNOREBLOCKCREATURE;
					player->setDirection(direction);
					destPos.z++;
				}
			}
		}
	}

	Tile* toTile = map.getTile(destPos);
	if (!toTile) {
		return RETURNVALUE_NOTPOSSIBLE;
	}
	return internalMoveCreature(*creature, *toTile, flags);
}

ReturnValue Game::internalMoveCreature(Creature& creature, Tile& toTile, uint32_t flags /*= 0*/)
{
	//check if we can move the creature to the destination
	ReturnValue ret = toTile.queryAdd(0, creature, 1, flags);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	map.moveCreature(creature, toTile);
	if (creature.getParent() != &toTile) {
		return RETURNVALUE_NOERROR;
	}

	int32_t index = 0;
	Item* toItem = nullptr;
	Tile* subCylinder = nullptr;
	Tile* toCylinder = &toTile;
	Tile* fromCylinder = nullptr;
	uint32_t n = 0;

	while ((subCylinder = toCylinder->queryDestination(index, creature, &toItem, flags)) != toCylinder) {
		map.moveCreature(creature, *subCylinder);

		if (creature.getParent() != subCylinder) {
			//could happen if a script move the creature
			fromCylinder = nullptr;
			break;
		}

		fromCylinder = toCylinder;
		toCylinder = subCylinder;
		flags = 0;

		//to prevent infinite loop
		if (++n >= MAP_MAX_LAYERS) {
			break;
		}
	}

	if (fromCylinder) {
		const Position& fromPosition = fromCylinder->getPosition();
		const Position& toPosition = toCylinder->getPosition();
		if (fromPosition.z != toPosition.z && (fromPosition.x != toPosition.x || fromPosition.y != toPosition.y)) {
			Direction dir = getDirectionTo(fromPosition, toPosition);
			if ((dir & DIRECTION_DIAGONAL_MASK) == 0) {
				internalCreatureTurn(&creature, dir);
			}
		}
	}

	if (Player* movedPlayer = creature.getPlayer()) {
		g_playerShop.onPlayerMoved(movedPlayer);
	}

	return RETURNVALUE_NOERROR;
}

void Game::playerMoveItemByPlayerID(uint32_t playerId, const Position& fromPos, uint16_t spriteId, uint8_t fromStackPos, const Position& toPos, uint8_t count)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	playerMoveItem(player, fromPos, spriteId, fromStackPos, toPos, count, nullptr, nullptr);
}

void Game::playerMoveItem(Player* player, const Position& fromPos,
                          uint16_t spriteId, uint8_t fromStackPos, const Position& toPos, uint8_t count, Item* item, Cylinder* toCylinder)
{
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (item == nullptr) {
		uint8_t fromIndex = 0;
		if (fromPos.x == 0xFFFF) {
			if (fromPos.y & 0x40) {
				fromIndex = fromPos.z;
			} else {
				fromIndex = static_cast<uint8_t>(fromPos.y);
			}
		} else {
			fromIndex = fromStackPos;
		}

		Thing* thing = internalGetThing(player, fromPos, fromIndex, 0, STACKPOS_MOVE);
		if (!thing || !thing->getItem()) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		item = thing->getItem();
	}

	if (item->getClientID() != spriteId) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Cylinder* fromCylinder = internalGetCylinder(player, fromPos);
	if (fromCylinder == nullptr) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (toCylinder == nullptr) {
		toCylinder = internalGetCylinder(player, toPos);
		if (toCylinder == nullptr) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}
	}

	if (!item->isPushable() || item->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTMOVEABLE);
		return;
	}

	const Position& playerPos = player->getPosition();
	const Position& mapFromPos = fromCylinder->getTile()->getPosition();
	if (playerPos.z != mapFromPos.z) {
		player->sendCancelMessage(playerPos.z > mapFromPos.z ? RETURNVALUE_FIRSTGOUPSTAIRS : RETURNVALUE_FIRSTGODOWNSTAIRS);
		return;
	}

	if (!Position::areInRange<1, 1>(playerPos, mapFromPos)) {
		//need to walk to the item first before using it
		std::vector<Direction> listDir;
		if (player->getPathTo(item->getPosition(), listDir, 0, 1, true, true)) {
			g_dispatcher.addTask(createTask(std::bind(&Game::playerAutoWalk,
			                                this, player->getID(), std::move(listDir))));

			SchedulerTask* task = createSchedulerTask(RANGE_MOVE_ITEM_INTERVAL, std::bind(&Game::playerMoveItemByPlayerID, this,
			                      player->getID(), fromPos, spriteId, fromStackPos, toPos, count));
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	const Tile* toCylinderTile = toCylinder->getTile();
	const Position& mapToPos = toCylinderTile->getPosition();

	//hangable item specific code
	if (item->isHangable() && toCylinderTile->hasFlag(TILESTATE_SUPPORTS_HANGABLE)) {
		//destination supports hangable objects so need to move there first
		bool vertical = toCylinderTile->hasProperty(CONST_PROP_ISVERTICAL);
		if (vertical) {
			if (playerPos.x + 1 == mapToPos.x) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return;
			}
		} else { // horizontal
			if (playerPos.y + 1 == mapToPos.y) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return;
			}
		}

		if (!Position::areInRange<1, 1, 0>(playerPos, mapToPos)) {
			Position walkPos = mapToPos;
			if (vertical) {
				walkPos.x++;
			} else {
				walkPos.y++;
			}

			Position itemPos = fromPos;
			uint8_t itemStackPos = fromStackPos;

			if (fromPos.x != 0xFFFF && Position::areInRange<1, 1>(mapFromPos, playerPos)
			        && !Position::areInRange<1, 1, 0>(mapFromPos, walkPos)) {
				//need to pickup the item first
				Item* moveItem = nullptr;

				ReturnValue ret = internalMoveItem(fromCylinder, player, INDEX_WHEREEVER, item, count, &moveItem, 0, player, nullptr, &fromPos, &toPos);
				if (ret != RETURNVALUE_NOERROR) {
					player->sendCancelMessage(ret);
					return;
				}

				//changing the position since its now in the inventory of the player
				internalGetPosition(moveItem, itemPos, itemStackPos);
			}

			std::vector<Direction> listDir;
			if (player->getPathTo(walkPos, listDir, 0, 0, true, true)) {
				g_dispatcher.addTask(createTask(std::bind(&Game::playerAutoWalk,
				                                this, player->getID(), std::move(listDir))));

				SchedulerTask* task = createSchedulerTask(RANGE_MOVE_ITEM_INTERVAL, std::bind(&Game::playerMoveItemByPlayerID, this,
				                      player->getID(), itemPos, spriteId, itemStackPos, toPos, count));
				player->setNextWalkActionTask(task);
			} else {
				player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
			}
			return;
		}
	}

	if (!item->isPickupable() && playerPos.z != mapToPos.z) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		return;
	}

	int32_t throwRange = item->getThrowRange();
	if ((Position::getDistanceX(playerPos, mapToPos) > throwRange) ||
	        (Position::getDistanceY(playerPos, mapToPos) > throwRange)) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		return;
	}

	if (!canThrowObjectTo(mapFromPos, mapToPos, true, false, throwRange, throwRange)) {
		player->sendCancelMessage(RETURNVALUE_CANNOTTHROW);
		return;
	}

	uint8_t toIndex = 0;
	if (toPos.x == 0xFFFF) {
		if (toPos.y & 0x40) {
			toIndex = toPos.z;
		} else {
			toIndex = static_cast<uint8_t>(toPos.y);
		}
	}

	ReturnValue ret = internalMoveItem(fromCylinder, toCylinder, toIndex, item, count, nullptr, 0, player, nullptr, &fromPos, &toPos);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
	}
}

ReturnValue Game::internalMoveItem(Cylinder* fromCylinder, Cylinder* toCylinder, int32_t index,
                                   Item* item, uint32_t count, Item** _moveItem, uint32_t flags /*= 0*/, Creature* actor/* = nullptr*/, Item* tradeItem/* = nullptr*/, const Position* fromPos /*= nullptr*/, const Position* toPos/*= nullptr*/)
{
	DispatcherPhaseMetricsTimer moveTimer(DispatcherMetricsPhase::ITEM_MOVE_TOTAL);

	Player* actorPlayer = actor ? actor->getPlayer() : nullptr;
	if (actorPlayer && fromPos && toPos) {
		if (!g_events->eventPlayerOnMoveItem(actorPlayer, item, count, *fromPos, *toPos, fromCylinder, toCylinder)) {
			return RETURNVALUE_NOTPOSSIBLE;
		}
	}

	/*Tile* fromTile = fromCylinder->getTile();
	if (fromTile) {
		auto it = browseFields.find(fromTile);
		if (it != browseFields.end() && it->second == fromCylinder) {
			fromCylinder = fromTile;
		}
	}*/

	Item* toItem = nullptr;

	Cylinder* subCylinder;
	int floorN = 0;

	while ((subCylinder = toCylinder->queryDestination(index, *item, &toItem, flags)) != toCylinder) {
		toCylinder = subCylinder;
		flags = 0;

		//to prevent infinite loop
		if (++floorN >= MAP_MAX_LAYERS) {
			break;
		}
	}

	//destination is the same as the source?
	if (item == toItem) {
		return RETURNVALUE_NOERROR; //silently ignore move
	}

	FloorDirtyPlayerMutationScope floorDirtyPlayerMutationScope(*this, actorPlayer != nullptr);
	ItemMovePersistenceMetricsScope persistenceMetricsScope;

	//check if we can add this item
	ReturnValue ret = toCylinder->queryAdd(index, *item, count, flags, actor);
	if (ret == RETURNVALUE_NEEDEXCHANGE) {
		//check if we can add it to source cylinder
		ret = fromCylinder->queryAdd(fromCylinder->getThingIndex(item), *toItem, toItem->getItemCount(), 0);
		if (ret == RETURNVALUE_NOERROR) {
			if (actorPlayer && fromPos && toPos && !g_events->eventPlayerOnMoveItem(actorPlayer, toItem, count, *toPos, *fromPos, toCylinder, fromCylinder)) {
				return RETURNVALUE_NOTPOSSIBLE;
			}

			//check how much we can move
			uint32_t maxExchangeQueryCount = 0;
			ReturnValue retExchangeMaxCount = fromCylinder->queryMaxCount(INDEX_WHEREEVER, *toItem, toItem->getItemCount(), maxExchangeQueryCount, 0);

			if (retExchangeMaxCount != RETURNVALUE_NOERROR && maxExchangeQueryCount == 0) {
				return retExchangeMaxCount;
			}

			if (toCylinder->queryRemove(*toItem, toItem->getItemCount(), flags, actor) == RETURNVALUE_NOERROR) {
				int32_t oldToItemIndex = toCylinder->getThingIndex(toItem);
				toCylinder->removeThing(toItem, toItem->getItemCount());

				// Cylinder::addThing can notify clients immediately. The exchange
				// has passed every query and the source removal succeeded, so
				// identify it now, before its first destination packet is built.
				if (actorPlayer) {
					toItem->markAsPlayerMovedForFloorPersistence();
				}
				fromCylinder->addThing(toItem);

				if (actorPlayer) {
					persistenceMetricsScope.begin();
					stampFloorPersistenceActorAfterPlayerMutation(fromCylinder, toItem, actorPlayer);
					identifyFloorPersistenceMovableContainerAfterPlayerMutation(toCylinder, actorPlayer);
					identifyFloorPersistenceMovableContainerAfterPlayerMutation(fromCylinder, actorPlayer);
					attributeSuccessfulItemEndpoint(
						fromCylinder, toItem, actorPlayer->getGUID());
					attributeContainerPathAfterMutation(
						toCylinder, actorPlayer->getGUID());
					persistenceMetricsScope.end();
				}

				if (oldToItemIndex != -1) {
					toCylinder->postRemoveNotification(toItem, fromCylinder, oldToItemIndex);
				}

				int32_t newToItemIndex = fromCylinder->getThingIndex(toItem);
				if (newToItemIndex != -1) {
					fromCylinder->postAddNotification(toItem, toCylinder, newToItemIndex);
				}

				ret = toCylinder->queryAdd(index, *item, count, flags);

				if (actorPlayer && fromPos && toPos) {
					g_events->eventPlayerOnItemMoved(actorPlayer, toItem, count, *toPos, *fromPos, toCylinder, fromCylinder);
				}

				toItem = nullptr;
			}
		}
	}

	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	//check how much we can move
	uint32_t maxQueryCount = 0;
	ReturnValue retMaxCount = toCylinder->queryMaxCount(index, *item, count, maxQueryCount, flags);
	if (retMaxCount != RETURNVALUE_NOERROR && maxQueryCount == 0) {
		return retMaxCount;
	}

	uint32_t m;
	if (item->isStackable()) {
		m = std::min<uint32_t>(count, maxQueryCount);
	} else {
		m = maxQueryCount;
	}

	Item* moveItem = item;
	Item* sourceRemainder = nullptr;

	//check if we can remove this item
	ret = fromCylinder->queryRemove(*item, m, flags, actor);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	if (tradeItem) {
		if (toCylinder->getItem() == tradeItem) {
			return RETURNVALUE_NOTENOUGHROOM;
		}

		Cylinder* tmpCylinder = toCylinder->getParent();
		while (tmpCylinder) {
			if (tmpCylinder->getItem() == tradeItem) {
				return RETURNVALUE_NOTENOUGHROOM;
			}

			tmpCylinder = tmpCylinder->getParent();
		}
	}

	//remove the item
	int32_t itemIndex = fromCylinder->getThingIndex(item);
	Item* updateItem = nullptr;
	const bool sourceCreatureStack = isCreatureStack(item);
	const bool destinationCreatureStack = isCreatureStack(toItem);
	fromCylinder->removeThing(item, m);

	//update item(s)
	if (item->isStackable()) {
		uint32_t n;

		if (item->equals(toItem)) {
			n = std::min<uint32_t>(100 - toItem->getItemCount(), m);
			clearCreatureStackAfterMixedMerge(
				toItem, sourceCreatureStack, destinationCreatureStack, n);
			toCylinder->updateThing(toItem, toItem->getID(), toItem->getItemCount() + n);
			updateItem = toItem;
		} else {
			n = 0;
		}

		int32_t newCount = m - n;
		if (newCount > 0) {
			moveItem = item->clone();
			moveItem->setItemCount(newCount);
		} else {
			moveItem = nullptr;
		}

		if (item->isRemoved()) {
			ReleaseItem(item);
		} else {
			sourceRemainder = item;
		}
	}

	// Cylinder::addThing emits the first tile/container/inventory packet itself.
	// All move validations and the source removal have succeeded at this point,
	// so assign the identity immediately before insertion. Rejected moves return
	// above and never reach this operation.
	if (actorPlayer && moveItem) {
		moveItem->markAsPlayerMovedForFloorPersistence();
	}

	Tile* physicalMailboxTile = nullptr;
	const bool directMailboxDestination = dynamic_cast<Mailbox*>(toCylinder) != nullptr;
	if (!directMailboxDestination) {
		Tile* destinationTile = dynamic_cast<Tile*>(toCylinder);
		if (destinationTile && destinationTile->hasFlag(TILESTATE_MAILBOX) &&
		    destinationTile->getMailbox()) {
			physicalMailboxTile = destinationTile;
		}
	}
	const bool startsAtomicMailCheckpoint =
		actorPlayer && moveItem &&
		(moveItem->getID() == ITEM_PARCEL || moveItem->getID() == ITEM_LETTER) &&
		(directMailboxDestination || physicalMailboxTile) &&
		!activeMailTransferCheckpoint.active;
	if (startsAtomicMailCheckpoint) {
		activeMailTransferCheckpoint = {};
		activeMailTransferCheckpoint.sourceCylinder = fromCylinder;
		activeMailTransferCheckpoint.sender = actorPlayer;
		activeMailTransferCheckpoint.mailboxTile = physicalMailboxTile;
		activeMailTransferCheckpoint.sourceIndex = itemIndex;
		activeMailTransferCheckpoint.originalItemId = moveItem->getID();
		activeMailTransferCheckpoint.active = true;
	}

	//add item
	if (moveItem /*m - n > 0*/) {
		toCylinder->addThing(index, moveItem);
	}

	bool atomicMailCommitted = false;

	if (actorPlayer) {
		persistenceMetricsScope.begin();
		stampFloorPersistenceActorAfterPlayerMutation(fromCylinder, sourceRemainder, actorPlayer);
		stampFloorPersistenceActorAfterPlayerMutation(toCylinder, moveItem, actorPlayer);
		if (updateItem && updateItem != moveItem) {
			stampFloorPersistenceActorAfterPlayerMutation(toCylinder, updateItem, actorPlayer);
		}
		if (updateItem && updateItem != moveItem) {
			updateItem->markAsPlayerMovedForFloorPersistence();
		}
		identifyFloorPersistenceMovableContainerAfterPlayerMutation(fromCylinder, actorPlayer);
		identifyFloorPersistenceMovableContainerAfterPlayerMutation(toCylinder, actorPlayer);

		const uint32_t sourceActorGuid = actorPlayer->getGUID();
		Player* destinationOwner = findPlayerStorageOwner(toCylinder);
		const uint32_t destinationActorGuid = destinationOwner ?
			destinationOwner->getGUID() : sourceActorGuid;
		attributeSuccessfulItemEndpoint(fromCylinder, sourceRemainder, sourceActorGuid);
		attributeContainerPathAfterMutation(fromCylinder, sourceActorGuid);
		attributeSuccessfulItemEndpoint(toCylinder, moveItem, destinationActorGuid);
		if (updateItem && updateItem != moveItem) {
			attributeSuccessfulItemEndpoint(toCylinder, updateItem, destinationActorGuid);
		}
		attributeContainerPathAfterMutation(toCylinder, destinationActorGuid);
		persistenceMetricsScope.end();
	} else if (Player* destinationOwner = findPlayerStorageOwner(toCylinder);
	           destinationOwner &&
	           destinationOwner->getTradeState() != TRADE_TRANSFER) {
		persistenceMetricsScope.begin();
		const uint32_t destinationActorGuid = destinationOwner->getGUID();
		attributeSuccessfulItemEndpoint(toCylinder, moveItem, destinationActorGuid);
		if (updateItem && updateItem != moveItem) {
			attributeSuccessfulItemEndpoint(toCylinder, updateItem, destinationActorGuid);
		}
		attributeContainerPathAfterMutation(toCylinder, destinationActorGuid);
		persistenceMetricsScope.end();
	}

	if (itemIndex != -1) {
		fromCylinder->postRemoveNotification(item, toCylinder, itemIndex);
	}

	if (moveItem) {
		int32_t moveItemIndex = toCylinder->getThingIndex(moveItem);
		if (moveItemIndex != -1) {
			toCylinder->postAddNotification(moveItem, fromCylinder, moveItemIndex);
		}
	}

	if (updateItem) {
		int32_t updateItemIndex = toCylinder->getThingIndex(updateItem);
		if (updateItemIndex != -1) {
			toCylinder->postAddNotification(updateItem, fromCylinder, updateItemIndex);
		}
	}

	if (startsAtomicMailCheckpoint) {
		// For a physical mailbox tile, sendItem is called by the post-add
		// notification immediately above. Keep the active context alive until
		// that notification has either committed the joint transaction or
		// returned the parcel to its original source.
		atomicMailCommitted = activeMailTransferCheckpoint.committed;
		const bool atomicMailRolledBack = activeMailTransferCheckpoint.rolledBack;
		const std::string atomicMailError = activeMailTransferCheckpoint.error;
		activeMailTransferCheckpoint = {};
		if (!atomicMailCommitted) {
			if (!atomicMailRolledBack) {
				std::cout << "[Error - Game::internalMoveItem] Mail delivery failed and "
					"the parcel could not be restored: " << atomicMailError << std::endl;
			}
			return RETURNVALUE_NOTPOSSIBLE;
		}
	}

	if (_moveItem) {
		if (moveItem) {
			*_moveItem = moveItem;
		} else {
			*_moveItem = item;
		}
	}

	//we could not move all, inform the player
	if (item->isStackable() && maxQueryCount < count) {
		return retMaxCount;
	}

	if (moveItem && moveItem->getDuration() > 0) {
		if (moveItem->getDecaying() != DECAYING_TRUE) {
			if (emergencyActive) {
				startDecay(moveItem);
			} else {
				moveItem->incrementReferenceCounter();
				moveItem->setDecaying(DECAYING_TRUE);
				toDecayItems.push_front(moveItem);
			}
		}
	}

	if (actorPlayer && !atomicMailCommitted) {
		persistenceMetricsScope.begin();
		Item* checkpointItem = moveItem ? moveItem : updateItem;
		registerFloorCheckpointTransfer(fromCylinder, toCylinder, checkpointItem, actorPlayer);
		persistenceMetricsScope.end();
	}

	if (actorPlayer && fromPos && toPos) {
		g_events->eventPlayerOnItemMoved(actorPlayer, item, count, *fromPos, *toPos, fromCylinder, toCylinder);
	}

	return ret;
}

ReturnValue Game::internalAddItem(Cylinder* toCylinder, Item* item, int32_t index /*= INDEX_WHEREEVER*/,
                                  uint32_t flags/* = 0*/, bool test/* = false*/)
{
	uint32_t remainderCount = 0;
	return internalAddItem(toCylinder, item, index, flags, test, remainderCount, false, 0);
}

ReturnValue Game::internalAddItem(Cylinder* toCylinder, Item* item, int32_t index,
                                  uint32_t flags, bool test, uint32_t& remainderCount,
                                  bool identifyForFloorPersistence, uint32_t attributionGuid)
{
	if (toCylinder == nullptr || item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	Cylinder* destCylinder = toCylinder;
	Item* toItem = nullptr;
	toCylinder = toCylinder->queryDestination(index, *item, &toItem, flags);
	if (attributionGuid == 0) {
		if (Player* destinationOwner = findPlayerStorageOwner(toCylinder)) {
			attributionGuid = destinationOwner->getGUID();
		}
	}

	//check if we can add this item
	ReturnValue ret = toCylinder->queryAdd(index, *item, item->getItemCount(), flags);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	/*
	Check if we can move add the whole amount, we do this by checking against the original cylinder,
	since the queryDestination can return a cylinder that might only hold a part of the full amount.
	*/
	uint32_t maxQueryCount = 0;
	ret = destCylinder->queryMaxCount(INDEX_WHEREEVER, *item, item->getItemCount(), maxQueryCount, flags);

	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	if (test) {
		return RETURNVALUE_NOERROR;
	}

	if (item->isStackable() && item->equals(toItem)) {
		uint32_t m = std::min<uint32_t>(item->getItemCount(), maxQueryCount);
		uint32_t n = std::min<uint32_t>(100 - toItem->getItemCount(), m);
		const bool sourceCreatureStack = isCreatureStack(item);
		const bool destinationCreatureStack = isCreatureStack(toItem);

		clearCreatureStackAfterMixedMerge(
			toItem, sourceCreatureStack, destinationCreatureStack, n);
		toCylinder->updateThing(toItem, toItem->getID(), toItem->getItemCount() + n);
		if (attributionGuid != 0) {
			attributeSuccessfulItemEndpoint(toCylinder, toItem, attributionGuid);
		}

		int32_t count = m - n;
		if (count > 0) {
			if (item->getItemCount() != count) {
				Item* remainderItem = item->clone();
				remainderItem->setItemCount(count);
				uint32_t nestedRemainderCount = 0;
				if (internalAddItem(
				        destCylinder, remainderItem, INDEX_WHEREEVER, flags, false,
				        nestedRemainderCount, identifyForFloorPersistence,
				        attributionGuid) != RETURNVALUE_NOERROR) {
					ReleaseItem(remainderItem);
					remainderCount = count;
				}
			} else {
				if (identifyForFloorPersistence) {
					// addThing may send the first client packet synchronously.
					// Every add query has succeeded, so identify immediately
					// before the confirmed insertion.
					item->markAsPlayerMovedForFloorPersistence();
				}
				toCylinder->addThing(index, item);
				if (attributionGuid != 0) {
					attributeSuccessfulItemEndpoint(toCylinder, item, attributionGuid);
				}

				int32_t itemIndex = toCylinder->getThingIndex(item);
				if (itemIndex != -1) {
					toCylinder->postAddNotification(item, nullptr, itemIndex);
				}
			}
		} else {
			//fully merged with toItem, item will be destroyed
			item->onRemoved();
			ReleaseItem(item);

			int32_t itemIndex = toCylinder->getThingIndex(toItem);
			if (itemIndex != -1) {
				toCylinder->postAddNotification(toItem, nullptr, itemIndex);
			}
		}
	} else {
		if (identifyForFloorPersistence) {
			// addThing may send the first client packet synchronously. Every add
			// query has succeeded, so identify immediately before insertion.
			item->markAsPlayerMovedForFloorPersistence();
		}
		toCylinder->addThing(index, item);
		if (attributionGuid != 0) {
			attributeSuccessfulItemEndpoint(toCylinder, item, attributionGuid);
		}

		int32_t itemIndex = toCylinder->getThingIndex(item);
		if (itemIndex != -1) {
			toCylinder->postAddNotification(item, nullptr, itemIndex);
		}
	}

	if (item->getDuration() > 0) {
		if (emergencyActive) {
			startDecay(item);
		} else {
			item->incrementReferenceCounter();
			item->setDecaying(DECAYING_TRUE);
			toDecayItems.push_front(item);
		}
	}

	return RETURNVALUE_NOERROR;
}

ReturnValue Game::internalRemoveItem(Item* item, int32_t count /*= -1*/, bool test /*= false*/, uint32_t flags /*= 0*/)
{
	Cylinder* cylinder = item->getParent();
	if (cylinder == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	/*Tile* fromTile = cylinder->getTile();
	if (fromTile) {
		auto it = browseFields.find(fromTile);
		if (it != browseFields.end() && it->second == cylinder) {
			cylinder = fromTile;
		}
	}*/

	if (count == -1) {
		count = item->getItemCount();
	}

	//check if we can remove this item
	ReturnValue ret = cylinder->queryRemove(*item, count, flags | FLAG_IGNORENOTMOVEABLE);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	if (!item->canRemove()) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!test) {
		int32_t index = cylinder->getThingIndex(item);

		//remove the item
		cylinder->removeThing(item, count);

		if (item->isRemoved()) {
			item->onRemoved();
			if (item->canDecay()) {
				decayItems->remove(item);
			}
			ReleaseItem(item);
		}

		cylinder->postRemoveNotification(item, nullptr, index);
	}

	return RETURNVALUE_NOERROR;
}

ReturnValue Game::internalPlayerAddItem(Player* player, Item* item, bool dropOnMap /*= true*/, slots_t slot /*= CONST_SLOT_WHEREEVER*/)
{
	uint32_t remainderCount = 0;
	ReturnValue ret = internalAddItem(
		player, item, static_cast<int32_t>(slot), 0, false, remainderCount, true,
		player->getGUID());
	if (remainderCount != 0) {
		Item* remainderItem = Item::CreateItem(item->getID(), remainderCount);
		uint32_t ignoredRemainderCount = 0;
		ReturnValue remaindRet = internalAddItem(
			player->getTile(), remainderItem, INDEX_WHEREEVER, FLAG_NOLIMIT,
			false, ignoredRemainderCount, false, player->getGUID());
		if (remaindRet != RETURNVALUE_NOERROR) {
			ReleaseItem(remainderItem);
		}
	}

	if (ret != RETURNVALUE_NOERROR && dropOnMap) {
		uint32_t floorRemainderCount = 0;
		ret = internalAddItem(
			player->getTile(), item, INDEX_WHEREEVER, FLAG_NOLIMIT, false,
			floorRemainderCount, true, player->getGUID());
	}

	return ret;
}

Item* Game::findItemOfType(Cylinder* cylinder, uint16_t itemId,
                           bool depthSearch /*= true*/, int32_t subType /*= -1*/) const
{
	if (cylinder == nullptr) {
		return nullptr;
	}

	std::vector<Container*> containers;
	for (size_t i = cylinder->getFirstIndex(), j = cylinder->getLastIndex(); i < j; ++i) {
		Thing* thing = cylinder->getThing(i);
		if (!thing) {
			continue;
		}

		Item* item = thing->getItem();
		if (!item) {
			continue;
		}

		if (item->getID() == itemId && (subType == -1 || subType == item->getSubType())) {
			return item;
		}

		if (depthSearch) {
			Container* container = item->getContainer();
			if (container) {
				containers.push_back(container);
			}
		}
	}

	size_t i = 0;
	while (i < containers.size()) {
		Container* container = containers[i++];
		for (Item* item : container->getItemList()) {
			if (item->getID() == itemId && (subType == -1 || subType == item->getSubType())) {
				return item;
			}

			Container* subContainer = item->getContainer();
			if (subContainer) {
				containers.push_back(subContainer);
			}
		}
	}
	return nullptr;
}

bool Game::removeMoney(Cylinder* cylinder, uint64_t money, uint32_t flags /*= 0*/)
{
	if (cylinder == nullptr) {
		return false;
	}

	if (money == 0) {
		return true;
	}

	std::vector<Container*> containers;

	std::multimap<uint32_t, Item*> moneyMap;
	uint64_t moneyCount = 0;

	for (size_t i = cylinder->getFirstIndex(), j = cylinder->getLastIndex(); i < j; ++i) {
		Thing* thing = cylinder->getThing(i);
		if (!thing) {
			continue;
		}

		Item* item = thing->getItem();
		if (!item) {
			continue;
		}

		Container* container = item->getContainer();
		if (container) {
			containers.push_back(container);
		} else {
			const uint32_t worth = item->getWorth();
			if (worth != 0) {
				moneyCount += worth;
				moneyMap.emplace(worth, item);
			}
		}
	}

	size_t i = 0;
	while (i < containers.size()) {
		Container* container = containers[i++];
		for (Item* item : container->getItemList()) {
			Container* tmpContainer = item->getContainer();
			if (tmpContainer) {
				containers.push_back(tmpContainer);
			} else {
				const uint32_t worth = item->getWorth();
				if (worth != 0) {
					moneyCount += worth;
					moneyMap.emplace(worth, item);
				}
			}
		}
	}

	if (moneyCount < money) {
		return false;
	}

	for (const auto& moneyEntry : moneyMap) {
		Item* item = moneyEntry.second;
		if (moneyEntry.first < money) {
			internalRemoveItem(item);
			money -= moneyEntry.first;
		} else if (moneyEntry.first > money) {
			const uint32_t worth = moneyEntry.first / item->getItemCount();
			const uint32_t removeCount = std::ceil(money / static_cast<double>(worth));

			addMoney(cylinder, (worth * removeCount) - money, flags);
			internalRemoveItem(item, removeCount);
			break;
		} else {
			internalRemoveItem(item);
			break;
		}
	}
	return true;
}

void Game::addMoney(Cylinder* cylinder, uint64_t money, uint32_t flags /*= 0*/)
{
	if (money == 0) {
		return;
	}

	for (const auto& it : Item::items.currencyItems) {
		const uint64_t worth = it.first;

		uint32_t currencyCoins = money / worth;
		if (currencyCoins <= 0) {
			continue;
		}

		money -= currencyCoins * worth;
		while (currencyCoins > 0) {
			const uint16_t count = std::min<uint32_t>(100, currencyCoins);

			Item* remaindItem = Item::CreateItem(it.second, count);

			ReturnValue ret = internalAddItem(cylinder, remaindItem, INDEX_WHEREEVER, flags);
			if (ret != RETURNVALUE_NOERROR) {
				internalAddItem(cylinder->getTile(), remaindItem, INDEX_WHEREEVER, FLAG_NOLIMIT);
			}

			currencyCoins -= count;
		}
	}
}

Item* Game::transformItem(Item* item, uint16_t newId, int32_t newCount /*= -1*/)
{
	if (item->getID() == newId && (newCount == -1 || (newCount == item->getSubType() && newCount != 0))) { //chargeless item placed on map = infinite
		return item;
	}

	Cylinder* cylinder = item->getParent();
	if (cylinder == nullptr) {
		return nullptr;
	}

	/*Tile* fromTile = cylinder->getTile();
	if (fromTile) {
		auto it = browseFields.find(fromTile);
		if (it != browseFields.end() && it->second == cylinder) {
			cylinder = fromTile;
		}
	}*/

	int32_t itemIndex = cylinder->getThingIndex(item);
	if (itemIndex == -1) {
		return item;
	}

	if (!item->canTransform()) {
		return item;
	}

	const ItemType& newType = Item::items[newId];
	if (newType.id == 0) {
		return item;
	}

	const ItemType& curType = Item::items[item->getID()];
	if (curType.alwaysOnTop != newType.alwaysOnTop) {
		//This only occurs when you transform items on tiles from a downItem to a topItem (or vice versa)
		//Remove the old, and add the new
		cylinder->removeThing(item, item->getItemCount());
		cylinder->postRemoveNotification(item, cylinder, itemIndex);

		item->setID(newId);
		if (newCount != -1) {
			item->setSubType(newCount);
		}
		cylinder->addThing(item);

		Cylinder* newParent = item->getParent();
		if (newParent == nullptr) {
			ReleaseItem(item);
			return nullptr;
		}

		newParent->postAddNotification(item, cylinder, newParent->getThingIndex(item));
		return item;
	}

	if (curType.type == newType.type) {
		//Both items has the same type so we can safely change id/subtype
		if (newCount == 0 && (item->isStackable() || item->hasAttribute(ITEM_ATTRIBUTE_CHARGES))) {
			if (item->isStackable()) {
				internalRemoveItem(item);
				return nullptr;
			} else {
				int32_t newItemId = newId;
				if (curType.id == newType.id) {
					newItemId = item->getDecayTo();
				}

				if (newItemId < 0) {
					internalRemoveItem(item);
					return nullptr;
				} else if (newItemId != newId) {
					//Replacing the the old item with the new while maintaining the old position
					Item* newItem = Item::CreateItem(newItemId, 1);
					if (newItem == nullptr) {
						return nullptr;
					}

					cylinder->replaceThing(itemIndex, newItem);
					cylinder->postAddNotification(newItem, cylinder, itemIndex);

					item->setParent(nullptr);
					cylinder->postRemoveNotification(item, cylinder, itemIndex);
					ReleaseItem(item);
					return newItem;
				}
				return transformItem(item, newItemId);
			}
		} else {
			cylinder->postRemoveNotification(item, cylinder, itemIndex);
			uint16_t itemId = item->getID();
			int32_t count = item->getSubType();

			if (curType.id != newType.id) {
				if (newType.group != curType.group) {
					item->setDefaultSubtype();
				}

				itemId = newId;
			}

			if (newCount != -1 && newType.hasSubType()) {
				count = newCount;
			}

			cylinder->updateThing(item, itemId, count);
			cylinder->postAddNotification(item, cylinder, itemIndex);
			return item;
		}
	}

	//Replacing the old item with the new while maintaining the old position
	Item* newItem;
	if (newCount == -1) {
		newItem = Item::CreateItem(newId);
	} else {
		newItem = Item::CreateItem(newId, newCount);
	}

	if (newItem == nullptr) {
		return nullptr;
	}

	cylinder->replaceThing(itemIndex, newItem);
	cylinder->postAddNotification(newItem, cylinder, itemIndex);

	item->setParent(nullptr);
	cylinder->postRemoveNotification(item, cylinder, itemIndex);
	ReleaseItem(item);

	if (newItem->getDuration() > 0) {
		if (newItem->getDecaying() != DECAYING_TRUE) {
			if (emergencyActive) {
				startDecay(newItem);
			} else {
				newItem->incrementReferenceCounter();
				newItem->setDecaying(DECAYING_TRUE);
				toDecayItems.push_front(newItem);
			}
		}
	}

	return newItem;
}

ReturnValue Game::internalTeleport(Thing* thing, const Position& newPos, bool pushMove/* = true*/, uint32_t flags /*= 0*/)
{
	if (newPos == thing->getPosition()) {
		return RETURNVALUE_NOERROR;
	} else if (thing->isRemoved()) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	Tile* toTile = map.getTile(newPos);
	if (!toTile) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (Creature* creature = thing->getCreature()) {
		ReturnValue ret = toTile->queryAdd(0, *creature, 1, FLAG_NOLIMIT);
		if (ret != RETURNVALUE_NOERROR) {
			return ret;
		}

		map.moveCreature(*creature, *toTile, !pushMove);
		return RETURNVALUE_NOERROR;
	} else if (Item* item = thing->getItem()) {
		return internalMoveItem(item->getParent(), toTile, INDEX_WHEREEVER, item, item->getItemCount(), nullptr, flags);
	}
	return RETURNVALUE_NOTPOSSIBLE;
}

Item* searchForItem(Container* container, uint16_t itemId)
{
	for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
		if ((*it)->getID() == itemId) {
			return *it;
		}
	}

	return nullptr;
}

slots_t getSlotType(const ItemType& it)
{
	slots_t slot = CONST_SLOT_RIGHT;
	if (it.weaponType != WeaponType_t::WEAPON_SHIELD) {
		int32_t slotPosition = it.slotPosition;

		if (slotPosition & SLOTP_HEAD) {
			slot = CONST_SLOT_HEAD;
		} else if (slotPosition & SLOTP_NECKLACE) {
			slot = CONST_SLOT_NECKLACE;
		} else if (slotPosition & SLOTP_ARMOR) {
			slot = CONST_SLOT_ARMOR;
		} else if (slotPosition & SLOTP_LEGS) {
			slot = CONST_SLOT_LEGS;
		} else if (slotPosition & SLOTP_FEET) {
			slot = CONST_SLOT_FEET;
		} else if (slotPosition & SLOTP_RING) {
			slot = CONST_SLOT_RING;
		} else if (slotPosition & SLOTP_AMMO) {
			slot = CONST_SLOT_AMMO;
		} else if (slotPosition & SLOTP_TWO_HAND || slotPosition & SLOTP_LEFT) {
			slot = CONST_SLOT_LEFT;
		}
	}

	return slot;
}

//Implementation of player invoked events
void Game::playerEquipItem(uint32_t playerId, uint16_t spriteId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Item* item = player->getInventoryItem(CONST_SLOT_BACKPACK);
	if (!item) {
		return;
	}

	Container* backpack = item->getContainer();
	if (!backpack) {
		return;
	}

	const ItemType& it = Item::items.getItemIdByClientId(spriteId);
	slots_t slot = getSlotType(it);

	Item* slotItem = player->getInventoryItem(slot);
	Item* equipItem = searchForItem(backpack, it.id);
	if (slotItem && slotItem->getID() == it.id && (!it.stackable || slotItem->getItemCount() == 100 || !equipItem)) {
		internalMoveItem(slotItem->getParent(), player, CONST_SLOT_WHEREEVER, slotItem, slotItem->getItemCount(), nullptr);
	} else if (equipItem) {
		internalMoveItem(equipItem->getParent(), player, slot, equipItem, equipItem->getItemCount(), nullptr);
	}
}

void Game::playerMove(uint32_t playerId, Direction direction)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (g_playerShop.shouldBlockMovement(player)) {
		player->sendCancelWalk();
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (player->isMovementBlocked()) {
		player->sendCancelWalk();
		return;
	}

	player->resetIdleTime();
	player->setNextWalkActionTask(nullptr);

	player->startAutoWalk(direction);
}

bool Game::playerBroadcastMessage(Player* player, const std::string& text) const
{
	if (!player->hasFlag(PlayerFlag_CanBroadcast)) {
		return false;
	}

	std::cout << "> " << player->getName() << " broadcasted: \"" << text << "\"." << std::endl;

	for (const auto& it : players) {
		it.second->sendPrivateMessage(player, TALKTYPE_BROADCAST, text);
	}

	return true;
}

void Game::playerCreatePrivateChannel(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player || !player->isPremium()) {
		return;
	}

	ChatChannel* channel = g_chat->createChannel(*player, CHANNEL_PRIVATE);
	if (!channel || !channel->addUser(*player)) {
		return;
	}

	player->sendCreatePrivateChannel(channel->getId(), channel->getName());
}

void Game::playerChannelInvite(uint32_t playerId, const std::string& name)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	PrivateChatChannel* channel = g_chat->getPrivateChannel(*player);
	if (!channel) {
		return;
	}

	Player* invitePlayer = getPlayerByName(name);
	if (!invitePlayer) {
		return;
	}

	if (player == invitePlayer) {
		return;
	}

	channel->invitePlayer(*player, *invitePlayer);
}

void Game::playerChannelExclude(uint32_t playerId, const std::string& name)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	PrivateChatChannel* channel = g_chat->getPrivateChannel(*player);
	if (!channel) {
		return;
	}

	Player* excludePlayer = getPlayerByName(name);
	if (!excludePlayer) {
		return;
	}

	if (player == excludePlayer) {
		return;
	}

	channel->excludePlayer(*player, *excludePlayer);
}

void Game::playerRequestChannels(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->sendChannelsDialog();
}

void Game::playerOpenChannel(uint32_t playerId, uint16_t channelId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	ChatChannel* channel = g_chat->addUserToChannel(*player, channelId);
	if (!channel) {
		return;
	}

	const InvitedMap* invitedUsers = channel->getInvitedUsers();
	const UsersMap* users;
	if (!channel->isPublicChannel()) {
		users = &channel->getUsers();
	} else {
		users = nullptr;
	}

	player->sendChannel(channel->getId(), channel->getName(), users, invitedUsers);
}

void Game::playerCloseChannel(uint32_t playerId, uint16_t channelId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	g_chat->removeUserFromChannel(*player, channelId);
}

void Game::playerOpenPrivateChannel(uint32_t playerId, std::string& receiver)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!IOLoginData::formatPlayerName(receiver)) {
		player->sendCancelMessage("A player with this name does not exist.");
		return;
	}

	if (player->getName() == receiver) {
		player->sendCancelMessage("You cannot set up a private message channel with yourself.");
		return;
	}

	player->sendOpenPrivateChannel(receiver);
}

void Game::playerCloseNpcChannel(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	SpectatorVec spectators;
	map.getSpectators(spectators, player->getPosition());
	for (Creature* spectator : spectators) {
		if (Npc* npc = spectator->getNpc()) {
			npc->onPlayerCloseChannel(player);
		}
	}
}

void Game::playerReceivePing(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->receivePing();
}

void Game::playerReceivePingBack(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->sendPingBack();
}

void Game::playerAutoWalk(uint32_t playerId, const std::vector<Direction>& listDir)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (g_playerShop.shouldBlockMovement(player)) {
		player->sendCancelWalk();
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	player->resetIdleTime();
	player->setNextWalkTask(nullptr);
	player->startAutoWalk(listDir);
}

void Game::playerStopAutoWalk(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->stopWalk();
}

void Game::playerUseItemEx(uint32_t playerId, const Position& fromPos, uint8_t fromStackPos, uint16_t fromSpriteId,
                           const Position& toPos, uint8_t toStackPos, uint16_t toSpriteId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	bool isHotkey = (fromPos.x == 0xFFFF && fromPos.y == 0 && fromPos.z == 0);
	if (isHotkey && !g_config.getBoolean(ConfigManager::AIMBOT_HOTKEY_ENABLED)) {
		return;
	}

	Thing* thing = internalGetThing(player, fromPos, fromStackPos, fromSpriteId, STACKPOS_USEITEM);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Item* item = thing->getItem();
	if (!item || !item->isUseable() || item->getClientID() != fromSpriteId) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	Position walkToPos = fromPos;
	ReturnValue ret = g_actions->canUse(player, fromPos);
	if (ret == RETURNVALUE_NOERROR) {
		ret = g_actions->canUse(player, toPos, item);
		if (ret == RETURNVALUE_TOOFARAWAY) {
			walkToPos = toPos;
		}
	}

	if (ret != RETURNVALUE_NOERROR) {
		if (ret == RETURNVALUE_TOOFARAWAY) {
			Position itemPos = fromPos;
			uint8_t itemStackPos = fromStackPos;

			if (fromPos.x != 0xFFFF && toPos.x != 0xFFFF && Position::areInRange<1, 1, 0>(fromPos, player->getPosition()) &&
			        !Position::areInRange<1, 1, 0>(fromPos, toPos)) {
				Item* moveItem = nullptr;

				ret = internalMoveItem(item->getParent(), player, INDEX_WHEREEVER, item, item->getItemCount(), &moveItem, 0, player, nullptr, &fromPos, &toPos);
				if (ret != RETURNVALUE_NOERROR) {
					player->sendCancelMessage(ret);
					return;
				}

				//changing the position since its now in the inventory of the player
				internalGetPosition(moveItem, itemPos, itemStackPos);
			}

			std::vector<Direction> listDir;
			if (player->getPathTo(walkToPos, listDir, 0, 1, true, true)) {
				g_dispatcher.addTask(createTask(std::bind(&Game::playerAutoWalk, this, player->getID(), std::move(listDir))));

				SchedulerTask* task = createSchedulerTask(RANGE_USE_ITEM_EX_INTERVAL, std::bind(&Game::playerUseItemEx, this,
				                      playerId, itemPos, itemStackPos, fromSpriteId, toPos, toStackPos, toSpriteId));
				player->setNextWalkActionTask(task);
			} else {
				player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
			}
			return;
		}

		player->sendCancelMessage(ret);
		return;
	}

	if (usesRuneActionExhaust(item) && !player->canDoRuneAction()) {
		uint32_t delay = player->getNextRuneActionTime();
		if (canQueueShortRuneRetry(item, delay)) {
			SchedulerTask* task = createSchedulerTask(delay, std::bind(&Game::playerUseItemEx, this,
			                      playerId, fromPos, fromStackPos, fromSpriteId, toPos, toStackPos, toSpriteId));
			player->setNextRuneActionTask(task);
			return;
		}

		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return;
	}

	player->resetIdleTime();
	if (usesRuneActionExhaust(item)) {
		player->setNextRuneActionTask(nullptr);
	}

	g_actions->useItemEx(player, fromPos, toPos, toStackPos, item, isHotkey);
}

void Game::playerUseItem(uint32_t playerId, const Position& pos, uint8_t stackPos,
                         uint8_t index, uint16_t spriteId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	bool isHotkey = (pos.x == 0xFFFF && pos.y == 0 && pos.z == 0);
	if (isHotkey && !g_config.getBoolean(ConfigManager::AIMBOT_HOTKEY_ENABLED)) {
		return;
	}

	Thing* thing = internalGetThing(player, pos, stackPos, spriteId, STACKPOS_USEITEM);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Item* item = thing->getItem();
	if (!item || item->isUseable() || item->getClientID() != spriteId) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	ReturnValue ret = g_actions->canUse(player, pos);
	if (ret != RETURNVALUE_NOERROR) {
		if (ret == RETURNVALUE_TOOFARAWAY) {
			std::vector<Direction> listDir;
			if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
				g_dispatcher.addTask(createTask(std::bind(&Game::playerAutoWalk,
				                                this, player->getID(), std::move(listDir))));

				SchedulerTask* task = createSchedulerTask(RANGE_USE_ITEM_INTERVAL, std::bind(&Game::playerUseItem, this,
				                      playerId, pos, stackPos, index, spriteId));
				player->setNextWalkActionTask(task);
				return;
			}

			ret = RETURNVALUE_THEREISNOWAY;
		}

		player->sendCancelMessage(ret);
		return;
	}

	player->resetIdleTime();

	g_actions->useItem(player, pos, index, item, isHotkey);
}

void Game::playerUseWithCreature(uint32_t playerId, const Position& fromPos, uint8_t fromStackPos, uint32_t creatureId, uint16_t spriteId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Creature* creature = getCreatureByID(creatureId);
	if (!creature) {
		return;
	}

	if (!Position::areInRange<Map::maxClientViewportX - 1, Map::maxClientViewportY - 1, 0>(creature->getPosition(), player->getPosition())) {
		return;
	}

	bool isHotkey = (fromPos.x == 0xFFFF && fromPos.y == 0 && fromPos.z == 0);
	if (!g_config.getBoolean(ConfigManager::AIMBOT_HOTKEY_ENABLED)) {
		if (creature->getPlayer() || isHotkey) {
			player->sendCancelMessage(RETURNVALUE_DIRECTPLAYERSHOOT);
			return;
		}
	}

	Thing* thing = internalGetThing(player, fromPos, fromStackPos, spriteId, STACKPOS_USEITEM);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Item* item = thing->getItem();
	if (!item || !item->isUseable() || item->getClientID() != spriteId) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	Position toPos = creature->getPosition();
	Position walkToPos = fromPos;
	ReturnValue ret = g_actions->canUse(player, fromPos);
	if (ret == RETURNVALUE_NOERROR) {
		ret = g_actions->canUse(player, toPos, item);
		if (ret == RETURNVALUE_TOOFARAWAY) {
			walkToPos = toPos;
		}
	}

	if (ret != RETURNVALUE_NOERROR) {
		if (ret == RETURNVALUE_TOOFARAWAY) {
			Position itemPos = fromPos;
			uint8_t itemStackPos = fromStackPos;

			if (fromPos.x != 0xFFFF && Position::areInRange<1, 1, 0>(fromPos, player->getPosition()) && !Position::areInRange<1, 1, 0>(fromPos, toPos)) {
				Item* moveItem = nullptr;
				ret = internalMoveItem(item->getParent(), player, INDEX_WHEREEVER, item, item->getItemCount(), &moveItem, 0, player, nullptr, &fromPos, &toPos);
				if (ret != RETURNVALUE_NOERROR) {
					player->sendCancelMessage(ret);
					return;
				}

				//changing the position since its now in the inventory of the player
				internalGetPosition(moveItem, itemPos, itemStackPos);
			}

			std::vector<Direction> listDir;
			if (player->getPathTo(walkToPos, listDir, 0, 1, true, true)) {
				g_dispatcher.addTask(createTask(std::bind(&Game::playerAutoWalk,
				                                this, player->getID(), std::move(listDir))));

				SchedulerTask* task = createSchedulerTask(RANGE_USE_WITH_CREATURE_INTERVAL, std::bind(&Game::playerUseWithCreature, this,
				                      playerId, itemPos, itemStackPos, creatureId, spriteId));
				player->setNextWalkActionTask(task);
			} else {
				player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
			}
			return;
		}

		player->sendCancelMessage(ret);
		return;
	}

	if (usesRuneActionExhaust(item) && !player->canDoRuneAction()) {
		uint32_t delay = player->getNextRuneActionTime();
		if (canQueueShortRuneRetry(item, delay)) {
			SchedulerTask* task = createSchedulerTask(delay, std::bind(&Game::playerUseWithCreature, this,
			                      playerId, fromPos, fromStackPos, creatureId, spriteId));
			player->setNextRuneActionTask(task);
			return;
		}

		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return;
	}

	player->resetIdleTime();
	if (usesRuneActionExhaust(item)) {
		player->setNextRuneActionTask(nullptr);
	}

	g_actions->useItemEx(player, fromPos, creature->getPosition(), creature->getParent()->getThingIndex(creature), item, isHotkey, creature);
}

void Game::playerCloseContainer(uint32_t playerId, uint8_t cid)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	player->closeContainer(cid);
	player->sendCloseContainer(cid);
}

void Game::playerMoveUpContainer(uint32_t playerId, uint8_t cid)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Container* container = player->getContainerByID(cid);
	if (!container) {
		return;
	}

	Container* parentContainer = dynamic_cast<Container*>(container->getRealParent());
	/*if (!parentContainer) {
		Tile* tile = container->getTile();
		if (!tile) {
			return;
		}

		if (!g_events->eventPlayerOnBrowseField(player, tile->getPosition())) {
			return;
		}

		auto it = browseFields.find(tile);
		if (it == browseFields.end()) {
			parentContainer = new Container(tile);
			parentContainer->incrementReferenceCounter();
			browseFields[tile] = parentContainer;
			g_scheduler.addEvent(createSchedulerTask(30000, std::bind(&Game::decreaseBrowseFieldRef, this, tile->getPosition())));
		} else {
			parentContainer = it->second;
		}
	}*/

	if (!parentContainer) {
		return;
	}

	player->addContainer(cid, parentContainer);
	player->sendContainer(cid, parentContainer, parentContainer->hasParent(), player->getContainerIndex(cid));
}

void Game::playerUpdateContainer(uint32_t playerId, uint8_t cid)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Container* container = player->getContainerByID(cid);
	if (!container) {
		return;
	}

	player->sendContainer(cid, container, container->hasParent(), player->getContainerIndex(cid));
}

void Game::playerRotateItem(uint32_t playerId, const Position& pos, uint8_t stackPos, const uint16_t spriteId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Thing* thing = internalGetThing(player, pos, stackPos, 0, STACKPOS_TOPDOWN_ITEM);
	if (!thing) {
		return;
	}

	Item* item = thing->getItem();
	if (!item || item->getClientID() != spriteId || !item->isRotatable() || item->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (pos.x != 0xFFFF && !Position::areInRange<1, 1, 0>(pos, player->getPosition())) {
		std::vector<Direction> listDir;
		if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
			g_dispatcher.addTask(createTask(std::bind(&Game::playerAutoWalk,
			                                this, player->getID(), std::move(listDir))));

			SchedulerTask* task = createSchedulerTask(RANGE_ROTATE_ITEM_INTERVAL, std::bind(&Game::playerRotateItem, this,
			                      playerId, pos, stackPos, spriteId));
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	uint16_t newId = Item::items[item->getID()].rotateTo;
	if (newId != 0) {
		transformItem(item, newId);
	}
}

void Game::playerWriteItem(uint32_t playerId, uint32_t windowTextId, const std::string& text)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	uint16_t maxTextLength = 0;
	uint32_t internalWindowTextId = 0;

	Item* writeItem = player->getWriteItem(internalWindowTextId, maxTextLength);
	if (text.length() > maxTextLength || windowTextId != internalWindowTextId) {
		return;
	}

	if (!writeItem || writeItem->isRemoved()) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Cylinder* topParent = writeItem->getTopParent();

	Player* owner = dynamic_cast<Player*>(topParent);
	if (owner && owner != player) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (!Position::areInRange<1, 1, 0>(writeItem->getPosition(), player->getPosition())) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	for (auto creatureEvent : player->getCreatureEvents(CREATURE_EVENT_TEXTEDIT)) {
		if (!creatureEvent->executeTextEdit(player, writeItem, text)) {
			player->setWriteItem(nullptr);
			return;
		}
	}

	if (!text.empty()) {
		if (writeItem->getText() != text) {
			writeItem->setText(text);
			writeItem->setWriter(player->getName());
			writeItem->setDate(time(nullptr));
		}
	} else {
		writeItem->resetText();
		writeItem->resetWriter();
		writeItem->resetDate();
	}

	uint16_t newId = Item::items[writeItem->getID()].writeOnceItemId;
	if (newId != 0) {
		transformItem(writeItem, newId);
	} else {
		writeItem->markFloorPersistenceAttributeDirty(true);
	}

	player->setWriteItem(nullptr);
}

void Game::playerBrowseField(uint32_t playerId, const Position& pos)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	const Position& playerPos = player->getPosition();
	if (playerPos.z != pos.z) {
		player->sendCancelMessage(playerPos.z > pos.z ? RETURNVALUE_FIRSTGOUPSTAIRS : RETURNVALUE_FIRSTGODOWNSTAIRS);
		return;
	}

	if (!Position::areInRange<1, 1>(playerPos, pos)) {
		std::vector<Direction> listDir;
		if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
			g_dispatcher.addTask(createTask(std::bind(&Game::playerAutoWalk,
			                                this, player->getID(), std::move(listDir))));
			SchedulerTask* task = createSchedulerTask(RANGE_BROWSE_FIELD_INTERVAL, std::bind(
			                          &Game::playerBrowseField, this, playerId, pos
			                      ));
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	Tile* tile = map.getTile(pos);
	if (!tile) {
		return;
	}

	if (!g_events->eventPlayerOnBrowseField(player, pos)) {
		return;
	}

	Container* container;

	auto it = browseFields.find(tile);
	if (it == browseFields.end()) {
		container = new Container(tile);
		container->incrementReferenceCounter();
		browseFields[tile] = container;
		g_scheduler.addEvent(createSchedulerTask(30000, std::bind(&Game::decreaseBrowseFieldRef, this, tile->getPosition())));
	} else {
		container = it->second;
	}

	uint8_t dummyContainerId = 0xF - ((pos.x % 3) * 3 + (pos.y % 3));
	Container* openContainer = player->getContainerByID(dummyContainerId);
	if (openContainer) {
		player->onCloseContainer(openContainer);
		player->closeContainer(dummyContainerId);
	} else {
		player->addContainer(dummyContainerId, container);
		player->sendContainer(dummyContainerId, container, false, 0);
	}
}

void Game::playerSeekInContainer(uint32_t playerId, uint8_t containerId, uint16_t index)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Container* container = player->getContainerByID(containerId);
	if (!container || !container->hasPagination()) {
		return;
	}

	if ((index % container->capacity()) != 0 || index >= container->size()) {
		return;
	}

	player->setContainerIndex(containerId, index);
	player->sendContainer(containerId, container, container->hasParent(), index);
}

void Game::playerUpdateHouseWindow(uint32_t playerId, uint8_t listId, uint32_t windowTextId, const std::string& text)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	uint32_t internalWindowTextId;
	uint32_t internalListId;

	House* house = player->getEditHouse(internalWindowTextId, internalListId);
	if (house && house->canEditAccessList(internalListId, player) && internalWindowTextId == windowTextId && listId == 0) {
		house->setAccessList(internalListId, text);
	}

	player->setEditHouse(nullptr);
}

void Game::playerWrapItem(uint32_t playerId, const Position& position, uint8_t stackPos, const uint16_t spriteId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Thing* thing = internalGetThing(player, position, stackPos, 0, STACKPOS_TOPDOWN_ITEM);
	if (!thing) {
		return;
	}

	Item* item = thing->getItem();
	if (!item || item->getClientID() != spriteId || !item->hasAttribute(ITEM_ATTRIBUTE_WRAPID) || item->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (position.x != 0xFFFF && !Position::areInRange<1, 1, 0>(position, player->getPosition())) {
		std::vector<Direction> listDir;
		if (player->getPathTo(position, listDir, 0, 1, true, true)) {
			g_dispatcher.addTask(createTask(std::bind(&Game::playerAutoWalk,
				this, player->getID(), std::move(listDir))));

			SchedulerTask* task = createSchedulerTask(RANGE_WRAP_ITEM_INTERVAL, std::bind(&Game::playerWrapItem, this,
				playerId, position, stackPos, spriteId));
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	g_events->eventPlayerOnWrapItem(player, item);
}

void Game::playerRequestTrade(uint32_t playerId, const Position& pos, uint8_t stackPos,
                              uint32_t tradePlayerId, uint16_t spriteId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Player* tradePartner = getPlayerByID(tradePlayerId);
	if (!tradePartner || tradePartner == player) {
		player->sendCancelMessage("Select a player to trade with.");
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(tradePartner)) {
		player->sendCancelMessage("This player is busy.");
		return;
	}

	if (!Position::areInRange<2, 2, 0>(tradePartner->getPosition(), player->getPosition())) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		return;
	}

	if (!canThrowObjectTo(tradePartner->getPosition(), player->getPosition(), true, true)) {
		player->sendCancelMessage(RETURNVALUE_CANNOTTHROW);
		return;
	}

	Thing* tradeThing = internalGetThing(player, pos, stackPos, 0, STACKPOS_TOPDOWN_ITEM);
	if (!tradeThing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Item* tradeItem = tradeThing->getItem();
	if (tradeItem->getClientID() != spriteId || !tradeItem->isPickupable() || tradeItem->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (g_config.getBoolean(ConfigManager::ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS)) {
		if (HouseTile* houseTile = dynamic_cast<HouseTile*>(tradeItem->getTile())) {
			House* house = houseTile->getHouse();
			if (house && !house->isInvited(player)) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return;
			}
		}
	}

	const Position& playerPosition = player->getPosition();
	const Position& tradeItemPosition = tradeItem->getPosition();
	if (playerPosition.z != tradeItemPosition.z) {
		player->sendCancelMessage(playerPosition.z > tradeItemPosition.z ? RETURNVALUE_FIRSTGOUPSTAIRS : RETURNVALUE_FIRSTGODOWNSTAIRS);
		return;
	}

	if (!Position::areInRange<1, 1>(tradeItemPosition, playerPosition)) {
		std::vector<Direction> listDir;
		if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
			g_dispatcher.addTask(createTask(std::bind(&Game::playerAutoWalk,
			                                this, player->getID(), std::move(listDir))));

			SchedulerTask* task = createSchedulerTask(RANGE_REQUEST_TRADE_INTERVAL, std::bind(&Game::playerRequestTrade, this,
			                      playerId, pos, stackPos, tradePlayerId, spriteId));
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	Container* tradeItemContainer = tradeItem->getContainer();
	if (tradeItemContainer) {
		for (const auto& it : tradeItems) {
			Item* item = it.first;
			if (tradeItem == item) {
				player->sendCancelMessage("This item is already being traded.");
				return;
			}

			if (tradeItemContainer->isHoldingItem(item)) {
				player->sendCancelMessage("This item is already being traded.");
				return;
			}

			Container* container = item->getContainer();
			if (container && container->isHoldingItem(tradeItem)) {
				player->sendCancelMessage("This item is already being traded.");
				return;
			}
		}
	} else {
		for (const auto& it : tradeItems) {
			Item* item = it.first;
			if (tradeItem == item) {
				player->sendCancelMessage("This item is already being traded.");
				return;
			}

			Container* container = item->getContainer();
			if (container && container->isHoldingItem(tradeItem)) {
				player->sendCancelMessage("This item is already being traded.");
				return;
			}
		}
	}

	Container* tradeContainer = tradeItem->getContainer();
	if (tradeContainer && tradeContainer->getItemHoldingCount() + 1 > 100) {
		player->sendCancelMessage("You can only trade up to 100 objects at once.");
		return;
	}

	if (!g_events->eventPlayerOnTradeRequest(player, tradePartner, tradeItem)) {
		return;
	}

	internalStartTrade(player, tradePartner, tradeItem);
}

bool Game::internalStartTrade(Player* player, Player* tradePartner, Item* tradeItem)
{
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return false;
	}
	if (g_playerShop.shouldBlockInventoryAction(tradePartner)) {
		player->sendCancelMessage("This player is busy.");
		return false;
	}

	if (player->tradeState != TRADE_NONE && !(player->tradeState == TRADE_ACKNOWLEDGE && player->tradePartner == tradePartner)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREALREADYTRADING);
		return false;
	} else if (tradePartner->tradeState != TRADE_NONE && tradePartner->tradePartner != player) {
		player->sendCancelMessage(RETURNVALUE_THISPLAYERISALREADYTRADING);
		return false;
	}

	player->tradePartner = tradePartner;
	player->tradeItem = tradeItem;
	player->tradeState = TRADE_INITIATED;
	tradeItem->incrementReferenceCounter();
	tradeItems[tradeItem] = player->getID();

	player->sendTradeItemRequest(player->getName(), tradeItem, true);

	if (tradePartner->tradeState == TRADE_NONE) {
		tradePartner->sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("{:s} wants to trade with you.", player->getName()));
		tradePartner->tradeState = TRADE_ACKNOWLEDGE;
		tradePartner->tradePartner = player;
	} else {
		Item* counterOfferItem = tradePartner->tradeItem;
		player->sendTradeItemRequest(tradePartner->getName(), counterOfferItem, false);
		tradePartner->sendTradeItemRequest(player->getName(), tradeItem, false);
	}

	return true;
}

void Game::playerAcceptTrade(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (!(player->getTradeState() == TRADE_ACKNOWLEDGE || player->getTradeState() == TRADE_INITIATED)) {
		return;
	}

	Player* tradePartner = player->tradePartner;
	if (!tradePartner) {
		return;
	}

	player->setTradeState(TRADE_ACCEPT);

	if (tradePartner->getTradeState() == TRADE_ACCEPT) {
		if (!canThrowObjectTo(tradePartner->getPosition(), player->getPosition(), true, true)) {
			internalCloseTrade(player, false);
			player->sendCancelMessage(RETURNVALUE_CANNOTTHROW);
			tradePartner->sendCancelMessage(RETURNVALUE_CANNOTTHROW);
			return;
		}

		Item* playerTradeItem = player->tradeItem;
		Item* partnerTradeItem = tradePartner->tradeItem;
		Cylinder* playerTradeSource = playerTradeItem ?
			playerTradeItem->getParent() : nullptr;
		Cylinder* partnerTradeSource = partnerTradeItem ?
			partnerTradeItem->getParent() : nullptr;

		if (!g_events->eventPlayerOnTradeAccept(player, tradePartner, playerTradeItem, partnerTradeItem)) {
			internalCloseTrade(player, false);
			return;
		}

		player->setTradeState(TRADE_TRANSFER);
		tradePartner->setTradeState(TRADE_TRANSFER);

		auto it = tradeItems.find(playerTradeItem);
		if (it != tradeItems.end()) {
			ReleaseItem(it->first);
			tradeItems.erase(it);
		}

		it = tradeItems.find(partnerTradeItem);
		if (it != tradeItems.end()) {
			ReleaseItem(it->first);
			tradeItems.erase(it);
		}

		bool isSuccess = false;
		uint64_t tradeCheckpointGroupId = 0;

		ReturnValue tradePartnerRet = RETURNVALUE_NOERROR;
		ReturnValue playerRet = RETURNVALUE_NOERROR;

		// if player is trying to trade its own backpack
		if (tradePartner->getInventoryItem(CONST_SLOT_BACKPACK) == partnerTradeItem) {
			tradePartnerRet = (tradePartner->getInventoryItem(getSlotType(Item::items[playerTradeItem->getID()])) ? RETURNVALUE_NOTENOUGHROOM : RETURNVALUE_NOERROR);
		}

		if (player->getInventoryItem(CONST_SLOT_BACKPACK) == playerTradeItem) {
			playerRet = (player->getInventoryItem(getSlotType(Item::items[partnerTradeItem->getID()])) ? RETURNVALUE_NOTENOUGHROOM : RETURNVALUE_NOERROR);
		}

		// both players try to trade equipped backpacks
		if (player->getInventoryItem(CONST_SLOT_BACKPACK) == playerTradeItem && tradePartner->getInventoryItem(CONST_SLOT_BACKPACK) == partnerTradeItem) {
			playerRet = RETURNVALUE_NOTENOUGHROOM;
		}
		
		if (tradePartnerRet == RETURNVALUE_NOERROR && playerRet == RETURNVALUE_NOERROR) {
			tradePartnerRet = internalAddItem(tradePartner, playerTradeItem, INDEX_WHEREEVER, 0, true);
			playerRet = internalAddItem(player, partnerTradeItem, INDEX_WHEREEVER, 0, true);
			if (tradePartnerRet == RETURNVALUE_NOERROR && playerRet == RETURNVALUE_NOERROR) {
				playerRet = internalRemoveItem(playerTradeItem, playerTradeItem->getItemCount(), true);
				tradePartnerRet = internalRemoveItem(partnerTradeItem, partnerTradeItem->getItemCount(), true);
				if (tradePartnerRet == RETURNVALUE_NOERROR && playerRet == RETURNVALUE_NOERROR) {
					Item* tradePartnerReceivedItem = nullptr;
					Item* playerReceivedItem = nullptr;
					tradePartnerRet = internalMoveItem(playerTradeItem->getParent(), tradePartner, INDEX_WHEREEVER, playerTradeItem, playerTradeItem->getItemCount(), &tradePartnerReceivedItem, FLAG_IGNOREAUTOSTACK, nullptr, partnerTradeItem);
					if (tradePartnerRet == RETURNVALUE_NOERROR) {
						playerRet = internalMoveItem(partnerTradeItem->getParent(), player, INDEX_WHEREEVER, partnerTradeItem, partnerTradeItem->getItemCount(), &playerReceivedItem, FLAG_IGNOREAUTOSTACK);
						if (playerRet == RETURNVALUE_NOERROR) {
							// Attribution is deliberately deferred until both sides
							// of the atomic exchange have reached their recipients.
							attributeDeliveredItem(
								tradePartnerReceivedItem, tradePartner->getGUID());
							attributeDeliveredItem(
								playerReceivedItem, player->getGUID());
							attributeContainerPathAfterMutation(
								playerTradeSource, player->getGUID());
							attributeContainerPathAfterMutation(
								partnerTradeSource, tradePartner->getGUID());
							// Register both participants before trade callbacks run. This
							// merges every floor/house/item checkpoint already associated
							// with either side and prevents an independent asynchronous
							// logout save until the joint transaction commits.
							tradeCheckpointGroupId = registerTradeCheckpoint(
								player, tradePartner, playerTradeSource, partnerTradeSource,
								playerTradeItem, partnerTradeItem);
							playerTradeItem->onTradeEvent(ON_TRADE_TRANSFER, tradePartner);
							partnerTradeItem->onTradeEvent(ON_TRADE_TRANSFER, player);
							isSuccess = true;
						}
					}
				}
			}
		}

		if (!isSuccess) {
			std::string errorDescription;

			if (tradePartner->tradeItem) {
				errorDescription = getTradeErrorDescription(tradePartnerRet, playerTradeItem);
				tradePartner->sendTextMessage(MESSAGE_EVENT_ADVANCE, errorDescription);
				tradePartner->tradeItem->onTradeEvent(ON_TRADE_CANCEL, tradePartner);
			}

			if (player->tradeItem) {
				errorDescription = getTradeErrorDescription(playerRet, partnerTradeItem);
				player->sendTextMessage(MESSAGE_EVENT_ADVANCE, errorDescription);
				player->tradeItem->onTradeEvent(ON_TRADE_CANCEL, player);
			}
		}

		if (isSuccess && tradeCheckpointGroupId != 0 &&
		    !executeFloorCheckpointGroup(tradeCheckpointGroupId)) {
			auto failedGroupIt = floorCheckpointGroups.find(tradeCheckpointGroupId);
			std::cout << "[Error - Game::playerAcceptTrade] Atomic trade checkpoint "
			          << tradeCheckpointGroupId << " remains pending";
			if (failedGroupIt != floorCheckpointGroups.end() &&
			    !failedGroupIt->second.lastError.empty()) {
				std::cout << ": " << failedGroupIt->second.lastError;
			}
			std::cout << std::endl;
		}

		g_events->eventPlayerOnTradeCompleted(player, tradePartner, playerTradeItem, partnerTradeItem, isSuccess);

		player->setTradeState(TRADE_NONE);
		player->tradeItem = nullptr;
		player->tradePartner = nullptr;
		player->sendTradeClose();

		tradePartner->setTradeState(TRADE_NONE);
		tradePartner->tradeItem = nullptr;
		tradePartner->tradePartner = nullptr;
		tradePartner->sendTradeClose();
	}
}

std::string Game::getTradeErrorDescription(ReturnValue ret, Item* item)
{
	if (item) {
		if (ret == RETURNVALUE_NOTENOUGHCAPACITY) {
			return fmt::format("You do not have enough capacity to carry {:s}.\n {:s}", item->isStackable() && item->getItemCount() > 1 ? "these objects" : "this object", item->getWeightDescription());
		} else if (ret == RETURNVALUE_NOTENOUGHROOM || ret == RETURNVALUE_CONTAINERNOTENOUGHROOM) {
			return fmt::format("You do not have enough room to carry {:s}.", item->isStackable() && item->getItemCount() > 1 ? "these objects" : "this object");
		}
	}
	return "Trade could not be completed.";
}

void Game::playerLookInTrade(uint32_t playerId, bool lookAtCounterOffer, uint8_t index)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Player* tradePartner = player->tradePartner;
	if (!tradePartner) {
		return;
	}

	Item* tradeItem;
	if (lookAtCounterOffer) {
		tradeItem = tradePartner->getTradeItem();
	} else {
		tradeItem = player->getTradeItem();
	}

	if (!tradeItem) {
		return;
	}

	const Position& playerPosition = player->getPosition();
	const Position& tradeItemPosition = tradeItem->getPosition();

	int32_t lookDistance = std::max<int32_t>(Position::getDistanceX(playerPosition, tradeItemPosition),
	                                         Position::getDistanceY(playerPosition, tradeItemPosition));
	if (index == 0) {
		g_events->eventPlayerOnLookInTrade(player, tradePartner, tradeItem, lookDistance);
		return;
	}

	Container* tradeContainer = tradeItem->getContainer();
	if (!tradeContainer) {
		return;
	}

	std::vector<const Container*> containers {tradeContainer};
	size_t i = 0;
	while (i < containers.size()) {
		const Container* container = containers[i++];
		for (Item* item : container->getItemList()) {
			Container* tmpContainer = item->getContainer();
			if (tmpContainer) {
				containers.push_back(tmpContainer);
			}

			if (--index == 0) {
				g_events->eventPlayerOnLookInTrade(player, tradePartner, item, lookDistance);
				return;
			}
		}
	}
}

void Game::playerCloseTrade(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	internalCloseTrade(player);
}

void Game::internalCloseTrade(Player* player, bool sendCancel/* = true*/)
{
	Player* tradePartner = player->tradePartner;
	if ((tradePartner && tradePartner->getTradeState() == TRADE_TRANSFER) || player->getTradeState() == TRADE_TRANSFER) {
		return;
	}

	if (player->getTradeItem()) {
		auto it = tradeItems.find(player->getTradeItem());
		if (it != tradeItems.end()) {
			ReleaseItem(it->first);
			tradeItems.erase(it);
		}

		player->tradeItem->onTradeEvent(ON_TRADE_CANCEL, player);
		player->tradeItem = nullptr;
	}

	player->setTradeState(TRADE_NONE);
	player->tradePartner = nullptr;

	if (sendCancel) {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, "Trade cancelled.");
	}
	player->sendTradeClose();

	if (tradePartner) {
		if (tradePartner->getTradeItem()) {
			auto it = tradeItems.find(tradePartner->getTradeItem());
			if (it != tradeItems.end()) {
				ReleaseItem(it->first);
				tradeItems.erase(it);
			}

			tradePartner->tradeItem->onTradeEvent(ON_TRADE_CANCEL, tradePartner);
			tradePartner->tradeItem = nullptr;
		}

		tradePartner->setTradeState(TRADE_NONE);
		tradePartner->tradePartner = nullptr;

		if (sendCancel) {
			tradePartner->sendTextMessage(MESSAGE_STATUS_SMALL, "Trade cancelled.");
		}
		tradePartner->sendTradeClose();
	}
}

void Game::playerPurchaseItem(uint32_t playerId, uint16_t spriteId, uint8_t count, uint8_t amount,
                              bool ignoreCap/* = false*/, bool inBackpacks/* = false*/)
{
	if (amount == 0 || amount > 100) {
		return;
	}

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	int32_t onBuy, onSell;

	Npc* merchant = player->getShopOwner(onBuy, onSell);
	if (!merchant) {
		return;
	}

	const ItemType& it = Item::items.getItemIdByClientId(spriteId);
	if (it.id == 0) {
		return;
	}

	uint8_t subType;
	if (it.isSplash() || it.isFluidContainer()) {
		subType = clientFluidToServer(count);
	} else {
		subType = count;
	}

	if (!player->hasShopItemForSale(it.id, subType)) {
		return;
	}

	merchant->onPlayerTrade(player, onBuy, it.id, subType, amount, ignoreCap, inBackpacks);
}

void Game::playerSellItem(uint32_t playerId, uint16_t spriteId, uint8_t count, uint8_t amount, bool ignoreEquipped)
{
	if (amount == 0 || amount > 100) {
		return;
	}

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	int32_t onBuy, onSell;

	Npc* merchant = player->getShopOwner(onBuy, onSell);
	if (!merchant) {
		return;
	}

	const ItemType& it = Item::items.getItemIdByClientId(spriteId);
	if (it.id == 0) {
		return;
	}

	uint8_t subType;
	if (it.isSplash() || it.isFluidContainer()) {
		subType = clientFluidToServer(count);
	} else {
		subType = count;
	}

	merchant->onPlayerTrade(player, onSell, it.id, subType, amount, ignoreEquipped);
}

void Game::playerCloseShop(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	player->closeShopWindow();
}

void Game::playerLookInShop(uint32_t playerId, uint16_t spriteId, uint8_t count)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}
	if (g_playerShop.shouldBlockInventoryAction(player)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	int32_t onBuy, onSell;

	Npc* merchant = player->getShopOwner(onBuy, onSell);
	if (!merchant) {
		return;
	}

	const ItemType& it = Item::items.getItemIdByClientId(spriteId);
	if (it.id == 0) {
		return;
	}

	int32_t subType;
	if (it.isFluidContainer() || it.isSplash()) {
		subType = clientFluidToServer(count);
	} else {
		subType = count;
	}

	if (!player->hasShopItemForSale(it.id, subType)) {
		return;
	}

	const std::string& description = Item::getDescription(it, 1, nullptr, subType);
	g_events->eventPlayerOnLookInShop(player, &it, subType, description);
}

void Game::playerLookAt(uint32_t playerId, const Position& pos, uint8_t stackPos)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Thing* thing = internalGetThing(player, pos, stackPos, 0, STACKPOS_LOOK);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Position thingPos = thing->getPosition();
	if (!player->canSee(thingPos)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Position playerPos = player->getPosition();

	int32_t lookDistance;
	if (thing != player) {
		lookDistance = std::max<int32_t>(Position::getDistanceX(playerPos, thingPos), Position::getDistanceY(playerPos, thingPos));
		if (playerPos.z != thingPos.z) {
			lookDistance += 15;
		}
	} else {
		lookDistance = -1;
	}

	g_events->eventPlayerOnLook(player, pos, thing, stackPos, lookDistance);
}

void Game::playerLookInBattleList(uint32_t playerId, uint32_t creatureId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Creature* creature = getCreatureByID(creatureId);
	if (!creature) {
		return;
	}

	if (!player->canSeeCreature(creature)) {
		return;
	}

	const Position& creaturePos = creature->getPosition();
	if (!player->canSee(creaturePos)) {
		return;
	}

	int32_t lookDistance;
	if (creature != player) {
		const Position& playerPos = player->getPosition();
		lookDistance = std::max<int32_t>(Position::getDistanceX(playerPos, creaturePos), Position::getDistanceY(playerPos, creaturePos));
		if (playerPos.z != creaturePos.z) {
			lookDistance += 15;
		}
	} else {
		lookDistance = -1;
	}

	g_events->eventPlayerOnLookInBattleList(player, creature, lookDistance);
}

void Game::playerCancelAttackAndFollow(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	playerSetAttackedCreature(playerId, 0);
	playerFollowCreature(playerId, 0);
	player->stopWalk();
}

void Game::playerSetAttackedCreature(uint32_t playerId, uint32_t creatureId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (player->getAttackedCreature() && creatureId == 0) {
		player->setAttackedCreature(nullptr);
		player->sendCancelTarget();
		return;
	}

	Creature* attackCreature = getCreatureByID(creatureId);
	if (!attackCreature) {
		player->setAttackedCreature(nullptr);
		player->sendCancelTarget();
		return;
	}

	ReturnValue ret = Combat::canTargetCreature(player, attackCreature);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
		player->sendCancelTarget();
		player->setAttackedCreature(nullptr);
		return;
	}

	player->setAttackedCreature(attackCreature);
	g_dispatcher.addTask(createTask(std::bind(&Game::updateCreatureWalk, this, player->getID())));
}

void Game::playerFollowCreature(uint32_t playerId, uint32_t creatureId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->setAttackedCreature(nullptr);
	g_dispatcher.addTask(createTask(std::bind(&Game::updateCreatureWalk, this, player->getID())));
	player->setFollowCreature(getCreatureByID(creatureId));
}

void Game::playerSetFightModes(uint32_t playerId, fightMode_t fightMode, bool chaseMode, bool secureMode)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->setFightMode(fightMode);
	player->setChaseMode(chaseMode);
	player->setSecureMode(secureMode);
}

void Game::playerRequestAddVip(uint32_t playerId, const std::string& name)
{
	if (name.length() > PLAYER_NAME_LENGTH) {
		return;
	}

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Player* vipPlayer = getPlayerByName(name);
	if (!vipPlayer) {
		uint32_t guid;
		bool specialVip;
		std::string formattedName = name;
		if (!IOLoginData::getGuidByNameEx(guid, specialVip, formattedName)) {
			player->sendTextMessage(MESSAGE_STATUS_SMALL, "A player with this name does not exist.");
			return;
		}

		if (specialVip && !player->hasFlag(PlayerFlag_SpecialVIP)) {
			player->sendTextMessage(MESSAGE_STATUS_SMALL, "You can not add this player.");
			return;
		}

		player->addVIP(guid, formattedName, VIPSTATUS_OFFLINE);
	} else {
		if (vipPlayer->hasFlag(PlayerFlag_SpecialVIP) && !player->hasFlag(PlayerFlag_SpecialVIP)) {
			player->sendTextMessage(MESSAGE_STATUS_SMALL, "You can not add this player.");
			return;
		}

		if (!vipPlayer->isInGhostMode() || player->canSeeGhostMode(vipPlayer)) {
			player->addVIP(vipPlayer->getGUID(), vipPlayer->getName(), VIPSTATUS_ONLINE);
		} else {
			player->addVIP(vipPlayer->getGUID(), vipPlayer->getName(), VIPSTATUS_OFFLINE);
		}
	}
}

void Game::playerRequestRemoveVip(uint32_t playerId, uint32_t guid)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->removeVIP(guid);
}

void Game::playerRequestEditVip(uint32_t playerId, uint32_t guid, const std::string& description, uint32_t icon, bool notify)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->editVIP(guid, description, icon, notify);
}

void Game::playerTurn(uint32_t playerId, Direction dir)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!g_events->eventPlayerOnTurn(player, dir)) {
		return;
	}

	player->resetIdleTime();
	internalCreatureTurn(player, dir);
}

void Game::playerRequestOutfit(uint32_t playerId)
{
	if (!g_config.getBoolean(ConfigManager::ALLOW_CHANGEOUTFIT)) {
		return;
	}

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->sendOutfitWindow();
}

void Game::playerToggleMount(uint32_t playerId, bool mount)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->toggleMount(mount);
}

void Game::playerChangeOutfit(uint32_t playerId, Outfit_t outfit)
{
	if (!g_config.getBoolean(ConfigManager::ALLOW_CHANGEOUTFIT)) {
		return;
	}

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const Outfit* playerOutfit = Outfits::getInstance().getOutfitByLookType(player->getSex(), outfit.lookType);
	if (!playerOutfit) {
		outfit.lookMount = 0;
	}

	if (outfit.lookMount != 0) {
		Mount* mount = mounts.getMountByClientID(outfit.lookMount);
		if (!mount) {
			return;
		}

		if (!player->hasMount(mount)) {
			return;
		}

		if (player->isMounted()) {
			Mount* prevMount = mounts.getMountByID(player->getCurrentMount());
			if (prevMount) {
				changeSpeed(player, mount->speed - prevMount->speed);
			}

			player->setCurrentMount(mount->id);
		} else {
			player->setCurrentMount(mount->id);
			outfit.lookMount = 0;
		}
	} else if (player->isMounted()) {
		player->dismount();
	}

	if (player->canWear(outfit.lookType, outfit.lookAddons)) {
		player->defaultOutfit = outfit;

		if (player->hasCondition(CONDITION_OUTFIT)) {
			return;
		}

		internalCreatureChangeOutfit(player, outfit);
	}
}

void Game::playerShowQuestLog(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->sendQuestLog();
}

void Game::playerShowQuestLine(uint32_t playerId, uint16_t questId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Quest* quest = quests.getQuestByID(questId);
	if (!quest) {
		return;
	}

	player->sendQuestLine(quest);
}

void Game::playerSay(uint32_t playerId, uint16_t channelId, SpeakClasses type,
                     const std::string& receiver, const std::string& text)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->resetIdleTime();

	if (playerSaySpell(player, type, text)) {
		return;
	}

	if (g_playerShop.shouldBlockDefaultChat(player) && (type == TALKTYPE_SAY || type == TALKTYPE_WHISPER || type == TALKTYPE_YELL)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	uint32_t muteTime = player->isMuted();
	if (muteTime > 0) {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("You are still muted for {:d} seconds.", muteTime));
		return;
	}

	if (!text.empty() && text.front() == '/' && player->isAccessPlayer()) {
		return;
	}

	//if (type != TALKTYPE_PRIVATE_PN) {
	if (type != TALKTYPE_PRIVATE) {
		player->removeMessageBuffer();
	}

	switch (type) {
		case TALKTYPE_SAY:
			internalCreatureSay(player, TALKTYPE_SAY, text, false);
			break;

		case TALKTYPE_WHISPER:
			playerWhisper(player, text);
			break;

		case TALKTYPE_YELL:
			playerYell(player, text);
			break;

		//case TALKTYPE_PRIVATE_TO:
		//case TALKTYPE_PRIVATE_RED_TO:
		case TALKTYPE_PRIVATE:
		case TALKTYPE_PRIVATE_RED:
			playerSpeakTo(player, type, receiver, text);
			break;

		case TALKTYPE_CHANNEL_O:
		case TALKTYPE_CHANNEL_Y:
		case TALKTYPE_CHANNEL_R1:
		case TALKTYPE_CHANNEL_R2:
			g_chat->talkToChannel(*player, type, text, channelId);
			break;

		/*case TALKTYPE_PRIVATE_PN:
			playerSpeakToNpc(player, text);
			break;
			*/

		case TALKTYPE_BROADCAST:
			playerBroadcastMessage(player, text);
			break;

		default:
			break;
	}
}

bool Game::playerSaySpell(Player* player, SpeakClasses type, const std::string& text)
{
	std::string words = text;

	TalkActionResult_t result = g_talkActions->playerSaySpell(player, type, words);
	if (result == TALKACTION_BREAK) {
		return true;
	}

	result = g_spells->playerSaySpell(player, words);
	if (result == TALKACTION_BREAK) {
		if (!g_config.getBoolean(ConfigManager::EMOTE_SPELLS)) {
			return internalCreatureSay(player, TALKTYPE_SAY, words, false);
		} else {
			return internalCreatureSay(player, TALKTYPE_MONSTER_SAY, words, false);
		}

	} else if (result == TALKACTION_FAILED) {
		return true;
	}

	return false;
}

void Game::playerWhisper(Player* player, const std::string& text)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, player->getPosition(), false, false,
	              Map::maxClientViewportX, Map::maxClientViewportX,
	              Map::maxClientViewportY, Map::maxClientViewportY);

	//send to client
	for (Creature* spectator : spectators) {
		if (Player* spectatorPlayer = spectator->getPlayer()) {
			if (!Position::areInRange<1, 1>(player->getPosition(), spectatorPlayer->getPosition())) {
				spectatorPlayer->sendCreatureSay(player, TALKTYPE_WHISPER, "pspsps");
			} else {
				spectatorPlayer->sendCreatureSay(player, TALKTYPE_WHISPER, text);
			}
		}
	}

	//event method
	for (Creature* spectator : spectators) {
		spectator->onCreatureSay(player, TALKTYPE_WHISPER, text);
	}
}

bool Game::playerYell(Player* player, const std::string& text)
{
	if (player->hasCondition(CONDITION_YELLTICKS)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return false;
	}

	if (!player->isAccessPlayer() && !player->hasFlag(PlayerFlag_IgnoreYellCheck)) {
		uint32_t minimumLevel = g_config.getNumber(ConfigManager::YELL_MINIMUM_LEVEL);
		if (player->getLevel() < minimumLevel) {
			if (g_config.getBoolean(ConfigManager::YELL_ALLOW_PREMIUM)) {
				if (!player->isPremium()) {
					player->sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("You may not yell unless you have reached level {:d} or have a premium account.", minimumLevel));
					return false;
				}
			} else {
				player->sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("You may not yell unless you have reached level {:d}.", minimumLevel));
				return false;
			}
		}

		Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_YELLTICKS, 30000, 0);
		player->addCondition(condition);
	}

	internalCreatureSay(player, TALKTYPE_YELL, asUpperCaseString(text), false);
	return true;
}

bool Game::playerSpeakTo(Player* player, SpeakClasses type, const std::string& receiver,
                         const std::string& text)
{
	Player* toPlayer = getPlayerByName(receiver);
	if (!toPlayer) {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, "A player with this name is not online.");
		return false;
	}

	/*if (type == TALKTYPE_PRIVATE_RED_TO && (player->hasFlag(PlayerFlag_CanTalkRedPrivate) || player->getAccountType() >= ACCOUNT_TYPE_GAMEMASTER)) {
		type = TALKTYPE_PRIVATE_RED_FROM;
	} else {
		type = TALKTYPE_PRIVATE_FROM;
	}*/

	if (type == TALKTYPE_PRIVATE_RED && (player->hasFlag(PlayerFlag_CanTalkRedPrivate) || player->getAccountType() >= ACCOUNT_TYPE_GAMEMASTER)) {
		type = TALKTYPE_PRIVATE_RED;
	} else {
		type = TALKTYPE_PRIVATE;
	}

	if (!player->isAccessPlayer() && !player->hasFlag(PlayerFlag_IgnoreSendPrivateCheck)) {
		uint32_t minimumLevel = g_config.getNumber(ConfigManager::MINIMUM_LEVEL_TO_SEND_PRIVATE);
		if (player->getLevel() < minimumLevel) {
			if (g_config.getBoolean(ConfigManager::PREMIUM_TO_SEND_PRIVATE)) {
				if (!player->isPremium()) {
					player->sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("You may not send private messages unless you have reached level {:d} or have a premium account.", minimumLevel));
					return false;
				}
			} else {
				player->sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("You may not send private messages unless you have reached level {:d}.", minimumLevel));
				return false;
			}
		}
	}

	toPlayer->sendPrivateMessage(player, type, text);
	toPlayer->onCreatureSay(player, type, text);

	if (toPlayer->isInGhostMode() && !player->canSeeGhostMode(toPlayer)) {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, "A player with this name is not online.");
	} else {
		player->sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("Message sent to {:s}.", toPlayer->getName()));
	}
	return true;
}

void Game::playerSpeakToNpc(Player* player, const std::string& text)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, player->getPosition());
	for (Creature* spectator : spectators) {
		if (spectator->getNpc()) {
			spectator->onCreatureSay(player, TALKTYPE_PRIVATE, text);
		}
	}
}

//--
bool Game::canThrowObjectTo(const Position& fromPos, const Position& toPos, bool checkLineOfSight /*= true*/, bool sameFloor /*= false*/,
                            int32_t rangex /*= Map::maxClientViewportX*/, int32_t rangey /*= Map::maxClientViewportY*/) const
{
	return map.canThrowObjectTo(fromPos, toPos, checkLineOfSight, sameFloor, rangex, rangey);
}

bool Game::isSightClear(const Position& fromPos, const Position& toPos, bool sameFloor /*= false*/) const
{
	return map.isSightClear(fromPos, toPos, sameFloor);
}

bool Game::internalCreatureTurn(Creature* creature, Direction dir)
{
	if (creature->getDirection() == dir) {
		return false;
	}

	creature->setDirection(dir);

	//send to client
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		spectator->getPlayer()->sendCreatureTurn(creature);
	}
	return true;
}

bool Game::internalCreatureSay(Creature* creature, SpeakClasses type, const std::string& text,
                               bool ghostMode, SpectatorVec* spectatorsPtr/* = nullptr*/, const Position* pos/* = nullptr*/)
{
	if (text.empty()) {
		return false;
	}

	if (!pos) {
		pos = &creature->getPosition();
	}

	SpectatorVec spectators;

	if (!spectatorsPtr || spectatorsPtr->empty()) {
		// This somewhat complex construct ensures that the cached SpectatorVec
		// is used if available and if it can be used, else a local vector is
		// used (hopefully the compiler will optimize away the construction of
		// the temporary when it's not used).
		if (type != TALKTYPE_YELL && type != TALKTYPE_MONSTER_YELL) {
			map.getSpectators(spectators, *pos, false, false,
			              Map::maxClientViewportX, Map::maxClientViewportX,
			              Map::maxClientViewportY, Map::maxClientViewportY);
		} else {
			map.getSpectators(spectators, *pos, true, false,
						(Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportX * 2) + 2,
						(Map::maxClientViewportY * 2) + 2, (Map::maxClientViewportY * 2) + 2);
		}
	} else {
		spectators = (*spectatorsPtr);
	}

	//send to client
	for (Creature* spectator : spectators) {
		if (Player* tmpPlayer = spectator->getPlayer()) {
			if (!ghostMode || tmpPlayer->canSeeCreature(creature)) {
				tmpPlayer->sendCreatureSay(creature, type, text, pos);
			}
		}
	}

	//event method
	for (Creature* spectator : spectators) {
		spectator->onCreatureSay(creature, type, text);
		if (creature != spectator) {
			g_events->eventCreatureOnHear(spectator, creature, text, type);
		}
	}
	return true;
}

void Game::checkCreatureWalk(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (creature && creature->getHealth() > 0) {
		creature->onWalk();
		cleanup();
	}
}

void Game::updateCreatureWalk(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (creature && creature->getHealth() > 0) {
		creature->goToFollowCreature();
	}
}

void Game::checkCreatureAttack(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (creature && creature->getHealth() > 0) {
		creature->onAttacking(0);
	}
}

void Game::addCreatureCheck(Creature* creature)
{
	creature->creatureCheck = true;

	if (creature->inCheckCreaturesVector) {
		// already in a vector
		return;
	}

	creature->inCheckCreaturesVector = true;
	checkCreatureLists[uniform_random(0, EVENT_CREATURECOUNT - 1)].push_back(creature);
	creature->incrementReferenceCounter();
}

void Game::removeCreatureCheck(Creature* creature)
{
	if (creature->inCheckCreaturesVector) {
		creature->creatureCheck = false;
	}
}

void Game::checkCreatures(size_t index)
{
	g_scheduler.addEvent(createSchedulerTask(EVENT_CHECK_CREATURE_INTERVAL, std::bind(&Game::checkCreatures, this, (index + 1) % EVENT_CREATURECOUNT)));

	auto& checkCreatureList = checkCreatureLists[index];
	auto it = checkCreatureList.begin(), end = checkCreatureList.end();
	while (it != end) {
		Creature* creature = *it;
		if (creature->creatureCheck) {
			if (creature->getHealth() > 0) {
				creature->onThink(EVENT_CREATURE_THINK_INTERVAL);
				creature->onAttacking(EVENT_CREATURE_THINK_INTERVAL);
				creature->executeConditions(EVENT_CREATURE_THINK_INTERVAL);
			}
			++it;
		} else {
			creature->inCheckCreaturesVector = false;
			it = checkCreatureList.erase(it);
			ReleaseCreature(creature);
		}
	}

	cleanup();
}

void Game::changeSpeed(Creature* creature, int32_t varSpeedDelta)
{
	int32_t varSpeed = creature->getSpeed() - creature->getBaseSpeed();
	varSpeed += varSpeedDelta;

	creature->setSpeed(varSpeed);

	//send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), false, true);
	for (Creature* spectator : spectators) {
		spectator->getPlayer()->sendChangeSpeed(creature, creature->getStepSpeed());
	}
}

void Game::internalCreatureChangeOutfit(Creature* creature, const Outfit_t& outfit)
{
	if (!g_events->eventCreatureOnChangeOutfit(creature, outfit)) {
		return;
	}

	creature->setCurrentOutfit(outfit);

	if (creature->isInvisible()) {
		return;
	}

	//send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		spectator->getPlayer()->sendCreatureChangeOutfit(creature, outfit);
	}
}

void Game::internalCreatureChangeVisible(Creature* creature, bool visible)
{
	//send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		spectator->getPlayer()->sendCreatureChangeVisible(creature, visible);
	}
}

void Game::changeLight(const Creature* creature)
{
	//send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		spectator->getPlayer()->sendCreatureLight(creature);
	}
}

bool Game::combatBlockHit(CombatDamage& damage, Creature* attacker, Creature* target, bool checkDefense, bool checkArmor, bool field, bool ignoreResistances /*= false */)
{
	if (damage.primary.type == COMBAT_NONE && damage.secondary.type == COMBAT_NONE) {
		return true;
	}

	if (target->getPlayer() && target->isInGhostMode()) {
		return true;
	}

	if (damage.primary.value > 0) {
		return false;
	}

	static const auto sendBlockEffect = [this](BlockType_t blockType, CombatType_t combatType, const Position& targetPos) {
		if (blockType == BLOCK_DEFENSE) {
			addMagicEffect(targetPos, CONST_ME_POFF);
		} else if (blockType == BLOCK_ARMOR) {
			addMagicEffect(targetPos, CONST_ME_BLOCKHIT);
		} else if (blockType == BLOCK_IMMUNITY) {
			uint8_t hitEffect = 0;
			switch (combatType) {
				case COMBAT_UNDEFINEDDAMAGE: {
					return;
				}
				case COMBAT_ENERGYDAMAGE:
				case COMBAT_FIREDAMAGE:
				case COMBAT_PHYSICALDAMAGE:
				case COMBAT_ICEDAMAGE:
				case COMBAT_DEATHDAMAGE: {
					hitEffect = CONST_ME_BLOCKHIT;
					break;
				}
				case COMBAT_EARTHDAMAGE: {
					hitEffect = CONST_ME_GREEN_RINGS;
					break;
				}
				case COMBAT_HOLYDAMAGE: {
					hitEffect = CONST_ME_HOLYDAMAGE;
					break;
				}
				default: {
					hitEffect = CONST_ME_POFF;
					break;
				}
			}
			addMagicEffect(targetPos, hitEffect);
		}
	};

	BlockType_t primaryBlockType, secondaryBlockType;
	if (damage.primary.type != COMBAT_NONE) {
		damage.primary.value = -damage.primary.value;
		const bool rangedIgnoresDefense = damage.origin == ORIGIN_RANGED;
		const bool criticalCreatureHit = damage.critical && target->getMonster() && !Combat::isPlayerCombat(target);
		if (criticalCreatureHit) {
			const int32_t originalDamage = damage.primary.value;
			if (checkArmor) {
				damage.primary.value -= target->getArmorReduction(attacker, damage.primary.type);
				if (damage.primary.value < 0) {
					damage.primary.value = 0;
				}
			}

			primaryBlockType = target->blockHit(attacker, damage.primary.type, damage.primary.value, false, false, field, ignoreResistances);
			if (primaryBlockType == BLOCK_NONE && checkArmor && originalDamage > 0 && damage.primary.value == 0) {
				primaryBlockType = BLOCK_ARMOR;
			}
		} else {
			primaryBlockType = target->blockHit(attacker, damage.primary.type, damage.primary.value, rangedIgnoresDefense ? false : checkDefense, checkArmor, field, ignoreResistances);
		}

		damage.primary.value = -damage.primary.value;
		sendBlockEffect(primaryBlockType, damage.primary.type, target->getPosition());
	} else {
		primaryBlockType = BLOCK_NONE;
	}

	if (damage.secondary.type != COMBAT_NONE) {
		damage.secondary.value = -damage.secondary.value;
		secondaryBlockType = target->blockHit(attacker, damage.secondary.type, damage.secondary.value, false, false, field, ignoreResistances);
		damage.secondary.value = -damage.secondary.value;
		sendBlockEffect(secondaryBlockType, damage.secondary.type, target->getPosition());
	} else {
		secondaryBlockType = BLOCK_NONE;
	}

	damage.blockType = primaryBlockType;

	return (primaryBlockType != BLOCK_NONE) && (secondaryBlockType != BLOCK_NONE);
}

void Game::combatGetTypeInfo(CombatType_t combatType, Creature* target, TextColor_t& color, uint8_t& effect)
{
	switch (combatType) {
		case COMBAT_PHYSICALDAMAGE: {
			Item* splash = nullptr;
			switch (target->getRace()) {
				case RACE_VENOM:
					color = TEXTCOLOR_LIGHTGREEN;
					effect = CONST_ME_HITBYPOISON;
					splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_SLIME);
					break;
				case RACE_BLOOD:
					color = TEXTCOLOR_RED;
					effect = CONST_ME_DRAWBLOOD;
					if (const Tile* tile = target->getTile()) {
						if (!tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
							splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_BLOOD);
						}
					}
					break;
				case RACE_UNDEAD:
					color = TEXTCOLOR_LIGHTGREY;
					effect = CONST_ME_HITAREA;
					break;
				case RACE_FIRE:
					color = TEXTCOLOR_RED;
					effect = CONST_ME_DRAWBLOOD;
					break;
				case RACE_ENERGY:
					color = TEXTCOLOR_ELECTRICPURPLE;
					effect = CONST_ME_ENERGYHIT;
					break;
				default:
					color = TEXTCOLOR_NONE;
					effect = CONST_ME_NONE;
					break;
			}

			if (splash) {
				internalAddItem(target->getTile(), splash, INDEX_WHEREEVER, FLAG_NOLIMIT);
				startDecay(splash);
			}

			break;
		}

		case COMBAT_ENERGYDAMAGE: {
			color = TEXTCOLOR_ELECTRICPURPLE;
			effect = CONST_ME_ENERGYHIT;
			break;
		}

		case COMBAT_EARTHDAMAGE: {
			color = TEXTCOLOR_LIGHTGREEN;
			effect = CONST_ME_GREEN_RINGS;
			break;
		}

		case COMBAT_DROWNDAMAGE: {
			color = TEXTCOLOR_LIGHTBLUE;
			effect = CONST_ME_LOSEENERGY;
			break;
		}
		case COMBAT_FIREDAMAGE: {
			color = TEXTCOLOR_ORANGE;
			effect = CONST_ME_HITBYFIRE;
			break;
		}
		case COMBAT_ICEDAMAGE: {
			color = TEXTCOLOR_SKYBLUE;
			effect = CONST_ME_ICEATTACK;
			break;
		}
		case COMBAT_HOLYDAMAGE: {
			color = TEXTCOLOR_YELLOW;
			effect = CONST_ME_HOLYDAMAGE;
			break;
		}
		case COMBAT_DEATHDAMAGE: {
			color = TEXTCOLOR_DARKRED;
			effect = CONST_ME_SMALLCLOUDS;
			break;
		}
		case COMBAT_LIFEDRAIN: {
			color = TEXTCOLOR_RED;
			effect = CONST_ME_MAGIC_RED;
			break;
		}
		default: {
			color = TEXTCOLOR_NONE;
			effect = CONST_ME_NONE;
			break;
		}
	}
}

bool Game::combatChangeHealth(Creature* attacker, Creature* target, CombatDamage& damage)
{
	const Position& targetPos = target->getPosition();
	if (damage.primary.value > 0) {
		if (target->getHealth() <= 0) {
			return false;
		}

		Player* attackerPlayer;
		if (attacker) {
			attackerPlayer = attacker->getPlayer();
		} else {
			attackerPlayer = nullptr;
		}

		Player* targetPlayer = target->getPlayer();
		if (attackerPlayer && targetPlayer && attackerPlayer->getSkull() == SKULL_BLACK && attackerPlayer->getSkullClient(targetPlayer) == SKULL_NONE) {
			return false;
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto& events = target->getCreatureEvents(CREATURE_EVENT_HEALTHCHANGE);
			if (!events.empty()) {
				for (CreatureEvent* creatureEvent : events) {
					creatureEvent->executeHealthChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeHealth(attacker, target, damage);
			}
		}

		int32_t realHealthChange = target->getHealth();
		target->gainHealth(attacker, damage.primary.value);
		realHealthChange = target->getHealth() - realHealthChange;

		if (realHealthChange > 0 && !target->isInGhostMode()) {
			auto damageString = fmt::format("{:d} hitpoint{:s}", realHealthChange, realHealthChange != 1 ? "s" : "");

			std::string spectatorMessage;

			TextMessage message;
			/*message.position = targetPos;
			message.primary.value = realHealthChange;
			message.primary.color = TEXTCOLOR_PASTELRED;
			*/

			SpectatorVec spectators;
			map.getSpectators(spectators, targetPos, false, true);
			for (Creature* spectator : spectators) {
				Player* tmpPlayer = spectator->getPlayer();
				if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
					//message.type = MESSAGE_HEALED;
					message.type = MESSAGE_EVENT_DEFAULT;
					message.text = fmt::format("You heal {:s} for {:s}.", target->getNameDescription(), damageString);
				} else if (tmpPlayer == targetPlayer) {
					//message.type = MESSAGE_HEALED;
					message.type = MESSAGE_EVENT_DEFAULT;
					if (!attacker) {
						message.text = fmt::format("You were healed for {:s}.", damageString);
					} else if (targetPlayer == attackerPlayer) {
						message.text = fmt::format("You healed yourself for {:s}.", damageString);
					} else {
						message.text = fmt::format("You were healed by {:s} for {:s}.", attacker->getNameDescription(), damageString);
					}
				} else {
					//message.type = MESSAGE_HEALED_OTHERS;
					message.type = MESSAGE_EVENT_DEFAULT;
					if (spectatorMessage.empty()) {
						if (!attacker) {
							spectatorMessage = fmt::format("{:s} was healed for {:s}.", target->getNameDescription(), damageString);
						} else if (attacker == target) {
							spectatorMessage = fmt::format("{:s} healed {:s}self for {:s}.", attacker->getNameDescription(), targetPlayer ? (targetPlayer->getSex() == PLAYERSEX_FEMALE ? "her" : "him") : "it", damageString);
						} else {
							spectatorMessage = fmt::format("{:s} healed {:s} for {:s}.", attacker->getNameDescription(), target->getNameDescription(), damageString);
						}
						spectatorMessage[0] = std::toupper(spectatorMessage[0]);
					}
					message.text = spectatorMessage;
				}
				tmpPlayer->sendTextMessage(message);
			}
		}
	} else {
		if (!target->isAttackable()) {
			if (!target->isInGhostMode()) {
				addMagicEffect(targetPos, CONST_ME_POFF);
			}
			return true;
		}

		Player* attackerPlayer;
		if (attacker) {
			attackerPlayer = attacker->getPlayer();
		} else {
			attackerPlayer = nullptr;
		}

		Player* targetPlayer = target->getPlayer();
		if (attackerPlayer && targetPlayer && attackerPlayer->getSkull() == SKULL_BLACK && attackerPlayer->getSkullClient(targetPlayer) == SKULL_NONE) {
			return false;
		}

		damage.primary.value = std::abs(damage.primary.value);
		damage.secondary.value = std::abs(damage.secondary.value);

		// Elite Creatures: amplify damage dealt by elite monsters. The
		// origin check keeps the recursive CREATURE_EVENT_HEALTHCHANGE
		// re-entry (origin reset to ORIGIN_NONE) from stacking the bonus.
		if (attacker && damage.origin != ORIGIN_NONE) {
			if (const Monster* attackerMonster = attacker->getMonster()) {
				const double eliteDamageMult = EliteCreatures::damageMultiplier(attackerMonster->getEliteTier());
				if (eliteDamageMult > 1.0) {
					damage.primary.value = static_cast<int32_t>(damage.primary.value * eliteDamageMult);
					damage.secondary.value = static_cast<int32_t>(damage.secondary.value * eliteDamageMult);
				}
			}
		}

		int32_t healthChange = damage.primary.value + damage.secondary.value;
		if (healthChange == 0) {
			return true;
		}

		TextMessage message;
		//message.position = targetPos;

		ColoredText coloredText;
		coloredText.position = targetPos;

		SpectatorVec spectators;
		if (targetPlayer && target->hasCondition(CONDITION_MANASHIELD) && damage.primary.type != COMBAT_UNDEFINEDDAMAGE) {
			int32_t manaDamage = std::min<int32_t>(targetPlayer->getMana(), healthChange);
			if (manaDamage != 0) {
				if (damage.origin != ORIGIN_NONE) {
					const auto& events = target->getCreatureEvents(CREATURE_EVENT_MANACHANGE);
					if (!events.empty()) {
						for (CreatureEvent* creatureEvent : events) {
							creatureEvent->executeManaChange(target, attacker, damage);
						}
						healthChange = damage.primary.value + damage.secondary.value;
						if (healthChange == 0) {
							return true;
						}
						manaDamage = std::min<int32_t>(targetPlayer->getMana(), healthChange);
					}
				}

				targetPlayer->drainMana(attacker, manaDamage);
				map.getSpectators(spectators, targetPos, true, true);
				addMagicEffect(spectators, targetPos, CONST_ME_LOSEENERGY);

				std::string spectatorMessage;

				//message.primary.value = manaDamage;
				//message.primary.color = TEXTCOLOR_BLUE;

				coloredText.text = std::to_string(manaDamage);
				coloredText.position = targetPos;
				coloredText.color = TEXTCOLOR_BLUE;

				for (Creature* spectator : spectators) {
					Player* tmpPlayer = spectator->getPlayer();
					if (!tmpPlayer || tmpPlayer->getPosition().z != targetPos.z) {
						continue;
					}

					if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
						//message.type = MESSAGE_DAMAGE_DEALT;
						message.type = MESSAGE_STATUS_SMALL;
						message.text = fmt::format("{:s} loses {:d} mana due to your attack.", target->getNameDescription(), manaDamage);
						message.text[0] = std::toupper(message.text[0]);
					} else if (tmpPlayer == targetPlayer) {
						//message.type = MESSAGE_DAMAGE_RECEIVED;
						message.type = MESSAGE_STATUS_SMALL;
						if (!attacker) {
							message.text = fmt::format("You lose {:d} mana.", manaDamage);
						} else if (targetPlayer == attackerPlayer) {
							message.text = fmt::format("You lose {:d} mana due to your own attack.", manaDamage);
						} else {
							message.text = fmt::format("You lose {:d} mana due to an attack by {:s}.", manaDamage, attacker->getNameDescription());
						}
					} else {
						//message.type = MESSAGE_DAMAGE_OTHERS;
						message.type = MESSAGE_STATUS_SMALL;
						if (spectatorMessage.empty()) {
							if (!attacker) {
								spectatorMessage = fmt::format("{:s} loses {:d} mana.", target->getNameDescription(), manaDamage);
							} else if (attacker == target) {
								spectatorMessage = fmt::format("{:s} loses {:d} mana due to {:s} own attack.", target->getNameDescription(), manaDamage, targetPlayer->getSex() == PLAYERSEX_FEMALE ? "her" : "his");
							} else {
								spectatorMessage = fmt::format("{:s} loses {:d} mana due to an attack by {:s}.", target->getNameDescription(), manaDamage, attacker->getNameDescription());
							}
							spectatorMessage[0] = std::toupper(spectatorMessage[0]);
						}
						message.text = spectatorMessage;
					}
					tmpPlayer->sendTextMessage(message);
					tmpPlayer->sendColoredText(coloredText);
				}

				damage.primary.value -= manaDamage;
				if (damage.primary.value < 0) {
					damage.secondary.value = std::max<int32_t>(0, damage.secondary.value + damage.primary.value);
					damage.primary.value = 0;
				}
			}
		}

		int32_t realDamage = damage.primary.value + damage.secondary.value;
		if (realDamage == 0) {
			return true;
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto& events = target->getCreatureEvents(CREATURE_EVENT_HEALTHCHANGE);
			if (!events.empty()) {
				for (CreatureEvent* creatureEvent : events) {
					creatureEvent->executeHealthChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeHealth(attacker, target, damage);
			}
		}

		int32_t targetHealth = target->getHealth();
		if (damage.primary.value >= targetHealth) {
			damage.primary.value = targetHealth;
			damage.secondary.value = 0;
		} else if (damage.secondary.value) {
			damage.secondary.value = std::min<int32_t>(damage.secondary.value, targetHealth - damage.primary.value);
		}

		realDamage = damage.primary.value + damage.secondary.value;
		if (realDamage == 0) {
			return true;
		}

		if (spectators.empty()) {
			map.getSpectators(spectators, targetPos, true, true);
		}

		//message.primary.value = damage.primary.value;
		//message.secondary.value = damage.secondary.value;

		coloredText.text = std::to_string(damage.primary.value);

		uint8_t hitEffect;
		//if (message.primary.value) {
		if (!coloredText.text.empty()) {
			//combatGetTypeInfo(damage.primary.type, target, message.primary.color, hitEffect);
			combatGetTypeInfo(damage.primary.type, target, coloredText.color, hitEffect);
			if (hitEffect != CONST_ME_NONE) {
				addMagicEffect(spectators, targetPos, hitEffect);
			}
		}

		/*if (message.secondary.value) {
			combatGetTypeInfo(damage.secondary.type, target, message.secondary.color, hitEffect);
			if (hitEffect != CONST_ME_NONE) {
				addMagicEffect(spectators, targetPos, hitEffect);
			}
		}*/

		if (coloredText.color != TEXTCOLOR_NONE) {
		//if (message.primary.color != TEXTCOLOR_NONE || message.secondary.color != TEXTCOLOR_NONE) {
			auto damageString = fmt::format("{:d} hitpoint{:s}", realDamage, realDamage != 1 ? "s" : "");

			std::string spectatorMessage;

			for (Creature* spectator : spectators) {
				Player* tmpPlayer = spectator->getPlayer();
				if (tmpPlayer->getPosition().z != targetPos.z) {
					continue;
				}

				if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
					//message.type = MESSAGE_DAMAGE_DEALT;
					message.type = MESSAGE_EVENT_DEFAULT;
					message.text = fmt::format("{:s} loses {:s} due to your attack.", target->getNameDescription(), damageString);
					message.text[0] = std::toupper(message.text[0]);
				} else if (tmpPlayer == targetPlayer) {
					//message.type = MESSAGE_DAMAGE_RECEIVED;
					message.type = MESSAGE_EVENT_DEFAULT;
					if (!attacker) {
						message.text = fmt::format("You lose {:s}.", damageString);
					} else if (targetPlayer == attackerPlayer) {
						message.text = fmt::format("You lose {:s} due to your own attack.", damageString);
					} else {
						message.text = fmt::format("You lose {:s} due to an attack by {:s}.", damageString, attacker->getNameDescription());
					}
				} else {
					//message.type = MESSAGE_DAMAGE_OTHERS;
					message.type = MESSAGE_EVENT_DEFAULT;
					if (spectatorMessage.empty()) {
						if (!attacker) {
							spectatorMessage = fmt::format("{:s} loses {:s}.", target->getNameDescription(), damageString);
						} else if (attacker == target) {
							spectatorMessage = fmt::format("{:s} loses {:s} due to {:s} own attack.", target->getNameDescription(), damageString, targetPlayer ? (targetPlayer->getSex() == PLAYERSEX_FEMALE ? "her" : "his") : "its");
						} else {
							spectatorMessage = fmt::format("{:s} loses {:s} due to an attack by {:s}.", target->getNameDescription(), damageString, attacker->getNameDescription());
						}
						spectatorMessage[0] = std::toupper(spectatorMessage[0]);
					}
					message.text = spectatorMessage;
				}
				tmpPlayer->sendTextMessage(message);
				tmpPlayer->sendColoredText(coloredText);
			}
		}

		if (realDamage >= targetHealth) {
			for (CreatureEvent* creatureEvent : target->getCreatureEvents(CREATURE_EVENT_PREPAREDEATH)) {
				if (!creatureEvent->executeOnPrepareDeath(target, attacker)) {
					return false;
				}
			}
		}

		target->drainHealth(attacker, realDamage);
		addCreatureHealth(spectators, target);
	}

	return true;
}

bool Game::combatChangeMana(Creature* attacker, Creature* target, CombatDamage& damage)
{
	Player* targetPlayer = target->getPlayer();
	if (!targetPlayer) {
		return true;
	}

	int32_t manaChange = damage.primary.value + damage.secondary.value;
	if (manaChange > 0) {
		if (attacker) {
			const Player* attackerPlayer = attacker->getPlayer();
			if (attackerPlayer && attackerPlayer->getSkull() == SKULL_BLACK && attackerPlayer->getSkullClient(target) == SKULL_NONE) {
				return false;
			}
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto& events = target->getCreatureEvents(CREATURE_EVENT_MANACHANGE);
			if (!events.empty()) {
				for (CreatureEvent* creatureEvent : events) {
					creatureEvent->executeManaChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeMana(attacker, target, damage);
			}
		}

		//int32_t realManaChange = targetPlayer->getMana();
		targetPlayer->changeMana(manaChange);
		//realManaChange = targetPlayer->getMana() - realManaChange;

		/*if (realManaChange > 0 && !targetPlayer->isInGhostMode()) {
			TextMessage message(MESSAGE_HEALED, "You gained " + std::to_string(realManaChange) + " mana.");
			message.position = target->getPosition();
			message.primary.value = realManaChange;
			message.primary.color = TEXTCOLOR_MAYABLUE;
			targetPlayer->sendTextMessage(message);
		}*/
	} else {
		const Position& targetPos = target->getPosition();
		if (!target->isAttackable()) {
			if (!target->isInGhostMode()) {
				addMagicEffect(targetPos, CONST_ME_POFF);
			}
			return false;
		}

		Player* attackerPlayer;
		if (attacker) {
			attackerPlayer = attacker->getPlayer();
		} else {
			attackerPlayer = nullptr;
		}

		if (attackerPlayer && attackerPlayer->getSkull() == SKULL_BLACK && attackerPlayer->getSkullClient(targetPlayer) == SKULL_NONE) {
			return false;
		}

		int32_t manaLoss = std::min<int32_t>(targetPlayer->getMana(), -manaChange);
		BlockType_t blockType = target->blockHit(attacker, COMBAT_MANADRAIN, manaLoss);
		if (blockType != BLOCK_NONE) {
			addMagicEffect(targetPos, CONST_ME_POFF);
			return false;
		}

		if (manaLoss <= 0) {
			return true;
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto& events = target->getCreatureEvents(CREATURE_EVENT_MANACHANGE);
			if (!events.empty()) {
				for (CreatureEvent* creatureEvent : events) {
					creatureEvent->executeManaChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeMana(attacker, target, damage);
			}
		}

		targetPlayer->drainMana(attacker, manaLoss);

		std::string spectatorMessage;

		TextMessage message;
		/*message.position = targetPos;
		message.primary.value = manaLoss;
		message.primary.color = TEXTCOLOR_BLUE;*/

		ColoredText coloredText(std::to_string(manaLoss), targetPos, TEXTCOLOR_BLUE);

		SpectatorVec spectators;
		map.getSpectators(spectators, targetPos, false, true);
		for (Creature* spectator : spectators) {
			Player* tmpPlayer = spectator->getPlayer();
			if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
				//message.type = MESSAGE_DAMAGE_DEALT;
				message.type = MESSAGE_EVENT_DEFAULT;
				message.text = fmt::format("{:s} loses {:d} mana due to your attack.", target->getNameDescription(), manaLoss);
				message.text[0] = std::toupper(message.text[0]);
			} else if (tmpPlayer == targetPlayer) {
				//message.type = MESSAGE_DAMAGE_RECEIVED;
				message.type = MESSAGE_EVENT_DEFAULT;
				if (!attacker) {
					message.text = fmt::format("You lose {:d} mana.", manaLoss);
				} else if (targetPlayer == attackerPlayer) {
					message.text = fmt::format("You lose {:d} mana due to your own attack.", manaLoss);
				} else {
					message.text = fmt::format("You lose {:d} mana due to an attack by {:s}.", manaLoss, attacker->getNameDescription());
				}
			} else {
				//message.type = MESSAGE_DAMAGE_OTHERS;
				message.type = MESSAGE_EVENT_DEFAULT;
				if (spectatorMessage.empty()) {
					if (!attacker) {
						spectatorMessage = fmt::format("{:s} loses {:d} mana.", target->getNameDescription(), manaLoss);
					} else if (attacker == target) {
						spectatorMessage = fmt::format("{:s} loses {:d} mana due to {:s} own attack.", target->getNameDescription(), manaLoss, targetPlayer->getSex() == PLAYERSEX_FEMALE ? "her" : "his");
					} else {
						spectatorMessage = fmt::format("{:s} loses {:d} mana due to an attack by {:s}.", target->getNameDescription(), manaLoss, attacker->getNameDescription());
					}
					spectatorMessage[0] = std::toupper(spectatorMessage[0]);
				}
				message.text = spectatorMessage;
			}
			tmpPlayer->sendTextMessage(message);
			tmpPlayer->sendColoredText(coloredText);
		}
	}

	return true;
}

void Game::addCreatureHealth(const Creature* target)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, target->getPosition(), true, true);
	addCreatureHealth(spectators, target);
}

void Game::addCreatureHealth(const SpectatorVec& spectators, const Creature* target)
{
	for (Creature* spectator : spectators) {
		if (Player* tmpPlayer = spectator->getPlayer()) {
			tmpPlayer->sendCreatureHealth(target);
		}
	}
}

void Game::addColoredText(const ColoredText& coloredText)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, coloredText.position, true, true);
	addColoredText(spectators, coloredText);
}

void Game::addColoredText(const SpectatorVec& spectators, const ColoredText& coloredText)
{
	for (Creature* spectator : spectators) {
		if (Player* tmpPlayer = spectator->getPlayer()) {
			tmpPlayer->sendColoredText(coloredText);
		}
	}
}

void Game::addMagicEffect(const Position& pos, uint8_t effect)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, pos, true, true);
	addMagicEffect(spectators, pos, effect);
}

void Game::addMagicEffect(const SpectatorVec& spectators, const Position& pos, uint8_t effect)
{
	for (Creature* spectator : spectators) {
		if (Player* tmpPlayer = spectator->getPlayer()) {
			tmpPlayer->sendMagicEffect(pos, effect);
		}
	}
}

void Game::addDistanceEffect(const Position& fromPos, const Position& toPos, uint8_t effect)
{
	SpectatorVec spectators, toPosSpectators;
	map.getSpectators(spectators, fromPos, true, true);
	map.getSpectators(toPosSpectators, toPos, true, true);
	spectators.addSpectators(toPosSpectators);

	addDistanceEffect(spectators, fromPos, toPos, effect);
}

void Game::addDistanceEffect(const SpectatorVec& spectators, const Position& fromPos, const Position& toPos, uint8_t effect)
{
	for (Creature* spectator : spectators) {
		if (Player* tmpPlayer = spectator->getPlayer()) {
			tmpPlayer->sendDistanceShoot(fromPos, toPos, effect);
		}
	}
}

void Game::setAccountStorageValue(const uint32_t accountId, const uint32_t key, const int32_t value)
{
	if (value == -1) {
		accountStorageMap[accountId].erase(key);
		return;
	}

	accountStorageMap[accountId][key] = value;
}

int32_t Game::getAccountStorageValue(const uint32_t accountId, const uint32_t key) const
{
	const auto& accountMapIt = accountStorageMap.find(accountId);
	if (accountMapIt != accountStorageMap.end()) {
		const auto& storageMapIt = accountMapIt->second.find(key);
		if (storageMapIt != accountMapIt->second.end()) {
			return storageMapIt->second;
		}
	}
	return -1;
}

void Game::loadAccountStorageValues()
{
	Database& db = Database::getInstance();

	DBResult_ptr result;
	if ((result = db.storeQuery("SELECT `account_id`, `key`, `value` FROM `account_storage`"))) {
		do {
			g_game.setAccountStorageValue(result->getNumber<uint32_t>("account_id"), result->getNumber<uint32_t>("key"), result->getNumber<int32_t>("value"));
		} while (result->next());
	}
}

bool Game::saveAccountStorageValues() const
{
	DBTransaction transaction;
	Database& db = Database::getInstance();

	if (!transaction.begin()) {
		return false;
	}

	if (!db.executeQuery("DELETE FROM `account_storage`")) {
		return false;
	}

	for (const auto& accountIt : g_game.accountStorageMap) {
		if (accountIt.second.empty()) {
			break;
		}

		DBInsert accountStorageQuery("INSERT INTO `account_storage` (`account_id`, `key`, `value`) VALUES");
		for (const auto& storageIt : accountIt.second) {
			if (!accountStorageQuery.addRow(fmt::format("{:d}, {:d}, {:d}", accountIt.first, storageIt.first, storageIt.second))) {
				return false;
			}
		}

		if (!accountStorageQuery.execute()) {
			return false;
		}
	}

	return transaction.commit();
}

void Game::loadBestiaryMonsters()
{
	bestiaryMonsters.clear();

	Database& db = Database::getInstance();
	DBResult_ptr result;
	if (!(result = db.storeQuery("SELECT `creature_id`, `name`, `kills_stage_1`, `kills_stage_2`, `kills_stage_3`, `charm_points`, `enabled` FROM `bestiary_monsters`"))) {
		return;
	}

	do {
		std::string monsterName = asLowerCaseString(result->getString("name"));
		trimString(monsterName);
		if (monsterName.empty()) {
			continue;
		}

		BestiaryMonsterEntry entry;
		entry.creatureId = result->getNumber<uint16_t>("creature_id");
		entry.killsStage1 = result->getNumber<uint16_t>("kills_stage_1");
		entry.killsStage2 = result->getNumber<uint16_t>("kills_stage_2");
		entry.killsStage3 = result->getNumber<uint16_t>("kills_stage_3");
		entry.charmPoints = result->getNumber<uint16_t>("charm_points");
		entry.enabled = result->getNumber<uint16_t>("enabled") != 0;
		bestiaryMonsters[monsterName] = entry;
	} while (result->next());
}

void Game::recordBestiaryKill(Player& player, const Monster& monster)
{
	if (monster.isSummon()) {
		return;
	}

	std::string monsterName = asLowerCaseString(monster.getName());
	trimString(monsterName);

	const auto bestiaryIt = bestiaryMonsters.find(monsterName);
	if (bestiaryIt == bestiaryMonsters.end()) {
		return;
	}

	const BestiaryMonsterEntry& bestiaryEntry = bestiaryIt->second;
	if (!bestiaryEntry.enabled) {
		return;
	}

	Database& db = Database::getInstance();
	const uint32_t playerId = player.getGUID();
	const uint16_t creatureId = bestiaryEntry.creatureId;

	uint32_t kills = 0;
	bool hasProgress = false;

	if (DBResult_ptr result = db.storeQuery(fmt::format(
		"SELECT `kills`, `last_stage_reached` FROM `player_bestiary_progress` WHERE `player_id` = {:d} AND `creature_id` = {:d}",
		playerId, creatureId))) {
		kills = result->getNumber<uint32_t>("kills");
		hasProgress = true;
	}

	const bool isFirstKill = kills == 0;
	const uint32_t previousKills = kills;
	// Elite Creatures: elite kills count 3x/5x/10x towards the base
	// creature's bestiary progress.
	kills += EliteCreatures::bestiaryKillMultiplier(monster.getEliteTier());

	uint16_t newStageReached = 0;
	if (kills >= bestiaryEntry.killsStage3) {
		newStageReached = 3;
	} else if (kills >= bestiaryEntry.killsStage2) {
		newStageReached = 2;
	} else if (kills >= bestiaryEntry.killsStage1) {
		newStageReached = 1;
	}

	const int64_t timestamp = OTSYS_TIME();
	bool persisted = false;
	if (hasProgress) {
		persisted = db.executeQuery(fmt::format(
			"UPDATE `player_bestiary_progress` SET `kills` = {:d}, `last_stage_reached` = {:d}, `updated_at` = {:d} WHERE `player_id` = {:d} AND `creature_id` = {:d}",
			kills, newStageReached, timestamp, playerId, creatureId));
	} else {
		persisted = db.executeQuery(fmt::format(
			"INSERT INTO `player_bestiary_progress` (`player_id`, `creature_id`, `kills`, `last_stage_reached`, `created_at`, `updated_at`) VALUES ({:d}, {:d}, {:d}, {:d}, {:d}, {:d})",
			playerId, creatureId, kills, newStageReached, timestamp, timestamp));
	}

	if (persisted) {
		// Multi-kill increments can skip over a stage threshold, so detect
		// completion by crossing it instead of landing exactly on it.
		uint16_t completedStage = 0;
		if (previousKills < bestiaryEntry.killsStage3 && kills >= bestiaryEntry.killsStage3) {
			completedStage = 3;
		} else if (previousKills < bestiaryEntry.killsStage2 && kills >= bestiaryEntry.killsStage2) {
			completedStage = 2;
		} else if (previousKills < bestiaryEntry.killsStage1 && kills >= bestiaryEntry.killsStage1) {
			completedStage = 1;
		}

		if (!isFirstKill && completedStage == 0) {
			return;
		}

		const Outfit_t outfit = monster.getDefaultOutfit();
		const auto sendBestiaryBanner = [&](uint16_t stage) {
			player.sendExtendedOpcode(BESTIARY_UNLOCK_EXTENDED_OPCODE,
				fmt::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
					stage, creatureId, outfit.lookType, outfit.lookTypeEx, outfit.lookMount,
					static_cast<uint16_t>(outfit.lookHead), static_cast<uint16_t>(outfit.lookBody),
					static_cast<uint16_t>(outfit.lookLegs), static_cast<uint16_t>(outfit.lookFeet),
					static_cast<uint16_t>(outfit.lookAddons), monster.getName()));
		};

		if (isFirstKill) {
			sendBestiaryBanner(0);
		}
		if (completedStage != 0) {
			sendBestiaryBanner(completedStage);
		}
	}
}

uint32_t Game::getBestiaryCharmPoints(uint32_t playerId) const
{
	DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
		"SELECT COALESCE(SUM(bm.`charm_points`), 0) AS `total` "
		"FROM `bestiary_monsters` bm "
		"INNER JOIN `player_bestiary_progress` pbp ON pbp.`creature_id` = bm.`creature_id` "
		"WHERE pbp.`player_id` = {:d} AND bm.`enabled` = 1 AND pbp.`kills` >= bm.`kills_stage_3`",
		playerId));
	return result ? result->getNumber<uint32_t>("total") : 0;
}

void Game::playerUnlockCharm(uint32_t playerId, uint8_t charmId)
{
	Player* player = getPlayerByID(playerId);
	const CharmDefinition* definition = getCharmDefinition(charmId);
	if (!player || !definition) {
		return;
	}

	player->loadCharmStatesFromDatabase();
	if (player->getCharmState(charmId) != PlayerCharmState::LOCKED) {
		player->sendCancelMessage("This charm is already unlocked.");
		player->sendCharmData();
		return;
	}

	const uint32_t earnedPoints = getBestiaryCharmPoints(player->getGUID());
	const uint32_t spentPoints = player->getSpentCharmPoints();
	const uint32_t availablePoints = earnedPoints > spentPoints ? earnedPoints - spentPoints : 0;
	if (availablePoints < definition->unlockCost) {
		player->sendCancelMessage("You do not have enough Charm Points.");
		player->sendCharmData();
		return;
	}

	if (!player->unlockCharm(charmId)) {
		player->sendCancelMessage("The charm could not be unlocked.");
		return;
	}

	player->sendTextMessage(MESSAGE_EVENT_ADVANCE,
		fmt::format("{:s} has been unlocked. Visit a charm master to activate it.", definition->name));
}

void Game::spawnEliteCreature(const Position& pos, const std::string& mTypeName, uint8_t eliteTierValue)
{
	const EliteTier tier = static_cast<EliteTier>(eliteTierValue);
	if (tier == EliteTier::None) {
		return;
	}

	// Remove the portal item (if it still exists) before placing the elite.
	if (Tile* tile = map.getTile(pos)) {
		if (TileItemVector* items = tile->getItemList()) {
			for (Item* tileItem : *items) {
				if (tileItem && tileItem->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_ELITE_PORTAL)) {
					internalRemoveItem(tileItem);
				}
			}
		}
	}

	Monster* eliteMonster = Monster::createMonster(mTypeName);
	if (!eliteMonster) {
		return;
	}

	eliteMonster->setEliteTier(tier);

	if (!g_events->eventMonsterOnSpawn(eliteMonster, pos, false, true)) {
		delete eliteMonster;
		return;
	}

	// Prefer the exact corpse tile; fall back to a neighbor tile and then
	// force the corpse tile, mirroring the regular spawn placement path.
	if (!placeCreature(eliteMonster, pos, false, false) && !placeCreature(eliteMonster, pos, false, true)) {
		delete eliteMonster;
		return;
	}

	eliteMonster->setDirection(DIRECTION_SOUTH);
}

void Game::cleanupElitePortal(const Position& pos)
{
	Tile* tile = map.getTile(pos);
	if (!tile) {
		return;
	}

	TileItemVector* items = tile->getItemList();
	if (!items) {
		return;
	}

	for (Item* tileItem : *items) {
		if (tileItem && tileItem->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_ELITE_PORTAL)) {
			internalRemoveItem(tileItem);
		}
	}
}

void Game::despawnEliteCreature(uint32_t creatureId)
{
	Creature* creature = getCreatureByID(creatureId);
	if (!creature) {
		return;
	}

	Monster* monster = creature->getMonster();
	if (!monster || !monster->isElite()) {
		return;
	}

	removeCreature(monster, false);
}

void Game::startDecay(Item* item)
{
	if (!item || !item->canDecay()) {
		return;
	}

	ItemDecayState_t decayState = item->getDecaying();
	if (decayState == DECAYING_TRUE) {
		return;
	}

	if (emergencyActive) {
		item->incrementReferenceCounter();
		item->setDecaying(DECAYING_TRUE);
		if (emergencyHeldDecaySet.emplace(item).second) {
			emergencyHeldDecayItems.push_back(item);
		} else {
			ReleaseItem(item);
		}
		return;
	}

	if (item->getDuration() > 0) {
		item->incrementReferenceCounter();
		item->setDecaying(DECAYING_TRUE);
		toDecayItems.push_front(item);
	} else {
		internalDecayItem(item);
	}
}

void Game::internalDecayItem(Item* item)
{
	const ItemType& it = Item::items[item->getID()];
	if (it.decayTo != 0) {
		const bool preserveDeathBundle = item->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_FLOOR_DEATH_BUNDLE) != nullptr;
		const bool preservePlayerCorpse = item->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_FLOOR_PLAYER_CORPSE) != nullptr;
		const bool preserveCreatureCorpse = item->isFloorPersistenceCreatureCorpse();
		const std::string preservedPlayerCorpseInstanceId =
			preservePlayerCorpse ? item->getFloorPersistenceInstanceId() : std::string();
		Item* newItem = transformItem(item, item->getDecayTo());
		if (preserveDeathBundle && newItem && !newItem->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_FLOOR_DEATH_BUNDLE)) {
			std::string deathBundleAttribute = ITEM_CUSTOM_ATTRIBUTE_FLOOR_DEATH_BUNDLE;
			newItem->setCustomAttribute(deathBundleAttribute, true);
		}
		if (preservePlayerCorpse && newItem) {
			if (!newItem->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_FLOOR_PLAYER_CORPSE)) {
				std::string playerCorpseAttribute = ITEM_CUSTOM_ATTRIBUTE_FLOOR_PLAYER_CORPSE;
				newItem->setCustomAttribute(playerCorpseAttribute, true);
			}
			if (!preservedPlayerCorpseInstanceId.empty() &&
			    newItem->getFloorPersistenceInstanceId() != preservedPlayerCorpseInstanceId) {
				std::string instanceIdAttribute = ITEM_CUSTOM_ATTRIBUTE_FLOOR_INSTANCE_ID;
				newItem->setCustomAttribute(instanceIdAttribute, preservedPlayerCorpseInstanceId);
			}
		}
		if (preserveCreatureCorpse && newItem &&
		    !newItem->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_FLOOR_CREATURE_CORPSE)) {
			std::string creatureCorpseAttribute = ITEM_CUSTOM_ATTRIBUTE_FLOOR_CREATURE_CORPSE;
			newItem->setCustomAttribute(creatureCorpseAttribute, true);
		}
		if (preservePlayerCorpse && newItem) {
			if (Tile* corpseTile = newItem->getTile()) {
				markFloorTileDirty(*corpseTile, FLOOR_DIRTY_ITEM_REPLACE, FLOOR_DIRTY_ORIGIN_EXPLICIT);
			}
		}
		startDecay(newItem);
	} else {
		const bool removePlayerCorpse =
			item->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_FLOOR_PLAYER_CORPSE) != nullptr;
		Tile* corpseTile = removePlayerCorpse ? item->getTile() : nullptr;
		ReturnValue ret = internalRemoveItem(item);
		if (ret == RETURNVALUE_NOERROR && corpseTile) {
			markFloorTileDirty(*corpseTile, FLOOR_DIRTY_ITEM_REMOVE, FLOOR_DIRTY_ORIGIN_EXPLICIT);
		}
		if (ret != RETURNVALUE_NOERROR) {
			std::cout << "[Debug - Game::internalDecayItem] internalDecayItem failed, error code: " << static_cast<uint32_t>(ret) << ", item id: " << item->getID() << std::endl;
		}
	}
}

void Game::checkDecay()
{
	g_scheduler.addEvent(createSchedulerTask(EVENT_DECAYINTERVAL, std::bind(&Game::checkDecay, this)));

	size_t bucket = (lastBucket + 1) % EVENT_DECAY_BUCKETS;

	auto it = decayItems[bucket].begin(), end = decayItems[bucket].end();
	while (it != end) {
		Item* item = *it;
		if (!item->canDecay()) {
			item->setDecaying(DECAYING_FALSE);
			ReleaseItem(item);
			it = decayItems[bucket].erase(it);
			continue;
		}

		int32_t duration = item->getDuration();
		int32_t decreaseTime = std::min<int32_t>(EVENT_DECAYINTERVAL * EVENT_DECAY_BUCKETS, duration);

		duration -= decreaseTime;
		item->decreaseDuration(decreaseTime);

		if (duration <= 0) {
			it = decayItems[bucket].erase(it);
			internalDecayItem(item);
			ReleaseItem(item);
		} else if (duration < EVENT_DECAYINTERVAL * EVENT_DECAY_BUCKETS) {
			it = decayItems[bucket].erase(it);
			size_t newBucket = (bucket + ((duration + EVENT_DECAYINTERVAL / 2) / 1000)) % EVENT_DECAY_BUCKETS;
			if (newBucket == bucket) {
				internalDecayItem(item);
				ReleaseItem(item);
			} else {
				decayItems[newBucket].push_back(item);
			}
		} else {
			++it;
		}
	}

	lastBucket = bucket;
	cleanup();
}

void Game::checkLight()
{
	g_scheduler.addEvent(createSchedulerTask(EVENT_LIGHTINTERVAL, std::bind(&Game::checkLight, this)));
	uint8_t previousLightLevel = lightLevel;
	updateWorldLightLevel();
	
	if (previousLightLevel != lightLevel) {
		LightInfo lightInfo = getWorldLightInfo();

		for (const auto& it : players) {
			it.second->sendWorldLight(lightInfo);
		}
	}
}

void Game::updateWorldLightLevel()
{
	if (getWorldTime() >= GAME_SUNRISE && getWorldTime() <= GAME_DAYTIME) {
		lightLevel = ((GAME_DAYTIME - GAME_SUNRISE) - (GAME_DAYTIME - getWorldTime())) * float(LIGHT_CHANGE_SUNRISE) + LIGHT_NIGHT;
	} else if (getWorldTime() >= GAME_SUNSET && getWorldTime() <= GAME_NIGHTTIME) {
		lightLevel = LIGHT_DAY - ((getWorldTime() - GAME_SUNSET) * float(LIGHT_CHANGE_SUNSET));
	} else if (getWorldTime() >= GAME_NIGHTTIME || getWorldTime() < GAME_SUNRISE) {
		lightLevel = LIGHT_NIGHT;
	} else {
		lightLevel = LIGHT_DAY;
	}
}

void Game::updateWorldTime()
{
	g_scheduler.addEvent(createSchedulerTask(EVENT_WORLDTIMEINTERVAL, std::bind(&Game::updateWorldTime, this)));
	time_t osTime = time(nullptr);
	tm* timeInfo = localtime(&osTime);
	worldTime = (timeInfo->tm_sec + (timeInfo->tm_min * 60)) / 2.5f;
}

void Game::shutdown()
{
	std::cout << "Shutting down..." << std::flush;

	g_playerIOManager.shutdown(true);
	// Commit every captured background checkpoint before tearing the database
	// down, then stop accepting new work.
	if (!drainCheckpointWorker(CHECKPOINT_SYNC_DRAIN_TIMEOUT_MS)) {
		std::cout << "[Warning - Game::shutdown] some background checkpoints did not "
		          << "commit before shutdown; crash recovery will restore the last "
		          << "committed state." << std::endl;
	}
	g_checkpointWorker.shutdown();
	g_scheduler.shutdown();
	g_databaseTasks.shutdown();
	g_dispatcher.shutdown();
	map.spawns.clear();
	raids.clear();

	cleanup();

	if (serviceManager) {
		serviceManager->stop();
	}

	ConnectionManager::getInstance().closeAll();

	std::cout << " done!" << std::endl;
}

void Game::cleanup()
{
	//free memory
	for (auto creature : ToReleaseCreatures) {
		creature->decrementReferenceCounter();
	}
	ToReleaseCreatures.clear();

	for (auto item : ToReleaseItems) {
		item->decrementReferenceCounter();
	}
	ToReleaseItems.clear();

	for (Item* item : toDecayItems) {
		const uint32_t dur = item->getDuration();
		if (dur >= EVENT_DECAYINTERVAL * EVENT_DECAY_BUCKETS) {
			decayItems[lastBucket].push_back(item);
		} else {
			decayItems[(lastBucket + 1 + dur / 1000) % EVENT_DECAY_BUCKETS].push_back(item);
		}
	}
	toDecayItems.clear();
}

void Game::ReleaseCreature(Creature* creature)
{
	ToReleaseCreatures.push_back(creature);
}

void Game::ReleaseItem(Item* item)
{
	ToReleaseItems.push_back(item);
}

void Game::broadcastMessage(const std::string& text, MessageClasses type) const
{
	std::cout << "> Broadcasted message: \"" << text << "\"." << std::endl;
	for (const auto& it : players) {
		it.second->sendTextMessage(type, text);
	}
}

void Game::updateCreatureWalkthrough(const Creature* creature)
{
	//send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		Player* tmpPlayer = spectator->getPlayer();
		tmpPlayer->sendCreatureWalkthrough(creature, tmpPlayer->canWalkthroughEx(creature));
	}
}

void Game::updateCreatureSkull(const Creature* creature)
{
	if (getWorldType() != WORLD_TYPE_PVP) {
		return;
	}

	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		spectator->getPlayer()->sendCreatureSkull(creature);
	}
}

void Game::updatePlayerShield(Player* player)
{
	SpectatorVec spectators;
	map.getSpectators(spectators, player->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		spectator->getPlayer()->sendCreatureShield(player);
	}
}

void Game::updatePlayerHelpers(const Player& player)
{
	uint32_t creatureId = player.getID();
	uint16_t helpers = player.getHelpers();

	SpectatorVec spectators;
	map.getSpectators(spectators, player.getPosition(), true, true);
	for (Creature* spectator : spectators) {
		spectator->getPlayer()->sendCreatureHelpers(creatureId, helpers);
	}
}

void Game::updateCreatureType(Creature* creature)
{
	const Player* masterPlayer = nullptr;

	uint32_t creatureId = creature->getID();
	CreatureType_t creatureType = creature->getType();
	if (creatureType == CREATURETYPE_MONSTER) {
		const Creature* master = creature->getMaster();
		if (master) {
			masterPlayer = master->getPlayer();
			if (masterPlayer) {
				creatureType = CREATURETYPE_SUMMON_OTHERS;
			}
		}
	}

	//send to clients
	SpectatorVec spectators;
	map.getSpectators(spectators, creature->getPosition(), true, true);

	if (creatureType == CREATURETYPE_SUMMON_OTHERS) {
		for (Creature* spectator : spectators) {
			Player* player = spectator->getPlayer();
			if (masterPlayer == player) {
				player->sendCreatureType(creatureId, CREATURETYPE_SUMMON_OWN);
			} else {
				player->sendCreatureType(creatureId, creatureType);
			}
		}
	} else {
		for (Creature* spectator : spectators) {
			spectator->getPlayer()->sendCreatureType(creatureId, creatureType);
		}
	}
}

void Game::loadMotdNum()
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery("SELECT `value` FROM `server_config` WHERE `config` = 'motd_num'");
	if (result) {
		motdNum = result->getNumber<uint32_t>("value");
	} else {
		db.executeQuery("INSERT INTO `server_config` (`config`, `value`) VALUES ('motd_num', '0')");
	}

	result = db.storeQuery("SELECT `value` FROM `server_config` WHERE `config` = 'motd_hash'");
	if (result) {
		motdHash = result->getString("value");
		if (motdHash != transformToSHA1(g_config.getString(ConfigManager::MOTD))) {
			++motdNum;
		}
	} else {
		db.executeQuery("INSERT INTO `server_config` (`config`, `value`) VALUES ('motd_hash', '')");
	}
}

void Game::saveMotdNum() const
{
	Database& db = Database::getInstance();
	db.executeQuery(fmt::format("UPDATE `server_config` SET `value` = '{:d}' WHERE `config` = 'motd_num'", motdNum));
	db.executeQuery(fmt::format("UPDATE `server_config` SET `value` = '{:s}' WHERE `config` = 'motd_hash'", transformToSHA1(g_config.getString(ConfigManager::MOTD))));
}

void Game::checkPlayersRecord()
{
	const size_t playersOnline = getPlayersOnline();
	if (playersOnline > playersRecord) {
		uint32_t previousRecord = playersRecord;
		playersRecord = playersOnline;

		for (auto& it : g_globalEvents->getEventMap(GLOBALEVENT_RECORD)) {
			it.second.executeRecord(playersRecord, previousRecord);
		}
		updatePlayersRecord();
	}
}

void Game::updatePlayersRecord() const
{
	Database& db = Database::getInstance();
	db.executeQuery(fmt::format("UPDATE `server_config` SET `value` = '{:d}' WHERE `config` = 'players_record'", playersRecord));
}

void Game::loadPlayersRecord()
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery("SELECT `value` FROM `server_config` WHERE `config` = 'players_record'");
	if (result) {
		playersRecord = result->getNumber<uint32_t>("value");
	} else {
		db.executeQuery("INSERT INTO `server_config` (`config`, `value`) VALUES ('players_record', '0')");
	}
}

void Game::playerInviteToParty(uint32_t playerId, uint32_t invitedId)
{
	if (playerId == invitedId) {
		return;
	}

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Player* invitedPlayer = getPlayerByID(invitedId);
	if (!invitedPlayer || invitedPlayer->isInviting(player)) {
		return;
	}

	if (invitedPlayer->getParty()) {
		player->sendTextMessage(MESSAGE_INFO_DESCR, fmt::format("{:s} is already in a party.", invitedPlayer->getName()));
		return;
	}

	Party* party = player->getParty();
	if (!party) {
		party = new Party(player);
	} else if (party->getLeader() != player) {
		return;
	}

	party->invitePlayer(*invitedPlayer);
}

void Game::playerJoinParty(uint32_t playerId, uint32_t leaderId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Player* leader = getPlayerByID(leaderId);
	if (!leader || !leader->isInviting(player)) {
		return;
	}

	Party* party = leader->getParty();
	if (!party || party->getLeader() != leader) {
		return;
	}

	if (player->getParty()) {
		player->sendTextMessage(MESSAGE_INFO_DESCR, "You are already in a party.");
		return;
	}

	party->joinParty(*player);
}

void Game::playerRevokePartyInvitation(uint32_t playerId, uint32_t invitedId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Party* party = player->getParty();
	if (!party || party->getLeader() != player) {
		return;
	}

	Player* invitedPlayer = getPlayerByID(invitedId);
	if (!invitedPlayer || !player->isInviting(invitedPlayer)) {
		return;
	}

	party->revokeInvitation(*invitedPlayer);
}

void Game::playerPassPartyLeadership(uint32_t playerId, uint32_t newLeaderId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Party* party = player->getParty();
	if (!party || party->getLeader() != player) {
		return;
	}

	Player* newLeader = getPlayerByID(newLeaderId);
	if (!newLeader || !player->isPartner(newLeader)) {
		return;
	}

	party->passPartyLeadership(newLeader);
}

void Game::playerLeaveParty(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Party* party = player->getParty();
	if (!party || player->hasCondition(CONDITION_INFIGHT)) {
		return;
	}

	party->leaveParty(player);
}

void Game::playerEnableSharedPartyExperience(uint32_t playerId, bool sharedExpActive)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Party* party = player->getParty();
	if (!party || (player->hasCondition(CONDITION_INFIGHT) && player->getZone() != ZONE_PROTECTION)) {
		return;
	}

	party->setSharedExperience(player, sharedExpActive);
}

void Game::sendGuildMotd(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	Guild* guild = player->getGuild();
	if (guild) {
		player->sendChannelMessage("Message of the Day", guild->getMotd(), TALKTYPE_CHANNEL_R1, CHANNEL_GUILD);
	}
}

void Game::kickPlayer(uint32_t playerId, bool displayEffect)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->kickPlayer(displayEffect);
}

void Game::playerReportRuleViolation(uint32_t playerId, const std::string& targetName, uint8_t reportType, uint8_t reportReason, const std::string& comment, const std::string& translation)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	g_events->eventPlayerOnReportRuleViolation(player, targetName, reportType, reportReason, comment, translation);
}

void Game::playerReportBug(uint32_t playerId, const std::string& message, const Position& position, uint8_t category)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	g_events->eventPlayerOnReportBug(player, message, position, category);
}

void Game::playerDebugAssert(uint32_t playerId, const std::string& assertLine, const std::string& date, const std::string& description, const std::string& comment)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	// TODO: move debug assertions to database
	FILE* file = fopen("client_assertions.txt", "a");
	if (file) {
		fprintf(file, "----- %s - %s (%s) -----\n", formatDate(time(nullptr)).c_str(), player->getName().c_str(), convertIPToString(player->getIP()).c_str());
		fprintf(file, "%s\n%s\n%s\n%s\n", assertLine.c_str(), date.c_str(), description.c_str(), comment.c_str());
		fclose(file);
	}
}

/*
void Game::playerLeaveMarket(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->setInMarket(false);
}

void Game::playerBrowseMarket(uint32_t playerId, uint16_t spriteId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->isInMarket()) {
		return;
	}

	const ItemType& it = Item::items.getItemIdByClientId(spriteId);
	if (it.id == 0) {
		return;
	}

	if (it.wareId == 0) {
		return;
	}

	const MarketOfferList& buyOffers = IOMarket::getActiveOffers(MARKETACTION_BUY, it.id);
	const MarketOfferList& sellOffers = IOMarket::getActiveOffers(MARKETACTION_SELL, it.id);
	player->sendMarketBrowseItem(it.id, buyOffers, sellOffers);
	player->sendMarketDetail(it.id);
}

void Game::playerBrowseMarketOwnOffers(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->isInMarket()) {
		return;
	}

	const MarketOfferList& buyOffers = IOMarket::getOwnOffers(MARKETACTION_BUY, player->getGUID());
	const MarketOfferList& sellOffers = IOMarket::getOwnOffers(MARKETACTION_SELL, player->getGUID());
	player->sendMarketBrowseOwnOffers(buyOffers, sellOffers);
}

void Game::playerBrowseMarketOwnHistory(uint32_t playerId)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->isInMarket()) {
		return;
	}

	const HistoryMarketOfferList& buyOffers = IOMarket::getOwnHistory(MARKETACTION_BUY, player->getGUID());
	const HistoryMarketOfferList& sellOffers = IOMarket::getOwnHistory(MARKETACTION_SELL, player->getGUID());
	player->sendMarketBrowseOwnHistory(buyOffers, sellOffers);
}

void Game::playerCreateMarketOffer(uint32_t playerId, uint8_t type, uint16_t spriteId, uint16_t amount, uint32_t price, bool anonymous)
{
	if (amount == 0 || amount > 64000) {
		return;
	}

	if (price == 0 || price > 999999999) {
		return;
	}

	if (type != MARKETACTION_BUY && type != MARKETACTION_SELL) {
		return;
	}

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->isInMarket()) {
		return;
	}

	if (g_config.getBoolean(ConfigManager::MARKET_PREMIUM) && !player->isPremium()) {
		player->sendMarketLeave();
		return;
	}

	const ItemType& itt = Item::items.getItemIdByClientId(spriteId);
	if (itt.id == 0 || itt.wareId == 0) {
		return;
	}

	const ItemType& it = Item::items.getItemIdByClientId(itt.wareId);
	if (it.id == 0 || it.wareId == 0) {
		return;
	}

	if (!it.stackable && amount > 2000) {
		return;
	}

	const uint32_t maxOfferCount = g_config.getNumber(ConfigManager::MAX_MARKET_OFFERS_AT_A_TIME_PER_PLAYER);
	if (maxOfferCount != 0 && IOMarket::getPlayerOfferCount(player->getGUID()) >= maxOfferCount) {
		return;
	}

	uint64_t fee = (price / 100.) * amount;
	if (fee < 20) {
		fee = 20;
	} else if (fee > 1000) {
		fee = 1000;
	}

	if (type == MARKETACTION_SELL) {
		if (fee > (player->getMoney() + player->bankBalance)) {
			return;
		}

		DepotChest* depotChest = player->getDepotChest(player->getLastDepotId(), false);
		if (!depotChest) {
			return;
		}

		std::forward_list<Item*> itemList = getMarketItemList(it.wareId, amount, depotChest, player->getInbox());
		if (itemList.empty()) {
			return;
		}

		if (it.stackable) {
			uint16_t tmpAmount = amount;
			for (Item* item : itemList) {
				uint16_t removeCount = std::min<uint16_t>(tmpAmount, item->getItemCount());
				tmpAmount -= removeCount;
				internalRemoveItem(item, removeCount);

				if (tmpAmount == 0) {
					break;
				}
			}
		} else {
			for (Item* item : itemList) {
				internalRemoveItem(item);
			}
		}

		const auto debitCash = std::min(player->getMoney(), fee);
		const auto debitBank = fee - debitCash;
		removeMoney(player, debitCash);
		player->bankBalance -= debitBank;
	} else {
		uint64_t totalPrice = static_cast<uint64_t>(price) * amount;
		totalPrice += fee;
		if (totalPrice > (player->getMoney() + player->bankBalance)) {
			return;
		}

		const auto debitCash = std::min(player->getMoney(), totalPrice);
		const auto debitBank = totalPrice - debitCash;
		removeMoney(player, debitCash);
		player->bankBalance -= debitBank;
	}

	IOMarket::createOffer(player->getGUID(), static_cast<MarketAction_t>(type), it.id, amount, price, anonymous);

	player->sendMarketEnter(player->getLastDepotId());
	const MarketOfferList& buyOffers = IOMarket::getActiveOffers(MARKETACTION_BUY, it.id);
	const MarketOfferList& sellOffers = IOMarket::getActiveOffers(MARKETACTION_SELL, it.id);
	player->sendMarketBrowseItem(it.id, buyOffers, sellOffers);
}

void Game::playerCancelMarketOffer(uint32_t playerId, uint32_t timestamp, uint16_t counter)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->isInMarket()) {
		return;
	}

	MarketOfferEx offer = IOMarket::getOfferByCounter(timestamp, counter);
	if (offer.id == 0 || offer.playerId != player->getGUID()) {
		return;
	}

	if (offer.type == MARKETACTION_BUY) {
		player->bankBalance += static_cast<uint64_t>(offer.price) * offer.amount;
		player->sendMarketEnter(player->getLastDepotId());
	} else {
		const ItemType& it = Item::items[offer.itemId];
		if (it.id == 0) {
			return;
		}

		if (it.stackable) {
			uint16_t tmpAmount = offer.amount;
			while (tmpAmount > 0) {
				int32_t stackCount = std::min<int32_t>(100, tmpAmount);
				Item* item = Item::CreateItem(it.id, stackCount);
				if (internalAddItem(player->getInbox(), item, INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
					delete item;
					break;
				}

				tmpAmount -= stackCount;
			}
		} else {
			int32_t subType;
			if (it.charges != 0) {
				subType = it.charges;
			} else {
				subType = -1;
			}

			for (uint16_t i = 0; i < offer.amount; ++i) {
				Item* item = Item::CreateItem(it.id, subType);
				if (internalAddItem(player->getInbox(), item, INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
					delete item;
					break;
				}
			}
		}
	}

	IOMarket::moveOfferToHistory(offer.id, OFFERSTATE_CANCELLED);
	offer.amount = 0;
	offer.timestamp += g_config.getNumber(ConfigManager::MARKET_OFFER_DURATION);
	player->sendMarketCancelOffer(offer);
	player->sendMarketEnter(player->getLastDepotId());
}

void Game::playerAcceptMarketOffer(uint32_t playerId, uint32_t timestamp, uint16_t counter, uint16_t amount)
{
	if (amount == 0 || amount > 64000) {
		return;
	}

	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->isInMarket()) {
		return;
	}

	MarketOfferEx offer = IOMarket::getOfferByCounter(timestamp, counter);
	if (offer.id == 0) {
		return;
	}

	uint32_t offerAccountId = IOLoginData::getAccountIdByPlayerId(offer.playerId);
	if (offerAccountId == player->getAccount()) {
		return;
	}

	if (amount > offer.amount) {
		return;
	}

	const ItemType& it = Item::items[offer.itemId];
	if (it.id == 0) {
		return;
	}

	uint64_t totalPrice = static_cast<uint64_t>(offer.price) * amount;

	if (offer.type == MARKETACTION_BUY) {
		DepotChest* depotChest = player->getDepotChest(player->getLastDepotId(), false);
		if (!depotChest) {
			return;
		}

		std::forward_list<Item*> itemList = getMarketItemList(it.wareId, amount, depotChest, player->getInbox());
		if (itemList.empty()) {
			return;
		}

		Player* buyerPlayer = getPlayerByGUID(offer.playerId);
		if (!buyerPlayer) {
			buyerPlayer = new Player(nullptr);
			if (!IOLoginData::loadPlayerById(buyerPlayer, offer.playerId)) {
				delete buyerPlayer;
				return;
			}
		}

		if (it.stackable) {
			uint16_t tmpAmount = amount;
			for (Item* item : itemList) {
				uint16_t removeCount = std::min<uint16_t>(tmpAmount, item->getItemCount());
				tmpAmount -= removeCount;
				internalRemoveItem(item, removeCount);

				if (tmpAmount == 0) {
					break;
				}
			}
		} else {
			for (Item* item : itemList) {
				internalRemoveItem(item);
			}
		}

		player->bankBalance += totalPrice;

		if (it.stackable) {
			uint16_t tmpAmount = amount;
			while (tmpAmount > 0) {
				uint16_t stackCount = std::min<uint16_t>(100, tmpAmount);
				Item* item = Item::CreateItem(it.id, stackCount);
				if (internalAddItem(buyerPlayer->getInbox(), item, INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
					delete item;
					break;
				}

				tmpAmount -= stackCount;
			}
		} else {
			int32_t subType;
			if (it.charges != 0) {
				subType = it.charges;
			} else {
				subType = -1;
			}

			for (uint16_t i = 0; i < amount; ++i) {
				Item* item = Item::CreateItem(it.id, subType);
				if (internalAddItem(buyerPlayer->getInbox(), item, INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
					delete item;
					break;
				}
			}
		}

		if (buyerPlayer->isOffline()) {
			IOLoginData::savePlayer(buyerPlayer);
			delete buyerPlayer;
		} else {
			buyerPlayer->onReceiveMail();
		}
	} else {
		if (totalPrice > (player->getMoney() + player->bankBalance)) {
			return;
		}

		const auto debitCash = std::min(player->getMoney(), totalPrice);
		const auto debitBank = totalPrice - debitCash;
		removeMoney(player, debitCash);
		player->bankBalance -= debitBank;

		if (it.stackable) {
			uint16_t tmpAmount = amount;
			while (tmpAmount > 0) {
				uint16_t stackCount = std::min<uint16_t>(100, tmpAmount);
				Item* item = Item::CreateItem(it.id, stackCount);
				if (internalAddItem(player->getInbox(), item, INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
					delete item;
					break;
				}

				tmpAmount -= stackCount;
			}
		} else {
			int32_t subType;
			if (it.charges != 0) {
				subType = it.charges;
			} else {
				subType = -1;
			}

			for (uint16_t i = 0; i < amount; ++i) {
				Item* item = Item::CreateItem(it.id, subType);
				if (internalAddItem(player->getInbox(), item, INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
					delete item;
					break;
				}
			}
		}

		Player* sellerPlayer = getPlayerByGUID(offer.playerId);
		if (sellerPlayer) {
			sellerPlayer->bankBalance += totalPrice;
		} else {
			IOLoginData::increaseBankBalance(offer.playerId, totalPrice);
		}

		player->onReceiveMail();
	}

	const int32_t marketOfferDuration = g_config.getNumber(ConfigManager::MARKET_OFFER_DURATION);

	IOMarket::appendHistory(player->getGUID(), (offer.type == MARKETACTION_BUY ? MARKETACTION_SELL : MARKETACTION_BUY), offer.itemId, amount, offer.price, offer.timestamp + marketOfferDuration, OFFERSTATE_ACCEPTEDEX);

	IOMarket::appendHistory(offer.playerId, offer.type, offer.itemId, amount, offer.price, offer.timestamp + marketOfferDuration, OFFERSTATE_ACCEPTED);

	offer.amount -= amount;

	if (offer.amount == 0) {
		IOMarket::deleteOffer(offer.id);
	} else {
		IOMarket::acceptOffer(offer.id, amount);
	}

	player->sendMarketEnter(player->getLastDepotId());
	offer.timestamp += marketOfferDuration;
	player->sendMarketAcceptOffer(offer);
}*/

void Game::parsePlayerExtendedOpcode(uint32_t playerId, uint8_t opcode, const std::string& buffer)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (opcode == PLAYER_SHOP_OPCODE) {
		g_playerShop.handleOpcode(player, buffer);
		return;
	}

	for (CreatureEvent* creatureEvent : player->getCreatureEvents(CREATURE_EVENT_EXTENDED_OPCODE)) {
		creatureEvent->executeExtendedOpcode(player, opcode, buffer);
	}
}

/*
std::forward_list<Item*> Game::getMarketItemList(uint16_t wareId, uint16_t sufficientCount, DepotChest* depotChest, Inbox* inbox)
{
	std::forward_list<Item*> itemList;
	uint16_t count = 0;

	std::list<Container*> containers { depotChest, inbox };
	do {
		Container* container = containers.front();
		containers.pop_front();

		for (Item* item : container->getItemList()) {
			Container* c = item->getContainer();
			if (c && !c->empty()) {
				containers.push_back(c);
				continue;
			}

			const ItemType& itemType = Item::items[item->getID()];
			if (itemType.wareId != wareId) {
				continue;
			}

			if (c && (!itemType.isContainer() || c->capacity() != itemType.maxItems)) {
				continue;
			}

			if (!item->hasMarketAttributes()) {
				continue;
			}

			itemList.push_front(item);

			count += Item::countByType(item, -1);
			if (count >= sufficientCount) {
				return itemList;
			}
		}
	} while (!containers.empty());
	return std::forward_list<Item*>();
}
*/

void Game::forceAddCondition(uint32_t creatureId, Condition* condition)
{
	Creature* creature = getCreatureByID(creatureId);
	if (!creature) {
		delete condition;
		return;
	}

	creature->addCondition(condition, true);
}

void Game::forceRemoveCondition(uint32_t creatureId, ConditionType_t type)
{
	Creature* creature = getCreatureByID(creatureId);
	if (!creature) {
		return;
	}

	creature->removeCondition(type, true);
}

/*void Game::sendOfflineTrainingDialog(Player* player)
{
	if (!player) {
		return;
	}

	if (!player->hasModalWindowOpen(offlineTrainingWindow.id)) {
		player->sendModalWindow(offlineTrainingWindow);
	}
}
*/

void Game::playerAnswerModalWindow(uint32_t playerId, uint32_t modalWindowId, uint8_t button, uint8_t choice)
{
	Player* player = getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->hasModalWindowOpen(modalWindowId)) {
		return;
	}

	player->onModalWindowHandled(modalWindowId);

	// offline training, hard-coded
	/*if (modalWindowId == std::numeric_limits<uint32_t>::max()) {
		if (button == offlineTrainingWindow.defaultEnterButton) {
			if (choice == SKILL_SWORD || choice == SKILL_AXE || choice == SKILL_CLUB || choice == SKILL_DISTANCE || choice == SKILL_MAGLEVEL) {
				BedItem* bedItem = player->getBedItem();
				if (bedItem && bedItem->sleep(player)) {
					player->setOfflineTrainingSkill(choice);
					return;
				}
			}
		} else {
			player->sendTextMessage(MESSAGE_EVENT_ADVANCE, "Offline training aborted.");
		}

		player->setBedItem(nullptr);
	} else {
		for (auto creatureEvent : player->getCreatureEvents(CREATURE_EVENT_MODALWINDOW)) {
			creatureEvent->executeModalWindow(player, modalWindowId, button, choice);
		}
	}
	*/

	for (auto creatureEvent : player->getCreatureEvents(CREATURE_EVENT_MODALWINDOW)) {
		creatureEvent->executeModalWindow(player, modalWindowId, button, choice);
	}
}

void Game::addPlayer(Player* player)
{
	const std::string& lowercase_name = asLowerCaseString(player->getName());
	mappedPlayerNames[lowercase_name] = player;
	mappedPlayerGuids[player->getGUID()] = player;
	wildcardTree.insert(lowercase_name);
	players[player->getID()] = player;
	updateSpawnPlayerBucket();
}

void Game::removePlayer(Player* player)
{
	const std::string& lowercase_name = asLowerCaseString(player->getName());
	mappedPlayerNames.erase(lowercase_name);
	mappedPlayerGuids.erase(player->getGUID());
	wildcardTree.remove(lowercase_name);
	players.erase(player->getID());
	updateSpawnPlayerBucket();
}

void Game::updateSpawnPlayerBucket()
{
	if (spawnPlayerBucketOverride >= 0) {
		spawnPlayerBucket = static_cast<uint16_t>(spawnPlayerBucketOverride);
		return;
	}

	spawnPlayerBucket = calculateSpawnPlayerBucket(players.size());
}

bool Game::setSpawnPlayerBucketOverride(int32_t playerBucket)
{
	if (playerBucket < 0 || playerBucket > 600 || playerBucket % 50 != 0) {
		return false;
	}

	spawnPlayerBucketOverride = static_cast<int16_t>(playerBucket);
	spawnPlayerBucket = static_cast<uint16_t>(playerBucket);
	return true;
}

void Game::clearSpawnPlayerBucketOverride()
{
	spawnPlayerBucketOverride = -1;
	updateSpawnPlayerBucket();
}

bool Game::isSpawnRateBoostActive() const
{
	return spawnRateBoostExpiresAt > static_cast<int64_t>(time(nullptr));
}

uint32_t Game::getSpawnRateBoostRemainingSeconds() const
{
	const int64_t remaining = spawnRateBoostExpiresAt - static_cast<int64_t>(time(nullptr));
	if (remaining <= 0) {
		return 0;
	}

	return static_cast<uint32_t>(std::min<int64_t>(remaining, std::numeric_limits<uint32_t>::max()));
}

bool Game::addSpawnRateBoostDuration(uint32_t durationSeconds)
{
	if (durationSeconds == 0) {
		return false;
	}

	const int64_t now = static_cast<int64_t>(time(nullptr));
	const int64_t baseTime = std::max(now, spawnRateBoostExpiresAt);
	if (baseTime > std::numeric_limits<int64_t>::max() - durationSeconds) {
		return false;
	}

	const int64_t newExpiration = baseTime + durationSeconds;
	Database& db = Database::getInstance();
	if (!db.executeQuery(fmt::format(
			"INSERT INTO `server_config` (`config`, `value`) VALUES ('spawn_rate_boost_expires_at', '{:d}') "
			"ON DUPLICATE KEY UPDATE `value` = VALUES(`value`)", newExpiration))) {
		return false;
	}

	spawnRateBoostExpiresAt = newExpiration;
	return true;
}

void Game::loadSpawnRateBoost()
{
	Database& db = Database::getInstance();
	DBResult_ptr result = db.storeQuery("SELECT `value` FROM `server_config` WHERE `config` = 'spawn_rate_boost_expires_at'");
	if (result) {
		spawnRateBoostExpiresAt = result->getNumber<int64_t>("value");
		if (!isSpawnRateBoostActive()) {
			spawnRateBoostExpiresAt = 0;
		}
		return;
	}

	db.executeQuery("INSERT INTO `server_config` (`config`, `value`) VALUES ('spawn_rate_boost_expires_at', '0')");
}

void Game::addNpc(Npc* npc)
{
	npcs[npc->getID()] = npc;
}

void Game::removeNpc(Npc* npc)
{
	npcs.erase(npc->getID());
}

void Game::addMonster(Monster* monster)
{
	monsters[monster->getID()] = monster;
}

void Game::removeMonster(Monster* monster)
{
	monsters.erase(monster->getID());
}

Guild* Game::getGuild(uint32_t id) const
{
	auto it = guilds.find(id);
	if (it == guilds.end()) {
		return nullptr;
	}
	return it->second;
}

void Game::addGuild(Guild* guild)
{
	guilds[guild->getId()] = guild;
}

void Game::removeGuild(uint32_t guildId)
{
	guilds.erase(guildId);
}

void Game::decreaseBrowseFieldRef(const Position& pos)
{
	Tile* tile = map.getTile(pos.x, pos.y, pos.z);
	if (!tile) {
		return;
	}

	auto it = browseFields.find(tile);
	if (it != browseFields.end()) {
		it->second->decrementReferenceCounter();
	}
}

void Game::internalRemoveItems(std::vector<Item*> itemList, uint32_t amount, bool stackable)
{
	if (stackable) {
		for (Item* item : itemList) {
			if (item->getItemCount() > amount) {
				internalRemoveItem(item, amount);
				break;
			} else {
				amount -= item->getItemCount();
				internalRemoveItem(item);
			}
		}
	} else {
		for (Item* item : itemList) {
			internalRemoveItem(item);
		}
	}
}

BedItem* Game::getBedBySleeper(uint32_t guid) const
{
	auto it = bedSleepersMap.find(guid);
	if (it == bedSleepersMap.end()) {
		return nullptr;
	}
	return it->second;
}

void Game::setBedSleeper(BedItem* bed, uint32_t guid)
{
	bedSleepersMap[guid] = bed;
}

void Game::removeBedSleeper(uint32_t guid)
{
	auto it = bedSleepersMap.find(guid);
	if (it != bedSleepersMap.end()) {
		bedSleepersMap.erase(it);
	}
}

Item* Game::getUniqueItem(uint16_t uniqueId)
{
	auto it = uniqueItems.find(uniqueId);
	if (it == uniqueItems.end()) {
		return nullptr;
	}
	return it->second;
}

bool Game::addUniqueItem(uint16_t uniqueId, Item* item)
{
	auto result = uniqueItems.emplace(uniqueId, item);
	if (!result.second) {
		std::cout << "Duplicate unique id: " << uniqueId << std::endl;
	}
	return result.second;
}

void Game::removeUniqueItem(uint16_t uniqueId)
{
	auto it = uniqueItems.find(uniqueId);
	if (it != uniqueItems.end()) {
		uniqueItems.erase(it);
	}
}

bool Game::reload(ReloadTypes_t reloadType)
{
	switch (reloadType) {
		case RELOAD_TYPE_ACTIONS: return g_actions->reload();
		case RELOAD_TYPE_CHAT: return g_chat->load();
		case RELOAD_TYPE_CONFIG: return g_config.reload();
		case RELOAD_TYPE_CREATURESCRIPTS: {
			g_creatureEvents->reload();
			g_creatureEvents->removeInvalidEvents();
			return true;
		}
		case RELOAD_TYPE_EVENTS: return g_events->load();
		case RELOAD_TYPE_GLOBALEVENTS: return g_globalEvents->reload();
		case RELOAD_TYPE_ITEMS: return Item::items.reload();
		case RELOAD_TYPE_MONSTERS: return g_monsters.reload();
		case RELOAD_TYPE_MOUNTS: return mounts.reload();
		case RELOAD_TYPE_MOVEMENTS: return g_moveEvents->reload();
		case RELOAD_TYPE_NPCS: {
			Npcs::reload();
			return true;
		}

		case RELOAD_TYPE_QUESTS: return quests.reload();
		case RELOAD_TYPE_RAIDS: return raids.reload() && raids.startup();

		case RELOAD_TYPE_SPELLS: {
			if (!g_spells->reload()) {
				std::cout << "[Error - Game::reload] Failed to reload spells." << std::endl;
				std::terminate();
			} else if (!g_monsters.reload()) {
				std::cout << "[Error - Game::reload] Failed to reload monsters." << std::endl;
				std::terminate();
			}
			return true;
		}

		case RELOAD_TYPE_TALKACTIONS: return g_talkActions->reload();

		case RELOAD_TYPE_WEAPONS: {
			bool results = g_weapons->reload();
			g_weapons->loadDefaults();
			return results;
		}

		case RELOAD_TYPE_SCRIPTS: {
			// commented out stuff is TODO, once we approach further in revscriptsys
			g_actions->clear(true);
			g_creatureEvents->clear(true);
			g_moveEvents->clear(true);
			g_talkActions->clear(true);
			g_globalEvents->clear(true);
			g_weapons->clear(true);
			g_weapons->loadDefaults();
			g_spells->clear(true);
			g_scripts->loadScripts("scripts", false, true);
			g_creatureEvents->removeInvalidEvents();
			/*
			Npcs::reload();
			raids.reload() && raids.startup();
			Item::items.reload();
			quests.reload();
			mounts.reload();
			g_config.reload();
			g_events->load();
			g_chat->load();
			*/
			return true;
		}

		default: {
			if (!g_spells->reload()) {
				std::cout << "[Error - Game::reload] Failed to reload spells." << std::endl;
				std::terminate();
			} else if (!g_monsters.reload()) {
				std::cout << "[Error - Game::reload] Failed to reload monsters." << std::endl;
				std::terminate();
			}

			g_actions->reload();
			g_config.reload();
			g_creatureEvents->reload();
			g_monsters.reload();
			g_moveEvents->reload();
			Npcs::reload();
			raids.reload() && raids.startup();
			g_talkActions->reload();
			Item::items.reload();
			g_weapons->reload();
			g_weapons->clear(true);
			g_weapons->loadDefaults();
			quests.reload();
			mounts.reload();
			g_globalEvents->reload();
			g_events->load();
			g_chat->load();
			g_actions->clear(true);
			g_creatureEvents->clear(true);
			g_moveEvents->clear(true);
			g_talkActions->clear(true);
			g_globalEvents->clear(true);
			g_spells->clear(true);
			g_scripts->loadScripts("scripts", false, true);
			g_creatureEvents->removeInvalidEvents();
			return true;
		}
	}
	return true;
}
