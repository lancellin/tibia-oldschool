# BACKUP_RESTORATION_CHANGELOG

Chronological record of the changes, validations, build fixes, operational decisions, and generated artifacts produced during the conversation focused on environment restoration, full recompilation, and preparation of a reproducible cloud-ready backup in `D:\tibia-oldschool`.

## Executive Summary

This conversation covered the most sensitive project topic so far: the ability to rebuild the full environment after a critical Windows format, recompile TFS/RME/OTClient with the correct dependencies, validate actual runtime behavior, and convert that experience into a backup guided enough to be restored later from a single Codex request.

The work had four main tracks:

1. local machine restoration in `D:\tibia-oldschool`, with tools installed preferably under `D:\tibia-dev-tools`;
2. correction of real build and link issues, including `CMP0167`, `LNK4098`, `MSB8027`, and the Boost Bind global placeholder warning;
3. consolidation of technical documentation and a detailed restoration report;
4. preparation of a separate cloud upload copy, without regenerable executables and without requiring `D:\tibia-dev-tools` to be included in the final backup package.

The practical output of this stage was:

- TFS rebuilt and reaching `Tibia Oldschool 7.72 Test Server Online!`;
- OTClient rebuilt and remaining open for 15 seconds without crashing;
- RME rebuilt, opening the map and remaining open for 20 seconds without crashing;
- `Build-All.ps1` adjusted for reproducible builds;
- `RELATORIO_RESTAURACAO_AMBIENTE_2026-06-10.md` turned into a strong reference for the real machine state;
- preparation of a separate clean backup for cloud upload, including end-to-end restoration automation.

## Phase 1. Initial Reading And State Survey

### Assumed scope

- The main workspace was confirmed as `D:\tibia-oldschool`.
- The initial request required reading `LEIA-ME-BACKUP.md` and the entire `docs` folder.
- The goal was not only to recompile, but to understand the existing backup, identify documentation gaps, and reach a state that could be repeated later.

### Reference files inspected in the workspace

- `D:\tibia-oldschool\LEIA-ME-BACKUP.md`
- `D:\tibia-oldschool\docs\RETOMADA_APOS_FORMATACAO_2026-06-10.md`
- `D:\tibia-oldschool\docs\BACKUP_RECOMPILAVEL_VALIDADO_2026-06-10.md`
- `D:\tibia-oldschool\docs\COMO_CRIAR_BACKUP_RECOMPILAVEL.md`
- `D:\tibia-oldschool\docs\CONTEXTO_CONTINUACAO_2026-06-02.md`
- `D:\tibia-oldschool\docs\architecture.md`
- `D:\tibia-oldschool\docs\assets-workflow.md`
- `D:\tibia-oldschool\docs\backup.md`

### Initial conclusions

- The backup already preserved the vcpkg dependency trees required by all three builds.
- The documentation explained a large part of the process, but still left important gaps for repeating the restoration without trial and error.
- There was practical knowledge not yet consolidated into one flow: where to install tools, how to handle MariaDB on `D:`, how to fix build warnings/link issues, and how to clean the backup for cloud upload without destroying the dependency trees that must remain.

## Phase 2. Toolchain Restoration Under `D:\tibia-dev-tools`

### Disk policy

- The user asked to avoid `C:` whenever possible.
- The operational decision was to centralize tools in `D:\tibia-dev-tools`, while accepting minimal unavoidable `C:` usage for Microsoft and Windows-managed components.

### Tools installed or extracted

- `D:\tibia-dev-tools\cmake-4.3.2`
- `D:\tibia-dev-tools\Git`
- `D:\tibia-dev-tools\Python312`
- `D:\tibia-dev-tools\mariadb-10.11.17`
- `D:\tibia-dev-tools\VisualStudio\2022\BuildTools`
- `D:\tibia-dev-tools\installers`
- `D:\tibia-dev-tools\logs`
- `D:\tibia-dev-tools\temp`

### Validated downloads

