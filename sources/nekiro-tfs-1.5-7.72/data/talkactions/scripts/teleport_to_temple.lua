local templeDestinations = {
    ["ab"] = Position(32732, 31634, 7),
    ["kazz"] = Position(32649, 31925, 11),
    ["thais"] = Position(32369, 32241, 7),
    ["venore"] = Position(32957, 32076, 7),
    ["carlin"] = Position(32360, 31782, 7),
    ["ank"] = Position(33194, 32853, 8),
    ["edron"] = Position(33217, 31814, 8),
    ["rook"] = Position(32097, 32219, 7),
}

function onSay(player, words, param)
    if not player:getGroup():getAccess() then
        return true
    end

    local key = param:lower():gsub("^%s+", ""):gsub("%s+$", "")
    if key == "" then
        player:sendCancelMessage("Usage: /temple <ab|kazz|thais|venore|carlin|ank|edron|rook>")
        return false
    end

    local position = templeDestinations[key]
    if not position then
        local town = Town(param) or Town(tonumber(param))
        if town then
            position = town:getTemplePosition()
        end
    end

    if not position then
        player:sendCancelMessage("Temple destination not found.")
        return false
    end

    player:teleportTo(position)
    return false
end
