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

#ifndef FS_GAME_H_3EC96D67DD024E6093B3BAC29B7A6D7F
#define FS_GAME_H_3EC96D67DD024E6093B3BAC29B7A6D7F

#include "account.h"
#include "combat.h"
#include "groups.h"
#include "map.h"
#include "position.h"
#include "item.h"
#include "container.h"
#include "player.h"
#include "raids.h"
#include "npc.h"
#include "wildcardtree.h"
#include "quests.h"
#include "floorpersistence.h"
#include "dispatchermetrics.h"

#include <set>

class ServiceManager;
class Creature;
class Monster;
class Npc;
class CombatInfo;

struct BestiaryMonsterEntry {
	uint16_t creatureId = 0;
	uint16_t killsStage1 = 25;
	uint16_t killsStage2 = 250;
	uint16_t killsStage3 = 1000;
	uint16_t charmPoints = 0;
	bool enabled = true;
};

enum stackPosType_t {
	STACKPOS_MOVE,
	STACKPOS_LOOK,
	STACKPOS_TOPDOWN_ITEM,
	STACKPOS_USEITEM,
	STACKPOS_USETARGET,
};

enum WorldType_t {
	WORLD_TYPE_NO_PVP = 1,
	WORLD_TYPE_PVP = 2,
	WORLD_TYPE_PVP_ENFORCED = 3,
};

enum GameState_t {
	GAME_STATE_STARTUP,
	GAME_STATE_INIT,
	GAME_STATE_NORMAL,
	GAME_STATE_CLOSED,
	GAME_STATE_SHUTDOWN,
	GAME_STATE_CLOSING,
	GAME_STATE_MAINTAIN,
};

enum FloorDirtyReason_t : uint32_t {
	FLOOR_DIRTY_NONE = 0,
	FLOOR_DIRTY_ITEM_ADD = 1 << 0,
	FLOOR_DIRTY_ITEM_REMOVE = 1 << 1,
	FLOOR_DIRTY_ITEM_UPDATE = 1 << 2,
	FLOOR_DIRTY_ITEM_REPLACE = 1 << 3,
	FLOOR_DIRTY_CONTAINER_ADD = 1 << 4,
	FLOOR_DIRTY_CONTAINER_REMOVE = 1 << 5,
	FLOOR_DIRTY_CONTAINER_UPDATE = 1 << 6,
	FLOOR_DIRTY_ATTRIBUTE_UPDATE = 1 << 7,
	FLOOR_DIRTY_DEATH_BUNDLE = 1 << 8,
};

enum FloorDirtyOrigin_t : uint32_t {
	FLOOR_DIRTY_ORIGIN_SYSTEM = 0,
	FLOOR_DIRTY_ORIGIN_PLAYER_MOVE = 1 << 0,
	FLOOR_DIRTY_ORIGIN_PLAYER_DEATH = 1 << 1,
	FLOOR_DIRTY_ORIGIN_EXPLICIT = 1 << 2,
};

struct FloorDirtyTileRecord {
	uint64_t firstSequence = 0;
	uint64_t lastSequence = 0;
	uint64_t tileVersion = 0;
	uint64_t eventCount = 0;
	int64_t firstModifiedAt = 0;
	int64_t lastModifiedAt = 0;
	int64_t firstModifiedMonotonic = 0;
	int64_t lastModifiedMonotonic = 0;
	int64_t snapshotRetryNotBefore = 0;
	uint64_t snapshotVersionInFlight = 0;
	uint32_t snapshotRetryCount = 0;
	uint32_t reasonMask = FLOOR_DIRTY_NONE;
	FloorDirtyReason_t lastReason = FLOOR_DIRTY_NONE;
	uint32_t originMask = FLOOR_DIRTY_ORIGIN_SYSTEM;
	FloorDirtyOrigin_t lastOrigin = FLOOR_DIRTY_ORIGIN_SYSTEM;
	bool snapshotInFlight = false;
	std::string lastSnapshotError;
};

struct FloorSnapshotRuntimeRecord {
	uint64_t tileVersion = 0;
	uint32_t itemCount = 0;
	uint32_t topItemCount = 0;
	uint32_t serializedBytes = 0;
	uint32_t persistAlwaysCount = 0;
	uint32_t persistCleanOnlyCount = 0;
	uint32_t persistFoodCount = 0;
	uint32_t deathBundleCount = 0;
	uint32_t excludedItemCount = 0;
	uint32_t identityMissingCount = 0;
	uint32_t identityInvalidCount = 0;
	uint32_t playerCorpseCount = 0;
	int64_t persistedAt = 0;
	uint64_t serializationMicros = 0;
	std::string checksum;
};

struct FloorSnapshotStats {
	uint64_t queued = 0;
	uint64_t succeeded = 0;
	uint64_t failed = 0;
	uint64_t serializationFailed = 0;
	uint64_t staleCompletions = 0;
	uint64_t totalSerializedBytes = 0;
	uint64_t totalSerializationMicros = 0;
	uint64_t lastSerializationMicros = 0;
	uint64_t checkpointGroupsCreated = 0;
	uint64_t checkpointGroupsMerged = 0;
	uint64_t checkpointGroupsSucceeded = 0;
	uint64_t checkpointGroupsFailed = 0;
	uint64_t checkpointPlayersSaved = 0;
	uint64_t checkpointHousesSaved = 0;
	uint64_t checkpointTilesSaved = 0;
	uint64_t checkpointStuckAlerts = 0;
	int64_t lastSuccessAt = 0;
	std::string lastError;
};

struct FloorCheckpointGroup {
	uint64_t id = 0;
	uint64_t version = 0;
	std::set<Position> positions;
	std::set<uint32_t> playerGuids;
	std::set<uint32_t> houseIds;
	std::set<std::string> itemInstanceIds;
	int64_t firstModifiedMonotonic = 0;
	int64_t lastModifiedMonotonic = 0;
	int64_t retryNotBefore = 0;
	uint32_t retryCount = 0;
	std::string lastError;
	// True while a background checkpoint job captured from this group is
	// queued or executing in the checkpoint worker. New mutations must not
	// merge into an in-flight group; they form a fresh group instead.
	bool workerInFlight = false;
};

struct PendingItemActorAttribution {
	Item* root = nullptr;
	uint32_t playerGuid = 0;
	int64_t firstModifiedMonotonic = 0;
	int64_t lastModifiedMonotonic = 0;
};

struct ActiveMailTransferCheckpoint {
	Cylinder* sourceCylinder = nullptr;
	Player* sender = nullptr;
	Tile* mailboxTile = nullptr;
	int32_t sourceIndex = INDEX_WHEREEVER;
	uint16_t originalItemId = 0;
	bool active = false;
	bool committed = false;
	bool rolledBack = false;
	std::string error;
};

struct PreparedFloorSnapshot {
	Position position;
	uint64_t tileVersion = 0;
	uint32_t reasonMask = FLOOR_DIRTY_NONE;
	uint32_t originMask = FLOOR_DIRTY_ORIGIN_SYSTEM;
	bool cityCleanupFiltered = false;
	FloorSnapshotRuntimeRecord runtimeRecord;
	std::string query;
};

