# BESTIARY_CHANGELOG

Chronological record of the Bestiary project changes, tests, decisions, reversions, and pending work in `D:\tibia-oldschool`.

## Executive Summary

Bestiary development started from a partially reusable client base, but without a fully compatible server flow and without proper database persistence. The work advanced in stages, beginning with protocol and UI reuse assessment, followed by data modeling, kill tracking, parse adjustments, visual fixes, separation between regular creatures and bosses, and preparation of the Charms interface for logic controlled entirely by the server.

Throughout the project, the following principles were consolidated:

- the server is the source of truth for kills, thresholds, stages, and charm points;
- the client receives prepared data and only interprets/renders the sent state;
- the relevant persisted progress is the number of kills per player and creature;
- stage and charm points are derived values, not the primary persisted reference;
- the Cyclopedia UI was reduced to the scope required by the legacy client;
- compatibility with the OTClient build used by the project took priority over parity with modern implementations.

The implemented flow at this stage covers:

1. Bestiary creature registration and persistence;
2. per-player kill counting;
3. real-time stage derivation;
4. overview and detail delivery through the protocol;
5. progressive unlocking of visuals and loot;
6. separation between `Creatures` and `Bosses`;
7. display of charm points computed on the server;
8. enablement of the `Charms` tab in reduced Cyclopedia mode.

## Phase 1. Technical Survey And Base Reuse

### Initial scope

- An analysis was performed to identify what already existed in TFS and OTClient regarding Bestiary, Cyclopedia, and Charms.
- The initial goal was not to replicate the full Cyclopedia, but to build a functional Bestiary within the constraints of the current codebase.
- It was also defined that the flow should prioritize lookup and progression, leaving detailed charm activation for a later phase.

### Tools and files inspected

- Server:
  - `sources/nekiro-tfs-1.5-7.72/src/protocolgame.cpp`
  - `sources/nekiro-tfs-1.5-7.72/src/protocolgame.h`
  - `sources/nekiro-tfs-1.5-7.72/src/game.cpp`
  - `sources/nekiro-tfs-1.5-7.72/src/game.h`
- Client:
  - `sources/otclient-redemption/src/client/protocolgameparse.cpp`
  - `sources/otclient-redemption/src/client/protocolgamesend.cpp`
  - `sources/otclient-redemption/src/client/staticdata.h`
  - `sources/otclient-redemption/modules/game_cyclopedia/game_cyclopedia.lua`
  - `sources/otclient-redemption/modules/game_cyclopedia/game_cyclopedia.otui`
  - `sources/otclient-redemption/modules/game_cyclopedia/tab/bestiary/bestiary.lua`
  - `sources/otclient-redemption/modules/game_cyclopedia/tab/bestiary/bestiary.otui`
  - `sources/otclient-redemption/modules/game_cyclopedia/tab/charms/charms.lua`
  - `sources/otclient-redemption/modules/game_cyclopedia/tab/charms/charms.otui`
- Text search with `rg`.

### Initial conclusions

- The client already contained Bestiary parsing and UI structures, even if part of the flow was hidden or incomplete for the version in use.
- The server had enough extension points to build the required delivery flow.
- The implementation had to be guided by protocol behavior and real client compatibility, not only by function names.

### Decisions taken

- The project would be implemented in layers: persistence and kill tracking first, then protocol, then UI.
- The Charms flow would not depend on direct client-side validation of points.
- The database would not store a final aggregated charm point balance; the total would be computed by the server from progression data.

## Phase 2. Data Modeling And Initial Bestiary Structure

### Tables introduced

- `bestiary_monsters`
- `player_bestiary_progress`

### Modeling goals

- Maintain a central Bestiary creature registry with thresholds and rewards.
- Separate creature configuration from individual player progress.
- Allow future threshold changes without depending on client-side hardcoded rules.

### Consolidated structure

`bestiary_monsters` stores:

- `creature_id`
- `name`
- `kills_stage_1`
- `kills_stage_2`
- `kills_stage_3`
- `charm_points`
- `enabled`

`player_bestiary_progress` stores:

- `player_id`
- `creature_id`
- `kills`
- `last_stage_reached`
- `created_at`
- `updated_at`

