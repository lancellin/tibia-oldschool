local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'I am the abbot of the White Raven Monastery on the Isle of the Kings.'})
keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'My name is Costello.'})
keywordHandler:addKeyword({'tibia'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'That is the name of our world and its major continent.'})
keywordHandler:addKeyword({'god'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'They created Tibia and all life on it.'})
keywordHandler:addKeyword({'life'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'On Tibia there are many forms of life. Plants, the citizens, and monsters.'})
keywordHandler:addKeyword({'plant'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'Just walk around, you will see grass, trees, and bushes.'})
keywordHandler:addKeyword({'white raven'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'The legends tell us of a white raven which lead the ship of the first monk of our order here. He discovered this isle and the caves beneath it.'})
keywordHandler:addKeyword({'caves'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'Anselm, the first of our order, discovered them while looking for a suitable burial place for his king.'})
keywordHandler:addKeyword({'anselm'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'He was a humble and pious man, and he was chosen by the royal family of Thais to find a resting place for their dead.'})
keywordHandler:addKeyword({'isle'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'We founded our monastery to guard the royal tombs and to gather wisdom and knowledge.'})
keywordHandler:addKeyword({'wisdom'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "You are allowed to enter the library upstairs. Stay there and don't go upstairs, because that area is reserved for members of our order."})
keywordHandler:addKeyword({'tibianus'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'One day every Tibianus ends up here.'})
keywordHandler:addKeyword({'king'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'The bygone leaders of the Thaian empire rest beneath this monastery in tombs and crypts.'})
keywordHandler:addKeyword({'passage'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "You should not be here at all and I won't allow anyone to transport you from or to this isle."})
keywordHandler:addKeyword({'ferumbras'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Don't mention this servant of evil here."})
keywordHandler:addKeyword({'excalibug'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'Sadly we have only little knowledge on this topic.'})
keywordHandler:addKeyword({'news'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'Sorry, we rarely hear anything new here.'})
keywordHandler:addKeyword({'monster'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'There are really too many of them in Tibia. But who are we to question the wisdom of the gods?'})

npcHandler:addModule(FocusModule:new())
