/**
 * Redemption player shop.
 * Online-only first version, isolated behind extended opcode 202.
 */

#ifndef FS_PLAYERSHOP_H
#define FS_PLAYERSHOP_H

#include "position.h"

#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

class Container;
class Item;
class Player;

static constexpr uint8_t PLAYER_SHOP_OPCODE = 202;
static constexpr uint16_t PLAYER_SHOP_TILE_ACTIONID = 45001;
static constexpr uint8_t PLAYER_SHOP_MAX_OFFERS = 20;
static constexpr uint64_t PLAYER_SHOP_MAX_PRICE = 1000000000;

enum class PlayerShopState : uint8_t {
	None = 0,
	Configuring = 1,
	Active = 2,
};

class PlayerShopManager
{
	public:
		bool beginConfigure(Player* player);
		void handleOpcode(Player* player, const std::string& buffer);

		bool isInShop(const Player* player) const;
		bool isActiveSeller(const Player* player) const;
		bool hasShopAtAdjacentPosition(const Position& position, const Player* exceptPlayer = nullptr) const;

		void cancel(Player* player, const std::string& reason, bool silent = false, bool kick = false);
		void onPlayerDisappear(Player* player);
		void onCreatureAppear(Player* receiver, const Player* seenPlayer);
		void onPlayerMoved(Player* player);
		void syncVisibleLabels(Player* receiver) const;

		bool shouldBlockMovement(const Player* player) const;
		bool shouldBlockInventoryAction(const Player* player) const;
		bool shouldBlockDefaultChat(const Player* player) const;

	private:
		struct Offer {
			bool active = false;
			uint8_t slot = 0;
			Item* item = nullptr;
			uint16_t itemId = 0;
			uint16_t clientId = 0;
			uint16_t count = 0;
			uint64_t price = 0;
		};

		struct Shop {
			uint32_t sellerId = 0;
			uint32_t sellerGuid = 0;
			PlayerShopState state = PlayerShopState::None;
			Position position;
			Item* mainBackpackItem = nullptr;
			Container* mainBackpack = nullptr;
			std::string description = "Shop";
			std::array<Offer, PLAYER_SHOP_MAX_OFFERS> offers;
			std::set<uint32_t> viewers;
			uint32_t timeoutEvent = 0;
			uint32_t maintenanceEvent = 0;
			bool buying = false;
		};

		Shop* getShop(Player* player);
		const Shop* getShop(const Player* player) const;
		Shop* getShopBySellerId(uint32_t sellerId);

		bool validateStart(Player* player, std::string& reason) const;
		bool validateTile(const Player* player) const;
		bool validateConditions(const Player* player, std::string& reason) const;
		bool validateMainBackpack(Player* player, Container*& mainBackpack, Item*& mainBackpackItem, std::string& reason) const;
		bool validateOffer(Player* player, Shop& shop, Offer& offer, std::string& reason, bool clearInvalid);
		bool hasActiveOffers(const Shop& shop) const;
		int32_t findFirstFreeOfferSlot(const Shop& shop) const;
		bool compactOffers(Shop& shop);

		void addOffer(Player* player, const std::vector<std::string>& parts);
		void confirm(Player* player, const std::vector<std::string>& parts);
		void openBuyer(Player* buyer, const std::vector<std::string>& parts);
		void buy(Player* buyer, const std::vector<std::string>& parts);
		void cancelByOpcode(Player* player);

		void sendWindow(Player* receiver, const Shop& shop, const char* mode) const;
		void sendClose(Player* receiver, uint32_t sellerId) const;
		void sendPopup(Player* receiver, const std::string& message) const;
		void broadcastLabel(const Shop& shop, bool active) const;
		void sendLabel(Player* receiver, const Shop& shop, bool active) const;
		void notifyShopUpdated(Shop& shop);

		void scheduleTimeout(uint32_t sellerId);
		void scheduleMaintenance(uint32_t sellerId);
		void runTimeout(uint32_t sellerId);
		void runMaintenance(uint32_t sellerId);

		bool revalidateActiveState(Player* seller, Shop& shop, std::string& reason);
		bool removeInvalidOffers(Player* seller, Shop& shop);

		static std::vector<std::string> split(const std::string& text, char separator);
		static std::string sanitizeDescription(const std::string& text);
		static std::string jsonEscape(const std::string& text);
		static uint64_t parseUnsigned64(const std::string& text, bool& ok);
		static uint32_t parseUnsigned32(const std::string& text, bool& ok);
		static uint32_t getItemWeightForCount(const Item* item, uint32_t count);
		static uint32_t estimateFreedMoneyWeight(Player* player, uint64_t money);
		bool checkRateLimit(Player* player, std::map<uint32_t, int64_t>& rateLimits, uint32_t cooldownMs);
		void clearRateLimits(uint32_t playerId);

		std::map<uint32_t, Shop> shops;
		std::map<uint32_t, int64_t> configureRateLimits;
		std::map<uint32_t, int64_t> opcodeRateLimits;
};

extern PlayerShopManager g_playerShop;

#endif
