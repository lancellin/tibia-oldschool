/**
 * Stage 3 floor persistence shadow snapshots.
 */

#include "otpch.h"

#include "floorpersistence.h"

#include "container.h"
#include "fileloader.h"
#include "housetile.h"
#include "item.h"
#include "tile.h"

#include <cryptopp/sha.h>

#include <array>

namespace {
	enum class SnapshotPolicyState : uint8_t {
		EXCLUDED,
		DISCARD_CREATURE_CORPSE,
		DISCARD_TERMINAL_PLAYER_CORPSE,
		PERSIST_ALWAYS,
		PERSIST_CLEAN_ONLY,
		PERSIST_FOOD,
		PERSIST_DEATH_BUNDLE,
	};

	struct SnapshotContext {
		bool deathBundle = false;
		bool cityException = false;
		bool creatureCorpse = false;
	};

	struct PendingEntry {
		const Item* item = nullptr;
		SnapshotContext context;
		uint32_t depth = 0;
		bool endContainer = false;
	};

	struct DecodingFrame {
		uint32_t remaining = 0;
		uint32_t depth = 0;
		uint32_t parentIndex = UINT32_MAX;
		bool expectEnd = false;
	};

	struct ValidationFrame {
		uint32_t remaining = 0;
		uint32_t depth = 0;
		bool expectEnd = false;
	};

	struct DecodedSnapshotItem {
		std::unique_ptr<Item> item;
		uint32_t parentIndex = UINT32_MAX;
		uint32_t depth = 0;
		SnapshotContext context;
		SnapshotPolicyState policy = SnapshotPolicyState::EXCLUDED;
	};

	struct DecodedSnapshot {
		uint32_t topItemCount = 0;
		uint32_t maxDepth = 0;
		std::vector<DecodedSnapshotItem> items;
	};

	bool isFoodId(uint16_t itemId)
	{
		return (itemId >= 2666 && itemId <= 2691) || itemId == 2695 || itemId == 2696 ||
		       (itemId >= 2787 && itemId <= 2796);
	}

	bool hasCustomAttribute(const Item& item, const char* key)
	{
		return item.getCustomAttribute(key) != nullptr;
	}

	bool isDiscardableTerminalPlayerCorpse(const Item& item)
	{
		if (!hasCustomAttribute(item, ITEM_CUSTOM_ATTRIBUTE_FLOOR_PLAYER_CORPSE) ||
		    item.getContainer() || item.getDecayTo() != 0) {
			return false;
		}

		const ItemType& itemType = Item::items[item.getID()];
		return itemType.decayTime > 0;
	}

	const char* policyName(SnapshotPolicyState policy)
	{
		switch (policy) {
			case SnapshotPolicyState::PERSIST_ALWAYS:
				return "PERSIST_ALWAYS";
			case SnapshotPolicyState::PERSIST_CLEAN_ONLY:
				return "PERSIST_CLEAN_ONLY";
			case SnapshotPolicyState::PERSIST_FOOD:
				return "PERSIST_FOOD";
			case SnapshotPolicyState::PERSIST_DEATH_BUNDLE:
				return "PERSIST_DEATH_BUNDLE";
			case SnapshotPolicyState::DISCARD_CREATURE_CORPSE:
				return "DISCARD_CREATURE_CORPSE";
			case SnapshotPolicyState::DISCARD_TERMINAL_PLAYER_CORPSE:
				return "DISCARD_TERMINAL_PLAYER_CORPSE";
			case SnapshotPolicyState::EXCLUDED:
			default:
				return "EXCLUDED";
		}
	}

