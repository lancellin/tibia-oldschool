notificationsController = Controller:new()

local function onBestiaryUnlockOpcode(protocolGame, opcode, buffer)
    local stage, raceId, lookType, lookTypeEx, lookMount, lookHead, lookBody, lookLegs, lookFeet, lookAddons, creatureName =
        buffer:match("^(%d+)|(%d+)|(%d+)|(%d+)|(%d+)|(%d+)|(%d+)|(%d+)|(%d+)|(%d+)|(.+)$")
    stage = tonumber(stage)
    raceId = tonumber(raceId)
    lookType = tonumber(lookType)
    lookTypeEx = tonumber(lookTypeEx)
    lookMount = tonumber(lookMount)
    lookHead = tonumber(lookHead)
    lookBody = tonumber(lookBody)
    lookLegs = tonumber(lookLegs)
    lookFeet = tonumber(lookFeet)
    lookAddons = tonumber(lookAddons)

    if not stage or stage < 0 or stage > 3 or not raceId or not lookType or not lookTypeEx or not lookMount or not lookHead or not lookBody or
        not lookLegs or not lookFeet or not lookAddons or not creatureName or creatureName == "" then
        g_logger.warning(string.format("Invalid Bestiary unlock payload: %s", tostring(buffer)))
        return
    end

    local outfit = {
        type = lookType,
        auxType = lookTypeEx,
        mount = lookMount,
        head = lookHead,
        body = lookBody,
        legs = lookLegs,
        feet = lookFeet,
        addons = lookAddons
    }

    if stage == 0 then
        notificationsController:showBestiaryUnlock(outfit, creatureName)
    else
        notificationsController:showBestiaryStage(outfit, creatureName, stage)
    end
end

local function onAdvanceOpcode(protocolGame, opcode, buffer)
    local skill, level = buffer:match("^(%d+)|(%d+)$")
    skill = tonumber(skill)
    level = tonumber(level)

    if not skill or skill < 0 or skill > 8 or not level then
        g_logger.warning(string.format("Invalid advance payload: %s", tostring(buffer)))
        return
    end

    notificationsController:showAdvance(skill, level)
end

function notificationsController:onInit()
    self:registerExtendedOpcode(ExtendedIds.BestiaryUnlock, onBestiaryUnlockOpcode)
    self:registerExtendedOpcode(ExtendedIds.Advance, onAdvanceOpcode)
    self:registerEvents(g_game, {
        onClientEvent = function(...)
            self:onClientEvent(...)
        end,
    })
end
function notificationsController:onTerminate()
    screenshot_onTerminate()
    infoBanner_onTerminate()
end

function notificationsController:onGameStart()
    screenshot_onGameStart()
end

function notificationsController:onGameEnd()
    screenshot_onGameEnd()
end
