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

#include "spawn.h"
#include "game.h"
#include "monster.h"
#include "configmanager.h"
#include "scheduler.h"

#include "pugicast.h"
#include "events.h"

extern ConfigManager g_config;
extern Monsters g_monsters;
extern Game g_game;
extern Events* g_events;

static constexpr int32_t MINSPAWN_INTERVAL = 10 * 1000; // 10 seconds to match RME
static constexpr int32_t MAXSPAWN_INTERVAL = 24 * 60 * 60 * 1000; // 1 day

namespace {
	struct SpawnIntervalRange {
		uint32_t minimum;
		uint32_t maximum;
	};

	constexpr SpawnIntervalRange getSpawnIntervalRange(uint32_t baseSeconds, uint32_t playerBucket)
	{
		const uint32_t denominator = playerBucket + 400;
		return {
			static_cast<uint32_t>((static_cast<uint64_t>(baseSeconds) * 200 + denominator - 1) / denominator),
			static_cast<uint32_t>((static_cast<uint64_t>(baseSeconds) * 400 + denominator - 1) / denominator),
		};
	}

	constexpr uint32_t applySpawnTimeRate(uint32_t seconds, uint32_t spawnTimeRate, uint32_t boostPercent)
	{
		const uint64_t denominator = static_cast<uint64_t>(spawnTimeRate) * boostPercent;
		const uint64_t adjusted = (static_cast<uint64_t>(seconds) * 100 + denominator - 1) / denominator;
		return static_cast<uint32_t>(adjusted == 0 ? 1 : adjusted);
	}

	static_assert(getSpawnIntervalRange(600, 0).minimum == 300 && getSpawnIntervalRange(600, 0).maximum == 600);
	static_assert(getSpawnIntervalRange(600, 50).minimum == 267 && getSpawnIntervalRange(600, 50).maximum == 534);
	static_assert(getSpawnIntervalRange(600, 100).minimum == 240 && getSpawnIntervalRange(600, 100).maximum == 480);
	static_assert(getSpawnIntervalRange(600, 150).minimum == 219 && getSpawnIntervalRange(600, 150).maximum == 437);
	static_assert(getSpawnIntervalRange(600, 200).minimum == 200 && getSpawnIntervalRange(600, 200).maximum == 400);
	static_assert(getSpawnIntervalRange(600, 250).minimum == 185 && getSpawnIntervalRange(600, 250).maximum == 370);
	static_assert(getSpawnIntervalRange(600, 300).minimum == 172 && getSpawnIntervalRange(600, 300).maximum == 343);
	static_assert(getSpawnIntervalRange(600, 350).minimum == 160 && getSpawnIntervalRange(600, 350).maximum == 320);
	static_assert(getSpawnIntervalRange(600, 400).minimum == 150 && getSpawnIntervalRange(600, 400).maximum == 300);
	static_assert(getSpawnIntervalRange(600, 450).minimum == 142 && getSpawnIntervalRange(600, 450).maximum == 283);
	static_assert(getSpawnIntervalRange(600, 500).minimum == 134 && getSpawnIntervalRange(600, 500).maximum == 267);
	static_assert(getSpawnIntervalRange(600, 550).minimum == 127 && getSpawnIntervalRange(600, 550).maximum == 253);
	static_assert(getSpawnIntervalRange(600, 600).minimum == 120 && getSpawnIntervalRange(600, 600).maximum == 240);
	static_assert(applySpawnTimeRate(300, 1, 100) == 300);
	static_assert(applySpawnTimeRate(600, 2, 100) == 300);
	static_assert(applySpawnTimeRate(300, 1, 125) == 240);
	static_assert(applySpawnTimeRate(600, 2, 125) == 240);
}

