-- @docclass
UIWindow = extends(UIWidget, 'UIWindow')

local function restoreGameFocusAfterWindowClose()
    addEvent(function()
        if not g_game.isOnline() or not rootWidget or not modules.game_interface then
            return
        end

        local gameRootPanel = modules.game_interface.getRootPanel and modules.game_interface.getRootPanel()
        local topMenu = modules.client_topmenu and modules.client_topmenu.getTopMenu and
                            modules.client_topmenu.getTopMenu()
        local focusedRootChild = rootWidget:getFocusedChild()

        -- Preserve the focus when another foreground interface is already active.
        if focusedRootChild and focusedRootChild ~= gameRootPanel and focusedRootChild ~= topMenu and
            focusedRootChild:isVisible() then
            return
        end

        -- If more than one window was open, move focus to the remaining topmost one.
        local foregroundWindow
        for _, child in ipairs(rootWidget:getChildren()) do
            if child ~= gameRootPanel and child ~= topMenu and child:isVisible() then
                local className = child:getClassName() or ''
                local styleName = child:getStyleName() or ''
                if className:find('Window') or styleName:find('Window') then
                    foregroundWindow = child
                end
            end
        end

        if foregroundWindow then
            foregroundWindow:raise()
            foregroundWindow:focus()
        elseif gameRootPanel and gameRootPanel:isVisible() then
            gameRootPanel:focus()
        end
    end)
end

function UIWindow.create()
    local window = UIWindow.internalCreate()
    window:setTextAlign(AlignTopCenter)
    window:setDraggable(true)
    window:setAutoFocusPolicy(AutoFocusFirst)
    window.hotkeyBlock = false
    return window
end

function UIWindow:onKeyPress(keyCode, keyboardModifiers)
    if keyboardModifiers == KeyboardNoModifier then
        if keyCode == KeyEnter then
            signalcall(self.onEnter, self)
        elseif keyCode == KeyEscape then
            signalcall(self.onEscape, self)
        end
    end
end

function UIWindow:onFocusChange(focused)
    if focused then
        self:raise()
    end
end

function UIWindow:onVisibilityChange(visible)
    if not visible then
        restoreGameFocusAfterWindowClose()
    end
end

function UIWindow:onDragEnter(mousePos)
    self:breakAnchors()
    self.movingReference = {
        x = mousePos.x - self:getX(),
        y = mousePos.y - self:getY()
    }
    return true
end

function UIWindow:onDragLeave(droppedWidget, mousePos)
    -- TODO: auto detect and reconnect anchors
end

function UIWindow:onDragMove(mousePos, mouseMoved)
    local pos = {
        x = mousePos.x - self.movingReference.x,
        y = mousePos.y - self.movingReference.y
    }
    self:setPosition(pos)
    self:bindRectToParent()
end

function UIWindow:onDestroy()
    if self.hotkeyBlock then
        self.hotkeyBlock.release()
        self.hotkeyBlock = false
    end
    restoreGameFocusAfterWindowClose()
end
