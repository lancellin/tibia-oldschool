# NPC_CHANGELOG

Chronological record of NPC-related changes, tests, decisions, and fixes in
`D:\tibia-oldschool`.

This document has an equivalent Portuguese version:
`NPC_CHANGELOG.md`.

## June 21, 2026 Update

### Scope

This update records the work that started with Xodet's shop and the related
fluid fixes completed through final in-game validation.

The goal was to preserve the current TFS 1.5 and OTClient architecture,
without porting the old RealOTX system or introducing a compatibility layer.

### Xodet NPC

Xodet was implemented as a simple merchant using the current system:

- XML with `script="default.lua"`;
- `module_shop=1`;
- no dedicated Lua script;
- no sale of ready-to-use runes;
- only `blank rune` is sold among rune items;
- no wand or rod sales;
- no Amulet of Loss sales.

Main file:

- `server/data/npc/Xodet.xml`

Spawn:

- file: `server/data/world/world-spawn.xml`;
- final position: `{x = 32397, y = 32222, z = 7}`;
- town: Thais.

Items available in the shop:

| Item | Item ID | Price | Subtype |
| --- | ---: | ---: | ---: |
| blank rune | 2260 | 10 gp | - |
| life fluid | 2006 | 50 gp | 10 |
| magic lightwand | 2163 | 400 gp | - |
| mana fluid | 2006 | 40 gp | 7 |
| spellbook | 2175 | 150 gp | - |

The real names were explicitly provided in the `life fluid` and `mana fluid`
parameters. This prevents both products from being displayed only as `vial`
in the trade window.

### Mana Fluid And Life Fluid Subtypes

The correct shop format was confirmed in the existing parser:

- `server/data/npc/lib/npcsystem/modules.lua`

The adopted format for items with a subtype is:

`name,itemid,cost,subType,realName`

Server values:

- `mana fluid`: subtype `7`;
- `life fluid`: subtype `10`.

These values match `FLUID_MANA` and `FLUID_LIFE` in the current codebase.
They were not copied directly from RealOTX.

### Legacy Shop Visual Fix

Even after names, prices, and purchased items were correct, the legacy shop
window displayed the wrong fluid colors.

Changed file:

- `sources/otclient-redemption/modules/game_npctrade/controllers/npc_legacy_ui.lua`

Relevant functions:

- `onOpenNpcTrade(items)`;
- `refreshTradeItems()`.

Final solution:

- the fix is restricted to client version `772`;
- only items recognized as fluid containers are considered;
- only the names `mana fluid` and `life fluid` receive special handling;
- an item clone is stored in `newItem.displayPtr`;
- the clone receives the visual value required to represent the fluid;
- `refreshTradeItems()` uses `item.displayPtr or item.ptr` only to render the
  item in the shop.

Purchases still use:

- `selectedItem.ptr`;
- `g_game.buyItem(...)`.

Therefore, the visual correction does not change the actual item sent to the
server, its price, or its purchase subtype.

### Visual Container Desynchronization

Tests with mana fluid and life fluid exposed a broader backpack issue:

- items visually moved to different slots;
- the used fluid could appear to remain filled;
- another fluid could appear empty instead;
- some items stopped responding until relog;
- relog rebuilt the correct container state.

The cause was not the NPC, item `2006`, protocol, or sprites. Automatic
sorting rearranged only the visual container widgets, while incremental
updates continued to use the real slots sent by the server.

Changed file:

- `sources/otclient-redemption/modules/game_containers/containers.lua`

Relevant structures and functions:

- `automaticContainerSortingEnabled = false`;
- `enforceManualContainerOrder()`;
- `init()`;
- `sortContainerItems(container, sortMode)`;
- `onContainersMenuAction(actionId)`;
- `onContainerOpen(container, previousContainer)`;
- `toggleContainerPages(containerWindow, pages)`.

Final behavior:

- automatic sorting is disabled in source;
- `useManualSortMode` is forced to `1`;
- `currentSortMode` is forced to `none`;
- `sortContainersFirst` is forced to `0`;
- `sortNestedContainers` is forced to `0`;
- old configurations are migrated during `init()`;
- new users start without automatic sorting;
- menu actions cannot reactivate sorting;
- the sorting `contextMenuButton` is hidden;
- remaining buttons were repositioned to avoid an empty gap.

Settings are written only when one of these values needs correction.

### Container Patch Decisions

No protocol fix or full refresh for every incremental update was introduced.

In particular:

- `onContainerUpdateItem(...)` still directly updates the server-provided
  slot;
- `refreshContainerItems(container)` remains in its normal container size
  change flow;
- no special fluid conversion was added to the protocol;
- no TFS or OTClient C++ file was changed.

The adopted solution removes the source of the desynchronization instead of
masking it with full container refreshes.

### Error When Pouring Fluid

After the container fix, the following error was found when using a fluid on
a floor tile or ordinary object:

`data/global.lua:101: bad argument #1 to 'pairs' (table expected, got nil)`

The error originated from this `fluids.lua` flow:

- the script called `table.contains(distillery, target.itemid)`;
- the `distillery` table was never declared;
- any ordinary target that was not a player, fluid container, or fluid source
  could reach this branch;
