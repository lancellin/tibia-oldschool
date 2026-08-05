# CLIENT_PERFORMANCE_CHANGELOG

Chronological record of OTClient Redemption performance investigations,
decisions, tests, and fixes in `D:\tibia-oldschool`.

This document has an equivalent Portuguese version:
`CLIENT_PERFORMANCE_CHANGELOG.md`.

## July 3, 2026 Update

### Scope

This update records the first fix applied during the client performance audit,
focused on reducing microstutters without removing important visual features
from the game.

The target of this step was the map lighting system. Lighting must remain
enabled because the server uses RPG visual effects such as torches, light
spells, and floor-based ambient light.

### Diagnosis

`LightView` was found to recalculate lightmap pixels based on a hash that
included the camera visual `src`:

- `sources/otclient-redemption/src/client/lightview.cpp`
- `sources/otclient-redemption/src/client/mapview.cpp`

During smooth walking, `src` changes because of the visual camera offset. This
could invalidate the lightmap even when no real light source had changed.

The behavior was visually safe, but expensive: the client could recalculate
lighting pixels only because the camera moved visually.

### Implemented Fix

Coordinate updates were separated from pixel updates:

- `updateCoords(dest, src)` still runs to keep the lighting crop aligned with
  the camera.
- `updatePixels()` now depends on a real light-content hash.
- The content hash includes lightmap size, tile size, global light, light
  sources, shades, and relevant light tile data.
- The camera visual `src` no longer forces pixel recalculation in the new mode.
- Lightmap resize still forces an immediate refresh.

Main files changed:

- `sources/otclient-redemption/src/client/lightview.cpp`
- `sources/otclient-redemption/src/client/lightview.h`

### Rollback

The change was left with a simple rollback point in:

`sources/otclient-redemption/src/client/lightview.cpp`

Constant:

`LIGHTVIEW_CONTENT_CACHE_ENABLED`

When set to `true`, the client uses the light-content cache.
When set to `false`, the client returns to the previous behavior also based on
the camera `src`, while keeping the performance logs.

### Performance Logs

Logs were added with the tag:

`[LightViewPerf]`

The logs record:

- active mode;
- number of analyzed frames;
- number of `pixelUpdates`;
- number of `pixelSkips`;
- number of `coordUpdates`;
- number of lights in the last frame;
- number of shades in the last frame;
- lightmap size;
- tile size;
- visual `src` used by the frame.

Example:

`[LightViewPerf] mode=content frames=720 pixelUpdates=0 pixelSkips=720 coordUpdates=0 lastLights=2 lastShades=252 map=18x14 tileSize=64 src=(64,64 959x704)`

### Observed Results

Initial tests kept the visual behavior correct, and no visual bug was observed.

The gain was strongest while standing still or walking slowly:

- while standing still, the client started avoiding almost all `updatePixels`;
- while walking slowly, logs showed a relevant reduction in recalculations;
- while walking with an artificially very fast character and player-attached
  light, `pixelUpdates` remained high as expected, because the light source
  itself changes position almost every frame.

It was confirmed that `pixelUpdates` follows `coordUpdates` in scenarios where
light is attached to the character. This is expected in the current model,
because the real light source position changes during smooth walking.

### Decisions

- Do not disable client lighting.
- Do not apply time-based light throttling in milliseconds at this stage.
- Do not classify equipped torches or `utevo lux` as static lights, because
  both follow the character.
- Keep the current implementation because it is safe, small, and reversible.
- Investigate a future static/dynamic lighting split only if new measurements
  show a real need.

### Validation

The client was rebuilt successfully with:

`cmake --build build-validation\otclient --config RelWithDebInfo --target otclient --parallel 8`

The validated executable was copied to:

`sources/otclient-redemption/otclient.exe`

SHA256 hash recorded at build time:

`5229583912ED70A86CA4056143E4A48B80410F75D8D7B362549101C3A41AEF27`

## July 3, 2026 Update - Battle List

### Scope

This update records a small and localized Battle List optimization, focused on
the case where the list is sorted by distance.

The goal was to reduce repeated work during movement without changing critical
list events and without making the UI feel noticeably delayed.

### Diagnosis

`onCreaturePositionChange` in:

`sources/otclient-redemption/modules/game_battle/battle.lua`

was found to perform heavy work when the Battle List was sorted by distance:

- recalculating distances for creatures in the list;
- sorting the `binaryTree`;
- calling `correctBattleButtons`;
- adjusting widget visibility.

This path could run very frequently during player or creature movement,
especially in areas with many visible creatures.

### Implemented Fix

A simple batch was added for distance updates:

- with fewer than 10 creatures, distance updates are grouped in `70ms`;
- with 10 or more creatures, distance updates are grouped in `120ms`;
- repeated calls during the batch window only increment a pending counter;
- when the batch runs, distances are recalculated once, the list is sorted once,
  and widgets are corrected once.

Critical events remain immediate:

- creature appearance;
- creature disappearance;
- filters and sorting changed by the player;
- player floor changes;
- full Battle List rebuilds.

### Performance Logs

The following tag was added:

`[BattleListPerf]`

The aggregated log records, per performance window:

- number of scheduled updates;
- number of executed batches;
- number of sorts;
- number of `correctBattleButtons` calls;
- number of small and large batches;
- number of grouped movement events;
- accumulated time spent inside batches.

### Validation

Lua syntax was validated with:

`tools\dependencies\otclient-vcpkg-installed\x64-windows-static\tools\luajit\luajit.exe -b sources\otclient-redemption\modules\game_battle\battle.lua NUL`

### Decisions

- Do not delay sorting modes that do not depend on distance.
- Do not change critical Battle List events.
- Use `70ms` and `120ms` because they are conservative UI values.
- Keep aggregated logs instead of logging every batch, avoiding extra I/O cost.
