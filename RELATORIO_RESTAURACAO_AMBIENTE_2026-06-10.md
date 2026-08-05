# Relatorio de restauracao do ambiente - 2026-06-10

## Objetivo

Este documento registra o que foi feito depois da formatacao do computador
para restaurar o ambiente de desenvolvimento do Tibia Oldschool em:

```text
D:\tibia-oldschool
```

Ele complementa os documentos anteriores, principalmente:

```text
LEIA-ME-BACKUP.md
docs\RETOMADA_APOS_FORMATACAO_2026-06-10.md
docs\BACKUP_RECOMPILAVEL_VALIDADO_2026-06-10.md
docs\COMO_CRIAR_BACKUP_RECOMPILAVEL.md
```

Use este relatorio como referencia principal para o estado real da maquina
apos a restauracao. Os documentos antigos continuam importantes para o
contexto tecnico e historico do projeto.

## Resultado final

Em 2026-06-10 foram concluidos:

- leitura integral de `LEIA-ME-BACKUP.md` e da pasta `docs`;
- verificacao dos hashes do backup antes de qualquer recompilacao;
- instalacao das ferramentas de desenvolvimento;
- restauracao do MariaDB e do banco de teste;
- recompilacao limpa do TFS, RME e OTClient;
- smoke test dos tres programas;
- correcao dos caminhos absolutos do RME;
- criacao e teste dos atalhos na Area de Trabalho;

Estado validado:

- TFS chegou a `Tibia Oldschool 7.72 Test Server Online!`;
- mapa carregado com tamanho `33500x33500`;
- OTClient permaneceu aberto por 15 segundos sem crash;
- RME abriu o mapa e permaneceu aberto por 20 segundos sem crash;
- os atalhos abriram os tres programas;
- as portas `7171` e `7172` ficaram acessiveis durante o teste do atalho;
- MariaDB ficou acessivel apenas em `127.0.0.1:3306`;
- banco restaurado com 6 players e 1 account.

## Sistema encontrado depois da formatacao

```text
Windows 11 Home x64
Windows version: 10.0.26200
Windows build: 26200
Projeto: D:\tibia-oldschool
```

No inicio nao estavam disponiveis no `PATH`:

```text
cmake
git
py
msbuild
mariadb
mysql
ninja
```

O comando `python` apontava apenas para o alias da Microsoft Store em:

```text
C:\Users\guisu\AppData\Local\Microsoft\WindowsApps\python.exe
```

Nao havia MariaDB instalado como servico e nao havia Visual Studio ou Build
Tools registrados como instalados.

Os executaveis e assets preservados no backup estavam corretos antes da
recompilacao. Os hashes conferiam com a documentacao:

```text
server\tfs.exe
968F0BCDB0059AC55A5B5812B31E977CB888C932BF852AC2261DD7958E46B3C0

sources\otclient-redemption\otclient.exe
43D29BC186ABC478DE5A827D592EDE65C7B162F8E387B56308E83663FDEC5CA1

sources\rme-otacademy\rme.exe
04418D2468E44DDB962CF2D78287BB31BC310AF53049B60EDB50E95104D2EE52

sources\otclient-redemption\data\things\772\Tibia.dat
D4DBACCC3C4994F00A08C77E7E7FBD77F58606C34328C3A6337D2F4DACAF6F86

sources\otclient-redemption\data\things\772\Tibia.spr
BA2415DAA7F02BD62A04BD4E1F6B92E5407A38EB58864C6672F1F6C2E074726D

sources\otclient-redemption\data\things\772\Tibia.cwm
60B6B052629F646B3B50D7B64537697A1239D864E3A1ED47EE316780EE33D20D

backup-extras\database\oldschool772db-2026-06-10.sql
6C20716A461B851693E0FC15B49807D88D7C87083D325EA35F99155C56529B02
```

As dependencias vcpkg preservadas tambem estavam presentes:

