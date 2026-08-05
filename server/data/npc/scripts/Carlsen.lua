dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Carlsen. That is enough."})
keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I study patterns, predict outcomes and occasionally help those who think before acting."})
keywordHandler:addKeyword({'trade'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I trade only with those who know the value of patience."})
keywordHandler:addKeyword({'mission'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Perhaps another time. I am considering several lines of play."})
keywordHandler:addKeyword({'game'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Life resembles a game more than people realize."})
keywordHandler:addKeyword({'chess'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "An elegant battlefield. No luck, only decisions."})
keywordHandler:addKeyword({'king'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Protect your king, but never become dependent on him."})
keywordHandler:addKeyword({'queen'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Power means little without proper positioning."})
keywordHandler:addKeyword({'rook'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Strength is most useful when it finally has room to move."})
keywordHandler:addKeyword({'knight'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The most interesting paths are rarely straight."})
keywordHandler:addKeyword({'bishop'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Some pieces see farther than others."})
keywordHandler:addKeyword({'pawn'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Never underestimate something that keeps moving forward."})
keywordHandler:addKeyword({'strategy'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "A good strategy wins before the first move."})
keywordHandler:addKeyword({'luck'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Luck is simply preparation meeting opportunity."})
keywordHandler:addKeyword({'checkmate'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "By the time people notice the checkmate, the game was decided long ago."})
keywordHandler:addKeyword({'time'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The clock defeats more players than their opponents."})
keywordHandler:addKeyword({'victory'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Winning is satisfying. Understanding why you won is even better."})
keywordHandler:addKeyword({'defeat'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "A loss teaches more than an effortless victory."})
keywordHandler:addKeyword({'help'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Slow down. Most mistakes begin with unnecessary haste."})
keywordHandler:addKeyword({'magnus'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "An interesting surname. I've heard it once or twice."})
keywordHandler:addKeyword({'stockfish'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "A formidable opponent. Surprisingly patient."})
keywordHandler:addKeyword({'engine'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Some calculate faster. That doesn't always mean they understand."})
keywordHandler:addKeyword({'elo'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Numbers measure results, not potential."})
keywordHandler:addKeyword({'sacrifice'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The best sacrifices are those your opponent accepts willingly."})
keywordHandler:addKeyword({'blunder'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Everyone blunders. The strongest recover immediately."})
keywordHandler:addKeyword({'draw'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Sometimes avoiding defeat is the greatest victory available."})
keywordHandler:addKeyword({'krikor'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "A strong player."})
keywordHandler:addKeyword({'hikaru'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Always dangerous."})

npcHandler:addModule(FocusModule:new())
