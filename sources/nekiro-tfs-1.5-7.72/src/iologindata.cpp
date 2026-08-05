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

#include "iologindata.h"
#include "configmanager.h"
#include "dispatchermetrics.h"
#include "game.h"
#include "ioguild.h"
#include "playeriodatabase.h"
#include "playeriomanager.h"

#include <fmt/format.h>

extern ConfigManager g_config;
extern Game g_game;

namespace {
std::string buildPlayerCoreQuery(const std::string& predicate)
{
	return "SELECT `id`, `name`, `account_id`, `group_id`, `sex`, `vocation`, "
		"`experience`, `level`, `maglevel`, `health`, `healthmax`, `blessings`, "
		"`mana`, `manamax`, `manaspent`, `soul`, `lookbody`, `lookfeet`, `lookhead`, "
		"`looklegs`, `looktype`, `lookaddons`, `posx`, `posy`, `posz`, `cap`, "
		"`lastlogin`, `lastlogout`, `lastip`, `conditions`, `skulltime`, `skull`, "
		"`town_id`, `balance`, `offlinetraining_time`, `offlinetraining_skill`, "
		"`stamina`, `skill_fist`, `skill_fist_tries`, `skill_club`, "
		"`skill_club_tries`, `skill_sword`, `skill_sword_tries`, `skill_axe`, "
		"`skill_axe_tries`, `skill_dist`, `skill_dist_tries`, `skill_shielding`, "
		"`skill_shielding_tries`, `skill_fishing`, `skill_fishing_tries`, "
		"`direction`, `save` FROM `players` WHERE " + predicate;
}
}

Account IOLoginData::loadAccount(uint32_t accno)
{
	Account account;

	DBResult_ptr result = Database::getInstance().storeQuery(fmt::format("SELECT `id`, `name`, `password`, `type`, `premium_ends_at` FROM `accounts` WHERE `id` = {:d}", accno));
	if (!result) {
		return account;
	}

	account.id = result->getNumber<uint32_t>("id");
	account.name = result->getString("name");
	account.accountType = static_cast<AccountType_t>(result->getNumber<int32_t>("type"));
	account.premiumEndsAt = result->getNumber<time_t>("premium_ends_at");
	return account;
}

std::string decodeSecret(const std::string& secret)
{
	// simple base32 decoding
	std::string key;
	key.reserve(10);

	uint32_t buffer = 0, left = 0;
	for (const auto& ch : secret) {
		buffer <<= 5;
		if (ch >= 'A' && ch <= 'Z') {
			buffer |= (ch & 0x1F) - 1;
		} else if (ch >= '2' && ch <= '7') {
			buffer |= ch - 24;
		} else {
			// if a key is broken, return empty and the comparison
			// will always be false since the token must not be empty
			return {};
		}

		left += 5;
		if (left >= 8) {
			left -= 8;
			key.push_back(static_cast<char>(buffer >> left));
		}
	}

	return key;
}

bool IOLoginData::loginserverAuthentication(const std::string& name, const std::string& password, Account& account)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `id`, `name`, `password`, `secret`, `type`, `premium_ends_at` FROM `accounts` WHERE `name` = {:s}", db.escapeString(name)));
	if (!result) {
		return false;
	}

	if (transformToSHA1(password) != result->getString("password")) {
		return false;
	}

	account.id = result->getNumber<uint32_t>("id");
	account.name = result->getString("name");
	account.key = decodeSecret(result->getString("secret"));
	account.accountType = static_cast<AccountType_t>(result->getNumber<int32_t>("type"));
	account.premiumEndsAt = result->getNumber<time_t>("premium_ends_at");

	result = db.storeQuery(fmt::format("SELECT `name` FROM `players` WHERE `account_id` = {:d} AND `deletion` = 0 ORDER BY `name` ASC", account.id));
	if (result) {
		do {
			account.characters.push_back(result->getString("name"));
		} while (result->next());
	}
	return true;
}

uint32_t IOLoginData::gameworldAuthentication(const std::string& accountName, const std::string& password, std::string& characterName, std::string&, uint32_t)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `id`, `password`, `secret` FROM `accounts` WHERE `name` = {:s}", db.escapeString(accountName)));
	if (!result) {
		return 0;
	}

	/*std::string secret = decodeSecret(result->getString("secret"));
	if (!secret.empty()) {
		if (token.empty()) {
			return 0;
		}

		bool tokenValid = token == generateToken(secret, tokenTime) || token == generateToken(secret, tokenTime - 1) || token == generateToken(secret, tokenTime + 1);
		if (!tokenValid) {
			return 0;
		}
	}*/

	if (transformToSHA1(password) != result->getString("password")) {
		return 0;
	}

	uint32_t accountId = result->getNumber<uint32_t>("id");

	result = db.storeQuery(fmt::format("SELECT `name` FROM `players` WHERE `name` = {:s} AND `account_id` = {:d} AND `deletion` = 0", db.escapeString(characterName), accountId));
	if (!result) {
		return 0;
	}

	characterName = result->getString("name");
	return accountId;
}

uint32_t IOLoginData::getAccountIdByPlayerName(const std::string& playerName)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `account_id` FROM `players` WHERE `name` = {:s}", db.escapeString(playerName)));
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("account_id");
}

uint32_t IOLoginData::getAccountIdByPlayerId(uint32_t playerId)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `account_id` FROM `players` WHERE `id` = {:d}", playerId));
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("account_id");
}

AccountType_t IOLoginData::getAccountType(uint32_t accountId)
{
	DBResult_ptr result = Database::getInstance().storeQuery(fmt::format("SELECT `type` FROM `accounts` WHERE `id` = {:d}", accountId));
	if (!result) {
		return ACCOUNT_TYPE_NORMAL;
	}
	return static_cast<AccountType_t>(result->getNumber<uint16_t>("type"));
}

void IOLoginData::setAccountType(uint32_t accountId, AccountType_t accountType)
{
	Database::getInstance().executeQuery(fmt::format("UPDATE `accounts` SET `type` = {:d} WHERE `id` = {:d}", static_cast<uint16_t>(accountType), accountId));
}