struct FloorSnapshotDatabaseStats {
	bool available = false;
	uint64_t rowCount = 0;
	uint64_t totalBytes = 0;
	uint64_t maxTileVersion = 0;
	int64_t lastUpdatedAt = 0;
	std::string error;
};

struct FloorSnapshotVerification {
	bool rowFound = false;
	bool storedChecksumValid = false;
	bool storedBlobValid = false;
	bool liveSnapshotValid = false;
	bool matchesLive = false;
	bool dirty = false;
	bool inFlight = false;
	uint64_t dirtyTileVersion = 0;
	uint64_t storedTileVersion = 0;
	uint32_t storedItemCount = 0;
	uint32_t storedTopItemCount = 0;
	uint32_t storedBytes = 0;
	uint32_t liveItemCount = 0;
	uint32_t liveTopItemCount = 0;
	uint32_t liveBytes = 0;
	uint16_t storedPolicyVersion = 0;
	int64_t storedUpdatedAt = 0;
	std::string storedChecksum;
	std::string liveChecksum;
	std::string error;
};

struct FloorInstanceLookup {
	bool validFormat = false;
	bool databaseAvailable = false;
	bool safeToRecreate = false;
	uint64_t databaseMatches = 0;
	uint64_t liveMatches = 0;
	std::string firstDatabaseLocation;
	std::string firstLiveLocation;
	std::string error;
};

struct FloorRecoveryPlan {
	bool evaluated = false;
	bool databaseAvailable = false;
	bool replayEnabled = false;
	bool applyEnabled = false;
	uint64_t newestSessionId = 0;
	uint64_t sourceSessionId = 0;
	uint64_t ignoredEmptySessions = 0;
	uint64_t sourcePlayerCount = 0;
	uint64_t sourceTileCount = 0;
	uint64_t sourceSessionSnapshotRows = 0;
	uint64_t sourceCheckpointCount = 0;
	uint64_t sourceCommittedCheckpointCount = 0;
	uint64_t sourceCleanCheckpointCount = 0;
	uint64_t sourceCleanCheckpointTileCount = 0;
	uint64_t snapshotRows = 0;
	uint64_t validRows = 0;
	uint64_t invalidRows = 0;
	uint64_t itemCount = 0;
	uint64_t topItemCount = 0;
	uint64_t serializedBytes = 0;
	uint64_t cityFilteredRows = 0;
	uint64_t deathBundleCount = 0;
	uint64_t playerCorpseCount = 0;
	uint64_t identityMissingCount = 0;
	uint64_t identityInvalidCount = 0;
	uint64_t formatMismatchRows = 0;
	uint64_t policyMismatchRows = 0;
	uint64_t sizeMismatchRows = 0;
	uint64_t checksumMismatchRows = 0;
	uint64_t blobInvalidRows = 0;
	uint64_t counterMismatchRows = 0;
	uint64_t identityProblemRows = 0;
	uint64_t validationMicros = 0;
	bool dryRunEvaluated = false;
	bool dryRunReady = false;
	uint64_t dryRunRows = 0;
	uint64_t dryRunItemCount = 0;
	uint64_t dryRunTopItemCount = 0;
	uint64_t dryRunRestoreItemCount = 0;
	uint64_t dryRunQuarantineItemCount = 0;
	uint64_t dryRunRejectedItemCount = 0;
	uint64_t dryRunRestoreTopItemCount = 0;
	uint64_t dryRunQuarantineTopItemCount = 0;
	uint64_t dryRunRejectedTopItemCount = 0;
	uint64_t dryRunPersistAlwaysCount = 0;
	uint64_t dryRunPersistCleanOnlyCount = 0;
	uint64_t dryRunPersistFoodCount = 0;
	uint64_t dryRunDeathBundleCount = 0;
	uint64_t dryRunContainerCount = 0;
	uint64_t dryRunIdentityCount = 0;
	uint64_t dryRunDuplicateIdentityCount = 0;
	uint64_t dryRunMaxDepth = 0;
	uint64_t dryRunMicros = 0;
	bool reconciliationEvaluated = false;
	bool reconciliationReady = false;
	uint64_t reconciliationCandidateRows = 0;
	uint64_t reconciliationDecodedRows = 0;
	uint64_t reconciliationInvalidRows = 0;
	uint64_t reconciliationFalsePositiveRows = 0;
	uint64_t playerIdentityCount = 0;
	uint64_t playerUniqueIdentityCount = 0;
	uint64_t playerDuplicateIdentityCount = 0;
	uint64_t inventoryIdentityCount = 0;
	uint64_t depotLockerIdentityCount = 0;
	uint64_t depotIdentityCount = 0;
	uint64_t inboxIdentityCount = 0;
	uint64_t storeInboxIdentityCount = 0;
	uint64_t floorOnlyIdentityCount = 0;
	uint64_t floorPlayerIdentityMatchCount = 0;
	uint64_t floorPlayerAmbiguousIdentityCount = 0;
	uint64_t reconciliationMicros = 0;
	bool quarantineEvaluated = false;
	bool quarantineReady = false;
	uint64_t quarantinePlannedRows = 0;
	uint64_t quarantineStackableItemCount = 0;
	uint64_t quarantinePlayerMatchItemCount = 0;
	uint64_t quarantineSnapshotItemCount = 0;
	uint64_t quarantineSerializedBytes = 0;
	uint64_t quarantinePersistedRows = 0;
	uint64_t quarantinePersistedStackableItems = 0;
	uint64_t quarantinePersistedPlayerMatches = 0;
	uint64_t quarantinePersistedBytes = 0;
	uint64_t quarantineMicros = 0;
	bool applyEvaluated = false;
	bool applyReady = false;
	bool applyCompleted = false;
	uint64_t applyRows = 0;
	uint64_t applyTargetTiles = 0;
	uint64_t applyPolicyRestoreItemCount = 0;
	uint64_t applyPolicyRestoreTopItemCount = 0;
	uint64_t applyRestoredItemCount = 0;
	uint64_t applyRestoredTopItemCount = 0;
	uint64_t applyQuarantineItemCount = 0;
	uint64_t applySuppressedItemCount = 0;
	uint64_t applySuppressedTopItemCount = 0;
	uint64_t applyDirectSuppressedIdentityCount = 0;
	uint64_t applyMicros = 0;
	bool confirmationEvaluated = false;
	bool confirmationReady = false;
	bool confirmationCompleted = false;
	uint64_t confirmationRecordId = 0;
	uint64_t confirmationSourceSessionId = 0;
	uint64_t confirmationApplySessionId = 0;
	uint64_t confirmationPendingQuarantineRows = 0;
	uint64_t confirmationPendingQuarantineItems = 0;
	uint64_t confirmationPendingPlayerMatches = 0;
	uint64_t confirmationMicros = 0;
	uint32_t confirmationPlayerGuid = 0;
	int64_t confirmationConfirmedAt = 0;
	int64_t sourceStartedAt = 0;
	int64_t sourceUpdatedAt = 0;
	int64_t sourceCommittedAt = 0;
	std::string mode = "NOT_EVALUATED";
	std::string sourceState = "-";
	std::string sourceError;
	std::string reason;
	std::string validationError;
	std::string dryRunError;
	std::string reconciliationError;
	std::string reconciliationFirstMatch;
	std::string quarantineError;
	std::string applyError;
	std::string confirmationPlayerName;
	std::string confirmationError;
};