bool Spawns::loadFromXml(const std::string& filename)
{
	if (loaded) {
		return true;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.c_str());
	if (!result) {
		printXMLError("Error - Spawns::loadFromXml", filename, result);
		return false;
	}

	this->filename = filename;
	loaded = true;

	for (auto spawnNode : doc.child("spawns").children()) {
		Position centerPos(
			pugi::cast<uint16_t>(spawnNode.attribute("centerx").value()),
			pugi::cast<uint16_t>(spawnNode.attribute("centery").value()),
			pugi::cast<uint16_t>(spawnNode.attribute("centerz").value())
		);

		int32_t radius;
		pugi::xml_attribute radiusAttribute = spawnNode.attribute("radius");
		if (radiusAttribute) {
			radius = pugi::cast<int32_t>(radiusAttribute.value());
		} else {
			radius = -1;
		}

		if (radius > 30) {
			std::cout << "[Warning - Spawns::loadFromXml] Radius size bigger than 30 at position: " << centerPos << ", consider lowering it." << std::endl;
		}

		if (!spawnNode.first_child()) {
			std::cout << "[Warning - Spawns::loadFromXml] Empty spawn at position: " << centerPos << " with radius: " << radius << '.' << std::endl;
			continue;
		}

		spawnList.emplace_front(centerPos, radius);
		Spawn& spawn = spawnList.front();

		for (auto childNode : spawnNode.children()) {
			if (strcasecmp(childNode.name(), "monsters") == 0) {
				Position pos(
					centerPos.x + pugi::cast<uint16_t>(childNode.attribute("x").value()),
					centerPos.y + pugi::cast<uint16_t>(childNode.attribute("y").value()),
					centerPos.z
				);

				int32_t interval = pugi::cast<int32_t>(childNode.attribute("spawntime").value()) * 1000;
				if (interval < MINSPAWN_INTERVAL) {
					std::cout << "[Warning - Spawns::loadFromXml] " << pos << " spawntime can not be less than " << MINSPAWN_INTERVAL / 1000 << " seconds." << std::endl;
					continue;
				} else if (interval > MAXSPAWN_INTERVAL) {
					std::cout << "[Warning - Spawns::loadFromXml] " << pos << " spawntime can not be more than " << MAXSPAWN_INTERVAL / 1000 << " seconds." << std::endl;
					continue;
				}

				size_t monstersCount = std::distance(childNode.children().begin(), childNode.children().end());
				if (monstersCount == 0) {
					std::cout << "[Warning - Spawns::loadFromXml] " << pos << " empty monsters set." << std::endl;
					continue;
				}

				uint16_t totalChance = 0;
				spawnBlock_t sb;
				sb.pos = pos;
				sb.direction = DIRECTION_NORTH;
				sb.interval = interval;
				sb.lastSpawn = 0;

				for (auto monsterNode : childNode.children()) {
					pugi::xml_attribute nameAttribute = monsterNode.attribute("name");
					if (!nameAttribute) {
						continue;
					}

					MonsterType* mType = g_monsters.getMonsterType(nameAttribute.as_string());
					if (!mType) {
						std::cout << "[Warning - Spawn::loadFromXml] " << pos << " can not find " << nameAttribute.as_string() << std::endl;
						continue;
					}

					uint16_t chance = 100 / monstersCount;
					pugi::xml_attribute chanceAttribute = monsterNode.attribute("chance");
					if (chanceAttribute) {
						chance = pugi::cast<uint16_t>(chanceAttribute.value());
					}

					if (chance + totalChance > 100) {
						chance = 100 - totalChance;
						totalChance = 100;
						std::cout << "[Warning - Spawns::loadFromXml] " << mType->name << ' ' << pos << " total chance for set can not be higher than 100." << std::endl;
					} else {
						totalChance += chance;
					}

					sb.mTypes.push_back({mType, chance});
				}

				if (sb.mTypes.empty()) {
					std::cout << "[Warning - Spawns::loadFromXml] " << pos << " empty monsters set." << std::endl;
					continue;
				}

				sb.mTypes.shrink_to_fit();
				if (sb.mTypes.size() > 1) {
					std::sort(sb.mTypes.begin(), sb.mTypes.end(), [](std::pair<MonsterType*, uint16_t> a, std::pair<MonsterType*, uint16_t> b) {
						return a.second > b.second;
					});
				}

				spawn.addBlock(sb);
			} else if (strcasecmp(childNode.name(), "monster") == 0) {
				pugi::xml_attribute nameAttribute = childNode.attribute("name");
				if (!nameAttribute) {
					continue;
				}

				Direction dir;

				pugi::xml_attribute directionAttribute = childNode.attribute("direction");
				if (directionAttribute) {
					dir = static_cast<Direction>(pugi::cast<uint16_t>(directionAttribute.value()));
				} else {
					dir = DIRECTION_NORTH;
				}

				Position pos(
					centerPos.x + pugi::cast<uint16_t>(childNode.attribute("x").value()),
					centerPos.y + pugi::cast<uint16_t>(childNode.attribute("y").value()),
					centerPos.z
				);
				int32_t interval = pugi::cast<int32_t>(childNode.attribute("spawntime").value()) * 1000;
				if (interval >= MINSPAWN_INTERVAL && interval <= MAXSPAWN_INTERVAL) {
					spawn.addMonster(nameAttribute.as_string(), pos, dir, static_cast<uint32_t>(interval));
				} else {
					if (interval < MINSPAWN_INTERVAL) {
						std::cout << "[Warning - Spawns::loadFromXml] " << nameAttribute.as_string() << ' ' << pos << " spawntime can not be less than " << MINSPAWN_INTERVAL / 1000 << " seconds." << std::endl;
					} else {
						std::cout << "[Warning - Spawns::loadFromXml] " << nameAttribute.as_string() << ' ' << pos << " spawntime can not be more than " << MAXSPAWN_INTERVAL / 1000 << " seconds." << std::endl;
					}
				}
			} else if (strcasecmp(childNode.name(), "npc") == 0) {
				pugi::xml_attribute nameAttribute = childNode.attribute("name");
				if (!nameAttribute) {
					continue;
				}

				Npc* npc = Npc::createNpc(nameAttribute.as_string());
				if (!npc) {
					continue;
				}

				pugi::xml_attribute directionAttribute = childNode.attribute("direction");
				if (directionAttribute) {
					npc->setDirection(static_cast<Direction>(pugi::cast<uint16_t>(directionAttribute.value())));
				}

				npc->setMasterPos(Position(
					centerPos.x + pugi::cast<uint16_t>(childNode.attribute("x").value()),
					centerPos.y + pugi::cast<uint16_t>(childNode.attribute("y").value()),
					centerPos.z
				), radius);
				npcList.push_front(npc);
			}
		}
	}
	return true;
}

