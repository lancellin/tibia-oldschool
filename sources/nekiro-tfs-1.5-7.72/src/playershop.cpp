#include "otpch.h"

#include "playershop.h"

#include "game.h"
#include "iologindata.h"
#include "item.h"
#include "map.h"
#include "scheduler.h"
#include "tools.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>

namespace {
static constexpr uint32_t PLAYER_SHOP_CONFIG_TIMEOUT_MS = 5 * 60 * 1000;
static constexpr uint32_t PLAYER_SHOP_MAINTENANCE_MS = 500;
static constexpr size_t PLAYER_SHOP_MAX_OPCODE_BUFFER = 256;
static constexpr uint32_t PLAYER_SHOP_CONFIG_RATE_LIMIT_MS = 1000;
static constexpr uint32_t PLAYER_SHOP_OPCODE_RATE_LIMIT_MS = 150;
static constexpr bool PLAYER_SHOP_DEBUG = false;
static constexpr bool PLAYER_SHOP_SAVE_AFTER_BUY = true;

void debugLog(const std::string& message)
{
	if (PLAYER_SHOP_DEBUG) {
		std::cout << "[PlayerShop] " << message << std::endl;
	}
}

bool isSameOrNear(const Position& a, const Position& b, uint8_t range)
{
	return a.z == b.z && std::max<int32_t>(std::abs(static_cast<int32_t>(a.x) - static_cast<int32_t>(b.x)),
		std::abs(static_cast<int32_t>(a.y) - static_cast<int32_t>(b.y))) <= range;
}
}

PlayerShopManager g_playerShop;

std::vector<std::string> PlayerShopManager::split(const std::string& text, char separator)
{
	std::vector<std::string> parts;
	std::string part;
	std::stringstream stream(text);
	while (std::getline(stream, part, separator)) {
		parts.push_back(part);
	}
	return parts;
}

uint64_t PlayerShopManager::parseUnsigned64(const std::string& text, bool& ok)
{
	uint64_t value = 0;
	const auto* begin = text.data();
	const auto* end = text.data() + text.size();
	const auto result = std::from_chars(begin, end, value);
	ok = result.ec == std::errc() && result.ptr == end;
	return value;
}

uint32_t PlayerShopManager::parseUnsigned32(const std::string& text, bool& ok)
{
	uint32_t value = 0;
	const auto* begin = text.data();
	const auto* end = text.data() + text.size();
	const auto result = std::from_chars(begin, end, value);
	ok = result.ec == std::errc() && result.ptr == end;
	return value;
}

std::string PlayerShopManager::sanitizeDescription(const std::string& text)
{
	std::string out;
	out.reserve(38);
	for (char c : text) {
		if (out.size() >= 38) {
			break;
		}
		if (c == '\n' || c == '\r' || c == '|') {
			if (!out.empty() && out.back() != ' ') {
				out.push_back(' ');
			}
			continue;
		}
		if (static_cast<unsigned char>(c) < 32) {
			continue;
		}
		out.push_back(c);
	}

	while (!out.empty() && out.front() == ' ') {
		out.erase(out.begin());
	}
	while (!out.empty() && out.back() == ' ') {
		out.pop_back();
	}

	if (out.empty()) {
		out = "description";
	}
	return out;
}

std::string PlayerShopManager::jsonEscape(const std::string& text)
{
	std::string out;
	out.reserve(text.size() + 8);
	for (char c : text) {
		switch (c) {
			case '\\': out += "\\\\"; break;
			case '"': out += "\\\""; break;
			case '\n': out += "\\n"; break;
			case '\r': break;
			case '\t': out += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 32) {
					out += ' ';
				} else {
					out += c;
				}
				break;
		}
	}
	return out;
}

uint32_t PlayerShopManager::getItemWeightForCount(const Item* item, uint32_t count)
{
	if (!item) {
		return 0;
	}
	if (item->isStackable()) {
		return item->getBaseWeight() * count;
	}
	return item->getWeight();
}

uint32_t PlayerShopManager::estimateFreedMoneyWeight(Player* player, uint64_t money)
{
	if (!player || money == 0) {
		return 0;
	}

	Cylinder* rootCylinder = player;
	std::vector<Container*> containers;
	std::multimap<uint32_t, Item*> moneyMap;
	for (size_t i = rootCylinder->getFirstIndex(), j = rootCylinder->getLastIndex(); i < j; ++i) {
		Thing* thing = rootCylinder->getThing(i);
		Item* item = thing ? thing->getItem() : nullptr;
		if (!item) {
			continue;
		}
		if (Container* container = item->getContainer()) {
			containers.push_back(container);
		} else if (item->getWorth() != 0) {
			moneyMap.emplace(item->getWorth(), item);
		}
	}

	size_t i = 0;
	while (i < containers.size()) {
		Container* container = containers[i++];
		for (Item* item : container->getItemList()) {
			if (Container* tmpContainer = item->getContainer()) {
				containers.push_back(tmpContainer);
			} else if (item->getWorth() != 0) {
				moneyMap.emplace(item->getWorth(), item);
			}
		}
	}

	uint32_t freedWeight = 0;
	uint64_t remaining = money;
	for (const auto& moneyEntry : moneyMap) {
		Item* item = moneyEntry.second;
		if (moneyEntry.first < remaining) {
			freedWeight += item->getWeight();
			remaining -= moneyEntry.first;
			continue;
		}

		const uint32_t worthPerUnit = moneyEntry.first / std::max<uint32_t>(1, item->getItemCount());
		if (worthPerUnit == 0) {
			break;
		}
		const uint32_t removeCount = static_cast<uint32_t>(std::ceil(remaining / static_cast<double>(worthPerUnit)));
		freedWeight += item->getBaseWeight() * removeCount;
		break;
	}
	return freedWeight;
}