	SnapshotPolicyState classifyItem(const Item& item, bool cityExcluded, const SnapshotContext& parentContext,
	                                 SnapshotContext& childContext)
	{
		const bool itemDeathBundle = parentContext.deathBundle ||
		                             hasCustomAttribute(item, ITEM_CUSTOM_ATTRIBUTE_FLOOR_DEATH_BUNDLE);
		const bool playerCorpse = hasCustomAttribute(item, ITEM_CUSTOM_ATTRIBUTE_FLOOR_PLAYER_CORPSE);
		const bool creatureCorpse = parentContext.creatureCorpse ||
		                            item.isFloorPersistenceCreatureCorpse();

		childContext.deathBundle = itemDeathBundle;
		childContext.cityException = parentContext.cityException || playerCorpse;
		childContext.creatureCorpse = creatureCorpse;

		// The terminal player-corpse phase has no container and therefore no
		// recoverable loot. Persisting that visual remnant would resurrect a
		// corpse that has already completed its useful lifetime.
		if (isDiscardableTerminalPlayerCorpse(item)) {
			return SnapshotPolicyState::DISCARD_TERMINAL_PLAYER_CORPSE;
		}

		if (itemDeathBundle) {
			return SnapshotPolicyState::PERSIST_DEATH_BUNDLE;
		}

		if (creatureCorpse) {
			return SnapshotPolicyState::DISCARD_CREATURE_CORPSE;
		}

		if (cityExcluded && !childContext.cityException) {
			return SnapshotPolicyState::EXCLUDED;
		}

		if (item.isLoadedFromMap()) {
			return SnapshotPolicyState::EXCLUDED;
		}

		const ItemType& itemType = Item::items[item.getID()];
		if (!itemType.moveable) {
			return SnapshotPolicyState::EXCLUDED;
		}

		if (itemType.stackable) {
			return isFoodId(item.getID()) ? SnapshotPolicyState::PERSIST_FOOD :
			                                  SnapshotPolicyState::PERSIST_CLEAN_ONLY;
		}

		return SnapshotPolicyState::PERSIST_ALWAYS;
	}

	void recordClassification(const Item& item, SnapshotPolicyState state, FloorSnapshotData& snapshot)
	{
		switch (state) {
			case SnapshotPolicyState::PERSIST_ALWAYS:
				++snapshot.persistAlwaysCount;
				break;
			case SnapshotPolicyState::PERSIST_CLEAN_ONLY:
				++snapshot.persistCleanOnlyCount;
				break;
			case SnapshotPolicyState::PERSIST_FOOD:
				++snapshot.persistFoodCount;
				break;
			case SnapshotPolicyState::PERSIST_DEATH_BUNDLE:
				++snapshot.deathBundleCount;
				break;
			case SnapshotPolicyState::DISCARD_CREATURE_CORPSE:
			case SnapshotPolicyState::DISCARD_TERMINAL_PLAYER_CORPSE:
			case SnapshotPolicyState::EXCLUDED:
				++snapshot.excludedItemCount;
				return;
		}

		if (hasCustomAttribute(item, ITEM_CUSTOM_ATTRIBUTE_FLOOR_PLAYER_CORPSE)) {
			++snapshot.playerCorpseCount;
		}

		if (state != SnapshotPolicyState::PERSIST_ALWAYS) {
			return;
		}

		const ItemType& itemType = Item::items[item.getID()];
		if (!itemType.moveable || itemType.stackable || item.hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
			return;
		}

		if (!item.getFloorPersistenceInstanceId().empty()) {
			return;
		}

		if (item.getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_FLOOR_INSTANCE_ID)) {
			++snapshot.identityInvalidCount;
		} else {
			++snapshot.identityMissingCount;
		}
	}

	bool checkSnapshotSize(const PropWriteStream& stream, std::string& error)
	{
		size_t size = 0;
		stream.getStream(size);
		if (size > FLOOR_SNAPSHOT_MAX_BYTES) {
			error = "snapshot exceeds the 8 MiB stage 3 safety limit";
			return false;
		}
		return true;
	}

	bool decodeSnapshot(const std::string& data, const Position& expectedPosition,
	                    DecodedSnapshot& decoded, std::string& error)
	{
		decoded = {};
		error.clear();

		if (data.size() > FLOOR_SNAPSHOT_MAX_BYTES) {
			error = "stored snapshot exceeds the stage 3 byte limit";
			return false;
		}

		PropStream stream;
		stream.init(data.data(), data.size());

		uint8_t magic[4];
		for (uint8_t& character : magic) {
			if (!stream.read<uint8_t>(character)) {
				error = "truncated snapshot header";
				return false;
			}
		}
		if (magic[0] != 'F' || magic[1] != 'P' || magic[2] != 'S' || magic[3] != '3') {
			error = "invalid snapshot magic";
			return false;
		}

		uint16_t formatVersion;
		uint16_t policyVersion;
		uint16_t x;
		uint16_t y;
		uint8_t z;
		uint8_t reserved;
		if (!stream.read<uint16_t>(formatVersion) || !stream.read<uint16_t>(policyVersion) ||
		    !stream.read<uint16_t>(x) || !stream.read<uint16_t>(y) || !stream.read<uint8_t>(z) ||
		    !stream.read<uint8_t>(reserved) || !stream.read<uint32_t>(decoded.topItemCount)) {
			error = "truncated snapshot header";
			return false;
		}

		if (formatVersion != FLOOR_SNAPSHOT_FORMAT_VERSION) {
			error = "unsupported snapshot format version";
			return false;
		}
		if (policyVersion != FLOOR_SNAPSHOT_POLICY_VERSION) {
			error = "snapshot policy version mismatch";
			return false;
		}
		if (x != expectedPosition.x || y != expectedPosition.y || z != expectedPosition.z) {
			error = "snapshot position mismatch";
			return false;
		}
		if (reserved != 0 || decoded.topItemCount > FLOOR_SNAPSHOT_MAX_ITEMS) {
			error = "invalid snapshot header values";
			return false;
		}

		decoded.items.reserve(decoded.topItemCount);
		std::vector<DecodingFrame> frames;
		frames.push_back({decoded.topItemCount, 0, UINT32_MAX, false});
		while (!frames.empty()) {
			DecodingFrame& frame = frames.back();
			if (frame.remaining == 0) {
				if (frame.expectEnd) {
					uint8_t endAttribute;
					if (!stream.read<uint8_t>(endAttribute) || endAttribute != 0) {
						error = "container snapshot is missing its end marker";
						return false;
					}
				}
				frames.pop_back();
				continue;
			}

			--frame.remaining;
			const uint32_t itemDepth = frame.depth + 1;
			const uint32_t parentIndex = frame.parentIndex;
			if (decoded.items.size() >= FLOOR_SNAPSHOT_MAX_ITEMS) {
				error = "stored snapshot exceeds the item limit";
				return false;
			}
			if (itemDepth > FLOOR_SNAPSHOT_MAX_DEPTH) {
				error = "stored snapshot exceeds the depth limit";
				return false;
			}

			uint16_t itemId;
			if (!stream.read<uint16_t>(itemId) || itemId >= Item::items.size() || Item::items[itemId].id == 0) {
				error = "snapshot contains a truncated or unknown item id";
				return false;
			}

			std::unique_ptr<Item> item(Item::CreateItem(itemId));
			if (!item || !item->unserializeAttr(stream)) {
				error = "snapshot item attributes are invalid";
				return false;
			}

			Container* container = item->getContainer();
			const uint32_t childCount = container ? container->getSerializationCount() : 0;
			if (childCount > FLOOR_SNAPSHOT_MAX_ITEMS - decoded.items.size() - 1) {
				error = "snapshot container count exceeds the item limit";
				return false;
			}

			const uint32_t itemIndex = static_cast<uint32_t>(decoded.items.size());
			decoded.items.push_back({std::move(item), parentIndex, itemDepth, {}, SnapshotPolicyState::EXCLUDED});
			decoded.maxDepth = std::max(decoded.maxDepth, itemDepth);

			if (container) {
				frames.push_back({childCount, itemDepth, itemIndex, true});
			}
		}

		if (stream.size() != 0) {
			error = "snapshot has trailing bytes";
			return false;
		}
		return true;
	}
}