- `table.contains` received `nil` and failed inside `pairs`.

Corrected files:

- `server/data/actions/scripts/other/fluids.lua`;
- `sources/nekiro-tfs-1.5-7.72/data/actions/scripts/other/fluids.lua`.

The correction removed only the legacy distillery branch. Its items and
mechanic do not exist in the current datapack, so the branch was dead code
and not applicable to this project.

Preserved behavior:

- drinking mana fluid;
- drinking life fluid;
- transforming the used container into an empty vial;
- transferring fluid between containers;
- filling a container from a fluid source;
- pouring fluid on a floor tile or ordinary object;
- creating the corresponding splash;
- reporting `It is empty.` when an empty container is used on an ordinary
  target.

The runtime and source copies of `fluids.lua` are byte-for-byte identical.

### Validation

- Xodet opened the graphical trade interface.
- `mana fluid` and `life fluid` names were displayed correctly.
- Final prices were validated: `40 gp` and `50 gp`.
- Purchased items arrived in the backpack with the correct fluid and color.
- The legacy shop preview correctly distinguished mana fluid and life fluid.
- Automatic sorting was disabled and container behavior was validated in
  game.
- The sorting button no longer appears.
- Old configurations using `sortAscByName` are neutralized at startup.
- TFS loaded all scripts and reached
  `Tibia Oldschool 7.72 Test Server Online!`.
- In-game validation confirmed that fluid can be poured on the floor without
  a Lua error.

### Source And Distribution Persistence

Permanent fixes are stored in source files:

- `sources/otclient-redemption/modules/game_npctrade/controllers/npc_legacy_ui.lua`;
- `sources/otclient-redemption/modules/game_containers/containers.lua`;
- `sources/nekiro-tfs-1.5-7.72/data/actions/scripts/other/fluids.lua`.

The server runtime was also updated:

- `server/data/actions/scripts/other/fluids.lua`.

Rebuilding the OTClient executable preserves these changes because its Lua
modules remain in the source tree and are part of the client distribution.

There is no second copy of these modules in a build directory that needs to
be synchronized. A new distribution must continue to include `modules`,
`data`, `mods`, and `init.lua`.

The change made to the current user's `config.otml` was only for local
testing. The guarantee for new users and old configurations is implemented
in `containers.lua`.

### Preserved Limits

- No NpcSystem change.
- No `modules.lua` change.
- No protocol change.
- No DAT, SPR, or CWM change.
- No sprite change.
- No RealOTX compatibility layer.
- No RealOTX NPC code was ported.
- No dedicated Lua script was created for Xodet.
- No other NPC received an individual visual fluid fix.

## June 22, 2026 Update

### Clickable Keywords In NPC Dialogue

The reusable logic for clickable NPC keywords was recorded and consolidated for
future NPC work without relying on legacy NPC private chat behavior.

Changed files:

- `sources/otclient-redemption/modules/game_console/console.lua`;
- `sources/otclient-redemption/modules/game_npctrade/controllers/npc_dialog.lua`.

Documentation file created:

- `docs/NPC_KEYWORDS_CLICKAVEIS.md`.

Adopted functional rule:

- any word or expression wrapped in `{}` inside an NPC line can be displayed as
  a clickable keyword in the console;
- clicking it makes the client send the keyword as a normal player message;
- in `7.72`, the send path uses `SAY`, not `NpcTo`;
- NPC speaker detection in the `SAY` flow uses the real speaker present on the
  map;
- final verification checks `isNpc()`, name, and position;
- overhead NPC text continues to be shown without curly braces.

Technical motivation:

- the current TFS speaks to players using `TALKTYPE_SAY`;
- the legacy `NpcFrom/NpcTo` flow is not the correct base for this project;
- the solution had to preserve the current architecture and avoid a
  compatibility layer.

Pattern for future NPCs:

- mark only relevant keywords as `{keyword}`;
- reuse the same current client infrastructure;
- do not create new protocol behavior for this;
- do not change the NpcSystem on a per-NPC basis.

Validation performed:

- the client started without Lua errors after the final fix;
- the earlier detection based on `g_creatures` was discarded because that
  global does not exist in this Lua runtime;
- the final version now uses `g_map` spectators to confirm NPC speakers in the
  `SAY` flow.

## June 22, 2026 Update - Clickable Keywords

Work started to mark NPC speech with `{keyword}` so the client can render
clickable links.

Files adjusted in this step:

- `server/data/npc/Benjamin.xml`;
- `server/data/npc/Captain.xml`;
- `server/data/npc/Gamon.xml`;
- `server/data/npc/Luna.xml`;
- `server/data/npc/Quentin.xml`;
- `server/data/npc/Quero.xml`;
- `server/data/npc/Suzy.xml`;
- `server/data/npc/Wyat.xml`;
- `server/data/npc/Lynda.xml`;
- `server/data/npc/Oswald.xml`;
- `server/data/npc/scripts/Lynda.lua`;
- `server/data/npc/scripts/Quentin.lua`.

Applied rule:

- only relevant words or expressions received `{}` in the final speech text;
- technical names, paths, and identifiers were not altered;
- the goal is to reuse the same clickable-keyword logic for future NPCs.