PlayerShopManager::Shop* PlayerShopManager::getShop(Player* player)
{
	if (!player) {
		return nullptr;
	}
	return getShopBySellerId(player->getID());
}

const PlayerShopManager::Shop* PlayerShopManager::getShop(const Player* player) const
{
	if (!player) {
		return nullptr;
	}
	auto it = shops.find(player->getID());
	return it != shops.end() ? &it->second : nullptr;
}

PlayerShopManager::Shop* PlayerShopManager::getShopBySellerId(uint32_t sellerId)
{
	auto it = shops.find(sellerId);
	return it != shops.end() ? &it->second : nullptr;
}

bool PlayerShopManager::isInShop(const Player* player) const
{
	return getShop(player) != nullptr;
}

bool PlayerShopManager::isActiveSeller(const Player* player) const
{
	const Shop* shop = getShop(player);
	return shop && shop->state == PlayerShopState::Active;
}

bool PlayerShopManager::hasShopAtAdjacentPosition(const Position& position, const Player* exceptPlayer) const
{
	for (const auto& entry : shops) {
		const Shop& shop = entry.second;
		if (exceptPlayer && shop.sellerId == exceptPlayer->getID()) {
			continue;
		}
		if (isSameOrNear(position, shop.position, 1)) {
			return true;
		}
	}
	return false;
}

bool PlayerShopManager::validateTile(const Player* player) const
{
	if (!player) {
		return false;
	}

	const Tile* tile = player->getTile();
	if (!tile || !tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
		return false;
	}

	if (const Item* ground = tile->getGround(); ground && ground->getActionId() == PLAYER_SHOP_TILE_ACTIONID) {
		return true;
	}

	if (const TileItemVector* items = tile->getItemList()) {
		for (const Item* item : *items) {
			if (item && item->getActionId() == PLAYER_SHOP_TILE_ACTIONID) {
				return true;
			}
		}
	}
	return false;
}

bool PlayerShopManager::validateConditions(const Player* player, std::string& reason) const
{
	if (!player || player->getHealth() <= 0 || player->isRemoved()) {
		reason = "You cannot create a store here.";
		return false;
	}

	if (player->hasCondition(CONDITION_INFIGHT)) {
		reason = "You cannot create a store while in battle.";
		return false;
	}

	const std::array<ConditionType_t, 7> blockedConditions = {
		CONDITION_POISON, CONDITION_FIRE, CONDITION_ENERGY, CONDITION_BLEEDING,
		CONDITION_DROWN, CONDITION_PARALYZE, CONDITION_HASTE
	};
	for (ConditionType_t condition : blockedConditions) {
		if (player->hasCondition(condition)) {
			reason = "You cannot create a store while affected by a condition.";
			return false;
		}
	}

	if (player->getTradeState() != TRADE_NONE || player->getTradeItem()) {
		reason = "You cannot create a store while trading.";
		return false;
	}

	int32_t onBuy = -1;
	int32_t onSell = -1;
	if (player->getShopOwner(onBuy, onSell) != nullptr) {
		reason = "You cannot create a store while trading with an NPC.";
		return false;
	}

	return true;
}

bool PlayerShopManager::validateMainBackpack(Player* player, Container*& mainBackpack, Item*& mainBackpackItem, std::string& reason) const
{
	mainBackpack = nullptr;
	mainBackpackItem = nullptr;
	if (!player) {
		reason = "You cannot create a store here.";
		return false;
	}

	mainBackpackItem = player->getInventoryItem(CONST_SLOT_BACKPACK);
	mainBackpack = mainBackpackItem ? mainBackpackItem->getContainer() : nullptr;
	if (!mainBackpackItem || !mainBackpack) {
		reason = "You need a main backpack to create a store.";
		return false;
	}
	return true;
}

bool PlayerShopManager::validateStart(Player* player, std::string& reason) const
{
	if (!player) {
		reason = "You cannot create a store here.";
		return false;
	}

	if (isInShop(player)) {
		reason = "You already have a store open.";
		return false;
	}

	if (!validateTile(player)) {
		reason = "You cannot create a store here.";
		return false;
	}

	if (!validateConditions(player, reason)) {
		return false;
	}

	Container* mainBackpack = nullptr;
	Item* mainBackpackItem = nullptr;
	if (!validateMainBackpack(player, mainBackpack, mainBackpackItem, reason)) {
		return false;
	}

	if (hasShopAtAdjacentPosition(player->getPosition(), player)) {
		reason = "A store has already been created around you, please look for another location.";
		return false;
	}

	return true;
}