void Spawns::startup()
{
	if (!loaded || isStarted()) {
		return;
	}

	for (Npc* npc : npcList) {
		if (!g_game.placeCreature(npc, npc->getMasterPos(), false, true)) {
			std::cout << "[Warning - Spawns::startup] Couldn't spawn npc \"" << npc->getName() << "\" on position: " << npc->getMasterPos() << '.' << std::endl;
			delete npc;
		}
	}
	npcList.clear();

	if (g_config.getBoolean(ConfigManager::DISABLE_MONSTER_SPAWNS)) {
		std::cout << ">> Monster spawns disabled by configuration." << std::endl;
	} else {
		for (Spawn& spawn : spawnList) {
			spawn.startup();
		}
	}

	started = true;
}

void Spawns::clear()
{
	for (Spawn& spawn : spawnList) {
		spawn.stopEvent();
	}
	spawnList.clear();

	loaded = false;
	started = false;
	filename.clear();
}

bool Spawns::isInZone(const Position& centerPos, int32_t radius, const Position& pos)
{
	if (radius == -1) {
		return true;
	}

	return ((pos.getX() >= centerPos.getX() - radius) && (pos.getX() <= centerPos.getX() + radius) &&
			(pos.getY() >= centerPos.getY() - radius) && (pos.getY() <= centerPos.getY() + radius));
}