void IOLoginData::updateOnlineStatus(uint32_t guid, bool login)
{
	if (g_config.getBoolean(ConfigManager::ALLOW_CLONES)) {
		return;
	}

	if (login) {
		Database::getInstance().executeQuery(fmt::format("INSERT INTO `players_online` VALUES ({:d})", guid));
	} else {
		Database::getInstance().executeQuery(fmt::format("DELETE FROM `players_online` WHERE `player_id` = {:d}", guid));
	}
}

bool IOLoginData::preloadPlayer(Player* player, const std::string& name)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(buildPlayerPreloadQuery(name));
	if (!result) {
		return false;
	}

	player->setGUID(result->getNumber<uint32_t>("id"));
	Group* group = g_game.groups.getGroup(result->getNumber<uint16_t>("group_id"));
	if (!group) {
		std::cout << "[Error - IOLoginData::preloadPlayer] " << player->name << " has Group ID " << result->getNumber<uint16_t>("group_id") << " which doesn't exist." << std::endl;
		return false;
	}
	player->setGroup(group);
	player->accountNumber = result->getNumber<uint32_t>("account_id");
	player->accountType = static_cast<AccountType_t>(result->getNumber<uint16_t>("type"));
	player->premiumEndsAt = result->getNumber<time_t>("premium_ends_at");
	return true;
}

std::string IOLoginData::buildPlayerPreloadQuery(const std::string& name)
{
	return fmt::format(
		"SELECT `p`.`id`, `p`.`name`, `p`.`account_id`, `p`.`group_id`, `a`.`type`, "
		"`a`.`premium_ends_at` FROM `players` as `p` JOIN `accounts` as `a` "
		"ON `a`.`id` = `p`.`account_id` WHERE `p`.`name` = {:s} AND "
		"`p`.`deletion` = 0",
		makePlayerIOSqlLiteral(name));
}

std::vector<std::string> IOLoginData::buildPlayerLoadQueries(
	uint32_t playerId, uint32_t accountId)
{
	std::vector<std::string> queries;
	queries.reserve(10);
	queries.emplace_back(buildPlayerCoreQuery(
		fmt::format("`id` = {:d} AND `deletion` = 0", playerId)));
	queries.emplace_back(fmt::format(
		"SELECT `id`, `name`, `password`, `type`, `premium_ends_at` FROM "
		"`accounts` WHERE `id` = {:d}", accountId));
	queries.emplace_back(fmt::format(
		"SELECT `guild_id`, `rank_id`, `nick` FROM `guild_membership` WHERE "
		"`player_id` = {:d}", playerId));
	queries.emplace_back(fmt::format(
		"SELECT `player_id`, `name` FROM `player_spells` WHERE `player_id` = {:d}",
		playerId));
	queries.emplace_back(fmt::format(
		"SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM "
		"`player_items` WHERE `player_id` = {:d} ORDER BY `sid` DESC", playerId));
	queries.emplace_back(fmt::format(
		"SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM "
		"`player_depotlockeritems` WHERE `player_id` = {:d} ORDER BY `sid` DESC",
		playerId));
	queries.emplace_back(fmt::format(
		"SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM "
		"`player_depotitems` WHERE `player_id` = {:d} ORDER BY `sid` DESC", playerId));
	queries.emplace_back(fmt::format(
		"SELECT `key`, `value` FROM `player_storage` WHERE `player_id` = {:d}",
		playerId));
	queries.emplace_back(fmt::format(
		"SELECT `charm_id`, `state` FROM `player_charms` WHERE `player_id` = {:d}",
		playerId));
	queries.emplace_back(fmt::format(
		"SELECT `player_id` FROM `account_viplist` WHERE `account_id` = {:d}",
		accountId));
	return queries;
}

bool IOLoginData::materializePlayerLoginSnapshot(
	Player* player, const std::string& name,
	const std::vector<PlayerIOReadSnapshotEntry>& entries, std::string& error)
{
	PlayerIORemoteDatabaseScope replay(entries);
	if (!preloadPlayer(player, name)) {
		error = replay.getError().empty() ? "character preload materialization failed" :
			replay.getError();
		return false;
	}
	if (!loadPlayerById(player, player->getGUID(), true)) {
		error = replay.getError().empty() ? "character materialization failed" :
			replay.getError();
		return false;
	}
	if (!replay.getError().empty()) {
		error = replay.getError();
		return false;
	}
	if (!replay.replayComplete()) {
		error = "character snapshot contained unexpected trailing query results";
		return false;
	}
	return true;
}

bool IOLoginData::loadPlayerById(Player* player, uint32_t id, bool deferGuild)
{
	const PlayerIORemoteDatabaseScope* scope = PlayerIORemoteDatabaseScope::current();
	const bool needsLegacyReservation =
		g_playerIOManager.isEnabled() &&
		player->isOffline() &&
		(!scope || !scope->isReplayRead());
	if (needsLegacyReservation) {
		if (player->playerIOReservationId != 0 ||
				!g_playerIOManager.reserveLegacyOperation(id)) {
			return false;
		}
		player->playerIOReservationId = id;
	}

	Database& db = Database::getInstance();
	DispatcherPhaseMetricsTimer playerRowTimer(DispatcherMetricsPhase::LOGIN_PLAYER_ROW_QUERY);
	const std::string playerPredicate =
		scope && scope->isReplayRead()
			? fmt::format("`id` = {:d} AND `deletion` = 0", id)
			: fmt::format("`id` = {:d}", id);
	DBResult_ptr result = db.storeQuery(
		buildPlayerCoreQuery(playerPredicate));
	playerRowTimer.stop();
	const bool loaded = loadPlayer(player, std::move(result), deferGuild);
	if (!loaded && needsLegacyReservation) {
		g_playerIOManager.releaseLegacyOperation(player->playerIOReservationId);
		player->playerIOReservationId = 0;
	}
	return loaded;
}

bool IOLoginData::loadPlayerByName(Player* player, const std::string& name)
{
	const PlayerIORemoteDatabaseScope* scope = PlayerIORemoteDatabaseScope::current();
	if (g_playerIOManager.isEnabled() && player->isOffline() &&
			(!scope || !scope->isReplayRead())) {
		const uint32_t id = getGuidByName(name);
		return id != 0 && loadPlayerById(player, id);
	}

	Database& db = Database::getInstance();
	return loadPlayer(player, db.storeQuery(buildPlayerCoreQuery(
		fmt::format("`name` = {:s}", db.escapeString(name)))));
}