bool PlayerShopManager::beginConfigure(Player* player)
{
	if (!checkRateLimit(player, configureRateLimits, PLAYER_SHOP_CONFIG_RATE_LIMIT_MS)) {
		debugLog("begin throttled player=" + (player ? player->getName() : std::string("<null>")));
		return false;
	}

	std::string reason;
	if (!validateStart(player, reason)) {
		sendPopup(player, reason);
		debugLog("begin rejected player=" + (player ? player->getName() : std::string("<null>")) + " reason=\"" + reason + "\"");
		return false;
	}

	Container* mainBackpack = nullptr;
	Item* mainBackpackItem = nullptr;
	if (!validateMainBackpack(player, mainBackpack, mainBackpackItem, reason)) {
		sendPopup(player, reason);
		return false;
	}

	Shop shop;
	shop.sellerId = player->getID();
	shop.sellerGuid = player->getGUID();
	shop.state = PlayerShopState::Configuring;
	shop.position = player->getPosition();
	shop.mainBackpack = mainBackpack;
	shop.mainBackpackItem = mainBackpackItem;
	shops[player->getID()] = shop;
	player->setPlayerShopState(static_cast<uint8_t>(PlayerShopState::Configuring));

	debugLog("begin player=" + player->getName());
	sendWindow(player, shops[player->getID()], "config");
	scheduleTimeout(player->getID());
	scheduleMaintenance(player->getID());
	return true;
}

void PlayerShopManager::handleOpcode(Player* player, const std::string& buffer)
{
	if (!player || buffer.empty()) {
		return;
	}
	if (buffer.size() > PLAYER_SHOP_MAX_OPCODE_BUFFER) {
		debugLog("reject opcode oversized player=" + player->getName() + " size=" + std::to_string(buffer.size()));
		return;
	}
	if (!checkRateLimit(player, opcodeRateLimits, PLAYER_SHOP_OPCODE_RATE_LIMIT_MS)) {
		debugLog("reject opcode throttled player=" + player->getName());
		return;
	}

	std::vector<std::string> parts = split(buffer, '|');
	if (parts.empty()) {
		return;
	}

	const std::string& action = parts[0];
	if (action == "add") {
		addOffer(player, parts);
	} else if (action == "confirm") {
		confirm(player, parts);
	} else if (action == "open") {
		openBuyer(player, parts);
	} else if (action == "buy") {
		buy(player, parts);
	} else if (action == "cancel") {
		cancelByOpcode(player);
	} else {
		debugLog("reject opcode unknown player=" + player->getName() + " action=" + action);
	}
}

bool PlayerShopManager::validateOffer(Player* player, Shop& shop, Offer& offer, std::string& reason, bool clearInvalid)
{
	auto clearOffer = [&]() {
		if (clearInvalid) {
			offer = Offer();
		}
	};

	if (!offer.active || !offer.item) {
		reason = "Invalid offer.";
		clearOffer();
		return false;
	}

	Container* currentMainBackpack = nullptr;
	Item* currentMainBackpackItem = nullptr;
	if (!validateMainBackpack(player, currentMainBackpack, currentMainBackpackItem, reason) ||
		currentMainBackpack != shop.mainBackpack || currentMainBackpackItem != shop.mainBackpackItem) {
		reason = "Main backpack changed.";
		clearOffer();
		return false;
	}

	Item* item = offer.item;
	if (item->isRemoved() || item->getID() != offer.itemId || item->getClientID() != offer.clientId ||
		item->getParent() != shop.mainBackpack || item->getTopParent() != player || item->getContainer()) {
		reason = "Listed item is no longer valid.";
		clearOffer();
		return false;
	}

	if (item->isStackable()) {
		if (offer.count == 0 || offer.count > item->getItemCount()) {
			reason = "Listed item count is no longer valid.";
			clearOffer();
			return false;
		}
	} else if (offer.count != 1) {
		reason = "Invalid item count.";
		clearOffer();
		return false;
	}

	if (offer.price == 0 || offer.price > PLAYER_SHOP_MAX_PRICE) {
		reason = "Invalid price.";
		clearOffer();
		return false;
	}
	return true;
}

bool PlayerShopManager::hasActiveOffers(const Shop& shop) const
{
	for (const Offer& offer : shop.offers) {
		if (offer.active) {
			return true;
		}
	}
	return false;
}

int32_t PlayerShopManager::findFirstFreeOfferSlot(const Shop& shop) const
{
	for (uint8_t i = 0; i < PLAYER_SHOP_MAX_OFFERS; ++i) {
		if (!shop.offers[i].active) {
			return i;
		}
	}
	return -1;
}