void Spawn::startSpawnCheck()
{
	scheduleNextSpawnCheck();
}

Spawn::~Spawn()
{
	for (const auto& it : spawnedMap) {
		Monster* monster = it.second;
		monster->setSpawn(nullptr);
		monster->decrementReferenceCounter();
	}
}

bool Spawn::findPlayer(const Position& pos)
{
	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, pos, false, true);
	for (Creature* spectator : spectators) {
		if (!spectator->getPlayer()->hasFlag(PlayerFlag_IgnoredByMonsters)) {
			return true;
		}
	}
	return false;
}

uint32_t Spawn::getBlockedSpawnRetryInterval()
{
	constexpr int32_t minRetrySeconds = 1;
	constexpr int32_t maxRetrySeconds = 24 * 60 * 60;
	const int32_t retrySeconds = std::clamp(g_config.getNumber(ConfigManager::BLOCKED_SPAWN_RETRY_INTERVAL), minRetrySeconds, maxRetrySeconds);
	return static_cast<uint32_t>(retrySeconds) * 1000;
}

uint32_t Spawn::getDynamicSpawnInterval(uint32_t baseInterval)
{
	const uint32_t playerBucket = g_game.getSpawnPlayerBucket();
	const SpawnIntervalRange range = getSpawnIntervalRange(baseInterval / 1000, playerBucket);
	const uint32_t spawnTimeRate = static_cast<uint32_t>(std::max<int32_t>(1, g_config.getNumber(ConfigManager::RATE_SPAWN_TIME)));
	const uint32_t boostPercent = g_game.isSpawnRateBoostActive() ? 125 : 100;
	const uint32_t minimum = applySpawnTimeRate(range.minimum, spawnTimeRate, boostPercent);
	const uint32_t maximum = applySpawnTimeRate(range.maximum, spawnTimeRate, boostPercent);
	return static_cast<uint32_t>(uniform_random(static_cast<int32_t>(minimum), static_cast<int32_t>(maximum))) * 1000;
}

void Spawn::markSpawnPending(uint32_t spawnId, int64_t now)
{
	auto it = spawnMap.find(spawnId);
	if (it == spawnMap.end()) {
		return;
	}

	spawnBlock_t& sb = it->second;
	sb.lastSpawn = now;
	sb.blockedRetryAt = 0;
	sb.nextSpawnAt = now + getDynamicSpawnInterval(sb.interval);
}

void Spawn::scheduleSpawnCheck(uint32_t delay)
{
	delay = std::max<uint32_t>(delay, SCHEDULER_MINTICKS);
	const int64_t eventAt = OTSYS_TIME() + delay;
	if (checkSpawnEvent != 0) {
		if (checkSpawnAt != 0 && checkSpawnAt <= eventAt) {
			return;
		}

		g_scheduler.stopEvent(checkSpawnEvent);
		checkSpawnEvent = 0;
	}

	const uint64_t generation = ++checkSpawnGeneration;
	checkSpawnAt = eventAt;
	checkSpawnEvent = g_scheduler.addEvent(createSchedulerTask(delay, std::bind(&Spawn::checkSpawn, this, generation)));
}

void Spawn::scheduleNextSpawnCheck()
{
	const int64_t now = OTSYS_TIME();
	int64_t nextCheckAt = 0;
	for (const auto& it : spawnMap) {
		const uint32_t spawnId = it.first;
		if (spawnedMap.find(spawnId) != spawnedMap.end()) {
			continue;
		}

		const spawnBlock_t& sb = it.second;
		const int64_t candidate = sb.blockedRetryAt != 0 ? sb.blockedRetryAt : sb.nextSpawnAt;
		if (candidate != 0 && (nextCheckAt == 0 || candidate < nextCheckAt)) {
			nextCheckAt = candidate;
		}
	}

	if (nextCheckAt != 0) {
		scheduleSpawnCheck(static_cast<uint32_t>(std::max<int64_t>(SCHEDULER_MINTICKS, nextCheckAt - now)));
	}
}

