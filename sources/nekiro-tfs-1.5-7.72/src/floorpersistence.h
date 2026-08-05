/**
 * Stage 3 floor persistence shadow snapshots and stage 5 recovery decoding.
 *
 * Stage 3 captures, validates and stores the filtered state of dirty floor
 * tiles. Stage 5 can also prepare a fully validated, policy-filtered item tree
 * for the explicit administrative recovery path.
 */

#ifndef FS_FLOORPERSISTENCE_H_8F3F3B6F25614D0A9F748541C58BF665
#define FS_FLOORPERSISTENCE_H_8F3F3B6F25614D0A9F748541C58BF665

#include "position.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class Item;
class Tile;

inline constexpr uint16_t FLOOR_SNAPSHOT_FORMAT_VERSION = 1;
inline constexpr uint16_t FLOOR_SNAPSHOT_POLICY_VERSION = 1;
inline constexpr uint32_t FLOOR_SNAPSHOT_MAX_ITEMS = 100000;
inline constexpr uint32_t FLOOR_SNAPSHOT_MAX_DEPTH = 4096;
inline constexpr size_t FLOOR_SNAPSHOT_MAX_BYTES = 8 * 1024 * 1024;

struct FloorSnapshotData {
	std::string serializedData;
	std::string checksum;
	uint32_t itemCount = 0;
	uint32_t topItemCount = 0;
	uint32_t persistAlwaysCount = 0;
	uint32_t persistCleanOnlyCount = 0;
	uint32_t persistFoodCount = 0;
	uint32_t deathBundleCount = 0;
	uint32_t excludedItemCount = 0;
	uint32_t identityMissingCount = 0;
	uint32_t identityInvalidCount = 0;
	uint32_t playerCorpseCount = 0;
};

enum class FloorRecoverySnapshotMode : uint8_t {
	CLEAN_RESTART,
	CRASH_RECOVERY,
};

struct FloorRecoverySnapshotAnalysis {
	uint32_t itemCount = 0;
	uint32_t topItemCount = 0;
	uint32_t restoreItemCount = 0;
	uint32_t quarantineItemCount = 0;
	uint32_t rejectedItemCount = 0;
	uint32_t restoreTopItemCount = 0;
	uint32_t quarantineTopItemCount = 0;
	uint32_t rejectedTopItemCount = 0;
	uint32_t persistAlwaysCount = 0;
	uint32_t persistCleanOnlyCount = 0;
	uint32_t persistFoodCount = 0;
	uint32_t deathBundleCount = 0;
	uint32_t containerCount = 0;
	uint32_t maxDepth = 0;
	std::vector<std::string> instanceIds;
};

struct FloorRecoverySnapshotRestore {
	uint32_t sourceItemCount = 0;
	uint32_t sourceTopItemCount = 0;
	uint32_t policyRestoreItemCount = 0;
	uint32_t policyRestoreTopItemCount = 0;
	uint32_t quarantineItemCount = 0;
	uint32_t quarantineTopItemCount = 0;
	uint32_t suppressedItemCount = 0;
	uint32_t suppressedTopItemCount = 0;
	uint32_t directSuppressedIdentityCount = 0;
	uint32_t preparedItemCount = 0;
	uint32_t preparedTopItemCount = 0;
};

struct FloorQuarantineManifestItem {
	uint32_t sourceIndex = 0;
	int64_t parentSourceIndex = -1;
	uint32_t depth = 0;
	uint16_t itemId = 0;
	uint16_t itemCount = 0;
	uint16_t itemSubtype = 0;
	uint32_t containerCapacity = 0;
	uint32_t actionId = 0;
	uint32_t uniqueId = 0;
	uint32_t durationMs = 0;
	uint32_t lastActorGuid = 0;
	uint32_t reasonMask = 0;
	bool container = false;
	bool quarantined = false;
	bool deathBundle = false;
	bool playerCorpse = false;
	std::string policy;
	std::string name;
	std::string description;
	std::string instanceId;
	std::string specialDescription;
	std::string writtenText;
	std::string writer;
	int64_t writtenDate = 0;
};

class FloorPersistenceSerializer final
{
	public:
		static bool serializeTile(const Position& position, const Tile* tile, bool cityExcluded,
		                          FloorSnapshotData& snapshot, std::string& error);
		static bool validateSnapshot(const std::string& data, const Position& expectedPosition,
		                             uint32_t& itemCount, uint32_t& topItemCount, std::string& error);
		static bool hasOnlyDiscardableLegacyIdentityProblems(const std::string& data,
		                                                     const Position& expectedPosition,
		                                                     uint32_t expectedMissing,
		                                                     uint32_t expectedInvalid);
		static bool analyzeRecoverySnapshot(const std::string& data, const Position& expectedPosition,
		                                    FloorRecoverySnapshotMode mode,
		                                    FloorRecoverySnapshotAnalysis& analysis, std::string& error);
		static bool buildQuarantineManifest(
			const std::string& data, const Position& expectedPosition,
			const std::unordered_set<std::string>& suppressedInstanceIds,
			std::vector<FloorQuarantineManifestItem>& manifest, std::string& error);
		static bool prepareRecoverySnapshot(const std::string& data, const Position& expectedPosition,
		                                    FloorRecoverySnapshotMode mode,
		                                    const std::unordered_set<std::string>& suppressedInstanceIds,
		                                    std::vector<std::unique_ptr<Item>>& topItems,
		                                    FloorRecoverySnapshotRestore& restore, std::string& error);
		static std::string checksum(const std::string& data);
};

#endif
