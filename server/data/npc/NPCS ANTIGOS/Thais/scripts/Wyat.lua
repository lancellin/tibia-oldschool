local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid)              npcHandler:onCreatureAppear(cid)            end
function onCreatureDisappear(cid)           npcHandler:onCreatureDisappear(cid)         end
function onCreatureSay(cid, type, msg)      npcHandler:onCreatureSay(cid, type, msg)    end
function onThink()                          npcHandler:onThink()                        end

keywordHandler:addKeyword({'job'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'I am the sheriff of the Thaian territory.'
})
keywordHandler:addKeyword({'sheriff'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'I am the sheriff of the Thaian territory.'
})
keywordHandler:addKeyword({'god'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'I am follower of Banor.'
})
keywordHandler:addKeyword({'banor'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'He is the patron of justice and bravery.'
})
keywordHandler:addKeyword({'king'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'HAIL TO KING TIBIANUS!'
})
keywordHandler:addKeyword({'mission'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Look up our 'Tibia's most wanted' lists."
})
keywordHandler:addKeyword({'quest'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Look up our 'Tibia's most wanted' lists."
})
keywordHandler:addKeyword({'apon'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Sam, the Thaian smith, is a man of great diligence. Whenever in need of weapons or armor, just ask him.'
})
keywordHandler:addKeyword({'sam'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Sam, the Thaian smith, is a man of great diligence. Whenever in need of weapons or armor, just ask him.'
})
keywordHandler:addKeyword({'castle'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The castle should be relatively safe from criminal transgressions.'
})
keywordHandler:addKeyword({'criminal'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Our enemies are numerous and not all are obvious.'
})
keywordHandler:addKeyword({'city'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The city is not as bad as some people might claim, but we certainly have our problems here.'
})
keywordHandler:addKeyword({'problems'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'We will handle each problem with care.'
})
keywordHandler:addKeyword({'benjamin'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "The poor fool lost his mind some years ago. It's a good thing they gave him a job in the post office."
})
keywordHandler:addKeyword({'bozo'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "He's so funny, I could listen to his jokes for hours."
})
keywordHandler:addKeyword({'elane'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'A woman of great skill and courage. No one deserves the title of a Grandmaster of the Paladins more then her.'
})
keywordHandler:addKeyword({'gorn'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'He was a rowdy in his youth, but now he\'s a fine citizen as far as I can tell.'
})

npcHandler:addModule(FocusModule:new())
