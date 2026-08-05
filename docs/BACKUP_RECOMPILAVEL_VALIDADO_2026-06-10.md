# Backup recompilavel validado - 2026-06-10

## Resumo

Origem:

```text
D:\tibia-oldschool
```

Backup:

```text
D:\tibia-oldschool-backup-teste-2026-06-10
```

A copia bruta foi feita antes de qualquer limpeza:

- 49.823 diretorios
- 490.604 arquivos
- 56,616 GiB
- zero falhas no Robocopy

Depois da recompilacao, dos testes e da remocao dos artefatos intermediarios,
o backup ficou com:

- aproximadamente 39.056 arquivos;
- aproximadamente 8.356.867.101 bytes;
- aproximadamente 7,783 GiB.

Esse total inclui o snapshot separado dos assets visuais ativos e o documento
de retomada apos formatacao.

Foram preservados:

- pacote executavel do servidor;
- source do Nekiro TFS 1.5 downgrade 7.72;
- source e assets completos do OTClient Redemption;
- `Tibia.dat`, `Tibia.spr` e `Tibia.cwm` ativos;
- source, dados e configuracao do RME OTAcademy;
- ferramentas de assets;
- bibliotecas vcpkg instaladas;
- dependencias estaticas exatas usadas pelo OTClient;
- sprites permanentes externas;
- dump SQL do banco;
- documentos tecnicos;
- executaveis recompilados e logs de validacao.

## Estado validado

### TFS

Executavel:

```text
server\tfs.exe
```

SHA256:

```text
968F0BCDB0059AC55A5B5812B31E977CB888C932BF852AC2261DD7958E46B3C0
```

Resultado:

- configurado do zero com CMake;
- compilado em `Release`;
- conectou ao MariaDB;
- carregou scripts, monsters, outfits e mapa;
- mapa carregado: `33500x33500`;
- chegou a `Tibia Oldschool 7.72 Test Server Online!`.

Log:

```text
build-results\logs\tfs-smoke-stdout.log
```

### OTClient Redemption

Executavel:

```text
sources\otclient-redemption\otclient.exe
```

SHA256:

```text
43D29BC186ABC478DE5A827D592EDE65C7B162F8E387B56308E83663FDEC5CA1
```

Resultado:

- configurado do zero com CMake e Visual Studio;
- compilado em `RelWithDebInfo`;
- iniciou usando os assets desta copia;
- permaneceu aberto por 10 segundos sem crash imediato.

Warnings conhecidos e nao fatais:

- `MSB8027`: dois arquivos chamados `luafunctions.cpp`;
- `LNK4098`: conflito historico de runtime `LIBCMTD`.

### RME OTAcademy

Executavel:

```text
sources\rme-otacademy\rme.exe
```

SHA256:

```text
04418D2468E44DDB962CF2D78287BB31BC310AF53049B60EDB50E95104D2EE52
```

Resultado:

- configurado do zero com CMake;
- compilado em `Release`;
- iniciou com dados e assets desta copia;
- permaneceu aberto por 10 segundos sem crash imediato.

O arquivo `open-world.cmd` usa caminho relativo. O `rme.cfg` aponta para os
assets e mapa dentro deste backup.

## Banco de dados

Dump:

```text
backup-extras\database\oldschool772db-2026-06-10.sql
```

SHA256:

```text
6C20716A461B851693E0FC15B49807D88D7C87083D325EA35F99155C56529B02
```

O servidor desta copia usa:

```text
oldschool772db_backup_test_20260610
```

Esse banco foi criado a partir do dump e validado com:

- 6 players;
- 1 account.

O banco principal `oldschool772db` nao foi usado no smoke test.

O config original esta em:

```text
backup-extras\config.production-original.lua
```

## Sprites permanentes

A pasta externa foi incorporada em:

```text
backup-extras\Sprites Permanentes
```

Conteudo copiado:

- 492 arquivos;
- aproximadamente 93,27 MiB.

O estado exato ativo em 2026-06-10 tambem foi preservado separadamente em:

```text
backup-extras\Sprites Permanentes\estado-ativo-2026-06-10
```