static constexpr int32_t PLAYER_NAME_LENGTH = 25;

static constexpr int32_t EVENT_LIGHTINTERVAL = 10000;
static constexpr int32_t EVENT_WORLDTIMEINTERVAL = 2500;
static constexpr int32_t EVENT_DECAYINTERVAL = 250;
static constexpr int32_t EVENT_DECAY_BUCKETS = 4;

static constexpr int32_t MOVE_CREATURE_INTERVAL = 1000;
static constexpr int32_t RANGE_MOVE_CREATURE_INTERVAL = 1500;
static constexpr int32_t RANGE_MOVE_ITEM_INTERVAL = 400;
static constexpr int32_t RANGE_USE_ITEM_INTERVAL = 400;
static constexpr int32_t RANGE_USE_ITEM_EX_INTERVAL = 400;
static constexpr int32_t RANGE_USE_WITH_CREATURE_INTERVAL = 400;
static constexpr int32_t RANGE_ROTATE_ITEM_INTERVAL = 400;
static constexpr int32_t RANGE_BROWSE_FIELD_INTERVAL = 400;
static constexpr int32_t RANGE_WRAP_ITEM_INTERVAL = 400;
static constexpr int32_t RANGE_REQUEST_TRADE_INTERVAL = 400;

/**
  * Main Game class.
  * This class is responsible to control everything that happens
  */

class Game
{
	public:
		Game();
		~Game();

		// non-copyable
		Game(const Game&) = delete;
		Game& operator=(const Game&) = delete;

		void start(ServiceManager* manager);

		// Immediately apply all pending lastActorGuid subtree normalizations so a
		// save/logout commit serializes consistent GUIDs even on an early relog.
		void flushItemActorAttributions();

		void forceAddCondition(uint32_t creatureId, Condition* condition);
		void forceRemoveCondition(uint32_t creatureId, ConditionType_t type);

		bool loadMainMap(const std::string& filename);
		void loadMap(const std::string& path);

		/**
		  * Get the map size - info purpose only
		  * \param width width of the map
		  * \param height height of the map
		  */
		void getMapDimensions(uint32_t& width, uint32_t& height) const {
			width = map.width;
			height = map.height;
		}

		void setWorldType(WorldType_t type);
		WorldType_t getWorldType() const {
			return worldType;
		}

		Cylinder* internalGetCylinder(Player* player, const Position& pos) const;
		Thing* internalGetThing(Player* player, const Position& pos, int32_t index,
		                        uint32_t spriteId, stackPosType_t type) const;
		static void internalGetPosition(Item* item, Position& pos, uint8_t& stackpos);

		static std::string getTradeErrorDescription(ReturnValue ret, Item* item);

		/**
		  * Returns a creature based on the unique creature identifier
		  * \param id is the unique creature id to get a creature pointer to
		  * \returns A Creature pointer to the creature
		  */
		Creature* getCreatureByID(uint32_t id);

		/**
		  * Returns a monster based on the unique creature identifier
		  * \param id is the unique monster id to get a monster pointer to
		  * \returns A Monster pointer to the monster
		  */
		Monster* getMonsterByID(uint32_t id);

		/**
		  * Returns an npc based on the unique creature identifier
		  * \param id is the unique npc id to get a npc pointer to
		  * \returns A NPC pointer to the npc
		  */
		Npc* getNpcByID(uint32_t id);

		/**
		  * Returns a player based on the unique creature identifier
		  * \param id is the unique player id to get a player pointer to
		  * \returns A Pointer to the player
		  */
		Player* getPlayerByID(uint32_t id);

		/**
		  * Returns a creature based on a string name identifier
		  * \param s is the name identifier
		  * \returns A Pointer to the creature
		  */
		Creature* getCreatureByName(const std::string& s);

		/**
		  * Returns a npc based on a string name identifier
		  * \param s is the name identifier
		  * \returns A Pointer to the npc
		  */
		Npc* getNpcByName(const std::string& s);

		/**
		  * Returns a player based on a string name identifier
		  * \param s is the name identifier
		  * \returns A Pointer to the player
		  */
		Player* getPlayerByName(const std::string& s);

		/**
		  * Returns a player based on guid
		  * \returns A Pointer to the player
		  */
		Player* getPlayerByGUID(const uint32_t& guid);

		/**
		  * Returns a player based on a string name identifier, with support for the "~" wildcard.
		  * \param s is the name identifier, with or without wildcard
		  * \param player will point to the found player (if any)
		  * \return "RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE" or "RETURNVALUE_NAMEISTOOAMBIGIOUS"
		  */
		ReturnValue getPlayerByNameWildcard(const std::string& s, Player*& player);

		/**
		  * Returns a player based on an account number identifier
		  * \param acc is the account identifier
		  * \returns A Pointer to the player
		  */
		Player* getPlayerByAccount(uint32_t acc);

		/**
		  * Place Creature on the map without sending out events to the surrounding.
		  * \param creature Creature to place on the map
		  * \param pos The position to place the creature
		  * \param extendedPos If true, the creature will in first-hand be placed 2 tiles away
		  * \param forced If true, placing the creature will not fail because of obstacles (creatures/items)
		  */
		bool internalPlaceCreature(Creature* creature, const Position& pos, bool extendedPos = false, bool forced = false);

		/**
		  * Place Creature on the map.
		  * \param creature Creature to place on the map
		  * \param pos The position to place the creature
		  * \param extendedPos If true, the creature will in first-hand be placed 2 tiles away
		  * \param force If true, placing the creature will not fail because of obstacles (creatures/items)
		  */
		bool placeCreature(Creature* creature, const Position& pos, bool extendedPos = false, bool forced = false);

		/**
		  * Remove Creature from the map.
		  * Removes the Creature from the map
		  * \param c Creature to remove
		  */
		bool removeCreature(Creature* creature, bool isLogout = true);
		void executeDeath(uint32_t creatureId);

		void addCreatureCheck(Creature* creature);
		static void removeCreatureCheck(Creature* creature);

		size_t getPlayersOnline() const {
			return players.size();
		}
		uint16_t getSpawnPlayerBucket() const {
			return spawnPlayerBucket;
		}
		bool isSpawnPlayerBucketOverridden() const {
			return spawnPlayerBucketOverride >= 0;
		}
		bool setSpawnPlayerBucketOverride(int32_t playerBucket);
		void clearSpawnPlayerBucketOverride();
		bool isSpawnRateBoostActive() const;
		uint32_t getSpawnRateBoostRemainingSeconds() const;
		int64_t getSpawnRateBoostExpiresAt() const {
			return spawnRateBoostExpiresAt;
		}
		bool addSpawnRateBoostDuration(uint32_t durationSeconds);
		size_t getMonstersOnline() const {
			return monsters.size();
		}
		size_t getNpcsOnline() const {
			return npcs.size();
		}
		uint32_t getPlayersRecord() const {
			return playersRecord;
		}