```text
tools\vcpkg\installed\x64-windows
6893 arquivos, aproximadamente 0,380 GiB

tools\vcpkg\installed\x64-windows-static
9261 arquivos, aproximadamente 1,593 GiB

tools\dependencies\otclient-vcpkg-installed\x64-windows-static
2757 arquivos, aproximadamente 5,173 GiB
```

Essas pastas permitiram recompilar sem reinstalar as bibliotecas C/C++.

## Diretorio das ferramentas

Para evitar o disco `C:` sempre que possivel, foi criado:

```text
D:\tibia-dev-tools
```

Estrutura atual:

```text
D:\tibia-dev-tools\cmake-4.3.2
D:\tibia-dev-tools\Git
D:\tibia-dev-tools\installers
D:\tibia-dev-tools\logs
D:\tibia-dev-tools\mariadb-10.11.17
D:\tibia-dev-tools\mariadb-data
D:\tibia-dev-tools\Python312
D:\tibia-dev-tools\temp
D:\tibia-dev-tools\VisualStudio\2022\BuildTools
```

Scripts adicionados fora da arvore do projeto:

```text
D:\tibia-dev-tools\Initialize-MariaDB.ps1
D:\tibia-dev-tools\Start-MariaDB.ps1
D:\tibia-dev-tools\Stop-MariaDB.ps1
D:\tibia-dev-tools\Start-Tibia-Server.cmd
```

## Downloads realizados

Todos os instaladores e arquivos compactados foram baixados para:

```text
D:\tibia-dev-tools\installers
```

### Visual Studio Build Tools

URL:

```text
https://aka.ms/vs/17/release/vs_BuildTools.exe
```

Arquivo:

```text
vs_BuildTools.exe
4458176 bytes
SHA256 B7339471DBDAFB6F69152308F63204F12F6000E905FCA9FED4E09672B43F3DB5
```

### CMake

URL:

```text
https://github.com/Kitware/CMake/releases/download/v4.3.2/cmake-4.3.2-windows-x86_64.zip
```

Arquivo:

```text
cmake-4.3.2-windows-x86_64.zip
52961068 bytes
SHA256 83D20C23F5C5F64B3B328785E35B23C532E33057A97ED6294ACACA3781B78A01
```

### MariaDB

URL:

```text
https://archive.mariadb.org/mariadb-10.11.17/winx64-packages/mariadb-10.11.17-winx64.zip
```

Arquivo:

```text
mariadb-10.11.17-winx64.zip
93256035 bytes
SHA256 DE457D1C18C20F6D8E84882CE1B33205EC44E7CA18DE47778741E441ED346213
```

### Python

URL:

```text
https://www.python.org/ftp/python/3.12.10/python-3.12.10-amd64.exe
```

Arquivo:

```text
python-3.12.10-amd64.exe
26964224 bytes
SHA256 67B5635E80EA51072B87941312D00EC8927C4DB9BA18938F7AD2D27B328B95FB
```

### Git for Windows portatil

URL:

```text
https://github.com/git-for-windows/git/releases/download/v2.54.0.windows.1/PortableGit-2.54.0-64-bit.7z.exe
```

Arquivo:

```text
PortableGit-2.54.0-64-bit.7z.exe
58995352 bytes
SHA256 BEA006A6CC69673F27B1647E84AB3A68E912FBC175AB6320C5987E012897F311
```

## Versoes instaladas

```text
Visual Studio Build Tools 2022: 17.14.37328.6
MSBuild: 17.14.40.60911
MSVC compiler: 19.44.35228
MSVC tools directory: 14.44.35207
Windows SDK: 10.0.26100.0
CMake: 4.3.2
MariaDB: 10.11.17
Git for Windows: 2.54.0.windows.1
Python: 3.12.10
Pillow: 12.2.0
```

O Python documentado antes da formatacao era `3.12.13`. Essa versao exata nao
foi necessaria para os scripts atuais. Foi instalado Python `3.12.10`, que e
compativel com o Pillow 12.2.0 e com as ferramentas de assets preservadas.

## Instalacao do Visual Studio Build Tools

