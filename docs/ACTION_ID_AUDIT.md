# Auditoria de Action IDs do world.otbm

Gerado em: 2026-06-27 13:21:56

Escopo: `server/data/world/world.otbm`, `server/data/items/items.xml`, `server/data/actions/actions.xml` e scripts atuais em `server/data/actions/scripts`.

## Resumo Executivo

- Objetos com `actionid` no mapa: 1054
- Action IDs distintos no mapa: 216
- Action IDs registrados diretamente em `actions.xml`: 1 (2001)
- Action IDs presentes no mapa sem registro direto por `actionid`: 215
- Unique IDs com cara de quest/storage e tratamento incerto: 59

Leitura importante: ausencia de registro direto em `actions.xml` nao significa sempre bug. Portas sao tratadas pelo core antes do fallback de container, e muitos `actionid` de portas/chaves sao parametros, nao scripts Lua. O risco sobe quando ha `uniqueid` de faixa 10000-29999, objeto nao-porta, e nenhum script claro.

## Tabela Geral por Action ID

| Action ID | Qtde | Item IDs envolvidos | Unique IDs | Exemplos de posicao | Tipo | Cobertura actions.xml/core | Hipotese | Risco |
|---:|---:|---|---|---|---|---|---|---|
| 100 | 34 | 355:muddy floor (34) | - | 31979,31564,8; 32040,31636,8; 32002,31694,8; 32725,31588,13; 32724,31588,14; 32162,31921,13 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 101 | 68 | 1209:closed door (30), 1212:closed door (29), 1231:closed door (3), 1210:closed door (2), 1234:closed door (2), 1225:closed door (1), 3545:closed door (1) | - | 31943,31709,6; 32100,31569,9; 32100,31570,9; 32114,31569,9; 32114,31570,9; 32644,31615,6 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 102 | 6 | 1368:draw well (3), 1369:draw well (3) | - | 32353,32131,8; 32354,32131,8; 32507,32176,13; 32508,32176,13; 32171,32439,7; 32172,32439,7 | nao-container | itemid->other/teleport.lua | desconhecido | baixo |
| 103 | 12 | 2018:pool (5), 2016:pool (3), 2019:pool (2), 2017:pool (2) | - | 32511,32068,8; 32513,32066,8; 32517,32065,8; 32516,32069,8; 32517,32068,8; 32591,32210,9 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 231 | 1 | 1225:closed door (1) | - | 32569,32023,6 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 242 | 1 | 1223:closed door (1) | - | 32515,32248,8 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 244 | 1 | 1225:closed door (1) | - | 32567,32023,6 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 689 | 1 | 1397:mystic flame (1) | - | 32278,31903,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 690 | 2 | 426:stone tile (2) | - | 32215,31838,15; 32216,31838,15 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 691 | 1 | 1397:mystic flame (1) | - | 32215,31849,15 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 692 | 1 | 1397:mystic flame (1) | - | 32171,31853,15 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 693 | 1 | 425:stone tile (1) | - | 32243,31892,14 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 888 | 17 | 426:stone tile (17) | - | 32187,31936,14; 32187,31938,14; 32187,31939,14; 32188,31936,14; 32188,31939,14; 32189,31936,14 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 889 | 8 | 426:stone tile (8) | - | 32187,31937,14; 32188,31937,14; 32188,31938,14; 32189,31938,14; 32189,31939,14; 32191,31938,14 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 890 | 1 | 1397:mystic flame (1) | - | 32192,31938,14 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 987 | 1 | 1397:mystic flame (1) | - | 32311,31978,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 988 | 1 | 1387:magic forcefield (1) | - | 32805,31587,1 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 1020 | 1 | 1229:gate of expertise (1) | - | 32673,32100,8 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1025 | 2 | 1229:gate of expertise (2) | - | 32325,32389,9; 32804,31583,2 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1030 | 8 | 1227:gate of expertise (6), 1229:gate of expertise (2) | - | 32483,31630,10; 32479,31634,10; 32483,31635,10; 32486,31633,10; 32567,31969,3; 32424,32145,15 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1032 | 1 | 1229:gate of expertise (1) | - | 32510,31956,13 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1035 | 7 | 1229:gate of expertise (2), 1261:gate of expertise (2), 1259:gate of expertise (2), 1227:gate of expertise (1) | - | 32475,31946,13; 32448,32042,8; 32795,32327,10; 33102,32537,6; 33103,32537,6; 33050,32622,6 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1040 | 3 | 1229:gate of expertise (2), 1227:gate of expertise (1) | - | 32353,32073,11; 32544,32179,14; 32981,31760,9 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1045 | 1 | 1229:gate of expertise (1) | - | 32602,32207,10 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1050 | 3 | 1229:gate of expertise (3) | - | 32212,32435,10; 32874,31974,12; 32875,31974,12 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1060 | 5 | 1227:gate of expertise (4), 1229:gate of expertise (1) | - | 32483,31722,15; 32223,31869,14; 33085,31650,10; 33032,32398,11; 33037,32398,11 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1070 | 2 | 1229:gate of expertise (2) | - | 33190,31684,14; 33195,31684,14 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1075 | 10 | 1247:gate of expertise (8), 1245:gate of expertise (2) | - | 33083,32620,14; 33084,32620,14; 33192,32665,15; 33123,32762,14; 33257,32706,13; 33262,32706,13 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1080 | 2 | 1229:gate of expertise (2) | - | 33297,31670,14; 32834,32280,10 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1100 | 2 | 1229:gate of expertise (1), 1227:gate of expertise (1) | - | 33211,31638,13; 33214,31671,13 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 1335 | 1 | 1387:magic forcefield (1) | - | 32176,31869,15 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 1336 | 1 | 1387:magic forcefield (1) | - | 32177,31869,15 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 1337 | 1 | 1397:mystic flame (1) | - | 32250,31892,14 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 1999 | 1 | 3549:gate of expertise (1) | - | 32882,32527,11 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 2000 | 124 | 1740:chest (61), 1741:box (8), 3128:dead human (7), 1419:sarcophagus (7), 3103:a pile of bones (6), 1738:box (6), 1748:chest (6), 1747:chest (4), 1749:chest (4), 1718:bookcase (2), 1721:bookcase (2), 1717:drawers (2), +6 tipos | 10001, 10002, 10003, 10004, 10005, 10006, 10008, 10010, 10011, 10012, 10013, 10014, 10015, 10016, 10017, 10021, 10022, 10023, 10552, 20000, 20001, 20002, 20003, 20004, +94 | 32105,31567,9; 32109,31567,9; 32172,31602,10; 32201,31571,10; 32031,31686,8; 32423,31591,15 | misto (118 containers/6 nao-container) | itemid->quests/quests.lua | quest chest/container ou reward container com item filho | medio |
| 2001 | 20 | 2720:dead tree (6), 1742:wooden coffin (3), 2725:palm (2), 405:wooden floor (2), 4369 (2), 2103:honey flower (1), 1747:chest (1), 1290:stone (1), 385:small hole (1), 3061:dead human (1) | 10007, 10009, 10018, 10019, 10020, 10024, 20040, 20085, 20086, 20087, 20088, 20089, 20091, 20092, 20093, 20104, 20105, 20106 | 32497,31888,7; 31983,32193,5; 31984,32246,10; 32005,32139,3; 32084,32181,8; 32172,32169,7 | misto (4 containers/16 nao-container) | actionid->quests/quests.lua; itemid->quests/quests.lua | quest nao-container por tabela uniqueid->reward; alguns ainda sem recompensa configurada | critico |
| 2112 | 1 | 1387:magic forcefield (1) | - | 32815,31599,9 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 2451 | 1 | 1945:lever (1) | - | 32310,31975,13 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 2452 | 1 | 1945:lever (1) | - | 32310,31976,13 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 2453 | 1 | 1945:lever (1) | - | 32312,31975,13 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 2454 | 1 | 1945:lever (1) | - | 32312,31976,13 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 2455 | 1 | 1945:lever (1) | - | 32314,31975,13 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 2456 | 1 | 1945:lever (1) | - | 32314,31976,13 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 2551 | 1 | 1945:lever (1) | - | 32220,31843,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 2552 | 1 | 1945:lever (1) | - | 32220,31845,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 2553 | 1 | 1945:lever (1) | - | 32220,31844,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 2554 | 1 | 1945:lever (1) | - | 32220,31842,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 2555 | 1 | 1945:lever (1) | - | 32220,31846,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 3001 | 2 | 2088:silver key (1), 1212:closed door (1) | - | 32794,31572,7; 32814,31597,7 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 3002 | 2 | 2088:silver key (1), 1212:closed door (1) | - | 32800,31578,7; 32796,31597,7 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 3003 | 2 | 2088:silver key (1), 1212:closed door (1) | - | 32790,31593,7; 32811,31597,7 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 3004 | 2 | 2088:silver key (1), 1212:closed door (1) | - | 32807,31576,6; 32809,31593,6 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 3005 | 2 | 1212:closed door (1), 2088:silver key (1) | - | 32794,31582,6; 32792,31590,6 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 3006 | 2 | 2088:silver key (1), 1212:closed door (1) | - | 32801,31579,6; 32814,31595,6 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 3007 | 2 | 2088:silver key (1), 1212:closed door (1) | - | 32813,31576,5; 32797,31592,4 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 3008 | 2 | 2088:silver key (1), 1212:closed door (1) | - | 32800,31582,2; 32804,31585,1 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 3012 | 1 | 1212:closed door (1) | - | 32675,31671,10 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 3142 | 1 | 1209:closed door (1) | - | 32450,32044,8 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 3301 | 1 | 1212:closed door (1) | - | 32619,32241,8 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 3302 | 1 | 1212:closed door (1) | - | 32619,32240,8 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 3303 | 1 | 1212:closed door (1) | - | 32614,32175,9 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 3304 | 2 | 2091:golden key (1), 1212:closed door (1) | - | 32623,32187,9; 32620,32199,10 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 3350 | 1 | 1212:closed door (1) | - | 32178,31928,6 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 3600 | 2 | 1209:closed door (1), 2088:silver key (1) | - | 32506,32175,14; 32509,32181,13 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 3666 | 1 | 1212:closed door (1) | - | 32578,32197,15 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 3701 | 1 | 1209:closed door (1) | - | 32786,32328,7 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 3702 | 1 | 2090:crystal key (1) | - | 32778,32282,11 | chave | sem cobertura em actions.xml detectada | chave | medio |
| 3899 | 1 | 1212:closed door (1) | - | 32479,31903,4 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 3940 | 1 | 1212:closed door (1) | - | 32190,32432,8 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 3980 | 1 | 1212:closed door (1) | - | 32277,32420,10 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 4501 | 2 | 1209:closed door (1), 2089:copper key (1) | - | 32039,31603,10; 32172,31602,10 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 4502 | 3 | 1212:closed door (2), 2089:copper key (1) | - | 32107,31568,9; 32108,31568,9; 32201,31571,10 | misto (2 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 4503 | 2 | 1209:closed door (1), 2089:copper key (1) | - | 32035,31642,8; 32031,31686,8 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 4600 | 1 | 1209:closed door (1) | - | 32109,32172,8 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 4601 | 2 | 1212:closed door (1), 2089:copper key (1) | - | 32145,32100,11; 32150,32111,12 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 4603 | 2 | 2088:silver key (1), 1209:closed door (1) | - | 32176,32132,9; 32179,32149,10 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 5000 | 2 | 1209:closed door (1), 2088:silver key (1) | - | 32726,31976,6; 32726,31980,7 | misto (1 portas/1 outros) | sem cobertura em actions.xml detectada | chave | medio |
| 5002 | 1 | 1212:closed door (1) | - | 32817,31965,8 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 5010 | 1 | 1209:closed door (1) | - | 32824,31969,8 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 5999 | 1 | 1397:mystic flame (1) | - | 33093,32823,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6000 | 1 | 1397:mystic flame (1) | - | 33097,32816,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6001 | 236 | 421:sandstone floor (236) | - | 33198,32959,14; 33199,32959,14; 33200,32959,14; 33201,32959,14; 33202,32959,14; 33203,32959,14 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 6003 | 1 | 1397:mystic flame (1) | - | 33293,32742,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6004 | 1 | 1397:mystic flame (1) | - | 33298,32742,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6005 | 1 | 1397:mystic flame (1) | - | 33073,32590,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6006 | 1 | 1397:mystic flame (1) | - | 33080,32589,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6007 | 1 | 1397:mystic flame (1) | - | 33240,32856,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6008 | 1 | 1397:mystic flame (1) | - | 33246,32849,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6009 | 1 | 1397:mystic flame (1) | - | 33276,32553,14 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6010 | 1 | 1212:closed door (1) | - | 33211,31634,13 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 6011 | 1 | 1397:mystic flame (1) | - | 33135,32683,12 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6012 | 1 | 1397:mystic flame (1) | - | 33131,32683,12 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6013 | 1 | 1397:mystic flame (1) | - | 33162,32831,10 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6014 | 1 | 1397:mystic flame (1) | - | 33157,32832,10 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6015 | 1 | 416:stone tile (1) | - | 33362,32811,14 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 6016 | 1 | 1397:mystic flame (1) | - | 33234,32692,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6017 | 1 | 1397:mystic flame (1) | - | 33234,32688,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 6018 | 1 | 1397:mystic flame (1) | - | 33272,32553,14 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 11001 | 2 | 446:wooden floor (2) | - | 32091,32175,6; 32092,32175,6 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 11002 | 2 | 446:wooden floor (2) | - | 32057,32192,7; 32057,32193,7 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 11003 | 1 | 354:muddy floor (1) | - | 32066,32068,10 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 11004 | 1 | 353:dirt floor (1) | - | 32169,32157,8 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 11005 | 2 | 1945:lever (2) | - | 32098,32204,8; 32104,32204,8 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 11006 | 1 | 1945:lever (1) | - | 32148,32105,11 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 11007 | 1 | 1945:lever (1) | - | 32093,32174,8 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 11008 | 1 | 1945:lever (1) | - | 32088,32148,9 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 11009 | 1 | 1945:lever (1) | - | 32090,32148,9 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 11010 | 1 | 1945:lever (1) | - | 32092,32148,9 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 11011 | 1 | 1945:lever (1) | - | 32094,32148,9 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 11012 | 1 | 1945:lever (1) | - | 32182,32145,11 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 11013 | 1 | 1209:closed door (1) | - | 32177,32148,11 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 13000 | 1 | 1387:magic forcefield (1) | - | 33399,32802,14 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 13001 | 1 | 1387:magic forcefield (1) | - | 33179,32890,11 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 13002 | 1 | 416:stone tile (1) | - | 33198,32877,11 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 20001 | 1 | 1225:closed door (1) | - | 32223,31872,14 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 20002 | 1 | 1225:closed door (1) | - | 32223,31875,14 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 20003 | 1 | 1225:closed door (1) | - | 32223,31878,14 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 20004 | 1 | 1225:closed door (1) | - | 32223,31881,14 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 20005 | 1 | 1225:closed door (1) | - | 32223,31884,14 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 20006 | 1 | 1225:closed door (1) | - | 32223,31887,14 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 20007 | 1 | 1225:closed door (1) | - | 32223,31890,14 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 25001 | 8 | 231:sand (8) | - | 33133,32568,7; 33208,32591,7; 33161,32598,7; 33133,32640,7; 33233,32704,7; 33282,32743,7 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 25002 | 225 | 231:sand (225) | - | 33135,32567,7; 33127,32570,7; 33132,32570,7; 33130,32574,7; 33140,32567,7; 33138,32570,7 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 25003 | 15 | 1225:closed door (11), 1223:closed door (4) | - | 32703,31605,14; 32259,31948,14; 32448,31965,10; 32459,31965,10; 32453,31975,10; 32455,31975,10 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 25004 | 1 | 1945:lever (1) | - | 32180,31633,8 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 25005 | 1 | 1945:lever (1) | - | 33172,31896,8 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 25006 | 1 | 1945:lever (1) | - | 33290,31715,12 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 25007 | 1 | 1945:lever (1) | - | 32528,31724,10 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 25008 | 1 | 1945:lever (1) | - | 32488,31628,13 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 25009 | 1 | 1945:lever (1) | - | 32614,32173,9 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 25010 | 1 | 1946:lever (1) | - | 32594,32212,9 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 25011 | 1 | 1945:lever (1) | - | 32616,32222,10 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 25012 | 1 | 4857:wrinkled parchment (1) | - | 33063,31624,15 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 25013 | 2 | 426:stone tile (2) | - | 33190,31629,13; 33191,31629,13 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 25014 | 1 | 1945:lever (1) | - | 33330,31591,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 30021 | 1 | 1387:magic forcefield (1) | - | 32607,31682,7 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 30022 | 1 | 1387:magic forcefield (1) | - | 32642,31925,12 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 30023 | 1 | 1387:magic forcefield (1) | - | 32369,32246,6 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 30024 | 1 | 1387:magic forcefield (1) | - | 32951,32035,7 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 30025 | 1 | 1387:magic forcefield (1) | - | 32360,31784,8 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 30026 | 1 | 1387:magic forcefield (1) | - | 33195,32849,6 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 30027 | 1 | 1387:magic forcefield (1) | - | 33216,32455,2 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 30028 | 1 | 1387:magic forcefield (1) | - | 32595,32749,6 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 30029 | 1 | 1387:magic forcefield (1) | - | 33210,31804,8 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 50001 | 1 | 474:time tile (1) | - | 32791,32330,10 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 50002 | 1 | 1945:lever (1) | - | 32479,31905,6 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 50003 | 1 | 426:stone tile (1) | - | 32481,31905,7 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 50004 | 1 | 426:stone tile (1) | - | 32476,31900,5 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 50005 | 1 | 1945:lever (1) | - | 32481,31904,5 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 50006 | 1 | 354:muddy floor (1) | - | 32497,31889,7 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 50007 | 1 | 1945:lever (1) | - | 32479,31905,4 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 50008 | 1 | 1945:lever (1) | - | 32478,31904,3 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 50009 | 3 | 426:stone tile (3) | - | 32476,31903,1; 32477,31903,1; 32478,31903,1 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 50010 | 3 | 426:stone tile (3) | - | 32476,31902,1; 32479,31903,1; 32481,31902,1 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 50011 | 3 | 426:stone tile (3) | - | 32478,31902,1; 32479,31902,1; 32480,31902,1 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 50012 | 3 | 426:stone tile (3) | - | 32477,31902,1; 32480,31903,1; 32481,31903,1 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 50013 | 4 | 474:time tile (4) | - | 32486,31927,7; 32487,31927,7; 32486,31928,7; 32487,31928,7 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 50014 | 1 | 474:time tile (1) | - | 32566,31957,1 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 50015 | 1 | 2334:Santa's Mailbox (1) | - | 31948,31711,6 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 50016 | 1 | 1945:lever (1) | - | 32315,31910,12 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 50017 | 1 | 1945:lever (1) | - | 32212,31888,12 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 50018 | 1 | 2593:mailbox (1) | - | 32013,31562,4 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 50019 | 1 | 1945:lever (1) | - | 32266,31861,11 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 50099 | 1 | 353:dirt floor (1) | - | 32592,31787,4 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60001 | 1 | 1945:lever (1) | - | 32853,32318,9 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60002 | 1 | 1945:lever (1) | - | 32933,32261,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60003 | 1 | 1945:lever (1) | - | 32848,32335,12 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60004 | 1 | 2843:slain skeleton (1) | - | 32832,32277,10 | container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60005 | 1 | 1209:closed door (1) | - | 32827,32246,10 | porta | core door handling (actions.cpp Door::canUse/use) | porta/level door/quest door tratado pelo core ou movements | baixo |
| 60006 | 1 | 1654:throne (1) | - | 32839,32305,15 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 60007 | 1 | 1654:throne (1) | - | 32800,32459,13 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 60008 | 1 | 1654:throne (1) | - | 32785,32275,15 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 60009 | 1 | 1654:throne (1) | - | 32865,32431,15 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 60010 | 1 | 1656:throne (1) | - | 32845,32400,15 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 60011 | 1 | 1654:throne (1) | - | 32927,32253,15 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 60012 | 1 | 1654:throne (1) | - | 32843,32324,15 | nao-container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 60013 | 1 | 1850:wall mirror (1) | - | 32793,32469,14 | nao-container | itemid->other/wallmirror.lua | desconhecido | baixo |
| 60014 | 1 | 1746:treasure chest (1) | - | 32799,32471,15 | container | sem cobertura em actions.xml detectada | desconhecido | medio |
| 60015 | 1 | 353:dirt floor (1) | - | 32803,32391,15 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60016 | 1 | 1945:lever (1) | - | 32802,32362,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60017 | 1 | 1945:lever (1) | - | 32804,32357,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60018 | 1 | 1945:lever (1) | - | 32803,32353,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60019 | 1 | 1945:lever (1) | - | 32811,32357,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60020 | 1 | 1945:lever (1) | - | 32814,32349,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60021 | 1 | 1945:lever (1) | - | 32811,32338,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60022 | 1 | 1945:lever (1) | - | 32815,32333,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60023 | 1 | 1945:lever (1) | - | 32818,32326,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60024 | 1 | 1945:lever (1) | - | 32824,32323,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60025 | 1 | 1945:lever (1) | - | 32808,32322,15 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60026 | 1 | 1945:lever (1) | - | 32827,32263,11 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60027 | 1 | 426:stone tile (1) | - | 32827,32273,11 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60028 | 1 | 1945:lever (1) | - | 32843,32281,12 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60029 | 1 | 1945:lever (1) | - | 32850,32268,10 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60030 | 1 | 1945:lever (1) | - | 32851,32308,11 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60031 | 1 | 426:stone tile (1) | - | 32842,32276,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60032 | 1 | 426:stone tile (1) | - | 32842,32275,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60033 | 1 | 426:stone tile (1) | - | 32842,32274,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60034 | 1 | 426:stone tile (1) | - | 32906,32285,12 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60035 | 1 | 1945:lever (1) | - | 32862,32312,11 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60036 | 1 | 1945:lever (1) | - | 32820,32345,10 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60037 | 1 | 1945:lever (1) | - | 32847,32339,10 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60038 | 1 | 1945:lever (1) | - | 32847,32327,10 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60039 | 1 | 1945:lever (1) | - | 32820,32321,10 | nao-container | sem cobertura em actions.xml detectada | alavanca/objeto clicavel especial | medio |
| 60040 | 1 | 355:muddy floor (1) | - | 32854,32324,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60041 | 1 | 354:muddy floor (1) | - | 32862,32311,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60042 | 11 | 351:dirt floor (10), 354:muddy floor (1) | - | 32858,32285,9; 32846,32294,9; 32837,32302,9; 32858,32289,9; 32837,32304,9; 32842,32305,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60043 | 1 | 351:dirt floor (1) | - | 32836,32288,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60044 | 1 | 351:dirt floor (1) | - | 32844,32294,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60045 | 1 | 354:muddy floor (1) | - | 32848,32325,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60048 | 1 | 352:dirt floor (1) | - | 32855,32301,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60049 | 1 | 351:dirt floor (1) | - | 32858,32324,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60050 | 1 | 351:dirt floor (1) | - | 32839,32312,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60051 | 1 | 353:dirt floor (1) | - | 32852,32297,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60052 | 1 | 351:dirt floor (1) | - | 32832,32299,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60053 | 1 | 351:dirt floor (1) | - | 32834,32310,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |
| 60054 | 1 | 351:dirt floor (1) | - | 32850,32319,9 | nao-container | sem cobertura em actions.xml detectada | objeto clicavel especial ou quest nao-container | medio |

## Action IDs Proximos de 2000

- `1999`: 1 objetos; cobertura: core door handling (actions.cpp Door::canUse/use); hipotese: porta/level door/quest door tratado pelo core ou movements; risco: baixo.
- `2000`: 124 objetos; cobertura: itemid->quests/quests.lua; hipotese: quest chest/container ou reward container com item filho; risco: medio.
- `2001`: 20 objetos; cobertura: actionid->quests/quests.lua; itemid->quests/quests.lua; hipotese: quest nao-container por tabela uniqueid->reward; alguns ainda sem recompensa configurada; risco: critico.
- `2002`: ausente no mapa.
- `2003`: ausente no mapa.
- `2004`: ausente no mapa.
- `2005`: ausente no mapa.
- `2006`: ausente no mapa.
- `2007`: ausente no mapa.

## Action IDs no Mapa Ausentes em actions.xml

Esta lista considera ausencia de registro direto por `actionid`. Alguns IDs podem ainda ser cobertos por `itemid` ou pelo core de portas.
- `100`: 34 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=355:muddy floor (34); pos=31979,31564,8; 32040,31636,8; 32002,31694,8
- `101`: 68 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1209:closed door (30), 1212:closed door (29), 1231:closed door (3), 1210:closed door (2), 1234:closed door (2), +2 tipos; pos=31943,31709,6; 32100,31569,9; 32100,31570,9
- `102`: 6 objetos; tipo=nao-container; cobertura efetiva=itemid->other/teleport.lua; risco=baixo; itens=1368:draw well (3), 1369:draw well (3); pos=32353,32131,8; 32354,32131,8; 32507,32176,13
- `103`: 12 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2018:pool (5), 2016:pool (3), 2019:pool (2), 2017:pool (2); pos=32511,32068,8; 32513,32066,8; 32517,32065,8
- `231`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1225:closed door (1); pos=32569,32023,6
- `242`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1223:closed door (1); pos=32515,32248,8
- `244`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1225:closed door (1); pos=32567,32023,6
- `689`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=32278,31903,13
- `690`: 2 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (2); pos=32215,31838,15; 32216,31838,15
- `691`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=32215,31849,15
- `692`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=32171,31853,15
- `693`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=425:stone tile (1); pos=32243,31892,14
- `888`: 17 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (17); pos=32187,31936,14; 32187,31938,14; 32187,31939,14
- `889`: 8 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (8); pos=32187,31937,14; 32188,31937,14; 32188,31938,14
- `890`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=32192,31938,14
- `987`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=32311,31978,13
- `988`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=32805,31587,1
- `1020`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1229:gate of expertise (1); pos=32673,32100,8
- `1025`: 2 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1229:gate of expertise (2); pos=32325,32389,9; 32804,31583,2
- `1030`: 8 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1227:gate of expertise (6), 1229:gate of expertise (2); pos=32483,31630,10; 32479,31634,10; 32483,31635,10
- `1032`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1229:gate of expertise (1); pos=32510,31956,13
- `1035`: 7 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1229:gate of expertise (2), 1261:gate of expertise (2), 1259:gate of expertise (2), 1227:gate of expertise (1); pos=32475,31946,13; 32448,32042,8; 32795,32327,10
- `1040`: 3 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1229:gate of expertise (2), 1227:gate of expertise (1); pos=32353,32073,11; 32544,32179,14; 32981,31760,9
- `1045`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1229:gate of expertise (1); pos=32602,32207,10
- `1050`: 3 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1229:gate of expertise (3); pos=32212,32435,10; 32874,31974,12; 32875,31974,12
- `1060`: 5 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1227:gate of expertise (4), 1229:gate of expertise (1); pos=32483,31722,15; 32223,31869,14; 33085,31650,10
- `1070`: 2 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1229:gate of expertise (2); pos=33190,31684,14; 33195,31684,14
- `1075`: 10 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1247:gate of expertise (8), 1245:gate of expertise (2); pos=33083,32620,14; 33084,32620,14; 33192,32665,15
- `1080`: 2 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1229:gate of expertise (2); pos=33297,31670,14; 32834,32280,10
- `1100`: 2 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1229:gate of expertise (1), 1227:gate of expertise (1); pos=33211,31638,13; 33214,31671,13
- `1335`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=32176,31869,15
- `1336`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=32177,31869,15
- `1337`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=32250,31892,14
- `1999`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=3549:gate of expertise (1); pos=32882,32527,11
- `2000`: 124 objetos; tipo=misto (118 containers/6 nao-container); cobertura efetiva=itemid->quests/quests.lua; risco=medio; itens=1740:chest (61), 1741:box (8), 3128:dead human (7), 1419:sarcophagus (7), 3103:a pile of bones (6), +13 tipos; pos=32105,31567,9; 32109,31567,9; 32172,31602,10
- `2112`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=32815,31599,9
- `2451`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32310,31975,13
- `2452`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32310,31976,13
- `2453`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32312,31975,13
- `2454`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32312,31976,13
- `2455`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32314,31975,13
- `2456`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32314,31976,13
- `2551`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32220,31843,15
- `2552`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32220,31845,15
- `2553`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32220,31844,15
- `2554`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32220,31842,15
- `2555`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32220,31846,15
- `3001`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2088:silver key (1), 1212:closed door (1); pos=32794,31572,7; 32814,31597,7
- `3002`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2088:silver key (1), 1212:closed door (1); pos=32800,31578,7; 32796,31597,7
- `3003`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2088:silver key (1), 1212:closed door (1); pos=32790,31593,7; 32811,31597,7
- `3004`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2088:silver key (1), 1212:closed door (1); pos=32807,31576,6; 32809,31593,6
- `3005`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1212:closed door (1), 2088:silver key (1); pos=32794,31582,6; 32792,31590,6
- `3006`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2088:silver key (1), 1212:closed door (1); pos=32801,31579,6; 32814,31595,6
- `3007`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2088:silver key (1), 1212:closed door (1); pos=32813,31576,5; 32797,31592,4
- `3008`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2088:silver key (1), 1212:closed door (1); pos=32800,31582,2; 32804,31585,1
- `3012`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1212:closed door (1); pos=32675,31671,10
- `3142`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1209:closed door (1); pos=32450,32044,8
- `3301`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1212:closed door (1); pos=32619,32241,8
- `3302`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1212:closed door (1); pos=32619,32240,8
- `3303`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1212:closed door (1); pos=32614,32175,9
- `3304`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2091:golden key (1), 1212:closed door (1); pos=32623,32187,9; 32620,32199,10
- `3350`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1212:closed door (1); pos=32178,31928,6
- `3600`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1209:closed door (1), 2088:silver key (1); pos=32506,32175,14; 32509,32181,13
- `3666`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1212:closed door (1); pos=32578,32197,15
- `3701`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1209:closed door (1); pos=32786,32328,7
- `3702`: 1 objetos; tipo=chave; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2090:crystal key (1); pos=32778,32282,11
- `3899`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1212:closed door (1); pos=32479,31903,4
- `3940`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1212:closed door (1); pos=32190,32432,8
- `3980`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1212:closed door (1); pos=32277,32420,10
- `4501`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1209:closed door (1), 2089:copper key (1); pos=32039,31603,10; 32172,31602,10
- `4502`: 3 objetos; tipo=misto (2 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1212:closed door (2), 2089:copper key (1); pos=32107,31568,9; 32108,31568,9; 32201,31571,10
- `4503`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1209:closed door (1), 2089:copper key (1); pos=32035,31642,8; 32031,31686,8
- `4600`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1209:closed door (1); pos=32109,32172,8
- `4601`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1212:closed door (1), 2089:copper key (1); pos=32145,32100,11; 32150,32111,12
- `4603`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2088:silver key (1), 1209:closed door (1); pos=32176,32132,9; 32179,32149,10
- `5000`: 2 objetos; tipo=misto (1 portas/1 outros); cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1209:closed door (1), 2088:silver key (1); pos=32726,31976,6; 32726,31980,7
- `5002`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1212:closed door (1); pos=32817,31965,8
- `5010`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1209:closed door (1); pos=32824,31969,8
- `5999`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33093,32823,13
- `6000`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33097,32816,13
- `6001`: 236 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=421:sandstone floor (236); pos=33198,32959,14; 33199,32959,14; 33200,32959,14
- `6003`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33293,32742,13
- `6004`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33298,32742,13
- `6005`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33073,32590,13
- `6006`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33080,32589,13
- `6007`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33240,32856,13
- `6008`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33246,32849,13
- `6009`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33276,32553,14
- `6010`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1212:closed door (1); pos=33211,31634,13
- `6011`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33135,32683,12
- `6012`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33131,32683,12
- `6013`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33162,32831,10
- `6014`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33157,32832,10
- `6015`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=416:stone tile (1); pos=33362,32811,14
- `6016`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33234,32692,13
- `6017`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33234,32688,13
- `6018`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1397:mystic flame (1); pos=33272,32553,14
- `11001`: 2 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=446:wooden floor (2); pos=32091,32175,6; 32092,32175,6
- `11002`: 2 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=446:wooden floor (2); pos=32057,32192,7; 32057,32193,7
- `11003`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=354:muddy floor (1); pos=32066,32068,10
- `11004`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=353:dirt floor (1); pos=32169,32157,8
- `11005`: 2 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (2); pos=32098,32204,8; 32104,32204,8
- `11006`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32148,32105,11
- `11007`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32093,32174,8
- `11008`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32088,32148,9
- `11009`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32090,32148,9
- `11010`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32092,32148,9
- `11011`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32094,32148,9
- `11012`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32182,32145,11
- `11013`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1209:closed door (1); pos=32177,32148,11
- `13000`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=33399,32802,14
- `13001`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=33179,32890,11
- `13002`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=416:stone tile (1); pos=33198,32877,11
- `20001`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1225:closed door (1); pos=32223,31872,14
- `20002`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1225:closed door (1); pos=32223,31875,14
- `20003`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1225:closed door (1); pos=32223,31878,14
- `20004`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1225:closed door (1); pos=32223,31881,14
- `20005`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1225:closed door (1); pos=32223,31884,14
- `20006`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1225:closed door (1); pos=32223,31887,14
- `20007`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1225:closed door (1); pos=32223,31890,14
- `25001`: 8 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=231:sand (8); pos=33133,32568,7; 33208,32591,7; 33161,32598,7
- `25002`: 225 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=231:sand (225); pos=33135,32567,7; 33127,32570,7; 33132,32570,7
- `25003`: 15 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1225:closed door (11), 1223:closed door (4); pos=32703,31605,14; 32259,31948,14; 32448,31965,10
- `25004`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32180,31633,8
- `25005`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=33172,31896,8
- `25006`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=33290,31715,12
- `25007`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32528,31724,10
- `25008`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32488,31628,13
- `25009`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32614,32173,9
- `25010`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1946:lever (1); pos=32594,32212,9
- `25011`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32616,32222,10
- `25012`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=4857:wrinkled parchment (1); pos=33063,31624,15
- `25013`: 2 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (2); pos=33190,31629,13; 33191,31629,13
- `25014`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=33330,31591,15
- `30021`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=32607,31682,7
- `30022`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=32642,31925,12
- `30023`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=32369,32246,6
- `30024`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=32951,32035,7
- `30025`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=32360,31784,8
- `30026`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=33195,32849,6
- `30027`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=33216,32455,2
- `30028`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=32595,32749,6
- `30029`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1387:magic forcefield (1); pos=33210,31804,8
- `50001`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=474:time tile (1); pos=32791,32330,10
- `50002`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32479,31905,6
- `50003`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (1); pos=32481,31905,7
- `50004`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (1); pos=32476,31900,5
- `50005`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32481,31904,5
- `50006`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=354:muddy floor (1); pos=32497,31889,7
- `50007`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32479,31905,4
- `50008`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32478,31904,3
- `50009`: 3 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (3); pos=32476,31903,1; 32477,31903,1; 32478,31903,1
- `50010`: 3 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (3); pos=32476,31902,1; 32479,31903,1; 32481,31902,1
- `50011`: 3 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (3); pos=32478,31902,1; 32479,31902,1; 32480,31902,1
- `50012`: 3 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (3); pos=32477,31902,1; 32480,31903,1; 32481,31903,1
- `50013`: 4 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=474:time tile (4); pos=32486,31927,7; 32487,31927,7; 32486,31928,7
- `50014`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=474:time tile (1); pos=32566,31957,1
- `50015`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2334:Santa's Mailbox (1); pos=31948,31711,6
- `50016`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32315,31910,12
- `50017`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32212,31888,12
- `50018`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2593:mailbox (1); pos=32013,31562,4
- `50019`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32266,31861,11
- `50099`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=353:dirt floor (1); pos=32592,31787,4
- `60001`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32853,32318,9
- `60002`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32933,32261,15
- `60003`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32848,32335,12
- `60004`: 1 objetos; tipo=container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=2843:slain skeleton (1); pos=32832,32277,10
- `60005`: 1 objetos; tipo=porta; cobertura efetiva=core door handling (actions.cpp Door::canUse/use); risco=baixo; itens=1209:closed door (1); pos=32827,32246,10
- `60006`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1654:throne (1); pos=32839,32305,15
- `60007`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1654:throne (1); pos=32800,32459,13
- `60008`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1654:throne (1); pos=32785,32275,15
- `60009`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1654:throne (1); pos=32865,32431,15
- `60010`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1656:throne (1); pos=32845,32400,15
- `60011`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1654:throne (1); pos=32927,32253,15
- `60012`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1654:throne (1); pos=32843,32324,15
- `60013`: 1 objetos; tipo=nao-container; cobertura efetiva=itemid->other/wallmirror.lua; risco=baixo; itens=1850:wall mirror (1); pos=32793,32469,14
- `60014`: 1 objetos; tipo=container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1746:treasure chest (1); pos=32799,32471,15
- `60015`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=353:dirt floor (1); pos=32803,32391,15
- `60016`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32802,32362,15
- `60017`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32804,32357,15
- `60018`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32803,32353,15
- `60019`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32811,32357,15
- `60020`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32814,32349,15
- `60021`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32811,32338,15
- `60022`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32815,32333,15
- `60023`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32818,32326,15
- `60024`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32824,32323,15
- `60025`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32808,32322,15
- `60026`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32827,32263,11
- `60027`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (1); pos=32827,32273,11
- `60028`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32843,32281,12
- `60029`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32850,32268,10
- `60030`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32851,32308,11
- `60031`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (1); pos=32842,32276,9
- `60032`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (1); pos=32842,32275,9
- `60033`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (1); pos=32842,32274,9
- `60034`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=426:stone tile (1); pos=32906,32285,12
- `60035`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32862,32312,11
- `60036`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32820,32345,10
- `60037`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32847,32339,10
- `60038`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32847,32327,10
- `60039`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=1945:lever (1); pos=32820,32321,10
- `60040`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=355:muddy floor (1); pos=32854,32324,9
- `60041`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=354:muddy floor (1); pos=32862,32311,9
- `60042`: 11 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=351:dirt floor (10), 354:muddy floor (1); pos=32858,32285,9; 32846,32294,9; 32837,32302,9
- `60043`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=351:dirt floor (1); pos=32836,32288,9
- `60044`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=351:dirt floor (1); pos=32844,32294,9
- `60045`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=354:muddy floor (1); pos=32848,32325,9
- `60048`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=352:dirt floor (1); pos=32855,32301,9
- `60049`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=351:dirt floor (1); pos=32858,32324,9
- `60050`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=351:dirt floor (1); pos=32839,32312,9
- `60051`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=353:dirt floor (1); pos=32852,32297,9
- `60052`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=351:dirt floor (1); pos=32832,32299,9
- `60053`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=351:dirt floor (1); pos=32834,32310,9
- `60054`: 1 objetos; tipo=nao-container; cobertura efetiva=sem cobertura em actions.xml detectada; risco=medio; itens=351:dirt floor (1); pos=32850,32319,9

## Action IDs em actions.xml Ausentes no Mapa

- Nenhum registro direto por `actionid` esta ausente no mapa.

## Registros de actions.xml Sem Objeto Correspondente no Mapa

- uniqueid [30015] -> quests/annihilator.lua
- fromid 2666-2691 missing [2669, 2683, 2685, 2688] -> other/food.lua
- fromid 2787-2796 missing [2795] -> other/food.lua
- itemid [2114] -> other/piggybank.lua
- fromid 1728-1731 missing [1728, 1729, 1730, 1731] -> other/watch.lua
- fromid 3901-3938 missing [3901, 3902, 3903, 3904, 3905, 3906, 3907, 3908, 3909, 3910, 3912, 3913]... -> other/constructionkits.lua
- fromid 5086-5088 missing [5086, 5087, 5088] -> other/constructionkits.lua
- fromid 2070-2085 missing [2075, 2077, 2079, 2080, 2081, 2082, 2083, 2084] -> other/music.lua
- fromid 3951-3953 missing [3951, 3953] -> other/music.lua
- itemid [3957] -> other/music.lua
- fromid 2376-2404 missing [2379, 2382, 2402, 2403] -> other/destroy.lua
- fromid 2406-2419 missing [2415] -> other/destroy.lua
- fromid 2421-2453 missing [2424, 2433, 2437, 2438, 2441, 2447, 2449, 2451, 2452] -> other/destroy.lua
- fromid 2011-2015 missing [2011] -> other/fluids.lua
- fromid 2031-2034 missing [2031] -> other/fluids.lua
- fromid 2574-2577 missing [2574, 2575, 2576, 2577] -> other/fluids.lua
- fromid 5792-5797 missing [5792, 5793, 5794, 5795, 5796, 5797] -> other/die.lua
- fromid 5303-5304 missing [5303, 5304] -> other/windows.lua
- fromid 1843-1844 missing [1844] -> other/wallmirror.lua
- fromid 1846-1847 missing [1846, 1847] -> other/wallmirror.lua
- fromid 1849-1850 missing [1849] -> other/wallmirror.lua

## Unique IDs de Quest/Storage com Tratamento Incerto

| Unique ID | Action ID | Item | Posicao | Motivo |
|---:|---:|---|---|---|
| 10008 | 2000 | 2844:dead dragon | 32179,32224,9 | sem cobertura |
| 10009 | 2001 | 2103:honey flower | 32005,32139,3 | actionid 2001 sem reward configurado |
| 10010 | 2000 | 3129:dead human | 32176,32132,9 | sem cobertura |
| 10011 | 2000 | 3128:dead human | 32175,32145,11 | sem cobertura |
| 10012 | 2000 | 3128:dead human | 32174,32149,11 | sem cobertura |
| 10018 | 2001 | 1747:chest | 32170,32197,7 | actionid 2001 sem reward configurado |
| 10019 | 2001 | 2725:palm | 32172,32169,7 | actionid 2001 sem reward configurado |
| 10020 | 2001 | 2725:palm | 31983,32193,5 | actionid 2001 sem reward configurado |
| 10024 | 2001 | 1742:wooden coffin | 31984,32246,10 | actionid 2001 sem reward configurado |
| 10200 | - | 1409:gravestone | 32791,32333,9 | sem cobertura |
| 10202 | - | 474:time tile | 32791,32328,10 | sem cobertura |
| 10203 | - | 1945:lever | 32795,32337,11 | sem cobertura |
| 20000 | 2000 | 1721:bookcase | 33155,32840,6 | sem cobertura |
| 20011 | 2000 | 3103:a pile of bones | 32305,32254,9 | sem cobertura |
| 20019 | 2000 | 2843:slain skeleton | 32427,31943,14 | sem cobertura |
| 20025 | 2000 | 1718:bookcase | 32428,31591,15 | sem cobertura |
| 20026 | 2000 | 1721:bookcase | 32421,31594,15 | sem cobertura |
| 20027 | 2000 | 1718:bookcase | 32423,31591,15 | sem cobertura |
| 20036 | 2000 | 1717:drawers | 32588,31644,3 | sem cobertura |
| 20037 | 2000 | 1717:drawers | 32588,31645,3 | sem cobertura |
| 20040 | 2001 | 385:small hole | 32219,32401,10 | actionid 2001 sem reward configurado |
| 20041 | 2000 | 3103:a pile of bones | 32239,32471,10 | sem cobertura |
| 20042 | 2000 | 3129:dead human | 32256,32500,10 | sem cobertura |
| 20043 | 2000 | 3128:dead human | 32233,32491,10 | sem cobertura |
| 20044 | 2000 | 3104:dead dragon | 32245,32490,10 | sem cobertura |
| 20046 | 2000 | 3103:a pile of bones | 32503,31724,15 | sem cobertura |
| 20047 | 2000 | 3103:a pile of bones | 32496,31721,15 | sem cobertura |
| 20048 | 2000 | 3128:dead human | 32494,31721,15 | sem cobertura |
| 20054 | 2000 | 1410:stone coffin | 33049,32399,10 | sem cobertura |
| 20058 | 2000 | 1738:box | 32980,31727,9 | sem cobertura |
| 20059 | 2000 | 1738:box | 32981,31727,9 | sem cobertura |
| 20060 | 2000 | 1738:box | 32985,31727,9 | sem cobertura |
| 20066 | 2000 | 3058:dead human | 32814,32281,8 | sem cobertura |
| 20069 | 2000 | 3128:dead human | 32514,32248,8 | sem cobertura |
| 20077 | 2000 | 1419:sarcophagus | 33182,32712,14 | sem cobertura |
| 20078 | 2000 | 1419:sarcophagus | 33145,32663,15 | sem cobertura |
| 20079 | 2000 | 1419:sarcophagus | 33126,32589,15 | sem cobertura |
| 20080 | 2000 | 1419:sarcophagus | 33349,32825,14 | sem cobertura |
| 20081 | 2000 | 1419:sarcophagus | 33051,32774,14 | sem cobertura |
| 20082 | 2000 | 1419:sarcophagus | 33178,33013,14 | sem cobertura |
| 20083 | 2000 | 1419:sarcophagus | 33174,32932,15 | sem cobertura |
| 20085 | 2001 | 1742:wooden coffin | 33327,32182,7 | actionid 2001 sem reward configurado |
| 20086 | 2001 | 1742:wooden coffin | 33063,31624,15 | actionid 2001 sem reward configurado |
| 20087 | 2001 | 2720:dead tree | 32868,31955,11 | actionid 2001 sem reward configurado |
| 20088 | 2001 | 2720:dead tree | 32880,31955,11 | actionid 2001 sem reward configurado |
| 20089 | 2001 | 2720:dead tree | 32769,31968,7 | actionid 2001 sem reward configurado |
| 20090 | 2000 | 3103:a pile of bones | 32239,32478,10 | sem cobertura |
| 20091 | 2001 | 3061:dead human | 33086,31650,12 | actionid 2001 sem reward configurado |
| 20092 | 2001 | 2720:dead tree | 32497,31888,7 | actionid 2001 sem reward configurado |
| 20093 | 2001 | 2720:dead tree | 32800,31959,7 | actionid 2001 sem reward configurado |
| 20099 | 2000 | 1410:stone coffin | 32726,31980,7 | sem cobertura |
| 20100 | 2000 | 1738:box | 32495,31992,14 | sem cobertura |
| 20101 | 2000 | 1738:box | 32497,31992,14 | sem cobertura |
| 20102 | 2000 | 3128:dead human | 33182,31869,12 | sem cobertura |
| 20103 | 2000 | 3128:dead human | 32514,32303,10 | sem cobertura |
| 20104 | 2001 | 4369: | 32617,32250,7 | actionid 2001 sem reward configurado |
| 20105 | 2001 | 2720:dead tree | 32609,32244,7 | actionid 2001 sem reward configurado |
| 20106 | 2001 | 4369: | 32651,32244,7 | actionid 2001 sem reward configurado |
| 20108 | 2000 | 3103:a pile of bones | 32509,32181,13 | sem cobertura |

## Recomendacao de Prioridade

1. Critico: completar a tabela de `actionid=2001` em `quests.lua` para os 17 objetos com `uniqueid` ainda sem recompensa configurada, usando evidencia de mapa/reference antes de adicionar recompensa.
2. Critico: revisar `uniqueid` de faixa 10000-29999 marcados como tratamento incerto, especialmente objetos nao-container e `actionid=2000` sem item filho.
3. Medio: revisar action IDs ausentes em `actions.xml` que nao sejam portas e tenham muitos objetos, porque podem representar alavancas, passagens ou scripts nao migrados.
4. Baixo: portas e chaves devem ser auditadas em lote separado contra `items.xml`/core, pois muitos `actionid` ali sao parametros esperados e nao exigem script Lua.