		void setFloorDirtyTrackingEnabled(bool enabled) {
			floorDirtyTrackingEnabled = enabled;
		}
		bool isFloorDirtyTrackingEnabled() const {
			return floorDirtyTrackingEnabled;
		}
		void setFloorPersistenceCityPosition(const Position& position, bool excluded);
		void clearFloorPersistenceCityPositions();
		bool isFloorPersistenceCityPosition(const Position& position) const;
		size_t getFloorPersistenceCityPositionCount() const {
			return floorPersistenceCityPositions.size();
		}
		void beginFloorDirtyPlayerMutation() {
			++floorDirtyPlayerMutationDepth;
		}
		void endFloorDirtyPlayerMutation() {
			if (floorDirtyPlayerMutationDepth > 0) {
				--floorDirtyPlayerMutationDepth;
			}
		}
		bool isFloorDirtyPlayerMutationActive() const {
			return floorDirtyPlayerMutationDepth > 0;
		}
		void markFloorTileDirty(const Tile& tile, FloorDirtyReason_t reason,
		                        FloorDirtyOrigin_t origin = FLOOR_DIRTY_ORIGIN_SYSTEM);
		const FloorDirtyTileRecord* getFloorDirtyTile(const Position& position) const;
		const std::map<Position, FloorDirtyTileRecord>& getFloorDirtyTiles() const {
			return floorDirtyTiles;
		}
		size_t getFloorDirtyTileCount() const {
			return floorDirtyTiles.size();
		}
		uint64_t getFloorDirtySequence() const {
			return floorDirtySequence;
		}
		uint64_t getFloorDirtyTotalEvents() const {
			return floorDirtyTotalEvents;
		}
		uint64_t getFloorDirtyIgnoredSystemEvents() const {
			return floorDirtyIgnoredSystemEvents;
		}
		bool clearFloorDirtyTile(const Position& position);
		size_t clearFloorDirtyTiles();
		bool isFloorSnapshotShadowEnabled() const {
			return floorSnapshotShadowEnabled;
		}
		uint32_t getFloorSnapshotWorldId() const {
			return floorSnapshotWorldId;
		}
		uint32_t getFloorSnapshotGenerationId() const {
			return floorSnapshotGenerationId;
		}
		uint32_t getFloorSnapshotInFlightCount() const;
		const FloorSnapshotStats& getFloorSnapshotStats() const {
			return floorSnapshotStats;
		}
		size_t getFloorCheckpointGroupCount() const {
			return floorCheckpointGroups.size();
		}
		bool hasFloorCheckpointForPlayer(uint32_t playerGuid) const;
		bool hasInFlightCheckpointForPlayer(uint32_t playerGuid) const;
		bool saveFloorCheckpointForPlayer(Player* player);
		// Blocks the Dispatcher until every queued/in-flight background
		// checkpoint job has committed and its bookkeeping was applied. Used by
		// the synchronous checkpoint/save paths so they never race an in-flight
		// background job for the same players/tiles.
		bool drainCheckpointWorker(uint32_t timeoutMs);
		// Detects an unexpectedly dead checkpoint worker thread and releases any
		// reservations/groups it left behind, so nothing stays stuck forever.
		void recoverDeadCheckpointWorker();
		bool beginFloorPersistenceCleanSave(bool resetFloorSnapshots = false);
		bool activateEmergency(uint32_t activatorGuid, const std::string& activatorName);
		bool finishEmergency(uint32_t finisherGuid, const std::string& finisherName);
		bool isEmergencyActive() const {
			return emergencyActive;
		}
		void recordCleanSavePlayerResult(bool success);
		uint64_t getFloorPersistenceSessionId() const {
			return floorPersistenceSessionId;
		}
		const std::string& getFloorPersistenceSessionState() const {
			return floorPersistenceSessionState;
		}
		bool isFloorCleanSaveWindowActive() const {
			return floorCleanSaveWindowActive;
		}
		FloorSnapshotDatabaseStats getFloorSnapshotDatabaseStats() const;
		FloorSnapshotVerification verifyFloorSnapshot(const Position& position) const;
		FloorInstanceLookup inspectFloorInstanceId(const std::string& instanceId);
		const FloorRecoveryPlan& getFloorRecoveryPlan() const {
			return floorRecoveryPlan;
		}
		uint64_t getFloorRecoveryHeldDecayItemCount() const;
		uint64_t getFloorRecoveryResumedDecayItemCount() const;
		uint64_t getFloorRecoveryExtendedPlayerCorpseCount() const;
		uint64_t getFloorRecoveryRemovedHeldDecayItemCount() const;
		bool applyFloorRecovery(uint64_t expectedSourceSessionId);
		bool confirmFloorRecovery(uint64_t expectedSourceSessionId, uint32_t confirmerGuid,
		                          const std::string& confirmerName);
		bool isFloorRecoveryBlocked() const {
			return floorRecoveryPlan.evaluated &&
			       (floorRecoveryPlan.mode == "RECOVERY_BLOCKED" ||
			        (floorRecoveryPlan.dryRunEvaluated && !floorRecoveryPlan.dryRunReady));
		}
		bool isFloorCleanRestartReplayPending() const {
			return floorRecoveryPlan.evaluated &&
			       floorRecoveryPlan.mode == "CLEAN_RESTART" &&
			       !floorRecoveryAppliedThisSession;
		}
		bool isFloorRecoveryLoginRestricted() const {
			return floorRecoveryPlan.evaluated &&
			       ((floorRecoveryPlan.mode == "CRASH_RECOVERY" &&
			         !floorRecoveryConfirmedThisSession) ||
			        isFloorCleanRestartReplayPending() || isFloorRecoveryBlocked());
		}
		uint32_t flushFloorSnapshots();
		void simulateFloorSnapshotFailures(uint32_t count);
		uint32_t getFloorSnapshotSimulatedFailures() const {
			return floorSnapshotSimulatedFailures;
		}

		LightInfo getWorldLightInfo() const {
			return {lightLevel, lightColor};
		}
		void setWorldLightInfo(LightInfo lightInfo) {
			lightLevel = lightInfo.level;
			lightColor = lightInfo.color;
			for (const auto& it : players) {
				it.second->sendWorldLight(lightInfo);
			}
		}
		void updateWorldLightLevel();

		ReturnValue internalMoveCreature(Creature* creature, Direction direction, uint32_t flags = 0);
		ReturnValue internalMoveCreature(Creature& creature, Tile& toTile, uint32_t flags = 0);

		ReturnValue internalMoveItem(Cylinder* fromCylinder, Cylinder* toCylinder, int32_t index,
		                             Item* item, uint32_t count, Item** _moveItem, uint32_t flags = 0, Creature* actor = nullptr, Item* tradeItem = nullptr, const Position* fromPos = nullptr, const Position* toPos = nullptr);