### Tools used

- SQL in `server/schema_bestiary.sql`
- direct MariaDB commands for loading and test adjustments

### Modeling decisions

- `creature_id` was adopted as the internal Bestiary identifier, independent from sprite `Client ID`.
- The initial creature list was organized alphabetically while keeping room for future out-of-order additions.
- Threshold configuration stayed in the database, not hardcoded in the client.
- Some custom or technical entries were preserved in the structure, but disabled when necessary.

### Later adjustments

- The initial threshold set `25 / 250 / 1000` was replaced with `100 / 1000 / 2500`.
- Test creatures received specific configuration, such as `dragon` with `charm_points = 10`.
- Invalid, raid-like, clone, or visually unsupported entries were filtered or disabled over time.

## Phase 3. Kill Tracking, Thresholds, And Diagnostic TalkActions

### Initial implementation

- The server kill hook started recording per-player progress in `player_bestiary_progress`.
- Updates were bound to the killed monster and its corresponding Bestiary `creature_id`.

### Strategy discussion

During the early phase, the implementation strategy was evaluated around whether the server should:

- validate thresholds on every kill;
- validate in intervals;
- or defer checks until the interface was opened.

### Adopted logic

- Kills are always persisted at death time.
- Stage promotion does not depend on the client opening the interface.
- Validation was later shaped around relevant thresholds, avoiding unnecessary checks after the final stage.

### Problems found

- In early testing, `last_stage_reached` produced incorrect or inconsistent values.
- A visible case occurred where the field alternated between improper values such as `49` and `1`.
- This demonstrated that the persisted stage state could not be used as the absolute reference for display.

### Temporary support tool

- A diagnostic TalkAction `!bestiary <monster name>` was introduced.
- It allowed inspection of:
  - `creature_id`
  - current kills
  - configured thresholds
  - persisted stage
  - reward/charm points

### Consolidated decision

- `kills` became the truly relevant persisted value.
- `last_stage_reached` remained only as auxiliary data.
- Visible stage and rewards should be recalculated from kills and current thresholds.

## Phase 4. First Bestiary Protocol Flow

### Goal

- Connect database and kill tracking to the client using the Bestiary flow already present in OTClient.

### Central server functions adjusted

- `sendBestiaryRaces()`
- `sendBestiaryOverview(...)`
- `sendBestiaryMonsterData(...)`
- `sendBestiaryCharmsData()`

### Existing client parsers

- `GameServerBestiaryRaces`
- `GameServerBestiaryOverview`
- `GameServerBestiaryMonsterData`
- `GameServerBestiaryCharmsData`

### Data sent in overview

- `creature_id`
- current progress
- creature name
- outfit for rendering

### Data sent in detail

- kills
- thresholds
- class/difficulty
- loot
- text and complementary information

### Protocol decision

- The server had to strictly respect field order and conditional fields expected by the 7.72/OTClient build used by the project.
- Compatibility was treated as part of the system's functionality, not merely as a parse detail.

## Phase 5. Parse Fixes And Progress Semantics

### Critical error observed

- Opening the Bestiary or entering the creature tab caused a client parse error:
  - `InputMessage eof reached`

### Cause

- The payload sent by the server did not exactly match what the client expected when the creature was still hidden or had no revealed progress.

### Applied correction

- The occurrence byte started being sent only when `currentLevel > 0`.

### Impact

- Overview and detail parsing became aligned again.
- The client stopped breaking when opening the window with undiscovered creatures.

### Consolidated reveal logic

- `0 kills` -> hidden creature
- `1+ kill` -> revealed creature
- later thresholds control loot rarity unlocks and other progression states

### Stage reinterpretation

- The first kill became a visual reveal event rather than a completed threshold stage.
- Loot unlocking was adjusted around this meaning, separating discovery from formal stage completion.

## Phase 6. Sprite, Outfit, And ThingType Debugging

### Symptoms observed

- Valid creatures missing sprites in Bestiary.
- Incorrect name capitalization, such as unexpected uppercase letters inside names.
- Lua and client errors while trying to render invalid outfits.
- Cases where a creature appeared normally in the game world, but not in Bestiary.

### Diagnosis performed