bool PlayerShopManager::compactOffers(Shop& shop)
{
	std::array<Offer, PLAYER_SHOP_MAX_OFFERS> compactedOffers;
	uint8_t nextSlot = 0;
	bool changed = false;

	for (uint8_t i = 0; i < PLAYER_SHOP_MAX_OFFERS; ++i) {
		Offer offer = shop.offers[i];
		if (!offer.active) {
			continue;
		}

		if (offer.slot != nextSlot || i != nextSlot) {
			changed = true;
		}
		offer.slot = nextSlot;
		compactedOffers[nextSlot++] = offer;
	}

	if (changed) {
		shop.offers = compactedOffers;
	}
	return changed;
}

void PlayerShopManager::addOffer(Player* player, const std::vector<std::string>& parts)
{
	Shop* shop = getShop(player);
	if (!player || !shop || shop->state != PlayerShopState::Configuring || parts.size() < 9) {
		sendPopup(player, "Invalid store state.");
		return;
	}

	bool ok = false;
	parseUnsigned32(parts[1], ok);
	if (!ok) {
		sendPopup(player, "Invalid store slot.");
		return;
	}

	Position itemPos;
	itemPos.x = static_cast<uint16_t>(parseUnsigned32(parts[2], ok));
	if (!ok) {
		sendPopup(player, "Invalid item.");
		return;
	}
	itemPos.y = static_cast<uint16_t>(parseUnsigned32(parts[3], ok));
	if (!ok) {
		sendPopup(player, "Invalid item.");
		return;
	}
	itemPos.z = static_cast<uint8_t>(parseUnsigned32(parts[4], ok));
	if (!ok) {
		sendPopup(player, "Invalid item.");
		return;
	}
	const uint32_t stackPos = parseUnsigned32(parts[5], ok);
	if (!ok || stackPos > 255) {
		sendPopup(player, "Invalid item.");
		return;
	}
	const uint32_t clientId = parseUnsigned32(parts[6], ok);
	if (!ok || clientId > std::numeric_limits<uint16_t>::max()) {
		sendPopup(player, "Invalid item.");
		return;
	}
	const uint32_t count = parseUnsigned32(parts[7], ok);
	if (!ok || count == 0 || count > 100) {
		sendPopup(player, "Invalid amount.");
		return;
	}
	const uint64_t price = parseUnsigned64(parts[8], ok);
	if (!ok || price == 0 || price > PLAYER_SHOP_MAX_PRICE) {
		sendPopup(player, "Invalid price.");
		return;
	}

	uint8_t index = 0;
	if (itemPos.x == 0xFFFF) {
		index = (itemPos.y & 0x40) ? itemPos.z : static_cast<uint8_t>(itemPos.y);
	} else {
		index = static_cast<uint8_t>(stackPos);
	}

	Thing* thing = g_game.internalGetThing(player, itemPos, index, static_cast<uint16_t>(clientId), STACKPOS_MOVE);
	Item* item = thing ? thing->getItem() : nullptr;
	if (!item || item->getClientID() != clientId) {
		sendPopup(player, "Invalid item.");
		return;
	}

	if (item->getContainer()) {
		sendPopup(player, "Containers cannot be listed.");
		return;
	}

	if (item->getParent() != shop->mainBackpack || item->getTopParent() != player) {
		sendPopup(player, "Only items directly inside your main backpack can be listed.");
		return;
	}

	const uint16_t finalCount = item->isStackable() ? static_cast<uint16_t>(count) : 1;
	if (!item->isStackable() && count != 1) {
		sendPopup(player, "Invalid amount.");
		return;
	}
	if (item->isStackable() && finalCount > item->getItemCount()) {
		sendPopup(player, "Invalid amount.");
		return;
	}

	for (const Offer& existing : shop->offers) {
		if (existing.active && existing.item == item) {
			sendPopup(player, "This item is already listed.");
			return;
		}
	}

	const int32_t freeSlot = findFirstFreeOfferSlot(*shop);
	if (freeSlot < 0) {
		sendPopup(player, "There are no free store slots.");
		return;
	}

	Offer& offer = shop->offers[freeSlot];
	offer.active = true;
	offer.slot = static_cast<uint8_t>(freeSlot);
	offer.item = item;
	offer.itemId = item->getID();
	offer.clientId = item->getClientID();
	offer.count = finalCount;
	offer.price = price;

	debugLog("add player=" + player->getName() + " slot=" + std::to_string(freeSlot) +
		" itemId=" + std::to_string(offer.itemId) + " count=" + std::to_string(offer.count) +
		" price=" + std::to_string(offer.price));
	sendWindow(player, *shop, "config");
}