		ReturnValue internalAddItem(Cylinder* toCylinder, Item* item, int32_t index = INDEX_WHEREEVER,
		                            uint32_t flags = 0, bool test = false);
		ReturnValue internalAddItem(Cylinder* toCylinder, Item* item, int32_t index,
		                            uint32_t flags, bool test, uint32_t& remainderCount,
		                            bool identifyForFloorPersistence = false,
		                            uint32_t attributionGuid = 0);
		ReturnValue internalRemoveItem(Item* item, int32_t count = -1, bool test = false, uint32_t flags = 0);

		ReturnValue internalPlayerAddItem(Player* player, Item* item, bool dropOnMap = true, slots_t slot = CONST_SLOT_WHEREEVER);
		void attributeDeliveredItem(Item* item, uint32_t recipientGuid,
		                           bool normalizeImmediately = false);
		void attributeContainerMutation(Cylinder* cylinder, uint32_t playerGuid);
		bool hasActiveMailTransferCheckpoint() const {
			return activeMailTransferCheckpoint.active;
		}
		bool commitActiveMailTransferCheckpoint(Player* recipient, Item* deliveredItem);
		bool rollbackActiveMailTransfer(Item* deliveredItem);

		/**
		  * Find an item of a certain type
		  * \param cylinder to search the item
		  * \param itemId is the item to remove
		  * \param subType is the extra type an item can have such as charges/fluidtype, default is -1
			* meaning it's not used
		  * \param depthSearch if true it will check child containers aswell
		  * \returns A pointer to the item to an item and nullptr if not found
		  */
		Item* findItemOfType(Cylinder* cylinder, uint16_t itemId,
		                     bool depthSearch = true, int32_t subType = -1) const;

		/**
		  * Remove/Add item(s) with a monetary value
		  * \param cylinder to remove the money from
		  * \param money is the amount to remove
		  * \param flags optional flags to modify the default behavior
		  * \returns true if the removal was successful
		  */
		bool removeMoney(Cylinder* cylinder, uint64_t money, uint32_t flags = 0);

		/**
		  * Add item(s) with monetary value
		  * \param cylinder which will receive money
		  * \param money the amount to give
		  * \param flags optional flags to modify default behavior
		  */
		void addMoney(Cylinder* cylinder, uint64_t money, uint32_t flags = 0);

		/**
		  * Transform one item to another type/count
		  * \param item is the item to transform
		  * \param newId is the new itemid
		  * \param newCount is the new count value, use default value (-1) to not change it
		  * \returns true if the transformation was successful
		  */
		Item* transformItem(Item* item, uint16_t newId, int32_t newCount = -1);

		/**
		  * Teleports an object to another position
		  * \param thing is the object to teleport
		  * \param newPos is the new position
		  * \param pushMove force teleport if false
		  * \param flags optional flags to modify default behavior
		  * \returns true if the teleportation was successful
		  */
		ReturnValue internalTeleport(Thing* thing, const Position& newPos, bool pushMove = true, uint32_t flags = 0);

		/**
		  * Turn a creature to a different direction.
		  * \param creature Creature to change the direction
		  * \param dir Direction to turn to
		  */
		bool internalCreatureTurn(Creature* creature, Direction dir);

		/**
		  * Creature wants to say something.
		  * \param creature Creature pointer
		  * \param type Type of message
		  * \param text The text to say
		  */
		bool internalCreatureSay(Creature* creature, SpeakClasses type, const std::string& text,
		                         bool ghostMode, SpectatorVec* spectatorsPtr = nullptr, const Position* pos = nullptr);

		void loadPlayersRecord();
		void checkPlayersRecord();

		void sendGuildMotd(uint32_t playerId);
		void kickPlayer(uint32_t playerId, bool displayEffect);
		void playerReportBug(uint32_t playerId, const std::string& message, const Position& position, uint8_t category);
		void playerDebugAssert(uint32_t playerId, const std::string& assertLine, const std::string& date, const std::string& description, const std::string& comment);
		void playerAnswerModalWindow(uint32_t playerId, uint32_t modalWindowId, uint8_t button, uint8_t choice);
		void playerReportRuleViolation(uint32_t playerId, const std::string& targetName, uint8_t reportType, uint8_t reportReason, const std::string& comment, const std::string& translation);

		bool internalStartTrade(Player* player, Player* tradePartner, Item* tradeItem);
		void internalCloseTrade(Player* player, bool sendCancel = true);
		bool playerBroadcastMessage(Player* player, const std::string& text) const;
		void broadcastMessage(const std::string& text, MessageClasses type) const;