The following were compared:

- the outfit sent by the normal in-map creature flow;
- the outfit sent specifically by Bestiary;
- the `lookType` coming from `MonsterType`;
- the `Outfit` object built by the client parser;
- the `widget.Sprite:setOutfit(...)` call site in `bestiary.lua`.

### Relevant findings

- Some creatures only rendered in Bestiary after appearing on the map.
- This indicated dependency on `ThingType` loading or outfit availability in the client.
- There were cases of `outfit.type == 0`, which naturally broke graphical rendering.
- The issue was not limited to `Elf Scout`; other entries also had inconsistencies.

### Tools used

- Temporary logs on server and client.
- Temporary validations in `protocolgame.cpp`, `protocolgameparse.cpp`, and `bestiary.lua`.
- Comparisons between registered names and actual monster names.

### Applied corrections

- Name formatting fixes.
- Deactivation or cleanup of invalid entries.
- Defensive validation before `setOutfit(...)` in Lua.
- Targeted adjustments/fallbacks during diagnosis, later reduced or removed once the main flow was stabilized.

### Reversions and conclusions

- Over-simplified fallbacks that destroyed the original outfit structure were discarded.
- The principle of preserving the original outfit structure whenever possible was kept.
- Bestiary was aligned to use the same valid outfit type already accepted by the client in the normal creature flow.

## Phase 7. Name Handling, Creature List Cleanup, And Invalid Entries

### Problems observed

- Inclusion of entries that should not belong to the playable Bestiary.
- Raid variants, technical clones, and experimental creatures appearing together with the real list.
- Some legitimate creatures had been removed incorrectly during early cleanup.

### Adopted guidelines

- Remove only entries that were actually invalid, technical, or undesired.
- Re-add bosses and real creatures that had been removed solely because of sprite or naming issues.
- Treat known exceptions without compromising the global list structure.

### Cases handled

- Removal of variants such as:
  - `orcraid`
  - `orcwarlordraid`
  - `slime2`
  - `demongoblin`
- Removal of `butterflys` from Bestiary.
- Reassessment of bosses and special creatures such as:
  - `Demodras`
  - `Necropharus`
  - `The Horned Fox`
  - `The Old Widow`
  - `Yeti`
  - `Orshabaal`
  - `Elf Scout`

### Final logic of this stage

- The fact that a boss uses a sprite similar to a regular creature is not a reason to exclude it.
- The main criterion became: the creature exists on the server, has a valid playable identity, and can be supported by the client visual flow.

## Phase 8. Loot, Client IDs, Containers, And Rarity Classification

### Initial problem

- Bestiary loot items were rendered with incorrect sprites.

### Cause

- The server was sending the `server item id`.
- The client needed the item `clientId` to render the correct sprite in OTClient.

### Applied correction

- Loot delivery in `sendBestiaryMonsterData(...)` was changed to use `Item::items[loot.id].clientId`.

### Additional problem

- Bags and containers defined in XML only as wrappers were being shown as real loot.

### Display decision

- Bestiary should not show the `bag/container` wrapper when it exists only to group inner drops.
- Displayed loot should reflect the contained items, not the technical wrapper itself.

### Consolidated rarity classification

- `chance >= 20000` -> `Common`
- `chance >= 7100` -> `Uncommon`
- `chance >= 2000` -> `Semi-Rare`
- `chance >= 500` -> `Rare`
- `chance >= 100` -> `Very Rare`
- `chance < 100` -> `Extremely Rare`

### Notes

- Classification was adjusted to better reflect the actual chance sent by the server.
- The `Extremely Rare` tier was introduced later without changing monster loot tables.

## Phase 9. Discovery Progression, Silhouette, And Information Locking

### Functional goal

- Undiscovered creatures should appear in a limited form in Bestiary.
- The player should only see complete information after discovering or progressing that creature.

### Consolidated rules

- Before the first kill:
  - name displayed as `Unknown Creature`
  - visual hidden/silhouetted
  - detailed information not shown
- After the first kill:
  - normal sprite
  - real name
  - basic information unlocked

### Loot progression rules

- discovered without full threshold completion:
  - creature revealed
  - rarer loot remains hidden