bool Spawn::isInSpawnZone(const Position& pos)
{
	return Spawns::isInZone(centerPos, radius, pos);
}

Spawn::SpawnResult Spawn::spawnMonster(uint32_t spawnId, const spawnBlock_t& sb, bool startup/* = false*/)
{
	bool isBlocked = !startup && findPlayer(sb.pos);
	size_t monstersCount = sb.mTypes.size(), blockedMonsters = 0;

	const auto spawnFunc = [&](bool roll) {
		for (const auto& pair : sb.mTypes) {
			if (isBlocked && !pair.first->info.isIgnoringSpawnBlock) {
				++blockedMonsters;
				continue;
			}

			if (!roll) {
				return spawnMonster(spawnId, pair.first, sb.pos, sb.direction, startup);
			}

			if (pair.second >= normal_random(1, 100) && spawnMonster(spawnId, pair.first, sb.pos, sb.direction, startup)) {
				return true;
			}
		}

		return false;
	};

	// Try to spawn something with chance check, unless it's single spawn
	if (spawnFunc(monstersCount > 1)) {
		return SpawnResult::Spawned;
	}

	// Every monster spawn is blocked, bail out
	if (monstersCount == blockedMonsters) {
		return SpawnResult::PlayerBlocked;
	}

	// Just try to spawn something without chance check
	return spawnFunc(false) ? SpawnResult::Spawned : SpawnResult::Failed;
}

bool Spawn::spawnMonster(uint32_t spawnId, MonsterType* mType, const Position& pos, Direction dir, bool startup/*= false*/)
{
	std::unique_ptr<Monster> monster_ptr(new Monster(mType));
	if (!g_events->eventMonsterOnSpawn(monster_ptr.get(), pos, startup, false)) {
		return false;
	}

	if (startup) {
		//No need to send out events to the surrounding since there is no one out there to listen!
		if (!g_game.internalPlaceCreature(monster_ptr.get(), pos, true)) {
			std::cout << "[Warning - Spawns::startup] Couldn't spawn monster \"" << monster_ptr->getName() << "\" on position: " << pos << '.' << std::endl;
			return false;
		}
	} else {
		if (!g_game.placeCreature(monster_ptr.get(), pos, false, true)) {
			return false;
		}
	}

	Monster* monster = monster_ptr.release();
	monster->setDirection(dir);
	monster->setSpawn(this);
	monster->setMasterPos(pos);
	monster->incrementReferenceCounter();

	spawnedMap.insert({spawnId, monster});
	spawnBlock_t& sb = spawnMap[spawnId];
	sb.lastSpawn = OTSYS_TIME();
	sb.nextSpawnAt = 0;
	sb.blockedRetryAt = 0;
	return true;
}

void Spawn::startup()
{
	const int64_t now = OTSYS_TIME();
	for (const auto& it : spawnMap) {
		uint32_t spawnId = it.first;
		const spawnBlock_t& sb = it.second;
		if (spawnMonster(spawnId, sb, true) != SpawnResult::Spawned) {
			markSpawnPending(spawnId, now);
		}
	}
	scheduleNextSpawnCheck();
}

