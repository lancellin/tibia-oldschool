CamList = {}

local camListWindow
local camTextList
local recordControls
local recordUpdateEvent
local playing = false
local wasSeeking = false
local audioWasEnabled = false

local RECORD_SPEEDS = { 0.25, 0.5, 1, 2, 4, 8, 16 }
local RECORD_CONTROLS_HOTKEY = 'Ctrl+Shift+H'

local function isCamFile(file)
    return type(file) == 'string' and file:lower():match('%.cam$') ~= nil
end

local function formatRecordTime(milliseconds)
    local totalSeconds = math.max(0, math.floor((tonumber(milliseconds) or 0) / 1000))
    local hours = math.floor(totalSeconds / 3600)
    local minutes = math.floor((totalSeconds % 3600) / 60)
    local seconds = totalSeconds % 60
    return string.format('%02d:%02d:%02d', hours, minutes, seconds)
end

local function formatRecordSpeed(speed)
    if speed == 0.25 then
        return '0,25x'
    elseif speed == 0.5 then
        return '0,5x'
    end
    return string.format('%dx', speed)
end

local function restoreAudioAfterSeek()
    if wasSeeking and audioWasEnabled and g_sounds then
        g_sounds.setAudioEnabled(true)
    end
    wasSeeking = false
    audioWasEnabled = false
end

local function destroyRecordControls()
    if recordUpdateEvent then
        removeEvent(recordUpdateEvent)
        recordUpdateEvent = nil
    end

    restoreAudioAfterSeek()
    if recordControls then
        recordControls:destroy()
        recordControls = nil
    end
end

local function updateRecordControls()
    recordUpdateEvent = nil
    if not recordControls or not playing or not g_game.isPlayingRecord() then
        return
    end

    local position = g_game.getRecordPosition()
    local duration = g_game.getRecordDuration()
    local percent = duration > 0 and math.min(100, (position * 100) / duration) or 0
    local seeking = g_game.isRecordSeeking()

    recordControls:getChildById('progress'):setPercent(percent)
    recordControls:getChildById('time'):setText(formatRecordTime(position) .. ' / ' .. formatRecordTime(duration))
    recordControls:getChildById('status'):setText(seeking and tr('Rebuilding...') or tr('CAM playback'))
    recordControls:recursiveGetChildById('pause'):setText(g_game.isRecordPaused() and tr('Resume') or tr('Pause'))
    recordControls:recursiveGetChildById('speed'):setText(formatRecordSpeed(g_game.getRecordSpeed()))

    for _, id in ipairs({ 'rewind', 'rewindSmall', 'pause', 'forwardSmall', 'forward', 'speedDown', 'speedUp' }) do
        recordControls:recursiveGetChildById(id):setEnabled(not seeking)
    end

    if seeking and not wasSeeking then
        wasSeeking = true
        if g_sounds then
            audioWasEnabled = g_sounds.isAudioEnabled()
            if audioWasEnabled then
                g_sounds.setAudioEnabled(false)
            end
        end
    elseif not seeking and wasSeeking then
        restoreAudioAfterSeek()
    end

    recordUpdateEvent = scheduleEvent(updateRecordControls, 100)
end

local function onRecordStart()
    if not playing then
        return
    end

    if recordControls then
        return
    end

    recordControls = g_ui.displayUI('recordcontrols', modules.game_interface.getRootPanel())
    updateRecordControls()
end

local function onRecordEnd()
    if not playing then
        return
    end

    destroyRecordControls()
    playing = false
    scheduleEvent(function()
        if CharacterList.isVisible() then
            CharacterList.hide(false)
        end
        EnterGame.show()
    end, 1)
end

function CamList.init()
    camListWindow = g_ui.displayUI('camlist')
    camTextList = camListWindow:getChildById('camList')
    camListWindow:hide()

    connect(g_game, {
        onGameStart = onRecordStart,
        onRecordEnd = onRecordEnd
    })

    g_keyboard.bindKeyPress(RECORD_CONTROLS_HOTKEY, CamList.toggleControls)
end

function CamList.terminate()
    g_keyboard.unbindKeyPress(RECORD_CONTROLS_HOTKEY, CamList.toggleControls)

    disconnect(g_game, {
        onGameStart = onRecordStart,
        onRecordEnd = onRecordEnd
    })

    destroyRecordControls()

    if camListWindow then
        camListWindow:destroy()
        camListWindow = nil
        camTextList = nil
    end

    playing = false
    CamList = nil
end

function CamList.reload()
    camTextList:destroyChildren()
    g_resources.makeDir('records')

    local files = {}
    for _, file in ipairs(g_resources.listDirectoryFiles('/records')) do
        if isCamFile(file) then
            files[#files + 1] = file
        end
    end

    table.sort(files)
    for index = #files, 1, -1 do
        local file = files[index]
        local widget = g_ui.createWidget('CamWidget', camTextList)
        widget:setId('cam-' .. index)
        widget.camFile = file
        widget:getChildById('name'):setText(file:gsub('%.cam$', ''))
        widget:setTooltip(file)
        connect(widget, {
            onDoubleClick = function()
                CamList.play()
                return true
            end
        })
    end

    camListWindow:getChildById('emptyLabel'):setVisible(#files == 0)
    camListWindow:getChildById('buttonPlay'):setEnabled(#files > 0)
end

function CamList.show()
    if g_game.isOnline() or g_game.isLogging() then
        return
    end

    CamList.reload()
    camListWindow:show()
    camListWindow:raise()
    camListWindow:focus()
end

function CamList.hide(showLogin)
    if camListWindow then
        camListWindow:hide()
    end

    if showLogin and not g_game.isOnline() and not g_game.isLogging() then
        EnterGame.show()
    end
end

function CamList.play()
    local selected = camTextList:getFocusedChild()
    if not selected or not selected.camFile then
        return
    end

    local file = selected.camFile
    CamList.hide(false)
    playing = true

    local ok, errorMessage = pcall(function()
        g_game.playRecord(file)
    end)

    if not ok then
        playing = false
        EnterGame.show()
        displayErrorBox(tr('CAM Error'), tr('Unable to play this recording.') .. '\n' .. tostring(errorMessage))
    end
end

function CamList.togglePause()
    if playing and g_game.isPlayingRecord() and not g_game.isRecordSeeking() then
        g_game.setRecordPaused(not g_game.isRecordPaused())
    end
end

function CamList.seek(seconds)
    if playing and g_game.isPlayingRecord() and not g_game.isRecordSeeking() then
        g_game.seekRecord(seconds * 1000)
    end
end

function CamList.toggleControls()
    if playing and recordControls and g_game.isPlayingRecord() then
        recordControls:setVisible(not recordControls:isVisible())
    end
end

function CamList.changeSpeed(direction)
    if not playing or not g_game.isPlayingRecord() or g_game.isRecordSeeking() then
        return
    end

    local current = g_game.getRecordSpeed()
    local currentIndex = 1
    local smallestDifference = math.huge
    for index, speed in ipairs(RECORD_SPEEDS) do
        local difference = math.abs(speed - current)
        if difference < smallestDifference then
            smallestDifference = difference
            currentIndex = index
        end
    end

    local newIndex = math.max(1, math.min(#RECORD_SPEEDS, currentIndex + direction))
    g_game.setRecordSpeed(RECORD_SPEEDS[newIndex])
end