- later thresholds:
  - gradual unlock by rarity range

### Problems observed

- The black silhouette did not work correctly in the first attempt.
- In another iteration, loot disappeared after the first kill.
- The `mitigation` field could be absent, causing Lua errors through concatenation with `nil`.

### Applied corrections

- Safe handling for `mitigation == nil`.
- Review of the loot filtering logic so valid lists were not erased after discovery.
- Adjustments so creature reveal happened after the first kill without depending on the first formal threshold.

### UI decision

- When blocked, loot should not reveal the real name or sprite.
- Placeholders should preserve grid structure without leaking visual information.
- For undiscovered creatures, the main creature placeholder was kept as the primary visual indicator, without excessive noise.

## Phase 10. Stage Recalculation And Charm Point Totals

### Reason for the change

- If thresholds or charm point values change in the database, the system must reflect the new state without depending on outdated stored history.

### Problem observed

- A player with intermediate kills could retain charm points that should only exist after completing the final stage under the previous rules.

### Consolidated correction

- Stage started being recalculated from `kills` and current thresholds.
- Total charm points started being summed in real time based only on creatures actually complete under the current rules.
- `sendBestiaryCharmsData()` and the progression logic were aligned with that principle.

### Positive consequences

- Future threshold changes are reflected correctly.
- Future per-creature `charm_points` changes are also recalculated.
- The database does not need to store an aggregated balance that could diverge.

### Tests and auxiliary data

- Temporary player progress adjustments were applied to accelerate high-threshold tests.
- Example: `dragon` progress was artificially raised to validate transition near the final stage.

## Phase 11. Separation Between Creatures And Bosses

### Problem

- All creatures appeared in a single list.
- Bosses needed to leave the common list and appear in a separate category.

### Classification sources

- `data/monster/monsters.xml`
- folder `data/monster/Bosses`
- data loaded into `MonsterType`

### Adopted logic

- The server started classifying bosses primarily by `monsterType->info.isBoss`.
- A fallback based on XML path/listing was kept where origin data helped preserve classification.

### Result

- `Creatures` became restricted to regular creatures.
- `Bosses` started listing bosses only.
- Duplication between tabs was removed.

### Complementary adjustment

- Bestiary search was expanded to consider both tabs, not only the last visited category.
- Even undiscovered creatures can still be found by name, appearing as `Unknown Creature` when necessary.

## Phase 12. List UI And Navigation Adjustments

### Problems addressed

- redundant textual `Back` button;
- incorrect creature total on the initial screen;
- page indicator returning to `1/x` after leaving detail, even while the list remained on the previous page;
- need to hide undiscovered creatures.

### Applied corrections

- The textual `Back` button was removed/hidden, keeping only the existing arrow.
- The initial screen total stopped using an incorrect fixed value and began reflecting the real count of enabled creatures.
- The current page index was preserved when opening detail and returning.
- The `Hide Unknown` option was created to hide undiscovered creatures in the current tab.

### Implementation notes

- `Hide Unknown` acts on the displayed list without changing real progression data.
- Search and navigation needed to coexist with different page, filter, and active tab states.

## Phase 13. Charms Interface Preparation And Removal Of Old Elements

### Project direction

- The project moved away from creature-specific charm assignment as the main rule.
- This made the old charm selection block in the Bestiary detail screen unnecessary.

### Adjustments applied

- The `Charm Selection` block in the creature area was removed/hidden.
- The area that previously showed `gold` in the reduced Cyclopedia footer was visually repurposed to show charm points.
- The old gold logic was not deleted, only left separate for possible future reuse.

### Important technical decision

- Charm point display in the UI must consume a total computed on the server.
- The client must not decide how many points the player has.

### State at this stage

- The displayed total started coming from the server flow.
- The existence of more than one visual element with the charm icon required cleanup to avoid duplication.

## Phase 14. Enabling The Charms Tab In Reduced Cyclopedia

### Goal

- Keep the Cyclopedia in reduced mode, but no longer limit navigation to Bestiary only.
- Expose the `Charms` tab as well for evaluation and future adaptation.

### Central files

