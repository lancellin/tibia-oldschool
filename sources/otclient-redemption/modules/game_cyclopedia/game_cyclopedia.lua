Cyclopedia = {}

trackerButton = nil
trackerMiniWindow = nil
trackerButtonBosstiary = nil
trackerMiniWindowBosstiary = nil
contentContainer = nil

local buttonSelection = nil
local items = nil
local bestiary = nil
local charms = nil
local map = nil
local houses = nil
local character = nil
local CyclopediaButton = nil
local bosstiary = nil
local bossSlot = nil
local ButtonBossSlot = nil
local ButtonBestiary = nil
local tabStack = {}
local previousType = nil
local windowTypes = {}
local magicalArchives = nil
local bestiaryOnlyMode = false
Cyclopedia.StoredBestiaryCharmPoints = 0

local function isBestiaryOnlyAllowedTab(tabName)
    return tabName == "bestiary" or tabName == "charms"
end

local function configureBestiaryOnlyTabs()
    local tabsToHide = { items, map, houses, character, bosstiary, bossSlot, magicalArchives }
    for _, tab in ipairs(tabsToHide) do
        if tab then
            tab:hide()
        end
    end

    if bestiary then
        bestiary:breakAnchors()
        bestiary:addAnchor(AnchorTop, "parent", AnchorTop)
        bestiary:addAnchor(AnchorLeft, "parent", AnchorLeft)
        bestiary:setMarginLeft(1)
    end

    if charms then
        charms:show()
    end
end

local function refreshBestiaryOnlyTabState(currentType)
    if not bestiaryOnlyMode then
        return
    end

    local function setReducedTabState(tabWidget, isActive, activeImageSource)
        if not tabWidget then
            return
        end

        local inactiveBackground = tabWidget:getChildById("InactiveBackground")
        local iconOverlay = tabWidget:getChildById("IconOverlay")
        local textOverlay = tabWidget:getChildById("TextOverlay")

        tabWidget:setWidth(150)
        tabWidget:setHeight(34)
        tabWidget:setOn(isActive)

        if isActive then
            tabWidget:setImageSource(activeImageSource)
            if inactiveBackground then
                inactiveBackground:hide()
            end
            if iconOverlay then
                iconOverlay:hide()
            end
            if textOverlay then
                textOverlay:hide()
            end
            tabWidget:disable()
        else
            tabWidget:setImageSource("")
            if inactiveBackground then
                inactiveBackground:show()
            end
            if iconOverlay then
                iconOverlay:show()
            end
            if textOverlay then
                textOverlay:show()
            end
            tabWidget:enable()
        end
    end

    setReducedTabState(bestiary, currentType == "bestiary", "/images/game/cyclopedia/bestiary_on")
    setReducedTabState(charms, currentType == "charms", "/images/game/cyclopedia/charms_on")

    for tabName, window in pairs(windowTypes) do
        if tabName ~= "bestiary" and tabName ~= "charms" and window and window.obj then
            window.obj:setOn(false)
            window.obj:enable()
            if window.obj.getChildById then
                local iconOverlay = window.obj:getChildById("IconOverlay")
                local textOverlay = window.obj:getChildById("TextOverlay")
                if iconOverlay then
                    iconOverlay:show()
                end
                if textOverlay then
                    textOverlay:show()
                end
            end
        end
    end
end
function toggle(defaultWindow)
    if not controllerCyclopedia.ui then
        return
    end
    if controllerCyclopedia.ui:isVisible() then
        return hide()
    end
    show(defaultWindow)
end

controllerCyclopedia = Controller:new()
controllerCyclopedia:setUI('game_cyclopedia')

function controllerCyclopedia:onInit()
    -- pre m_gameInitialized
    self:registerEvents(g_game, {
        onParseCyclopediaTracker = function(trackerType, data)
            if Cyclopedia.onParseCyclopediaTracker then
                Cyclopedia.onParseCyclopediaTracker(trackerType, data)
            end
        end
    })
end

