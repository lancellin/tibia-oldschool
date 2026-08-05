local LIGHT_COLOR = 215
local NIGHT_LEVEL = 40
local DAY_LEVEL = 250

local isDay = false

function onThink(interval)
	if isDay then
		setWorldLight(NIGHT_LEVEL, LIGHT_COLOR)
	else
		setWorldLight(DAY_LEVEL, LIGHT_COLOR)
	end

	isDay = not isDay
	return true
end
