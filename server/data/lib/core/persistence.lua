Persistence = Persistence or {}

Persistence.houseItemsDirty = false
Persistence.dirtyHouseIds = Persistence.dirtyHouseIds or {}

function Persistence.markHouseItemsDirty(house, reason)
	if not house then
		return
	end

	local houseId = house:getId()
	if not houseId then
		return
	end

	if not Persistence.houseItemsDirty then
		print("[Persistence] House items marked dirty" .. (reason and (": " .. reason) or "."))
	end

	Persistence.houseItemsDirty = true
	Persistence.dirtyHouseIds[houseId] = true
end

function Persistence.clearHouseItemsDirty()
	Persistence.houseItemsDirty = false
	Persistence.dirtyHouseIds = {}
end

function Persistence.saveDirtyHouses(reason)
	if not Persistence.houseItemsDirty then
		return false
	end

	print("[Persistence] Saving dirty houses" .. (reason and (" before " .. reason) or "."))
	for houseId in pairs(Persistence.dirtyHouseIds) do
		local house = House(houseId)
		if house then
			house:save()
		end
	end

	Persistence.clearHouseItemsDirty()
	return true
end

Persistence.saveIfHouseItemsDirty = Persistence.saveDirtyHouses