Esse e o conjunto preferencial para restaurar `Tibia.dat`, `Tibia.spr` e
`Tibia.cwm` juntos. Ele inclui as alteracoes de agua e CID 104 posteriores ao
primeiro pacote aprovado de 2026-06-09.

## Dependencias preservadas

### vcpkg geral

```text
tools\vcpkg
tools\vcpkg\installed\x64-windows
tools\vcpkg\installed\x64-windows-static
```

Usado pelo TFS e RME.

### OTClient

```text
tools\dependencies\otclient-vcpkg-installed\x64-windows-static
```

Essa pasta veio do antigo build do client e foi preservada antes da limpeza.
Ela contem exatamente os pacotes que permitiram recompilar o client sem
manifest install e sem download.

Nao apagar essas pastas se a intencao for recompilar offline.

## Software externo necessario

### Limite do modo offline

Este snapshot recompila offline somente depois que o Windows possui Visual
Studio C++ e CMake instalados. O MariaDB Server precisa estar instalado para
executar o TFS com banco.

Os instaladores desses programas nao estao no backup. Portanto, uma maquina
recem-formatada e sem internet ainda nao esta pronta apenas com esta pasta.

O backup preserva as bibliotecas vcpkg ja instaladas para a configuracao
atual. Ele nao preserva todos os downloads e buildtrees necessarios para
reconstruir cada dependencia de terceiros do zero.

Ambiente usado na validacao:

- Windows x64;
- Visual Studio Professional 2022 `17.14.37301.10`;
- workload `Desktop development with C++`;
- MSVC `19.44.35227`;
- Windows SDK `10.0.26100.0`;
- CMake `4.3.2`;
- MariaDB Community Server `10.11.17`;
- Git for Windows `2.54.0` apenas para baixar ou versionar sources.

Para os scripts de assets:

- Python 3 x64;
- Pillow;
- Upscayl e ComfyUI somente quando o workflow visual exigir.

O MariaDB nao e portavel por simples copia de `Program Files`. O dump SQL esta
preservado, mas o servico de banco precisa ser instalado e configurado.

Paginas oficiais:

- Visual Studio: https://visualstudio.microsoft.com/downloads/
- C++ no Visual Studio: https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation
- CMake: https://cmake.org/download/
- MariaDB: https://mariadb.org/download/
- MariaDB 10.11: https://mariadb.org/mariadb/all-releases/
- Git: https://git-scm.com/download/win
- vcpkg: https://learn.microsoft.com/en-us/vcpkg/get_started/overview

## Recompilar tudo

Abra PowerShell e execute:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
& "D:\tibia-oldschool-backup-teste-2026-06-10\tools\backup\Build-All.ps1" -Clean
```

O script:

1. configura e compila o TFS;
2. configura e compila o RME;
3. configura e compila o OTClient;
4. instala os executaveis nos diretorios de runtime;
5. copia os executaveis para `build-results\executables`;
6. grava logs em `build-results\logs`.

Os diretorios em `build-validation` sao temporarios. Depois de uma compilacao
validada, eles podem ser removidos sem apagar os executaveis instalados, os
logs ou as dependencias preservadas.

## Comandos manuais

Defina:

```powershell
$ROOT = "D:\tibia-oldschool-backup-teste-2026-06-10"
$CMAKE = "C:\Program Files\CMake\bin\cmake.exe"
```

### TFS

```powershell
& $CMAKE `
  -S "$ROOT\sources\nekiro-tfs-1.5-7.72" `
  -B "$ROOT\build-validation\tfs" `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DVCPKG_MANIFEST_MODE=OFF `
  -DSKIP_GIT=ON

& $CMAKE --build "$ROOT\build-validation\tfs" `
  --config Release --parallel 8
```

### RME

```powershell
& $CMAKE `
  -S "$ROOT\sources\rme-otacademy" `
  -B "$ROOT\build-validation\rme" `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DVCPKG_MANIFEST_MODE=OFF

& $CMAKE --build "$ROOT\build-validation\rme" `
  --config Release --parallel 8
```

### OTClient