- `modules/game_cyclopedia/game_cyclopedia.lua`
- `modules/game_cyclopedia/game_cyclopedia.otui`
- `modules/game_cyclopedia/tab/charms/charms.lua`

### Visual and behavioral fixes

- The `Charms` tab was enabled in the reduced flow.
- Only `Bestiary` and `Charms` remained visible in that mode.
- The active/inactive tab state started being explicitly controlled.
- Both buttons were expanded so the clickable area covered icon and text.
- Problems were fixed where:
  - only the icon was clickable;
  - the text disappeared on hover;
  - both tabs looked pressed at the same time;
  - the inactive tab shrank back to an icon-only appearance;
  - a visual divider appeared between icon and text on the inactive tab.

### Implementation details

- Automatic `UIButton` states were reduced for those specific tabs.
- Visual state started depending on overlays and Lua-side control.
- Font, clipping, and auxiliary background adjustments were required to stabilize layout on the OTClient build in use.

### Functional pending item

- The `Charms` tab still requires content review and possible simplification, because the final charm rule of the project differs from the classic per-creature model.

## Phase 15. Initial Charms Tab Restructuring

### Goal of this stage

- Replace the old creature-specific charm flow with an initial layout aligned with the new project model.
- Prepare the interface for lookup, visual selection, and future unlocking without yet integrating combat, database persistence, or the final application logic.

### Scope decisions

- The `Charms` tab started using mock/hardcoded client-side data to validate layout and interaction.
- The visible charm point total continued to come from the server, preserving backend authority.
- The old `Select Creature` flow stopped being functionally relevant at this stage and was removed from the interface.

### Structural UI changes

- The creature selection block was removed from the `Charms` tab, including:
  - the `Select Creature` label;
  - the creature list;
  - the search field;
  - interactions tied to creature selection.
- The left panel was reworked to concentrate:
  - charm name;
  - selected icon/rune;
  - status;
  - effect;
  - chance;
  - area damage reduction, when present;
  - unlock cost;
  - descriptive text.
- The right panel started rendering a charm grid with four visual columns.

### Charm cards

- Each charm started being displayed in its own card containing:
  - a name at the top;
  - a centered icon;
  - a cost at the bottom;
  - a distinct visual state for `locked`, `unlocked`, and `selected`.
- Card selection started updating the left panel in real time.
- The `Unlock` button started respecting both the selected charm state and point availability.

### Mock data introduced

- Real-named charms were kept as anchor examples, such as:
  - `Wound`
  - `Enflame`
  - `Freeze`
  - `Mana Spring`
  - `Savage Blow`
- Additional fictional charms (`Charm 2` through `Charm 11`) were later added to fill the grid and validate line breaks, selection, and scrollbar behavior.
- `Wound` was fixed at a cost of `600`.

### Later layout adjustments

- The `Lore` label was replaced with `Description`.
- The `Cost:` row in the left panel was removed.
- The former `Points` block was converted into `Unlock Cost`, showing the cost of the currently selected charm.
- The overall `Cyclopedia` window height was increased to create more usable vertical space without changing the main structure.
- The `Available Charms` grid received its own visual centering inside the charms panel.

### Technical problems encountered

- The first version of the layout produced errors because of mismatches between OTUI IDs and Lua references.
- Some widgets existed, but were nested inside containers while Lua still tried to access them as direct children.
- Strings containing `%` in the wrong context caused the `invalid option '%' to 'format'` error.
- The descriptive charm block required several iterations before width, wrapping, and scroll area behavior became usable.

### OTUI/Lua contract correction

- An explicit UI contract validation was introduced when loading the tab.
- Instead of letting errors surface later as `nil` accesses, the screen started validating the expected widgets up front.
- Generic or ambiguous IDs were renamed to layout-specific charm IDs.

### Widgets and references aligned

IDs that were introduced or aligned include:

- `SelectedCharmIcon`
- `SelectedLockedMask`
- `StatusValue`
- `EffectValue`
- `ChanceValue`
- `AoeValue`
- `UnlockCostValue`
- `HistoryText`, later replaced with `DescriptionText`
- `CardName`
- `CardIcon`
- `CardCostValue`
- `CardLockedMask`
- `CardStatusBar`
- `CardStatusLabel`

### Charm point balance

