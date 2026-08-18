local ec = EventCallback

-- Elite Creatures: drop-chance multipliers applied to the whole loot
-- table (including child loot) of an elite corpse, per tier.
local ELITE_LOOT_CHANCE_MULTIPLIER = {
	[1] = 1.25, -- Tier 1: +25%
	[2] = 1.50, -- Tier 2: +50%
	[3] = 4.00, -- Tier 3: 4x (compensa a força extrema da elite)
}

-- Crystal drops are rolled only for creatures with at least this base HP
-- (the HP before any elite modifier, i.e. the MonsterType max health).
local ELITE_CRYSTAL_MIN_BASE_HP = 200

-- Chances are expressed in units of MAX_LOOTCHANCE (100000), so 1% = 1000.
-- chance = baseChance + floor(baseHp / 100) * hpStep
local ELITE_CRYSTALS = {
	{
		itemId = 26399, -- spark crystal
		tiers = { [1] = true, [2] = true, [3] = true },
		baseChance = 10000, -- 10%
		hpStep = 1000, -- +1 percentage point per 100 base HP
	},
	{
		itemId = 26400, -- lightning crystal
		tiers = { [2] = true, [3] = true },
		baseChance = 3000, -- 3%
		hpStep = 1000, -- +1 percentage point per 100 base HP
	},
	{
		itemId = 26401, -- infernal crystal
		tiers = { [3] = true },
		baseChance = 2000, -- 2%
		hpStep = 800, -- +0.8 percentage point per 100 base HP
	},
}

local function applyEliteLootChanceMultiplier(lootList, multiplier)
	for i = 1, #lootList do
		local lootBlock = lootList[i]
		lootBlock.chance = math.min(MAX_LOOTCHANCE, math.floor(lootBlock.chance * multiplier))
		if lootBlock.childLoot and #lootBlock.childLoot > 0 then
			applyEliteLootChanceMultiplier(lootBlock.childLoot, multiplier)
		end
	end
end

local function dropEliteCrystals(monster, corpse, eliteTier)
	local baseHp = monster:getType():maxHealth()
	if baseHp < ELITE_CRYSTAL_MIN_BASE_HP then
		return
	end

	local hpStacks = math.floor(baseHp / 100)
	for _, crystal in ipairs(ELITE_CRYSTALS) do
		if crystal.tiers[eliteTier] then
			local chance = math.min(MAX_LOOTCHANCE, crystal.baseChance + hpStacks * crystal.hpStep)
			if getLootRandom() < chance then
				local crystalItem = Game.createItem(crystal.itemId, 1)
				if crystalItem then
					if corpse:addItemEx(crystalItem) ~= RETURNVALUE_NOERROR then
						crystalItem:remove()
					end
				end
			end
		end
	end
end

ec.onDropLoot = function(self, corpse)
	if configManager.getNumber(configKeys.RATE_LOOT) == 0 then
		return
	end

	local player = Player(corpse:getCorpseOwner())
	local mType = self:getType()
	if not player or player:getStamina() > 840 then
		local eliteTier = corpse:getCustomAttribute("elite_tier") or 0
		local monsterLoot = mType:getLoot()
		if eliteTier > 0 then
			applyEliteLootChanceMultiplier(monsterLoot, ELITE_LOOT_CHANCE_MULTIPLIER[eliteTier] or 1)
		end

		for i = 1, #monsterLoot do
			local item = corpse:createLootItem(monsterLoot[i], true)
			if not item then
				print('[Warning] DropLoot:', 'Could not add loot item to corpse.')
			end
		end

		if eliteTier > 0 then
			dropEliteCrystals(self, corpse, eliteTier)
		end

		if player then
			local text = ("Loot of %s: %s"):format(mType:getNameDescription(), corpse:getContentDescription())
			local party = player:getParty()
			if party then
				party:broadcastPartyLoot(text)
			else
				player:sendTextMessage(MESSAGE_INFO_DESCR, text)
			end
		end
	else
		local text = ("Loot of %s: nothing (due to low stamina)"):format(mType:getNameDescription())
		local party = player:getParty()
		if party then
			party:broadcastPartyLoot(text)
		else
			player:sendTextMessage(MESSAGE_INFO_DESCR, text)
		end
	end
end

ec:register()
