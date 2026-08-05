local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid)              npcHandler:onCreatureAppear(cid)            end
function onCreatureDisappear(cid)           npcHandler:onCreatureDisappear(cid)         end
function onThink()                          npcHandler:onThink()                        end

function onCreatureSay(cid, type, msg)
	if not npcHandler:isFocused(cid) and msgcontains(msg, 'hail general') then
		npcHandler:onCreatureSay(cid, type, 'hi')
		return
	end

	npcHandler:onCreatureSay(cid, type, msg)
end

keywordHandler:addKeyword({'enemy'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Evil has many faces. The servants of evil cannot always be recognized as easily as Ferumbras, for instance.'
})
keywordHandler:addKeyword({'god'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'I whorship Banor, the first warrior!'
})
keywordHandler:addKeyword({'job'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I am the general of the king's army."
})
keywordHandler:addKeyword({'king'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'HAIL TO KING TIBIANUS, OUR WISE LEADER!'
})
keywordHandler:addKeyword({'mission'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Sometimes the king calls for heroes. Keep eyes and ears open! I also heared Baxter has some work for young adventurers."
})
keywordHandler:addKeyword({'quest'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Sometimes the king calls for heroes. Keep eyes and ears open! I also heared Baxter has some work for young adventurers."
})
keywordHandler:addKeyword({'sell'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Are you suggesting I am corruptible?'
})
keywordHandler:addKeyword({'tbi'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "The Tibian Bureau of Investigation. Kind of secret police. I don't bother much about such things, I prefer my fights eye to eye."
})
keywordHandler:addKeyword({'weapon'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Sam is responsible to supply our troops with weapons and armor.'
})
keywordHandler:addKeyword({'castle'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The castle is prepared to withstand any direct assault.'
})
keywordHandler:addKeyword({'city'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The rapid growth of the city makes it hard to patrol and vulnerable to attacks.'
})
keywordHandler:addKeyword({'monsters'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'They seldom dare to attack the city itself.'
})
keywordHandler:addKeyword({'monster'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'They seldom dare to attack the city itself.'
})
keywordHandler:addKeyword({'excalibug'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'In the legends it is told, that this weapon made its wielder able to fight the mightiest demons hand to hand.'
})
keywordHandler:addKeyword({'ferumbras'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'He is allied with evil itself! Each time we kill him he returns to take revenge.'
})
keywordHandler:addKeyword({'banor'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'He is the idol of all knights and paladins.'
})
keywordHandler:addKeyword({'elane'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'AH! WHAT A WOMAN!'
})
keywordHandler:addKeyword({'benjamin'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "He was the king's general before I was promoted. Poor guy, lost his mind in a battle against the evil Ferumbras."
})
keywordHandler:addKeyword({'bozo'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'I hardly know him.'
})
keywordHandler:addKeyword({'chester'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I don't know much about him. He is a very secretive person."
})
keywordHandler:addKeyword({'gorn'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'He was an adventurer once. He was a fine fighter but lacked the discipline to serve in our army.'
})
keywordHandler:addKeyword({'harsky'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'He is one of our best men and serves in the silver guard.'
})
keywordHandler:addKeyword({'sam'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Sam is responsible to supply our troops with weapons and armor.'
})
keywordHandler:addKeyword({'army'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The army protects our city. I divided it into three battlegroups.'
})
keywordHandler:addKeyword({'battlegroup'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "The battlegroups are the 'dogs of war', the 'red guards', and the 'silver guards'."
})
keywordHandler:addKeyword({'dogs of war'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'They are our main army.'
})
keywordHandler:addKeyword({'red guards'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'They are our special forces. Some serve as city guards, others as secret police.'
})
keywordHandler:addKeyword({'silver guards'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The best sorcerers, paladins, knights, and druids of our forces are chosen to serve as silver guards. They are the bodyguards of the king.'
})

npcHandler:addModule(FocusModule:new())