- The total displayed in the `Charms` tab did not remain tied to the mock state.
- The screen was reconnected to `charmsData.points` and `Cyclopedia.StoredBestiaryCharmPoints`, preserving real server-side calculation.
- Local unlocking remained only as a visual layout simulation and did not redefine backend authority.

### Relationship with Bestiary

- Grid centering was not applied to the main `Bestiary` panel.
- A later rule was established:
  - the main `Bestiary` screen keeps its original alignment;
  - internal `Creatures` and `Bosses` lists may be centered inside their own list panel;
  - `Available Charms` centering remains restricted to the charms panel.

### Reversions and refinements

- There was an early attempt to center Bestiary grids too broadly; that change was reverted and restricted to the correct contexts.
- There were also intermediate attempts to render description text through simpler label setups, later replaced by a text block with controlled width.

### Result of this stage

- The `Charms` tab reached a functional initial layout ready for future evolution.
- The old creature selection flow stopped interfering visually.
- The client now has a clearer base for future integration of the real charms system with server and combat logic.

## Phase 16. First Real Charms System Integration

### Adopted scope

- Only the first functional Tier 1 version was implemented.
- Tier 2, Tier 3, and chance upgrades remained outside this phase.
- The first real charm was named `Savage Blow`.
- Its initial cost was provisionally set to 10 charm points so it can be tested with the current Bestiary economy.

### Authority and persistence

- The client no longer unlocks charms locally.
- The server now validates balance, cost, state, and transitions.
- The `player_charms` table was introduced with one key per player and charm.
- Persisted states are:
  - `0`: locked;
  - `1`: unlocked but inactive;
  - `2`: active.
- Available balance is recalculated as points earned from completed Bestiaries minus the cost of unlocked charms.
- No fixed aggregated balance column was added to the player.

### Central bonus structure

- A central `CharacterBonuses` structure was added to the `Player` object.
- The structure separates charm bonuses from equipment bonuses.
- Fields were prepared for critical chance, critical damage, skills, hit chance, and mana leech.
- Recalculation occurs on player load, charm state changes, and equipment special skill changes.

### Tier 1 critical charm

- Active `Savage Blow` grants a 4% critical chance.
- Critical damage uses the normal maximum hit as its base.
- Additional variation is rolled between 0% and 15% of the maximum hit.
- The effect applies only against PvE creatures.
- Players and player-controlled summons are excluded.
- Condition ticks, fields, poison, burning, reflect, environmental damage, and non-direct origins cannot trigger the charm.
- Accepted origins are melee, ranged, wand, and spell.
- The critical value enters before target mitigation, preserving armor, defense, immunity, and resistance handling.
- Charm critical remains separate from the pre-existing equipment critical special skills.

### Client/server protocol

- Existing request `0xE4` now handles charm unlocking.
- Packet `0xD8` now sends:
  - available charm points;
  - real charm definitions;
  - locked, unlocked, or active state;
  - cost, icon, and chance;
  - an extensible list of real character bonuses.
- The server sends updates on login, Bestiary requests, charm changes, and relevant equipment changes.

### Activation NPC

- The `Charm Master` NPC was prepared.
- The NPC requires the charm to have already been unlocked with charm points.
- Initial activation cost is configured as 10,000 gold and 1 crystal coin.
- Charging, activation, recalculation, and client refresh all happen on the server.
- The NPC data files exist, but the NPC still needs to be placed on the map.

### Character Bonuses

- A `Character Bonuses` area was added below the equipment panel.
- The panel consumes only bonuses sent by the server.
- While the charm is active it displays:
  - `Critical Chance: 4%`;
  - `Critical Damage: Max Hit + 0-15%`.
- Dynamic rows allow future bonus sources to be added without replacing the panel.

### Validation performed

- The `player_charms` table was applied and verified in the active database.
- TFS and OTClient compiled successfully.
- Executables were published to operational paths and `build-results`.
- TFS completed startup without errors and the smoke-test process terminated cleanly.
- The client started without new Lua or OTUI errors.

## Phase 17. Current State And Pending Work

### Current functional state