std::string FloorPersistenceSerializer::checksum(const std::string& data)
{
	CryptoPP::SHA256 hash;
	std::array<CryptoPP::byte, CryptoPP::SHA256::DIGESTSIZE> digest {};
	hash.CalculateDigest(digest.data(), reinterpret_cast<const CryptoPP::byte*>(data.data()), data.size());

	static constexpr char hex[] = "0123456789abcdef";
	std::string output;
	output.resize(digest.size() * 2);
	for (size_t index = 0; index < digest.size(); ++index) {
		output[index * 2] = hex[digest[index] >> 4];
		output[index * 2 + 1] = hex[digest[index] & 0x0F];
	}
	return output;
}

bool FloorPersistenceSerializer::serializeTile(const Position& position, const Tile* tile, bool cityExcluded,
                                               FloorSnapshotData& snapshot, std::string& error)
{
	snapshot = {};
	error.clear();

	if (tile && dynamic_cast<const HouseTile*>(tile)) {
		error = "house tile belongs to the existing house persistence system";
		return false;
	}

	std::vector<PendingEntry> topItems;
	if (tile) {
		if (const TileItemVector* items = tile->getItemList()) {
			topItems.reserve(items->size());
			for (const Item* item : *items) {
				SnapshotContext childContext;
				const SnapshotPolicyState state = classifyItem(*item, cityExcluded, {}, childContext);
				if (state == SnapshotPolicyState::EXCLUDED ||
				    state == SnapshotPolicyState::DISCARD_CREATURE_CORPSE ||
				    state == SnapshotPolicyState::DISCARD_TERMINAL_PLAYER_CORPSE) {
					++snapshot.excludedItemCount;
					continue;
				}
				topItems.push_back({item, childContext, 1, false});
			}
		}
	}

	snapshot.topItemCount = static_cast<uint32_t>(topItems.size());

	PropWriteStream stream;
	stream.write<uint8_t>('F');
	stream.write<uint8_t>('P');
	stream.write<uint8_t>('S');
	stream.write<uint8_t>('3');
	stream.write<uint16_t>(FLOOR_SNAPSHOT_FORMAT_VERSION);
	stream.write<uint16_t>(FLOOR_SNAPSHOT_POLICY_VERSION);
	stream.write<uint16_t>(position.x);
	stream.write<uint16_t>(position.y);
	stream.write<uint8_t>(position.z);
	stream.write<uint8_t>(0); // reserved
	stream.write<uint32_t>(snapshot.topItemCount);

	std::vector<PendingEntry> pending;
	pending.reserve(topItems.size() + 8);
	for (const PendingEntry& entry : topItems) {
		pending.push_back(entry);
	}

	while (!pending.empty()) {
		PendingEntry entry = pending.back();
		pending.pop_back();

		if (entry.endContainer) {
			stream.write<uint8_t>(0x00);
			continue;
		}

		if (!entry.item) {
			error = "null item found while serializing snapshot";
			return false;
		}
		if (entry.depth > FLOOR_SNAPSHOT_MAX_DEPTH) {
			error = "snapshot exceeds the 4096-level stage 3 safety limit";
			return false;
		}
		if (++snapshot.itemCount > FLOOR_SNAPSHOT_MAX_ITEMS) {
			error = "snapshot exceeds the 100000-item stage 3 safety limit";
			return false;
		}

		SnapshotContext effectiveContext;
		const SnapshotPolicyState state = classifyItem(*entry.item, cityExcluded, entry.context, effectiveContext);
		if (state == SnapshotPolicyState::EXCLUDED ||
		    state == SnapshotPolicyState::DISCARD_CREATURE_CORPSE ||
		    state == SnapshotPolicyState::DISCARD_TERMINAL_PLAYER_CORPSE) {
			++snapshot.excludedItemCount;
			continue;
		}
		recordClassification(*entry.item, state, snapshot);

		stream.write<uint16_t>(entry.item->getID());
		entry.item->serializeAttr(stream);

		const Container* container = entry.item->getContainer();
		if (!container) {
			stream.write<uint8_t>(0x00);
			if (!checkSnapshotSize(stream, error)) {
				return false;
			}
			continue;
		}

		std::vector<PendingEntry> children;
		children.reserve(container->size());
		for (const Item* child : container->getItemList()) {
			SnapshotContext childContext;
			const SnapshotPolicyState childState = classifyItem(*child, cityExcluded, effectiveContext, childContext);
			if (childState == SnapshotPolicyState::EXCLUDED ||
			    childState == SnapshotPolicyState::DISCARD_CREATURE_CORPSE ||
			    childState == SnapshotPolicyState::DISCARD_TERMINAL_PLAYER_CORPSE) {
				++snapshot.excludedItemCount;
				continue;
			}
			children.push_back({child, childContext, entry.depth + 1, false});
		}

		stream.write<uint8_t>(ATTR_CONTAINER_ITEMS);
		stream.write<uint32_t>(static_cast<uint32_t>(children.size()));
		pending.push_back({nullptr, {}, entry.depth, true});
		for (const PendingEntry& child : children) {
			pending.push_back(child);
		}

		if (!checkSnapshotSize(stream, error)) {
			return false;
		}
	}

	size_t serializedSize = 0;
	const char* serialized = stream.getStream(serializedSize);
	snapshot.serializedData.assign(serialized, serializedSize);
	snapshot.checksum = checksum(snapshot.serializedData);
	return true;
}

