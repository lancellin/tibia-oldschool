# FLOOR_PERSISTENCE_CHANGELOG

Consolidated technical record of the incremental floor persistence,
post-crash recovery, quarantine, item-audit, and operational-control work in
the TFS workspace at `D:\tibia-oldschool`.

This document has an equivalent Portuguese version:
`FLOOR_PERSISTENCE_CHANGELOG.md`.

> Record status: the functional stages described below were implemented and
> validated locally. This does **not** declare the system ready for public
> launch: closed beta, final configuration, and operational hardening remain
> later stages.

## Executive Summary

An incremental floor persistence system was implemented for movable ground
items. Its goal is to preserve eligible bagloot and items across server saves
and reduce losses after a crash, without replacing the OTBM map, native player
saving, houses, depots, or the server's normal respawn logic.

The system tracks only changed tiles, serializes their state with versioned
metadata, and creates coordinated checkpoints between players and tiles. After
a clean shutdown, suitable snapshots are automatically replayed on the next
startup. After a crash, the server selects a recovery source, validates it
before changing the map, blocks common players, and requires an explicit GOD
apply-and-confirm action.

Main outcomes:

- eligible movable ground items can survive clean restarts and confirmed crash
  recovery;
- risky stackables are quarantined during crash recovery instead of being
  restored automatically;
- player corpses and `death_bundle` receive dedicated handling;
- houses, depots, and inventories remain under their native persistence paths;
- OTBM map items continue to be recreated by the map; recovered replay is
  added without overwriting the map base;
- manual weekly reset, clean save, crash-recovery, and emergency flows exist;
- a private read-only web panel supports quarantine investigation;
- `instance_id`, last-actor GUID, and signed CAM evidence support
  investigation without being treated as automatic proof of abuse.

## Consolidated Update — July 19 To 27, 2026

This changelog consolidates the stages implemented and tested during this
period. Final build dates, executable hashes, and the public-launch decision
are intentionally not fixed here because they still depend on production
preparation and closed beta.

## Scope And Safety Contract

The system was intentionally built with the following boundaries:

- it does not persist the full map after every change;
- it does not alter houses through the floor mechanism;
- it does not replace native player, locker, depot, inbox, or market saves;
- it does not partially restore an invalid snapshot set;
- it does not automatically apply a recovery classified as a crash;
- it does not directly restore crash stackables to the map;
- it does not attempt to preserve creature corpses or loot left inside them;
- it does not stop OTBM from recreating map items on startup;
- it does not use CAM, GUID, or `instance_id` as the sole basis for punitive
  action.

The system is additive to OTBM. OTBM remains the source of map geometry, base
items, and respawns. Snapshots represent only persistable changes made after
the world was loaded.

## Implemented Architecture

### 1. Item Identity, Classification, And Origin

Movable non-stackable items receive a persistent `instance_id` after creation
or insertion has completed successfully. The identity supports reconciliation,
duplication prevention during recovery, and investigation.

Covered flows include:

- GM creation of a movable non-stackable item;
- shop purchases;
- actual acquisition of a quest item with AID 2000/2001;
- an item removed from a creature, container, or corpse;
- insertion into a movable container;
- movement of a movable container with contents;
- the backpack delivered to a player on death, where applicable.

Movable containers are handled as subtrees. When they receive contents or are
moved, the container and its eligible contents are normalized so previously
unidentified items receive an `instance_id`. This supports backpacks nested
inside backpacks without relying on an artificial gameplay depth limit.

Stackables do not receive per-unit `instance_id`. For investigation, movable
items may also carry the GUID of the last player who handled them. The action
item is updated immediately; container-subtree propagation uses an 8-second
debounce with a 24-second maximum delay. This GUID is investigative evidence,
not proof of ownership.

### 2. Floor Persistence Policy

The implemented classification distinguishes, at a high level:

- houses: excluded from the floor mechanism;
- `death_bundle` and protected player corpses: special persistence;
- creature corpses: excluded;
- OTBM base items and immovable items: not serialized as floor changes;
- movable non-stackable items: normal persistence;
- configured foods: normal persistence;
- other stackables: `PERSIST_CLEAN_ONLY`, subject to crash quarantine.

Currently recognized food IDs are `2666..2691`, `2695`, `2696`, and
`2787..2796`. The city-position list currently present in code is a test list
and must be replaced by the real list before production.