bool IOLoginData::loadPlayer(Player* player, DBResult_ptr result, bool deferGuild)
{
	if (!result) {
		return false;
	}

	DispatcherPhaseMetricsTimer coreTimer(DispatcherMetricsPhase::LOGIN_LOAD_CORE);
	Database& db = Database::getInstance();

	uint32_t accno = result->getNumber<uint32_t>("account_id");
	Account acc = loadAccount(accno);

	player->setGUID(result->getNumber<uint32_t>("id"));
	player->name = result->getString("name");
	player->accountNumber = accno;
	player->databaseSaveEnabled = result->getNumber<uint16_t>("save") != 0;

	player->accountType = acc.accountType;

	player->premiumEndsAt = acc.premiumEndsAt;

	Group* group = g_game.groups.getGroup(result->getNumber<uint16_t>("group_id"));
	if (!group) {
		std::cout << "[Error - IOLoginData::loadPlayer] " << player->name << " has Group ID " << result->getNumber<uint16_t>("group_id") << " which doesn't exist" << std::endl;
		return false;
	}
	player->setGroup(group);

	player->bankBalance = result->getNumber<uint64_t>("balance");

	player->setSex(static_cast<PlayerSex_t>(result->getNumber<uint16_t>("sex")));
	player->level = std::max<uint32_t>(1, result->getNumber<uint32_t>("level"));

	uint64_t experience = result->getNumber<uint64_t>("experience");

	uint64_t currExpCount = Player::getExpForLevel(player->level);
	uint64_t nextExpCount = Player::getExpForLevel(player->level + 1);
	if (experience < currExpCount || experience > nextExpCount) {
		experience = currExpCount;
	}

	player->experience = experience;

	if (currExpCount < nextExpCount) {
		player->levelPercent = Player::getPercentLevel(player->experience - currExpCount, nextExpCount - currExpCount);
	} else {
		player->levelPercent = 0;
	}

	player->soul = result->getNumber<uint16_t>("soul");
	player->capacity = result->getNumber<uint32_t>("cap") * 100;
	player->blessings = result->getNumber<uint16_t>("blessings");

	unsigned long conditionsSize;
	const char* conditions = result->getStream("conditions", conditionsSize);
	PropStream propStream;
	propStream.init(conditions, conditionsSize);

	Condition* condition = Condition::createCondition(propStream);
	while (condition) {
		if (condition->unserialize(propStream)) {
			player->storedConditionList.push_front(condition);
		} else {
			delete condition;
		}
		condition = Condition::createCondition(propStream);
	}

	if (!player->setVocation(result->getNumber<uint16_t>("vocation"))) {
		std::cout << "[Error - IOLoginData::loadPlayer] " << player->name << " has Vocation ID " << result->getNumber<uint16_t>("vocation") << " which doesn't exist" << std::endl;
		return false;
	}

	player->mana = result->getNumber<uint32_t>("mana");
	player->manaMax = result->getNumber<uint32_t>("manamax");
	player->magLevel = result->getNumber<uint32_t>("maglevel");

	uint64_t nextManaCount = player->vocation->getReqMana(player->magLevel + 1);
	uint64_t manaSpent = result->getNumber<uint64_t>("manaspent");
	if (manaSpent > nextManaCount) {
		manaSpent = 0;
	}

	player->manaSpent = manaSpent;
	player->magLevelPercent = Player::getPercentLevel(player->manaSpent, nextManaCount);

	player->health = result->getNumber<int32_t>("health");
	player->healthMax = result->getNumber<int32_t>("healthmax");

	player->defaultOutfit.lookType = result->getNumber<uint16_t>("looktype");
	player->defaultOutfit.lookHead = result->getNumber<uint16_t>("lookhead");
	player->defaultOutfit.lookBody = result->getNumber<uint16_t>("lookbody");
	player->defaultOutfit.lookLegs = result->getNumber<uint16_t>("looklegs");
	player->defaultOutfit.lookFeet = result->getNumber<uint16_t>("lookfeet");
	player->defaultOutfit.lookAddons = result->getNumber<uint16_t>("lookaddons");
	player->currentOutfit = player->defaultOutfit;
	player->direction = static_cast<Direction> (result->getNumber<uint16_t>("direction"));

	if (g_game.getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
		const time_t skullSeconds = result->getNumber<time_t>("skulltime") - time(nullptr);
		if (skullSeconds > 0) {
			//ensure that we round up the number of ticks
			player->skullTicks = (skullSeconds + 2);

			uint16_t skull = result->getNumber<uint16_t>("skull");
			if (skull == SKULL_RED) {
				player->skull = SKULL_RED;
			} else if (skull == SKULL_BLACK) {
				player->skull = SKULL_BLACK;
			}
		}
	}

	player->loginPosition.x = result->getNumber<uint16_t>("posx");
	player->loginPosition.y = result->getNumber<uint16_t>("posy");
	player->loginPosition.z = result->getNumber<uint16_t>("posz");

	player->lastLoginSaved = result->getNumber<time_t>("lastlogin");
	player->lastLogout = result->getNumber<time_t>("lastlogout");

	player->offlineTrainingTime = result->getNumber<int32_t>("offlinetraining_time") * 1000;
	player->offlineTrainingSkill = result->getNumber<int32_t>("offlinetraining_skill");

	Town* town = g_game.map.towns.getTown(result->getNumber<uint32_t>("town_id"));
	if (!town) {
		std::cout << "[Error - IOLoginData::loadPlayer] " << player->name << " has Town ID " << result->getNumber<uint32_t>("town_id") << " which doesn't exist" << std::endl;
		return false;
	}

	player->town = town;

	const Position& loginPos = player->loginPosition;
	if (loginPos.x == 0 && loginPos.y == 0 && loginPos.z == 0) {
		player->loginPosition = player->getTemplePosition();
	}

	player->staminaMinutes = result->getNumber<uint16_t>("stamina");

	static const std::string skillNames[] = {"skill_fist", "skill_club", "skill_sword", "skill_axe", "skill_dist", "skill_shielding", "skill_fishing"};
	static const std::string skillNameTries[] = {"skill_fist_tries", "skill_club_tries", "skill_sword_tries", "skill_axe_tries", "skill_dist_tries", "skill_shielding_tries", "skill_fishing_tries"};
	static constexpr size_t size = sizeof(skillNames) / sizeof(std::string);
	for (uint8_t i = 0; i < size; ++i) {
		uint16_t skillLevel = result->getNumber<uint16_t>(skillNames[i]);
		uint64_t skillTries = result->getNumber<uint64_t>(skillNameTries[i]);
		uint64_t nextSkillTries = player->vocation->getReqSkillTries(i, skillLevel + 1);
		if (skillTries > nextSkillTries) {
			skillTries = 0;
		}

		player->skills[i].level = skillLevel;
		player->skills[i].tries = skillTries;
		player->skills[i].percent = Player::getPercentLevel(skillTries, nextSkillTries);
	}
	coreTimer.stop();

	DispatcherPhaseMetricsTimer socialTimer(DispatcherMetricsPhase::LOGIN_LOAD_SOCIAL);
	if ((result = db.storeQuery(fmt::format("SELECT `guild_id`, `rank_id`, `nick` FROM `guild_membership` WHERE `player_id` = {:d}", player->getGUID())))) {
		uint32_t guildId = result->getNumber<uint32_t>("guild_id");
		uint32_t playerRankId = result->getNumber<uint32_t>("rank_id");
		player->guildNick = result->getString("nick");

		if (deferGuild) {
			player->deferredGuildId = guildId;
			player->deferredGuildRankId = playerRankId;
			player->hasDeferredGuildLoad = true;
		} else {
			loadGuild(player, guildId, playerRankId);
		}
	}

	if ((result = db.storeQuery(fmt::format("SELECT `player_id`, `name` FROM `player_spells` WHERE `player_id` = {:d}", player->getGUID())))) {
		do {
			player->learnedInstantSpellList.emplace_front(result->getString("name"));
		} while (result->next());
	}
	socialTimer.stop();

	//load inventory items
	DispatcherPhaseMetricsTimer inventoryTimer(DispatcherMetricsPhase::LOGIN_LOAD_INVENTORY);
	ItemMap itemMap;

	DispatcherPhaseMetricsTimer inventoryQueryTimer(DispatcherMetricsPhase::LOGIN_INVENTORY_QUERY);
	result = db.storeQuery(fmt::format("SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_items` WHERE `player_id` = {:d} ORDER BY `sid` DESC", player->getGUID()));
	inventoryQueryTimer.stop();
	if (result) {
		DispatcherPhaseMetricsTimer inventoryDecodeTimer(DispatcherMetricsPhase::LOGIN_INVENTORY_DECODE);
		loadItems(itemMap, result);
		inventoryDecodeTimer.stop();

		DispatcherPhaseMetricsTimer inventoryAttachTimer(DispatcherMetricsPhase::LOGIN_INVENTORY_ATTACH);
		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;
			int32_t pid = pair.second;
			if (pid >= CONST_SLOT_FIRST && pid <= CONST_SLOT_LAST) {
				player->internalAddThing(pid, item);
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);
				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
				}
			}
		}
	}
	inventoryTimer.stop();

	//load depot locker items
	DispatcherPhaseMetricsTimer lockerTimer(DispatcherMetricsPhase::LOGIN_LOAD_LOCKER);
	itemMap.clear();

	if ((result = db.storeQuery(fmt::format("SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_depotlockeritems` WHERE `player_id` = {:d} ORDER BY `sid` DESC", player->getGUID())))) {
		loadItems(itemMap, result);

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;

			int32_t pid = pair.second;
			if (pid >= 0 && pid < 100) {
				DepotLocker* depotLocker = player->getDepotLocker(pid);
				if (depotLocker) {
					depotLocker->internalAddThing(item);
				}
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);
				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
				}
			}
		}
	}
	lockerTimer.stop();

	//load depot items
	DispatcherPhaseMetricsTimer depotTimer(DispatcherMetricsPhase::LOGIN_LOAD_DEPOT);
	itemMap.clear();

	if ((result = db.storeQuery(fmt::format("SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_depotitems` WHERE `player_id` = {:d} ORDER BY `sid` DESC", player->getGUID())))) {
		loadItems(itemMap, result);

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;

			int32_t pid = pair.second;
			if (pid >= 0 && pid < 100) {
				DepotChest* depotChest = player->getDepotChest(pid, true);
				if (depotChest) {
					depotChest->internalAddThing(item);
				}
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);
				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
				}
			}
		}
	}
	depotTimer.stop();

	//load inbox items
	/*itemMap.clear();

	if ((result = db.storeQuery(fmt::format("SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_inboxitems` WHERE `player_id` = {:d} ORDER BY `sid` DESC", player->getGUID())))) {
		loadItems(itemMap, result);

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;
			int32_t pid = pair.second;

			if (pid >= 0 && pid < 100) {
				player->getInbox()->internalAddThing(item);
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);

				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
				}
			}
		}
	}

	//load store inbox items
	itemMap.clear();

	if ((result = db.storeQuery(fmt::format("SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_storeinboxitems` WHERE `player_id` = {:d} ORDER BY `sid` DESC", player->getGUID())))) {
		loadItems(itemMap, result);

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;
			int32_t pid = pair.second;

			if (pid >= 0 && pid < 100) {
				player->getStoreInbox()->internalAddThing(item);
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);

				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
				}
			}
		}
	}*/

	//load storage map
	DispatcherPhaseMetricsTimer storageTimer(DispatcherMetricsPhase::LOGIN_LOAD_STORAGE);
	if ((result = db.storeQuery(fmt::format("SELECT `key`, `value` FROM `player_storage` WHERE `player_id` = {:d}", player->getGUID())))) {
		do {
			player->addStorageValue(result->getNumber<uint32_t>("key"), result->getNumber<int32_t>("value"), true);
		} while (result->next());
	}
	storageTimer.stop();

	DispatcherPhaseMetricsTimer charmsTimer(DispatcherMetricsPhase::LOGIN_LOAD_CHARMS);
	player->loadCharmStatesFromDatabase();
	charmsTimer.stop();

	//load vip list
	DispatcherPhaseMetricsTimer vipTimer(DispatcherMetricsPhase::LOGIN_LOAD_VIP);
	if ((result = db.storeQuery(fmt::format("SELECT `player_id` FROM `account_viplist` WHERE `account_id` = {:d}", player->getAccount())))) {
		do {
			player->addVIPInternal(result->getNumber<uint32_t>("player_id"));
		} while (result->next());
	}
	vipTimer.stop();

	DispatcherPhaseMetricsTimer finalizeTimer(DispatcherMetricsPhase::LOGIN_LOAD_FINALIZE);
	player->updateBaseSpeed();
	player->updateInventoryWeight();
	player->updateItemsLight(true);
	return true;
}