		//Implementation of player invoked events
		void playerMoveThing(uint32_t playerId, const Position& fromPos, uint16_t spriteId, uint8_t fromStackPos,
		                     const Position& toPos, uint8_t count);
		void playerMoveCreatureByID(uint32_t playerId, uint32_t movingCreatureId, const Position& movingCreatureOrigPos, const Position& toPos);
		void playerMoveCreature(Player* player, Creature* movingCreature, const Position& movingCreatureOrigPos, Tile* toTile);
		void playerMoveItemByPlayerID(uint32_t playerId, const Position& fromPos, uint16_t spriteId, uint8_t fromStackPos, const Position& toPos, uint8_t count);
		void playerMoveItem(Player* player, const Position& fromPos,
		                    uint16_t spriteId, uint8_t fromStackPos, const Position& toPos, uint8_t count, Item* item, Cylinder* toCylinder);
		void playerEquipItem(uint32_t playerId, uint16_t spriteId);
		void playerMove(uint32_t playerId, Direction direction);
		void playerCreatePrivateChannel(uint32_t playerId);
		void playerChannelInvite(uint32_t playerId, const std::string& name);
		void playerChannelExclude(uint32_t playerId, const std::string& name);
		void playerRequestChannels(uint32_t playerId);
		void playerOpenChannel(uint32_t playerId, uint16_t channelId);
		void playerCloseChannel(uint32_t playerId, uint16_t channelId);
		void playerOpenPrivateChannel(uint32_t playerId, std::string& receiver);
		void playerCloseNpcChannel(uint32_t playerId);
		void playerReceivePing(uint32_t playerId);
		void playerReceivePingBack(uint32_t playerId);
		void playerAutoWalk(uint32_t playerId, const std::vector<Direction>& listDir);
		void playerStopAutoWalk(uint32_t playerId);
		void playerUseItemEx(uint32_t playerId, const Position& fromPos, uint8_t fromStackPos,
		                     uint16_t fromSpriteId, const Position& toPos, uint8_t toStackPos, uint16_t toSpriteId);
		void playerUseItem(uint32_t playerId, const Position& pos, uint8_t stackPos, uint8_t index, uint16_t spriteId);
		void playerUseWithCreature(uint32_t playerId, const Position& fromPos, uint8_t fromStackPos, uint32_t creatureId, uint16_t spriteId);
		void playerCloseContainer(uint32_t playerId, uint8_t cid);
		void playerMoveUpContainer(uint32_t playerId, uint8_t cid);
		void playerUpdateContainer(uint32_t playerId, uint8_t cid);
		void playerRotateItem(uint32_t playerId, const Position& pos, uint8_t stackPos, const uint16_t spriteId);
		void playerWriteItem(uint32_t playerId, uint32_t windowTextId, const std::string& text);
		void playerBrowseField(uint32_t playerId, const Position& pos);
		void playerSeekInContainer(uint32_t playerId, uint8_t containerId, uint16_t index);
		void playerUpdateHouseWindow(uint32_t playerId, uint8_t listId, uint32_t windowTextId, const std::string& text);
		void playerWrapItem(uint32_t playerId, const Position& position, uint8_t stackPos, const uint16_t spriteId);
		void playerRequestTrade(uint32_t playerId, const Position& pos, uint8_t stackPos,
		                        uint32_t tradePlayerId, uint16_t spriteId);
		void playerAcceptTrade(uint32_t playerId);
		void playerLookInTrade(uint32_t playerId, bool lookAtCounterOffer, uint8_t index);
		void playerPurchaseItem(uint32_t playerId, uint16_t spriteId, uint8_t count, uint8_t amount,
		                        bool ignoreCap = false, bool inBackpacks = false);
		void playerSellItem(uint32_t playerId, uint16_t spriteId, uint8_t count,
		                    uint8_t amount, bool ignoreEquipped = false);
		void playerCloseShop(uint32_t playerId);
		void playerLookInShop(uint32_t playerId, uint16_t spriteId, uint8_t count);
		void playerCloseTrade(uint32_t playerId);
		void playerSetAttackedCreature(uint32_t playerId, uint32_t creatureId);
		void playerFollowCreature(uint32_t playerId, uint32_t creatureId);
		void playerCancelAttackAndFollow(uint32_t playerId);
		void playerSetFightModes(uint32_t playerId, fightMode_t fightMode, bool chaseMode, bool secureMode);
		void playerLookAt(uint32_t playerId, const Position& pos, uint8_t stackPos);
		void playerLookInBattleList(uint32_t playerId, uint32_t creatureId);
		void playerRequestAddVip(uint32_t playerId, const std::string& name);
		void playerRequestRemoveVip(uint32_t playerId, uint32_t guid);
		void playerRequestEditVip(uint32_t playerId, uint32_t guid, const std::string& description, uint32_t icon, bool notify);
		void playerTurn(uint32_t playerId, Direction dir);
		void playerRequestOutfit(uint32_t playerId);
		void playerShowQuestLog(uint32_t playerId);
		void playerShowQuestLine(uint32_t playerId, uint16_t questId);
		void playerSay(uint32_t playerId, uint16_t channelId, SpeakClasses type,
		               const std::string& receiver, const std::string& text);
		void playerChangeOutfit(uint32_t playerId, Outfit_t outfit);
		void playerInviteToParty(uint32_t playerId, uint32_t invitedId);
		void playerJoinParty(uint32_t playerId, uint32_t leaderId);
		void playerRevokePartyInvitation(uint32_t playerId, uint32_t invitedId);
		void playerPassPartyLeadership(uint32_t playerId, uint32_t newLeaderId);
		void playerLeaveParty(uint32_t playerId);
		void playerEnableSharedPartyExperience(uint32_t playerId, bool sharedExpActive);
		void playerToggleMount(uint32_t playerId, bool mount);
		void playerLeaveMarket(uint32_t playerId);
		void playerBrowseMarket(uint32_t playerId, uint16_t spriteId);
		void playerBrowseMarketOwnOffers(uint32_t playerId);
		void playerBrowseMarketOwnHistory(uint32_t playerId);
		void playerCreateMarketOffer(uint32_t playerId, uint8_t type, uint16_t spriteId, uint16_t amount, uint32_t price, bool anonymous);
		void playerCancelMarketOffer(uint32_t playerId, uint32_t timestamp, uint16_t counter);
		void playerAcceptMarketOffer(uint32_t playerId, uint32_t timestamp, uint16_t counter, uint16_t amount);

		void parsePlayerExtendedOpcode(uint32_t playerId, uint8_t opcode, const std::string& buffer);

		//std::forward_list<Item*> getMarketItemList(uint16_t wareId, uint16_t sufficientCount, DepotChest* depotChest, Inbox* inbox);

		void cleanup();
		void shutdown();
		void ReleaseCreature(Creature* creature);
		void ReleaseItem(Item* item);

		bool canThrowObjectTo(const Position& fromPos, const Position& toPos, bool checkLineOfSight = true, bool sameFloor = false,
		                      int32_t rangex = Map::maxClientViewportX, int32_t rangey = Map::maxClientViewportY) const;
		bool isSightClear(const Position& fromPos, const Position& toPos, bool sameFloor = false) const;

		void changeSpeed(Creature* creature, int32_t varSpeedDelta);
		void internalCreatureChangeOutfit(Creature* creature, const Outfit_t& outfit);
		void internalCreatureChangeVisible(Creature* creature, bool visible);
		void changeLight(const Creature* creature);
		void updateCreatureSkull(const Creature* creature);
		void updatePlayerShield(Player* player);
		void updatePlayerHelpers(const Player& player);
		void updateCreatureType(Creature* creature);
		void updateCreatureWalkthrough(const Creature* creature);

		GameState_t getGameState() const;
		void setGameState(GameState_t newState);
		bool saveGameState();

		//Events
		void checkCreatureWalk(uint32_t creatureId);
		void updateCreatureWalk(uint32_t creatureId);
		void checkCreatureAttack(uint32_t creatureId);
		void checkCreatures(size_t index);
		void checkLight();

		bool combatBlockHit(CombatDamage& damage, Creature* attacker, Creature* target, bool checkDefense, bool checkArmor, bool field, bool ignoreResistances = false);

		void combatGetTypeInfo(CombatType_t combatType, Creature* target, TextColor_t& color, uint8_t& effect);

		bool combatChangeHealth(Creature* attacker, Creature* target, CombatDamage& damage);
		bool combatChangeMana(Creature* attacker, Creature* target, CombatDamage& damage);

		//animation help functions
		void addCreatureHealth(const Creature* target);
		static void addCreatureHealth(const SpectatorVec& spectators, const Creature* target);
		void addColoredText(const ColoredText& coloredText);
		static void addColoredText(const SpectatorVec& spectators, const ColoredText& coloredText);
		void addMagicEffect(const Position& pos, uint8_t effect);
		static void addMagicEffect(const SpectatorVec& spectators, const Position& pos, uint8_t effect);
		void addDistanceEffect(const Position& fromPos, const Position& toPos, uint8_t effect);
		static void addDistanceEffect(const SpectatorVec& spectators, const Position& fromPos, const Position& toPos, uint8_t effect);

		void setAccountStorageValue(const uint32_t accountId, const uint32_t key, const int32_t value);
		int32_t getAccountStorageValue(const uint32_t accountId, const uint32_t key) const;
		void loadAccountStorageValues();
		bool saveAccountStorageValues() const;
		void loadBestiaryMonsters();
		const std::unordered_map<std::string, BestiaryMonsterEntry>& getBestiaryMonsters() const;
		void recordBestiaryKill(Player& player, const Monster& monster);
		uint32_t getBestiaryCharmPoints(const Player& player) const;
		void playerUnlockCharm(uint32_t playerId, uint8_t charmId);