- Bestiary with per-player, per-creature persistence.
- Kills counted on the server.
- Stage derived from current kills.
- Loot with correct sprite via `clientId`.
- Technical containers removed from loot display.
- Rarity adjusted and expanded with `Extremely Rare`.
- Initial discovery through first kill.
- Separation between `Creatures` and `Bosses`.
- Search considering both tabs.
- `Hide Unknown` working.
- Total charm points calculated on the server and displayed in the client.
- `Charms` tab enabled in reduced mode.
- `Charms` tab consuming real definitions, costs, and states sent by the server.
- First Tier 1 critical charm integrated into PvE combat.
- `Character Bonuses` panel receiving real server-side bonuses.

### Recorded reversions and discarded approaches

- No permanent persistence of aggregated charm point balance was adopted.
- The creature-specific charm model was not kept in the Bestiary detail screen.
- Blind reliance on `last_stage_reached` as the source of truth was not kept.
- Simplified outfit fallbacks that destroyed the original structure were discarded.

### Open pending work

- Place the `Charm Master` NPC on the map.
- Validate the locked, unlocked, and active transitions in game.
- Measure a combat sample to confirm the practical 4% proc rate and maximum-hit-plus-0-15% damage range.
- Define final unlock and activation costs.
- Add new charms only after the first real case is stable.
- Revisit whether all remaining special creatures have consistent visual support in Bestiary.
- Evaluate whether the bosses interface needs extra indicators distinct from common creatures.
- Decide whether total charm points should also appear outside Cyclopedia.

### Consolidated project logic at the end of this stage

- The server calculates, validates, and authorizes.
- The client looks up, searches, and renders.
- The database persists configuration and raw progress.
- Rewards and derived states must always be recalculable whenever thresholds or values change.

## Phase 18. Real Charms, Critical Combat, Aggregated Bonuses, And Operational Tools

### Scope of this phase

- Real integration of the first charm with persisted `locked`, `unlocked`, and `active` state.
- Real insertion of critical strike handling into the server combat pipeline.
- Display of real character bonuses in a dedicated MiniWindow.
- UX adjustments for charm unlocking and documentation of central variables.
- Inclusion of operational helpers for repeated tests and administrative teleports.

### Main server files

- `sources/nekiro-tfs-1.5-7.72/src/player.h`
- `sources/nekiro-tfs-1.5-7.72/src/player.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/combat.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/protocolgame.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/iologindata.cpp`
- `sources/nekiro-tfs-1.5-7.72/data/npc/scripts/charm_master.lua`
- `sources/nekiro-tfs-1.5-7.72/data/talkactions/scripts/activate_charm.lua`
- `sources/nekiro-tfs-1.5-7.72/data/talkactions/scripts/teleport_home.lua`
- `sources/nekiro-tfs-1.5-7.72/data/talkactions/scripts/teleport_to_town.lua`
- `sources/nekiro-tfs-1.5-7.72/data/talkactions/scripts/teleport_to_temple.lua`

### Main client files

- `sources/otclient-redemption/modules/game_cyclopedia/tab/charms/charms.lua`
- `sources/otclient-redemption/modules/game_cyclopedia/tab/charms/charms.otui`
- `sources/otclient-redemption/modules/game_inventory/inventory.lua`
- `sources/otclient-redemption/modules/game_inventory/characterbonuses.otui`

### Relevant structures and variables

#### Server

- `PlayerCharmState` in `player.h`
  - enum responsible for `LOCKED`, `UNLOCKED`, and `ACTIVE`.
- `CharmDefinition` in `player.h`
  - static definition of each charm.
  - important fields:
    - `id`
    - `name`
    - `description`
    - `unlockCost`
    - `iconIndex`
    - `chance`
    - `criticalDamageMinPercent`
    - `criticalDamageMaxPercent`
- `CharacterBonuses` in `player.h`
  - central aggregate of derived character modifiers.
  - relevant fields in this phase:
    - `totalCriticalChance`
    - `equipmentCriticalChance`
    - `equipmentCriticalDamagePercent`
    - `charmCriticalChance`
    - `charmCriticalDamageMinPercent`
    - `charmCriticalDamageMaxPercent`
    - `manaLeechChance`
    - `manaLeechAmount`
