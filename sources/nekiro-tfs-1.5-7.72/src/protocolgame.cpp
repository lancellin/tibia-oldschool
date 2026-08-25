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

#include "protocolgame.h"

#include "outputmessage.h"

#include "player.h"

#include "configmanager.h"
#include "actions.h"
#include "dispatchermetrics.h"
#include "game.h"
#include "iologindata.h"
#include "iomarket.h"
#include "playeriomanager.h"
#include "ban.h"
#include "monsters.h"
#include "monster.h"
#include "scheduler.h"
#include "tools.h"
#include "playershop.h"

#include <fmt/format.h>

extern ConfigManager g_config;
extern Actions actions;
extern CreatureEvents* g_creatureEvents;
extern Chat* g_chat;

namespace {

constexpr uint8_t CONTAINER_RESTORE_EXTENDED_OPCODE = 8;
constexpr uint8_t PROFESSIONS_EXTENDED_OPCODE = 11;

using WaitList = std::deque<std::pair<int64_t, uint32_t>>; // (timeout, player guid)

WaitList priorityWaitList, waitList;

std::tuple<WaitList&, WaitList::iterator, WaitList::size_type> findClient(const Player& player) {
	const auto fn = [&](const WaitList::value_type& it) { return it.second == player.getGUID(); };

	auto it = std::find_if(priorityWaitList.begin(), priorityWaitList.end(), fn);
	if (it != priorityWaitList.end()) {
		return std::make_tuple(std::ref(priorityWaitList), it, std::distance(it, priorityWaitList.end()) + 1);
	}

	it = std::find_if(waitList.begin(), waitList.end(), fn);
	if (it != waitList.end()) {
		return std::make_tuple(std::ref(waitList), it, priorityWaitList.size() + std::distance(it, waitList.end()) + 1);
	}

	return std::make_tuple(std::ref(waitList), waitList.end(), priorityWaitList.size() + waitList.size());
}

uint8_t getWaitTime(std::size_t slot)
{
	if (slot < 5) {
		return 5;
	} else if (slot < 10) {
		return 10;
	} else if (slot < 20) {
		return 20;
	} else if (slot < 50) {
		return 60;
	} else {
		return 120;
	}
}

int64_t getTimeout(std::size_t slot)
{
	// timeout is set to 15 seconds longer than expected retry attempt
	return getWaitTime(slot) + 15;
}

void cleanupList(WaitList& list)
{
	int64_t time = OTSYS_TIME();

	auto it = list.begin();
	while (it != list.end()) {
		if (it->first <= time) {
			it = list.erase(it);
		} else {
			++it;
		}
	}
}

std::size_t clientLogin(const Player& player)
{
	// Currentslot = position in wait list, 0 for direct access
	if (player.hasFlag(PlayerFlag_CanAlwaysLogin) || player.getAccountType() >= ACCOUNT_TYPE_GAMEMASTER) {
		return 0;
	}

	cleanupList(priorityWaitList);
	cleanupList(waitList);

	uint32_t maxPlayers = static_cast<uint32_t>(g_config.getNumber(ConfigManager::MAX_PLAYERS));
	if (maxPlayers == 0 || (priorityWaitList.empty() && waitList.empty() && g_game.getPlayersOnline() < maxPlayers)) {
		return 0;
	}

	auto result = findClient(player);
	if (std::get<1>(result) != std::get<0>(result).end()) {
		auto currentSlot = std::get<2>(result);
		// If server has capacity for this client, let him in even though his current slot might be higher than 0.
		if ((g_game.getPlayersOnline() + currentSlot) <= maxPlayers) {
			std::get<0>(result).erase(std::get<1>(result));
			return 0;
		}

		//let them wait a bit longer
		std::get<1>(result)->second = OTSYS_TIME() + (getTimeout(currentSlot) * 1000);
		return currentSlot;
	}

	auto currentSlot = priorityWaitList.size();
	if (player.isPremium()) {
		priorityWaitList.emplace_back(OTSYS_TIME() + (getTimeout(++currentSlot) * 1000), player.getGUID());
	} else {
		currentSlot += waitList.size();
		waitList.emplace_back(OTSYS_TIME() + (getTimeout(++currentSlot) * 1000), player.getGUID());
	}
	return currentSlot;
}

struct BestiaryClientEntry {
	BestiaryMonsterEntry config;
	std::string databaseName;
	std::string displayName;
	Outfit_t outfit;
	uint32_t kills = 0;
	uint16_t lastStageReached = 0;
	const MonsterType* monsterType = nullptr;
};

constexpr char BESTIARY_CATEGORY_CREATURES[] = "Creatures";
constexpr char BESTIARY_CATEGORY_BOSSES[] = "Bosses";
constexpr char BESTIARY_CATEGORY_RESULT[] = "Result";

const std::unordered_set<std::string>& getBestiaryBossNames()
{
	static const std::unordered_set<std::string> bossNames = []() {
		std::unordered_set<std::string> names;
		pugi::xml_document doc;
		const pugi::xml_parse_result result = doc.load_file("data/monster/monsters.xml");
		if (!result) {
			printXMLError("Bestiary Bosses", "data/monster/monsters.xml", result);
			return names;
		}

		for (const pugi::xml_node& monsterNode : doc.child("monsters").children("monster")) {
			const std::string file = asLowerCaseString(monsterNode.attribute("file").as_string());
			if (file.rfind("bosses/", 0) != 0) {
				continue;
			}

			const std::string name = asLowerCaseString(monsterNode.attribute("name").as_string());
			if (!name.empty()) {
				names.insert(name);
			}
		}

		return names;
	}();

	return bossNames;
}

bool isBestiaryBossEntry(const BestiaryClientEntry& entry)
{
	if (entry.monsterType && entry.monsterType->info.isBoss) {
		return true;
	}

	return getBestiaryBossNames().find(asLowerCaseString(entry.databaseName)) != getBestiaryBossNames().end();
}

const char* getBestiaryCategoryName(const BestiaryClientEntry& entry)
{
	return isBestiaryBossEntry(entry) ? BESTIARY_CATEGORY_BOSSES : BESTIARY_CATEGORY_CREATURES;
}

bool matchesBestiaryCategory(const BestiaryClientEntry& entry, const std::string& categoryName)
{
	if (categoryName == BESTIARY_CATEGORY_BOSSES) {
		return isBestiaryBossEntry(entry);
	}

	return !isBestiaryBossEntry(entry);
}

std::string getBestiaryDisplayName(const std::string& databaseName, const MonsterType* monsterType)
{
	if (monsterType && !monsterType->name.empty()) {
		return monsterType->name;
	}

	std::string displayName = databaseName;
	if (!displayName.empty()) {
		displayName[0] = static_cast<char>(std::toupper(displayName[0]));
	}
	return displayName;
}

uint8_t getBestiaryCurrentLevel(const BestiaryClientEntry& entry)
{
	if (entry.kills == 0) {
		return 0;
	}

	uint16_t effectiveStageReached = 0;
	if (entry.kills >= entry.config.killsStage3) {
		effectiveStageReached = 3;
	} else if (entry.kills >= entry.config.killsStage2) {
		effectiveStageReached = 2;
	} else if (entry.kills >= entry.config.killsStage1) {
		effectiveStageReached = 1;
	}

	return static_cast<uint8_t>(std::min<uint16_t>(4, effectiveStageReached + 1));
}

uint8_t getBestiaryDifficulty(const BestiaryClientEntry& entry)
{
	if (!entry.monsterType) {
		return 0;
	}

	const uint32_t health = static_cast<uint32_t>(std::max<int32_t>(entry.monsterType->info.healthMax, 0));
	if (health >= 5000) {
		return 4;
	}
	if (health >= 2000) {
		return 3;
	}
	if (health >= 750) {
		return 2;
	}
	if (health >= 250) {
		return 1;
	}
	return 0;
}

uint8_t getBestiaryAttackMode(const BestiaryClientEntry& entry)
{
	if (!entry.monsterType) {
		return 0;
	}

	return entry.monsterType->info.targetDistance <= 1 ? 1 : 0;
}

uint16_t getBestiaryElementValue(const BestiaryClientEntry& entry, CombatType_t combatType)
{
	if (!entry.monsterType) {
		return 100;
	}

	const auto& elementMap = entry.monsterType->info.elementMap;
	const auto it = elementMap.find(combatType);
	if (it == elementMap.end()) {
		return 100;
	}

	return static_cast<uint16_t>(std::max<int32_t>(0, it->second));
}

uint8_t getBestiaryLootDifficulty(const LootBlock& loot)
{
	if (loot.chance >= 20000) {
		return 0;
	}
	if (loot.chance >= 7100) {
		return 1;
	}
	if (loot.chance >= 2000) {
		return 2;
	}
	if (loot.chance >= 500) {
		return 3;
	}
	if (loot.chance >= 100) {
		return 4;
	}
	return 5;
}

void collectBestiaryLootEntries(const LootBlock& loot, std::vector<const LootBlock*>& entries)
{
	if (!loot.childLoot.empty()) {
		for (const LootBlock& childLoot : loot.childLoot) {
			collectBestiaryLootEntries(childLoot, entries);
		}
		return;
	}

	entries.push_back(&loot);
}

void addBestiaryOutfit(NetworkMessage& msg, const Outfit_t& outfit)
{
	msg.add<uint16_t>(outfit.lookType);
	msg.addByte(outfit.lookHead);
	msg.addByte(outfit.lookBody);
	msg.addByte(outfit.lookLegs);
	msg.addByte(outfit.lookFeet);
	msg.addByte(outfit.lookAddons);
}

std::vector<BestiaryClientEntry> loadBestiaryEntries(const Player& player)
{
	std::vector<BestiaryClientEntry> entries;

	for (const auto& bestiaryIt : g_game.getBestiaryMonsters()) {
		const BestiaryMonsterEntry& config = bestiaryIt.second;
		if (!config.enabled) {
			continue;
		}

		BestiaryClientEntry entry;
		entry.config = config;
		entry.databaseName = bestiaryIt.first;
		if (const PlayerBestiaryProgress* progress = player.getBestiaryProgress(config.creatureId)) {
			entry.kills = progress->kills;
			entry.lastStageReached = progress->lastStageReached;
		}
		const std::string lookupName = entry.databaseName;
		entry.monsterType = g_monsters.getMonsterType(lookupName);
		if (entry.monsterType) {
			entry.displayName = getBestiaryDisplayName(entry.databaseName, entry.monsterType);
			entry.outfit = entry.monsterType->info.outfit;
		} else {
			entry.displayName = getBestiaryDisplayName(entry.databaseName, nullptr);
		}
		entries.emplace_back(std::move(entry));
	}

	std::sort(entries.begin(), entries.end(), [](const BestiaryClientEntry& a, const BestiaryClientEntry& b) {
		return a.databaseName < b.databaseName;
	});

	return entries;
}

bool loadBestiaryEntry(const Player& player, uint16_t creatureId, BestiaryClientEntry& entry)
{
	for (const auto& bestiaryIt : g_game.getBestiaryMonsters()) {
		const BestiaryMonsterEntry& config = bestiaryIt.second;
		if (!config.enabled || config.creatureId != creatureId) {
			continue;
		}

		entry.config = config;
		entry.databaseName = bestiaryIt.first;
		if (const PlayerBestiaryProgress* progress = player.getBestiaryProgress(creatureId)) {
			entry.kills = progress->kills;
			entry.lastStageReached = progress->lastStageReached;
		}
		const std::string lookupName = entry.databaseName;
		entry.monsterType = g_monsters.getMonsterType(lookupName);
		if (entry.monsterType) {
			entry.displayName = getBestiaryDisplayName(entry.databaseName, entry.monsterType);
			entry.outfit = entry.monsterType->info.outfit;
		} else {
			entry.displayName = getBestiaryDisplayName(entry.databaseName, nullptr);
		}
		return true;
	}
	return false;
}

}

void ProtocolGame::release()
{
	//dispatcher thread

	// Lifecycle race guard (A10): parsePacket dereferences `player` on the
	// network io thread while this function destroys the Player here on the
	// Dispatcher. Connection::close() sets closed=true under connectionLock
	// BEFORE posting this release task, and every parse entry point bails out
	// on closed under that same lock — so once we have held connectionLock
	// once, no parse can still be running or ever start again for this
	// connection, and every `player` access below is race-free.
	//
	// SAFETY INVARIANT: this barrier is only cheap because parse handlers
	// never block waiting for the Dispatcher (they only enqueue tasks). If a
	// future parse path ever waits synchronously on the Dispatcher while
	// holding connectionLock, this lock acquisition deadlocks the Dispatcher.
	if (auto connection = getConnection()) {
		std::lock_guard<std::recursive_mutex> parseDrainBarrier(connection->getConnectionLock());
	}

	if (pendingAttackTargetEvent != 0) {
		g_scheduler.stopEvent(pendingAttackTargetEvent);
		pendingAttackTargetEvent = 0;
		pendingAttackCreatureId = 0;
	}

	if (player && player->client == shared_from_this()) {
		Player* currentPlayer = player;
		currentPlayer->client.reset();

		Tile* currentTile = currentPlayer->getTile();
		if (currentTile && !currentPlayer->isRemoved() && !currentPlayer->isConnecting && !currentPlayer->isAccessPlayer()
				&& !currentTile->hasFlag(TILESTATE_NOLOGOUT)
				&& !currentPlayer->hasCondition(CONDITION_INFIGHT)
				&& g_creatureEvents->playerLogout(currentPlayer)) {
			g_game.removeCreature(currentPlayer);
		}

		currentPlayer->decrementReferenceCounter();
		player = nullptr;
	}

	OutputMessagePool::getInstance().removeProtocolFromAutosend(shared_from_this());
	Protocol::release();
}

void ProtocolGame::login(const std::string& name, uint32_t accountId, OperatingSystem_t operatingSystem)
{
	//dispatcher thread
	auto loginMetrics = std::make_shared<DispatcherLoginMetricsTimer>();
	Player* foundPlayer = g_game.getPlayerByName(name);
	if (!foundPlayer || g_config.getBoolean(ConfigManager::ALLOW_CLONES)) {
		if (g_playerIOManager.isEnabled()) {
			if (!g_playerIOManager.reserveLogin(name)) {
				disconnectClient("Your character is already being loaded.");
				return;
			}

			ProtocolGame_ptr self = getThis();
			if (!g_playerIOManager.enqueueLogin(name,
					[self, name, accountId, operatingSystem, loginMetrics](
						std::shared_ptr<const PlayerIOLoginSnapshot> snapshot,
						const std::string& error) {
						if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
							g_playerIOManager.releaseLogin(
								name, snapshot ? snapshot->playerId : 0);
							self->disconnect();
							return;
						}
						if (self->isConnectionExpired()) {
							g_playerIOManager.releaseLogin(
								name, snapshot ? snapshot->playerId : 0);
							return;
						}

						if (!snapshot) {
							g_playerIOManager.releaseLogin(name);
							self->disconnectClient(error.empty() ?
								"Your character could not be loaded." :
								fmt::format("Your character could not be loaded.\n{:s}", error));
							return;
						}

						if (!g_config.getBoolean(ConfigManager::ALLOW_CLONES) &&
								g_game.getPlayerByName(name)) {
							g_playerIOManager.releaseLogin(name, snapshot->playerId);
							self->disconnectClient("Your character is already logged in.");
							return;
						}
						if (snapshot->accountId != accountId) {
							g_playerIOManager.releaseLogin(name, snapshot->playerId);
							self->disconnectClient(
								"The character account changed while login was being completed.");
							return;
						}

						Player* loadingPlayer = new Player(self);
						loadingPlayer->setName(name);
						loadingPlayer->incrementReferenceCounter();
						loadingPlayer->setID();

						std::string materializeError;
						if (!IOLoginData::materializePlayerLoginSnapshot(
								loadingPlayer, name, snapshot->entries,
								materializeError) ||
								loadingPlayer->getGUID() != snapshot->playerId) {
							g_playerIOManager.releaseLogin(name, snapshot->playerId);
							loadingPlayer->decrementReferenceCounter();
							self->disconnectClient(materializeError.empty() ?
								"Your character snapshot could not be materialized." :
								fmt::format("Your character could not be loaded.\n{:s}",
									materializeError));
							return;
						}
						IOLoginData::finalizeDeferredGuild(loadingPlayer);
						self->player = loadingPlayer;
						g_playerIOManager.releaseLogin(name, snapshot->playerId);
						self->finishNewPlayerLogin(accountId, operatingSystem, loginMetrics);
					})) {
				g_playerIOManager.releaseLogin(name);
				disconnectClient("The character loading service is shutting down.");
			}
			return;
		}

		Player* loadingPlayer = new Player(getThis());
		loadingPlayer->setName(name);
		loadingPlayer->incrementReferenceCounter();
		loadingPlayer->setID();
		player = loadingPlayer;

		DispatcherPhaseMetricsTimer preloadTimer(DispatcherMetricsPhase::LOGIN_PRELOAD);
		if (!IOLoginData::preloadPlayer(player, name)) {
			disconnectClient("Your character could not be loaded.");
			return;
		}
		preloadTimer.stop();

		DispatcherPhaseMetricsTimer fullLoadTimer(DispatcherMetricsPhase::LOGIN_FULL_LOAD);
		if (!IOLoginData::loadPlayerById(player, player->getGUID())) {
			disconnectClient("Your character could not be loaded.");
			return;
		}
		fullLoadTimer.stop();
		finishNewPlayerLogin(accountId, operatingSystem, loginMetrics);
		return;
	} else {
		if (eventConnect != 0 || !g_config.getBoolean(ConfigManager::REPLACE_KICK_ON_LOGIN)) {
			//Already trying to connect
			disconnectClient("You are already logged in.");
			return;
		}

		if (foundPlayer->client) {
			foundPlayer->disconnect();
			foundPlayer->isConnecting = true;

			eventConnect = g_scheduler.addEvent(createSchedulerTask(1000, std::bind(&ProtocolGame::connect, getThis(), foundPlayer->getID(), operatingSystem)));
		} else {
			connect(foundPlayer->getID(), operatingSystem);
		}
	}
	OutputMessagePool::getInstance().addProtocolToAutosend(shared_from_this());
	if (acceptPackets) {
		loginMetrics->markSuccessful();
	}
}