void Spawn::checkSpawn(uint64_t generation)
{
	if (generation != checkSpawnGeneration) {
		return;
	}

	checkSpawnEvent = 0;
	checkSpawnAt = 0;

	cleanup();

	const int64_t now = OTSYS_TIME();
	uint32_t spawnCount = 0;
	bool rateLimitReached = false;
	const uint32_t blockedRetryInterval = getBlockedSpawnRetryInterval();
	const uint32_t spawnLimit = std::max<int32_t>(1, g_config.getNumber(ConfigManager::RATE_SPAWN));

	const auto trySpawn = [&](uint32_t spawnId, spawnBlock_t& sb) {
		switch (spawnMonster(spawnId, sb)) {
			case SpawnResult::Spawned:
				sb.blockedRetryAt = 0;
				return true;

			case SpawnResult::PlayerBlocked:
				sb.blockedRetryAt = now + blockedRetryInterval;
				return false;

			case SpawnResult::Failed:
				markSpawnPending(spawnId, now);
				return false;
		}

		return false;
	};

	// Retry player-blocked spawns first. Their chosen respawn deadline stays
	// untouched, so a blocked retry never rolls another dynamic interval.
	for (auto& it : spawnMap) {
		const uint32_t spawnId = it.first;
		spawnBlock_t& sb = it.second;
		if (sb.blockedRetryAt == 0 || now < sb.blockedRetryAt || spawnedMap.find(spawnId) != spawnedMap.end()) {
			continue;
		}

		if (trySpawn(spawnId, sb) && ++spawnCount >= spawnLimit) {
			rateLimitReached = true;
			break;
		}
	}

	if (!rateLimitReached) {
		for (auto& it : spawnMap) {
			const uint32_t spawnId = it.first;
			if (spawnedMap.find(spawnId) != spawnedMap.end()) {
				continue;
			}

			spawnBlock_t& sb = it.second;
			if (sb.blockedRetryAt != 0 || sb.nextSpawnAt == 0 || now < sb.nextSpawnAt) {
				continue;
			}

			if (trySpawn(spawnId, sb) && ++spawnCount >= spawnLimit) {
				rateLimitReached = true;
				break;
			}
		}
	}

	scheduleNextSpawnCheck();
}

void Spawn::cleanup()
{
	const int64_t now = OTSYS_TIME();
	auto it = spawnedMap.begin();
	while (it != spawnedMap.end()) {
		uint32_t spawnId = it->first;
		Monster* monster = it->second;
		if (monster->isRemoved()) {
			if (spawnId != 0 && spawnMap[spawnId].nextSpawnAt == 0) {
				markSpawnPending(spawnId, now);
			}

			monster->decrementReferenceCounter();
			it = spawnedMap.erase(it);
		} else if (!isInSpawnZone(monster->getPosition()) && spawnId != 0) {
			if (spawnMap[spawnId].nextSpawnAt == 0) {
				markSpawnPending(spawnId, now);
			}
			spawnedMap.insert({0, monster});
			it = spawnedMap.erase(it);
		} else {
			++it;
		}
	}
}

bool Spawn::addBlock(spawnBlock_t sb)
{
	interval = std::min(interval, sb.interval);
	spawnMap[spawnMap.size() + 1] = sb;

	return true;
}

bool Spawn::addMonster(const std::string& name, const Position& pos, Direction dir, uint32_t interval)
{
	MonsterType* mType = g_monsters.getMonsterType(name);
	if (!mType) {
		std::cout << "[Warning - Spawn::addMonster] Can not find " << name << std::endl;
		return false;
	}

	spawnBlock_t sb;
	sb.mTypes.push_back({mType, 100});
	sb.pos = pos;
	sb.direction = dir;
	sb.interval = interval;
	sb.lastSpawn = 0;

	return addBlock(sb);
}

void Spawn::removeMonster(Monster* monster)
{
	for (auto it = spawnedMap.begin(), end = spawnedMap.end(); it != end; ++it) {
		if (it->second == monster) {
			const uint32_t spawnId = it->first;
			monster->decrementReferenceCounter();
			spawnedMap.erase(it);
			if (spawnId != 0) {
				markSpawnPending(spawnId, OTSYS_TIME());
				scheduleNextSpawnCheck();
			}
			break;
		}
	}
}

void Spawn::stopEvent()
{
	++checkSpawnGeneration;
	if (checkSpawnEvent != 0) {
		g_scheduler.stopEvent(checkSpawnEvent);
		checkSpawnEvent = 0;
	}
	checkSpawnAt = 0;
}