- `charmStates` in `Player`
  - in-memory map of states persisted in `player_charms`.

#### Client

- `Cyclopedia.Charms.data` in `charms.lua`
  - local cache of charm definitions sent by the server.
- `Cyclopedia.Charms.points`
  - available balance already discounted by the server.
- `unlockConfirmWindow`
  - temporary confirmation popup before unlocking.
- `characterBonuses`
  - bonus list consumed by the `Character Bonuses` MiniWindow.

### Persistence and recalculation

- The `player_charms` table became the persisted source of unlock/activation state.
- Charm point balance remained derived:
  - `earnedPoints = completed bestiary rewards`
  - `spentPoints = sum of unlocked/active charms`
  - `availablePoints = earnedPoints - spentPoints`
- State loading happens in `Player::loadCharmStatesFromDatabase()`.
- A database read issue was fixed where `uint8_t` values could be interpreted as ASCII characters.
- The applied fix was to read `charm_id` and `state` as `uint16_t` first and convert afterward, preventing invalid in-memory charm states.

### Combat and critical strike

- The first real charm is `Savage Blow`.
- The central application point is `applyCharmCritical(...)` in `combat.cpp`.
- Eligibility still passes through `canApplyCharmCritical(...)`.
- The correct PvE/PvP separation uses the existing `Combat::isPlayerCombat(const Creature* target)` logic.
- This function treats the following as player combat:
  - `target->getPlayer()`
  - `target->isSummon() && target->getMaster()->getPlayer()`
- Consolidated real Stage I:
  - chance: `4%`
  - PvE: `max hit + 5-25%`
  - PvP: same proc chance, but only `50%` of the extra bonus
- Final PvP formula:
  - roll `bonusPercent`
  - compute `extraDamage = floor(maxHit * bonusPercent / 100)`
  - reduce to `floor(extraDamage * 0.5)`
  - final damage = `maxHit + reduced extraDamage`

### Display and aggregated bonuses

- The `Character Bonuses` window stopped relying on mock data and now consumes only server data.
- Current delivery is built in `ProtocolGame::sendBestiaryCharmsData()` in `protocolgame.cpp`.
- The list now includes:
  - `Critical Chance`
  - `Crit. Damage`
  - `Equipment Critical Damage`
  - `Mana Leech`
- `Critical Chance` now uses `CharacterBonuses.totalCriticalChance`, created to centralize future sums from additional sources.
- The client MiniWindow paints `Critical Chance` green when the value is `> 0`.

### Charms tab UX

- Before spending points on `Unlock`, the client now opens a confirmation popup in `charms.lua`.
- Unlock only calls `g_game.BuyCharmRune(...)` after confirmation.
- The `Effect` field for `Savage Blow` was visually decoupled from the long description and now displays:
  - `Critical chance / damage.`
- The long description was updated with:
  - supported attack types;
  - PvP adjustment;
  - textual preview of Stage I, II, and III;
  - charm lore.

### Temporary indicators and testing

- A temporary log was added in `combat.cpp`:
  - `[CharmCritical] player=... target=... maxHit=... bonusPercent=... extraDamage=...`
- Floating `CRITICAL!` text was added through `ColoredText` on the target when a proc occurs.
- The flow reuses the existing client `AnimatedText` infrastructure without introducing a new protocol path.

### Operational helpers added

- `reset_gm_lancellin_2490_dragons.bat`
  - external helper to repeat dragon unlock/activation tests.
- `!activatecharm savage blow`
  - test TalkAction that activates the charm and immediately calls `player:save()`.
- `/temple <destination>`
  - new administrative TalkAction for quick temple teleports by alias.
  - aliases introduced in this phase:
    - `ab`
    - `kazz`
    - `thais`
    - `venore`
    - `carlin`
    - `ank`
    - `edron`
    - `rook`
  - the implementation also falls back to `Town(param)` when a valid town name exists in the server data.

### Decisions and limits preserved

- No protocol change was introduced.
- No database change was introduced beyond the already adopted `player_charms` usage.
- No change was made to Bestiary Charm Point logic.
- Stage II and Stage III remain UI-only descriptions, without real combat implementation.
- `Character Bonuses` remains display-only; all validation still lives on the server.