Items placed in city areas remain available for crash protection according to
the recovery policy, but are handled by the cleanup policy during clean
save/weekly reset. Weekly reset removes persisted floor snapshots; it does not
clean houses, inventories, lockers, or depots.

### 3. Dirty Tracking, Serialization, And Checkpoints

Tiles become `dirty` after relevant item or container changes, including add,
remove, update, replacement, and internal container changes. Recurring system
events without a player origin are filtered so map effects, fields, or decay do
not create an endless snapshot queue.

Current local configuration:

- normal debounce: 15 seconds;
- maximum delay: 60 seconds;
- retry after failure: 5 seconds;
- maximum batch size: 32 tiles;
- world and generation: `1`/`1`.

When a movement involves both players and tiles, participants are grouped into
a coordinated checkpoint. Its purpose is to avoid saving an item in the player
and on the ground at incompatible moments. A checkpoint becomes durable only
after the required parts are written.

Every snapshot stores format version, policy version, position, item counters,
size, and SHA-256 checksum. Reading validates format, policy, size, checksum,
blob, position, counters, and required identities.

### 4. Clean Save, Clean Restart, And Weekly Reset

The coordinated clean save closes common access, saves players and floor
snapshots together, and keeps login blocked until restart. After a valid clean
checkpoint, the next startup enters `CLEAN_RESTART` and automatically replays
the selected floor state before allowing normal play.

Weekly reset is manual and explicit. It runs a clean save that atomically
empties persisted non-house floor snapshots. The base OTBM map loads normally
on the next startup; persisted floor items are not replayed.

The scheduled server save uses the same coordinated path. The process may
remain alive while the checkpoint is verified, with public access closed,
before the configured final shutdown.

### 5. Post-Crash Selection And Recovery

At startup, a previous session is classified as one of:

- `NOTHING_TO_RECOVER`;
- `CLEAN_RESTART`;
- `CRASH_RECOVERY`;
- `RECOVERY_BLOCKED`.

A clean restart requires a compatible atomic clean checkpoint. Empty sessions
created shortly before a new startup are ignored so they cannot hide a useful
previous source.

During `CLEAN_RESTART`, replay is automatic. During `CRASH_RECOVERY`, common
players remain blocked. The required flow is:

1. inspect selection and dry-run;
2. inspect reconciliation and quarantine;
3. explicitly apply the selected source;
4. inspect the restored map;
5. durably confirm the recovery;
6. only then allow common login and a new clean save.

Apply adds recovered items beside OTBM items without firing movement scripts or
creating new player dirty events. Apply cannot be repeated in the same process.

### 6. Reconciliation And Quarantine

Before applying crash recovery, the system compares floor identities with
player inventory, locker, depot, inbox, and store inbox. Houses and market are
outside this reconciliation policy by explicit decision.

Crash stackables are materialized in quarantine with their source blob retained
for context. Quarantine does not itself remove or restore items. It keeps risky
items out of automatic replay until human investigation.

The private read-only `admin\floor-quarantine-web` panel supports browsing by
stackable type, tile, container, source, temporal risk, and last-actor GUID.
It does not return, discard, alter items, or modify the game database.

### 7. Corpses, Decay, And Emergency

Recovered player corpses have decay paused while recovery is blocked. After
confirmation, only those corpses receive an additional 50 minutes; other items
resume their remaining decay time without extension.

Creature corpses do not participate in floor persistence. If an unlooted
creature corpse or its contents disappear after a crash, that is accepted
behavior. An item removed from that corpse enters the normal identity and
persistence flow.

The GOD command `!emergency` closes common access, disconnects normal players,
pauses decay, and blocks server saves. `!emergency finish` resumes decay, adds
50 minutes only to player corpses, and starts a coordinated clean save. The
automatic trigger based on packet absence was deliberately postponed.

### 8. CAM Evidence

The client received forensic CAM support for inspection of recorded items,
including GOD look. Evidence is signed in the packet flow and the client
refuses look when evidence is missing, incompatible, or tampered with. CAM is
an investigation tool; it cannot edit a replay or replace server confirmation.

Specific measurement of additional CAM packet frequency and bytes for an idle
character remains pending before closed beta.

## Relevant Administrative Commands

The commands below are GOD-only diagnostic/control tools. This changelog does
not replace the staff operating manual that will be written later.

- `/floorsnapshot status`, `here`, `front`, or `x,y,z`: runtime and snapshot
  inspection;