bool ProtocolGame::finishNewPlayerLogin(uint32_t accountId, OperatingSystem_t operatingSystem,
	const std::shared_ptr<DispatcherLoginMetricsTimer>& loginMetrics)
{
	if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
		disconnect();
		return false;
	}

	DispatcherPhaseMetricsTimer policyTimer(DispatcherMetricsPhase::LOGIN_POLICY);
	if (IOBan::isPlayerNamelocked(player->getGUID())) {
		disconnectClient("Your character has been namelocked.");
		return false;
	}

	const bool canBypassServerClosure = player->hasFlag(PlayerFlag_CanAlwaysLogin);

	if (g_game.isEmergencyActive() && !canBypassServerClosure) {
		disconnectClient("Emergency maintenance is in progress.\nPlease try again after the restart.");
		return false;
	}

	if (g_game.isFloorRecoveryLoginRestricted() && !canBypassServerClosure) {
		if (g_game.isFloorRecoveryBlocked()) {
			disconnectClient("Floor recovery is blocked for investigation.\nPlease try again later.");
		} else if (g_game.isFloorCleanRestartReplayPending()) {
			disconnectClient("Clean restart floor replay did not complete.\nPlease try again later.");
		} else {
			disconnectClient("Crash recovery is in progress.\nPlease try again after the restart.");
		}
		return false;
	}

	if (g_game.isFloorCleanSaveWindowActive() && !canBypassServerClosure) {
		disconnectClient("Server save is in progress.\nPlease try again after the restart.");
		return false;
	}

	if (g_game.getGameState() == GAME_STATE_CLOSING && !canBypassServerClosure) {
		disconnectClient("The game is just going down.\nPlease try again later.");
		return false;
	}

	if (g_game.getGameState() == GAME_STATE_CLOSED && !canBypassServerClosure) {
		disconnectClient("Server is currently closed.\nPlease try again later.");
		return false;
	}

	if (g_config.getBoolean(ConfigManager::ONE_PLAYER_ON_ACCOUNT) &&
			player->getAccountType() < ACCOUNT_TYPE_GAMEMASTER &&
			g_game.getPlayerByAccount(player->getAccount())) {
		disconnectClient("You may only login with one character\nof your account at the same time.");
		return false;
	}

	if (!player->hasFlag(PlayerFlag_CannotBeBanned)) {
		BanInfo banInfo;
		if (IOBan::isAccountBanned(accountId, banInfo)) {
			if (banInfo.reason.empty()) {
				banInfo.reason = "(none)";
			}

			if (banInfo.expiresAt > 0) {
				disconnectClient(fmt::format(
					"Your account has been banned until {:s} by {:s}.\n\nReason specified:\n{:s}",
					formatDateShort(banInfo.expiresAt), banInfo.bannedBy, banInfo.reason));
			} else {
				disconnectClient(fmt::format(
					"Your account has been permanently banned by {:s}.\n\nReason specified:\n{:s}",
					banInfo.bannedBy, banInfo.reason));
			}
			return false;
		}
	}

	if (std::size_t currentSlot = clientLogin(*player)) {
		uint8_t retryTime = getWaitTime(currentSlot);
		auto output = OutputMessagePool::getOutputMessage();
		output->addByte(0x16);
		output->addString(fmt::format(
			"Too many players online.\nYou are at place {:d} on the waiting list.", currentSlot));
		output->addByte(retryTime);
		send(output);
		disconnect();
		return false;
	}
	policyTimer.stop();

	DispatcherPhaseMetricsTimer placeTimer(DispatcherMetricsPhase::LOGIN_PLACE_CREATURE);
	player->setOperatingSystem(operatingSystem);

	if (!g_game.placeCreature(player, player->getLoginPosition())) {
		if (!g_game.placeCreature(player, player->getTemplePosition(), false, true)) {
			disconnectClient("Temple position is wrong. Contact the administrator.");
			return false;
		}
	}
	placeTimer.stop();

	DispatcherPhaseMetricsTimer postPlaceTimer(DispatcherMetricsPhase::LOGIN_POST_PLACE);
	if (operatingSystem >= CLIENTOS_OTCLIENT_LINUX) {
		player->registerCreatureEvent("ExtendedOpcode");
	}

	player->lastIP = player->getIP();
	player->lastLoginSaved = std::max<time_t>(time(nullptr), player->lastLoginSaved + 1);
	acceptPackets = true;
	sendBestiaryCharmsData();
	sendProfessionData();
	postPlaceTimer.stop();

	OutputMessagePool::getInstance().addProtocolToAutosend(shared_from_this());
	loginMetrics->markSuccessful();
	return true;
}

void ProtocolGame::connect(uint32_t playerId, OperatingSystem_t operatingSystem)
{
	eventConnect = 0;

	Player* foundPlayer = g_game.getPlayerByID(playerId);
	if (!foundPlayer || foundPlayer->client) {
		disconnectClient("You are already logged in.");
		return;
	}

	if (isConnectionExpired()) {
		//ProtocolGame::release() has been called at this point and the Connection object
		//no longer exists, so we return to prevent leakage of the Player.
		return;
	}

	player = foundPlayer;
	player->incrementReferenceCounter();

	g_chat->removeUserFromAllChannels(*player);
	//player->clearModalWindows();
	player->setOperatingSystem(operatingSystem);
	player->isConnecting = false;

	player->client = getThis();
	sendAddCreature(player, player->getPosition(), 0, false);
	player->restoreOpenContainerState();
	if (operatingSystem >= CLIENTOS_OTCLIENT_LINUX) {
		sendExtendedOpcode(CONTAINER_RESTORE_EXTENDED_OPCODE, "end");
	}
	player->lastIP = player->getIP();
	player->lastLoginSaved = std::max<time_t>(time(nullptr), player->lastLoginSaved + 1);
	player->resetIdleTime();
	acceptPackets = true;
	sendBestiaryCharmsData();
	sendProfessionData();
}

void ProtocolGame::logout(bool displayEffect, bool forced)
{
	//dispatcher thread
	if (!player) {
		return;
	}

	if (!player->isRemoved()) {
		if (!forced) {
			if (!player->isAccessPlayer()) {
				if (player->getTile()->hasFlag(TILESTATE_NOLOGOUT)) {
					player->sendCancelMessage(RETURNVALUE_YOUCANNOTLOGOUTHERE);
					return;
				}

				if (!player->getTile()->hasFlag(TILESTATE_PROTECTIONZONE) && player->hasCondition(CONDITION_INFIGHT)) {
					player->sendCancelMessage(RETURNVALUE_YOUMAYNOTLOGOUTDURINGAFIGHT);
					return;
				}
			}

			//scripting event - onLogout
			if (!g_creatureEvents->playerLogout(player)) {
				//Let the script handle the error message
				return;
			}
		}

		if (displayEffect && player->getHealth() > 0 && !player->isInGhostMode()) {
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		}
	}

	DispatcherPhaseMetricsTimer logoutTimer(DispatcherMetricsPhase::LOGOUT_ACCEPTED_TOTAL);
	disconnect();

	g_game.removeCreature(player);
}

void ProtocolGame::onRecvFirstMessage(NetworkMessage& msg)
{
	if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
		disconnect();
		return;
	}

	OperatingSystem_t operatingSystem = static_cast<OperatingSystem_t>(msg.get<uint16_t>());
	version = msg.get<uint16_t>();

	//msg.skipBytes(7); // U32 client version, U8 client type, U16 dat revision

	if (!Protocol::RSA_decrypt(msg)) {
		disconnect();
		return;
	}

	xtea::key key;
	key[0] = msg.get<uint32_t>();
	key[1] = msg.get<uint32_t>();
	key[2] = msg.get<uint32_t>();
	key[3] = msg.get<uint32_t>();
	enableXTEAEncryption();
	setXTEAKey(std::move(key));

	if (operatingSystem >= CLIENTOS_OTCLIENT_LINUX) {
		NetworkMessage opcodeMessage;
		opcodeMessage.addByte(0x32);
		opcodeMessage.addByte(0x00);
		opcodeMessage.add<uint16_t>(0x00);
		writeToOutputBuffer(opcodeMessage);
	}

	msg.skipBytes(1); // gamemaster flag

	/*std::string sessionKey = msg.getString();

	auto sessionArgs = explodeString(sessionKey, "\n", 4);
	if (sessionArgs.size() != 4) {
		disconnect();
		return;
	}

	std::string& accountName = sessionArgs[0];
	std::string& password = sessionArgs[1];
	std::string& token = sessionArgs[2];
	uint32_t tokenTime = 0;
	try {
		tokenTime = std::stoul(sessionArgs[3]);
	} catch (const std::invalid_argument&) {
		disconnectClient("Malformed token packet.");
		return;
	} catch (const std::out_of_range&) {
		disconnectClient("Token time is too long.");
		return;
	}*/

	uint32_t accountNumber = msg.get<uint32_t>();
	if (accountNumber == 0) {
		disconnectClient("You must enter your account number.");
		return;
	}

	std::string accountName = std::to_string(accountNumber);
	if (accountName.empty()) {
		disconnectClient("You must enter your account number.");
		return;
	}

	std::string characterName = msg.getString();
	const std::string& password = msg.getString();

	std::string token = "";
	uint32_t tokenTime = 0;

	/*uint32_t timeStamp = msg.get<uint32_t>();
	uint8_t randNumber = msg.getByte();
	if (challengeTimestamp != timeStamp || challengeRandom != randNumber) {
		disconnect();
		return;
	}*/

	if (version < CLIENT_VERSION_MIN || version > CLIENT_VERSION_MAX) {
		disconnectClient(fmt::format("Only clients with protocol {:s} allowed!", CLIENT_VERSION_STR));
		return;
	}

	if (g_game.getGameState() == GAME_STATE_STARTUP) {
		disconnectClient("Gameworld is starting up. Please wait.");
		return;
	}

	if (g_game.getGameState() == GAME_STATE_MAINTAIN) {
		disconnectClient("Gameworld is under maintenance. Please re-connect in a while.");
		return;
	}

	BanInfo banInfo;
	if (IOBan::isIpBanned(getIP(), banInfo)) {
		if (banInfo.reason.empty()) {
			banInfo.reason = "(none)";
		}

		disconnectClient(fmt::format("Your IP has been banned until {:s} by {:s}.\n\nReason specified:\n{:s}", formatDateShort(banInfo.expiresAt), banInfo.bannedBy, banInfo.reason));
		return;
	}

	uint32_t accountId = IOLoginData::gameworldAuthentication(accountName, password, characterName, token, tokenTime);
	if (accountId == 0) {
		disconnectClient("Account name or password is not correct.");
		return;
	}

	g_dispatcher.addTask(createTask(std::bind(&ProtocolGame::login, getThis(), characterName, accountId, operatingSystem)));
}

void ProtocolGame::onConnect()
{
	/*auto output = OutputMessagePool::getOutputMessage();
	static std::random_device rd;
	static std::ranlux24 generator(rd());
	static std::uniform_int_distribution<uint16_t> randNumber(0x00, 0xFF);

	// Skip checksum
	output->skipBytes(sizeof(uint32_t));

	// Packet length & type
	output->add<uint16_t>(0x0006);
	output->addByte(0x1F);

	// Add timestamp & random number
	challengeTimestamp = static_cast<uint32_t>(time(nullptr));
	output->add<uint32_t>(challengeTimestamp);

	challengeRandom = randNumber(generator);
	output->addByte(challengeRandom);

	// Go back and write checksum
	output->skipBytes(-12);
	output->add<uint32_t>(adlerChecksum(output->getOutputBuffer() + sizeof(uint32_t), 8));

	send(output);*/
}

void ProtocolGame::disconnectClient(const std::string& message) const
{
	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0x14);
	output->addString(message);
	send(output);
	disconnect();
}

OutputMessage_ptr ProtocolGame::appendToOutputBuffer(const NetworkMessage& msg)
{
	auto out = getOutputBuffer(msg.getLength());
	out->append(msg);
	return out;
}

void ProtocolGame::writeToOutputBuffer(const NetworkMessage& msg)
{
	appendToOutputBuffer(msg);
}

