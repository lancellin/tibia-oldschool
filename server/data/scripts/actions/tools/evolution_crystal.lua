-- Evolution crystals: evolve common training weapons into their
-- Spark/Lightning/Infernal versions. The success chance scales with the
-- player's Alchemy skill; the crystal is consumed on every attempt.

local CHANCE_PER_ALCHEMY_LEVEL_BASIS_POINTS = 50 -- 0.5%
local MAX_CHANCE_BASIS_POINTS = 10000 -- 100%

local CRYSTALS = {
	-- spark crystal: common -> spark
	[26399] = {
		baseChanceBasisPoints = 5500, -- 55%
		evolutions = {
			[26379] = 26384, -- training sword -> spark training sword
			[26380] = 26385, -- training axe -> spark training axe
			[26381] = 26386, -- training club -> spark training club
			[26382] = 26387, -- training spear -> spark training spear
			[26383] = 26388, -- training shield -> spark training shield
		},
	},
	-- lightning crystal: common -> lightning
	[26400] = {
		baseChanceBasisPoints = 4000, -- 40%
		evolutions = {
			[26379] = 26389, -- training sword -> lightning training sword
			[26380] = 26390, -- training axe -> lightning training axe
			[26381] = 26391, -- training club -> lightning training club
			[26382] = 26392, -- training spear -> lightning training spear
			[26383] = 26393, -- training shield -> lightning training shield
		},
	},
	-- infernal crystal: common -> infernal
	[26401] = {
		baseChanceBasisPoints = 1000, -- 10%
		evolutions = {
			[26379] = 26394, -- training sword -> infernal training sword
			[26380] = 26395, -- training axe -> infernal training axe
			[26381] = 26396, -- training club -> infernal training club
			[26382] = 26397, -- training spear -> infernal training spear
			[26383] = 26398, -- training shield -> infernal training shield
		},
	},
}

local evolutionCrystal = Action()

function evolutionCrystal.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local crystal = CRYSTALS[item:getId()]
	if not crystal then
		return false
	end

	if not target or not target:isItem() then
		return false
	end

	local evolvedId = crystal.evolutions[target:getId()]
	if not evolvedId then
		return false
	end

	local chance = math.min(MAX_CHANCE_BASIS_POINTS,
		crystal.baseChanceBasisPoints + player:getAlchemyLevel() * CHANCE_PER_ALCHEMY_LEVEL_BASIS_POINTS)

	-- Consume the crystal before the roll so the attempt is atomic.
	item:remove(1)

	local targetPosition = target:getPosition()
	if math.random(MAX_CHANCE_BASIS_POINTS) <= chance then
		local charges = ItemType(evolvedId):getCharges()
		if charges > 0 then
			target:transform(evolvedId, charges)
		else
			target:transform(evolvedId)
		end

		if target:getId() == evolvedId then
			targetPosition:sendMagicEffect(CONST_ME_MAGIC_BLUE)
		else
			print("[Error - Evolution Crystal] Successful roll did not transform the training weapon.")
			targetPosition:sendMagicEffect(CONST_ME_POFF)
		end
	else
		targetPosition:sendMagicEffect(CONST_ME_POFF)
	end

	return true
end

evolutionCrystal:id(26399, 26400, 26401)
evolutionCrystal:register()