function controllerCyclopedia:onGameStart()
    local versionClient = g_game.getClientVersion()
    bestiaryOnlyMode = versionClient < 1310
    Cyclopedia.BestiaryOnlyMode = bestiaryOnlyMode

    contentContainer = controllerCyclopedia.ui:recursiveGetChildById('contentContainer')
    buttonSelection = controllerCyclopedia.ui:recursiveGetChildById('buttonSelection')
    items = buttonSelection:recursiveGetChildById('items')
    bestiary = buttonSelection:recursiveGetChildById('bestiary')
    charms = buttonSelection:recursiveGetChildById('charms')
    map = buttonSelection:recursiveGetChildById('map')
    houses = buttonSelection:recursiveGetChildById('houses')
    character = buttonSelection:recursiveGetChildById('character')
    bosstiary = buttonSelection:recursiveGetChildById('bosstiary')
    bossSlot = buttonSelection:recursiveGetChildById('bossSlot')
    magicalArchives = buttonSelection:recursiveGetChildById('magicalArchives')

    if bestiaryOnlyMode then
        ButtonBestiary = modules.game_mainpanel.addToggleButton("bestiary", tr("Bestiary"),
            "/images/options/bestiaryTracker", function() toggle("bestiary") end, false, 17)
        configureBestiaryOnlyTabs()
    else
        CyclopediaButton = modules.game_mainpanel.addToggleButton('CyclopediaButton', tr('Cyclopedia'),
            '/images/options/cooldowns', function() toggle("items") end, false, 7)
        ButtonBossSlot = modules.game_mainpanel.addToggleButton("bossSlot", tr("Open Boss Slots dialog"),
            "/images/options/ButtonBossSlot", function() toggle("bossSlot") end, false, 20)
        CyclopediaButton:setOn(false)
        ButtonBestiary = modules.game_mainpanel.addToggleButton("bosstiary", tr("Open Bosstiary dialog"),
            "/images/options/ButtonBosstiary", function() toggle("bosstiary") end, false, 17)
    end

    windowTypes = {
        bestiary = { obj = bestiary, func = showBestiary },
        charms = { obj = charms, func = showCharms },
    }

    if not bestiaryOnlyMode then
        windowTypes.items = { obj = items, func = showItems }
        windowTypes.map = { obj = map, func = showMap }
        windowTypes.houses = { obj = houses, func = showHouse }
        windowTypes.character = { obj = character, func = showCharacter }
        windowTypes.bosstiary = { obj = bosstiary, func = showBosstiary }
        windowTypes.bossSlot = { obj = bossSlot, func = showBossSlot }
        windowTypes.magicalArchives = { obj = magicalArchives, func = showMagicalArchives }
    end

    g_ui.importStyle("cyclopedia_widgets")
    g_ui.importStyle("cyclopedia_pages")

    controllerCyclopedia:registerEvents(g_game, {
        onResourcesBalanceChange = Cyclopedia.onResourcesBalanceChange,
        -- bestiary
        onParseBestiaryRaces = Cyclopedia.loadBestiaryCategories,
        onParseBestiaryOverview = Cyclopedia.loadBestiaryOverview,
        onUpdateBestiaryMonsterData = Cyclopedia.loadBestiarySelectedCreature,
        -- bosstiary
        onParseSendBosstiary = Cyclopedia.LoadBosstiaryCreatures,
        -- boss_slot
        onParseBosstiarySlots = Cyclopedia.loadBossSlots,
        -- character
        onParseCyclopediaCharacterGeneralStats = Cyclopedia.loadCharacterGeneralStats,
        onParseCyclopediaCharacterCombatStats = Cyclopedia.loadCharacterCombatStats,
        onParseCyclopediaCharacterBadges = Cyclopedia.loadCharacterBadges,
        onCyclopediaCharacterRecentDeaths = Cyclopedia.loadCharacterRecentDeaths,
        onCyclopediaCharacterRecentKills = Cyclopedia.loadCharacterRecentKills,
        onUpdateCyclopediaCharacterItemSummary = Cyclopedia.loadCharacterItems,
        onParseCyclopediaCharacterAppearances = Cyclopedia.loadCharacterAppearances,
        onParseCyclopediaStoreSummary = Cyclopedia.onParseCyclopediaStoreSummary,
        -- character 14.10
        onCyclopediaCharacterOffenceStats = Cyclopedia.onCyclopediaCharacterOffenceStats,
        onCyclopediaCharacterDefenceStats = Cyclopedia.onCyclopediaCharacterDefenceStats,
        onCyclopediaCharacterMiscStats = Cyclopedia.onCyclopediaCharacterMiscStats,
        -- charms
        onUpdateBestiaryCharmsData = Cyclopedia.onBestiaryCharmsData,
        -- items
        onParseItemDetail = Cyclopedia.loadItemDetail
    })

    --[[===================================================
    =               Tracker Bestiary                      =
    =================================================== ]] --

        -- Only create if it doesn't exist
        if not trackerButton and not bestiaryOnlyMode then
            trackerButton = modules.game_mainpanel.addToggleButton("trackerButton", tr("Bestiary Tracker"),
                "/images/options/bestiaryTracker", Cyclopedia.toggleBestiaryTracker, false, 17)
        end

        if trackerButton then
            trackerButton:setOn(false)
        end
        
        -- Only create if it doesn't exist
        if not trackerMiniWindow and not bestiaryOnlyMode then
            trackerMiniWindow = g_ui.createWidget('BestiaryTracker', modules.game_interface.getRightPanel())

            -- Set the title with length limit like in containers
            local titleWidget = trackerMiniWindow:getChildById('miniwindowTitle')
            if titleWidget then
                local title = tr('Bestiary Tracker')
                if title:len() > 12 then
                    title = title:sub(1, 12) .. "..."
                end
                titleWidget:setText(title)
            end

            -- Set up contextMenuButton positioning and click handler
            local contextMenuButton = trackerMiniWindow:recursiveGetChildById('contextMenuButton')
            local newWindowButton = trackerMiniWindow:recursiveGetChildById('newWindowButton')
            local minimizeButton = trackerMiniWindow:recursiveGetChildById('minimizeButton')
            
            if contextMenuButton then
                contextMenuButton:setVisible(true)
                
                -- Position contextMenuButton like in ImbuementTracker
                if minimizeButton then
                    contextMenuButton:breakAnchors()
                    contextMenuButton:addAnchor(AnchorTop, minimizeButton:getId(), AnchorTop)
                    contextMenuButton:addAnchor(AnchorRight, minimizeButton:getId(), AnchorLeft)
                    contextMenuButton:setMarginRight(7)
                    contextMenuButton:setMarginTop(0)
                end
                
                contextMenuButton.onClick = function(widget, mousePos, mouseButton)
                    return Cyclopedia.createTrackerContextMenu("bestiary", mousePos)
                end
            end

            if newWindowButton then
                newWindowButton:setVisible(true)
                newWindowButton.onClick = function(widget, mousePos, mouseButton)
                    toggle("bestiary")
                    return true
                end
            end

            trackerMiniWindow.onOpen = function()
                trackerButton:setOn(true)
                Cyclopedia.refreshBestiaryTracker()
            end

            trackerMiniWindow.onClose = function()
                trackerButton:setOn(false)
            end

            trackerMiniWindow:setup()
            trackerMiniWindow:hide()
        end

        --[[===================================================
    =               Tracker Bosstiary                     =
    =================================================== ]] --

        -- Only create if it doesn't exist
        if not trackerButtonBosstiary and not bestiaryOnlyMode then
            trackerButtonBosstiary = modules.game_mainpanel.addToggleButton("bosstiarytrackerButton",
                tr("Bosstiary Tracker"), "/images/options/bosstiaryTracker", Cyclopedia.toggleBosstiaryTracker, false, 17)
        end

        if trackerButtonBosstiary then
            trackerButtonBosstiary:setOn(false)
        end
        
        -- Only create if it doesn't exist
        if not trackerMiniWindowBosstiary and not bestiaryOnlyMode then
            trackerMiniWindowBosstiary = g_ui.createWidget('BestiaryTracker', modules.game_interface.getRightPanel())
            
            -- Set the title with length limit like in containers
            local titleWidgetBosstiary = trackerMiniWindowBosstiary:getChildById('miniwindowTitle')
            if titleWidgetBosstiary then
                local title = tr('Bosstiary Tracker')
                if title:len() > 12 then
                    title = title:sub(1, 12) .. "..."
                end
                titleWidgetBosstiary:setText(title)
            end

            -- Set the icon for Bosstiary Tracker
            local iconWidgetBosstiary = trackerMiniWindowBosstiary:getChildById('miniwindowIcon')
            if iconWidgetBosstiary then
                iconWidgetBosstiary:setImageSource('/images/icons/icon-bosstracker-widget')
            end

            -- Set up contextMenuButton positioning and click handler for Bosstiary
            local contextMenuButtonBosstiary = trackerMiniWindowBosstiary:recursiveGetChildById('contextMenuButton')
            local newWindowButtonBosstiary = trackerMiniWindowBosstiary:recursiveGetChildById('newWindowButton')
            local minimizeButtonBosstiary = trackerMiniWindowBosstiary:recursiveGetChildById('minimizeButton')
            
            if contextMenuButtonBosstiary then
                contextMenuButtonBosstiary:setVisible(true)
                
                -- Position contextMenuButton like in ImbuementTracker
                if minimizeButtonBosstiary then
                    contextMenuButtonBosstiary:breakAnchors()
                    contextMenuButtonBosstiary:addAnchor(AnchorTop, minimizeButtonBosstiary:getId(), AnchorTop)
                    contextMenuButtonBosstiary:addAnchor(AnchorRight, minimizeButtonBosstiary:getId(), AnchorLeft)
                    contextMenuButtonBosstiary:setMarginRight(7)
                    contextMenuButtonBosstiary:setMarginTop(0)
                end
                
                contextMenuButtonBosstiary.onClick = function(widget, mousePos, mouseButton)
                    return Cyclopedia.createTrackerContextMenu("bosstiary", mousePos)
                end
            end

            if newWindowButtonBosstiary then
                newWindowButtonBosstiary:setVisible(true)
                newWindowButtonBosstiary.onClick = function(widget, mousePos, mouseButton)
                    toggle("bosstiary")
                    return true
                end
            end

            trackerMiniWindowBosstiary.onOpen = function()
                trackerButtonBosstiary:setOn(true)
                if not Cyclopedia.BosstiaryTrackerPending then
                    if trackerMiniWindowBosstiary.contentsPanel then
                        trackerMiniWindowBosstiary.contentsPanel:destroyChildren()
                    end
                    Cyclopedia.refreshBosstiaryTracker()
                end
                Cyclopedia.scheduleBosstiaryTrackerRetry(1000)
            end

            trackerMiniWindowBosstiary.onClose = function()
                trackerButtonBosstiary:setOn(false)
            end

            trackerMiniWindowBosstiary:setup()
            trackerMiniWindowBosstiary:hide()
        end
    if trackerMiniWindow then
        trackerMiniWindow:setupOnStart()
        Cyclopedia.loadTrackerFilters("bestiary")
        if trackerMiniWindow:isVisible() and trackerButton then
            trackerButton:setOn(true)
        end
    end

    if trackerMiniWindowBosstiary then
        trackerMiniWindowBosstiary:setupOnStart()
        Cyclopedia.loadTrackerFilters("bosstiary")
        if trackerMiniWindowBosstiary:isVisible() and trackerButtonBosstiary then
            trackerButtonBosstiary:setOn(true)
        end
    end

    Cyclopedia.BossSlots.UnlockBosses = {}

    if not bestiaryOnlyMode then
        Keybind.new("Windows", "Show/hide Bosstiary Tracker", "", "")
        Keybind.bind("Windows", "Show/hide Bosstiary Tracker", {{
            type = KEY_DOWN,
            callback = Cyclopedia.toggleBosstiaryTracker
        }})

        Keybind.new("Windows", "Show/hide Bestiary Tracker", "", "")
        Keybind.bind("Windows", "Show/hide Bestiary Tracker", {{
            type = KEY_DOWN,
            callback = Cyclopedia.toggleBestiaryTracker
        }})
    end

    if versionClient >= 1410 then
        controllerCyclopedia.ui.CharmsBase.Icon:setImageSource("/game_cyclopedia/images/monster-icon-bonuspoints")
    end