void PlayerShopManager::confirm(Player* player, const std::vector<std::string>& parts)
{
	Shop* shop = getShop(player);
	if (!player || !shop || shop->state != PlayerShopState::Configuring) {
		sendPopup(player, "Invalid store state.");
		return;
	}

	std::string reason;
	if (!validateTile(player)) {
		cancel(player, "invalid_tile");
		sendPopup(player, "You cannot create a store here.");
		return;
	}
	if (hasShopAtAdjacentPosition(player->getPosition(), player)) {
		cancel(player, "adjacent_store");
		sendPopup(player, "A store has already been created around you, please look for another location.");
		return;
	}
	if (!validateConditions(player, reason)) {
		sendPopup(player, reason);
		return;
	}

	shop->description = sanitizeDescription(parts.size() >= 2 ? parts[1] : "description");
	removeInvalidOffers(player, *shop);

	bool hasOffer = false;
	for (const Offer& offer : shop->offers) {
		if (offer.active) {
			hasOffer = true;
			break;
		}
	}
	if (!hasOffer) {
		sendPopup(player, "You need to list at least one item.");
		return;
	}

	shop->state = PlayerShopState::Active;
	player->setPlayerShopState(static_cast<uint8_t>(PlayerShopState::Active));
	debugLog("confirm player=" + player->getName() + " description=\"" + shop->description + "\"");
	sendWindow(player, *shop, "seller");
	broadcastLabel(*shop, true);
	notifyShopUpdated(*shop);
}

void PlayerShopManager::openBuyer(Player* buyer, const std::vector<std::string>& parts)
{
	if (!buyer || parts.size() < 2) {
		return;
	}

	bool ok = false;
	const uint32_t sellerId = parseUnsigned32(parts[1], ok);
	Player* seller = ok ? g_game.getPlayerByID(sellerId) : nullptr;
	Shop* shop = seller ? getShop(seller) : nullptr;
	if (!seller || !shop || shop->state != PlayerShopState::Active) {
		sendPopup(buyer, "This store is no longer available.");
		return;
	}

	if (seller == buyer) {
		sendWindow(buyer, *shop, "seller");
		return;
	}

	if (!isSameOrNear(buyer->getPosition(), seller->getPosition(), 1)) {
		sendPopup(buyer, "You are too far away.");
		return;
	}

	shop->viewers.insert(buyer->getID());
	debugLog("open buyer=" + buyer->getName() + " seller=" + seller->getName());
	sendWindow(buyer, *shop, "buyer");
}

void PlayerShopManager::buy(Player* buyer, const std::vector<std::string>& parts)
{
	if (!buyer || parts.size() < 4) {
		return;
	}

	bool ok = false;
	const uint32_t sellerId = parseUnsigned32(parts[1], ok);
	if (!ok) {
		sendPopup(buyer, "Invalid store.");
		return;
	}
	const uint32_t slotValue = parseUnsigned32(parts[2], ok);
	if (!ok || slotValue >= PLAYER_SHOP_MAX_OFFERS) {
		sendPopup(buyer, "Invalid offer.");
		return;
	}
	const uint32_t requestedCount = parseUnsigned32(parts[3], ok);
	if (!ok || requestedCount == 0 || requestedCount > 100) {
		sendPopup(buyer, "Invalid amount.");
		return;
	}

	Player* seller = g_game.getPlayerByID(sellerId);
	Shop* shop = seller ? getShop(seller) : nullptr;
	if (!seller || !shop || shop->state != PlayerShopState::Active) {
		sendPopup(buyer, "This store is no longer available.");
		return;
	}

	const auto refreshBuyerWindow = [&]() {
		sendWindow(buyer, *shop, "buyer");
	};

	const auto failBuy = [&](const std::string& message, const bool refreshWindow = true) {
		shop->buying = false;
		if (refreshWindow) {
			refreshBuyerWindow();
		}
		sendPopup(buyer, message);
	};

	if (seller == buyer) {
		sendPopup(buyer, "You cannot buy from your own store.");
		return;
	}
	if (shop->buying) {
		sendPopup(buyer, "Store is busy, try again.");
		return;
	}
	if (!isSameOrNear(buyer->getPosition(), seller->getPosition(), 1)) {
		sendClose(buyer, sellerId);
		sendPopup(buyer, "You are too far away.");
		return;
	}

	shop->buying = true;
	Offer& offer = shop->offers[slotValue];
	std::string reason;
	if (!validateOffer(seller, *shop, offer, reason, true)) {
		compactOffers(*shop);
		shop->buying = false;
		if (!hasActiveOffers(*shop)) {
			cancel(seller, "no_offers", true, true);
			sendPopup(buyer, "This store is no longer available.");
			return;
		}
		notifyShopUpdated(*shop);
		sendPopup(buyer, reason);
		return;
	}

	const uint32_t count = offer.item->isStackable() ? requestedCount : 1;
	if (!offer.item->isStackable() && requestedCount != 1) {
		failBuy("Invalid amount.");
		return;
	}
	if (count > offer.count || count > offer.item->getItemCount()) {
		failBuy("Invalid amount.");
		return;
	}

	if (offer.price > std::numeric_limits<uint64_t>::max() / count) {
		failBuy("Invalid price.");
		return;
	}
	const uint64_t totalPrice = offer.price * count;
	if (buyer->getMoney() < totalPrice) {
		failBuy("You do not have enough money.");
		return;
	}
	if (seller->getBankBalance() > std::numeric_limits<uint64_t>::max() - totalPrice) {
		failBuy("Seller bank balance limit reached.");
		return;
	}

	Item* testItem = Item::CreateItem(offer.itemId, offer.item->isStackable() ? count : 1);
	if (!testItem) {
		failBuy("Invalid item.");
		return;
	}
	const uint32_t requiredWeight = testItem->getWeight();
	const uint32_t freedMoneyWeight = estimateFreedMoneyWeight(buyer, totalPrice);
	if (buyer->getFreeCapacity() != std::numeric_limits<uint32_t>::max() &&
		buyer->getFreeCapacity() + freedMoneyWeight < requiredWeight) {
		g_game.ReleaseItem(testItem);
		failBuy("You do not have enough capacity.");
		return;
	}

	ReturnValue testAdd = g_game.internalAddItem(buyer, testItem, INDEX_WHEREEVER, 0, true);
	g_game.ReleaseItem(testItem);
	if (testAdd != RETURNVALUE_NOERROR) {
		failBuy(getReturnMessage(testAdd));
		return;
	}

	if (!g_game.removeMoney(buyer, totalPrice)) {
		failBuy("You do not have enough money.");
		return;
	}

	Item* movedItem = nullptr;
	Cylinder* sellerContainer = offer.item ? offer.item->getParent() : nullptr;
	ReturnValue moveRet = g_game.internalMoveItem(shop->mainBackpack, buyer, INDEX_WHEREEVER, offer.item, count, &movedItem, 0);
	if (moveRet != RETURNVALUE_NOERROR) {
		g_game.addMoney(buyer, totalPrice);
		failBuy(getReturnMessage(moveRet));
		return;
	}
	g_game.attributeContainerMutation(sellerContainer, seller->getGUID());

	seller->setBankBalance(seller->getBankBalance() + totalPrice);
	if (offer.item->isStackable() && offer.item && !offer.item->isRemoved() && offer.item->getParent() == shop->mainBackpack && offer.item->getItemCount() > 0) {
		offer.count = std::min<uint16_t>(offer.count - count, offer.item->getItemCount());
		if (offer.count == 0) {
			offer = Offer();
			compactOffers(*shop);
		}
	} else {
		offer = Offer();
		compactOffers(*shop);
	}

	if (PLAYER_SHOP_SAVE_AFTER_BUY) {
		IOLoginData::savePlayer(buyer);
		IOLoginData::savePlayer(seller);
	}

	debugLog("buy buyer=" + buyer->getName() + " seller=" + seller->getName() +
		" slot=" + std::to_string(slotValue) + " count=" + std::to_string(count) +
		" total=" + std::to_string(totalPrice));
	shop->buying = false;
	if (!hasActiveOffers(*shop)) {
		cancel(seller, "no_offers", true, true);
		return;
	}
	notifyShopUpdated(*shop);
}

