# MINIMAP_CHANGELOG

Chronological record of minimap-generation changes, tests, decisions, and fixes
in `D:\tibia-oldschool`.

This document has an equivalent Portuguese version:
`MINIMAP_CHANGELOG.md`.

## June 23, 2026 Update

### Scope

This update records the investigation and implementation of a custom workflow
to populate and save the `.otmm` minimap using OTClient Redemption with the
Nekiro TFS 1.5 downgrade 7.72 server, without relying on RME.

The goal was to generate the real server minimap from tiles sent through the
game protocol, preserving the normal client and server behavior.

### Diagnosis

The initial module in:

- `sources/otclient-redemption/modules/gerador_mapa/gerador_mapa.lua`
- `sources/otclient-redemption/modules/gerador_mapa/gerador_mapa.otmod`

was confirmed not to populate the real minimap cache because it only called:

`minimapWidget:setCameraPosition({x = currentX, y = currentY, z = currentZ})`

That call only moves the `UIMinimap` widget view/camera. It does not make the
client request map data from the server, does not create tiles in `g_map`, and
does not call the native routine that updates the minimap cache.

Functions and files confirmed during the investigation:

- `UIMinimap::setCameraPosition`: `sources/otclient-redemption/src/client/uiminimap.cpp`
- `UIMinimap::drawSelf`: `sources/otclient-redemption/src/client/uiminimap.cpp`
- `Minimap::updateTile`: `sources/otclient-redemption/src/client/minimap.cpp`
- `Minimap::loadOtmm`: `sources/otclient-redemption/src/client/minimap.cpp`
- `Minimap::saveOtmm`: `sources/otclient-redemption/src/client/minimap.cpp`
- `g_minimap` bindings: `sources/otclient-redemption/src/client/luafunctions.cpp`
- minimap updates through `Map::notificateTileUpdate`: `sources/otclient-redemption/src/client/map.cpp`

The `.otmm` cache is populated by `Minimap::updateTile(pos, tile)`, which is
called when real tiles arrive at the client and are inserted or updated in
`g_map`. Lua exposed `g_minimap.clean`, `g_minimap.loadImage`,
`g_minimap.saveImage`, `g_minimap.loadOtmm`, and `g_minimap.saveOtmm`, but did
not expose `g_minimap.updateTile`.

For that reason, the original script saved a file of roughly 1 KB: it contained
the header and terminator, but almost no `MinimapBlock` marked as seen.

### Confirmed Protocol Flow

The minimap was confirmed to be populated when the client receives map
descriptions through the protocol:

- `ProtocolGame::parseMapDescription`
- `ProtocolGame::parseMapMoveNorth`
- `ProtocolGame::parseMapMoveEast`
- `ProtocolGame::parseMapMoveSouth`
- `ProtocolGame::parseMapMoveWest`
- `ProtocolGame::parseUpdateTile`
- `ProtocolGame::parseTileAddThing`

On the server side, player teleports were confirmed to trigger map description
sends:

- `ProtocolGame::sendMapDescription`
- `ProtocolGame::sendMoveCreature(..., teleport = true)`
- `ProtocolGame::GetMapDescription`

Main files:

- `sources/otclient-redemption/src/client/protocolgameparse.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/protocolgame.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/map.h`

The server send area was also confirmed to use:

- `Map::maxClientViewportX = 8`
- `Map::maxClientViewportY = 6`

### GM, GOD, And Empty Tiles

GM/GOD players were confirmed not to receive a special global map view. The
group differences are permission flags in:

- `server/data/XML/groups.xml`

It was also confirmed that `Creature:teleportTo(position)` in server Lua calls
`g_game.internalTeleport(creature, position, pushMovement)`. That routine
requires a `Tile*` at the destination coordinate. If the point is black/empty,
the teleport fails.

Relevant files:

- `sources/nekiro-tfs-1.5-7.72/src/luascript.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/game.cpp`

For this reason, the implemented scanner does not try to stand on black tiles.
It searches for a valid nearby tile around each grid point before teleporting.

### Server Scan Command

The following talkaction was created:

- command: `/scanmap`
- script: `server/data/talkactions/scripts/scanmap.lua`
- registration: `server/data/talkactions/talkactions.xml`

Available commands:

- `/scanmap start`
- `/scanmap status`
- `/scanmap stop`

Initial implemented configuration:

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

After in-game testing, the script was accelerated:

```lua
delayMs = 80
emptyBatchPerTick = 1500
progressEvery = 250
```

The scanner traverses the map from bottom to top:

1. starts on floor `15`;
2. scans X/Y in `14` tile steps;
3. searches for a valid tile within a `7` tile radius;
4. teleports the GM/GOD to that tile if found;
5. counts the grid point as skipped if no tile is found;
6. stops after passing floor `7`;
7. supports safe interruption with `/scanmap stop`;
8. supports progress checks with `/scanmap status`.

Empty-point batching was accelerated because underground floors can have
hundreds of consecutive black points. The main delay is now reserved for real
teleports, giving the client time to process map packets.

### Client Module

The module:

- `sources/otclient-redemption/modules/gerador_mapa/gerador_mapa.lua`

was simplified. It no longer moves the minimap camera and now exposes helper
functions for the client terminal:

- `prepararGeracaoMapa()`
- `salvarMapaGerado()`
- `iniciarVarredura()`

The functions were registered in both `_G` and `commandEnv`, because the
OTClient Redemption terminal executes commands in its own environment.

Expected usage:

```lua
prepararGeracaoMapa()
```

Then, in-game:

```text
/scanmap start
```

When finished:

```lua
salvarMapaGerado()
```

Confirmed fallback:

```lua
g_minimap.saveOtmm('/minimap.otmm')
```

### Validation

Validated items:

- Lua syntax for `server/data/talkactions/scripts/scanmap.lua` with Luajit;
- Lua syntax for `sources/otclient-redemption/modules/gerador_mapa/gerador_mapa.lua` with Luajit;
- XML validity for `server/data/talkactions/talkactions.xml`;
- real in-game execution with OTClient Redemption in 64x64 HD mode;
- automatic scan through floor `7`;
- final `minimap.otmm` save;
- correct generated minimap coverage.

The in-game test confirmed that the client received tiles correctly during
teleports and that the saved minimap was correct.

### Decisions

- RME was not used.
- No offline OTBM -> OTMM generator was created in this phase.
- The client protocol was not changed.
- No new native `g_minimap.updateTile` Lua binding was exposed.
- The chosen solution uses the real protocol path: server sends tiles, client
  populates `g_map`, `g_minimap`, and finally saves `.otmm`.
- The scan was initially limited to `z = 15` through `z = 7`, because walking
  on `z = 7` also reveals the upper floors sent by the protocol.

### Changed Files

- `server/data/talkactions/scripts/scanmap.lua`
- `server/data/talkactions/talkactions.xml`
- `sources/otclient-redemption/modules/gerador_mapa/gerador_mapa.lua`

### Result

The workflow is now functional for generating the server minimap without RME:

1. clean the client minimap cache;
2. scan the map with `/scanmap start`;
3. monitor with `/scanmap status`;
4. stop with `/scanmap stop`, if needed;
5. save with `salvarMapaGerado()` or `g_minimap.saveOtmm('/minimap.otmm')`.

