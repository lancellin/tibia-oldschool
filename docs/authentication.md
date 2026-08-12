# Account password authentication

## Contract

`accounts.password` is the shared password-verifier field for the TFS and the
future website. It is `VARCHAR(255) NOT NULL`. `accounts.secret` is unchanged
and is reserved for a later TOTP/2FA phase.

New or changed passwords must be stored as a standard encoded Argon2id PHC
string:

```text
$argon2id$v=19$m=65536,t=3,p=1$<base64 salt>$<base64 hash>
```

The production policy is:

| Setting | Value |
| --- | ---: |
| Variant | Argon2id |
| Argon2 version | 19 (0x13) |
| Memory cost | 65536 KiB (64 MiB per concurrent hash) |
| Time cost | 3 |
| Parallelism | 1 |
| Salt | 16 CSPRNG bytes |
| Output | 32 bytes |
| Encoded storage capacity | 255 characters |

The TFS uses `libargon2`'s encoded APIs. Salt bytes come from Crypto++
`AutoSeededRandomPool`; gameplay RNG is not used.

The build dependency is installed in the project's classic vcpkg tree with:

```powershell
D:\tibia-oldschool\tools\vcpkg\vcpkg.exe install argon2[hwopt]:x64-windows
D:\tibia-oldschool\tools\vcpkg\vcpkg.exe install argon2[hwopt]:x86-windows
```

CMake links `unofficial::argon2::libargon2` and app-local deployment copies
`argon2.dll` beside the executable. Any manual Visual Studio deployment must
also place the matching-triplet DLL beside `tfs.exe`.

## Strict format detection

The verifier classifies storage before doing expensive work:

1. A syntactically valid `$argon2id$` PHC value with version 19 and bounded
   parameters is modern. The encoded value's own parameters are used by
   `argon2id_verify`.
2. Exactly 40 hexadecimal characters is legacy SHA-1.
3. Everything else is invalid and authentication fails closed.

Accepted PHC parameters are deliberately bounded (`m <= 262144 KiB`, `t <= 10`,
`p <= 8`, salt 8-64 bytes, output 16-64 bytes). This prevents a corrupted or
hostile database value from turning verification into unbounded resource use.
Passwords are case-sensitive. Authentication accepts 1-77 input bytes. This is
the common lossless limit imposed by the 128-byte RSA block in `ProtocolGame`
when combined with the server's 25-byte character-name limit; `ProtocolLogin`
alone could carry more. A future website should apply the same 77-byte limit,
must not trim or case-fold input, and should use the exact received byte string.
For new/change/reset flows, a product policy such as 12-77 bytes may be added
without changing the storage contract.

## Lazy migration and future rehash

After a successful legacy SHA-1 verification, the auth worker generates a new
Argon2id value and performs this logical compare-and-swap:

```sql
UPDATE accounts
SET password = :replacement_phc
WHERE id = :account_id
  AND password = :previous_value;
```

The Argon2 computation happens before the update and no database lock is held
while hashing. If the update affects zero rows, the worker reloads the current
value and verifies the presented password against it. Authentication continues
only when that new value also matches. A concurrent website password change can
therefore never be overwritten by a stale TFS migration.

The same path handles future policy changes: a valid Argon2id value whose
encoded parameters differ from the configured policy is verified using its own
parameters and then rehashed through the same compare-and-swap.

The migration `data/migrations/40.lua` widens the column without rewriting any
existing value. Existing SHA-1 accounts migrate one successful login at a time.

## Thread and queue model

Both protocol entry points create an `AuthenticationRequest` and submit it to a
dedicated, bounded `AuthenticationManager` pool:

```text
network I/O thread
  -> bounded authentication queue
  -> authentication worker (IP/account ban and namelock lookup, account DB
     lookup, verify, hash, CAS, ownership/list query)
  -> immutable AuthenticatedPrincipal
  -> Dispatcher completion
  -> character-list serialization or ProtocolGame::login
  -> existing PlayerIOManager asynchronous character load
```

The Dispatcher never performs the account query or Argon2 work. There is no
thread-per-login, unbounded queue, durable-save job, or Dispatcher fallback. A
full/stopped queue returns a temporary-login-unavailable response. Public
credential failures remain generic.

The legacy IP-ban query that ran on the network thread, plus account-ban and
namelock queries that ran in the Dispatcher after character materialization,
are now captured by the auth worker as immutable policy data. The Dispatcher
still applies `PlayerFlag_CannotBeBanned` after player materialization, so the
existing privilege semantics are preserved without another database call.
Transient MariaDB retries are bounded inside every auth worker; an outage
returns temporary unavailability instead of occupying the pool or delaying
shutdown indefinitely.

The auth pool is separate from `PlayerIOManager` and `player_io_service`, so
Argon2 cannot occupy logout/save workers. Shutdown stops admission, wipes queued
password buffers, drops queued work, waits for active auth workers, and then
continues the existing Player I/O drain. Defaults:

```lua
authWorkerThreads = 2
authQueueCapacity = 64
argon2MemoryCostKiB = 65536
argon2TimeCost = 3
argon2Parallelism = 1
```

The maximum intended Argon2 working set at the default concurrency is about
128 MiB. Raising the worker count multiplies memory cost and should only follow
a local benchmark and load test.

## Website implementation

The website must authenticate independently against MariaDB; it must not call
the TFS to create or verify a password.

PHP 8 password creation/change/reset:

```php
$hash = password_hash($password, PASSWORD_ARGON2ID, [
    'memory_cost' => 65536,
    'time_cost' => 3,
    'threads' => 1,
]);
// Store $hash verbatim in accounts.password.
```

PHP login:

```php
$valid = password_verify($password, $storedHash);
$rehash = $valid && password_needs_rehash(
    $storedHash,
    PASSWORD_ARGON2ID,
    ['memory_cost' => 65536, 'time_cost' => 3, 'threads' => 1]
);
```

If the website also supports the migration window, it should strictly recognize
40 hexadecimal characters, validate them with SHA-1 only for existing accounts,
and immediately replace a successful legacy value using the same compare-and-
swap rule. It must never create a new SHA-1 value. Hashes and raw passwords must
not be placed in logs, metrics, analytics, exception text, CSVs, or player-save
payloads.

MyAAC is not directly compatible as currently implemented: its account create,
login, change, and lost-password paths use its configured fast `encrypt()`
function and direct equality. It needs a relevant, coordinated adaptation of
all those paths to PHP's Argon2id APIs and the legacy migration/CAS contract.
A custom site is directly compatible when it follows the PHC and parameter
contract above. No ZnoteAAC implementation was evaluated for this change.

## Deliberate boundaries

- The 7.72 network protocol and OTClient were not changed.
- TOTP/2FA and login-to-game sessions were not implemented.
- `accounts.secret` was not read, repurposed, or migrated.
- The legacy Lua Account Manager remains unchanged because it is scheduled for
  deletion. Until it is removed, its SHA-1 account-writing path must not be
  exposed as a supported account-creation flow.
- The existing headless protocol test tool now redacts its `--password` value
  from generated `summary.json` files.
