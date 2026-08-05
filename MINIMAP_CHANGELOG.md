# MINIMAP_CHANGELOG

Registro cronologico das alteracoes, testes, decisoes e correcoes relacionadas
ao fluxo de geracao de minimap em `D:\tibia-oldschool`.

Este documento possui uma versao equivalente em ingles:
`MINIMAP_CHANGELOG_EN.md`.

## Atualizacao De 23 De Junho De 2026

### Escopo

Esta atualizacao registra a investigacao e a implementacao de um fluxo proprio
para preencher e salvar o minimap `.otmm` usando OTClient Redemption com o
servidor TFS 1.5 downgrade Nekiro 7.72, sem depender do RME.

O objetivo foi gerar o minimap real do servidor a partir dos tiles enviados pelo
protocolo do jogo, preservando o funcionamento normal do client e do servidor.

### Diagnostico

Foi confirmado que o modulo inicial em:

- `sources/otclient-redemption/modules/gerador_mapa/gerador_mapa.lua`
- `sources/otclient-redemption/modules/gerador_mapa/gerador_mapa.otmod`

nao preenchia o cache real do minimap porque chamava apenas:

`minimapWidget:setCameraPosition({x = currentX, y = currentY, z = currentZ})`

Essa chamada move somente a visualizacao/camera do widget `UIMinimap`. Ela nao
faz o client pedir mapa ao servidor, nao cria tiles em `g_map` e nao chama a
rotina nativa que atualiza o cache do minimap.

Funcoes e arquivos confirmados durante a investigacao:

- `UIMinimap::setCameraPosition`: `sources/otclient-redemption/src/client/uiminimap.cpp`
- `UIMinimap::drawSelf`: `sources/otclient-redemption/src/client/uiminimap.cpp`
- `Minimap::updateTile`: `sources/otclient-redemption/src/client/minimap.cpp`
- `Minimap::loadOtmm`: `sources/otclient-redemption/src/client/minimap.cpp`
- `Minimap::saveOtmm`: `sources/otclient-redemption/src/client/minimap.cpp`
- bindings de `g_minimap`: `sources/otclient-redemption/src/client/luafunctions.cpp`
- atualizacao do minimap via `Map::notificateTileUpdate`: `sources/otclient-redemption/src/client/map.cpp`

O cache `.otmm` e alimentado por `Minimap::updateTile(pos, tile)`, chamado
quando tiles reais chegam ao client e entram/atualizam em `g_map`. Em Lua, o
client expunha `g_minimap.clean`, `g_minimap.loadImage`, `g_minimap.saveImage`,
`g_minimap.loadOtmm` e `g_minimap.saveOtmm`, mas nao expunha
`g_minimap.updateTile`.

Por isso, o arquivo salvo pelo script original ficava com cerca de 1 KB: havia
cabecalho e terminador, mas praticamente nenhum bloco `MinimapBlock` marcado
como visto.

### Fluxo Do Protocolo Confirmado

Foi confirmado que o minimap e preenchido quando o client recebe descricoes de
mapa pelo protocolo:

- `ProtocolGame::parseMapDescription`
- `ProtocolGame::parseMapMoveNorth`
- `ProtocolGame::parseMapMoveEast`
- `ProtocolGame::parseMapMoveSouth`
- `ProtocolGame::parseMapMoveWest`
- `ProtocolGame::parseUpdateTile`
- `ProtocolGame::parseTileAddThing`

No servidor, foi confirmado que teleports do proprio player disparam envio de
descricao de mapa:

- `ProtocolGame::sendMapDescription`
- `ProtocolGame::sendMoveCreature(..., teleport = true)`
- `ProtocolGame::GetMapDescription`

Arquivos principais:

- `sources/otclient-redemption/src/client/protocolgameparse.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/protocolgame.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/map.h`

Tambem foi confirmado que a area enviada pelo servidor usa:

- `Map::maxClientViewportX = 8`
- `Map::maxClientViewportY = 6`

### GM, GOD E Tiles Vazios

Foi verificado que GM/GOD nao recebem uma visao global especial do mapa. As
diferencas entre grupos ficam nas flags de permissao em:

- `server/data/XML/groups.xml`

Tambem foi confirmado que `Creature:teleportTo(position)` no Lua do servidor
chama `g_game.internalTeleport(creature, position, pushMovement)`. Essa rotina
exige que exista `Tile*` na coordenada de destino. Se o ponto for preto/vazio,
o teleport falha.