bool IOLoginData::loadGuild(Player* player, uint32_t guildId, uint32_t playerRankId)
{
	Guild* guild = g_game.getGuild(guildId);
	if (!guild) {
		guild = IOGuild::loadGuild(guildId);
		if (guild) {
			g_game.addGuild(guild);
		}
	}

	if (!guild) {
		return false;
	}
	GuildRank_ptr rank = guild->getRankById(playerRankId);
	if (!rank) {
		return false;
	}

	player->guild = guild;
	player->guildRank = rank;
	return true;
}

bool IOLoginData::finalizeDeferredGuild(Player* player)
{
	if (!player->hasDeferredGuildLoad) {
		return true;
	}

	const uint32_t guildId = player->deferredGuildId;
	const uint32_t guildRankId = player->deferredGuildRankId;
	player->deferredGuildId = 0;
	player->deferredGuildRankId = 0;
	player->hasDeferredGuildLoad = false;
	return loadGuild(player, guildId, guildRankId);
}

bool IOLoginData::saveItems(const Player* player, const ItemBlockList& itemList, DBInsert& query_insert,
                            PropWriteStream& propWriteStream, bool measureInventory)
{
	using ContainerBlock = std::pair<Container*, int32_t>;
	std::vector<ContainerBlock> containers;
	containers.reserve(32);

	int32_t runningId = 100;
	uint64_t serializeNanoseconds = 0;
	uint64_t buildRowsNanoseconds = 0;
	const bool measure = measureInventory && dispatcherLogoutMetricsContextActive() && dispatcherMetricsEnabled();

	Database& db = Database::getInstance();
	for (const auto& it : itemList) {
		int32_t pid = it.first;
		Item* item = it.second;
		++runningId;

		const auto serializeStartedAt = measure
			? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};
		propWriteStream.clear();
		item->serializeAttr(propWriteStream);

		size_t attributesSize;
		const char* attributes = propWriteStream.getStream(attributesSize);
		if (measure) {
			serializeNanoseconds += static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - serializeStartedAt).count());
		}

		const auto buildStartedAt = measure
			? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};
		if (!query_insert.addRow(fmt::format("{:d}, {:d}, {:d}, {:d}, {:d}, {:s}", player->getGUID(), pid, runningId, item->getID(), item->getSubType(), db.escapeBlob(attributes, attributesSize)))) {
			return false;
		}
		if (measure) {
			buildRowsNanoseconds += static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - buildStartedAt).count());
		}

		if (Container* container = item->getContainer()) {
			containers.emplace_back(container, runningId);
		}
	}

	for (size_t i = 0; i < containers.size(); i++) {
		const ContainerBlock& cb = containers[i];
		Container* container = cb.first;
		int32_t parentId = cb.second;

		for (Item* item : container->getItemList()) {
			++runningId;

			Container* subContainer = item->getContainer();
			if (subContainer) {
				containers.emplace_back(subContainer, runningId);
			}

			const auto serializeStartedAt = measure
				? std::chrono::steady_clock::now()
				: std::chrono::steady_clock::time_point{};
			propWriteStream.clear();
			item->serializeAttr(propWriteStream);

			size_t attributesSize;
			const char* attributes = propWriteStream.getStream(attributesSize);
			if (measure) {
				serializeNanoseconds += static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - serializeStartedAt).count());
			}

			const auto buildStartedAt = measure
				? std::chrono::steady_clock::now()
				: std::chrono::steady_clock::time_point{};
			if (!query_insert.addRow(fmt::format("{:d}, {:d}, {:d}, {:d}, {:d}, {:s}", player->getGUID(), parentId, runningId, item->getID(), item->getSubType(), db.escapeBlob(attributes, attributesSize)))) {
				return false;
			}
			if (measure) {
				buildRowsNanoseconds += static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - buildStartedAt).count());
			}
		}
	}

	if (measure) {
		recordDispatcherPhase(DispatcherMetricsPhase::LOGOUT_INVENTORY_SERIALIZE, serializeNanoseconds);
		recordDispatcherPhase(DispatcherMetricsPhase::LOGOUT_INVENTORY_BUILD_ROWS, buildRowsNanoseconds);
	}
	DispatcherPhaseMetricsTimer insertTimer(
		DispatcherMetricsPhase::LOGOUT_INVENTORY_INSERT,
		measure);
	return query_insert.execute();
}

