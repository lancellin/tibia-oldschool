local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

local session = {}

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid)
	session[cid] = nil
	npcHandler:onCreatureDisappear(cid)
end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

local ACCOUNT_MANAGER_NAME = "Account Manager"
local CHARACTER_MANAGER_PREFIX = "Character Manager "
local STARTER_TOWN_ID = 11
local STARTER_POSITION = {x = 32097, y = 32219, z = 7}
local FEMALE_LOOKTYPE = 136
local MALE_LOOKTYPE = 128

local STEP_PASSWORD = 1
local STEP_NAME = 2
local STEP_SEX = 3

local createCharacter

local function getSession(cid)
	if not session[cid] then
		session[cid] = {}
	end
	return session[cid]
end

local function resetSession(cid, keepAccount)
	local current = session[cid] or {}
	session[cid] = keepAccount and {
		accountId = current.accountId,
		accountNumber = current.accountNumber
	} or {}
end

local function trim(text)
	return text:gsub("^%s+", ""):gsub("%s+$", "")
end

local function normalizeCharacterName(text)
	local name = trim(text):gsub("%s+", " ")
	return titleCase(name:lower())
end

local function isValidCharacterName(name)
	if #name < 3 or #name > 29 then
		return false
	end

	if name:find("  ", 1, true) then
		return false
	end

	if not name:match("^[%a ]+$") then
		return false
	end

	return true
end

local function playerExists(name)
	local resultId = db.storeQuery("SELECT `id` FROM `players` WHERE `name` = " .. db.escapeString(name))
	if resultId == false then
		return false
	end

	result.free(resultId)
	return true
end

local function getCharacterManagerName(accountNumber)
	return CHARACTER_MANAGER_PREFIX .. accountNumber
end

local function createCharacterManager(accountId, accountNumber)
	local managerName = getCharacterManagerName(accountNumber)
	return createCharacter(managerName, accountId, 1)
end

local function createAccount(cid, password)
	local nextAccountNumber = 1
	local resultId = db.storeQuery("SELECT MAX(CAST(`name` AS UNSIGNED)) AS `max_number` FROM `accounts` WHERE `name` REGEXP '^[0-9]+$'")
	if resultId ~= false then
		nextAccountNumber = result.getNumber(resultId, "max_number") + 1
		result.free(resultId)
	end

	local accountNumber = tostring(nextAccountNumber)
	if not db.query("INSERT INTO `accounts` (`name`, `password`, `creation`) VALUES (" .. db.escapeString(accountNumber) .. ", SHA1(" .. db.escapeString(password) .. "), " .. os.time() .. ")") then
		return nil
	end

	local accountId = db.lastInsertId()
	if not createCharacterManager(accountId, accountNumber) then
		db.query("DELETE FROM `accounts` WHERE `id` = " .. accountId)
		return nil
	end

	return accountId, accountNumber
end

createCharacter = function(name, accountId, sex)
	local lookType = sex == 0 and FEMALE_LOOKTYPE or MALE_LOOKTYPE
	return db.query("INSERT INTO `players` (`name`, `account_id`, `sex`, `looktype`, `town_id`, `posx`, `posy`, `posz`) VALUES (" ..
		db.escapeString(name) .. ", " .. accountId .. ", " .. sex .. ", " .. lookType .. ", " .. STARTER_TOWN_ID .. ", " ..
		STARTER_POSITION.x .. ", " .. STARTER_POSITION.y .. ", " .. STARTER_POSITION.z .. ")")
end

local function isCharacterManagerName(name)
	return name:sub(1, #CHARACTER_MANAGER_PREFIX) == CHARACTER_MANAGER_PREFIX
end

local function ensureManager(cid)
	local playerName = getPlayerName(cid)
	if playerName == ACCOUNT_MANAGER_NAME or isCharacterManagerName(playerName) then
		return true
	end

	npcHandler:say("This service is only available through the Account Manager or your Character Manager.", cid)
	return false
end

local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	if not ensureManager(cid) then
		return true
	end

	local state = getSession(cid)
	local message = trim(msg)
	local lowerMessage = message:lower()
	local playerName = getPlayerName(cid)
	local isAccountManager = playerName == ACCOUNT_MANAGER_NAME
	local isCharacterManager = isCharacterManagerName(playerName)

	if isCharacterManager and not state.accountId then
		state.accountId = getPlayerAccountId(cid)
	end

	if lowerMessage == "help" then
		if isAccountManager then
			npcHandler:say("Say account to create a new test account. After that, say character to create a new character for that account.", cid)
		else
			npcHandler:say("Say character to create a new character on your account.", cid)
		end
		return true
	end

	if lowerMessage == "cancel" then
		resetSession(cid, true)
		npcHandler:say("Very well. Say account or character when you are ready again.", cid)
		return true
	end

	if state.step == STEP_PASSWORD then
		if message == "" then
			npcHandler:say("Tell me the password you want to use for the new account.", cid)
			return true
		end

		local accountId, accountNumber = createAccount(cid, message)
		if not accountId then
			resetSession(cid, false)
			npcHandler:say("I could not create the account right now. Please try again in a moment.", cid)
			return true
		end

		resetSession(cid, false)
		state = getSession(cid)
		state.accountId = accountId
		state.accountNumber = accountNumber
		npcHandler:say("Your account number is " .. accountNumber .. ". Write it down. A Character Manager named " .. getCharacterManagerName(accountNumber) .. " has been created on this account. If you also want a playable character right now, say character.", cid)
		return true
	end

	if state.step == STEP_NAME then
		local characterName = normalizeCharacterName(message)
		if not isValidCharacterName(characterName) then
			npcHandler:say("That name is not valid. Use only letters and single spaces, then tell me another name.", cid)
			return true
		end

		if playerExists(characterName) then
			npcHandler:say("That name already exists. Tell me another name.", cid)
			return true
		end

		state.characterName = characterName
		state.step = STEP_SEX
		npcHandler:say("Should " .. characterName .. " be male or female?", cid)
		return true
	end

	if state.step == STEP_SEX then
		local sex = nil
		if lowerMessage == "female" then
			sex = 0
		elseif lowerMessage == "male" then
			sex = 1
		end

		if sex == nil then
			npcHandler:say("Please answer with male or female.", cid)
			return true
		end

		if not createCharacter(state.characterName, state.accountId, sex) then
			resetSession(cid, true)
			npcHandler:say("I could not create the character right now. Please try again.", cid)
			return true
		end

		local characterName = state.characterName
		local accountNumber = state.accountNumber
		resetSession(cid, true)
		if accountNumber then
			npcHandler:say(characterName .. " has been created on account " .. accountNumber .. ". You can now log out and enter that account with the password you chose.", cid)
		else
			npcHandler:say(characterName .. " has been created on your account.", cid)
		end
		return true
	end

	if lowerMessage == "account" then
		if not isAccountManager then
			npcHandler:say("This Character Manager can only create characters for its own account.", cid)
			return true
		end

		resetSession(cid, true)
		getSession(cid).step = STEP_PASSWORD
		npcHandler:say("Tell me the password you want to use for the new account.", cid)
		return true
	end

	if lowerMessage == "character" then
		if not state.accountId then
			npcHandler:say("First create an account. Say account when you are ready.", cid)
			return true
		end

		state.step = STEP_NAME
		npcHandler:say("Tell me the name of the new character.", cid)
		return true
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