```powershell
& $CMAKE `
  -S "$ROOT\sources\otclient-redemption" `
  -B "$ROOT\build-validation\otclient" `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DVCPKG_HOST_TRIPLET=x64-windows-static `
  -DVCPKG_INSTALLED_DIR="$ROOT/tools/dependencies/otclient-vcpkg-installed" `
  -DVCPKG_MANIFEST_MODE=OFF `
  -DVCPKG_BUILD_TYPE=release `
  -DBUILD_STATIC_LIBRARY=ON `
  -DOTCLIENT_BUILD_TESTS=OFF `
  -DSPEED_UP_BUILD_UNITY=ON `
  -DOPTIONS_ENABLE_SCCACHE=OFF

& $CMAKE --build "$ROOT\build-validation\otclient" `
  --config RelWithDebInfo --parallel 8
```

## Dependencias para reconstruir do zero

Use somente se as pastas `installed` forem perdidas.

### TFS, triplet x64-windows

```text
boost-date-time
boost-filesystem
boost-iostreams
boost-system
boost-asio
boost-variant
boost-lockfree
cryptopp
fmt
libmariadb
pugixml
luajit
```

### RME, triplet x64-windows-static

```text
boost-thread
boost-system
wxwidgets
libarchive
freeglut
```

### OTClient, triplet x64-windows-static

O arquivo `sources\otclient-redemption\vcpkg.json` e a referencia principal.
Ele fixa o baseline:

```text
56bb2411609227288b70117ead2c47585ba07713
```

Para uma reinstalacao realmente limpa, use um clone completo do vcpkg, faca
checkout desse baseline e execute a instalacao do manifest do client. O backup
atual nao precisa disso porque o conjunto instalado foi preservado.

## Restaurar o banco

Exemplo:

```powershell
$env:MYSQL_PWD = "SENHA_DO_ROOT"
& "C:\Program Files\MariaDB 10.11\bin\mariadb.exe" `
  -uroot -e "CREATE DATABASE oldschool772db CHARACTER SET utf8mb4;"

Get-Content ".\backup-extras\database\oldschool772db-2026-06-10.sql" |
  & "C:\Program Files\MariaDB 10.11\bin\mariadb.exe" `
    -uroot oldschool772db

Remove-Item Env:\MYSQL_PWD
```

Depois ajuste `server\config.lua`.

## O que foi removido

Total aproximado removido:

- 452.073 arquivos;
- 49,022 GiB.

Principais remocoes:

| Caminho | GiB | Motivo |
|---|---:|---|
| `tools\vcpkg\buildtrees` | 20,799 | fontes e objetos temporarios do vcpkg |
| antigo build do OTClient | 8,857 | objetos e CMake cache; dependencias foram separadas |
| `tools\vcpkg-otacademy` | 7,756 | stack antiga nao usada pelos tres projetos atuais |
| `.git` | 2,422 | historico pesado; snapshot de source foi preservado |
| `tools\vcpkg\downloads` | 2,248 | arquivos baixados regeneraveis |
| `tools\vcpkg\packages` | 1,969 | staging duplicado; `installed` foi preservado |
| `tools\downloads` | 1,669 | pacote antigo de dependencias |
| `tools\assets\tests` | 1,536 | lotes temporarios de testes |
| `docs\conversas` | 0,799 | conversas brutas; documentos consolidados foram mantidos |
| builds Debug/Release antigos | 0,532 | binarios e simbolos historicos |
| `experiments` e backups antigos | 0,106 | duplicacoes temporarias |

Tambem foram removidos logs antigos, `.vs`, `__pycache__`, executaveis
historicos do client e arquivos temporarios.

Manifesto:

```text
backup-extras\cleanup-manifest.json
```

## Seguranca e limitacoes

- Este e um snapshot de source, nao um clone Git com historico.
- Credenciais locais podem existir no `config.lua`, dump SQL e documentos.
- Nao envie o backup para repositorio publico.
- O banco de teste local nao faz parte da pasta; o dump SQL faz.
- Os atalhos usam o caminho absoluto atual. Se mover a pasta, recrie-os com
  `tools\backup\Create-Desktop-Shortcuts.ps1`.