bool IOLoginData::savePlayer(Player* player)

{
	const uint32_t legacyReservationId = player->playerIOReservationId;
	auto releaseLegacyReservation = [&]() {
		if (legacyReservationId != 0) {
			g_playerIOManager.releaseLegacyOperation(legacyReservationId);
			player->playerIOReservationId = 0;
		}
	};

	if (!player->isOffline() && g_game.hasFloorCheckpointForPlayer(player->getGUID())) {
		DispatcherPhaseMetricsTimer checkpointTimer(
			DispatcherMetricsPhase::LOGOUT_SAVE_CHECKPOINT,
			dispatcherLogoutMetricsContextActive());
		const bool saved = g_game.saveFloorCheckpointForPlayer(player);
		releaseLegacyReservation();
		return saved;
	}

	DispatcherPhaseMetricsTimer transactionBeginTimer(
		DispatcherMetricsPhase::LOGOUT_SAVE_TRANSACTION_BEGIN,
		dispatcherLogoutMetricsContextActive());
	DBTransaction transaction;
	if (!transaction.begin()) {
		releaseLegacyReservation();
		return false;
	}
	transactionBeginTimer.stop();

	if (!savePlayerData(player)) {
		releaseLegacyReservation();
		return false;
	}
	DispatcherPhaseMetricsTimer commitTimer(
		DispatcherMetricsPhase::LOGOUT_SAVE_COMMIT,
		dispatcherLogoutMetricsContextActive());
	const bool saved = transaction.commit();
	releaseLegacyReservation();
	return saved;
}

