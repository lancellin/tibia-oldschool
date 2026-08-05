local UI = nil
local UI_REFS = nil
local unlockConfirmWindow = nil

Cyclopedia.Charms = Cyclopedia.Charms or {}

local SAVAGE_BLOW_CHARM_ID = 1
local VAMPIRIC_EMBRACE_CHARM_ID = 2
local VOIDS_CALL_CHARM_ID = 3
local DODGE_CHARM_ID = 4
local CHARM_EFFECTS = {
    [SAVAGE_BLOW_CHARM_ID] = "Critical chance / damage.",
    [VAMPIRIC_EMBRACE_CHARM_ID] = "Life leech.",
    [VOIDS_CALL_CHARM_ID] = "Mana leech.",
    [DODGE_CHARM_ID] = "Avoids direct damage."
}

local function requireWidget(root, id)
    local widget = root and root:recursiveGetChildById(id)
    if not widget then
        error(string.format("Charms UI contract error: widget '%s' was not found.", id))
    end
    return widget
end

local function bindUI()
    UI_REFS = {
        charmList = requireWidget(UI, "CharmList"),
        selectedName = requireWidget(UI, "SelectedName"),
        selectedIcon = requireWidget(UI, "SelectedCharmIcon"),
        selectedLockedMask = requireWidget(UI, "SelectedLockedMask"),
        statusValue = requireWidget(UI, "StatusValue"),
        effectValue = requireWidget(UI, "EffectValue"),
        chanceValue = requireWidget(UI, "ChanceValue"),
        aoeValue = requireWidget(UI, "AoeValue"),
        unlockCostValue = requireWidget(UI, "UnlockCostValue"),
        unlockButton = requireWidget(UI, "UnlockButton"),
        descriptionText = requireWidget(UI, "DescriptionText")
    }
end

local function getCharmPoints()
    return Cyclopedia.Charms.points or Cyclopedia.StoredBestiaryCharmPoints or 0
end

local function getCharmEffect(charm)
    return charm and (CHARM_EFFECTS[charm.id] or charm.effect) or ""
end

local function findCharmById(id)
    for _, charm in ipairs(Cyclopedia.Charms.data or {}) do
        if charm.id == id then
            return charm
        end
    end
    return nil
end

local function centerCharmGrid()
    local charmList = UI_REFS and UI_REFS.charmList
    if not charmList then
        return
    end

    local itemCount = #(Cyclopedia.Charms.data or {})
    if itemCount <= 0 then
        charmList:setPaddingLeft(8)
        return
    end

    local areaWidth = charmList:getWidth()
    local cellWidth = 120
    local cellSpacing = 6
    local maxColumns = math.max(1, math.floor((areaWidth + cellSpacing) / (cellWidth + cellSpacing)))
    local visibleColumns = math.min(itemCount, maxColumns)
    local usedWidth = visibleColumns * cellWidth + math.max(0, visibleColumns - 1) * cellSpacing
    charmList:setPaddingLeft(math.max(8, math.floor((areaWidth - usedWidth) / 2)))
end

local function updateGlobalCharmPoints()
    if controllerCyclopedia and controllerCyclopedia.ui and controllerCyclopedia.ui.GoldBase then
        controllerCyclopedia.ui.GoldBase:setVisible(true)
        controllerCyclopedia.ui.GoldBase.Value:setText(tostring(getCharmPoints()))
    end
end

local function updateUnlockButton(charm)
    local button = UI_REFS.unlockButton
    if not charm then
        button:setText("Unlock")
        button:setEnabled(false)
    elseif charm.active then
        button:setText("Active")
        button:setEnabled(false)
    elseif charm.unlocked then
        button:setText("Activate at NPC")
        button:setEnabled(false)
    else
        button:setText("Unlock")
        button:setEnabled(getCharmPoints() >= charm.cost)
    end
end

local function updateSelectedCharmPanel(charm)
    if not charm then
        return
    end

    UI_REFS.selectedName:setText(charm.name)
    UI_REFS.effectValue:setText(getCharmEffect(charm))
    UI_REFS.chanceValue:setText(string.format("%d%%", charm.chance))
    UI_REFS.aoeValue:setText(charm.aoeReduction >= 0 and string.format("%d%%", charm.aoeReduction) or "-")
    UI_REFS.unlockCostValue:setText(tostring(charm.cost))
    UI_REFS.descriptionText:setText(charm.description)
    UI_REFS.selectedIcon:setImageSource("/game_cyclopedia/images/charms/monster-bonus-effects")
    UI_REFS.selectedIcon:setImageClip(string.format("%d 0 32 32", charm.iconIndex * 32))
    UI_REFS.selectedLockedMask:setVisible(not charm.unlocked)

    if charm.active then
        UI_REFS.statusValue:setText("Active")
        UI_REFS.statusValue:setColor("#74C365")
    elseif charm.unlocked then
        UI_REFS.statusValue:setText("Inactive")
        UI_REFS.statusValue:setColor("#D6B85C")
    else
        UI_REFS.statusValue:setText("Locked")
        UI_REFS.statusValue:setColor("#C65C5C")
    end

    updateUnlockButton(charm)
end