bool FloorPersistenceSerializer::validateSnapshot(const std::string& data, const Position& expectedPosition,
                                                  uint32_t& itemCount, uint32_t& topItemCount, std::string& error)
{
	itemCount = 0;
	topItemCount = 0;
	error.clear();

	if (data.size() > FLOOR_SNAPSHOT_MAX_BYTES) {
		error = "stored snapshot exceeds the stage 3 byte limit";
		return false;
	}

	PropStream stream;
	stream.init(data.data(), data.size());

	uint8_t magic[4];
	for (uint8_t& character : magic) {
		if (!stream.read<uint8_t>(character)) {
			error = "truncated snapshot header";
			return false;
		}
	}
	if (magic[0] != 'F' || magic[1] != 'P' || magic[2] != 'S' || magic[3] != '3') {
		error = "invalid snapshot magic";
		return false;
	}

	uint16_t formatVersion;
	uint16_t policyVersion;
	uint16_t x;
	uint16_t y;
	uint8_t z;
	uint8_t reserved;
	if (!stream.read<uint16_t>(formatVersion) || !stream.read<uint16_t>(policyVersion) ||
	    !stream.read<uint16_t>(x) || !stream.read<uint16_t>(y) || !stream.read<uint8_t>(z) ||
	    !stream.read<uint8_t>(reserved) || !stream.read<uint32_t>(topItemCount)) {
		error = "truncated snapshot header";
		return false;
	}

	if (formatVersion != FLOOR_SNAPSHOT_FORMAT_VERSION) {
		error = "unsupported snapshot format version";
		return false;
	}
	if (policyVersion != FLOOR_SNAPSHOT_POLICY_VERSION) {
		error = "snapshot policy version mismatch";
		return false;
	}
	if (x != expectedPosition.x || y != expectedPosition.y || z != expectedPosition.z) {
		error = "snapshot position mismatch";
		return false;
	}
	if (reserved != 0 || topItemCount > FLOOR_SNAPSHOT_MAX_ITEMS) {
		error = "invalid snapshot header values";
		return false;
	}

	std::vector<ValidationFrame> frames;
	frames.push_back({topItemCount, 0, false});
	while (!frames.empty()) {
		ValidationFrame& frame = frames.back();
		if (frame.remaining == 0) {
			if (frame.expectEnd) {
				uint8_t endAttribute;
				if (!stream.read<uint8_t>(endAttribute) || endAttribute != 0) {
					error = "container snapshot is missing its end marker";
					return false;
				}
			}
			frames.pop_back();
			continue;
		}

		--frame.remaining;
		if (++itemCount > FLOOR_SNAPSHOT_MAX_ITEMS) {
			error = "stored snapshot exceeds the item limit";
			return false;
		}
		if (frame.depth + 1 > FLOOR_SNAPSHOT_MAX_DEPTH) {
			error = "stored snapshot exceeds the depth limit";
			return false;
		}

		uint16_t itemId;
		if (!stream.read<uint16_t>(itemId) || itemId >= Item::items.size() || Item::items[itemId].id == 0) {
			error = "snapshot contains a truncated or unknown item id";
			return false;
		}

		std::unique_ptr<Item> item(Item::CreateItem(itemId));
		if (!item || !item->unserializeAttr(stream)) {
			error = "snapshot item attributes are invalid";
			return false;
		}

		if (Container* container = item->getContainer()) {
			const uint32_t childCount = container->serializationCount;
			if (childCount > FLOOR_SNAPSHOT_MAX_ITEMS - itemCount) {
				error = "snapshot container count exceeds the item limit";
				return false;
			}
			frames.push_back({childCount, frame.depth + 1, true});
		}
	}

	if (stream.size() != 0) {
		error = "snapshot has trailing bytes";
		return false;
	}
	return true;
}