bool IOLoginData::savePlayerDirect(Player* player)
{
	DispatcherPhaseMetricsTimer transactionBeginTimer(
		DispatcherMetricsPhase::LOGOUT_SAVE_TRANSACTION_BEGIN,
		dispatcherLogoutMetricsContextActive());
	DBTransaction transaction;
	if (!transaction.begin()) {
		return false;
	}
	transactionBeginTimer.stop();

	if (!savePlayerData(player)) {
		return false;
	}

	DispatcherPhaseMetricsTimer commitTimer(
		DispatcherMetricsPhase::LOGOUT_SAVE_COMMIT,
		dispatcherLogoutMetricsContextActive());
	return transaction.commit();
}

bool IOLoginData::buildPlayerSaveSnapshot(
	Player* player, std::vector<std::string>& statements, std::string& error)
{
	statements.clear();
	PlayerIORemoteDatabaseScope collector(statements);
	DBTransaction transaction;
	if (!transaction.begin()) {
		error = collector.getError().empty() ?
			"could not begin immutable player save snapshot" : collector.getError();
		return false;
	}
	updateOnlineStatus(player->getGUID(), false);
	if (!savePlayerData(player)) {
		error = collector.getError().empty() ?
			"could not serialize immutable player save snapshot" : collector.getError();
		return false;
	}
	bool finalized = false;
	{
		DispatcherPhaseMetricsTimer finalizeTimer(
			DispatcherMetricsPhase::LOGOUT_ASYNC_STATEMENTS_FINALIZE,
			dispatcherLogoutMetricsContextActive());
		finalized = transaction.commit();
	}
	if (!finalized) {
		error = collector.getError().empty() ?
			"could not finalize immutable player save snapshot" : collector.getError();
		return false;
	}
	if (statements.empty()) {
		error = "immutable player save snapshot is empty";
		return false;
	}
	return true;
}