Nao foi instalado o IDE completo do Visual Studio nesta etapa. Foi instalado
apenas o Build Tools, suficiente para CMake, MSBuild e compilacao C/C++.

Destino principal:

```text
D:\tibia-dev-tools\VisualStudio\2022\BuildTools
```

Componentes solicitados:

```text
Microsoft.VisualStudio.Workload.VCTools
Microsoft.VisualStudio.Component.VC.Tools.x86.x64
Microsoft.VisualStudio.Component.Windows11SDK.26100
```

O instalador foi executado com elevacao administrativa e terminou com codigo
`0`, sem exigir reinicio.

Mesmo apontando o destino principal para `D:`, alguns componentes da
Microsoft permaneceram obrigatoriamente em `C:`, incluindo:

```text
C:\Program Files (x86)\Microsoft Visual Studio\Installer
C:\Program Files (x86)\Windows Kits\10
```

Isso foi considerado uso absolutamente necessario do disco `C:`. O compilador
e o MSBuild ficaram no `D:`, enquanto o Windows SDK e metadados do instalador
ficaram no local padrao do Windows.

## Ferramentas portateis e PATH

CMake, Git e MariaDB foram extraidos, sem instalacao global, para o `D:`.
Python foi instalado em modo por usuario, mas com destino explicito no `D:`.

Foram adicionados ao `PATH` do usuario:

```text
D:\tibia-dev-tools\cmake-4.3.2\bin
D:\tibia-dev-tools\Git\cmd
D:\tibia-dev-tools\Python312
D:\tibia-dev-tools\Python312\Scripts
D:\tibia-dev-tools\mariadb-10.11.17\bin
```

Terminais que ja estavam abertos antes dessa alteracao podem precisar ser
fechados e abertos novamente para receber o novo `PATH`.

Pillow foi instalado com:

```powershell
D:\tibia-dev-tools\Python312\python.exe -m pip install --no-cache-dir Pillow==12.2.0
```

## MariaDB portatil no D

Os documentos anteriores recomendavam instalar o MariaDB Community Server
como programa e servico Windows. Nesta restauracao foi usada a distribuicao
ZIP oficial, inteiramente no `D:`, sem criar servico.

Binarios:

```text
D:\tibia-dev-tools\mariadb-10.11.17
```

Dados:

```text
D:\tibia-dev-tools\mariadb-data\data
```

Configuracao:

```text
D:\tibia-dev-tools\mariadb-data\my.ini
```

Configuracao principal:

```ini
[mysqld]
basedir=D:/tibia-dev-tools/mariadb-10.11.17
datadir=D:/tibia-dev-tools/mariadb-data/data
port=3306
bind-address=127.0.0.1
character-set-server=utf8mb4
collation-server=utf8mb4_general_ci
innodb_flush_log_at_trx_commit=1
skip-name-resolve
log-error=D:/tibia-dev-tools/mariadb-data/logs/mariadb-error.log
pid-file=D:/tibia-dev-tools/mariadb-data/mariadb.pid
```

O banco escuta somente em:

```text
127.0.0.1:3306
```

Nao existe servico Windows do MariaDB. O processo e iniciado pelo script:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  "D:\tibia-dev-tools\Start-MariaDB.ps1"
```

Encerramento limpo:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  "D:\tibia-dev-tools\Stop-MariaDB.ps1"
```

O script de inicializacao do TFS executa primeiro `Start-MariaDB.ps1`:

```text
D:\tibia-dev-tools\Start-Tibia-Server.cmd
```

Assim, depois de reiniciar o Windows, o atalho do servidor inicia o banco
automaticamente antes do `tfs.exe`.

### Credencial root do MariaDB

Foi gerada uma senha root aleatoria. Ela nao foi gravada em texto puro.
A credencial foi exportada com DPAPI para:

```text
D:\tibia-dev-tools\mariadb-data\root-credential.xml
```

Esse arquivo so deve funcionar para o mesmo usuario do Windows na mesma
maquina. Nao deve ser publicado.

### Banco restaurado

O script `Initialize-MariaDB.ps1` le do `server\config.lua`:

```text
mysqlUser
mysqlPass
mysqlDatabase
```

Ele cria o banco e o usuario com os valores ja preservados no projeto, sem
exibir a senha no terminal, e importa:

```text
D:\tibia-oldschool\backup-extras\database\oldschool772db-2026-06-10.sql
```

Banco usado:

```text
oldschool772db_backup_test_20260610
```

Resultado validado:

```text
players=6
accounts=1
```

Tambem foi testado o ciclo completo de shutdown e restart do MariaDB.

## Dificuldades encontradas no MariaDB

A primeira tentativa de executar toda a inicializacao em uma unica linha
PowerShell falhou no parser por causa de escapes de aspas e regex. Nenhuma
alteracao de banco ocorreu nessa tentativa.

A rotina foi movida para:

```text
D:\tibia-dev-tools\Initialize-MariaDB.ps1
```

Depois surgiram duas incompatibilidades do Windows PowerShell 5.1:

```text
RandomNumberGenerator.Fill nao existe
Convert.ToHexString nao existe
```

Foram substituidas por APIs compativeis:

```text
RandomNumberGenerator.Create().GetBytes(...)
BitConverter.ToString(...).Replace("-", "")
```

Apos essas correcoes, a inicializacao, importacao e validacao terminaram com
sucesso.

## Recompilacao

Foi usado o script existente:

```text
D:\tibia-oldschool\tools\backup\Build-All.ps1
```

Comando:

```powershell
$env:Path = "D:\tibia-dev-tools\cmake-4.3.2\bin;" + $env:Path

powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  "D:\tibia-oldschool\tools\backup\Build-All.ps1" `
  -Clean `
  -Parallel 8
```

O parametro `-Clean` removeu somente:

```text
D:\tibia-oldschool\build-validation
```

O proprio script valida que esse caminho esta dentro da raiz antes da
remocao.

Builds realizados:

```text
TFS: Release, x64, triplet x64-windows
RME: Release, x64, triplet x64-windows-static
OTClient: RelWithDebInfo, x64, triplet x64-windows-static
```

As dependencias vieram das pastas preservadas no backup. Nao houve
reinstalacao via vcpkg e nao houve download de bibliotecas durante os builds.

Logs:

```text
D:\tibia-oldschool\build-results\logs\tfs-configure.log
D:\tibia-oldschool\build-results\logs\tfs-build.log
D:\tibia-oldschool\build-results\logs\rme-configure.log
D:\tibia-oldschool\build-results\logs\rme-build.log
D:\tibia-oldschool\build-results\logs\otclient-configure.log
D:\tibia-oldschool\build-results\logs\otclient-build.log
```

## Warnings de compilacao

Os builds terminaram com sucesso.

### CMake e Boost

O primeiro build com CMake 4.3.2 apresentou:

```text
CMP0167 is not set: The FindBoost module is removed
```

O CMake 3.30 removeu o modulo legado `FindBoost`. TFS e RME ainda chamavam
`find_package(Boost)` sem selecionar um modo, fazendo o CMake tentar manter o
comportamento antigo e emitir o aviso.

As duas chamadas foram alteradas para o modo `CONFIG`, que usa diretamente o
`BoostConfig.cmake` instalado pelo vcpkg. No RME, `${Boost_LIBRARIES}` tambem
foi substituido pelos targets importados `Boost::thread` e `Boost::system`.

Depois da correcao:

```text
CMP0167 ausente nos logs atuais do TFS e do RME
TFS e RME configurados e compilados com sucesso
```

Permanece um aviso de codigo deprecado do Boost Bind no RME:

```text
declaring the Bind placeholders in the global namespace is deprecated
```

Esse aviso nao impediu o build e nao esta relacionado ao `CMP0167`.

### RME e OTClient

O primeiro build apresentou:

```text
LNK4098: defaultlib 'LIBCMTD' conflicts with use of other libs
```

A investigacao mostrou que o CMake, por usar um gerador multi-configuracao,
selecionou algumas bibliotecas da pasta `debug\lib` mesmo nos builds Release:

```text
RME:
debug\lib\archive.lib

OTClient:
debug\lib\lua51.lib
debug\lib\vorbisfile.lib
debug\lib\vorbis.lib
```

Essas bibliotecas solicitavam o runtime estatico de Debug `LIBCMTD`, enquanto
os executaveis eram compilados com o runtime estatico de Release `LIBCMT`.

O `Build-All.ps1` foi corrigido para fixar explicitamente as variantes da
pasta `lib` nos builds Release e RelWithDebInfo. Nao foi usado
`/NODEFAULTLIB`, pois essa opcao apenas esconderia o conflito.

Depois da correcao:

```text
LNK4098 ausente nos logs atuais
RME permaneceu aberto por 20 segundos sem crash
OTClient permaneceu aberto por 15 segundos sem crash
```

### OTClient

```text
MSB8027: dois arquivos luafunctions.cpp produzem saidas no mesmo local
```

O projeto contem dois arquivos com o mesmo nome:

```text
src\framework\luafunctions.cpp
src\client\luafunctions.cpp
```

No gerador Visual Studio, ambos poderiam produzir `luafunctions.obj` no mesmo
diretorio intermediario. O arquivo de framework tambem precisa ser compilado
separadamente do unity build por causa de uma contencao existente para um erro
interno do MSVC 19.44.

A correcao foi feita em `src\CMakeLists.txt`, sem renomear os fontes e sem
alterar simbolos C++:

```cmake
set_property(
        SOURCE framework/luafunctions.cpp
        PROPERTY VS_SETTINGS "ObjectFileName=$(IntDir)framework_luafunctions.obj"
)
```

O `.vcxproj` regenerado passou a produzir `framework_luafunctions.obj` para
esse fonte. Depois da recompilacao:

```text
MSB8027 ausente no log atual
LNK4098 ausente no log atual
OTClient permaneceu aberto por 15 segundos sem crash
```

## Executaveis recompilados

Os hashes novos diferem dos hashes do backup original porque os binarios foram
gerados novamente com a instalacao atual do MSVC.

```text
D:\tibia-oldschool\server\tfs.exe
2461184 bytes
SHA256 1FC42E564E671FF2694596CA2ACB2FC47CC89412C3B8806FB2E7882B4012C306

D:\tibia-oldschool\sources\rme-otacademy\rme.exe
14910976 bytes
SHA256 5DBE250A70DE16BC761B3997E28B4C1343894E1896DBB9CA43B3B97D45B907A0

D:\tibia-oldschool\sources\otclient-redemption\otclient.exe
18448384 bytes
SHA256 73D765504A2397E83F4C3F1690AF3B146716CC2250C5662937F5EC7254E6142B
```

Copias tambem foram colocadas em:

```text
D:\tibia-oldschool\build-results\executables
```

## Ajuste necessario no RME

O `rme.cfg` ainda continha os caminhos absolutos da copia usada na validacao
do backup:

```text
D:\tibia-oldschool-backup-teste-2026-06-10
```

Foram atualizados:

```text
file1
ASSETS_DATA_DIRS para a versao 7.72
```

Novo mapa:

```text
D:\tibia-oldschool\server\data\world\world.otbm
```

Novos assets:

```text
D:\tibia-oldschool\sources\otclient-redemption\data\things\772
```

Arquivo alterado:

```text
D:\tibia-oldschool\sources\rme-otacademy\rme.cfg
```

O `open-world.cmd` ja usava caminho relativo e nao precisou ser alterado.

## Smoke tests

### TFS

O TFS foi iniciado com:

```text
Working directory: D:\tibia-oldschool\server
Executable: D:\tibia-oldschool\server\tfs.exe
```

Resultado:

```text
database connection established
scripts loaded
monsters loaded
outfits loaded
map size 33500x33500
Tibia Oldschool 7.72 Test Server Online!
```

Log:

```text
D:\tibia-oldschool\build-results\logs\tfs-smoke-2026-06-10-stdout.log
```

### OTClient