		// Elite Creatures: delayed spawn of an elite variant after the 
		// portal interval elapsed at the death position of the origin monster.
		void spawnEliteCreature(const Position& pos, const std::string& mTypeName, uint8_t eliteTier);
		// Elite Creatures: safety net removing a portal item that outlived
		// the spawn delay (e.g. the spawn task failed to remove it).   
		void cleanupElitePortal(const Position& pos);
		// Elite Creatures: removes an elite that nobody killed within the
		// tier deadline (no loot, no experience).
		void despawnEliteCreature(uint32_t creatureId);

		void startDecay(Item* item);

		int16_t getWorldTime() { return worldTime; }
		void updateWorldTime();

		void loadMotdNum();
		void saveMotdNum() const;
		const std::string& getMotdHash() const { return motdHash; }
		uint32_t getMotdNum() const { return motdNum; }
		void incrementMotdNum() { motdNum++; }

		//void sendOfflineTrainingDialog(Player* player);

		const std::unordered_map<uint32_t, Player*>& getPlayers() const { return players; }
		const std::map<uint32_t, Npc*>& getNpcs() const { return npcs; }

		void addPlayer(Player* player);
		void removePlayer(Player* player);

		void addNpc(Npc* npc);
		void removeNpc(Npc* npc);

		void addMonster(Monster* monster);
		void removeMonster(Monster* monster);

		Guild* getGuild(uint32_t id) const;
		void addGuild(Guild* guild);
		void removeGuild(uint32_t guildId);
		void decreaseBrowseFieldRef(const Position& pos);

		std::unordered_map<Tile*, Container*> browseFields;

		void internalRemoveItems(std::vector<Item*> itemList, uint32_t amount, bool stackable);

		BedItem* getBedBySleeper(uint32_t guid) const;
		void setBedSleeper(BedItem* bed, uint32_t guid);
		void removeBedSleeper(uint32_t guid);

		Item* getUniqueItem(uint16_t uniqueId);
		bool addUniqueItem(uint16_t uniqueId, Item* item);
		void removeUniqueItem(uint16_t uniqueId);

		bool reload(ReloadTypes_t reloadType);

		Groups groups;
		Map map;
		Mounts mounts;
		Raids raids;
		Quests quests;

		std::forward_list<Item*> toDecayItems;

		std::unordered_set<Tile*> getTilesToClean() const {
			return tilesToClean;
		}
		void addTileToClean(Tile* tile) {
			tilesToClean.emplace(tile);
		}
		void removeTileToClean(Tile* tile) {
			tilesToClean.erase(tile);
		}
		void clearTilesToClean() {
			tilesToClean.clear();
		}

	private:
		bool playerSaySpell(Player* player, SpeakClasses type, const std::string& text);
		void playerWhisper(Player* player, const std::string& text);
		bool playerYell(Player* player, const std::string& text);
		bool playerSpeakTo(Player* player, SpeakClasses type, const std::string& receiver, const std::string& text);
		void playerSpeakToNpc(Player* player, const std::string& text);

		void checkDecay();
		void internalDecayItem(Item* item);
		void checkItemActorAttributions();
		void processItemActorAttributions(bool force = false);
		void queueItemActorAttribution(Item* root, uint32_t playerGuid);
		void normalizeItemActorSubtree(Item* root, uint32_t playerGuid,
		                               bool allowVirtualStorage = false);
		void certifyItemActorAncestorPath(Item* item, uint32_t playerGuid);
		bool hasPendingItemActorAttributionWithin(Item* containerItem,
		                                         uint32_t playerGuid,
		                                         bool includeContainer) const;
		void attributeSuccessfulItemEndpoint(Cylinder* expectedCylinder, Item* item,
		                                    uint32_t playerGuid);
		void attributeContainerPathAfterMutation(Cylinder* cylinder, uint32_t playerGuid);
		Item* findOutermostMovableActorContainer(Cylinder* cylinder) const;
		Player* findPlayerStorageOwner(Cylinder* cylinder) const;
		bool isInsideCreatureCorpse(Cylinder* cylinder) const;
		void checkFloorSnapshots();
		uint32_t processFloorSnapshots(bool force);
		bool queueFloorSnapshot(const Position& position, FloorDirtyTileRecord& record);
		bool prepareFloorSnapshot(const Position& position, const FloorDirtyTileRecord& record,
		                          bool cityCleanupFiltered, uint64_t groupId, uint64_t groupVersion,
		                          PreparedFloorSnapshot& prepared, std::string& error);
		bool executeFloorCheckpointGroup(uint64_t groupId, Player* requiredPlayer = nullptr);
		// Background (tick) checkpoint path: capture an immutable job from the
		// group and hand it to the checkpoint worker. Returns false when the
		// worker rejected the job (backpressure) or capture failed.
		bool enqueueFloorCheckpointGroup(uint64_t groupId);
		// Applies completed worker results (Dispatcher-side bookkeeping).
		void processCheckpointResults();
		// Central failure bookkeeping + alerting for checkpoint groups. Keeps
		// the existing retry/backoff behavior unchanged.
		void failFloorCheckpointGroup(FloorCheckpointGroup* group, const std::string& error,
		                              CheckpointGroupFailureKind kind);
		void releaseInFlightCheckpointPlayers(const std::set<uint32_t>& playerGuids);
		bool executeFloorSnapshotsTransaction(const std::vector<PreparedFloorSnapshot>& snapshots,
		                                      const std::vector<Player*>& checkpointPlayers,
		                                      const std::vector<House*>& checkpointHouses, uint64_t groupId,
		                                      uint64_t groupVersion, std::string& error,
		                                      bool commitCleanSave = false,
		                                      bool resetFloorSnapshots = false);
		bool flushFloorCheckpointGroups();
		bool saveAllFloorSnapshotsForCleanSave(uint32_t& savedTiles, std::string& error);
		void completePreparedFloorSnapshots(const std::vector<PreparedFloorSnapshot>& snapshots);
		void identifyFloorPersistenceMovableContainerAfterPlayerMutation(Cylinder* cylinder,
		                                                                 Player* actorPlayer);
		void stampFloorPersistenceActorAfterPlayerMutation(Cylinder* cylinder, Item* item,
		                                                   Player* actorPlayer);
		void registerFloorCheckpointTransfer(Cylinder* fromCylinder, Cylinder* toCylinder, Item* movedItem,
		                                     Player* actorPlayer);
		uint64_t registerTradeCheckpoint(Player* player, Player* tradePartner,
		                                 Cylinder* playerTradeSource, Cylinder* partnerTradeSource,
		                                 Item* playerTradeItem, Item* partnerTradeItem);
		uint64_t mergeFloorCheckpointGroups(const std::set<uint64_t>& groupIds);
		void touchFloorCheckpointGroup(uint64_t groupId);
		void removeFloorCheckpointGroup(uint64_t groupId);
		bool initializeFloorPersistenceSession();
		bool buildFloorRecoveryPlan();
		bool buildFloorRecoveryDryRun();
		bool buildFloorRecoveryPlayerReconciliation(
			const std::unordered_map<std::string, std::string>& floorIdentityPositions);
		bool materializeFloorRecoveryQuarantine();
		bool prepareAndApplyFloorRecovery();
		bool updateFloorPersistenceSession(const std::string& state, uint32_t playerCount,
		                                   uint32_t tileCount, const std::string& error = std::string());
		void completeFloorSnapshot(const Position& position, uint64_t tileVersion, bool success,
		                           FloorSnapshotRuntimeRecord runtimeRecord, const std::string& error);
		void updateSpawnPlayerBucket();
		void loadSpawnRateBoost();