bool FloorPersistenceSerializer::hasOnlyDiscardableLegacyIdentityProblems(
	const std::string& data, const Position& expectedPosition,
	uint32_t expectedMissing, uint32_t expectedInvalid)
{
	if (expectedMissing == 0 || expectedInvalid != 0) {
		return false;
	}

	DecodedSnapshot decoded;
	std::string error;
	if (!decodeSnapshot(data, expectedPosition, decoded, error)) {
		return false;
	}

	uint32_t discardableMissing = 0;
	for (const DecodedSnapshotItem& decodedItem : decoded.items) {
		if (decodedItem.parentIndex != UINT32_MAX ||
		    !isDiscardableTerminalPlayerCorpse(*decodedItem.item) ||
		    decodedItem.item->getCustomAttribute(ITEM_CUSTOM_ATTRIBUTE_FLOOR_INSTANCE_ID) ||
		    !decodedItem.item->getFloorPersistenceInstanceId().empty()) {
			continue;
		}
		++discardableMissing;
	}
	return discardableMissing == expectedMissing;
}

bool FloorPersistenceSerializer::analyzeRecoverySnapshot(const std::string& data,
                                                         const Position& expectedPosition,
                                                         FloorRecoverySnapshotMode mode,
                                                         FloorRecoverySnapshotAnalysis& analysis,
                                                         std::string& error)
{
	analysis = {};
	error.clear();

	DecodedSnapshot decoded;
	if (!decodeSnapshot(data, expectedPosition, decoded, error)) {
		return false;
	}

	analysis.itemCount = static_cast<uint32_t>(decoded.items.size());
	analysis.topItemCount = decoded.topItemCount;
	analysis.maxDepth = decoded.maxDepth;
	analysis.instanceIds.reserve(decoded.items.size());

	for (uint32_t index = 0; index < decoded.items.size(); ++index) {
		DecodedSnapshotItem& decodedItem = decoded.items[index];
		const bool topItem = decodedItem.parentIndex == UINT32_MAX;
		SnapshotContext parentContext;
		if (!topItem) {
			if (decodedItem.parentIndex >= index) {
				++analysis.rejectedItemCount;
				error = "snapshot hierarchy contains an invalid parent index";
				return false;
			}
			parentContext = decoded.items[decodedItem.parentIndex].context;
		}

		decodedItem.policy = classifyItem(*decodedItem.item, false, parentContext, decodedItem.context);
		if (decodedItem.item->getContainer()) {
			++analysis.containerCount;
		}

		bool restore = false;
		bool quarantine = false;
		switch (decodedItem.policy) {
			case SnapshotPolicyState::PERSIST_ALWAYS: {
				++analysis.persistAlwaysCount;
				if (!decodedItem.item->isFloorPersistenceIdentityReady()) {
					++analysis.rejectedItemCount;
					if (topItem) {
						++analysis.rejectedTopItemCount;
					}
					error = "snapshot contains a persistent item without a valid instance identity";
					return false;
				}
				if (decodedItem.item->isFloorPersistenceInstanceEligible()) {
					const std::string instanceId = decodedItem.item->getFloorPersistenceInstanceId();
					if (instanceId.empty()) {
						++analysis.rejectedItemCount;
						if (topItem) {
							++analysis.rejectedTopItemCount;
						}
						error = "snapshot contains an eligible item with an unreadable instance identity";
						return false;
					}
					analysis.instanceIds.push_back(instanceId);
				}
				restore = true;
				break;
			}
			case SnapshotPolicyState::PERSIST_CLEAN_ONLY:
				++analysis.persistCleanOnlyCount;
				if (mode == FloorRecoverySnapshotMode::CLEAN_RESTART) {
					restore = true;
				} else {
					quarantine = true;
				}
				break;
			case SnapshotPolicyState::PERSIST_FOOD:
				++analysis.persistFoodCount;
				restore = true;
				break;
			case SnapshotPolicyState::PERSIST_DEATH_BUNDLE:
				++analysis.deathBundleCount;
				restore = true;
				break;
			case SnapshotPolicyState::DISCARD_CREATURE_CORPSE:
				// Backward compatibility: old snapshots may contain a complete
				// creature corpse subtree. Count it as selected so stage totals
				// remain stable, but stage 5.5 suppresses it from the map.
				restore = true;
				break;
			case SnapshotPolicyState::DISCARD_TERMINAL_PLAYER_CORPSE:
				// A terminal player corpse is only a short-lived visual remnant.
				// It has no container or loot and is suppressed during apply.
				restore = true;
				break;
			case SnapshotPolicyState::EXCLUDED:
				++analysis.rejectedItemCount;
				if (topItem) {
					++analysis.rejectedTopItemCount;
				}
				error = "snapshot contains an item excluded by the current recovery policy";
				return false;
		}

		if (restore) {
			++analysis.restoreItemCount;
			if (topItem) {
				++analysis.restoreTopItemCount;
			}
		} else if (quarantine) {
			++analysis.quarantineItemCount;
			if (topItem) {
				++analysis.quarantineTopItemCount;
			}
		}
	}

	return true;
}