Foi iniciado no proprio diretorio e permaneceu aberto por 15 segundos sem
crash imediato.

Registro:

```text
D:\tibia-oldschool\build-results\logs\otclient-smoke-2026-06-10.json
```

### RME

Foi iniciado com o mapa como argumento:

```text
D:\tibia-oldschool\server\data\world\world.otbm
```

Permaneceu aberto por 20 segundos sem crash imediato.

Registro:

```text
D:\tibia-oldschool\build-results\logs\rme-smoke-2026-06-10.json
```

## Atalhos

Pasta criada:

```text
C:\Users\guisu\OneDrive\Area de Trabalho\Tibia Oldschool - Testes
```

Atalhos:

```text
01 - Servidor TFS (banco de teste).lnk
02 - OTClient Redemption.lnk
03 - RME OTAcademy.lnk
```

O script existente foi alterado:

```text
D:\tibia-oldschool\tools\backup\Create-Desktop-Shortcuts.ps1
```

Mudanca principal:

- quando existe `D:\tibia-dev-tools\Start-Tibia-Server.cmd`, o atalho do TFS
  aponta para esse launcher;
- o icone continua sendo o `tfs.exe`;
- o launcher garante que o MariaDB esteja online antes de abrir o servidor.

Os tres atalhos foram executados em teste. Foram confirmados:

```text
processo tfs.exe ativo
porta 7171 aberta
porta 7172 aberta
processo otclient.exe ativo
processo rme.exe ativo
```

Depois da validacao, TFS, OTClient e RME foram fechados. O MariaDB foi mantido
ativo em segundo plano para permitir teste imediato.

## Diferencas em relacao aos documentos anteriores

### 1. Build Tools em vez do Visual Studio completo

Os documentos diziam para instalar Visual Studio 2022 com o workload de C++.
Foi usado Visual Studio Build Tools 2022, que contem o mesmo compilador,
MSBuild e SDK necessarios, mas nao instala o IDE completo.

Isso foi suficiente para os tres builds.

### 2. MariaDB ZIP portatil em vez de instalacao com servico

Os documentos afirmavam corretamente que copiar `Program Files` nao seria uma
restauracao confiavel. Nesta maquina nao foi copiada uma instalacao antiga.
Foi usada a distribuicao ZIP oficial nova do MariaDB 10.11.17 e um novo
diretorio de dados foi inicializado com `mariadb-install-db.exe`.

Portanto, o MariaDB atual e uma instalacao valida, mas sem servico Windows.

### 3. Launcher necessario para o servidor

Sem servico Windows, o banco nao inicia automaticamente com o computador.
Foi criado `Start-Tibia-Server.cmd`, que inicia o MariaDB e depois abre o TFS.
O atalho do servidor aponta para esse launcher.

### 4. Caminho real do projeto

Os documentos de backup citavam:

```text
D:\tibia-oldschool-backup-teste-2026-06-10
```

O projeto restaurado esta em:

```text
D:\tibia-oldschool
```

O script de build descobre a raiz automaticamente e funcionou sem alteracao.
O `rme.cfg`, por conter caminhos absolutos, precisou ser atualizado.

### 5. Python

O ambiente antigo registrava Python 3.12.13. Foi instalado Python 3.12.10,
mantendo Pillow 12.2.0. Nao houve incompatibilidade observada.

### 6. Uso minimo inevitavel do C

Quase todas as ferramentas e dados foram colocados no `D:`. Permaneceram no
`C:` somente componentes que o ecossistema Microsoft instala ou gerencia no
local padrao:

```text
Visual Studio Installer
Windows SDK
metadados por usuario
atalhos na Area de Trabalho do usuario
```

### 7. Git nao representa um repositorio deste snapshot

O Git portatil foi instalado para desenvolvimento futuro. O snapshot atual
continua sem historico `.git`, conforme documentado. Instalar Git nao recriou
o historico removido.

### 8. Colisao de objetos do OTClient corrigida no CMake