local function refreshCharmCard(widget, charm)
    local name = requireWidget(widget, "CardName")
    local image = requireWidget(widget, "CardIcon")
    local costValue = requireWidget(widget, "CardCostValue")
    local lockedMask = requireWidget(widget, "CardLockedMask")
    local statusBar = requireWidget(widget, "CardStatusBar")
    local statusLabel = requireWidget(widget, "CardStatusLabel")

    name:setText(charm.name)
    image:setImageSource("/game_cyclopedia/images/charms/monster-bonus-effects")
    image:setImageClip(string.format("%d 0 32 32", charm.iconIndex * 32))
    costValue:setText(tostring(charm.cost))
    lockedMask:setVisible(not charm.unlocked)

    if charm.active then
        statusBar:setBackgroundColor("#385f32")
        statusLabel:setText("Active")
        statusLabel:setColor("#C9F0C2")
    elseif charm.unlocked then
        statusBar:setBackgroundColor("#66552f")
        statusLabel:setText("Inactive")
        statusLabel:setColor("#F0DFA8")
    else
        statusBar:setBackgroundColor("#5d3434")
        statusLabel:setText("Locked")
        statusLabel:setColor("#F0C2C2")
    end

    if charm.unlocked or getCharmPoints() >= charm.cost then
        costValue:setColor("#D7D7D7")
    else
        costValue:setColor("#D33C3C")
    end
end

local function rebuildCharmCards()
    local charmList = UI_REFS.charmList
    charmList:destroyChildren()

    for _, charm in ipairs(Cyclopedia.Charms.data or {}) do
        local widget = g_ui.createWidget("CharmMockCard", charmList)
        widget.data = charm
        refreshCharmCard(widget, charm)
    end

    centerCharmGrid()

    local selectedId = Cyclopedia.Charms.selectedId
    if not findCharmById(selectedId) then
        selectedId = Cyclopedia.Charms.data[1] and Cyclopedia.Charms.data[1].id
    end

    for _, child in ipairs(charmList:getChildren()) do
        if child.data and child.data.id == selectedId then
            child:setChecked(true)
            Cyclopedia.selectCharm(child, true)
            break
        end
    end
end

local function normalizeCharmData(charm)
    local state = tonumber(charm.state) or 0
    local unlocked = state >= 1 or charm.unlocked == true or charm.unlocked == 1

    return {
        id = charm.id,
        iconIndex = charm.iconIndex or 0,
        name = charm.name or "Charm",
        effect = charm.description or "",
        description = charm.description or "",
        chance = charm.chance or 0,
        aoeReduction = charm.aoeReduction or -1,
        cost = charm.unlockPrice or 0,
        unlocked = unlocked,
        active = state == 2 or charm.asignedStatus == true,
        state = state
    }
end

function showCharms()
    UI = g_ui.loadUI("charms", contentContainer)
    UI:show()
    bindUI()

    if controllerCyclopedia and controllerCyclopedia.ui then
        if controllerCyclopedia.ui.CharmsBase then
            controllerCyclopedia.ui.CharmsBase:setVisible(false)
        end
        if controllerCyclopedia.ui.CharmsBase1410 then
            controllerCyclopedia.ui.CharmsBase1410:setVisible(false)
        end
        if controllerCyclopedia.ui.BestiaryTrackerButton then
            controllerCyclopedia.ui.BestiaryTrackerButton:setVisible(false)
        end
    end

    g_game.requestBestiary()
    updateGlobalCharmPoints()
end

function onTerminateCharm()
    if unlockConfirmWindow then
        unlockConfirmWindow:destroy()
        unlockConfirmWindow = nil
    end

    UI_REFS = nil
    UI = nil
end

function Cyclopedia.loadCharms(charmsData)
    if not charmsData then
        return
    end

    Cyclopedia.Charms.points = charmsData.points or 0
    Cyclopedia.StoredBestiaryCharmPoints = Cyclopedia.Charms.points
    Cyclopedia.Charms.data = {}

    for _, charm in ipairs(charmsData.charms or {}) do
        table.insert(Cyclopedia.Charms.data, normalizeCharmData(charm))
    end

    updateGlobalCharmPoints()
    if UI_REFS then
        rebuildCharmCards()
    end
end

function Cyclopedia.selectCharm(widget, isChecked)
    if not UI_REFS or not widget or not widget.data then
        return
    end

    for _, child in ipairs(UI_REFS.charmList:getChildren()) do
        if child ~= widget and child:isChecked() then
            child:setChecked(false)
        end
    end

    if not isChecked then
        widget:setChecked(true)
    end

    Cyclopedia.Charms.selectedId = widget.data.id
    updateSelectedCharmPanel(widget.data)
end

function Cyclopedia.actionCharmButton()
    local charm = findCharmById(Cyclopedia.Charms.selectedId)
    if not charm or charm.unlocked or getCharmPoints() < charm.cost then
        updateUnlockButton(charm)
        return
    end

    local function closeConfirmWindow()
        if unlockConfirmWindow then
            unlockConfirmWindow:destroy()
            unlockConfirmWindow = nil
        end
    end

    local function confirmUnlock()
        closeConfirmWindow()
        UI_REFS.unlockButton:setEnabled(false)
        UI_REFS.unlockButton:setText("Unlocking...")
        g_game.BuyCharmRune(charm.id, 0, 0)
    end

    if unlockConfirmWindow then
        unlockConfirmWindow:destroy()
        unlockConfirmWindow = nil
    end

    unlockConfirmWindow = displayGeneralBox(
        "Unlock " .. charm.name,
        "Unlocking a charm DOES NOT activate it.\n\nAfter unlocking, you must complete a special quest before this charm can be activated.\n\nTHIS ACTION CANNOT BE UNDONE.\n\nDo you want to spend your Charm Points and unlock " .. charm.name .. "?",
        {
            { text = tr("Yes"), callback = confirmUnlock },
            { text = tr("No"), callback = closeConfirmWindow }
        },
        confirmUnlock,
        closeConfirmWindow
    )
end

function Cyclopedia.actionSelectCharmButton() end
function Cyclopedia.clearCharm() end
function Cyclopedia.resetAllCharms() end
function Cyclopedia.onSearchTextChange() end
function Cyclopedia.searchCharmMonster() end
function Cyclopedia.selectCreatureCharm() end