bool IOLoginData::savePlayerData(Player* player)
{
	DispatcherPhaseMetricsTimer coreTimer(
		DispatcherMetricsPhase::LOGOUT_SAVE_CORE,
		dispatcherLogoutMetricsContextActive());
	if (player->getHealth() <= 0) {
		player->changeHealth(1);
	}

	Database& db = Database::getInstance();

	if (!player->databaseSaveEnabled) {
		return db.executeQuery(fmt::format("UPDATE `players` SET `lastlogin` = {:d}, `lastip` = {:d} WHERE `id` = {:d}", player->lastLoginSaved, player->lastIP, player->getGUID()));
	}

	//serialize conditions
	PropWriteStream propWriteStream;
	for (Condition* condition : player->conditions) {
		if (condition->isPersistent()) {
			condition->serialize(propWriteStream);
			propWriteStream.write<uint8_t>(CONDITIONATTR_END);
		}
	}

	size_t conditionsSize;
	const char* conditions = propWriteStream.getStream(conditionsSize);

	//First, an UPDATE query to write the player itself
	std::ostringstream query;
	query << "UPDATE `players` SET ";
	query << "`level` = " << player->level << ',';
	query << "`group_id` = " << player->group->id << ',';
	query << "`vocation` = " << player->getVocationId() << ',';
	query << "`health` = " << player->health << ',';
	query << "`healthmax` = " << player->healthMax << ',';
	query << "`experience` = " << player->experience << ',';
	query << "`lookbody` = " << static_cast<uint32_t>(player->defaultOutfit.lookBody) << ',';
	query << "`lookfeet` = " << static_cast<uint32_t>(player->defaultOutfit.lookFeet) << ',';
	query << "`lookhead` = " << static_cast<uint32_t>(player->defaultOutfit.lookHead) << ',';
	query << "`looklegs` = " << static_cast<uint32_t>(player->defaultOutfit.lookLegs) << ',';
	query << "`looktype` = " << player->defaultOutfit.lookType << ',';
	query << "`lookaddons` = " << static_cast<uint32_t>(player->defaultOutfit.lookAddons) << ',';
	query << "`maglevel` = " << player->magLevel << ',';
	query << "`mana` = " << player->mana << ',';
	query << "`manamax` = " << player->manaMax << ',';
	query << "`manaspent` = " << player->manaSpent << ',';
	query << "`soul` = " << static_cast<uint16_t>(player->soul) << ',';
	query << "`town_id` = " << player->town->getID() << ',';

	const Position& loginPosition = player->getLoginPosition();
	query << "`posx` = " << loginPosition.getX() << ',';
	query << "`posy` = " << loginPosition.getY() << ',';
	query << "`posz` = " << loginPosition.getZ() << ',';

	query << "`cap` = " << (player->capacity / 100) << ',';
	query << "`sex` = " << static_cast<uint16_t>(player->sex) << ',';

	if (player->lastLoginSaved != 0) {
		query << "`lastlogin` = " << player->lastLoginSaved << ',';
	}

	if (player->lastIP != 0) {
		query << "`lastip` = " << player->lastIP << ',';
	}

	query << "`conditions` = " << db.escapeBlob(conditions, conditionsSize) << ',';

	if (g_game.getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
		int64_t skullTime = 0;

		if (player->skullTicks > 0) {
			skullTime = time(nullptr) + player->skullTicks;
		}
		query << "`skulltime` = " << skullTime << ',';

		Skulls_t skull = SKULL_NONE;
		if (player->skull == SKULL_RED) {
			skull = SKULL_RED;
		} else if (player->skull == SKULL_BLACK) {
			skull = SKULL_BLACK;
		}
		query << "`skull` = " << static_cast<int64_t>(skull) << ',';
	}

	query << "`lastlogout` = " << player->getLastLogout() << ',';
	query << "`balance` = " << player->bankBalance << ',';
	query << "`offlinetraining_time` = " << player->getOfflineTrainingTime() / 1000 << ',';
	query << "`offlinetraining_skill` = " << player->getOfflineTrainingSkill() << ',';
	query << "`stamina` = " << player->getStaminaMinutes() << ',';

	query << "`skill_fist` = " << player->skills[SKILL_FIST].level << ',';
	query << "`skill_fist_tries` = " << player->skills[SKILL_FIST].tries << ',';
	query << "`skill_club` = " << player->skills[SKILL_CLUB].level << ',';
	query << "`skill_club_tries` = " << player->skills[SKILL_CLUB].tries << ',';
	query << "`skill_sword` = " << player->skills[SKILL_SWORD].level << ',';
	query << "`skill_sword_tries` = " << player->skills[SKILL_SWORD].tries << ',';
	query << "`skill_axe` = " << player->skills[SKILL_AXE].level << ',';
	query << "`skill_axe_tries` = " << player->skills[SKILL_AXE].tries << ',';
	query << "`skill_dist` = " << player->skills[SKILL_DISTANCE].level << ',';
	query << "`skill_dist_tries` = " << player->skills[SKILL_DISTANCE].tries << ',';
	query << "`skill_shielding` = " << player->skills[SKILL_SHIELD].level << ',';
	query << "`skill_shielding_tries` = " << player->skills[SKILL_SHIELD].tries << ',';
	query << "`skill_fishing` = " << player->skills[SKILL_FISHING].level << ',';
	query << "`skill_fishing_tries` = " << player->skills[SKILL_FISHING].tries << ',';
	query << "`direction` = " << static_cast<uint16_t> (player->getDirection()) << ',';

	if (!player->isOffline()) {
		query << "`onlinetime` = `onlinetime` + " << (time(nullptr) - player->lastLoginSaved) << ',';
	}
	query << "`blessings` = " << player->blessings.to_ulong();
	query << " WHERE `id` = " << player->getGUID();

	if (!db.executeQuery(query.str())) {
		return false;
	}
	coreTimer.stop();

	// learned spells
	DispatcherPhaseMetricsTimer spellsTimer(
		DispatcherMetricsPhase::LOGOUT_SAVE_SPELLS,
		dispatcherLogoutMetricsContextActive());
	if (!db.executeQuery(fmt::format("DELETE FROM `player_spells` WHERE `player_id` = {:d}", player->getGUID()))) {
		return false;
	}

	DBInsert spellsQuery("INSERT INTO `player_spells` (`player_id`, `name` ) VALUES ");
	for (const std::string& spellName : player->learnedInstantSpellList) {
		if (!spellsQuery.addRow(fmt::format("{:d}, {:s}", player->getGUID(), db.escapeString(spellName)))) {
			return false;
		}
	}

	if (!spellsQuery.execute()) {
		return false;
	}
	spellsTimer.stop();

	DispatcherPhaseMetricsTimer inventoryTimer(
		DispatcherMetricsPhase::LOGOUT_SAVE_INVENTORY,
		dispatcherLogoutMetricsContextActive());
	const bool measureInventory = dispatcherLogoutMetricsContextActive() && dispatcherMetricsEnabled();
	uint64_t inventoryPrepareNanoseconds = 0;
	const auto openStateStartedAt = measureInventory
		? std::chrono::steady_clock::now()
		: std::chrono::steady_clock::time_point{};
	player->saveOpenContainerState();
	if (measureInventory) {
		inventoryPrepareNanoseconds += static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - openStateStartedAt).count());
	}

	//item saving
	DispatcherPhaseMetricsTimer inventoryDeleteTimer(
		DispatcherMetricsPhase::LOGOUT_INVENTORY_DELETE,
		measureInventory);
	if (!db.executeQuery(fmt::format("DELETE FROM `player_items` WHERE `player_id` = {:d}", player->getGUID()))) {
		return false;
	}
	inventoryDeleteTimer.stop();

	const auto prepareRowsStartedAt = measureInventory
		? std::chrono::steady_clock::now()
		: std::chrono::steady_clock::time_point{};
	DBInsert itemsQuery("INSERT INTO `player_items` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");

	ItemBlockList itemList;
	for (int32_t slotId = CONST_SLOT_FIRST; slotId <= CONST_SLOT_LAST; ++slotId) {
		Item* item = player->inventory[slotId];
		if (item) {
			itemList.emplace_back(slotId, item);
		}
	}
	if (measureInventory) {
		inventoryPrepareNanoseconds += static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - prepareRowsStartedAt).count());
		recordDispatcherPhase(
			DispatcherMetricsPhase::LOGOUT_INVENTORY_PREPARE,
			inventoryPrepareNanoseconds);
	}

	if (!saveItems(player, itemList, itemsQuery, propWriteStream, true)) {
		return false;
	}
	inventoryTimer.stop();

	//save depot locker items
	DispatcherPhaseMetricsTimer depotTimer(
		DispatcherMetricsPhase::LOGOUT_SAVE_DEPOT,
		dispatcherLogoutMetricsContextActive());
	bool needsSave = false;

	for (const auto& it : player->depotLockerMap) {
		if (it.second->needsSave()) {
			needsSave = true;
			break;
		}
	}

	if (needsSave) {
		if (!db.executeQuery(fmt::format("DELETE FROM `player_depotlockeritems` WHERE `player_id` = {:d}", player->getGUID()))) {
			return false;
		}

		DBInsert lockerQuery("INSERT INTO `player_depotlockeritems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
		itemList.clear();

		for (const auto& it : player->depotLockerMap) {
			for (Item* item : it.second->getItemList()) {
				if (item->getID() != ITEM_DEPOT) {
					itemList.emplace_back(it.first, item);
				}
			}
		}

		if (!saveItems(player, itemList, lockerQuery, propWriteStream)) {
			return false;
		}

		//save depot items
		if (needsSave) {
			if (!db.executeQuery(fmt::format("DELETE FROM `player_depotitems` WHERE `player_id` = {:d}", player->getGUID()))) {
				return false;
			}

			DBInsert depotQuery("INSERT INTO `player_depotitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
			itemList.clear();

			for (const auto& it : player->depotChests) {
				for (Item* item : it.second->getItemList()) {
					itemList.emplace_back(it.first, item);
				}
			}

			if (!saveItems(player, itemList, depotQuery, propWriteStream)) {
				return false;
			}
		}
	}
	depotTimer.stop();

	//save inbox items
	/*if (!db.executeQuery(fmt::format("DELETE FROM `player_inboxitems` WHERE `player_id` = {:d}", player->getGUID()))) {
		return false;
	}

	DBInsert inboxQuery("INSERT INTO `player_inboxitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
	itemList.clear();

	for (Item* item : player->getInbox()->getItemList()) {
		itemList.emplace_back(0, item);
	}

	if (!saveItems(player, itemList, inboxQuery, propWriteStream)) {
		return false;
	}

	//save store inbox items
	if (!db.executeQuery(fmt::format("DELETE FROM `player_storeinboxitems` WHERE `player_id` = {:d}", player->getGUID()))) {
		return false;
	}

	DBInsert storeInboxQuery("INSERT INTO `player_storeinboxitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
	itemList.clear();

	for (Item* item : player->getStoreInbox()->getItemList()) {
		itemList.emplace_back(0, item);
	}

	if (!saveItems(player, itemList, storeInboxQuery, propWriteStream)) {
		return false;
	}*/

	DispatcherPhaseMetricsTimer storageTimer(
		DispatcherMetricsPhase::LOGOUT_SAVE_STORAGE,
		dispatcherLogoutMetricsContextActive());
	if (!db.executeQuery(fmt::format("DELETE FROM `player_storage` WHERE `player_id` = {:d}", player->getGUID()))) {
		return false;
	}

	DBInsert storageQuery("INSERT INTO `player_storage` (`player_id`, `key`, `value`) VALUES ");
	player->genReservedStorageRange();

	for (const auto& it : player->storageMap) {
		if (!storageQuery.addRow(fmt::format("{:d}, {:d}, {:d}", player->getGUID(), it.first, it.second))) {
			return false;
		}
	}

	if (!storageQuery.execute()) {
		return false;
	}

	return true;
}

std::string IOLoginData::getNameByGuid(uint32_t guid)
{
	DBResult_ptr result = Database::getInstance().storeQuery(fmt::format("SELECT `name` FROM `players` WHERE `id` = {:d}", guid));
	if (!result) {
		return std::string();
	}
	return result->getString("name");
}

uint32_t IOLoginData::getGuidByName(const std::string& name)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `id` FROM `players` WHERE `name` = {:s}", db.escapeString(name)));
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("id");
}