- `vs_BuildTools.exe`
- `cmake-4.3.2-windows-x86_64.zip`
- `mariadb-10.11.17-winx64.zip`
- `python-3.12.10-amd64.exe`
- `PortableGit-2.54.0-64-bit.7z.exe`

### External components and recorded versions

- Visual Studio Build Tools 2022 `17.14.37328.6`
- MSBuild `17.14.40.60911`
- MSVC compiler `19.44.35228`
- Windows SDK `10.0.26100.0`
- CMake `4.3.2`
- MariaDB `10.11.17`
- Git for Windows `2.54.0.windows.1`
- Python `3.12.10`
- Pillow `12.2.0`

### Important variables and paths

- `PATH` received entries for:
  - `D:\tibia-dev-tools\cmake-4.3.2\bin`
  - `D:\tibia-dev-tools\Git\cmd`
  - `D:\tibia-dev-tools\Python312`
  - `D:\tibia-dev-tools\Python312\Scripts`
  - `D:\tibia-dev-tools\mariadb-10.11.17\bin`
- The broken Microsoft Store alias in `C:\Users\guisu\AppData\Local\Microsoft\WindowsApps\python.exe` stopped being the main Python reference for the project.

## Phase 3. Portable MariaDB And Test Database

### Main decision

- Instead of relying on a traditional Windows service installation, the official MariaDB ZIP distribution was used under `D:\tibia-dev-tools\mariadb-10.11.17`.
- The database was run as a portable local instance, without a service, listening only on `127.0.0.1:3306`.

### Structure created outside the project tree

- `D:\tibia-dev-tools\Initialize-MariaDB.ps1`
- `D:\tibia-dev-tools\Start-MariaDB.ps1`
- `D:\tibia-dev-tools\Stop-MariaDB.ps1`
- `D:\tibia-dev-tools\Start-Tibia-Server.cmd`
- `D:\tibia-dev-tools\mariadb-data\my.ini`
- `D:\tibia-dev-tools\mariadb-data\data`
- `D:\tibia-dev-tools\mariadb-data\logs`
- `D:\tibia-dev-tools\mariadb-data\root-credential.xml`

### Variables read from the project

The database bootstrap flow did not hardcode new credentials into documentation; it read the following fields directly from `server\config.lua`:

- `mysqlHost`
- `mysqlUser`
- `mysqlPass`
- `mysqlDatabase`
- `mysqlPort`

### Dump and functional validation

- Dump used: `D:\tibia-oldschool\backup-extras\database\oldschool772db-2026-06-10.sql`
- The active test database during restoration was `oldschool772db_backup_test_20260610`.
- Final database validation:
  - `players=6`
  - `accounts=1`

### Security decisions

- The MariaDB root password was generated randomly.
- The credential was exported through DPAPI into `root-credential.xml`, without being copied into the changelog or report.
- Backup and report documents were sanitized so they would not expose credentials directly.

## Phase 4. Build Orchestration

### Central script

The consolidated build process was encapsulated in:

- `D:\tibia-oldschool\tools\backup\Build-All.ps1`

### Main `Build-All.ps1` variables

- `$Root`
- `$CMake`
- `$Toolchain`
- `$ClientInstalled`
- `$StaticInstalled`
- `$ClientStaticInstalled`
- `$BuildRoot`
- `$Logs`
- `$Results`

### Operational directories used by the script

- `D:\tibia-oldschool\build-validation`
- `D:\tibia-oldschool\build-results\logs`
- `D:\tibia-oldschool\build-results\executables`

### Build settings per component

#### TFS

- generator: `Visual Studio 17 2022`
- platform: `x64`
- triplet: `x64-windows`
- relevant flag: `-DSKIP_GIT=ON`

#### RME

- generator: `Visual Studio 17 2022`
- platform: `x64`
- triplet: `x64-windows-static`
- explicitly pinned library:
  - `-DLibArchive_LIBRARY=.../lib/archive.lib`

#### OTClient