end


function controllerCyclopedia:onGameEnd()
    hide()
    
    if Cyclopedia.saveTrackerFilters then
        Cyclopedia.saveTrackerFilters("bestiary")
        Cyclopedia.saveTrackerFilters("bosstiary")
    end

    if Cyclopedia.clearTrackerDataForCharacterChange then
        Cyclopedia.clearTrackerDataForCharacterChange()
    end

    Keybind.delete("Windows", "Show/hide Bosstiary Tracker")
    Keybind.delete("Windows", "Show/hide Bestiary Tracker")
end

function controllerCyclopedia:onTerminate()
    if trackerButton then
        trackerButton:destroy()
        trackerButton = nil
    end

    if trackerMiniWindow then
        trackerMiniWindow:destroy()
        trackerMiniWindow = nil
    end

    if trackerButtonBosstiary then
        trackerButtonBosstiary:destroy()
        trackerButtonBosstiary = nil
    end

    if trackerMiniWindowBosstiary then
        trackerMiniWindowBosstiary:destroy()
        trackerMiniWindowBosstiary = nil
    end

    if CyclopediaButton then
        CyclopediaButton:destroy()
        CyclopediaButton = nil
    end
    if ButtonBossSlot then
        ButtonBossSlot:destroy()
        ButtonBossSlot = nil
    end
    if ButtonBestiary then
        ButtonBestiary:destroy()
        ButtonBestiary = nil
    end
    
    -- Save items data if available
    if Cyclopedia and Cyclopedia.Items and Cyclopedia.Items.terminate then
        Cyclopedia.Items.terminate()
    end
    
    onTerminateCharm()