- `/floorsnapshot recovery`, `dryrun`, `reconcile`, `quarantine`: read-only
  recovery inspection;
- `/floorsnapshot apply confirm <source>`: apply crash recovery;
- `/floorsnapshot recoveryconfirm <source>`: confirm recovery after inspection
  and allow common access;
- `/floorsnapshot cleansave [5..3600]`, `cleansave status`, and `cleansave
  cancel`: immediate or scheduled clean save;
- `/floorsnapshot weeklyreset [5..3600] confirm`: explicit weekly reset;
- `/floorsnapshot flush` and `failnext`: controlled diagnostics/tests, not
  normal operation;
- `!emergency` and `!emergency finish`: manual emergency.

## Database, Code, And Main Artifacts

Core TFS source components:

- `sources\nekiro-tfs-1.5-7.72\src\floorpersistence.cpp`;
- `sources\nekiro-tfs-1.5-7.72\src\floorpersistence.h`;
- `game.cpp`, `game.h`, `item.cpp`, `item.h`, `container.cpp`, `container.h`,
  `protocolgame.cpp`, `luascript.cpp`, and `luascript.h` in the same source
  directory.

Lua/runtime layers:

- `server\data\lib\core\floor_persistence.lua`;
- `server\data\talkactions\scripts\floor_snapshot.lua`;
- `server\data\talkactions\scripts\floor_dirty.lua`;
- `server\data\talkactions\scripts\floor_inspect.lua`;
- `server\data\talkactions\scripts\emergency.lua`;
- `server\data\globalevents\scripts\serversave.lua`;
- `server\data\migrations\30.lua` through the later floor-persistence
  migrations;
- `server\schema.sql`.

The client CAM integration is concentrated in:

- `sources\otclient-redemption\modules\game_cam_forensics\camforensics.lua`.

Reference executables from before critical stages were preserved under
`server\backup_executables` and the `server` directory.

## Local Validation Performed

Manual tests covered, among others:

- item identity, GM creation, shop, quest, and player death;
- bags, nested bags, items between players, ground, inventory, locker, depot,
  mailbox, and parcel;
- coordinated player/tile checkpoints;
- simulated write failures and retry;
- clean save, cancellation, clean restart, and weekly reset;
- crash recovery, explicit apply, durable confirmation, and login block;
- stackable quarantine and the read-only web panel;
- replay beside OTBM items, including height order and containers;
- city tiles, houses, player corpses, death bundle, decay, and emergency;
- forensic CAM and valid-evidence item look.

Local TFS load measurements, using 12 logical processors, showed:

| Scenario | Mean total TFS CPU | Mean equivalent of one core |
| --- | ---: | ---: |
| 0 players | 0.027% | 0.32% |
| 1 idle player | 0.205% | 2.46% |
| 5 idle players | 0.195% | 2.34% |
| 5 players moving/following | 0.527% | 6.33% |
| 5 archers moving and fighting | 0.586% | 7.03% |

No private-memory growth was observed during the test windows. Starting 10 full
OTClients locally consumed roughly 11.2 GiB of private memory and was safely
stopped; 50–200 player load will be measured during closed beta rather than by
dozens of full clients on this machine.

## Known Limits And Deliberately Deferred Work

The items below are deliberately outside the completed scope and must not be
mistaken for a silent failure:

- automatic emergency trigger based on global packet interruption;
- investigative GUID integration with Player Shop;
- player-filled fixed OTBM containers;
- house and market reconciliation;
- administrative corpse recovery command;
- special rare-item investigation list;
- quarantine decision, return, or discard through the web panel;
- detailed measurement of extra CAM packets while idle;
- 50–200 player load during closed beta.

## Next Steps Before Production

1. Replace test city tiles with the real list and review final cleanup policy.
2. Replace temporary panel credentials and restrict access through network
   controls, HTTPS/VPN, and a read-only MariaDB user.
3. Produce a staff operating manual for clean save, weekly reset, crash
   recovery, emergency, quarantine, and rollback.
4. Create repeatable closed-beta telemetry.
5. Run closed beta with real load and validate CPU, memory, database, network,
   snapshots, and CAM limits.
6. Perform a clean Release build, backup, rollback plan, and final approval.

## Rollback Decision

Do not roll back by deleting snapshot rows or executables without a backup.
Before any reversal, preserve the current database and binary, stop new login,
and decide whether the return path is a previous executable, a new persistence
generation, or a backup restore. Preserved pre-stage binaries exist to support
that process.