- generator: `Visual Studio 17 2022`
- platform: `x64`
- target triplet: `x64-windows-static`
- host triplet: `x64-windows-static`
- `-DVCPKG_INSTALLED_DIR`
- `-DVCPKG_MANIFEST_MODE=OFF`
- `-DVCPKG_BUILD_TYPE=release`
- `-DBUILD_STATIC_LIBRARY=ON`
- `-DOTCLIENT_BUILD_TESTS=OFF`
- `-DSPEED_UP_BUILD_UNITY=ON`
- `-DOPTIONS_ENABLE_SCCACHE=OFF`
- explicitly pinned libraries:
  - `-DLUAJIT_LIBRARY=.../lib/lua51.lib`
  - `-DVORBISFILE_LIBRARY=.../lib/vorbisfile.lib`
  - `-DVORBIS_LIBRARY=.../lib/vorbis.lib`

## Phase 5. Fixing Real Build Issues

### 1. `CMP0167` with `FindBoost`

#### Cause

- Modern CMake removed the legacy `FindBoost` module.
- TFS and RME were still calling `find_package(Boost ...)` without explicitly selecting `CONFIG` mode.

#### Files fixed

- `D:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\CMakeLists.txt`
- `D:\tibia-oldschool\sources\rme-otacademy\CMakeLists.txt`

#### Core changes

- TFS:
  - `find_package(Boost 1.66.0 CONFIG REQUIRED COMPONENTS date_time system filesystem iostreams)`
- RME:
  - `find_package(Boost 1.34.0 CONFIG REQUIRED COMPONENTS thread system)`
  - explicit linking against `Boost::thread` and `Boost::system`

#### Result

- `CMP0167` disappeared from the current TFS and RME logs.

### 2. `LNK4098 defaultlib 'LIBCMTD' conflicts`

#### Cause

- The multi-config generator could mix `debug\lib` libraries with `Release` and `RelWithDebInfo` builds.
- The issue affected RME and OTClient most visibly.

#### Main file fixed

- `D:\tibia-oldschool\tools\backup\Build-All.ps1`

#### Technical decision

- The correct Release libraries were pinned explicitly in the CMake command line.
- `/NODEFAULTLIB` was not used, because that would only hide the problem instead of fixing the root cause.

#### Result

- `LNK4098` stopped appearing in the current logs.

### 3. `MSB8027` in OTClient

#### Cause

Two different source files could generate the same intermediate object name:

- `framework/luafunctions.cpp`
- `client/luafunctions.cpp`

#### File fixed

- `D:\tibia-oldschool\sources\otclient-redemption\src\CMakeLists.txt`

#### Conceptually important line

The project was changed to define:

- `PROPERTY VS_SETTINGS "ObjectFileName=$(IntDir)framework_luafunctions.obj"`

#### Result

- The framework source now uses a unique object filename.
- `MSB8027` stopped appearing in the current logs.

### 4. Boost Bind global placeholder warning in RME

#### Cause

- The legacy RME JSON parser (`json_spirit`) depends on the old global `_1`, `_2`, etc. placeholder semantics.

#### File fixed

- `D:\tibia-oldschool\sources\rme-otacademy\CMakeLists.txt`

#### Core change

- `target_compile_definitions(rme PRIVATE BOOST_BIND_GLOBAL_PLACEHOLDERS)`

#### Result

- The parser's old behavior was preserved.
- The corresponding deprecation warning stopped being a pending issue in the validated clean build.

## Phase 6. Runtime And Local Operation Adjustments

### RME

- `D:\tibia-oldschool\sources\rme-otacademy\rme.cfg` was corrected to point to the actual root `D:\tibia-oldschool`.
- The active map remained:
  - `D:\tibia-oldschool\server\data\world\world.otbm`
- The active editor/client assets remained:
  - `D:\tibia-oldschool\sources\otclient-redemption\data\things\772`

### Shortcuts

The shortcut script was adjusted for the restored environment:

- `D:\tibia-oldschool\tools\backup\Create-Desktop-Shortcuts.ps1`

Validated shortcuts:

- `01 - Servidor TFS (banco de teste).lnk`
- `02 - OTClient Redemption.lnk`
- `03 - RME OTAcademy.lnk`

## Phase 7. Smoke Tests And Functional Verification

### TFS

- started in the foreground
- connected to the database
- loaded scripts, monsters, outfits, and the map
- reached `Tibia Oldschool 7.72 Test Server Online!`
- validated ports `7171` and `7172`

### OTClient

- launched from its runtime directory
- remained open for `15` seconds without an immediate crash

### RME

- launched with the project map
- remained open for `20` seconds without an immediate crash

### Hashes preserved before recompilation

- `server\tfs.exe`
  - `968F0BCDB0059AC55A5B5812B31E977CB888C932BF852AC2261DD7958E46B3C0`
- `sources\otclient-redemption\otclient.exe`
  - `43D29BC186ABC478DE5A827D592EDE65C7B162F8E387B56308E83663FDEC5CA1`
- `sources\rme-otacademy\rme.exe`
  - `04418D2468E44DDB962CF2D78287BB31BC310AF53049B60EDB50E95104D2EE52`
- `sources\otclient-redemption\data\things\772\Tibia.dat`
  - `D4DBACCC3C4994F00A08C77E7E7FBD77F58606C34328C3A6337D2F4DACAF6F86`
- `sources\otclient-redemption\data\things\772\Tibia.spr`
  - `BA2415DAA7F02BD62A04BD4E1F6B92E5407A38EB58864C6672F1F6C2E074726D`
- `sources\otclient-redemption\data\things\772\Tibia.cwm`
  - `60B6B052629F646B3B50D7B64537697A1239D864E3A1ED47EE316780EE33D20D`
- `backup-extras\database\oldschool772db-2026-06-10.sql`
  - `6C20716A461B851693E0FC15B49807D88D7C87083D325EA35F99155C56529B02`

### Hashes validated after local recompilation

- `D:\tibia-oldschool\server\tfs.exe`
  - `1FC42E564E671FF2694596CA2ACB2FC47CC89412C3B8806FB2E7882B4012C306`
- `D:\tibia-oldschool\sources\rme-otacademy\rme.exe`
  - `5DBE250A70DE16BC761B3997E28B4C1343894E1896DBB9CA43B3B97D45B907A0`
- `D:\tibia-oldschool\sources\otclient-redemption\otclient.exe`
  - `73D765504A2397E83F4C3F1690AF3B146716CC2250C5662937F5EC7254E6142B`

## Phase 8. Documentation Consolidation

### Most important document from this phase in the current workspace

- `D:\tibia-oldschool\RELATORIO_RESTAURACAO_AMBIENTE_2026-06-10.md`

That report became the main record for:

- post-format machine state;
- downloaded versions;
- `D:\tibia-dev-tools` structure;
- MariaDB bootstrap;
- build commands;
- explanation of the `CMP0167`, `LNK4098`, and `MSB8027` fixes;
- before/after hashes;
- smoke tests;
- practical differences between the older documents and the real procedure.

### Credential sanitization

- The user explicitly asked for login and password removal from the report because the file would become part of a backup.
- The final report was revised so it would not keep operational credentials in plain text.

## Phase 9. Preparation Of A Reproducible Cloud Backup

### Chosen base

- The base for the cloud backup was the historical copy `D:\tibia-oldschool-backup-teste-2026-06-10`.
- That copy was preferred as raw material because it represented the original snapshot before the local restoration changes.

### Goal of this second flow

- produce a self-contained project folder;
- avoid depending on bundling `D:\tibia-dev-tools` into the cloud backup;
- keep only sources, data, dependency trees preserved under `tools`, and enough scripts to recreate the external toolchain after formatting;
- remove executables, `build-validation`, `build-results`, and other regenerable artifacts.

### Scripts added in the clean backup prepared during the conversation

In the separate upload-ready backup, scripts such as the following were created or consolidated:

- `tools\backup\Install-Development-Tools.ps1`
- `tools\backup\Initialize-MariaDB.ps1`
- `tools\backup\Start-MariaDB.ps1`
- `tools\backup\Stop-MariaDB.ps1`
- `tools\backup\Update-Rme-Paths.ps1`
- `tools\backup\Smoke-Test.ps1`
- `tools\backup\Restore-All.ps1`
- `tools\backup\Validate-Backup.ps1`
- `tools\backup\Clean-Backup-For-Upload.ps1`
- `tools\backup\Start-Tibia-Server.cmd`

### Main documents generated in the clean backup

- `LEIA-ME-BACKUP.md`
- `docs\RESTAURACAO_COMPLETA_POS_FORMATACAO.md`
- `docs\VALIDACAO_FINAL_BACKUP_ONLINE_2026-06-10.md`

### Consolidated operational principle

The final cloud backup should not contain:

- `server\tfs.exe`
- `sources\rme-otacademy\rme.exe`
- `sources\otclient-redemption\otclient.exe`
- `sources\otclient-redemption\RelWithDebInfo`
- `build-validation`
- `build-results`
- `backup-extras\pre-rebuild-binaries`

But it should preserve:

- `tools\vcpkg\installed\x64-windows`
- `tools\vcpkg\installed\x64-windows-static`
- `tools\dependencies\otclient-vcpkg-installed\x64-windows-static`
- `backup-extras\database`
- `backup-extras\Sprites Permanentes`

### Validation of the clean backup

During the conversation, the clean upload backup was:

- rebuilt from scratch;
- subjected again to smoke tests;
- checked for recurrence of `CMP0167`, `LNK4098`, `MSB8027`, and the Boost Bind warning;
- tested with a fresh MariaDB initialization in a separate directory;
- cleaned again from regenerable artifacts;
- validated through script before being considered ready for cloud upload.

## Phase 10. Codex Cloud Discussion

### Topics covered

- difference between local/IDE Codex usage and the `Hand off to Codex in the cloud` option;
- task types best suited for handoff;
- credit/cost implications based on processing, model choice, and token usage.

### Practical result

- No project file was changed by that discussion.
- The consolidated understanding was operational: use the cloud flow for long, well-scoped, autonomous tasks such as build, cleanup, report generation, and background validation.

## Most Important Files From This Conversation In The Current Workspace

- `D:\tibia-oldschool\RELATORIO_RESTAURACAO_AMBIENTE_2026-06-10.md`
- `D:\tibia-oldschool\tools\backup\Build-All.ps1`
- `D:\tibia-oldschool\tools\backup\Create-Desktop-Shortcuts.ps1`
- `D:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\CMakeLists.txt`
- `D:\tibia-oldschool\sources\rme-otacademy\CMakeLists.txt`
- `D:\tibia-oldschool\sources\rme-otacademy\rme.cfg`
- `D:\tibia-oldschool\sources\otclient-redemption\src\CMakeLists.txt`

## Consolidated Rules And Decisions

- `D:\tibia-oldschool` is the main working root.
- `D:\tibia-dev-tools` is the preferred location for the external toolchain.
- `C:` should only be used when Windows or the Microsoft ecosystem requires it.
- TFS, RME, and OTClient must be rebuilt with a clean build whenever the goal is environment validation.
- The server should not depend on a traditional MariaDB Windows service for the restoration model described in this conversation.
- `server\config.lua` remains an operational private file and must not be mirrored into changelogs with password values.
- `Tibia.dat`, `Tibia.spr`, and `Tibia.cwm` must always be handled as one set.
- The dependency trees under `tools\vcpkg\installed` and `tools\dependencies\otclient-vcpkg-installed` are not disposable build garbage; they are part of the reproducible backup.

## Pending Items And Limits

- VS Code installation and Codex usage from inside it were mentioned as a next step, but were not the technical center of this changelog.
- Part of the clean backup automation was produced in a separate project copy, not in the current `D:\tibia-oldschool` root.
- This changelog documents the full process of the conversation, including the cloud backup preparation, even when some artifacts from that separate clean copy are no longer present in the current workspace.