void ProtocolGame::sendExtendedOpcode(uint8_t opcode, const std::string& buffer)
{
	NetworkMessage msg;
	msg.addByte(0x32);
	msg.addByte(opcode);
	msg.addString(buffer);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendProfessionData()
{
	if (!player || player->getOperatingSystem() < CLIENTOS_OTCLIENT_LINUX) {
		return;
	}

	sendExtendedOpcode(PROFESSIONS_EXTENDED_OPCODE,
		fmt::format("1|alchemy|{:d}|{:d}", player->getAlchemyLevel(),
			static_cast<uint32_t>(player->getAlchemyPercent())));
}

void ProtocolGame::parsePacket(NetworkMessage& msg)
{
	if (!acceptPackets || g_game.getGameState() == GAME_STATE_SHUTDOWN || msg.getLength() == 0) {
		return;
	}

	uint8_t recvbyte = msg.getByte();

	if (!player) {
		if (recvbyte == 0x0F) {
			disconnect();
		}

		return;
	}

	//a dead player can not performs actions
	if (player->isRemoved() || player->getHealth() <= 0) {
		if (recvbyte == 0x0F) {
			disconnect();
			return;
		}

		if (recvbyte != 0x14) {
			return;
		}
	}

	switch (recvbyte) {
		case 0x14: g_dispatcher.addTask(createTask(std::bind(&ProtocolGame::logout, getThis(), true, false))); break;
		case 0x1D: addGameTask(&Game::playerReceivePingBack, player->getID()); break;
		case 0x1E: addGameTask(&Game::playerReceivePing, player->getID()); break;
		case 0x32: parseExtendedOpcode(msg); break; //otclient extended opcode
		case 0x64: parseAutoWalk(msg); break;
		case 0x65: if (checkWalkRequestRateLimit()) { addGameTask(&Game::playerMove, player->getID(), DIRECTION_NORTH); } break;
		case 0x66: if (checkWalkRequestRateLimit()) { addGameTask(&Game::playerMove, player->getID(), DIRECTION_EAST); } break;
		case 0x67: if (checkWalkRequestRateLimit()) { addGameTask(&Game::playerMove, player->getID(), DIRECTION_SOUTH); } break;
		case 0x68: if (checkWalkRequestRateLimit()) { addGameTask(&Game::playerMove, player->getID(), DIRECTION_WEST); } break;
		case 0x69: addGameTask(&Game::playerStopAutoWalk, player->getID()); break;
		case 0x6A: if (checkWalkRequestRateLimit()) { addGameTask(&Game::playerMove, player->getID(), DIRECTION_NORTHEAST); } break;
		case 0x6B: if (checkWalkRequestRateLimit()) { addGameTask(&Game::playerMove, player->getID(), DIRECTION_SOUTHEAST); } break;
		case 0x6C: if (checkWalkRequestRateLimit()) { addGameTask(&Game::playerMove, player->getID(), DIRECTION_SOUTHWEST); } break;
		case 0x6D: if (checkWalkRequestRateLimit()) { addGameTask(&Game::playerMove, player->getID(), DIRECTION_NORTHWEST); } break;
		case 0x6F: addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerTurn, player->getID(), DIRECTION_NORTH); break;
		case 0x70: addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerTurn, player->getID(), DIRECTION_EAST); break;
		case 0x71: addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerTurn, player->getID(), DIRECTION_SOUTH); break;
		case 0x72: addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerTurn, player->getID(), DIRECTION_WEST); break;
		case 0x77: parseEquipObject(msg); break;
		case 0x78: parseThrow(msg); break;
		case 0x79: parseLookInShop(msg); break;
		case 0x7A: parsePlayerPurchase(msg); break;
		case 0x7B: parsePlayerSale(msg); break;
		case 0x7C: addGameTask(&Game::playerCloseShop, player->getID()); break;
		case 0x7D: parseRequestTrade(msg); break;
		case 0x7E: parseLookInTrade(msg); break;
		case 0x7F: addGameTask(&Game::playerAcceptTrade, player->getID()); break;
		case 0x80: addGameTask(&Game::playerCloseTrade, player->getID()); break;
		case 0x82: parseUseItem(msg); break;
		case 0x83: parseUseItemEx(msg); break;
		case 0x84: parseUseWithCreature(msg); break;
		case 0x85: parseRotateItem(msg); break;
		case 0x87: parseCloseContainer(msg); break;
		case 0x88: parseUpArrowContainer(msg); break;
		case 0x89: parseTextWindow(msg); break;
		case 0x8A: parseHouseWindow(msg); break;
		case 0x8B: parseWrapItem(msg); break;
		case 0x8C: parseLookAt(msg); break;
		case 0x8D: parseLookInBattleList(msg); break;
		case 0x8E: /* join aggression */ break;
		case 0x96: parseSay(msg); break;
		case 0x97: addGameTask(&Game::playerRequestChannels, player->getID()); break;
		case 0x98: parseOpenChannel(msg); break;
		case 0x99: parseCloseChannel(msg); break;
		case 0x9A: parseOpenPrivateChannel(msg); break;
		case 0x9E: addGameTask(&Game::playerCloseNpcChannel, player->getID()); break;
		case 0xA0: parseFightModes(msg); break;
		case 0xA1: parseAttack(msg); break;
		case 0xA2: parseFollow(msg); break;
		case 0xA3: parseInviteToParty(msg); break;
		case 0xA4: parseJoinParty(msg); break;
		case 0xA5: parseRevokePartyInvite(msg); break;
		case 0xA6: parsePassPartyLeadership(msg); break;
		case 0xA7: addGameTask(&Game::playerLeaveParty, player->getID()); break;
		case 0xA8: parseEnableSharedPartyExperience(msg); break;
		case 0xAA: addGameTask(&Game::playerCreatePrivateChannel, player->getID()); break;
		case 0xAB: parseChannelInvite(msg); break;
		case 0xAC: parseChannelExclude(msg); break;
		case 0xBE: addGameTask(&Game::playerCancelAttackAndFollow, player->getID()); break;
		case 0xC9: /* update tile */ break;
		case 0xCA: parseUpdateContainer(msg); break;
		//case 0xCB: parseBrowseField(msg); break;
		case 0xCC: parseSeekInContainer(msg); break;
		case 0xD2: addGameTask(&Game::playerRequestOutfit, player->getID()); break;
		case 0xD3: parseSetOutfit(msg); break;
		case 0xD4: parseToggleMount(msg); break;
		case 0xDC: parseAddVip(msg); break;
		case 0xDD: parseRemoveVip(msg); break;
		case 0xDE: parseEditVip(msg); break;
		case 0xE1: parseBestiaryRequest(msg); break;
		case 0xE2: parseBestiaryRequestOverview(msg); break;
		case 0xE3: parseBestiaryRequestSearch(msg); break;
		case 0xE4: parseCharmAction(msg); break;
		case 0xE6: parseBugReport(msg); break;
		case 0xE7: /* thank you */ break;
		case 0xE8: parseDebugAssert(msg); break;
		case 0xF0: addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerShowQuestLog, player->getID()); break;
		case 0xF1: parseQuestLine(msg); break;
		case 0xF2: parseRuleViolationReport(msg); break;
		case 0xF3: /* get object info */ break;
		/*case 0xF4: parseMarketLeave(); break;
		case 0xF5: parseMarketBrowse(msg); break;
		case 0xF6: parseMarketCreateOffer(msg); break;
		case 0xF7: parseMarketCancelOffer(msg); break;
		case 0xF8: parseMarketAcceptOffer(msg); break;
		case 0xF9: parseModalWindowAnswer(msg); break;*/

		default:
			// std::cout << "Player: " << player->getName() << " sent an unknown packet header: 0x" << std::hex << static_cast<uint16_t>(recvbyte) << std::dec << "!" << std::endl;
			break;
	}

	if (msg.isOverrun()) {
		disconnect();
	}
}

void ProtocolGame::GetTileDescription(const Tile* tile, NetworkMessage& msg)
{
	//msg.add<uint16_t>(0x00); //environmental effects

	int32_t count;
	Item* ground = tile->getGround();
	if (ground) {
		msg.addItem(ground);
		count = 1;
	} else {
		count = 0;
	}

	const TileItemVector* items = tile->getItemList();
	if (items) {
		for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
			msg.addItem(*it);

			if (++count == 10) {
				break;
			}
		}
	}

	const CreatureVector* creatures = tile->getCreatures();
	if (creatures) {
		for (auto it = creatures->rbegin(), end = creatures->rend(); it != end; ++it) {
			const Creature* creature = (*it);
			if (!player->canSeeCreature(creature)) {
				continue;
			}

			bool known;
			uint32_t removedKnown;
			checkCreatureAsKnown(creature->getID(), known, removedKnown);
			AddCreature(msg, creature, known, removedKnown);
			++count;
		}
	}

	if (items && count < 10) {
		for (auto it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end; ++it) {
			msg.addItem(*it);

			if (++count == 10) {
				return;
			}
		}
	}
}

void ProtocolGame::GetMapDescription(int32_t x, int32_t y, int32_t z, int32_t width,
                                     int32_t height, NetworkMessage& msg)
{
	int32_t skip = -1;
	int32_t startz, endz, zstep;

	if (z > 7) {
		startz = z - 2;
		endz = std::min<int32_t>(MAP_MAX_LAYERS - 1, z + 2);
		zstep = 1;
	} else {
		startz = 7;
		endz = 0;
		zstep = -1;
	}

	for (int32_t nz = startz; nz != endz + zstep; nz += zstep) {
		GetFloorDescription(msg, x, y, nz, width, height, z - nz, skip);
	}

	if (skip >= 0) {
		msg.addByte(skip);
		msg.addByte(0xFF);
	}
}

void ProtocolGame::GetFloorDescription(NetworkMessage& msg, int32_t x, int32_t y, int32_t z,
                                       int32_t width, int32_t height, int32_t offset, int32_t& skip)
{
	for (int32_t nx = 0; nx < width; nx++) {
		for (int32_t ny = 0; ny < height; ny++) {
			Tile* tile = g_game.map.getTile(x + nx + offset, y + ny + offset, z);
			if (tile) {
				if (skip >= 0) {
					msg.addByte(skip);
					msg.addByte(0xFF);
				}

				skip = 0;
				GetTileDescription(tile, msg);
			} else if (skip == 0xFE) {
				msg.addByte(0xFF);
				msg.addByte(0xFF);
				skip = -1;
			} else {
				++skip;
			}
		}
	}
}

void ProtocolGame::checkCreatureAsKnown(uint32_t id, bool& known, uint32_t& removedKnown)
{
	auto result = knownCreatureSet.insert(id);
	if (!result.second) {
		known = true;
		return;
	}

	known = false;

	//if (knownCreatureSet.size() > 1300) {
	if (knownCreatureSet.size() > 150) {
		// Look for a creature to remove
		for (auto it = knownCreatureSet.begin(), end = knownCreatureSet.end(); it != end; ++it) {
			Creature* creature = g_game.getCreatureByID(*it);
			if (!canSee(creature)) {
				removedKnown = *it;
				knownCreatureSet.erase(it);
				return;
			}
		}

		// Bad situation. Let's just remove anyone.
		auto it = knownCreatureSet.begin();
		if (*it == id) {
			++it;
		}

		removedKnown = *it;
		knownCreatureSet.erase(it);
	} else {
		removedKnown = 0;
	}
}

bool ProtocolGame::canSee(const Creature* c) const
{
	if (!c || !player || c->isRemoved()) {
		return false;
	}

	if (!player->canSeeCreature(c)) {
		return false;
	}

	return canSee(c->getPosition());
}

bool ProtocolGame::canSee(const Position& pos) const
{
	return canSee(pos.x, pos.y, pos.z);
}

bool ProtocolGame::canSee(int32_t x, int32_t y, int32_t z) const
{
	if (!player) {
		return false;
	}

	const Position& myPos = player->getPosition();
	if (myPos.z <= 7) {
		//we are on ground level or above (7 -> 0)
		//view is from 7 -> 0
		if (z > 7) {
			return false;
		}
	} else { // if (myPos.z >= 8) {
		//we are underground (8 -> 15)
		//view is +/- 2 from the floor we stand on
		if (std::abs(myPos.getZ() - z) > 2) {
			return false;
		}
	}

	//negative offset means that the action taken place is on a lower floor than ourself
	int32_t offsetz = myPos.getZ() - z;
	if ((x >= myPos.getX() - Map::maxClientViewportX + offsetz) && (x <= myPos.getX() + (Map::maxClientViewportX + 1) + offsetz) &&
		(y >= myPos.getY() - Map::maxClientViewportY + offsetz) && (y <= myPos.getY() + (Map::maxClientViewportY + 1) + offsetz)) {
		return true;
	}
	return false;
}

// Parse methods
void ProtocolGame::parseChannelInvite(NetworkMessage& msg)
{
	const std::string name = msg.getString();
	addGameTask(&Game::playerChannelInvite, player->getID(), name);
}

void ProtocolGame::parseChannelExclude(NetworkMessage& msg)
{
	const std::string name = msg.getString();
	addGameTask(&Game::playerChannelExclude, player->getID(), name);
}

void ProtocolGame::parseOpenChannel(NetworkMessage& msg)
{
	uint16_t channelId = msg.get<uint16_t>();
	addGameTask(&Game::playerOpenChannel, player->getID(), channelId);
}

void ProtocolGame::parseCloseChannel(NetworkMessage& msg)
{
	uint16_t channelId = msg.get<uint16_t>();
	addGameTask(&Game::playerCloseChannel, player->getID(), channelId);
}

void ProtocolGame::parseOpenPrivateChannel(NetworkMessage& msg)
{
	// The receiver name triggers a synchronous DB lookup (formatPlayerName)
	// on the Dispatcher; keep it to a human pace.
	static constexpr uint32_t OPEN_PRIVATE_CHANNEL_RATE_LIMIT_MS = 1000;

	const std::string receiver = msg.getString();
	if (!checkClientRequestRateLimit(nextOpenPrivateChannelRequest, OPEN_PRIVATE_CHANNEL_RATE_LIMIT_MS)) {
		return;
	}
	addGameTask(&Game::playerOpenPrivateChannel, player->getID(), receiver);
}

void ProtocolGame::parseAutoWalk(NetworkMessage& msg)
{
	uint8_t numdirs = msg.getByte();
	//if (numdirs == 0 || (msg.getBufferPosition() + numdirs) != (msg.getLength() + 8)) {
	if (numdirs == 0 || (msg.getBufferPosition() + numdirs) != (msg.getLength() + 4)) {
		return;
	}

	msg.skipBytes(numdirs);

	std::vector<Direction> path;
	path.reserve(numdirs);

	for (uint8_t i = 0; i < numdirs; ++i) {
		uint8_t rawdir = msg.getPreviousByte();
		switch (rawdir) {
			case 1: path.push_back(DIRECTION_EAST); break;
			case 2: path.push_back(DIRECTION_NORTHEAST); break;
			case 3: path.push_back(DIRECTION_NORTH); break;
			case 4: path.push_back(DIRECTION_NORTHWEST); break;
			case 5: path.push_back(DIRECTION_WEST); break;
			case 6: path.push_back(DIRECTION_SOUTHWEST); break;
			case 7: path.push_back(DIRECTION_SOUTH); break;
			case 8: path.push_back(DIRECTION_SOUTHEAST); break;
			default: break;
		}
	}

	if (path.empty()) {
		return;
	}

	if (!checkWalkRequestRateLimit()) {
		return;
	}

	addGameTask(&Game::playerAutoWalk, player->getID(), std::move(path));
}

void ProtocolGame::parseSetOutfit(NetworkMessage& msg)
{
	Outfit_t newOutfit;
	newOutfit.lookType = msg.get<uint16_t>();
	newOutfit.lookHead = msg.getByte();
	newOutfit.lookBody = msg.getByte();
	newOutfit.lookLegs = msg.getByte();
	newOutfit.lookFeet = msg.getByte();
	//newOutfit.lookAddons = msg.getByte();
	//newOutfit.lookMount = msg.get<uint16_t>();
	addGameTask(&Game::playerChangeOutfit, player->getID(), newOutfit);
}

void ProtocolGame::parseToggleMount(NetworkMessage& msg)
{
	bool mount = msg.getByte() != 0;
	addGameTask(&Game::playerToggleMount, player->getID(), mount);
}

bool ProtocolGame::checkClientRequestRateLimit(int64_t& nextAllowedRequest, uint32_t cooldownMs)
{
	const int64_t now = OTSYS_TIME();
	if (now < nextAllowedRequest) {
		return false;
	}

	nextAllowedRequest = now + cooldownMs;
	return true;
}

bool ProtocolGame::checkWalkRequestRateLimit()
{
	static constexpr uint32_t WALK_REQUEST_RATE_LIMIT_MS = 10;
	return checkClientRequestRateLimit(nextWalkRequest, WALK_REQUEST_RATE_LIMIT_MS);
}

void ProtocolGame::handleAttackTargetRequest(uint32_t creatureId)
{
	static constexpr int64_t ATTACK_TARGET_RATE_LIMIT_MS = 30;

	// Cancel target must remain immediate. Coalescing only applies to selecting targets.
	if (creatureId == 0) {
		if (pendingAttackTargetEvent != 0) {
			g_scheduler.stopEvent(pendingAttackTargetEvent);
			pendingAttackTargetEvent = 0;
			pendingAttackCreatureId = 0;
		}

		nextAttackTargetRequest = OTSYS_TIME() + ATTACK_TARGET_RATE_LIMIT_MS;
		addGameTask(&Game::playerSetAttackedCreature, player->getID(), creatureId);
		return;
	}

	const int64_t now = OTSYS_TIME();
	if (pendingAttackTargetEvent == 0 && now >= nextAttackTargetRequest) {
		nextAttackTargetRequest = now + ATTACK_TARGET_RATE_LIMIT_MS;
		addGameTask(&Game::playerSetAttackedCreature, player->getID(), creatureId);
		return;
	}

	pendingAttackCreatureId = creatureId;
	if (pendingAttackTargetEvent != 0) {
		return;
	}

	const int64_t delay = std::max<int64_t>(1, nextAttackTargetRequest - now);
	pendingAttackTargetEvent = g_scheduler.addEvent(createSchedulerTask(static_cast<uint32_t>(delay), std::bind(&ProtocolGame::flushPendingAttackTarget, getThis())));
}

void ProtocolGame::flushPendingAttackTarget()
{
	pendingAttackTargetEvent = 0;

	const uint32_t creatureId = pendingAttackCreatureId;
	pendingAttackCreatureId = 0;
	if (creatureId == 0 || !player) {
		return;
	}

	nextAttackTargetRequest = OTSYS_TIME() + 30;
	addGameTask(&Game::playerSetAttackedCreature, player->getID(), creatureId);
}

void ProtocolGame::parseBestiaryRequest(NetworkMessage& /*msg*/)
{
	static constexpr uint32_t BESTIARY_REQUEST_RATE_LIMIT_MS = 500;
	if (!checkClientRequestRateLimit(nextBestiaryRequest, BESTIARY_REQUEST_RATE_LIMIT_MS)) {
		return;
	}

	g_dispatcher.addTask(createTask(std::bind(&ProtocolGame::sendBestiaryRaces, getThis())));
	g_dispatcher.addTask(createTask(std::bind(&ProtocolGame::sendBestiaryCharmsData, getThis())));
}

void ProtocolGame::parseBestiaryRequestOverview(NetworkMessage& msg)
{
	static constexpr uint16_t MAX_BESTIARY_SEARCH_CREATURES = 256;
	static constexpr uint32_t BESTIARY_OVERVIEW_RATE_LIMIT_MS = 250;

	const bool search = msg.getByte() != 0;
	std::string categoryName = BESTIARY_CATEGORY_CREATURES;
	std::vector<uint16_t> creatureIds;
	if (search) {
		const uint16_t creatureCount = msg.get<uint16_t>();
		if (creatureCount > MAX_BESTIARY_SEARCH_CREATURES) {
			return;
		}

		creatureIds.reserve(creatureCount);
		for (uint16_t i = 0; i < creatureCount; ++i) {
			creatureIds.emplace_back(msg.get<uint16_t>());
		}
	} else {
		categoryName = msg.getString();
	}

	if (!checkClientRequestRateLimit(nextBestiaryOverviewRequest, BESTIARY_OVERVIEW_RATE_LIMIT_MS)) {
		return;
	}

	g_dispatcher.addTask(createTask(std::bind(&ProtocolGame::sendBestiaryOverview, getThis(), categoryName, search, creatureIds)));
}