		std::unordered_map<uint32_t, Player*> players;
		uint16_t spawnPlayerBucket = 0;
		int16_t spawnPlayerBucketOverride = -1;
		int64_t spawnRateBoostExpiresAt = 0;
		std::unordered_map<std::string, Player*> mappedPlayerNames;
		std::unordered_map<uint32_t, Player*> mappedPlayerGuids;
		std::unordered_map<uint32_t, Guild*> guilds;
		std::unordered_map<uint16_t, Item*> uniqueItems;
		std::map<uint32_t, uint32_t> stages;
		std::unordered_map<uint32_t, std::unordered_map<uint32_t, int32_t>> accountStorageMap;
		std::unordered_map<std::string, BestiaryMonsterEntry> bestiaryMonsters;

		std::list<Item*> decayItems[EVENT_DECAY_BUCKETS];
		std::vector<Item*> emergencyHeldDecayItems;
		std::unordered_set<Item*> emergencyHeldDecaySet;
		std::list<Creature*> checkCreatureLists[EVENT_CREATURECOUNT];

		std::vector<Creature*> ToReleaseCreatures;
		std::vector<Item*> ToReleaseItems;

		size_t lastBucket = 0;

		WildcardTreeNode wildcardTree { false };

		std::map<uint32_t, Npc*> npcs;
		std::map<uint32_t, Monster*> monsters;

		//list of items that are in trading state, mapped to the player
		std::map<Item*, uint32_t> tradeItems;
		ActiveMailTransferCheckpoint activeMailTransferCheckpoint;
		std::unordered_map<Item*, PendingItemActorAttribution> pendingItemActorAttributions;
		static constexpr uint32_t ITEM_ACTOR_DEBOUNCE_MS = 8000;
		static constexpr uint32_t ITEM_ACTOR_MAX_DELAY_MS = 24000;

		std::map<uint32_t, BedItem*> bedSleepersMap;

		std::unordered_set<Tile*> tilesToClean;
		std::map<Position, FloorDirtyTileRecord> floorDirtyTiles;
		std::map<Position, FloorSnapshotRuntimeRecord> floorSnapshotRuntimeRecords;
		std::set<Position> floorPersistenceCityPositions;
		std::map<uint64_t, FloorCheckpointGroup> floorCheckpointGroups;
		std::map<Position, uint64_t> floorCheckpointTileGroups;
		std::unordered_map<uint32_t, uint64_t> floorCheckpointPlayerGroups;
		std::unordered_map<uint32_t, uint64_t> floorCheckpointHouseGroups;
		std::unordered_map<std::string, uint64_t> floorCheckpointItemGroups;
		// Reference count of players that currently belong to a queued or
		// in-flight background checkpoint job. Used to keep asynchronous logout
		// from racing an in-flight checkpoint save for the same player.
		std::unordered_map<uint32_t, uint32_t> floorCheckpointInFlightPlayers;
		uint64_t floorDirtySequence = 0;
		uint64_t floorSnapshotVersionClock = 0;
		uint64_t floorCheckpointGroupClock = 0;
		uint64_t floorPersistenceSessionId = 0;
		uint64_t floorDirtyTotalEvents = 0;
		uint64_t floorDirtyIgnoredSystemEvents = 0;
		uint32_t floorDirtyPlayerMutationDepth = 0;
		uint32_t floorSnapshotWorldId = 1;
		uint32_t floorSnapshotGenerationId = 1;
		uint32_t floorSnapshotDebounceMs = 15000;
		uint32_t floorSnapshotMaxDelayMs = 60000;
		uint32_t floorSnapshotRetryMs = 5000;
		uint32_t floorSnapshotBatchSize = 32;
		uint32_t floorSnapshotSimulatedFailures = 0;
		bool floorDirtyTrackingEnabled = false;
		bool floorSnapshotShadowEnabled = false;
		bool floorCleanSaveInProgress = false;
		bool floorCleanSaveWindowActive = false;
		bool floorCleanSaveResetFloor = false;
		bool emergencyActive = false;
		uint64_t floorCleanSaveResetSnapshotCount = 0;
		uint32_t floorCleanSavePlayerCount = 0;
		uint32_t floorCleanSavePlayerFailures = 0;
		uint32_t floorCleanSaveTileCount = 0;
		std::string floorPersistenceSessionState = "DISABLED";
		FloorSnapshotStats floorSnapshotStats;
		FloorRecoveryPlan floorRecoveryPlan;
		bool floorRecoveryAppliedThisSession = false;
		bool floorRecoveryConfirmedThisSession = false;
		uint64_t floorRecoveryAppliedSourceSessionId = 0;
		uint64_t floorRecoveryConfirmedSourceSessionId = 0;
		std::unordered_set<std::string> floorRecoverySuppressedInstanceIds;

		ModalWindow offlineTrainingWindow { std::numeric_limits<uint32_t>::max(), "Choose a Skill", "Please choose a skill:" };

		static constexpr uint8_t LIGHT_DAY = 250;
		static constexpr uint8_t LIGHT_NIGHT = 40;
		// 1h realtime   = 1day worldtime
		// 2.5s realtime = 1min worldtime
		// worldTime is calculated in minutes
		static constexpr int16_t GAME_SUNRISE = 360;
		static constexpr int16_t GAME_DAYTIME = 480;
		static constexpr int16_t GAME_SUNSET = 1080;
		static constexpr int16_t GAME_NIGHTTIME = 1200;
		static constexpr float LIGHT_CHANGE_SUNRISE = static_cast<int>(float(float(LIGHT_DAY - LIGHT_NIGHT) / float(GAME_DAYTIME - GAME_SUNRISE)) * 100) / 100.0f;
		static constexpr float LIGHT_CHANGE_SUNSET = static_cast<int>(float(float(LIGHT_DAY - LIGHT_NIGHT) / float(GAME_NIGHTTIME - GAME_SUNSET)) * 100) / 100.0f;

		uint8_t lightLevel = LIGHT_DAY;
		uint8_t lightColor = 215;
		int16_t worldTime = 0;

		GameState_t gameState = GAME_STATE_NORMAL;
		WorldType_t worldType = WORLD_TYPE_PVP;

		ServiceManager* serviceManager = nullptr;

		void updatePlayersRecord() const;
		uint32_t playersRecord = 0;

		std::string motdHash;
		uint32_t motdNum = 0;

		uint32_t lastStageLevel = 0;
		bool stagesEnabled = false;
		bool useLastStageLevel = false;
};

#endif