void PlayerShopManager::cancelByOpcode(Player* player)
{
	if (!player) {
		return;
	}
	cancel(player, "user_cancel");
}

void PlayerShopManager::sendWindow(Player* receiver, const Shop& shop, const char* mode) const
{
	if (!receiver) {
		return;
	}

	Player* seller = g_game.getPlayerByID(shop.sellerId);
	std::ostringstream ss;
	ss << "{\"action\":\"window\",\"mode\":\"" << mode << "\",\"sellerId\":" << shop.sellerId
		<< ",\"sellerName\":\"" << jsonEscape(seller ? seller->getName() : "Unknown") << "\""
		<< ",\"description\":\"" << jsonEscape(shop.description) << "\""
		<< ",\"buyerMoney\":" << receiver->getMoney()
		<< ",\"maxPrice\":" << PLAYER_SHOP_MAX_PRICE << ",\"offers\":[";

	bool first = true;
	for (const Offer& offer : shop.offers) {
		if (!offer.active || !offer.item) {
			continue;
		}
		if (!first) {
			ss << ',';
		}
		first = false;
		ss << "{\"slot\":" << static_cast<uint32_t>(offer.slot)
			<< ",\"itemId\":" << offer.clientId
			<< ",\"serverId\":" << offer.itemId
			<< ",\"count\":" << offer.count
			<< ",\"price\":" << offer.price
			<< ",\"stackable\":" << (offer.item->isStackable() ? "true" : "false")
			<< ",\"name\":\"" << jsonEscape(offer.item->getNameDescription()) << "\""
			<< ",\"look\":\"" << jsonEscape(offer.item->getDescription(0)) << "\""
			<< ",\"weight\":" << getItemWeightForCount(offer.item, offer.count)
			<< "}";
	}
	ss << "]}";
	receiver->sendExtendedOpcode(PLAYER_SHOP_OPCODE, ss.str());
}

void PlayerShopManager::sendClose(Player* receiver, uint32_t sellerId) const
{
	if (!receiver) {
		return;
	}
	receiver->sendExtendedOpcode(PLAYER_SHOP_OPCODE, "{\"action\":\"close\",\"sellerId\":" + std::to_string(sellerId) + "}");
}

