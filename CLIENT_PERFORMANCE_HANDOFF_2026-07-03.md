# Client Performance Handoff - 2026-07-03

## Current State

This file records the context needed to resume the OTClient performance work after shutdown.

Workspace:
- `D:\tibia-oldschool`
- Client source: `sources\otclient-redemption`
- Active things path: `sources\otclient-redemption\data\things\772`

## Implemented Changes

### LightView

Files:
- `sources\otclient-redemption\src\client\lightview.cpp`
- `sources\otclient-redemption\src\client\lightview.h`

Status:
- Light pixel recalculation was optimized with content-based cache.
- `updateCoords()` still runs while walking.
- Old test logs `LightViewPerf` were removed.
- The optimization stayed active via `LIGHTVIEW_CONTENT_CACHE_ENABLED = true`.

Reasoning:
- Walking changes the visual source rect, but should not force full light pixel recalculation when light sources/global light/tile shades did not change.

### Battle List

File:
- `sources\otclient-redemption\modules\game_battle\battle.lua`

Status:
- Distance sort/update work is batched.
- Delay is `70ms` for fewer than `10` creatures.
- Delay is `120ms` for `10+` creatures.
- Old test logs `BattleListPerf` were removed.

Reasoning:
- Reduces repeated distance recalculation/sort while moving without changing battle list behavior materially.

### ThingType Texture Timing Logs

Files:
- `sources\otclient-redemption\src\client\thingtype.cpp`
- `sources\otclient-redemption\src\client\thingtype.h`
- `sources\otclient-redemption\src\framework\graphics\texture.cpp`

Status:
- Texture load instrumentation is active.
- Logs currently expected:
  - `ThingTypeLoadPerf`
  - `ThingTypeLoadSlow`
  - `TextureUploadPerf`
  - `TextureUploadSlow`

Meaning:
- `ThingTypeLoadPerf.decodeMs`: PNG decode from CWM/SPR path.
- `ThingTypeLoadPerf.atlasMs`: CPU atlas/image assembly.
- `TextureUploadPerf.uploadMs`: blocking CPU-side OpenGL upload path.

Important:
- Old light/battle logs were removed; only texture logs remain intentionally.

### ThingType Cache Lifetime

File:
- `sources\otclient-redemption\src\framework\core\garbagecollection.cpp`

Status:
- ThingType texture idle unload was increased from `60s` to `1200s` / `20min`.
- Constant changed to `uint32_t` because `1200 * 1000` does not fit in `uint16_t`.

Reasoning:
- Prevents repeated unload/reload of HD textures during regular gameplay.
- Heavy GM teleport test did not show memory/client failure.

### Nearby Texture Prewarm

Files:
- `sources\otclient-redemption\src\client\mapview.cpp`
- `sources\otclient-redemption\src\client\mapview.h`
- `sources\otclient-redemption\src\client\tile.cpp`
- `sources\otclient-redemption\src\client\tile.h`
- `sources\otclient-redemption\src\client\thingtype.cpp`
- `sources\otclient-redemption\src\client\thingtype.h`

Status:
- Added preload of textures up to `2` extra tiles around the draw area.
- Prewarm runs only after visible tiles are recalculated.
- Prewarm only schedules async loads.
- If `asyncTxtLoading` is disabled, prewarm does not force synchronous offscreen loads.
- It does not add tiles to visible draw cache.
- It should not affect battle list, click logic, spectators, or lighting.

Reasoning:
- Avoid converting offscreen prewarm into synchronous stutter.
- Keep behavior isolated to texture preparation.

### CWM Deduplication

Files:
- `sources\otclient-redemption\data\things\772\Tibia.cwm`
- `tools\assets\dedupe_cwm.py`
- `tools\assets\work\cwm-dedupe-current\dedupe-summary-20260703-044556.json`

Backup:
- `sources\otclient-redemption\data\things\772\Tibia.before-dedupe-20260703-044556.cwm`

Result:
- Old CWM size: `66,312,771 bytes`
- New CWM size: `53,065,166 bytes`
- Saved: `13,247,605 bytes`
- Entries: `10,672`
- Unique payloads: `8,707`
- Duplicate entries deduped: `1,965`
- New CWM SHA256: `14F7E13AF27379196300BAD1723555F8B36EF0E202F6ABA344C4AD9E5AA3798E`

Validation:
- Logical comparison against backup passed.
- Every Sprite ID still returns exactly the same PNG payload as before.
- Header preserved: version `1`, sprites count `16114`, entries `10672`.
- Client opened and loaded HD after dedupe.

Important:
- CWM dedupe reduces disk/package size and validates shared offsets.
- It does not automatically eliminate runtime PNG decode, because current loader still decodes by sprite ID when requested.

## Observed Performance Evidence

With HD and async enabled:
- `sync=0`, all ThingType loads async.
- `decodeMs` remains the dominant part of `ThingTypeLoadPerf`.
- Slow ThingType loads are concentrated around animated HD items such as `4633`, `4634`, `4635`, `4636`.
- Upload slow logs are often `256x512` textures.