end

function hide()
    if not controllerCyclopedia.ui then
        return
    end
    resetCyclopediaTabs()
    controllerCyclopedia.ui:hide()
    if CyclopediaButton then
        CyclopediaButton:setOn(false)
    end
    if ButtonBossSlot then
        ButtonBossSlot:setOn(false)
    end
    if ButtonBestiary then
        ButtonBestiary:setOn(false)
    end
end

function resetCyclopediaTabs()
    tabStack = {}
    controllerCyclopedia.ui.BackButton:setEnabled(false)
    controllerCyclopedia.ui.BackButton:setVisible(not bestiaryOnlyMode)
    if previousType then
        local previousWindow = windowTypes[previousType]
        previousWindow.obj:enable()
        previousWindow.obj:setOn(false)
        previousType = nil;
    end
end

function show(defaultWindow)
    if not controllerCyclopedia.ui then
        return
    end

    if bestiaryOnlyMode and not isBestiaryOnlyAllowedTab(defaultWindow) then
        defaultWindow = "bestiary"
    end

    if not defaultWindow then
        defaultWindow = "bestiary"
    end

    controllerCyclopedia.ui:show()
    controllerCyclopedia.ui:raise()
    controllerCyclopedia.ui:focus()
    controllerCyclopedia.ui.BackButton:setVisible(not bestiaryOnlyMode)
    SelectWindow(defaultWindow, false)
    if bestiaryOnlyMode then
        controllerCyclopedia.ui.GoldBase.Value:setText(Cyclopedia.formatGold(Cyclopedia.StoredBestiaryCharmPoints or 0))
    else
        controllerCyclopedia.ui.GoldBase.Value:setText(Cyclopedia.formatGold(g_game.getLocalPlayer():getTotalMoney()))
    end