void PlayerShopManager::sendPopup(Player* receiver, const std::string& message) const
{
	if (!receiver) {
		return;
	}
	receiver->sendExtendedOpcode(PLAYER_SHOP_OPCODE, "{\"action\":\"popup\",\"message\":\"" + jsonEscape(message) + "\"}");
	receiver->sendCancelMessage(message);
}

void PlayerShopManager::sendLabel(Player* receiver, const Shop& shop, bool active) const
{
	if (!receiver) {
		return;
	}

	std::ostringstream ss;
	ss << "{\"action\":\"label\",\"sellerId\":" << shop.sellerId
		<< ",\"active\":" << (active ? "true" : "false")
		<< ",\"text\":\"" << jsonEscape(shop.description) << "\"}";
	receiver->sendExtendedOpcode(PLAYER_SHOP_OPCODE, ss.str());
}

void PlayerShopManager::broadcastLabel(const Shop& shop, bool active) const
{
	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, shop.position, false, true, 8, 8, 6, 6);
	for (Creature* spectator : spectators) {
		if (Player* player = spectator->getPlayer()) {
			sendLabel(player, shop, active);
		}
	}
}

void PlayerShopManager::notifyShopUpdated(Shop& shop)
{
	Player* seller = g_game.getPlayerByID(shop.sellerId);
	if (seller) {
		sendWindow(seller, shop, shop.state == PlayerShopState::Active ? "seller" : "config");
	}

	for (auto it = shop.viewers.begin(); it != shop.viewers.end();) {
		Player* viewer = g_game.getPlayerByID(*it);
		if (!viewer || !seller || !isSameOrNear(viewer->getPosition(), seller->getPosition(), 1)) {
			if (viewer) {
				sendClose(viewer, shop.sellerId);
			}
			it = shop.viewers.erase(it);
			continue;
		}
		sendWindow(viewer, shop, "buyer");
		++it;
	}
}

void PlayerShopManager::scheduleTimeout(uint32_t sellerId)
{
	Shop* shop = getShopBySellerId(sellerId);
	if (!shop) {
		return;
	}
	g_scheduler.stopEvent(shop->timeoutEvent);
	shop->timeoutEvent = g_scheduler.addEvent(createSchedulerTask(PLAYER_SHOP_CONFIG_TIMEOUT_MS, [sellerId]() {
		g_playerShop.runTimeout(sellerId);
	}));
}

void PlayerShopManager::scheduleMaintenance(uint32_t sellerId)
{
	Shop* shop = getShopBySellerId(sellerId);
	if (!shop) {
		return;
	}
	g_scheduler.stopEvent(shop->maintenanceEvent);
	shop->maintenanceEvent = g_scheduler.addEvent(createSchedulerTask(PLAYER_SHOP_MAINTENANCE_MS, [sellerId]() {
		g_playerShop.runMaintenance(sellerId);
	}));
}

void PlayerShopManager::runTimeout(uint32_t sellerId)
{
	Player* seller = g_game.getPlayerByID(sellerId);
	Shop* shop = getShopBySellerId(sellerId);
	if (!seller || !shop || shop->state != PlayerShopState::Configuring) {
		return;
	}

	debugLog("timeout player=" + seller->getName());
	cancel(seller, "config_timeout", true, false);
}

bool PlayerShopManager::revalidateActiveState(Player* seller, Shop& shop, std::string& reason)
{
	if (!seller || seller->getHealth() <= 0 || seller->isRemoved()) {
		reason = "invalid_seller";
		return false;
	}
	if (seller->getPosition() != shop.position) {
		reason = "seller_moved";
		return false;
	}
	if (!validateTile(seller)) {
		reason = "invalid_tile";
		return false;
	}
	if (!validateConditions(seller, reason)) {
		return false;
	}

	Container* mainBackpack = nullptr;
	Item* mainBackpackItem = nullptr;
	if (!validateMainBackpack(seller, mainBackpack, mainBackpackItem, reason) ||
		mainBackpack != shop.mainBackpack || mainBackpackItem != shop.mainBackpackItem) {
		reason = "main_backpack_changed";
		return false;
	}
	return true;
}

void PlayerShopManager::runMaintenance(uint32_t sellerId)
{
	Player* seller = g_game.getPlayerByID(sellerId);
	Shop* shop = getShopBySellerId(sellerId);
	if (!seller || !shop) {
		return;
	}

	std::string reason;
	if (!revalidateActiveState(seller, *shop, reason)) {
		cancel(seller, reason);
		return;
	}

	if (shop->state == PlayerShopState::Active) {
		const bool offersChanged = removeInvalidOffers(seller, *shop);
		if (!hasActiveOffers(*shop)) {
			cancel(seller, "no_offers", true, true);
			return;
		}
		bool viewersChanged = false;
		for (auto it = shop->viewers.begin(); it != shop->viewers.end();) {
			Player* viewer = g_game.getPlayerByID(*it);
			if (!viewer || !isSameOrNear(viewer->getPosition(), seller->getPosition(), 1)) {
				if (viewer) {
					sendClose(viewer, shop->sellerId);
				}
				it = shop->viewers.erase(it);
				viewersChanged = true;
				continue;
			}
			++it;
		}
		if (offersChanged) {
			notifyShopUpdated(*shop);
		} else if (viewersChanged) {
			sendWindow(seller, *shop, "seller");
		}
	}

	scheduleMaintenance(sellerId);
}