bool FloorPersistenceSerializer::buildQuarantineManifest(
	const std::string& data, const Position& expectedPosition,
	const std::unordered_set<std::string>& suppressedInstanceIds,
	std::vector<FloorQuarantineManifestItem>& manifest, std::string& error)
{
	static constexpr uint32_t QUARANTINE_REASON_CRASH_STACKABLE = 1 << 0;
	static constexpr uint32_t QUARANTINE_REASON_PLAYER_MATCH = 1 << 1;

	manifest.clear();
	error.clear();

	DecodedSnapshot decoded;
	if (!decodeSnapshot(data, expectedPosition, decoded, error)) {
		return false;
	}

	std::vector<uint32_t> reasons(decoded.items.size(), 0);
	std::vector<bool> included(decoded.items.size(), false);
	for (uint32_t index = 0; index < decoded.items.size(); ++index) {
		DecodedSnapshotItem& decodedItem = decoded.items[index];
		SnapshotContext parentContext;
		if (decodedItem.parentIndex != UINT32_MAX) {
			if (decodedItem.parentIndex >= index) {
				error = "snapshot hierarchy contains an invalid parent index";
				manifest.clear();
				return false;
			}
			parentContext = decoded.items[decodedItem.parentIndex].context;
		}

		decodedItem.policy = classifyItem(*decodedItem.item, false, parentContext, decodedItem.context);
		if (decodedItem.policy == SnapshotPolicyState::PERSIST_CLEAN_ONLY) {
			reasons[index] |= QUARANTINE_REASON_CRASH_STACKABLE;
		}

		const std::string instanceId = decodedItem.item->getFloorPersistenceInstanceId();
		if (!instanceId.empty() && suppressedInstanceIds.find(instanceId) != suppressedInstanceIds.end()) {
			reasons[index] |= QUARANTINE_REASON_PLAYER_MATCH;
		}

		if (reasons[index] == 0) {
			continue;
		}

		uint32_t ancestor = index;
		while (ancestor != UINT32_MAX && !included[ancestor]) {
			included[ancestor] = true;
			ancestor = decoded.items[ancestor].parentIndex;
		}
	}

	for (uint32_t index = 0; index < decoded.items.size(); ++index) {
		if (!included[index]) {
			continue;
		}

		const DecodedSnapshotItem& decodedItem = decoded.items[index];
		const Item& item = *decodedItem.item;
		const Container* container = item.getContainer();
		FloorQuarantineManifestItem entry;
		entry.sourceIndex = index;
		entry.parentSourceIndex = decodedItem.parentIndex == UINT32_MAX ?
			-1 : static_cast<int64_t>(decodedItem.parentIndex);
		entry.depth = decodedItem.depth;
		entry.itemId = item.getID();
		entry.itemCount = item.getItemCount();
		entry.itemSubtype = item.getSubType();
		entry.container = container != nullptr;
		entry.containerCapacity = container ? container->capacity() : 0;
		entry.actionId = item.getActionId();
		entry.uniqueId = item.getUniqueId();
		entry.durationMs = item.getDuration();
		entry.lastActorGuid = item.getFloorPersistenceLastActorGuid();
		uint32_t actorAncestor = decodedItem.parentIndex;
		while (actorAncestor != UINT32_MAX) {
			const uint32_t ancestorActor =
				decoded.items[actorAncestor].item->getFloorPersistenceLastActorGuid();
			if (ancestorActor != 0) {
				// Outermost movable containers are stamped whenever their subtree
				// changes or the whole container moves, so the outermost recorded
				// actor is authoritative for contained stackables.
				entry.lastActorGuid = ancestorActor;
			}
			actorAncestor = decoded.items[actorAncestor].parentIndex;
		}
		entry.reasonMask = reasons[index];
		entry.quarantined = reasons[index] != 0;
		entry.deathBundle = hasCustomAttribute(item, ITEM_CUSTOM_ATTRIBUTE_FLOOR_DEATH_BUNDLE);
		entry.playerCorpse = hasCustomAttribute(item, ITEM_CUSTOM_ATTRIBUTE_FLOOR_PLAYER_CORPSE);
		entry.policy = policyName(decodedItem.policy);
		entry.name = item.getName();
		entry.description = item.getDescription(0);
		entry.instanceId = item.getFloorPersistenceInstanceId();
		entry.specialDescription = item.getSpecialDescription();
		entry.writtenText = item.getText();
		entry.writer = item.getWriter();
		entry.writtenDate = static_cast<int64_t>(item.getDate());
		manifest.push_back(std::move(entry));
	}

	return true;
}

