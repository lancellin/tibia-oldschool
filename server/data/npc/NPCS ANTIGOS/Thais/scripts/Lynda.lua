local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

local confessionState = {}

function onCreatureAppear(cid)              npcHandler:onCreatureAppear(cid)            end
function onCreatureDisappear(cid)
	confessionState[cid] = nil
	npcHandler:onCreatureDisappear(cid)
end
function onThink()                          npcHandler:onThink()                        end

function onCreatureSay(cid, type, msg)
	npcHandler:onCreatureSay(cid, type, msg)
end

local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	local message = msg:lower()

	if confessionState[cid] then
		if msgcontains(message, 'yes') then
			npcHandler:say('So tell me, what shadows your soul, my child? Do you want to confess your {sins}?', cid)
			confessionState[cid] = 'awaiting_confession'
			return true
		end

		if confessionState[cid] == 'awaiting_confession' then
			npcHandler:say('Meditate on that and pray for your soul.', cid)
			confessionState[cid] = nil
			return true
		end
	end

	if msgcontains(message, 'sins') then
		npcHandler:say('Do you wish to confess your {sins}, my child?', cid)
		confessionState[cid] = 'awaiting_confirmation'
		return true
	end

	return true
end

keywordHandler:addKeyword({'job'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'I am a priest of the great pantheon.'
})
keywordHandler:addKeyword({'king'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'King Tibianus is our benevolent sovereign.'
})
keywordHandler:addKeyword({'life'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Life is a gift of the gods, honor life and don't destroy it."
})
keywordHandler:addKeyword({'mission'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'It is my mission to spread knowledge about the gods.'
})
keywordHandler:addKeyword({'quest'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'It is my mission to spread knowledge about the gods.'
})
keywordHandler:addKeyword({'name'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'My name is Lynda. And the spirits tell me that you are |PLAYERNAME|.'
})
keywordHandler:addKeyword({'tibia'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The world of Tibia is the creation of the gods.'
})
keywordHandler:addKeyword({'monsters'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'They are creatures of the gods of evil!'
})
keywordHandler:addKeyword({'monster'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'They are creatures of the gods of evil!'
})
keywordHandler:addKeyword({'fire'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Fire is one of the primal elemental forces, sometimes worshipped by tribal shamans.'
})
keywordHandler:addKeyword({'air'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Air is one of the primal elemental forces, sometimes worshipped by tribal shamans.'
})
keywordHandler:addKeyword({'gods'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The {gods} of good guard us and guide us, the {gods} of evil want to destroy us and steal our souls!'
})
keywordHandler:addKeyword({'the gods of good'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The {gods} we call the good ones are {Fardos}, {Uman}, the {Elements}, {Suon}, {Crunor}, {Nornur}, {Bastesh}, {Kirok}, {Toth}, and {Banor}.'
})
keywordHandler:addKeyword({'fardos'}, StdModule.say, {
	npcHandler = npcHandler,
	text = '{Fardos} is the creator, the great obsever. He is our caretaker.'
})
keywordHandler:addKeyword({'uman'}, StdModule.say, {
	npcHandler = npcHandler,
	text = '{Uman} is the positive aspect of magic. He brings us the secrets of the arcane arts.'
})
keywordHandler:addKeyword({'suon'}, StdModule.say, {
	npcHandler = npcHandler,
	text = '{Suon} is the lifebringing sun. He observes the creation with love.'
})
keywordHandler:addKeyword({'crunor'}, StdModule.say, {
	npcHandler = npcHandler,
	text = '{Crunor}, the great tree, is the father of all plantlife. He is a prominent god for many druids.'
})
keywordHandler:addKeyword({'nornur'}, StdModule.say, {
	npcHandler = npcHandler,
	text = '{Nornur} is the mysterious god of fate. Who knows if he is its creator or just a chronist?'
})
keywordHandler:addKeyword({'bastesh'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Bastesh, the deep one, is the goddess of the sea and its creatures.'
})
keywordHandler:addKeyword({'kirok'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Kirok, the mad one, is the god of scientists and jesters.'
})
keywordHandler:addKeyword({'toth'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Toth, lord of death, is the keper of the souls, the guardian of the afterlife.'
})
keywordHandler:addKeyword({'banor'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Banor, the heavenly warrior, is the patron of all fighters against evil. He is the gift of the gods to inspire humanity.'
})
keywordHandler:addKeyword({'gods of evil'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The goods we call the evil ones are Zathroth, Fafnar, Brog, Urgith, and the Archdemons!'
})
keywordHandler:addKeyword({'zathroth'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Zathroth is the destructive aspect of magic. He is the deceiver and the thief of souls.'
})
keywordHandler:addKeyword({'fafnar'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Fafnar is the scorching sun. She observes the creation with hate and jealousy.'
})
keywordHandler:addKeyword({'brog'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'Brog, the raging one, is the great destroyer. The berserk of darkness.'
})
keywordHandler:addKeyword({'urgith'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The bonemaster Urgith is the lord of the undead and keeper of the damned souls.'
})
keywordHandler:addKeyword({'the archdemons'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'The demons are followers of Zathroth. The cruelest are known as the ruthless seven.'
})
keywordHandler:addKeyword({'ruthless seven'}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I dont want to talk about that subject!"
})
keywordHandler:addKeyword({'ferumbras'}, StdModule.say, {
	npcHandler = npcHandler,
	text = 'He is a favourite of the Gods of Evil and one of their Champions. He will have his come uppance one day.'
})

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