Os documentos anteriores apenas registravam o `MSB8027`. Nesta restauracao,
foi necessario definir um `ObjectFileName` exclusivo para
`framework\luafunctions.cpp`. Essa configuracao e especifica do gerador Visual
Studio e preserva os nomes e a organizacao atuais dos fontes.

## Arquivos alterados ou adicionados

Dentro do projeto:

```text
D:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\CMakeLists.txt
D:\tibia-oldschool\sources\rme-otacademy\CMakeLists.txt
D:\tibia-oldschool\sources\rme-otacademy\rme.cfg
D:\tibia-oldschool\sources\otclient-redemption\src\CMakeLists.txt
D:\tibia-oldschool\tools\backup\Build-All.ps1
D:\tibia-oldschool\tools\backup\Create-Desktop-Shortcuts.ps1
D:\tibia-oldschool\RELATORIO_RESTAURACAO_AMBIENTE_2026-06-10.md
```

Fora do projeto:

```text
D:\tibia-dev-tools\mariadb-data\my.ini
D:\tibia-dev-tools\Initialize-MariaDB.ps1
D:\tibia-dev-tools\Start-MariaDB.ps1
D:\tibia-dev-tools\Stop-MariaDB.ps1
D:\tibia-dev-tools\Start-Tibia-Server.cmd
```

Artefatos gerados:

```text
D:\tibia-oldschool\build-validation
D:\tibia-oldschool\build-results\executables
D:\tibia-oldschool\build-results\logs
```

## Como operar o ambiente atual

### Abrir para teste

Na Area de Trabalho, abrir nesta ordem:

```text
01 - Servidor TFS (banco de teste)
02 - OTClient Redemption
03 - RME OTAcademy
```

O primeiro atalho inicia o MariaDB quando necessario.

### Recompilar tudo

Em um PowerShell novo:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  "D:\tibia-oldschool\tools\backup\Build-All.ps1" `
  -Clean `
  -Parallel 8
```

Se o PowerShell ainda nao reconhecer `cmake`, use:

```powershell
$env:Path = "D:\tibia-dev-tools\cmake-4.3.2\bin;" + $env:Path
```

### Recriar atalhos

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  "D:\tibia-oldschool\tools\backup\Create-Desktop-Shortcuts.ps1" `
  -FolderName "Tibia Oldschool - Testes"
```

### Verificar MariaDB

```powershell
Test-NetConnection 127.0.0.1 -Port 3306
Get-Process mariadbd
```

### Encerrar MariaDB

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  "D:\tibia-dev-tools\Stop-MariaDB.ps1"
```

## Proximo passo planejado

O VS Code ainda nao foi instalado. A decisao original foi primeiro validar
manualmente TFS, OTClient e RME. Depois dessa validacao, instalar o VS Code e
configurar o Codex para trabalhar a partir de:

```text
D:\tibia-oldschool
```

Antes de alterar sources, uma nova conversa deve ler:

```text
D:\tibia-oldschool\LEIA-ME-BACKUP.md
D:\tibia-oldschool\RELATORIO_RESTAURACAO_AMBIENTE_2026-06-10.md
D:\tibia-oldschool\docs\RETOMADA_APOS_FORMATACAO_2026-06-10.md
D:\tibia-oldschool\docs\CONTEXTO_CONTINUACAO_2026-06-02.md
```

## Resumo para uma nova conversa Codex

```text
O ambiente foi restaurado apos formatacao em D:\tibia-oldschool.
Leia RELATORIO_RESTAURACAO_AMBIENTE_2026-06-10.md antes de executar qualquer
alteracao. As ferramentas estao em D:\tibia-dev-tools. O MariaDB 10.11.17 e
portatil, sem servico Windows, e o atalho do TFS inicia o banco por meio de
Start-Tibia-Server.cmd. TFS, OTClient Redemption e RME OTAcademy foram
recompilados e passaram nos smoke tests. O rme.cfg ja foi corrigido para a
raiz D:\tibia-oldschool. Preserve juntos Tibia.dat, Tibia.spr e Tibia.cwm.
Nao apague as dependencias vcpkg preservadas.
```