bool FloorPersistenceSerializer::prepareRecoverySnapshot(
	const std::string& data, const Position& expectedPosition, FloorRecoverySnapshotMode mode,
	const std::unordered_set<std::string>& suppressedInstanceIds,
	std::vector<std::unique_ptr<Item>>& topItems, FloorRecoverySnapshotRestore& restore,
	std::string& error)
{
	topItems.clear();
	restore = {};
	error.clear();

	DecodedSnapshot decoded;
	if (!decodeSnapshot(data, expectedPosition, decoded, error)) {
		return false;
	}

	restore.sourceItemCount = static_cast<uint32_t>(decoded.items.size());
	restore.sourceTopItemCount = decoded.topItemCount;
	std::vector<Item*> preparedItems(decoded.items.size(), nullptr);
	std::vector<bool> suppressedHierarchy(decoded.items.size(), false);

	for (uint32_t index = 0; index < decoded.items.size(); ++index) {
		DecodedSnapshotItem& decodedItem = decoded.items[index];
		const bool topItem = decodedItem.parentIndex == UINT32_MAX;
		SnapshotContext parentContext;
		if (!topItem) {
			if (decodedItem.parentIndex >= index) {
				error = "snapshot hierarchy contains an invalid parent index";
				topItems.clear();
				return false;
			}
			parentContext = decoded.items[decodedItem.parentIndex].context;
		}

		decodedItem.policy = classifyItem(*decodedItem.item, false, parentContext, decodedItem.context);
		bool restoreByPolicy = false;
		bool quarantine = false;
		bool discardSnapshotItem = false;
		std::string instanceId;
		switch (decodedItem.policy) {
			case SnapshotPolicyState::PERSIST_ALWAYS:
				if (!decodedItem.item->isFloorPersistenceIdentityReady()) {
					error = "snapshot contains a persistent item without a valid instance identity";
					topItems.clear();
					return false;
				}
				if (decodedItem.item->isFloorPersistenceInstanceEligible()) {
					instanceId = decodedItem.item->getFloorPersistenceInstanceId();
					if (instanceId.empty()) {
						error = "snapshot contains an eligible item with an unreadable instance identity";
						topItems.clear();
						return false;
					}
				}
				restoreByPolicy = true;
				break;
			case SnapshotPolicyState::PERSIST_CLEAN_ONLY:
				if (mode == FloorRecoverySnapshotMode::CLEAN_RESTART) {
					restoreByPolicy = true;
				} else {
					quarantine = true;
				}
				break;
			case SnapshotPolicyState::PERSIST_FOOD:
			case SnapshotPolicyState::PERSIST_DEATH_BUNDLE:
				restoreByPolicy = true;
				break;
			case SnapshotPolicyState::DISCARD_CREATURE_CORPSE:
				restoreByPolicy = true;
				discardSnapshotItem = true;
				break;
			case SnapshotPolicyState::DISCARD_TERMINAL_PLAYER_CORPSE:
				restoreByPolicy = true;
				discardSnapshotItem = true;
				break;
			case SnapshotPolicyState::EXCLUDED:
				error = "snapshot contains an item excluded by the current recovery policy";
				topItems.clear();
				return false;
		}

		if (quarantine) {
			++restore.quarantineItemCount;
			if (topItem) {
				++restore.quarantineTopItemCount;
			}
			continue;
		}

		if (!restoreByPolicy) {
			error = "snapshot recovery decision did not select an item";
			topItems.clear();
			return false;
		}

		++restore.policyRestoreItemCount;
		if (topItem) {
			++restore.policyRestoreTopItemCount;
		}

		const bool ancestorSuppressed = !topItem && suppressedHierarchy[decodedItem.parentIndex];
		const bool directSuppressed = !instanceId.empty() &&
			suppressedInstanceIds.find(instanceId) != suppressedInstanceIds.end();
		if (directSuppressed) {
			++restore.directSuppressedIdentityCount;
		}
		if (discardSnapshotItem || ancestorSuppressed || directSuppressed) {
			suppressedHierarchy[index] = true;
			++restore.suppressedItemCount;
			if (topItem) {
				++restore.suppressedTopItemCount;
			}
			continue;
		}

		Item* preparedItem = decodedItem.item.get();
		if (topItem) {
			topItems.emplace_back(std::move(decodedItem.item));
		} else {
			Item* parentItem = preparedItems[decodedItem.parentIndex];
			Container* parentContainer = parentItem ? parentItem->getContainer() : nullptr;
			if (!parentContainer) {
				error = "restorable snapshot child has no restorable container parent";
				topItems.clear();
				return false;
			}
			parentContainer->internalAddThing(decodedItem.item.release());
		}
		preparedItems[index] = preparedItem;
		++restore.preparedItemCount;
		if (topItem) {
			++restore.preparedTopItemCount;
		}
	}

	if (restore.policyRestoreItemCount + restore.quarantineItemCount != restore.sourceItemCount ||
	    restore.policyRestoreTopItemCount + restore.quarantineTopItemCount != restore.sourceTopItemCount) {
		error = "prepared recovery policy counters do not cover the complete snapshot";
		topItems.clear();
		return false;
	}
	if (restore.preparedItemCount + restore.suppressedItemCount != restore.policyRestoreItemCount ||
	    restore.preparedTopItemCount + restore.suppressedTopItemCount != restore.policyRestoreTopItemCount) {
		error = "prepared recovery suppression counters do not match the restore policy";
		topItems.clear();
		return false;
	}

	return true;
}
