local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

-- OTServ event handling functions
function onCreatureAppear(cid)			npcHandler:onCreatureAppear(cid)			end
function onCreatureDisappear(cid)		npcHandler:onCreatureDisappear(cid)			end
function onCreatureSay(cid, type, msg)		npcHandler:onCreatureSay(cid, type, msg)		end
function onThink()				npcHandler:onThink()					end

local shopModule = ShopModule:new()
npcHandler:addModule(shopModule)

shopModule:addBuyableItem({'antidote', 'antidote rune'}, 2266, 40)

keywordHandler:addKeyword({'how are you'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Very well. Thank you."})
keywordHandler:addKeyword({'offer'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I only sell my antidote runes and I'll be happy to buy some blueberries from you."})
keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am a druid, bound to the spirit of nature. I'm selling antidote runes that help against poison. Oh, and I buy blueberries, of course."})
keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "My name is Lily."})
keywordHandler:addKeyword({'hyacinth'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Hyacinth lives in the forest. He's never in town so I don't know him very well."})
keywordHandler:addKeyword({'help'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I can sell you an antidote rune. It's against the poison of so many dangerous creatures."})
keywordHandler:addKeyword({'monster'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Many monsters are poisonous. Don't let them bite you or you will need one of my antidote runes."})
keywordHandler:addKeyword({'creature'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Many monsters are poisonous. Don't let them bite you or you will need one of my antidote runes."})
keywordHandler:addKeyword({'poison'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Many monsters are poisonous. Don't let them bite you or you will need one of my antidote runes."})
keywordHandler:addKeyword({'life fluid'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I'm sorry, but Hyacinth is the only one on Rookgaard who knows how to brew life fluids."})
keywordHandler:addKeyword({'time'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "It is about |TIME|."})

local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

if msgcontains(msg, 'blueberry') or msgcontains(msg, 'blueberries') then
npcHandler:say("Do you want to sell 5 blueberries for 1 gold?", cid)
	npcHandler.topic[cid] = 301
			
elseif npcHandler.topic[cid] == 301 and msgcontains(msg, 'yes') then
	if doPlayerRemoveItem(cid, 2677, 5) == true then
	doPlayerAddMoney(cid, 1)
	npcHandler.topic[cid] = 0
	else
	npcHandler:say("Oh, I'm sorry. I'm not buying less than 5 blueberries.", cid)
	end
elseif npcHandler.topic[cid] == 301 and msgcontains(msg, 'no') then
	npcHandler:say("As you wish.", cid)
	npcHandler.topic[cid] = 0
	
end		
    return true
end


npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