end

function Cyclopedia.openTab(tabName)
    if not controllerCyclopedia.ui then
        return false
    end

    if bestiaryOnlyMode and not isBestiaryOnlyAllowedTab(tabName) then
        tabName = "bestiary"
    end

    if not controllerCyclopedia.ui:isVisible() then
        show(tabName)
        return true
    end

    if previousType ~= tabName then
        SelectWindow(tabName, false)
    end

    return true
end

function toggleBack()
    local previousTab = table.remove(tabStack, #tabStack)
    if #tabStack < 1 then
        controllerCyclopedia.ui.BackButton:setEnabled(false)
    end
    SelectWindow(previousTab, true)
end

function SelectWindow(type, isBackButtonPress)
    if bestiaryOnlyMode and not isBestiaryOnlyAllowedTab(type) then
        type = "bestiary"
    end

    if previousType then
        local previousWindow = windowTypes[previousType]
        previousWindow.obj:enable()
        previousWindow.obj:setOn(false)
        if not isBackButtonPress then
            table.insert(tabStack, previousType)
            controllerCyclopedia.ui.BackButton:setEnabled(true)
        end
    end
    contentContainer:destroyChildren()

    local window = windowTypes[type]
    if window then
        window.obj:setOn(true)
        window.obj:disable()
        previousType = type
        if window.func then
            window.func(contentContainer)
        end
    end
    if CyclopediaButton then
        CyclopediaButton:setOn(type == "items" or type == "charms" or type == "map" or type == "houses" or type == "character" or type == "magicalArchives")
    end
    if ButtonBossSlot then
        ButtonBossSlot:setOn(type == "bossSlot")
    end
    if ButtonBestiary then
        ButtonBestiary:setOn(type == "bosstiary" or type == "bestiary")
    end

    refreshBestiaryOnlyTabState(type)
end

function Cyclopedia.onResourcesBalanceChange()
    if not controllerCyclopedia.ui or not controllerCyclopedia.ui:isVisible() then
        return
    end

    local player = g_game.getLocalPlayer()
    if not player then
        return
    end

    if bestiaryOnlyMode then
        controllerCyclopedia.ui.GoldBase.Value:setText(Cyclopedia.formatGold(Cyclopedia.StoredBestiaryCharmPoints or 0))
    else
        controllerCyclopedia.ui.GoldBase.Value:setText(Cyclopedia.formatGold(player:getTotalMoney()))
    end

    local formatResourceBalance = function(resourceType, maxResourceType)
        return string.format("%d/%d", player:getResourceBalance(resourceType),
            player:getResourceBalance(maxResourceType))
    end

    controllerCyclopedia.ui.CharmsBase.Value:setText(formatResourceBalance(ResourceTypes.CHARM,
        ResourceTypes.MAX_CHARM))

    if controllerCyclopedia.ui.CharmsBase1410:isVisible() then
        controllerCyclopedia.ui.CharmsBase1410.Value:setText(formatResourceBalance(
            ResourceTypes.MINOR_CHARM, ResourceTypes.MAX_MINOR_CHARM))
    end
end

function Cyclopedia.onBestiaryCharmsData(charmsData)
    Cyclopedia.StoredBestiaryCharmPoints = charmsData.points or 0

    if controllerCyclopedia and controllerCyclopedia.ui and controllerCyclopedia.ui:isVisible() and bestiaryOnlyMode then
        controllerCyclopedia.ui.GoldBase.Value:setText(Cyclopedia.formatGold(Cyclopedia.StoredBestiaryCharmPoints))
    end

    if Cyclopedia.loadCharms then
        Cyclopedia.loadCharms(charmsData)
    end
end

function isVisible()
    return controllerCyclopedia and controllerCyclopedia.ui and controllerCyclopedia.ui:isVisible()
end
