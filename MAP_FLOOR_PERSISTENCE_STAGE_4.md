# Floor Persistence - Stage 4 Recovery Selection

## Scope

Stage 4 captures and validates a read-only recovery plan during startup. It does not deserialize or apply any item to the map. Startup replay remains disabled.

No database schema change is required for this stage.

## Startup order

1. Load the configured floor persistence world and generation.
2. Capture the newest session that existed before the current startup.
3. Select the newest meaningful recovery source.
4. Validate the materialized snapshot set.
5. Store the immutable decision in memory.
6. Create the new `RUNNING` session.

A `RUNNING` session without snapshot rows or checkpoint records is ignored. This prevents a process that crashed immediately after startup from hiding the previous meaningful recovery source.

## Decisions

| Previous state | Recovery mode |
| --- | --- |
| No meaningful session and no snapshots | `NOTHING_TO_RECOVER` |
| `CLEAN_COMMITTED` with exactly one matching atomic clean checkpoint | `CLEAN_RESTART` |
| Meaningful `RUNNING` | `CRASH_RECOVERY` |
| `CLEAN_PREPARING` | `CRASH_RECOVERY` |
| `CLEAN_FAILED` | `CRASH_RECOVERY` |
| Unknown state or inconsistent data | `RECOVERY_BLOCKED` |

For a clean restart, the clean checkpoint must be committed, its tile count must match the save session, `committed_at` must exist, and the session must not contain an error.

## Snapshot validation

Every materialized row for the configured world and generation is read and checked for:

- snapshot format version;
- persistence policy version;
- stored byte count and the 8 MiB per-tile limit;
- SHA-256 checksum;
- snapshot header and complete blob structure;
- embedded tile position;
- decoded item and top-item counters;
- missing or invalid required item identities.

Any invalid row produces `RECOVERY_BLOCKED`. Stage 4 never attempts partial recovery.

## Administrative inspection

Use:

```text
/floorsnapshot recovery
```

The command reports:

- selected mode and source session;
- empty sessions ignored;
- source session and checkpoint counters;
- materialized row, item and byte totals;
- city-filtered rows, death bundles and player corpses;
- validation failures and the decision reason;
- `replay=no` and `apply=no`.

The plan is captured at startup and intentionally does not change while the current process runs.

## GM behavior after a clean save

Stage 4 does not restrict GM actions after `CLEAN_COMMITTED`. A GM may inspect or modify the world. Avoiding unnecessary modifications after the final clean checkpoint is an operational responsibility, as explicitly decided for this server.

## Initial tests

1. Start after `CLEAN_FAILED`; expect `CRASH_RECOVERY`.
2. Start after `CLEAN_COMMITTED`; expect `CLEAN_RESTART`.
3. Stop a meaningful `RUNNING` session without a clean save; expect `CRASH_RECOVERY`.
4. Start a fresh generation without rows; expect `NOTHING_TO_RECOVER`.
5. Validate that any deliberately inconsistent test row produces `RECOVERY_BLOCKED`.

Do not corrupt production or irreplaceable rows to exercise test 5. Use a disposable generation or a transactionally restored test row.
