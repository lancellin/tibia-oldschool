local CREATURE_STACK_ATTRIBUTE = "creaturestack"
local MAX_CHANCE_BASIS_POINTS = 5000
local BASE_CHANCE_BASIS_POINTS = 400
local CHANCE_PER_ALCHEMY_LEVEL_BASIS_POINTS = 60

local goldConverter = Action()

local function consumeCharge(item, charges)
	if charges <= 1 then
		item:remove()
	else
		item:transform(item:getId(), charges - 1)
	end
end

function goldConverter.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if not target or target:getId() ~= ITEM_GOLD_COIN or target:getCount() ~= 100 then
		return false
	end

	local charges = item:getCharges()
	if charges <= 0 then
		return false
	end

	local isCreatureGold = target:getCustomAttribute(CREATURE_STACK_ATTRIBUTE) == true
	local chance = math.min(MAX_CHANCE_BASIS_POINTS,
		BASE_CHANCE_BASIS_POINTS + player:getAlchemyLevel() * CHANCE_PER_ALCHEMY_LEVEL_BASIS_POINTS)
	if math.random(10000) <= chance then
		target:transform(ITEM_PLATINUM_COIN, 1)
		target:removeCustomAttribute(CREATURE_STACK_ATTRIBUTE)

		if target:getId() == ITEM_PLATINUM_COIN and target:getCount() == 1 and
				target:getCustomAttribute(CREATURE_STACK_ATTRIBUTE) == nil then
			if isCreatureGold then
				player:addAlchemyTries(1)
			end
			player:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)
		else
			print("[Error - Gold Converter] Successful roll did not produce one common platinum coin.")
			player:getPosition():sendMagicEffect(CONST_ME_POFF)
		end
	else
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
	end

	consumeCharge(item, charges)
	return true
end

goldConverter:id(26378)
goldConverter:register()