void ProtocolGame::parseBestiaryRequestSearch(NetworkMessage& msg)
{
	static constexpr uint32_t BESTIARY_SEARCH_RATE_LIMIT_MS = 150;

	const uint16_t creatureId = msg.get<uint16_t>();
	if (!checkClientRequestRateLimit(nextBestiarySearchRequest, BESTIARY_SEARCH_RATE_LIMIT_MS)) {
		return;
	}

	g_dispatcher.addTask(createTask(std::bind(&ProtocolGame::sendBestiaryMonsterData, getThis(), creatureId)));
}

void ProtocolGame::parseCharmAction(NetworkMessage& msg)
{
	static constexpr uint32_t CHARM_ACTION_RATE_LIMIT_MS = 1000;

	const uint8_t charmId = msg.getByte();
	const uint8_t action = msg.getByte();
	msg.get<uint16_t>(); // Legacy race id, unused by global charms.

	if (!checkClientRequestRateLimit(nextCharmActionRequest, CHARM_ACTION_RATE_LIMIT_MS)) {
		return;
	}

	if (action == 0) {
		addGameTask(&Game::playerUnlockCharm, player->getID(), charmId);
	}
}

void ProtocolGame::parseUseItem(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	uint8_t index = msg.getByte();
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerUseItem, player->getID(), pos, stackpos, index, spriteId);
}

void ProtocolGame::parseUseItemEx(NetworkMessage& msg)
{
	Position fromPos = msg.getPosition();
	uint16_t fromSpriteId = msg.get<uint16_t>();
	uint8_t fromStackPos = msg.getByte();
	Position toPos = msg.getPosition();
	uint16_t toSpriteId = msg.get<uint16_t>();
	uint8_t toStackPos = msg.getByte();
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerUseItemEx, player->getID(), fromPos, fromStackPos, fromSpriteId, toPos, toStackPos, toSpriteId);
}

void ProtocolGame::parseUseWithCreature(NetworkMessage& msg)
{
	Position fromPos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t fromStackPos = msg.getByte();
	uint32_t creatureId = msg.get<uint32_t>();
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerUseWithCreature, player->getID(), fromPos, fromStackPos, creatureId, spriteId);
}

void ProtocolGame::parseCloseContainer(NetworkMessage& msg)
{
	uint8_t cid = msg.getByte();
	addGameTask(&Game::playerCloseContainer, player->getID(), cid);
}

void ProtocolGame::parseUpArrowContainer(NetworkMessage& msg)
{
	uint8_t cid = msg.getByte();
	addGameTask(&Game::playerMoveUpContainer, player->getID(), cid);
}

void ProtocolGame::parseUpdateContainer(NetworkMessage& msg)
{
	uint8_t cid = msg.getByte();
	addGameTask(&Game::playerUpdateContainer, player->getID(), cid);
}

void ProtocolGame::parseThrow(NetworkMessage& msg)
{
	Position fromPos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t fromStackpos = msg.getByte();
	Position toPos = msg.getPosition();
	uint8_t count = msg.getByte();

	if (toPos != fromPos) {
		addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerMoveThing, player->getID(), fromPos, spriteId, fromStackpos, toPos, count);
	}
}

void ProtocolGame::parseLookAt(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	msg.skipBytes(2); // spriteId
	uint8_t stackpos = msg.getByte();
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerLookAt, player->getID(), pos, stackpos);
}

void ProtocolGame::parseLookInBattleList(NetworkMessage& msg)
{
	uint32_t creatureId = msg.get<uint32_t>();
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerLookInBattleList, player->getID(), creatureId);
}

void ProtocolGame::parseSay(NetworkMessage& msg)
{
	std::string receiver;
	uint16_t channelId;

	SpeakClasses type = static_cast<SpeakClasses>(msg.getByte());
	switch (type) {
		//case TALKTYPE_PRIVATE_TO:
		//case TALKTYPE_PRIVATE_RED_TO:
		case TALKTYPE_PRIVATE:
		case TALKTYPE_PRIVATE_RED:
			receiver = msg.getString();
			channelId = 0;
			break;

		case TALKTYPE_CHANNEL_Y:
		case TALKTYPE_CHANNEL_R1:
		case TALKTYPE_CHANNEL_R2:
			channelId = msg.get<uint16_t>();
			break;

		default:
			channelId = 0;
			break;
	}

	const std::string text = msg.getString();
	if (text.length() > 255) {
		return;
	}

	addGameTask(&Game::playerSay, player->getID(), channelId, type, receiver, text);
}

void ProtocolGame::parseFightModes(NetworkMessage& msg)
{
	uint8_t rawFightMode = msg.getByte(); // 1 - offensive, 2 - balanced, 3 - defensive
	uint8_t rawChaseMode = msg.getByte(); // 0 - stand while fighting, 1 - chase opponent
	uint8_t rawSecureMode = msg.getByte(); // 0 - can't attack unmarked, 1 - can attack unmarked
	// uint8_t rawPvpMode = msg.getByte(); // pvp mode introduced in 10.0

	fightMode_t fightMode;
	if (rawFightMode == 1) {
		fightMode = FIGHTMODE_ATTACK;
	} else if (rawFightMode == 2) {
		fightMode = FIGHTMODE_BALANCED;
	} else {
		fightMode = FIGHTMODE_DEFENSE;
	}

	addGameTask(&Game::playerSetFightModes, player->getID(), fightMode, rawChaseMode != 0, rawSecureMode != 0);
}

void ProtocolGame::parseAttack(NetworkMessage& msg)
{
	uint32_t creatureId = msg.get<uint32_t>();
	// msg.get<uint32_t>(); creatureId (same as above)
	handleAttackTargetRequest(creatureId);
}

void ProtocolGame::parseFollow(NetworkMessage& msg)
{
	uint32_t creatureId = msg.get<uint32_t>();
	// msg.get<uint32_t>(); creatureId (same as above)
	addGameTask(&Game::playerFollowCreature, player->getID(), creatureId);
}

void ProtocolGame::parseEquipObject(NetworkMessage& msg)
{
	uint16_t spriteId = msg.get<uint16_t>();
	// msg.get<uint8_t>();

	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerEquipItem, player->getID(), spriteId);
}

void ProtocolGame::parseTextWindow(NetworkMessage& msg)
{
	uint32_t windowTextId = msg.get<uint32_t>();
	const std::string newText = msg.getString();
	addGameTask(&Game::playerWriteItem, player->getID(), windowTextId, newText);
}

void ProtocolGame::parseHouseWindow(NetworkMessage& msg)
{
	uint8_t doorId = msg.getByte();
	uint32_t id = msg.get<uint32_t>();
	const std::string text = msg.getString();
	addGameTask(&Game::playerUpdateHouseWindow, player->getID(), doorId, id, text);
}

void ProtocolGame::parseWrapItem(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerWrapItem, player->getID(), pos, stackpos, spriteId);
}

void ProtocolGame::parseLookInShop(NetworkMessage& msg)
{
	uint16_t id = msg.get<uint16_t>();
	uint8_t count = msg.getByte();
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerLookInShop, player->getID(), id, count);
}

void ProtocolGame::parsePlayerPurchase(NetworkMessage& msg)
{
	uint16_t id = msg.get<uint16_t>();
	uint8_t count = msg.getByte();
	uint8_t amount = msg.getByte();
	bool ignoreCap = msg.getByte() != 0;
	bool inBackpacks = msg.getByte() != 0;
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerPurchaseItem, player->getID(), id, count, amount, ignoreCap, inBackpacks);
}

void ProtocolGame::parsePlayerSale(NetworkMessage& msg)
{
	uint16_t id = msg.get<uint16_t>();
	uint8_t count = msg.getByte();
	uint8_t amount = msg.getByte();
	bool ignoreEquipped = msg.getByte() != 0;
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerSellItem, player->getID(), id, count, amount, ignoreEquipped);
}

void ProtocolGame::parseRequestTrade(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	uint32_t playerId = msg.get<uint32_t>();
	addGameTask(&Game::playerRequestTrade, player->getID(), pos, stackpos, playerId, spriteId);
}

void ProtocolGame::parseLookInTrade(NetworkMessage& msg)
{
	bool counterOffer = (msg.getByte() == 0x01);
	uint8_t index = msg.getByte();
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerLookInTrade, player->getID(), counterOffer, index);
}

void ProtocolGame::parseAddVip(NetworkMessage& msg)
{
	// Unknown names trigger a synchronous DB lookup (getGuidByNameEx) on the
	// Dispatcher; keep additions to a human pace.
	static constexpr uint32_t ADD_VIP_RATE_LIMIT_MS = 1000;

	const std::string name = msg.getString();
	if (!checkClientRequestRateLimit(nextAddVipRequest, ADD_VIP_RATE_LIMIT_MS)) {
		return;
	}
	addGameTask(&Game::playerRequestAddVip, player->getID(), name);
}

void ProtocolGame::parseRemoveVip(NetworkMessage& msg)
{
	uint32_t guid = msg.get<uint32_t>();
	addGameTask(&Game::playerRequestRemoveVip, player->getID(), guid);
}

void ProtocolGame::parseEditVip(NetworkMessage& msg)
{
	// Every edit writes a synchronous DB UPDATE carrying the (client-sized)
	// description; keep edits to a human pace and the text bounded.
	static constexpr uint32_t EDIT_VIP_RATE_LIMIT_MS = 1000;
	static constexpr size_t MAX_VIP_DESCRIPTION_LENGTH = 255;

	uint32_t guid = msg.get<uint32_t>();
	const std::string description = msg.getString();
	uint32_t icon = std::min<uint32_t>(10, msg.get<uint32_t>()); // 10 is max icon in 9.63
	bool notify = msg.getByte() != 0;

	if (description.length() > MAX_VIP_DESCRIPTION_LENGTH ||
	        !checkClientRequestRateLimit(nextEditVipRequest, EDIT_VIP_RATE_LIMIT_MS)) {
		return;
	}
	addGameTask(&Game::playerRequestEditVip, player->getID(), guid, description, icon, notify);
}

void ProtocolGame::parseRotateItem(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerRotateItem, player->getID(), pos, stackpos, spriteId);
}

void ProtocolGame::parseRuleViolationReport(NetworkMessage& msg)
{
	uint8_t reportType = msg.getByte();
	uint8_t reportReason = msg.getByte();
	const std::string& targetName = msg.getString();
	const std::string& comment = msg.getString();
	std::string translation;
	if (reportType == REPORT_TYPE_NAME) {
		translation = msg.getString();
	} else if (reportType == REPORT_TYPE_STATEMENT) {
		translation = msg.getString();
		msg.get<uint32_t>(); // statement id, used to get whatever player have said, we don't log that.
	}

	addGameTask(&Game::playerReportRuleViolation, player->getID(), targetName, reportType, reportReason, comment, translation);
}

void ProtocolGame::parseBugReport(NetworkMessage& msg)
{
	uint8_t category = msg.getByte();
	std::string message = msg.getString();

	Position position;
	if (category == BUG_CATEGORY_MAP) {
		position = msg.getPosition();
	}

	addGameTask(&Game::playerReportBug, player->getID(), message, position, category);
}

void ProtocolGame::parseDebugAssert(NetworkMessage& msg)
{
	if (debugAssertSent) {
		return;
	}

	debugAssertSent = true;

	std::string assertLine = msg.getString();
	std::string date = msg.getString();
	std::string description = msg.getString();
	std::string comment = msg.getString();
	addGameTask(&Game::playerDebugAssert, player->getID(), assertLine, date, description, comment);
}

void ProtocolGame::parseInviteToParty(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	addGameTask(&Game::playerInviteToParty, player->getID(), targetId);
}

void ProtocolGame::parseJoinParty(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	addGameTask(&Game::playerJoinParty, player->getID(), targetId);
}

void ProtocolGame::parseRevokePartyInvite(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	addGameTask(&Game::playerRevokePartyInvitation, player->getID(), targetId);
}

void ProtocolGame::parsePassPartyLeadership(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	addGameTask(&Game::playerPassPartyLeadership, player->getID(), targetId);
}

void ProtocolGame::parseEnableSharedPartyExperience(NetworkMessage& msg)
{
	bool sharedExpActive = msg.getByte() == 1;
	addGameTask(&Game::playerEnableSharedPartyExperience, player->getID(), sharedExpActive);
}

void ProtocolGame::parseQuestLine(NetworkMessage& msg)
{
	uint16_t questId = msg.get<uint16_t>();
	addGameTask(&Game::playerShowQuestLine, player->getID(), questId);
}

/*
void ProtocolGame::parseMarketLeave()
{
	addGameTask(&Game::playerLeaveMarket, player->getID());
}

void ProtocolGame::parseMarketBrowse(NetworkMessage& msg)
{
	uint16_t browseId = msg.get<uint16_t>();

	if (browseId == MARKETREQUEST_OWN_OFFERS) {
		addGameTask(&Game::playerBrowseMarketOwnOffers, player->getID());
	} else if (browseId == MARKETREQUEST_OWN_HISTORY) {
		addGameTask(&Game::playerBrowseMarketOwnHistory, player->getID());
	} else {
		addGameTask(&Game::playerBrowseMarket, player->getID(), browseId);
	}
}

void ProtocolGame::parseMarketCreateOffer(NetworkMessage& msg)
{
	uint8_t type = msg.getByte();
	uint16_t spriteId = msg.get<uint16_t>();
	uint16_t amount = msg.get<uint16_t>();
	uint32_t price = msg.get<uint32_t>();
	bool anonymous = (msg.getByte() != 0);
	addGameTask(&Game::playerCreateMarketOffer, player->getID(), type, spriteId, amount, price, anonymous);
}

void ProtocolGame::parseMarketCancelOffer(NetworkMessage& msg)
{
	uint32_t timestamp = msg.get<uint32_t>();
	uint16_t counter = msg.get<uint16_t>();
	addGameTask(&Game::playerCancelMarketOffer, player->getID(), timestamp, counter);
}

void ProtocolGame::parseMarketAcceptOffer(NetworkMessage& msg)
{
	uint32_t timestamp = msg.get<uint32_t>();
	uint16_t counter = msg.get<uint16_t>();
	uint16_t amount = msg.get<uint16_t>();
	addGameTask(&Game::playerAcceptMarketOffer, player->getID(), timestamp, counter, amount);
}

void ProtocolGame::parseModalWindowAnswer(NetworkMessage& msg)
{
	uint32_t id = msg.get<uint32_t>();
	uint8_t button = msg.getByte();
	uint8_t choice = msg.getByte();
	addGameTask(&Game::playerAnswerModalWindow, player->getID(), id, button, choice);
}

void ProtocolGame::parseBrowseField(NetworkMessage& msg)
{
	const Position& pos = msg.getPosition();
	addGameTask(&Game::playerBrowseField, player->getID(), pos);
}*/

void ProtocolGame::parseSeekInContainer(NetworkMessage& msg)
{
	uint8_t containerId = msg.getByte();
	uint16_t index = msg.get<uint16_t>();
	addGameTask(&Game::playerSeekInContainer, player->getID(), containerId, index);
}