bool PlayerShopManager::removeInvalidOffers(Player* seller, Shop& shop)
{
	bool changed = false;
	for (Offer& offer : shop.offers) {
		if (!offer.active) {
			continue;
		}
		const bool wasActive = offer.active;
		std::string reason;
		validateOffer(seller, shop, offer, reason, true);
		if (wasActive && !offer.active) {
			changed = true;
		}
	}
	if (changed) {
		compactOffers(shop);
	}
	return changed;
}

void PlayerShopManager::cancel(Player* player, const std::string& reason, bool silent, bool kick)
{
	if (!player) {
		return;
	}

	auto it = shops.find(player->getID());
	if (it == shops.end()) {
		if (kick) {
			player->kickPlayer(false);
		}
		return;
	}

	Shop shop = it->second;
	const bool wasActive = shop.state == PlayerShopState::Active;
	const bool alreadyLeavingWorld = reason == "disappear" || reason == "death";
	g_scheduler.stopEvent(shop.timeoutEvent);
	g_scheduler.stopEvent(shop.maintenanceEvent);
	shops.erase(it);
	player->setPlayerShopState(static_cast<uint8_t>(PlayerShopState::None));

	debugLog("cancel player=" + player->getName() + " reason=" + reason);
	for (uint32_t viewerId : shop.viewers) {
		if (Player* viewer = g_game.getPlayerByID(viewerId)) {
			sendClose(viewer, shop.sellerId);
		}
	}
	sendClose(player, shop.sellerId);
	broadcastLabel(shop, false);
	if (player->getPosition() != shop.position) {
		shop.position = player->getPosition();
		broadcastLabel(shop, false);
	}

	if (!silent) {
		player->sendCancelMessage("Store closed.");
	}
	if ((wasActive && !alreadyLeavingWorld) || kick) {
		IOLoginData::savePlayer(player);
		player->kickPlayer(false);
	}
}

void PlayerShopManager::onPlayerDisappear(Player* player)
{
	if (!player) {
		return;
	}

	clearRateLimits(player->getID());
	if (isInShop(player)) {
		cancel(player, "disappear", true);
	}
}

void PlayerShopManager::onCreatureAppear(Player* receiver, const Player* seenPlayer)
{
	const Shop* shop = getShop(seenPlayer);
	if (receiver && shop && shop->state == PlayerShopState::Active) {
		sendLabel(receiver, *shop, true);
	}
}

void PlayerShopManager::syncVisibleLabels(Player* receiver) const
{
	if (!receiver) {
		return;
	}

	const Position& receiverPosition = receiver->getPosition();
	const int32_t receiverX = receiverPosition.x;
	const int32_t receiverY = receiverPosition.y;
	for (const auto& entry : shops) {
		const Shop& shop = entry.second;
		if (shop.state != PlayerShopState::Active || shop.sellerId == receiver->getID()) {
			continue;
		}
		if (shop.position.z != receiverPosition.z) {
			continue;
		}
		if (static_cast<int32_t>(shop.position.x) < receiverX - Map::maxClientViewportX ||
			static_cast<int32_t>(shop.position.x) > receiverX + Map::maxClientViewportX + 1 ||
			static_cast<int32_t>(shop.position.y) < receiverY - Map::maxClientViewportY ||
			static_cast<int32_t>(shop.position.y) > receiverY + Map::maxClientViewportY + 1) {
			continue;
		}
		sendLabel(receiver, shop, true);
	}
}

void PlayerShopManager::onPlayerMoved(Player* player)
{
	if (player && isInShop(player)) {
		cancel(player, "moved");
	}
}

bool PlayerShopManager::shouldBlockMovement(const Player* player) const
{
	return isInShop(player);
}

bool PlayerShopManager::shouldBlockInventoryAction(const Player* player) const
{
	return isInShop(player);
}

bool PlayerShopManager::shouldBlockDefaultChat(const Player* player) const
{
	return isInShop(player);
}

bool PlayerShopManager::checkRateLimit(Player* player, std::map<uint32_t, int64_t>& rateLimits, uint32_t cooldownMs)
{
	if (!player) {
		return false;
	}

	const int64_t now = OTSYS_TIME();
	const uint32_t playerId = player->getID();
	auto it = rateLimits.find(playerId);
	if (it != rateLimits.end() && now < it->second) {
		return false;
	}

	rateLimits[playerId] = now + cooldownMs;
	return true;
}

void PlayerShopManager::clearRateLimits(uint32_t playerId)
{
	configureRateLimits.erase(playerId);
	opcodeRateLimits.erase(playerId);
}