bool IOLoginData::getGuidByNameEx(uint32_t& guid, bool& specialVip, std::string& name)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `name`, `id`, `group_id`, `account_id` FROM `players` WHERE `name` = {:s}", db.escapeString(name)));
	if (!result) {
		return false;
	}

	name = result->getString("name");
	guid = result->getNumber<uint32_t>("id");
	Group* group = g_game.groups.getGroup(result->getNumber<uint16_t>("group_id"));

	uint64_t flags;
	if (group) {
		flags = group->flags;
	} else {
		flags = 0;
	}

	specialVip = (flags & PlayerFlag_SpecialVIP) != 0;
	return true;
}

bool IOLoginData::formatPlayerName(std::string& name)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `name` FROM `players` WHERE `name` = {:s}", db.escapeString(name)));
	if (!result) {
		return false;
	}

	name = result->getString("name");
	return true;
}

void IOLoginData::loadItems(ItemMap& itemMap, DBResult_ptr result)
{
	do {
		uint32_t sid = result->getNumber<uint32_t>("sid");
		uint32_t pid = result->getNumber<uint32_t>("pid");
		uint16_t type = result->getNumber<uint16_t>("itemtype");
		uint16_t count = result->getNumber<uint16_t>("count");

		unsigned long attrSize;
		const char* attr = result->getStream("attributes", attrSize);

		PropStream propStream;
		propStream.init(attr, attrSize);

		Item* item = Item::CreateItem(type, count);
		if (item) {
			if (!item->unserializeAttr(propStream)) {
				std::cout << "WARNING: Serialize error in IOLoginData::loadItems" << std::endl;
			}

			std::pair<Item*, uint32_t> pair(item, pid);
			itemMap[sid] = pair;
		}
	} while (result->next());
}

void IOLoginData::increaseBankBalance(uint32_t guid, uint64_t bankBalance)
{
	Database::getInstance().executeQuery(fmt::format("UPDATE `players` SET `balance` = `balance` + {:d} WHERE `id` = {:d}", bankBalance, guid));
}

bool IOLoginData::hasBiddedOnHouse(uint32_t guid)
{
	Database& db = Database::getInstance();
	return db.storeQuery(fmt::format("SELECT `id` FROM `houses` WHERE `highest_bidder` = {:d} LIMIT 1", guid)).get() != nullptr;
}

std::forward_list<VIPEntry> IOLoginData::getVIPEntries(uint32_t accountId)
{
	std::forward_list<VIPEntry> entries;

	DBResult_ptr result = Database::getInstance().storeQuery(fmt::format("SELECT `player_id`, (SELECT `name` FROM `players` WHERE `id` = `player_id`) AS `name`, `description`, `icon`, `notify` FROM `account_viplist` WHERE `account_id` = {:d}", accountId));
	if (result) {
		do {
			entries.emplace_front(
				result->getNumber<uint32_t>("player_id"),
				result->getString("name"),
				result->getString("description"),
				result->getNumber<uint32_t>("icon"),
				result->getNumber<uint16_t>("notify") != 0
			);
		} while (result->next());
	}
	return entries;
}

void IOLoginData::addVIPEntry(uint32_t accountId, uint32_t guid, const std::string& description, uint32_t icon, bool notify)
{
	Database& db = Database::getInstance();
	db.executeQuery(fmt::format("INSERT INTO `account_viplist` (`account_id`, `player_id`, `description`, `icon`, `notify`) VALUES ({:d}, {:d}, {:s}, {:d}, {:d})", accountId, guid, db.escapeString(description), icon, notify));
}

void IOLoginData::editVIPEntry(uint32_t accountId, uint32_t guid, const std::string& description, uint32_t icon, bool notify)
{
	Database& db = Database::getInstance();
	db.executeQuery(fmt::format("UPDATE `account_viplist` SET `description` = {:s}, `icon` = {:d}, `notify` = {:d} WHERE `account_id` = {:d} AND `player_id` = {:d}", db.escapeString(description), icon, notify, accountId, guid));
}

void IOLoginData::removeVIPEntry(uint32_t accountId, uint32_t guid)
{
	Database::getInstance().executeQuery(fmt::format("DELETE FROM `account_viplist` WHERE `account_id` = {:d} AND `player_id` = {:d}", accountId, guid));
}

void IOLoginData::updatePremiumTime(uint32_t accountId, time_t endTime)
{
	Database::getInstance().executeQuery(fmt::format("UPDATE `accounts` SET `premium_ends_at` = {:d} WHERE `id` = {:d}", endTime, accountId));
}
