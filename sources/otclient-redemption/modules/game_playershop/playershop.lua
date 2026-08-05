local shopWindow
local priceWindow
local buyConfirmWindow
local shopContractButton
local lastShopContractRequestAt = 0
local mode = 'buyer'
local sellerId = 0
local selectedOffer
local selectedAmount = 1
local activeLabels = {}
local activeNameColors = {}
local shopDescription = 'description'
local visibleSlotOffset = 0
local currentOffers = {}
local maxShopSlots = 20
local visibleShopSlots = 18
local shopSlotsPerRow = 9
local defaultShopDescription = 'description'
local shopLabelMaxWidth = 210
local shopLabelMinWidth = 42
local shopLabelPadding = 6
local shopLabelLineHeight = 14
local shopLabelSingleLineHeight = 18
local shopLabelMaxHeight = shopLabelLineHeight * 2 + 4
local shopLabelVerticalOffset = 28
local shopContractCommand = '!shopcontract'
local shopContractCooldown = 1000

local function splitShopLabelText(text)
  text = tostring(text or defaultShopDescription)
  if #text <= 20 then
    return text, false
  end

  local middle = math.ceil(#text / 2)
  local splitAt
  for i = middle, 1, -1 do
    if text:sub(i, i) == ' ' then
      splitAt = i
      break
    end
  end
  if not splitAt or splitAt < math.floor(#text / 3) then
    for i = middle + 1, #text do
      if text:sub(i, i) == ' ' then
        splitAt = i
        break
      end
    end
  end
  if not splitAt then
    splitAt = middle
  end

  local firstLine = text:sub(1, splitAt):gsub('%s+$', '')
  local secondLine = text:sub(splitAt + 1):gsub('^%s+', '')
  if secondLine == '' then
    return text, false
  end
  return firstLine .. '\n' .. secondLine, true
end

local function getShopLabelWordWidth(label, text)
  local maxWidth = shopLabelMinWidth
  for line in tostring(text or ''):gmatch('[^\n]+') do
    label:setText(line)
    label:resizeToText()
    maxWidth = math.max(maxWidth, label:getWidth() + shopLabelPadding)
  end
  return maxWidth
end

local function sanitizeShopDescription(text)
  text = tostring(text or '')
  text = text:gsub('[\r\n|]', ' ')
  if #text > 38 then
    text = text:sub(1, 38)
  end
  return text
end

local function normalizeIncomingShopDescription(text)
  local description = sanitizeShopDescription(text)
  if description == '' or description == 'Shop' then
    return defaultShopDescription
  end
  return description
end

local function getLocalPlayerFloor()
  local player = g_game.getLocalPlayer()
  local position = player and player:getPosition()
  return position and position.z
end

local function getCreatureFloor(creature)
  local position = creature and creature:getPosition()
  return position and position.z
end

local function samePosition(a, b)
  return a and b and a.x == b.x and a.y == b.y and a.z == b.z
end

local function copyPosition(position)
  if not position then
    return nil
  end
  return { x = position.x, y = position.y, z = position.z }
end

local function protocol()
  return g_game.getProtocolGame()
end

local function requestShopContract()
  if not g_game.isOnline() then
    return
  end

  local now = g_clock.millis()
  if now - lastShopContractRequestAt < shopContractCooldown then
    if shopContractButton then
      shopContractButton:setOn(false)
    end
    return
  end

  lastShopContractRequestAt = now

  g_game.talk(shopContractCommand)

  if shopContractButton then
    shopContractButton:setOn(false)
  end
end

local function send(buffer)
  local protocolGame = protocol()
  if protocolGame then
    protocolGame:sendExtendedOpcode(ExtendedIds.PlayerShop, buffer)
  end
end

local function clearWindow()
  if shopWindow then
    shopWindow:destroy()
    shopWindow = nil
  end
  if buyConfirmWindow then
    buyConfirmWindow:destroy()
    buyConfirmWindow = nil
  end
  selectedOffer = nil
end

local function clearPriceWindow()
  if priceWindow then
    priceWindow:destroy()
    priceWindow = nil
  end
end

local function formatGold(value)
  return tostring(math.max(0, tonumber(value) or 0))
end

local function updateDescriptionText(widget, text)
  widget:setText(text or '')

  local lineCount = 1
  for _ in tostring(text or ''):gmatch('\n') do
    lineCount = lineCount + 1
  end

  local wrappedLines = math.ceil(#tostring(text or '') / 32)
  widget:setHeight(math.max(80, math.max(lineCount, wrappedLines) * 14 + 8))
end

local function shopChild(id)
  return shopWindow and shopWindow:recursiveGetChildById(id)
end

local function getDisplayOffers()
  return currentOffers or {}
end

local function getOfferUnitName(offer)
  local name = tostring(offer and offer.name or 'item')
  if offer and offer.stackable then
    name = name:gsub('^%d+%s+', '')
  end
  if name == '' then
    return 'item'
  end
  return name
end

local function updateSelectedOfferTexts(offer, amount)
  local selectedName = shopChild('selectedName')
  local selectedStats = shopChild('selectedStats')
  local selectedDescription = shopChild('selectedDescription')
  if not selectedName or not selectedStats or not selectedDescription or not offer then
    return
  end

  local available = math.max(1, offer.count or 1)
  local weight = (offer.weight or 0) / 100
  local name = offer.name or 'Item'
  local description = offer.look or name

  if offer.stackable then
    local unitName = getOfferUnitName(offer)
    local selectedWeight = ((offer.weight or 0) * amount / available) / 100
    name = string.format('%d %s', amount, unitName)
    description = string.format('%d %s.\nThey weigh %.2f oz.', amount, unitName, selectedWeight)
    weight = selectedWeight
  end

  selectedName:setText(name)
  selectedStats:setText(string.format('Available: %dx\nPrice: %d gp each\nWeight: %.2f oz',
    offer.count or 1, offer.price or 0, weight))
  updateDescriptionText(selectedDescription, description)
end

local function setSelected(offer)
  selectedOffer = offer
  if not shopWindow then
    return
  end

  local selectedItem = shopChild('selectedItem')
  local selectedName = shopChild('selectedName')
  local selectedStats = shopChild('selectedStats')
  local selectedDescription = shopChild('selectedDescription')
  local amountLabel = shopChild('amountLabel')
  local amountScrollBar = shopChild('amountScrollBar')
  if not selectedItem or not selectedName or not selectedStats or not selectedDescription or not amountLabel or not amountScrollBar then
    g_logger.error('[PlayerShop] missing detail widgets in PlayerShopWindow')
    return
  end

  if not offer then
    selectedAmount = 1
    selectedItem:setItemId(0)
    selectedName:setText('Select an item')
    selectedStats:setText('')
    updateDescriptionText(selectedDescription, '')
    amountLabel:setText('Amount: 0x')
    amountScrollBar:setMinimum(1)
    amountScrollBar:setMaximum(1)
    amountScrollBar:setValue(1)
    amountScrollBar:disable()
    return
  end

  local maxAmount = offer.stackable and math.max(1, offer.count or 1) or 1
  selectedAmount = 1

  selectedItem:setItemId(offer.itemId)
  selectedItem:setItemCount(1)
  amountLabel:setText('Amount: 1x')
  amountScrollBar:setMinimum(1)
  amountScrollBar:setMaximum(maxAmount)
  amountScrollBar:setValue(1)
  amountScrollBar:setEnabled(mode == 'buyer' and maxAmount > 1)
  onAmountChange(1)
end

local function readNumber(widget, fallback)
  local value = tonumber(widget:getText())
  if not value then
    return fallback
  end
  return math.floor(value)
end

local function openPriceDialog(slotIndex, item)
  clearPriceWindow()
  priceWindow = g_ui.createWidget('PlayerShopPriceWindow', rootWidget)
  priceWindow:raise()
  priceWindow:focus()

  local countEdit = priceWindow:getChildById('countEdit')
  countEdit:setText(tostring(math.max(1, item:getCount())))

  local okButton = priceWindow:getChildById('okButton')
  okButton.onClick = function()
    local price = readNumber(priceWindow:getChildById('priceEdit'), 0)
    local count = readNumber(countEdit, 1)
    if price <= 0 then
      displayErrorBox(tr('Player Shop'), tr('Invalid price.'))
      return
    end
    if count <= 0 then
      displayErrorBox(tr('Player Shop'), tr('Invalid amount.'))
      return
    end

    local pos = item:getPosition()
    if not pos then
      displayErrorBox(tr('Player Shop'), tr('Invalid item.'))
      return
    end

    send(string.format('add|%d|%d|%d|%d|%d|%d|%d|%d',
      slotIndex, pos.x, pos.y, pos.z, item:getStackPos(), item:getId(), count, price))
    clearPriceWindow()
  end

  priceWindow:getChildById('cancelButton').onClick = clearPriceWindow
end

local function sendBuy(offer, count)
  send(string.format('buy|%d|%d|%d', sellerId, offer.slot or 0, count))
end

local function getSelectedBuyAmount()
  if not selectedOffer then
    return 1
  end

  if not selectedOffer.stackable then
    return 1
  end

  local maxAmount = math.max(1, selectedOffer.count or 1)
  local amount = selectedAmount
  local amountScrollBar = shopChild('amountScrollBar')
  if amountScrollBar then
    amount = tonumber(amountScrollBar:getValue()) or amount
  end

  amount = math.floor(tonumber(amount) or 1)
  amount = math.max(1, math.min(amount, maxAmount))
  selectedAmount = amount
  onAmountChange(amount)
  return amount
end

local function buildSlots()
  local slotsPanel = shopChild('slotsPanel')
  slotsPanel:destroyChildren()

  for i = 0, visibleShopSlots - 1 do
    local slot = g_ui.createWidget('PlayerShopSlot', slotsPanel)
    slot.shopSlot = i
    slot.onClick = function(widget)
      setSelected(widget.offer)
    end
    slot.onDrop = function(widget, draggedWidget)
      if mode ~= 'config' then
        return false
      end
      local item = draggedWidget and draggedWidget.currentDragThing
      if not item or not item:isItem() then
        return false
      end
      openPriceDialog(widget.shopSlot, item)
      return true
    end
  end
end

local function offerBySlot(slotIndex)
  for _, offer in ipairs(currentOffers or {}) do
    if offer.slot == slotIndex then
      return offer
    end
  end
  return nil
end

local function applyOffers(offers)
  if not shopWindow then
    return
  end

  currentOffers = offers or {}
  local slotsPanel = shopChild('slotsPanel')
  if not slotsPanel then
    return
  end

  local compactList = mode ~= 'config'
  local displayOffers = getDisplayOffers()
  local slots = slotsPanel:getChildren()
  for index, slot in ipairs(slots) do
    local realSlot = visibleSlotOffset + index - 1
    local offer = compactList and displayOffers[visibleSlotOffset + index] or offerBySlot(realSlot)
    slot.shopSlot = offer and offer.slot or realSlot
    slot.offer = nil
    slot:setItemId(0)
    slot:setTooltip('')
    slot:setVisible(mode == 'config' or offer ~= nil)
    if offer then
      slot.offer = offer
      slot:setItemId(offer.itemId)
      slot:setItemCount(offer.count or 1)
      slot:setTooltip(string.format('%s\n%d gp each', offer.name or 'Item', offer.price or 0))
    end
  end

  if selectedOffer then
    local stillExists = false
    for _, offer in ipairs(offers or {}) do
      if offer.slot == selectedOffer.slot then
        selectedOffer = offer
        stillExists = true
        break
      end
    end
    if not stillExists then
      selectedOffer = nil
    end
  end
  setSelected(selectedOffer)
end

local function refreshSlotScrollBar()
  local slotsScrollBar = shopChild('slotsScrollBar')
  if not slotsScrollBar then
    return
  end

  local displayCount = maxShopSlots
  if mode ~= 'config' then
    displayCount = #getDisplayOffers()
  end

  local visibleRows = math.ceil(visibleShopSlots / shopSlotsPerRow)
  local totalRows = math.ceil(math.max(displayCount, 1) / shopSlotsPerRow)
  local maxRow = math.max(0, totalRows - visibleRows)
  local currentRow = math.min(math.floor(visibleSlotOffset / shopSlotsPerRow), maxRow)
  visibleSlotOffset = currentRow * shopSlotsPerRow

  slotsScrollBar:setMinimum(0)
  slotsScrollBar:setMaximum(maxRow)
  slotsScrollBar:setValue(currentRow)
  slotsScrollBar:setEnabled(maxRow > 0)
  slotsScrollBar.onValueChange = function(_, value)
    visibleSlotOffset = math.max(0, tonumber(value) or 0) * shopSlotsPerRow
    applyOffers(currentOffers)
  end
end

local function showWindow(data)
  local previousDescription = nil
  if shopWindow and mode == 'config' then
    local descriptionEdit = shopChild('descriptionEdit')
    previousDescription = sanitizeShopDescription(descriptionEdit and descriptionEdit:getText() or shopDescription)
  end

  clearWindow()
  clearPriceWindow()

  mode = data.mode or 'buyer'
  sellerId = data.sellerId or 0
  local incomingDescription = normalizeIncomingShopDescription(data.description)
  if mode == 'config' and previousDescription and (incomingDescription == defaultShopDescription) then
    shopDescription = previousDescription
  else
    shopDescription = incomingDescription
  end
  visibleSlotOffset = 0
  currentOffers = {}
  shopWindow = g_ui.createWidget('PlayerShopWindow', rootWidget)
  shopWindow:setText((data.sellerName or 'Player') .. "'s Shop")
  shopWindow:raise()
  shopWindow:focus()
  buildSlots()
  refreshSlotScrollBar()

  local descriptionEdit = shopChild('descriptionEdit')
  if descriptionEdit then
    descriptionEdit:setText(shopDescription)
    descriptionEdit:setMaxLength(38)
    descriptionEdit:setEditable(mode == 'config')
    descriptionEdit:setFocusable(mode == 'config')
    descriptionEdit.onTextChange = nil
    if mode == 'config' then
      descriptionEdit.onTextChange = function(widget, text)
        local sanitized = sanitizeShopDescription(text)
        if sanitized ~= text then
          widget:setText(sanitized)
          return
        end
        shopDescription = sanitized
      end
    end
  end

  local goldPanel = shopChild('goldPanel')
  local goldLabel = shopChild('goldLabel')
  if goldPanel then
    goldPanel:setVisible(mode == 'buyer')
  end
  if goldLabel then
    goldLabel:setText(formatGold(data.buyerMoney))
  end

  local amountScrollBar = shopChild('amountScrollBar')
  amountScrollBar.onValueChange = function(_, value)
    onAmountChange(value)
  end

  shopChild('buyButton'):setVisible(mode == 'buyer')
  shopChild('confirmButton'):setVisible(mode == 'config')
  shopChild('cancelButton'):setText(mode == 'seller' and tr('Cancel store') or tr(mode == 'buyer' and 'Close' or 'Cancel'))

  if mode == 'buyer' then
    shopChild('cancelButton').onClick = function()
      closeWindow()
    end
  else
    shopChild('cancelButton').onClick = function()
      cancelStore()
    end
  end

  applyOffers(data.offers)
end

local function detachLabel(creatureId)
  local creature = g_map.getCreatureById(creatureId)
  if creature then
    if activeNameColors[creatureId] and creature.setInformationColor then
      creature:setInformationColor(activeNameColors[creatureId])
    end
    if creature.detachWidgetById then
      creature:detachWidgetById('playerShopLabel')
    end
  end
  activeNameColors[creatureId] = nil
end

local function removeLabel(creatureId)
  activeLabels[creatureId] = nil
  detachLabel(creatureId)
end

local function applyLabel(creatureId, text)
  local labelText = tostring(text or defaultShopDescription)
  local creature = g_map.getCreatureById(creatureId)
  local creatureFloor = getCreatureFloor(creature)
  local creaturePosition = creature and creature:getPosition()
  activeLabels[creatureId] = {
    text = labelText,
    floor = creatureFloor,
    position = copyPosition(creaturePosition)
  }

  if not creature or not creature.attachWidget or creatureFloor ~= getLocalPlayerFloor() then
    detachLabel(creatureId)
    return
  end

  if creature.detachWidgetById then
    creature:detachWidgetById('playerShopLabel')
  end

  local label = g_ui.createWidget('PlayerShopCreatureLabel')
  local displayText, hasTwoLines = splitShopLabelText(labelText)
  label:setText(displayText)
  label:setTextWrap(false)
  label:resizeToText()

  local oneLineWidth = label:getWidth() + shopLabelPadding
  local longestLineWidth = getShopLabelWordWidth(label, displayText)
  local targetWidth = math.max(shopLabelMinWidth, math.min(shopLabelMaxWidth, longestLineWidth, oneLineWidth))

  label:setText(displayText)
  label:setWidth(targetWidth)
  label:setTextWrap(false)
  label:setHeight(hasTwoLines and shopLabelMaxHeight or shopLabelSingleLineHeight)
  label:setMarginLeft(-math.floor(label:getWidth() / 2))
  label:setMarginTop(-shopLabelVerticalOffset - math.floor(math.max(0, label:getHeight() - shopLabelSingleLineHeight) / 2))
  if creature.getInformationColor and not activeNameColors[creatureId] then
    activeNameColors[creatureId] = creature:getInformationColor()
  end
  if creature.setInformationColor then
    creature:setInformationColor('#ff66cc')
  end
  creature:attachWidget(label)
end

local function refreshLabelsForCurrentFloor()
  local playerFloor = getLocalPlayerFloor()
  for creatureId, labelData in pairs(activeLabels) do
    local creature = g_map.getCreatureById(creatureId)
    if creature and getCreatureFloor(creature) == playerFloor then
      applyLabel(creatureId, labelData.text)
    else
      detachLabel(creatureId)
    end
  end
end

local function onCreatureAppear(creature)
  if not creature then
    return
  end
  local labelData = activeLabels[creature:getId()]
  if labelData then
    if labelData.position and not samePosition(creature:getPosition(), labelData.position) then
      removeLabel(creature:getId())
      return
    end
    applyLabel(creature:getId(), labelData.text)
  end
end

local function onCreatureDisappear(creature)
  if not creature then
    return
  end

  if getCreatureFloor(creature) == getLocalPlayerFloor() then
    removeLabel(creature:getId())
  else
    detachLabel(creature:getId())
  end
end

local function onCreaturePositionChange(creature, newPos, oldPos)
  if not creature then
    return
  end

  if creature == g_game.getLocalPlayer() then
    if oldPos and newPos and oldPos.z ~= newPos.z then
      refreshLabelsForCurrentFloor()
    end
    return
  end

  local labelData = activeLabels[creature:getId()]
  if labelData then
    if labelData.position and newPos and not samePosition(newPos, labelData.position) then
      removeLabel(creature:getId())
      return
    end
    applyLabel(creature:getId(), labelData.text)
  end
end

local function onExtendedOpcode(protocolGame, opcode, buffer)
  local ok, data = pcall(json.decode, buffer)
  if not ok or not data then
    g_logger.error('[PlayerShop] invalid opcode payload: ' .. tostring(buffer))
    return
  end

  if data.action == 'window' then
    showWindow(data)
  elseif data.action == 'close' then
    if data.sellerId then
      removeLabel(data.sellerId)
    end
    if shopWindow and (sellerId == 0 or sellerId == data.sellerId) then
      clearWindow()
    end
  elseif data.action == 'popup' then
    displayErrorBox(tr('Player Shop'), tr(tostring(data.message or 'Invalid action.')))
  elseif data.action == 'label' then
    if not data.sellerId then
      return
    end
    if data.active then
      applyLabel(data.sellerId, tostring(data.text or 'Shop'))
    else
      removeLabel(data.sellerId)
    end
  end
end

local function onGameEnd()
  clearWindow()
  clearPriceWindow()
  for creatureId in pairs(activeLabels) do
    removeLabel(creatureId)
  end
  activeLabels = {}
  activeNameColors = {}
end

function init()
  g_ui.importStyle('playershop')
  ProtocolGame.registerExtendedOpcode(ExtendedIds.PlayerShop, onExtendedOpcode)
    shopContractButton = modules.game_mainpanel.addToggleButton('playerShopContractButton', tr('Player Shop Contract'),
      '/images/options/button_empty', requestShopContract, false, 6)
    if shopContractButton then
      shopContractButton:setText('')
      shopContractButton:setIcon('/images/options/button_shopcontract')
      shopContractButton:setOn(false)
    end
  connect(g_game, { onGameEnd = onGameEnd })
  connect(LocalPlayer, { onPositionChange = onCreaturePositionChange })
  connect(Creature, {
    onAppear = onCreatureAppear,
    onDisappear = onCreatureDisappear,
    onPositionChange = onCreaturePositionChange
  })
end

function terminate()
  onGameEnd()
  ProtocolGame.unregisterExtendedOpcode(ExtendedIds.PlayerShop)
  lastShopContractRequestAt = 0
  if shopContractButton then
    shopContractButton:destroy()
    shopContractButton = nil
  end
  disconnect(g_game, { onGameEnd = onGameEnd })
  disconnect(LocalPlayer, { onPositionChange = onCreaturePositionChange })
  disconnect(Creature, {
    onAppear = onCreatureAppear,
    onDisappear = onCreatureDisappear,
    onPositionChange = onCreaturePositionChange
  })
end

function closeWindow()
  clearWindow()
end

function cancelStore()
  send('cancel')
  clearWindow()
end

function confirmStore()
  if not shopWindow then
    return
  end
  local description = sanitizeShopDescription(shopDescription)
  send('confirm|' .. description)
end

function buySelected()
  if not selectedOffer then
    displayErrorBox(tr('Player Shop'), tr('Select an item first.'))
    return
  end

  if buyConfirmWindow then
    buyConfirmWindow:destroy()
    buyConfirmWindow = nil
  end

  local amount = getSelectedBuyAmount()
  local totalPrice = (selectedOffer.price or 0) * amount
  local itemName = getOfferUnitName(selectedOffer)
  local offerSlot = selectedOffer.slot

  local function closeConfirmWindow()
    if buyConfirmWindow then
      buyConfirmWindow:destroy()
      buyConfirmWindow = nil
    end
  end

  local function confirmBuy()
    closeConfirmWindow()
    if offerSlot then
      send(string.format('buy|%d|%d|%d', sellerId, offerSlot, amount))
    end
  end

  local message = string.format('Do you want to buy %dx %s for %d gp?', amount, itemName, totalPrice)
  buyConfirmWindow = displayGeneralBox(tr('Confirm purchase'), message, {
      { text = tr('No'), callback = closeConfirmWindow },
      { text = tr('Yes'), callback = confirmBuy }
    }, confirmBuy, closeConfirmWindow)
end

function onAmountChange(value)
  selectedAmount = math.max(1, tonumber(value) or 1)
  if selectedOffer then
    local maxAmount = selectedOffer.stackable and math.max(1, selectedOffer.count or 1) or 1
    selectedAmount = math.min(selectedAmount, maxAmount)
  end
  if shopWindow then
    local amountLabel = shopChild('amountLabel')
    if amountLabel then
      amountLabel:setText(string.format('Amount: %dx', selectedAmount))
    end
    local selectedItem = shopChild('selectedItem')
    if selectedItem and selectedOffer then
      selectedItem:setItemCount(selectedOffer.stackable and selectedAmount or 1)
    end
    if selectedOffer then
      updateSelectedOfferTexts(selectedOffer, selectedAmount)
    end
  end
end

function requestOpen(creature)
  if creature then
    send('open|' .. creature:getId())
  end
end

function canOpenStore(creature)
  return creature and activeLabels[creature:getId()] ~= nil and getCreatureFloor(creature) == getLocalPlayerFloor()
end