// Send methods
void ProtocolGame::sendOpenPrivateChannel(const std::string& receiver)
{
	NetworkMessage msg;
	msg.addByte(0xAD);
	msg.addString(receiver);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannelEvent(uint16_t channelId, const std::string& playerName, ChannelEvent_t channelEvent)
{
	NetworkMessage msg;
	msg.addByte(0xF3);
	msg.add<uint16_t>(channelId);
	msg.addString(playerName);
	msg.addByte(channelEvent);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureOutfit(const Creature* creature, const Outfit_t& outfit)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x8E);
	msg.add<uint32_t>(creature->getID());
	AddOutfit(msg, outfit);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureLight(const Creature* creature)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	AddCreatureLight(msg, creature);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendWorldLight(LightInfo lightInfo)
{
	NetworkMessage msg;
	AddWorldLight(msg, lightInfo);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureWalkthrough(const Creature* creature, bool walkthrough)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x92);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(walkthrough ? 0x00 : 0x01);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureShield(const Creature* creature)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x91);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(player->getPartyShield(creature->getPlayer()));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSkull(const Creature* creature)
{
	if (g_game.getWorldType() != WORLD_TYPE_PVP) {
		return;
	}

	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x90);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(player->getSkullClient(creature));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureType(uint32_t creatureId, uint8_t creatureType)
{
	NetworkMessage msg;
	msg.addByte(0x95);
	msg.add<uint32_t>(creatureId);
	msg.addByte(creatureType);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureHelpers(uint32_t creatureId, uint16_t helpers)
{
	NetworkMessage msg;
	msg.addByte(0x94);
	msg.add<uint32_t>(creatureId);
	msg.add<uint16_t>(helpers);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSquare(const Creature* creature, SquareColor_t color)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x86);
	msg.add<uint32_t>(creature->getID());
	//msg.addByte(0x01);
	msg.addByte(color);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTutorial(uint8_t tutorialId)
{
	NetworkMessage msg;
	msg.addByte(0xDC);
	msg.addByte(tutorialId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddMarker(const Position& pos, uint8_t markType, const std::string& desc)
{
	NetworkMessage msg;
	msg.addByte(0xDD);
	msg.addPosition(pos);
	msg.addByte(markType);
	msg.addString(desc);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendReLoginWindow(uint8_t unfairFightReduction)
{
	NetworkMessage msg;
	msg.addByte(0x28);
	msg.addByte(0x00);
	msg.addByte(unfairFightReduction);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendStats()
{
	NetworkMessage msg;
	AddPlayerStats(msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBasicData()
{
	NetworkMessage msg;
	msg.addByte(0x9F);
	if (player->isPremium()) {
		msg.addByte(1);
		msg.add<uint32_t>(g_config.getBoolean(ConfigManager::FREE_PREMIUM) ? 0 : player->premiumEndsAt);
	} else {
		msg.addByte(0);
		msg.add<uint32_t>(0);
	}
	msg.addByte(player->getVocation()->getClientId());
	msg.add<uint16_t>(0xFF); // number of known spells
	for (uint8_t spellId = 0x00; spellId < 0xFF; spellId++) {
		msg.addByte(spellId);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextMessage(const TextMessage& message)
{
	NetworkMessage msg;
	/*msg.addByte(0xB4);
	msg.addByte(message.type);
	switch (message.type) {
		case MESSAGE_DAMAGE_DEALT:
		case MESSAGE_DAMAGE_RECEIVED:
		case MESSAGE_DAMAGE_OTHERS: {
			msg.addPosition(message.position);
			msg.add<uint32_t>(message.primary.value);
			msg.addByte(message.primary.color);
			msg.add<uint32_t>(message.secondary.value);
			msg.addByte(message.secondary.color);
			break;
		}
		case MESSAGE_HEALED:
		case MESSAGE_HEALED_OTHERS:
		case MESSAGE_EXPERIENCE:
		case MESSAGE_EXPERIENCE_OTHERS: {
			msg.addPosition(message.position);
			msg.add<uint32_t>(message.primary.value);
			msg.addByte(message.primary.color);
			break;
		}
		case MESSAGE_GUILD:
		case MESSAGE_PARTY_MANAGEMENT:
		case MESSAGE_PARTY:
			msg.add<uint16_t>(message.channelId);
			break;
		default: {
			break;
		}
	}*/
	msg.addByte(0xB4);
	msg.addByte(message.type);
	msg.addString(message.text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendColoredText(const ColoredText& coloredText)
{
	if (!canSee(coloredText.position)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x84);
	msg.addPosition(coloredText.position);
	msg.addByte(coloredText.color);
	msg.addString(coloredText.text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendClosePrivate(uint16_t channelId)
{
	NetworkMessage msg;
	msg.addByte(0xB3);
	msg.add<uint16_t>(channelId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatePrivateChannel(uint16_t channelId, const std::string& channelName)
{
	NetworkMessage msg;
	msg.addByte(0xB2);
	msg.add<uint16_t>(channelId);
	msg.addString(channelName);
	/*msg.add<uint16_t>(0x01);
	msg.addString(player->getName());
	msg.add<uint16_t>(0x00);
	*/
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannelsDialog()
{
	NetworkMessage msg;
	msg.addByte(0xAB);

	const ChannelList& list = g_chat->getChannelList(*player);
	msg.addByte(list.size());
	for (ChatChannel* channel : list) {
		msg.add<uint16_t>(channel->getId());
		msg.addString(channel->getName());
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannel(uint16_t channelId, const std::string& channelName, const UsersMap*, const InvitedMap*)
{
	NetworkMessage msg;
	msg.addByte(0xAC);

	msg.add<uint16_t>(channelId);
	msg.addString(channelName);

	/*if (channelUsers) {
		msg.add<uint16_t>(channelUsers->size());
		for (const auto& it : *channelUsers) {
			msg.addString(it.second->getName());
		}
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (invitedUsers) {
		msg.add<uint16_t>(invitedUsers->size());
		for (const auto& it : *invitedUsers) {
			msg.addString(it.second->getName());
		}
	} else {
		msg.add<uint16_t>(0x00);
	}*/
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannelMessage(const std::string& author, const std::string& text, SpeakClasses type, uint16_t channel)
{
	NetworkMessage msg;
	msg.addByte(0xAA);
	msg.add<uint32_t>(0x00);
	msg.addString(author);
	msg.add<uint16_t>(0x00);
	msg.addByte(type);
	msg.add<uint16_t>(channel);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendIcons(uint16_t icons)
{
	NetworkMessage msg;
	msg.addByte(0xA2);
	//msg.add<uint16_t>(icons);
	msg.addByte(icons);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendContainer(uint8_t cid, const Container* container, bool hasParent, uint16_t firstIndex)
{
	NetworkMessage msg;
	msg.addByte(0x6E);

	msg.addByte(cid);

	/*if (container->getID() == ITEM_BROWSEFIELD) {
		msg.addItem(ITEM_BAG, 1);
		msg.addString("Browse Field");
	} else {
		msg.addItem(container);
		msg.addString(container->getName());
	}*/

	msg.addItem(container);
	msg.addString(container->getName());

	msg.addByte(container->capacity());

	msg.addByte(hasParent ? 0x01 : 0x00);

	msg.addByte(std::min<uint32_t>(0xFF, container->size()));

	uint32_t i = 0;
	const ItemDeque& itemList = container->getItemList();
	for (ItemDeque::const_iterator it = itemList.begin() + firstIndex, end = itemList.end(); i < 0xFF && it != end; ++it, ++i) {
		msg.addItem(*it);
	}

	/*msg.addByte(container->isUnlocked() ? 0x01 : 0x00); // Drag and drop
	msg.addByte(container->hasPagination() ? 0x01 : 0x00); // Pagination

	uint32_t containerSize = container->size();
	msg.add<uint16_t>(containerSize);
	msg.add<uint16_t>(firstIndex);
	if (firstIndex < containerSize) {
		uint8_t itemsToSend = std::min<uint32_t>(std::min<uint32_t>(container->capacity(), containerSize - firstIndex), std::numeric_limits<uint8_t>::max());

		msg.addByte(itemsToSend);
		for (auto it = container->getItemList().begin() + firstIndex, end = it + itemsToSend; it != end; ++it) {
			msg.addItem(*it);
		}
	} else {
		msg.addByte(0x00);
	}*/
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendShop(Npc* npc, const ShopInfoList& itemList)
{
	NetworkMessage msg;
	msg.addByte(0x7A);
	if (version >= 910) {
		msg.addString(npc->getName());
	}

	if (version >= 986) {
		uint16_t itemsToSend = std::min<size_t>(itemList.size(), std::numeric_limits<uint16_t>::max());
		msg.add<uint16_t>(itemsToSend);

		uint16_t i = 0;
		for (auto it = itemList.begin(); i < itemsToSend; ++it, ++i) {
			AddShopItem(msg, *it);
		}
	} else {
		uint8_t itemsToSend = std::min<size_t>(itemList.size(), std::numeric_limits<uint8_t>::max());
		msg.addByte(itemsToSend);

		uint8_t i = 0;
		for (auto it = itemList.begin(); i < itemsToSend; ++it, ++i) {
			AddShopItem(msg, *it);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseShop()
{
	NetworkMessage msg;
	msg.addByte(0x7C);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSaleItemList(const std::list<ShopInfo>& shop)
{
	NetworkMessage msg;
	msg.addByte(0x7B);
	const uint64_t totalMoney = player->getMoney() + player->getBankBalance();
	if (version >= 972) {
		msg.add<uint64_t>(totalMoney);
	} else {
		msg.add<uint32_t>(std::min<uint64_t>(totalMoney, std::numeric_limits<uint32_t>::max()));
	}

	std::map<uint16_t, uint32_t> saleMap;

	if (shop.size() <= 5) {
		// For very small shops it's not worth it to create the complete map
		for (const ShopInfo& shopInfo : shop) {
			if (shopInfo.sellPrice == 0) {
				continue;
			}

			int8_t subtype = -1;

			const ItemType& itemType = Item::items[shopInfo.itemId];
			if (itemType.hasSubType() && !itemType.stackable) {
				subtype = (shopInfo.subType == 0 ? -1 : shopInfo.subType);
			}

			uint32_t count = player->getItemTypeCount(shopInfo.itemId, subtype);
			if (count > 0) {
				saleMap[shopInfo.itemId] = count;
			}
		}
	} else {
		// Large shop, it's better to get a cached map of all item counts and use it
		// We need a temporary map since the finished map should only contain items
		// available in the shop
		std::map<uint32_t, uint32_t> tempSaleMap;
		player->getAllItemTypeCount(tempSaleMap);

		// We must still check manually for the special items that require subtype matches
		// (That is, fluids such as potions etc., actually these items are very few since
		// health potions now use their own ID)
		for (const ShopInfo& shopInfo : shop) {
			if (shopInfo.sellPrice == 0) {
				continue;
			}

			int8_t subtype = -1;

			const ItemType& itemType = Item::items[shopInfo.itemId];
			if (itemType.hasSubType() && !itemType.stackable) {
				subtype = (shopInfo.subType == 0 ? -1 : shopInfo.subType);
			}

			if (subtype != -1) {
				uint32_t count;
				if (itemType.isFluidContainer() || itemType.isSplash()) {
					count = player->getItemTypeCount(shopInfo.itemId, subtype); // This shop item requires extra checks
				} else {
					count = subtype;
				}

				if (count > 0) {
					saleMap[shopInfo.itemId] = count;
				}
			} else {
				std::map<uint32_t, uint32_t>::const_iterator findIt = tempSaleMap.find(shopInfo.itemId);
				if (findIt != tempSaleMap.end() && findIt->second > 0) {
					saleMap[shopInfo.itemId] = findIt->second;
				}
			}
		}
	}

	uint8_t itemsToSend = std::min<size_t>(saleMap.size(), std::numeric_limits<uint8_t>::max());
	msg.addByte(itemsToSend);

	uint8_t i = 0;
	for (std::map<uint16_t, uint32_t>::const_iterator it = saleMap.begin(); i < itemsToSend; ++it, ++i) {
		msg.addItemId(it->first);
		msg.addByte(std::min<uint32_t>(it->second, std::numeric_limits<uint8_t>::max()));
	}

	writeToOutputBuffer(msg);
}

/*
void ProtocolGame::sendMarketEnter(uint32_t depotId)
{
	NetworkMessage msg;
	msg.addByte(0xF6);

	msg.add<uint64_t>(player->getBankBalance());
	msg.addByte(std::min<uint32_t>(IOMarket::getPlayerOfferCount(player->getGUID()), std::numeric_limits<uint8_t>::max()));

	DepotChest* depotChest = player->getDepotChest(depotId, false);
	if (!depotChest) {
		msg.add<uint16_t>(0x00);
		writeToOutputBuffer(msg);
		return;
	}

	player->setInMarket(true);

	std::map<uint16_t, uint32_t> depotItems;
	std::forward_list<Container*> containerList { depotChest, player->getInbox() };

	do {
		Container* container = containerList.front();
		containerList.pop_front();

		for (Item* item : container->getItemList()) {
			Container* c = item->getContainer();
			if (c && !c->empty()) {
				containerList.push_front(c);
				continue;
			}

			const ItemType& itemType = Item::items[item->getID()];
			if (itemType.wareId == 0) {
				continue;
			}

			if (c && (!itemType.isContainer() || c->capacity() != itemType.maxItems)) {
				continue;
			}

			if (!item->hasMarketAttributes()) {
				continue;
			}

			depotItems[itemType.wareId] += Item::countByType(item, -1);
		}
	} while (!containerList.empty());

	uint16_t itemsToSend = std::min<size_t>(depotItems.size(), std::numeric_limits<uint16_t>::max());
	msg.add<uint16_t>(itemsToSend);

	uint16_t i = 0;
	for (std::map<uint16_t, uint32_t>::const_iterator it = depotItems.begin(); i < itemsToSend; ++it, ++i) {
		msg.add<uint16_t>(it->first);
		msg.add<uint16_t>(std::min<uint32_t>(0xFFFF, it->second));
	}

	writeToOutputBuffer(msg);
}
*/

void ProtocolGame::sendMarketLeave()
{
	NetworkMessage msg;
	msg.addByte(0xF7);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketBrowseItem(uint16_t itemId, const MarketOfferList& buyOffers, const MarketOfferList& sellOffers)
{
	NetworkMessage msg;

	msg.addByte(0xF9);
	msg.addItemId(itemId);

	msg.add<uint32_t>(buyOffers.size());
	for (const MarketOffer& offer : buyOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		msg.add<uint32_t>(offer.price);
		msg.addString(offer.playerName);
	}

	msg.add<uint32_t>(sellOffers.size());
	for (const MarketOffer& offer : sellOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		msg.add<uint32_t>(offer.price);
		msg.addString(offer.playerName);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketAcceptOffer(const MarketOfferEx& offer)
{
	NetworkMessage msg;
	msg.addByte(0xF9);
	msg.addItemId(offer.itemId);

	if (offer.type == MARKETACTION_BUY) {
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		msg.add<uint32_t>(offer.price);
		msg.addString(offer.playerName);
		msg.add<uint32_t>(0x00);
	} else {
		msg.add<uint32_t>(0x00);
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		msg.add<uint32_t>(offer.price);
		msg.addString(offer.playerName);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketBrowseOwnOffers(const MarketOfferList& buyOffers, const MarketOfferList& sellOffers)
{
	NetworkMessage msg;
	msg.addByte(0xF9);
	msg.add<uint16_t>(MARKETREQUEST_OWN_OFFERS);

	msg.add<uint32_t>(buyOffers.size());
	for (const MarketOffer& offer : buyOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.addItemId(offer.itemId);
		msg.add<uint16_t>(offer.amount);
		msg.add<uint32_t>(offer.price);
	}

	msg.add<uint32_t>(sellOffers.size());
	for (const MarketOffer& offer : sellOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.addItemId(offer.itemId);
		msg.add<uint16_t>(offer.amount);
		msg.add<uint32_t>(offer.price);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketCancelOffer(const MarketOfferEx& offer)
{
	NetworkMessage msg;
	msg.addByte(0xF9);
	msg.add<uint16_t>(MARKETREQUEST_OWN_OFFERS);

	if (offer.type == MARKETACTION_BUY) {
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.addItemId(offer.itemId);
		msg.add<uint16_t>(offer.amount);
		msg.add<uint32_t>(offer.price);
		msg.add<uint32_t>(0x00);
	} else {
		msg.add<uint32_t>(0x00);
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.addItemId(offer.itemId);
		msg.add<uint16_t>(offer.amount);
		msg.add<uint32_t>(offer.price);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketBrowseOwnHistory(const HistoryMarketOfferList& buyOffers, const HistoryMarketOfferList& sellOffers)
{
	uint32_t i = 0;
	std::map<uint32_t, uint16_t> counterMap;
	uint32_t buyOffersToSend = std::min<uint32_t>(buyOffers.size(), 810 + std::max<int32_t>(0, 810 - sellOffers.size()));
	uint32_t sellOffersToSend = std::min<uint32_t>(sellOffers.size(), 810 + std::max<int32_t>(0, 810 - buyOffers.size()));

	NetworkMessage msg;
	msg.addByte(0xF9);
	msg.add<uint16_t>(MARKETREQUEST_OWN_HISTORY);

	msg.add<uint32_t>(buyOffersToSend);
	for (auto it = buyOffers.begin(); i < buyOffersToSend; ++it, ++i) {
		msg.add<uint32_t>(it->timestamp);
		msg.add<uint16_t>(counterMap[it->timestamp]++);
		msg.addItemId(it->itemId);
		msg.add<uint16_t>(it->amount);
		msg.add<uint32_t>(it->price);
		msg.addByte(it->state);
	}

	counterMap.clear();
	i = 0;

	msg.add<uint32_t>(sellOffersToSend);
	for (auto it = sellOffers.begin(); i < sellOffersToSend; ++it, ++i) {
		msg.add<uint32_t>(it->timestamp);
		msg.add<uint16_t>(counterMap[it->timestamp]++);
		msg.addItemId(it->itemId);
		msg.add<uint16_t>(it->amount);
		msg.add<uint32_t>(it->price);
		msg.addByte(it->state);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketDetail(uint16_t itemId)
{
	NetworkMessage msg;
	msg.addByte(0xF8);
	msg.addItemId(itemId);

	const ItemType& it = Item::items[itemId];
	if (it.armor != 0) {
		msg.addString(std::to_string(it.armor));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.attack != 0) {
		// TODO: chance to hit, range
		// example:
		// "attack +x, chance to hit +y%, z fields"
		if (it.abilities && it.abilities->elementType != COMBAT_NONE && it.abilities->elementDamage != 0) {
			msg.addString(fmt::format("{:d} physical +{:d} {:s}", it.attack, it.abilities->elementDamage, getCombatName(it.abilities->elementType)));
		} else {
			msg.addString(std::to_string(it.attack));
		}
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.isContainer()) {
		msg.addString(std::to_string(it.maxItems));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.defense != 0) {
		if (it.extraDefense != 0) {
			msg.addString(fmt::format("{:d} {:+d}", it.defense, it.extraDefense));
		} else {
			msg.addString(std::to_string(it.defense));
		}
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (!it.description.empty()) {
		const std::string& descr = it.description;
		if (descr.back() == '.') {
			msg.addString(std::string(descr, 0, descr.length() - 1));
		} else {
			msg.addString(descr);
		}
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.decayTime != 0) {
		msg.addString(fmt::format("{:d} seconds", it.decayTime));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.abilities) {
		std::ostringstream ss;
		bool separator = false;

		for (size_t i = 0; i < COMBAT_COUNT; ++i) {
			if (it.abilities->absorbPercent[i] == 0) {
				continue;
			}

			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << getCombatName(indexToCombatType(i)) << ' ' << std::showpos << it.abilities->absorbPercent[i] << std::noshowpos << '%';
		}

		msg.addString(ss.str());
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.minReqLevel != 0) {
		msg.addString(std::to_string(it.minReqLevel));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.minReqMagicLevel != 0) {
		msg.addString(std::to_string(it.minReqMagicLevel));
	} else {
		msg.add<uint16_t>(0x00);
	}

	msg.addString(it.vocationString);

	msg.addString(it.runeSpellName);

	if (it.abilities) {
		std::ostringstream ss;
		bool separator = false;

		for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; i++) {
			if (!it.abilities->skills[i]) {
				continue;
			}

			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << getSkillName(i) << ' ' << std::showpos << it.abilities->skills[i] << std::noshowpos;
		}

		if (it.abilities->stats[STAT_MAGICPOINTS] != 0) {
			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << "magic level " << std::showpos << it.abilities->stats[STAT_MAGICPOINTS] << std::noshowpos;
		}

		if (it.abilities->speed != 0) {
			if (separator) {
				ss << ", ";
			}

			ss << "speed " << std::showpos << (it.abilities->speed >> 1) << std::noshowpos;
		}

		msg.addString(ss.str());
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.charges != 0) {
		msg.addString(std::to_string(it.charges));
	} else {
		msg.add<uint16_t>(0x00);
	}

	std::string weaponName = getWeaponName(it.weaponType);

	if (it.slotPosition & SLOTP_TWO_HAND) {
		if (!weaponName.empty()) {
			weaponName += ", two-handed";
		} else {
			weaponName = "two-handed";
		}
	}

	msg.addString(weaponName);

	if (it.weight != 0) {
		std::ostringstream ss;
		if (it.weight < 10) {
			ss << "0.0" << it.weight;
		} else if (it.weight < 100) {
			ss << "0." << it.weight;
		} else {
			std::string weightString = std::to_string(it.weight);
			weightString.insert(weightString.end() - 2, '.');
			ss << weightString;
		}
		ss << " oz";
		msg.addString(ss.str());
	} else {
		msg.add<uint16_t>(0x00);
	}

	MarketStatistics* statistics = IOMarket::getInstance().getPurchaseStatistics(itemId);
	if (statistics) {
		msg.addByte(0x01);
		msg.add<uint32_t>(statistics->numTransactions);
		msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), statistics->totalPrice));
		msg.add<uint32_t>(statistics->highestPrice);
		msg.add<uint32_t>(statistics->lowestPrice);
	} else {
		msg.addByte(0x00);
	}

	statistics = IOMarket::getInstance().getSaleStatistics(itemId);
	if (statistics) {
		msg.addByte(0x01);
		msg.add<uint32_t>(statistics->numTransactions);
		msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), statistics->totalPrice));
		msg.add<uint32_t>(statistics->highestPrice);
		msg.add<uint32_t>(statistics->lowestPrice);
	} else {
		msg.addByte(0x00);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendQuestLog()
{
	NetworkMessage msg;
	msg.addByte(0xF0);
	msg.add<uint16_t>(g_game.quests.getQuestsCount(player));

	for (const Quest& quest : g_game.quests.getQuests()) {
		if (quest.isStarted(player)) {
			msg.add<uint16_t>(quest.getID());
			msg.addString(quest.getName());
			msg.addByte(quest.isCompleted(player));
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendQuestLine(const Quest* quest)
{
	NetworkMessage msg;
	msg.addByte(0xF1);
	msg.add<uint16_t>(quest->getID());
	msg.addByte(quest->getMissionsCount(player));

	for (const Mission& mission : quest->getMissions()) {
		if (mission.isStarted(player)) {
			msg.addString(mission.getName(player));
			msg.addString(mission.getDescription(player));
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTradeItemRequest(const std::string& traderName, const Item* item, bool ack)
{
	NetworkMessage msg;

	if (ack) {
		msg.addByte(0x7D);
	} else {
		msg.addByte(0x7E);
	}

	msg.addString(traderName);

	if (const Container* tradeContainer = item->getContainer()) {
		std::list<const Container*> listContainer {tradeContainer};
		std::list<const Item*> itemList {tradeContainer};
		while (!listContainer.empty()) {
			const Container* container = listContainer.front();
			listContainer.pop_front();

			for (Item* containerItem : container->getItemList()) {
				Container* tmpContainer = containerItem->getContainer();
				if (tmpContainer) {
					listContainer.push_back(tmpContainer);
				}
				itemList.push_back(containerItem);
			}
		}

		msg.addByte(itemList.size());
		for (const Item* listItem : itemList) {
			msg.addItem(listItem);
		}
	} else {
		msg.addByte(0x01);
		msg.addItem(item);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseTrade()
{
	NetworkMessage msg;
	msg.addByte(0x7F);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseContainer(uint8_t cid)
{
	NetworkMessage msg;
	msg.addByte(0x6F);
	msg.addByte(cid);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureTurn(const Creature* creature, uint32_t stackPos)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	if (stackPos >= 10) {
		msg.add<uint16_t>(0xFFFF);
		msg.add<uint32_t>(creature->getID());
	} else {
		msg.addPosition(creature->getPosition());
		msg.addByte(stackPos);
	}

	msg.add<uint16_t>(0x63);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(creature->getDirection());
	//msg.addByte(player->canWalkthroughEx(creature) ? 0x00 : 0x01);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSay(const Creature* creature, SpeakClasses type, const std::string& text, const Position* pos/* = nullptr*/)
{
	NetworkMessage msg;
	msg.addByte(0xAA);

	static uint32_t statementId = 0;
	msg.add<uint32_t>(++statementId);

	msg.addString(creature->getName());

	//Add level only for players
	/*if (const Player* speaker = creature->getPlayer()) {
		msg.add<uint16_t>(speaker->getLevel());
	} else {
		msg.add<uint16_t>(0x00);
	}*/

	msg.addByte(type);
	if (pos) {
		msg.addPosition(*pos);
	} else {
		msg.addPosition(creature->getPosition());
	}

	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendToChannel(const Creature* creature, SpeakClasses type, const std::string& text, uint16_t channelId)
{
	NetworkMessage msg;
	msg.addByte(0xAA);

	static uint32_t statementId = 0;
	msg.add<uint32_t>(++statementId);
	if (!creature) {
		msg.add<uint32_t>(0x00);
	} else {
		msg.addString(creature->getName());
		//Add level only for players
		/*if (const Player* speaker = creature->getPlayer()) {
			msg.add<uint16_t>(speaker->getLevel());
		} else {
			msg.add<uint16_t>(0x00);
		}*/
	}

	msg.addByte(type);
	msg.add<uint16_t>(channelId);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPrivateMessage(const Player* speaker, SpeakClasses type, const std::string& text)
{
	NetworkMessage msg;
	msg.addByte(0xAA);
	static uint32_t statementId = 0;
	msg.add<uint32_t>(++statementId);
	if (speaker) {
		msg.addString(speaker->getName());
		//msg.add<uint16_t>(speaker->getLevel());
	} else {
		msg.add<uint32_t>(0x00);
	}
	msg.addByte(type);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCancelTarget()
{
	NetworkMessage msg;
	msg.addByte(0xA3);
	//msg.add<uint32_t>(0x00);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChangeSpeed(const Creature* creature, uint32_t speed)
{
	NetworkMessage msg;
	msg.addByte(0x8F);
	msg.add<uint32_t>(creature->getID());
	//msg.add<uint16_t>(creature->getBaseSpeed() / 2);
	//msg.add<uint16_t>(speed / 2);
	msg.add<uint16_t>(speed);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCancelWalk()
{
	NetworkMessage msg;
	msg.addByte(0xB5);
	msg.addByte(player->getDirection());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSkills()
{
	NetworkMessage msg;
	AddPlayerSkills(msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPing()
{
	NetworkMessage msg;
	//msg.addByte(0x1D);
	msg.addByte(0x1E);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPingBack()
{
	NetworkMessage msg;
	msg.addByte(0x1E);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendDistanceShoot(const Position& from, const Position& to, uint8_t type)
{
	NetworkMessage msg;
	msg.addByte(0x85);
	msg.addPosition(from);
	msg.addPosition(to);
	msg.addByte(type);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMagicEffect(const Position& pos, uint8_t type)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x83);
	msg.addPosition(pos);
	msg.addByte(type);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureHealth(const Creature* creature)
{
	NetworkMessage msg;
	msg.addByte(0x8C);
	msg.add<uint32_t>(creature->getID());

	if (creature->isHealthHidden()) {
		msg.addByte(0x00);
	} else {
		msg.addByte(std::ceil((static_cast<double>(creature->getHealth()) / std::max<int32_t>(creature->getMaxHealth(), 1)) * 100));
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendFYIBox(const std::string& message)
{
	NetworkMessage msg;
	msg.addByte(0x15);
	msg.addString(message);
	writeToOutputBuffer(msg);
}

//tile
void ProtocolGame::sendMapDescription(const Position& pos)
{
	NetworkMessage msg;
	msg.addByte(0x64);
	msg.addPosition(player->getPosition());
	GetMapDescription(
		pos.x - Map::maxClientViewportX, pos.y - Map::maxClientViewportY, pos.z,
		(Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2,
		msg);
	writeToOutputBuffer(msg);
	g_playerShop.syncVisibleLabels(player);
}

void ProtocolGame::sendAddTileItem(const Position& pos, uint32_t stackpos, const Item* item)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6A);
	msg.addPosition(pos);
	msg.addByte(stackpos);
	msg.addItem(item);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateTileItem(const Position& pos, uint32_t stackpos, const Item* item)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(pos);
	msg.addByte(stackpos);
	msg.addItem(item);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRemoveTileThing(const Position& pos, uint32_t stackpos)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	RemoveTileThing(msg, pos, stackpos);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateTileCreature(const Position& pos, uint32_t stackpos, const Creature* creature)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(pos);
	msg.addByte(stackpos);

	bool known;
	uint32_t removedKnown;
	checkCreatureAsKnown(creature->getID(), known, removedKnown);
	AddCreature(msg, creature, false, removedKnown);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRemoveTileCreature(const Creature* creature, const Position& pos, uint32_t stackpos)
{
	if (stackpos < 10) {
		if (!canSee(pos)) {
			return;
		}

		NetworkMessage msg;
		RemoveTileThing(msg, pos, stackpos);
		writeToOutputBuffer(msg);
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6C);
	msg.add<uint16_t>(0xFFFF);
	msg.add<uint32_t>(creature->getID());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateTile(const Tile* tile, const Position& pos)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x69);
	msg.addPosition(pos);

	if (tile) {
		GetTileDescription(tile, msg);
		msg.addByte(0x00);
		msg.addByte(0xFF);
	} else {
		msg.addByte(0x01);
		msg.addByte(0xFF);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPendingStateEntered()
{
	NetworkMessage msg;
	msg.addByte(0x0A);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendEnterWorld()
{
	NetworkMessage msg;
	msg.addByte(0x0F);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendFightModes()
{
	NetworkMessage msg;
	msg.addByte(0xA7);
	msg.addByte(player->fightMode);
	msg.addByte(player->chaseMode);
	msg.addByte(player->secureMode);
	//msg.addByte(PVP_MODE_DOVE);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddCreature(const Creature* creature, const Position& pos, int32_t stackpos, bool isLogin)
{
	if (!canSee(pos)) {
		return;
	}

	if (creature != player) {
		// stack pos is always real index now, so it can exceed the limit
		// if stack pos exceeds the limit, we need to refresh the tile instead
		// 1. this is a rare case, and is only triggered by forcing summon in a position
		// 2. since no stackpos will be send to the client about that creature, removing
		//    it must be done with its id if its stackpos remains >= 10. this is done to
		//    add creatures to battle list instead of rendering on screen
		/*if (stackpos >= 10) {
			// @todo: should we avoid this check?
			if (const Tile* tile = creature->getTile()) {
				sendUpdateTile(tile, pos);
			}
		} else*/
		if (stackpos != -1) {
			// if stackpos is -1, the client will automatically detect it
			NetworkMessage msg;
			msg.addByte(0x6A);
			msg.addPosition(pos);
			msg.addByte(stackpos);

			bool known;
			uint32_t removedKnown;
			checkCreatureAsKnown(creature->getID(), known, removedKnown);
			AddCreature(msg, creature, known, removedKnown);
			writeToOutputBuffer(msg);
		}

		if (isLogin) {
			sendMagicEffect(pos, CONST_ME_TELEPORT);
		}
		return;
	}

	NetworkMessage msg;
	//msg.addByte(0x17);
	msg.addByte(0x0A);

	msg.add<uint32_t>(player->getID());
	msg.add<uint16_t>(10); // beat duration, matches Creature::getStepDuration rounding

	/*msg.addDouble(Creature::speedA, 3);
	msg.addDouble(Creature::speedB, 3);
	msg.addDouble(Creature::speedC, 3);*/

	// can report bugs?
	if (player->getAccountType() >= ACCOUNT_TYPE_TUTOR) {
		msg.addByte(0x01);
	} else {
		msg.addByte(0x00);
	}

	//msg.addByte(0x00); // can change pvp framing option
	//msg.addByte(0x00); // expert mode button enabled

	//msg.add<uint16_t>(0x00); // URL (string) to ingame store images
	//msg.add<uint16_t>(25); // premium coin package size

	if (player->getAccountType() >= ACCOUNT_TYPE_GAMEMASTER) {
		msg.addByte(0x0B);
		for (uint8_t i = 0; i < 32; i++) {
			msg.addByte(0xFF);
		}
	}

	writeToOutputBuffer(msg);

	//sendPendingStateEntered();
	//sendEnterWorld();
	sendMapDescription(pos);

	if (isLogin) {
		sendMagicEffect(pos, CONST_ME_TELEPORT);
	}

	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		sendInventoryItem(static_cast<slots_t>(i), player->getInventoryItem(static_cast<slots_t>(i)));
	}

	//sendInventoryItem(CONST_SLOT_STORE_INBOX, player->getStoreInbox()->getItem());

	sendStats();
	sendSkills();

	//gameworld light-settings
	sendWorldLight(g_game.getWorldLightInfo());

	//player light level
	sendCreatureLight(creature);

	sendVIPEntries();

	//sendBasicData();
	player->sendIcons();

	if (isLogin) {
		player->restoreOpenContainerState();
		if (player->getOperatingSystem() >= CLIENTOS_OTCLIENT_LINUX) {
			sendExtendedOpcode(CONTAINER_RESTORE_EXTENDED_OPCODE, "end");
		}
	}
}

void ProtocolGame::sendMoveCreature(const Creature* creature, const Position& newPos, int32_t newStackPos, const Position& oldPos, int32_t oldStackPos, bool teleport)
{
	if (creature == player) {
		if (teleport) {
			sendRemoveTileCreature(creature, oldPos, oldStackPos);
			sendMapDescription(newPos);
		} else {
			NetworkMessage msg;
			if (oldPos.z == 7 && newPos.z >= 8) {
				RemoveTileCreature(msg, creature, oldPos, oldStackPos);
			} else {
				msg.addByte(0x6D);
				if (oldStackPos < 10) {
					msg.addPosition(oldPos);
					msg.addByte(oldStackPos);
				} else {
					msg.add<uint16_t>(0xFFFF);
					msg.add<uint32_t>(creature->getID());
				}
				msg.addPosition(newPos);
			}

			if (newPos.z > oldPos.z) {
				MoveDownCreature(msg, creature, newPos, oldPos);
			} else if (newPos.z < oldPos.z) {
				MoveUpCreature(msg, creature, newPos, oldPos);
			}

			if (oldPos.y > newPos.y) { // north, for old x
				msg.addByte(0x65);
				GetMapDescription(oldPos.x - Map::maxClientViewportX, newPos.y - Map::maxClientViewportY, newPos.z, (Map::maxClientViewportX * 2) + 2, 1, msg);
			} else if (oldPos.y < newPos.y) { // south, for old x
				msg.addByte(0x67);
				GetMapDescription(oldPos.x - Map::maxClientViewportX, newPos.y + (Map::maxClientViewportY + 1), newPos.z, (Map::maxClientViewportX * 2) + 2, 1, msg);
			}

			if (oldPos.x < newPos.x) { // east, [with new y]
				msg.addByte(0x66);
				GetMapDescription(newPos.x + (Map::maxClientViewportX + 1), newPos.y - Map::maxClientViewportY, newPos.z, 1, (Map::maxClientViewportY * 2) + 2, msg);
			} else if (oldPos.x > newPos.x) { // west, [with new y]
				msg.addByte(0x68);
				GetMapDescription(newPos.x - Map::maxClientViewportX, newPos.y - Map::maxClientViewportY, newPos.z, 1, (Map::maxClientViewportY * 2) + 2, msg);
			}
			writeToOutputBuffer(msg);
			g_playerShop.syncVisibleLabels(player);
		}
	} else if (canSee(oldPos) && canSee(creature->getPosition())) {
		if (teleport || (oldPos.z == 7 && newPos.z >= 8)) {
			sendRemoveTileCreature(creature, oldPos, oldStackPos);
			sendAddCreature(creature, newPos, newStackPos, false);
		} else {
			NetworkMessage msg;
			msg.addByte(0x6D);
			if (oldStackPos < 10) {
				msg.addPosition(oldPos);
				msg.addByte(oldStackPos);
			} else {
				msg.add<uint16_t>(0xFFFF);
				msg.add<uint32_t>(creature->getID());
			}
			msg.addPosition(creature->getPosition());
			writeToOutputBuffer(msg);
		}
	} else if (canSee(oldPos)) {
		sendRemoveTileCreature(creature, oldPos, oldStackPos);
	} else if (canSee(creature->getPosition())) {
		sendAddCreature(creature, newPos, newStackPos, false);
	}
}

void ProtocolGame::sendInventoryItem(slots_t slot, const Item* item)
{
	NetworkMessage msg;
	if (item) {
		msg.addByte(0x78);
		msg.addByte(slot);
		msg.addItem(item);
	} else {
		msg.addByte(0x79);
		msg.addByte(slot);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendItems()
{
	NetworkMessage msg;
	msg.addByte(0xF5);

	const std::vector<uint16_t>& inventory = Item::items.getInventory();
	msg.add<uint16_t>(inventory.size() + 11);
	for (uint16_t i = 1; i <= 11; i++) {
		msg.add<uint16_t>(i);
		msg.addByte(0); //always 0
		msg.add<uint16_t>(1); // always 1
	}

	for (auto clientId : inventory) {
		msg.add<uint16_t>(clientId);
		msg.addByte(0); //always 0
		msg.add<uint16_t>(1);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddContainerItem(uint8_t cid, uint16_t, const Item* item)
{
	NetworkMessage msg;
	msg.addByte(0x70);
	msg.addByte(cid);
	//msg.add<uint16_t>(slot);
	msg.addItem(item);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateContainerItem(uint8_t cid, uint16_t slot, const Item* item)
{
	NetworkMessage msg;
	msg.addByte(0x71);
	msg.addByte(cid);
	//msg.add<uint16_t>(slot);
	msg.addByte(slot);
	msg.addItem(item);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRemoveContainerItem(uint8_t cid, uint16_t slot, const Item*)
{
	NetworkMessage msg;
	msg.addByte(0x72);
	msg.addByte(cid);
	//msg.add<uint16_t>(slot);
	/*if (lastItem) {
		msg.addItem(lastItem);
	} else {
		msg.add<uint16_t>(0x00);
	}*/
	msg.addByte(slot);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextWindow(uint32_t windowTextId, Item* item, uint16_t maxlen, bool canWrite)
{
	NetworkMessage msg;
	msg.addByte(0x96);
	msg.add<uint32_t>(windowTextId);
	msg.addItem(item);

	if (canWrite) {
		msg.add<uint16_t>(maxlen);
		msg.addString(item->getText());
	} else {
		const std::string& text = item->getText();
		msg.add<uint16_t>(text.size());
		msg.addString(text);
	}

	msg.addString(item->getWriter());
	/*if (!writer.empty()) {
		msg.addString(writer);
	} else {
		msg.add<uint16_t>(0x00);
	}*/

	/*time_t writtenDate = item->getDate();
	if (writtenDate != 0) {
		msg.addString(formatDateShort(writtenDate));
	} else {
		msg.add<uint16_t>(0x00);
	}*/

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextWindow(uint32_t windowTextId, uint32_t itemId, const std::string& text)
{
	NetworkMessage msg;
	msg.addByte(0x96);
	msg.add<uint32_t>(windowTextId);
	msg.addItem(itemId, 1);
	msg.add<uint16_t>(text.size());
	msg.addString(text);
	//msg.add<uint16_t>(0x00);
	//msg.add<uint16_t>(0x00);
	msg.addString("");
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendHouseWindow(uint32_t windowTextId, const std::string& text)
{
	NetworkMessage msg;
	msg.addByte(0x97);
	msg.addByte(0x00);
	msg.add<uint32_t>(windowTextId);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendOutfitWindow()
{
	const auto& outfits = Outfits::getInstance().getOutfits(player->getSex());
	if (outfits.size() == 0) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xC8);

	Outfit_t currentOutfit = player->getDefaultOutfit();
	if (currentOutfit.lookType == 0) {
		Outfit_t newOutfit;
		newOutfit.lookType = outfits.front().lookType;
		currentOutfit = newOutfit;
	}

	/*Mount* currentMount = g_game.mounts.getMountByID(player->getCurrentMount());
	if (currentMount) {
		currentOutfit.lookMount = currentMount->clientId;
	}*/

	AddOutfit(msg, currentOutfit);

	/*std::vector<ProtocolOutfit> protocolOutfits;
	if (player->isAccessPlayer()) {
		static const std::string gamemasterOutfitName = "Gamemaster";
		protocolOutfits.emplace_back(gamemasterOutfitName, 75, 0);
	}

	protocolOutfits.reserve(outfits.size());
	for (const Outfit& outfit : outfits) {
		uint8_t addons;
		if (!player->getOutfitAddons(outfit, addons)) {
			continue;
		}

		protocolOutfits.emplace_back(outfit.name, outfit.lookType, addons);
		if (protocolOutfits.size() == std::numeric_limits<uint8_t>::max()) { // Game client currently doesn't allow more than 255 outfits
			break;
		}
	}

	msg.addByte(protocolOutfits.size());
	for (const ProtocolOutfit& outfit : protocolOutfits) {
		msg.add<uint16_t>(outfit.lookType);
		msg.addString(outfit.name);
		msg.addByte(outfit.addons);
	}

	std::vector<const Mount*> mounts;
	for (const Mount& mount : g_game.mounts.getMounts()) {
		if (player->hasMount(&mount)) {
			mounts.push_back(&mount);
		}
	}

	msg.addByte(mounts.size());
	for (const Mount* mount : mounts) {
		msg.add<uint16_t>(mount->clientId);
		msg.addString(mount->name);
	}*/

	switch (player->getSex()) {
		case PLAYERSEX_FEMALE: {
			msg.add<uint16_t>(136);
			if (player->isPremium()) {
				msg.add<uint16_t>(142);
			} else {
				msg.add<uint16_t>(139);
			}

			break;
		}

		case PLAYERSEX_MALE: {
			msg.add<uint16_t>(128);
			if (player->isPremium()) {
				msg.add<uint16_t>(134);
			} else {
				msg.add<uint16_t>(131);
			}

			break;
		}

		default: {
			msg.add<uint16_t>(128);
			msg.add<uint16_t>(134);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdatedVIPStatus(uint32_t guid, VipStatus_t newStatus)
{
	NetworkMessage msg;
	//msg.addByte(0xD3);
	msg.addByte(newStatus ? 0xD3 : 0xD4);
	msg.add<uint32_t>(guid);
	//msg.addByte(newStatus);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendVIP(uint32_t guid, const std::string& name, const std::string&, uint32_t, bool, VipStatus_t status)
{
	NetworkMessage msg;
	msg.addByte(0xD2);
	msg.add<uint32_t>(guid);
	msg.addString(name);
	//msg.addString(description);
	//msg.add<uint32_t>(std::min<uint32_t>(10, icon));
	//msg.addByte(notify ? 0x01 : 0x00);
	msg.addByte(status);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendVIPEntries()
{
	const std::forward_list<VIPEntry>& vipEntries = IOLoginData::getVIPEntries(player->getAccount());

	for (const VIPEntry& entry : vipEntries) {
		VipStatus_t vipStatus = VIPSTATUS_ONLINE;

		Player* vipPlayer = g_game.getPlayerByGUID(entry.guid);

		if (!vipPlayer || !player->canSeeCreature(vipPlayer)) {
			vipStatus = VIPSTATUS_OFFLINE;
		}

		sendVIP(entry.guid, entry.name, entry.description, entry.icon, entry.notify, vipStatus);
	}
}

void ProtocolGame::sendBestiaryRaces()
{
	if (!player) {
		return;
	}

	const auto entries = loadBestiaryEntries(*player);

	uint16_t unlockedCreatures = 0;
	uint16_t unlockedBosses = 0;
	uint16_t totalCreatures = 0;
	uint16_t totalBosses = 0;
	for (const BestiaryClientEntry& entry : entries) {
		const bool isBoss = isBestiaryBossEntry(entry);
		if (isBoss) {
			++totalBosses;
		} else {
			++totalCreatures;
		}

		if (entry.kills > 0) {
			if (isBoss) {
				++unlockedBosses;
			} else {
				++unlockedCreatures;
			}
		}
	}

	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0xD5);
	output->add<uint16_t>(2);
	output->addString(BESTIARY_CATEGORY_CREATURES);
	output->add<uint16_t>(totalCreatures);
	output->add<uint16_t>(unlockedCreatures);
	output->addString(BESTIARY_CATEGORY_BOSSES);
	output->add<uint16_t>(totalBosses);
	output->add<uint16_t>(unlockedBosses);
	send(output);
}

void ProtocolGame::sendBestiaryCharmsData()
{
	if (!player) {
		return;
	}

	// Charm states are authoritative in memory for the whole session: the only
	// writer (Player::setCharmState) updates memory and database atomically on
	// the Dispatcher, and login loads the states once. Reloading here made
	// every bestiary window opening run a synchronous database query on the
	// Dispatcher (M3).
	const auto entries = loadBestiaryEntries(*player);

	std::vector<uint16_t> finishedMonsters;
	finishedMonsters.reserve(entries.size());

	for (const BestiaryClientEntry& entry : entries) {
		if (entry.kills < entry.config.killsStage3) {
			continue;
		}

		finishedMonsters.emplace_back(entry.config.creatureId);
	}

	const uint32_t earnedCharmPoints = g_game.getBestiaryCharmPoints(*player);
	const uint32_t spentCharmPoints = player->getSpentCharmPoints();
	const uint32_t availableCharmPoints = earnedCharmPoints > spentCharmPoints ? earnedCharmPoints - spentCharmPoints : 0;
	const auto& charmDefinitions = getCharmDefinitions();

	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0xD8);
	output->add<uint32_t>(availableCharmPoints);
	const std::size_t charmLimit = std::min<std::size_t>(charmDefinitions.size(), std::numeric_limits<uint8_t>::max());
	output->addByte(static_cast<uint8_t>(charmLimit));
	for (std::size_t i = 0; i < charmLimit; ++i) {
		const CharmDefinition& definition = charmDefinitions[i];
		const PlayerCharmState state = player->getCharmState(definition.id);
		const bool unlocked = state != PlayerCharmState::LOCKED;
		const bool active = state == PlayerCharmState::ACTIVE;

		output->addByte(definition.id);
		output->addString(definition.name);
		output->addString(definition.description);
		output->addByte(0);
		output->add<uint16_t>(definition.unlockCost);
		output->addByte(unlocked ? 1 : 0);
		if (unlocked) {
			output->addByte(active ? 1 : 0);
			if (active) {
				output->add<uint16_t>(0);
				output->add<uint32_t>(0);
			}
		} else {
			output->addByte(0);
		}

		output->addByte(definition.iconIndex);
		output->addByte(definition.chance);
		output->addByte(definition.aoeReductionPercent < 0 ? 0xFF : static_cast<uint8_t>(definition.aoeReductionPercent));
		output->addByte(static_cast<uint8_t>(state));
	}

	output->addByte(0); // unknown / available slots for older clients
	output->add<uint16_t>(static_cast<uint16_t>(std::min<std::size_t>(finishedMonsters.size(), std::numeric_limits<uint16_t>::max())));

	const std::size_t limit = std::min<std::size_t>(finishedMonsters.size(), std::numeric_limits<uint16_t>::max());
	for (std::size_t i = 0; i < limit; ++i) {
		output->add<uint16_t>(finishedMonsters[i]);
	}

	struct BonusEntry {
		const char* name;
		std::string value;
		const char* source;
	};

	std::vector<BonusEntry> bonusEntries;
	const CharacterBonuses& bonuses = player->getCharacterBonuses();
	if (bonuses.totalCriticalChance > 0) {
		bonusEntries.push_back({"Critical Chance", fmt::format("{:d}%", bonuses.totalCriticalChance), "Character"});
	}
	if (bonuses.charmCriticalChance > 0) {
		bonusEntries.push_back({
			"Crit. Damage",
			fmt::format("{:d}-{:d}%", bonuses.charmCriticalDamageMinPercent, bonuses.charmCriticalDamageMaxPercent),
			"Savage Blow"
		});
	}
	if (bonuses.charmDodgeChance > 0) {
		bonusEntries.push_back({"Dodge Chance", fmt::format("{:d}%", bonuses.charmDodgeChance), "Dodge"});
	}
	if (bonuses.equipmentCriticalDamagePercent > 0) {
		bonusEntries.push_back({"Equipment Critical Damage", fmt::format("+{:d}%", bonuses.equipmentCriticalDamagePercent), "Equipment"});
	}
	if (bonuses.equipmentLifeLeechChance > 0 || bonuses.equipmentLifeLeechAmount > 0) {
		bonusEntries.push_back({
			"Life Leech",
			fmt::format("{:d}% chance / {:d}% amount", bonuses.equipmentLifeLeechChance, bonuses.equipmentLifeLeechAmount),
			"Equipment"
		});
	}
	if (bonuses.equipmentManaLeechChance > 0 || bonuses.equipmentManaLeechAmount > 0) {
		bonusEntries.push_back({
			"Mana Leech",
			fmt::format("{:d}% chance / {:d}% amount", bonuses.equipmentManaLeechChance, bonuses.equipmentManaLeechAmount),
			"Equipment"
		});
	}
	if (bonuses.charmLifeLeechChance > 0 && bonuses.charmLifeLeechAmount > 0) {
		bonusEntries.push_back({
			"Life Leech",
			fmt::format("{:d}% chance / {:d}% amount", bonuses.charmLifeLeechChance, bonuses.charmLifeLeechAmount),
			"Vampiric Embrace"
		});
	}
	if (bonuses.charmManaLeechChance > 0 && bonuses.charmManaLeechAmount > 0) {
		bonusEntries.push_back({
			"Mana Leech",
			fmt::format("{:d}% chance / {:d}% amount / {:d}% AoE", bonuses.charmManaLeechChance, bonuses.charmManaLeechAmount, bonuses.charmManaLeechAreaEfficiency),
			"Void's Call"
		});
	}

	const std::size_t bonusLimit = std::min<std::size_t>(bonusEntries.size(), std::numeric_limits<uint8_t>::max());
	output->addByte(static_cast<uint8_t>(bonusLimit));
	for (std::size_t i = 0; i < bonusLimit; ++i) {
		const BonusEntry& bonus = bonusEntries[i];
		output->addString(bonus.name);
		output->addString(bonus.value);
		output->addString(bonus.source);
	}

	send(output);
}

void ProtocolGame::sendBestiaryOverview(const std::string& categoryName, bool search, const std::vector<uint16_t>& creatureIds)
{
	if (!player) {
		return;
	}

	const auto entries = loadBestiaryEntries(*player);
	std::unordered_set<uint16_t> requestedIds(creatureIds.begin(), creatureIds.end());

	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0xD6);
	output->addString(search ? BESTIARY_CATEGORY_RESULT : categoryName);

	std::vector<const BestiaryClientEntry*> filteredEntries;
	filteredEntries.reserve(entries.size());
	for (const BestiaryClientEntry& entry : entries) {
		if (search) {
			if (requestedIds.find(entry.config.creatureId) != requestedIds.end()) {
				filteredEntries.emplace_back(&entry);
			}
			continue;
		}

		if (matchesBestiaryCategory(entry, categoryName)) {
			filteredEntries.emplace_back(&entry);
		}
	}

	output->add<uint16_t>(static_cast<uint16_t>(filteredEntries.size()));
	for (const BestiaryClientEntry* entry : filteredEntries) {
		const uint8_t currentLevel = getBestiaryCurrentLevel(*entry);
		output->add<uint16_t>(entry->config.creatureId);
		output->addByte(currentLevel);
		if (currentLevel > 0) {
			output->addByte(0);
		}
		output->addString(entry->displayName);
		addBestiaryOutfit(*output, entry->outfit);
	}

	send(output);
}

void ProtocolGame::sendBestiaryMonsterData(uint16_t creatureId)
{
	if (!player) {
		return;
	}

	BestiaryClientEntry entry;
	if (!loadBestiaryEntry(*player, creatureId, entry)) {
		return;
	}

	const uint8_t currentLevel = getBestiaryCurrentLevel(entry);

	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0xD7);
	output->add<uint16_t>(entry.config.creatureId);
	output->addString(getBestiaryCategoryName(entry));
	output->addByte(currentLevel);
	output->addString(entry.displayName);
	addBestiaryOutfit(*output, entry.outfit);
	output->add<uint32_t>(entry.kills);
	output->add<uint16_t>(entry.config.killsStage1);
	output->add<uint16_t>(entry.config.killsStage2);
	output->add<uint16_t>(entry.config.killsStage3);
	output->addByte(getBestiaryDifficulty(entry));
	output->addByte(0);

	std::vector<const LootBlock*> bestiaryLootEntries;
	if (entry.monsterType) {
		bestiaryLootEntries.reserve(entry.monsterType->info.lootItems.size());
		for (const LootBlock& loot : entry.monsterType->info.lootItems) {
			collectBestiaryLootEntries(loot, bestiaryLootEntries);
		}
		if (bestiaryLootEntries.size() > std::numeric_limits<uint8_t>::max()) {
			bestiaryLootEntries.resize(std::numeric_limits<uint8_t>::max());
		}
	}

	const uint8_t lootCount = static_cast<uint8_t>(bestiaryLootEntries.size());
	output->addByte(lootCount);
	for (uint8_t i = 0; i < lootCount; ++i) {
		const LootBlock& loot = *bestiaryLootEntries[i];
		uint16_t itemClientId = 0;
		if (loot.id != 0) {
			itemClientId = Item::items[loot.id].clientId;
		}

		output->add<uint16_t>(itemClientId);
		output->addByte(getBestiaryLootDifficulty(loot));
		output->addByte(0);

		if (loot.id != 0) {
			const ItemType& itemType = Item::items[loot.id];
			output->addString(itemType.name);
			output->addByte(static_cast<uint8_t>(std::min<uint32_t>(loot.countmax, std::numeric_limits<uint8_t>::max())));
		}
	}

	if (currentLevel > 1) {
		output->add<uint16_t>(entry.config.charmPoints);
		output->addByte(getBestiaryAttackMode(entry));
		output->addByte(0);
		output->add<uint32_t>(entry.monsterType ? static_cast<uint32_t>(std::max<int32_t>(entry.monsterType->info.healthMax, 0)) : 0);
		output->add<uint32_t>(entry.monsterType ? static_cast<uint32_t>(std::min<uint64_t>(entry.monsterType->info.experience, std::numeric_limits<uint32_t>::max())) : 0);
		output->add<uint16_t>(entry.monsterType ? static_cast<uint16_t>(std::max<uint32_t>(0, entry.monsterType->info.baseSpeed)) : 0);
		output->add<uint16_t>(entry.monsterType ? static_cast<uint16_t>(std::max<int32_t>(entry.monsterType->info.armor, 0)) : 0);
		output->addDouble(0.0);
	}

	if (currentLevel > 2) {
		output->addByte(8);
		output->addByte(0);
		output->add<uint16_t>(getBestiaryElementValue(entry, COMBAT_PHYSICALDAMAGE));
		output->addByte(1);
		output->add<uint16_t>(getBestiaryElementValue(entry, COMBAT_FIREDAMAGE));
		output->addByte(2);
		output->add<uint16_t>(getBestiaryElementValue(entry, COMBAT_EARTHDAMAGE));
		output->addByte(3);
		output->add<uint16_t>(getBestiaryElementValue(entry, COMBAT_ENERGYDAMAGE));
		output->addByte(4);
		output->add<uint16_t>(getBestiaryElementValue(entry, COMBAT_ICEDAMAGE));
		output->addByte(5);
		output->add<uint16_t>(getBestiaryElementValue(entry, COMBAT_HOLYDAMAGE));
		output->addByte(6);
		output->add<uint16_t>(getBestiaryElementValue(entry, COMBAT_DEATHDAMAGE));
		output->addByte(7);
		output->add<uint16_t>(100);
		output->add<uint16_t>(0);
	}

	if (currentLevel > 3) {
		output->addByte(0);
		output->addByte(0);
	}

	send(output);
}

void ProtocolGame::sendSpellCooldown(uint8_t spellId, uint32_t time)
{
	NetworkMessage msg;
	msg.addByte(0xA4);
	msg.addByte(spellId);
	msg.add<uint32_t>(time);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSpellGroupCooldown(SpellGroup_t groupId, uint32_t time)
{
	NetworkMessage msg;
	msg.addByte(0xA5);
	msg.addByte(groupId);
	msg.add<uint32_t>(time);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendModalWindow(const ModalWindow& modalWindow)
{
	NetworkMessage msg;
	msg.addByte(0xFA);

	msg.add<uint32_t>(modalWindow.id);
	msg.addString(modalWindow.title);
	msg.addString(modalWindow.message);

	msg.addByte(modalWindow.buttons.size());
	for (const auto& it : modalWindow.buttons) {
		msg.addString(it.first);
		msg.addByte(it.second);
	}

	msg.addByte(modalWindow.choices.size());
	for (const auto& it : modalWindow.choices) {
		msg.addString(it.first);
		msg.addByte(it.second);
	}

	msg.addByte(modalWindow.defaultEscapeButton);
	msg.addByte(modalWindow.defaultEnterButton);
	msg.addByte(modalWindow.priority ? 0x01 : 0x00);

	writeToOutputBuffer(msg);
}

////////////// Add common messages
void ProtocolGame::AddCreature(NetworkMessage& msg, const Creature* creature, bool known, uint32_t remove)
{
	//CreatureType_t creatureType = creature->getType();

	const Player* otherPlayer = creature->getPlayer();

	if (known) {
		msg.add<uint16_t>(0x62);
		msg.add<uint32_t>(creature->getID());
	} else {
		msg.add<uint16_t>(0x61);
		msg.add<uint32_t>(remove);
		msg.add<uint32_t>(creature->getID());
		//msg.addByte(creatureType);
		msg.addString(creature->getName());
	}

	if (creature->isHealthHidden()) {
		msg.addByte(0x00);
	} else {
		msg.addByte(std::ceil((static_cast<double>(creature->getHealth()) / std::max<int32_t>(creature->getMaxHealth(), 1)) * 100));
	}

	msg.addByte(creature->getDirection());

	if (!creature->isInGhostMode() && !creature->isInvisible()) {
		AddOutfit(msg, creature->getCurrentOutfit());
	} else {
		static Outfit_t outfit;
		AddOutfit(msg, outfit);
	}

	LightInfo lightInfo = creature->getCreatureLight();
	msg.addByte(player->isAccessPlayer() ? 0xFF : lightInfo.level);
	msg.addByte(lightInfo.color);

	//msg.add<uint16_t>(creature->getStepSpeed() / 2);
	msg.add<uint16_t>(creature->getStepSpeed());

	msg.addByte(player->getSkullClient(creature));
	msg.addByte(player->getPartyShield(otherPlayer));

	// Elite Creatures: tier (0 = regular) rendered by the custom client
	// as outfit darkening. Stars above the creature name are deferred.
	const Monster* creatureMonster = creature->getMonster();
	msg.addByte(creatureMonster ? static_cast<uint8_t>(creatureMonster->getEliteTier()) : 0);

	/*if (!known) {
		msg.addByte(player->getGuildEmblem(otherPlayer));
	}

	if (creatureType == CREATURETYPE_MONSTER) {
		const Creature* master = creature->getMaster();
		if (master) {
			const Player* masterPlayer = master->getPlayer();
			if (masterPlayer) {
				if (masterPlayer == player) {
					creatureType = CREATURETYPE_SUMMON_OWN;
				} else {
					creatureType = CREATURETYPE_SUMMON_OTHERS;
				}
			}
		}
	}

	msg.addByte(creatureType); // Type (for summons)
	msg.addByte(creature->getSpeechBubble());
	msg.addByte(0xFF); // MARK_UNMARKED
	

	if (otherPlayer) {
		msg.add<uint16_t>(otherPlayer->getHelpers());
	} else {
		msg.add<uint16_t>(0x00);
	}

	msg.addByte(player->canWalkthroughEx(creature) ? 0x00 : 0x01);
	*/
}

void ProtocolGame::AddPlayerStats(NetworkMessage& msg)
{
	msg.addByte(0xA0);

	msg.add<uint16_t>(std::min<int32_t>(player->getHealth(), std::numeric_limits<uint16_t>::max()));
	msg.add<uint16_t>(std::min<int32_t>(player->getMaxHealth(), std::numeric_limits<uint16_t>::max()));

	//msg.add<uint32_t>(player->getFreeCapacity());
	//msg.add<uint32_t>(player->getCapacity());
	msg.add<uint16_t>(player->getFreeCapacity() / 100);

	//msg.add<uint64_t>(player->getExperience());
	msg.add<uint32_t>(std::min<uint32_t>(player->getExperience(), 0x7FFFFFFF)); // tibia client debugs if value is higher than 0x7FFFFFFF

	msg.add<uint16_t>(player->getLevel());
	msg.addByte(player->getLevelPercent());

	/*msg.add<uint16_t>(100); // base xp gain rate
	msg.add<uint16_t>(0); // xp voucher
	msg.add<uint16_t>(0); // low level bonus
	msg.add<uint16_t>(0); // xp boost
	msg.add<uint16_t>(100); // stamina multiplier (100 = x1.0)
	*/

	msg.add<uint16_t>(std::min<int32_t>(player->getMana(), std::numeric_limits<uint16_t>::max()));
	msg.add<uint16_t>(std::min<int32_t>(player->getMaxMana(), std::numeric_limits<uint16_t>::max()));

	msg.addByte(std::min<uint32_t>(player->getMagicLevel(), std::numeric_limits<uint8_t>::max()));
	//msg.addByte(std::min<uint32_t>(player->getBaseMagicLevel(), std::numeric_limits<uint8_t>::max()));
	msg.addByte(player->getMagicLevelPercent());

	msg.addByte(player->getSoul());

	/*msg.add<uint16_t>(player->getStaminaMinutes());

	msg.add<uint16_t>(player->getBaseSpeed() / 2);

	Condition* condition = player->getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT);
	msg.add<uint16_t>(condition ? condition->getTicks() / 1000 : 0x00);

	msg.add<uint16_t>(player->getOfflineTrainingTime() / 60 / 1000);

	msg.add<uint16_t>(0); // xp boost time (seconds)
	msg.addByte(0); // enables exp boost in the store
	*/
}

void ProtocolGame::AddPlayerSkills(NetworkMessage& msg)
{
	msg.addByte(0xA1);

	for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
		//msg.add<uint16_t>(std::min<int32_t>(player->getSkillLevel(i), std::numeric_limits<uint16_t>::max()));
		//msg.add<uint16_t>(player->getBaseSkill(i));
		msg.addByte(std::min<int32_t>(player->getSkillLevel(i), std::numeric_limits<uint8_t>::max()));
		msg.addByte(player->getSkillPercent(i));
	}

	/*for (uint8_t i = SPECIALSKILL_FIRST; i <= SPECIALSKILL_LAST; ++i) {
		msg.add<uint16_t>(std::min<int32_t>(100, player->varSpecialSkills[i]));
		msg.add<uint16_t>(0);
	}*/
}

void ProtocolGame::AddOutfit(NetworkMessage& msg, const Outfit_t& outfit)
{
	msg.add<uint16_t>(outfit.lookType);

	if (outfit.lookType != 0) {
		msg.addByte(outfit.lookHead);
		msg.addByte(outfit.lookBody);
		msg.addByte(outfit.lookLegs);
		msg.addByte(outfit.lookFeet);
		//msg.addByte(outfit.lookAddons);
	} else {
		msg.addItemId(outfit.lookTypeEx);
	}

	//msg.add<uint16_t>(outfit.lookMount);
}

void ProtocolGame::AddWorldLight(NetworkMessage& msg, LightInfo lightInfo)
{
	msg.addByte(0x82);
	msg.addByte((player->isAccessPlayer() ? 0xFF : lightInfo.level));
	msg.addByte(lightInfo.color);
}

void ProtocolGame::AddCreatureLight(NetworkMessage& msg, const Creature* creature)
{
	LightInfo lightInfo = creature->getCreatureLight();

	msg.addByte(0x8D);
	msg.add<uint32_t>(creature->getID());
	msg.addByte((player->isAccessPlayer() ? 0xFF : lightInfo.level));
	msg.addByte(lightInfo.color);
}

//tile
void ProtocolGame::RemoveTileThing(NetworkMessage& msg, const Position& pos, uint32_t stackpos)
{
	if (stackpos >= 10) {
		return;
	}

	msg.addByte(0x6C);
	msg.addPosition(pos);
	msg.addByte(stackpos);
}

void ProtocolGame::RemoveTileCreature(NetworkMessage& msg, const Creature* creature, const Position& pos, uint32_t stackpos)
{
	if (stackpos < 10) {
		RemoveTileThing(msg, pos, stackpos);
		return;
	}

	msg.addByte(0x6C);
	msg.add<uint16_t>(0xFFFF);
	msg.add<uint32_t>(creature->getID());
}

void ProtocolGame::MoveUpCreature(NetworkMessage& msg, const Creature* creature, const Position& newPos,
                                  const Position& oldPos)
{
	if (creature != player) {
		return;
	}

	//floor change up
	msg.addByte(0xBE);

	//going to surface
	if (newPos.z == 7) {
		int32_t skip = -1;

		// floor 7 and 6 already set
		for (int i = 5; i >= 0; --i) {
			GetFloorDescription(msg, oldPos.x - Map::maxClientViewportX, oldPos.y - Map::maxClientViewportY, i, (Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2, 8 - i, skip);
		}
		if (skip >= 0) {
			msg.addByte(skip);
			msg.addByte(0xFF);
		}
	}
	//underground, going one floor up (still underground)
	else if (newPos.z > 7) {
		int32_t skip = -1;
		GetFloorDescription(msg, oldPos.x - Map::maxClientViewportX, oldPos.y - Map::maxClientViewportY, oldPos.getZ() - 3, (Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2, 3, skip);

		if (skip >= 0) {
			msg.addByte(skip);
			msg.addByte(0xFF);
		}
	}

	//moving up a floor up makes us out of sync
	//west
	msg.addByte(0x68);
	GetMapDescription(oldPos.x - Map::maxClientViewportX, oldPos.y - (Map::maxClientViewportY - 1), newPos.z, 1, (Map::maxClientViewportY * 2) + 2, msg);

	//north
	msg.addByte(0x65);
	GetMapDescription(oldPos.x - Map::maxClientViewportX, oldPos.y - Map::maxClientViewportY, newPos.z, (Map::maxClientViewportX * 2) + 2, 1, msg);
}

void ProtocolGame::MoveDownCreature(NetworkMessage& msg, const Creature* creature, const Position& newPos,
                                    const Position& oldPos)
{
	if (creature != player) {
		return;
	}

	//floor change down
	msg.addByte(0xBF);

	//going from surface to underground
	if (newPos.z == 8) {
		int32_t skip = -1;

		for (int i = 0; i < 3; ++i) {
			GetFloorDescription(msg, oldPos.x - Map::maxClientViewportX, oldPos.y - Map::maxClientViewportY, newPos.z + i, (Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2, -i - 1, skip);
		}
		if (skip >= 0) {
			msg.addByte(skip);
			msg.addByte(0xFF);
		}
	}
	//going further down
	else if (newPos.z > oldPos.z && newPos.z > 8 && newPos.z < 14) {
		int32_t skip = -1;
		GetFloorDescription(msg, oldPos.x - Map::maxClientViewportX, oldPos.y - Map::maxClientViewportY, newPos.z + 2, (Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2, -3, skip);

		if (skip >= 0) {
			msg.addByte(skip);
			msg.addByte(0xFF);
		}
	}

	//moving down a floor makes us out of sync
	//east
	msg.addByte(0x66);
	GetMapDescription(oldPos.x + (Map::maxClientViewportX + 1), oldPos.y - (Map::maxClientViewportY + 1), newPos.z, 1, (Map::maxClientViewportY * 2) + 2, msg);

	//south
	msg.addByte(0x67);
	GetMapDescription(oldPos.x - Map::maxClientViewportX, oldPos.y + (Map::maxClientViewportY + 1), newPos.z, (Map::maxClientViewportX * 2) + 2, 1, msg);
}

void ProtocolGame::AddShopItem(NetworkMessage& msg, const ShopInfo& item)
{
	const ItemType& it = Item::items[item.itemId];
	msg.add<uint16_t>(it.clientId);

	if (it.isSplash() || it.isFluidContainer()) {
		msg.addByte(serverFluidToClient(item.subType));
	} else {
		msg.addByte(0x00);
	}

	msg.addString(item.realName);
	msg.add<uint32_t>(it.weight);
	msg.add<uint32_t>(item.buyPrice);
	msg.add<uint32_t>(item.sellPrice);
}

void ProtocolGame::parseExtendedOpcode(NetworkMessage& msg)
{
	uint8_t opcode = msg.getByte();
	const std::string& buffer = msg.getString();

	// process additional opcodes via lua script event
	addGameTask(&Game::parsePlayerExtendedOpcode, player->getID(), opcode, buffer);
}