After CWM dedupe test:
- Client loaded normally.
- Example totals from the post-dedupe log:
  - `720` ThingType loads
  - `0` sync loads
  - `1280ms` total load time
  - `982ms` decode
  - `290ms` atlas
  - `1652` texture uploads
  - `984ms` aggregated upload time

Interpretation:
- Runtime decode still matters.
- CWM file dedupe is useful but not the final runtime optimization.

## Miracle Client Inspection

Path inspected read-only:
- `C:\Users\guisu\OneDrive\Área de Trabalho\Miracle7.4`

Findings:
- Distribution is mostly:
  - `data.zip`: about `203.6 MB`
  - executables: about `21 MB`
  - DLLs: about `12.6 MB`
- Inside `data.zip`, things data is packed as:
  - `data/things/Miracle/Tibia.pkhd`: `143.55 MB`
  - `data/things/Miracle/Tibia.pksd`: `40.17 MB`
  - `data/things/Miracle/Tibia.pkgd`: `0.71 MB`
- These files start with `ENC3`.
- Zip compression ratio is about `1`, meaning those packs are already compressed/encrypted/packed internally.

Conclusion:
- Miracle does not appear to ship PNG-per-sprite or our CWM format.
- They use custom packed things files.
- Their runtime package is clean; they do not ship source trees, build artifacts, CWM backups, tools, etc.

## Proposed Next Direction

Do not implement immediately without confirming.

### HD Fast Pack - RGBA Raw

Goal:
- Add a second HD option for testing a raw RGBA pack.
- Do not remove current CWM or PNG decoder.
- Do not redraw any sprites.

Expected flow:
1. Read current `Tibia.cwm`.
2. Decode each embedded PNG once during asset build/conversion.
3. Write a new pack, possibly named `Tibia.hdp`, `Tibia.rgba`, or `Tibia.pkhd`.
4. Store raw RGBA payloads with header + entry table.
5. In client, add loader that reads RGBA directly into `Image`.
6. If fast pack exists and option is enabled, load fast pack first.
7. If it fails, fallback to CWM.
8. If CWM fails or HD disabled, fallback to classic SPR.

Expected size:
- Current `10,672` HD sprites as 64x64 RGBA raw would be around `160-170 MB` installed.
- This is acceptable for optional HD.
- Download can be compressed; actual compressed size should be measured.

Performance expectation:
- Removes PNG decode cost from player runtime.
- Render still uploads RGBA textures to GPU.
- Should reduce `ThingTypeLoadPerf.decodeMs` substantially for HD fast pack.

### UI Option

User preference:
- Keep current `Sprites HD` option.
- Add a new experimental option below it for the fast pack.
- User plans to disable current HD before rebuild to save default as disabled.

Suggested UI behavior:
- Current `Sprites HD`: existing CWM PNG mode.
- New option: `Sprites HD Fast Pack` or `Use fast HD pack (experimental)`.
- New option default: disabled.
- If enabled and pack is valid, use RGBA fast pack.
- If invalid/missing, show warning and fallback.
- Avoid making two independent HD backends conflict; define clear priority.

### Integrity / Modification Detection

Possible and recommended for the fast pack:
- Hash whole pack with SHA-256.
- Optionally manifest/hash per pack version.
- Client can warning/fallback if hash mismatch.
- Client can report hash mismatch to server later.

Caution:
- Do not ban automatically on first mismatch.
- File corruption, stale patch, or partial update can create false positives.
- This is integrity checking, not unbreakable anti-cheat.

## Ideas Deferred

### Cache by CWM Offset/Size

Since deduped CWM has multiple Sprite IDs sharing the same payload offset, the PNG loader could cache decoded HD images by `(offset, size)`.

Benefit:
- Reduces repeated PNG decode for duplicated payloads.

Risk:
- Must avoid shared mutable `ImagePtr` contamination if later code mutates images.
- Needs mutex/thread safety.
- Clear cache on `loadCwmSpr()` and `unload()`.

This is still useful if keeping CWM PNG mode.

### RLE / Palette

Discussed but deferred.

RGBA raw is simpler and preferred for first HD fast pack test.
RLE/palette may reduce installed size, but adds format complexity.

### Classic SPR to RGBA

Deferred.

Classic `Tibia.spr` is already relatively efficient and not the main current bottleneck.

## Validation Commands Used

Build client:
```powershell
cmake --build build-validation\otclient --config RelWithDebInfo --target otclient --parallel 8
```

Copy built executable:
```powershell
Copy-Item sources\otclient-redemption\RelWithDebInfo\otclient.exe sources\otclient-redemption\otclient.exe -Force
```

Lua syntax check:
```powershell
tools\dependencies\otclient-vcpkg-installed\x64-windows-static\tools\luajit\luajit.exe -b sources\otclient-redemption\modules\game_battle\battle.lua NUL
```

Python runtime used when `python` PATH alias failed:
```powershell
C:\Users\guisu\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe
```

## Next Prompt Suggestion

When resuming, ask:

```text
Leia CLIENT_PERFORMANCE_HANDOFF_2026-07-03.md e vamos implementar a opção experimental HD Fast Pack RGBA cru, sem remover CWM/PNG e sem alterar sprites.
```