Arquivos relevantes:

- `sources/nekiro-tfs-1.5-7.72/src/luascript.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/game.cpp`

Por causa disso, o scanner implementado nao tenta pisar em tiles pretos. Ele
procura um tile valido proximo ao ponto do grid antes de teleportar.

### Comando De Varredura Do Servidor

Foi criado o talkaction:

- comando: `/scanmap`
- script: `server/data/talkactions/scripts/scanmap.lua`
- registro: `server/data/talkactions/talkactions.xml`

Comandos disponiveis:

- `/scanmap start`
- `/scanmap status`
- `/scanmap stop`

Configuracao inicial implementada:

```lua
minX = 31800
minY = 31500
maxX = 33400
maxY = 33000
startZ = 15
endZ = 7
step = 14
searchRadius = 7
delayMs = 350
emptyBatchPerTick = 250
progressEvery = 50
```

Apos testes em jogo, o script foi acelerado:

```lua
delayMs = 80
emptyBatchPerTick = 1500
progressEvery = 250
```

O scanner percorre o mapa de baixo para cima:

1. comeca no andar `15`;
2. varre X/Y em saltos de `14` tiles;
3. procura tile valido em um raio de `7` tiles;
4. se encontra, teleporta o GM/GOD para esse tile;
5. se nao encontra, contabiliza como ponto pulado;
6. termina ao passar do andar `7`;
7. permite interrupcao segura com `/scanmap stop`;
8. permite acompanhamento com `/scanmap status`.

O lote de pontos vazios foi acelerado porque andares subterraneos podem ter
centenas de pontos pretos consecutivos. O delay principal ficou reservado para
teleports reais, para dar tempo ao client de processar os pacotes de mapa.

### Modulo Do Client

O modulo:

- `sources/otclient-redemption/modules/gerador_mapa/gerador_mapa.lua`

foi simplificado. Ele deixou de mover a camera do minimap e passou a expor
funcoes auxiliares para o terminal do client:

- `prepararGeracaoMapa()`
- `salvarMapaGerado()`
- `iniciarVarredura()`

As funcoes tambem foram registradas em `_G` e em `commandEnv`, porque o terminal
do OTClient Redemption executa comandos em um ambiente proprio.

Uso esperado:

```lua
prepararGeracaoMapa()
```

Depois, no jogo:

```text
/scanmap start
```

Ao terminar:

```lua
salvarMapaGerado()
```

Fallback confirmado:

```lua
g_minimap.saveOtmm('/minimap.otmm')
```

### Validacao

Foram validados:

- sintaxe Lua de `server/data/talkactions/scripts/scanmap.lua` com Luajit;
- sintaxe Lua de `sources/otclient-redemption/modules/gerador_mapa/gerador_mapa.lua` com Luajit;
- XML de `server/data/talkactions/talkactions.xml`;
- execucao real em jogo com OTClient Redemption em HD mode 64x64;
- varredura automatica ate o andar `7`;
- salvamento final de `minimap.otmm`;
- preenchimento correto do minimap gerado.

O teste em jogo confirmou que o client recebeu os tiles corretamente durante os
teleports e que o minimap salvo ficou correto.

### Decisoes Tomadas

- Nao foi usado RME.
- Nao foi criada ferramenta offline OTBM -> OTMM nesta etapa.
- Nao foi alterado o protocolo do client.
- Nao foi exposta uma nova funcao nativa `g_minimap.updateTile` para Lua.
- A solucao adotada usa o caminho real do protocolo: servidor envia tiles,
  client popula `g_map`, `g_minimap` e finalmente salva `.otmm`.
- A varredura foi limitada inicialmente a `z = 15` ate `z = 7`, porque andar em
  `z = 7` tambem libera os andares superiores enviados pelo protocolo.

### Arquivos Alterados

- `server/data/talkactions/scripts/scanmap.lua`
- `server/data/talkactions/talkactions.xml`
- `sources/otclient-redemption/modules/gerador_mapa/gerador_mapa.lua`

### Resultado

O fluxo ficou funcional para gerar o minimap do servidor sem RME:

1. limpar o cache do minimap no client;
2. varrer o mapa com `/scanmap start`;
3. acompanhar com `/scanmap status`;
4. interromper com `/scanmap stop`, se necessario;
5. salvar com `salvarMapaGerado()` ou `g_minimap.saveOtmm('/minimap.otmm')`.

