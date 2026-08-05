# BACKUP_RESTORATION_CHANGELOG

Registro cronologico das alteracoes, validacoes, correcoes de build, decisoes operacionais e artefatos produzidos durante a conversa focada em restauracao de ambiente, recompilacao integral e preparo de backup recompilavel em `D:\tibia-oldschool`.

## Resumo Executivo

Esta conversa tratou do tema mais sensivel do projeto ate aqui: a capacidade de reconstruir o ambiente inteiro depois de uma formatacao critica do Windows, recompilar TFS/RME/OTClient com as dependencias corretas, validar a execucao real e transformar esse conhecimento em um backup suficientemente guiado para uma restauracao futura por um unico pedido ao Codex.

O trabalho teve quatro eixos principais:

1. restauracao local da maquina em `D:\tibia-oldschool`, com ferramentas instaladas preferencialmente em `D:\tibia-dev-tools`;
2. correcao de problemas reais de build e link, incluindo `CMP0167`, `LNK4098`, `MSB8027` e o warning de placeholders globais do Boost Bind;
3. consolidacao de documentacao tecnica e relatorio detalhado do processo de restauracao;
4. preparacao de uma copia separada do backup para nuvem, sem executaveis regeneraveis e sem dependencia de incluir `D:\tibia-dev-tools` no pacote final.

O resultado pratico desta etapa foi:

- TFS recompilado e chegando a `Tibia Oldschool 7.72 Test Server Online!`;
- OTClient recompilado e permanecendo aberto por 15 segundos sem crash;
- RME recompilado, abrindo o mapa e permanecendo aberto por 20 segundos sem crash;
- `Build-All.ps1` ajustado para builds reproduziveis;
- `RELATORIO_RESTAURACAO_AMBIENTE_2026-06-10.md` convertido em referencia forte do estado real da maquina;
- preparacao de um backup separado e limpo para upload, com automacao de restauracao ponta a ponta.

## Fase 1. Leitura Inicial E Levantamento Do Estado

### Escopo assumido

- A pasta principal de trabalho foi confirmada como `D:\tibia-oldschool`.
- A instruicao inicial exigia leitura de `LEIA-ME-BACKUP.md` e de toda a pasta `docs`.
- O objetivo nao era apenas recompilar, mas entender o backup existente, identificar lacunas de documentacao e chegar num estado que pudesse ser repetido futuramente.

### Arquivos de referencia inspecionados no workspace

- `D:\tibia-oldschool\LEIA-ME-BACKUP.md`
- `D:\tibia-oldschool\docs\RETOMADA_APOS_FORMATACAO_2026-06-10.md`
- `D:\tibia-oldschool\docs\BACKUP_RECOMPILAVEL_VALIDADO_2026-06-10.md`
- `D:\tibia-oldschool\docs\COMO_CRIAR_BACKUP_RECOMPILAVEL.md`
- `D:\tibia-oldschool\docs\CONTEXTO_CONTINUACAO_2026-06-02.md`
- `D:\tibia-oldschool\docs\architecture.md`
- `D:\tibia-oldschool\docs\assets-workflow.md`
- `D:\tibia-oldschool\docs\backup.md`

### Conclusoes iniciais

- O backup preservava as dependencias vcpkg necessarias para os tres builds.
- A documentacao explicava boa parte do processo, mas ainda deixava lacunas relevantes para repetir a restauracao sem tentativa e erro.
- Havia conhecimento pratico nao consolidado em um unico fluxo: onde instalar ferramentas, como tratar MariaDB no `D:`, como fixar warnings/erros de link e como limpar o backup para nuvem sem destruir dependencias importantes.

## Fase 2. Restauracao Da Toolchain Em `D:\tibia-dev-tools`

### Politica de disco

- O usuario pediu que o `C:` fosse evitado, exceto se absolutamente necessario.
- A decisao operacional foi concentrar as ferramentas em `D:\tibia-dev-tools`, aceitando uso minimo inevitavel do `C:` para componentes do ecossistema Microsoft.

### Ferramentas instaladas ou extraidas

- `D:\tibia-dev-tools\cmake-4.3.2`
- `D:\tibia-dev-tools\Git`
- `D:\tibia-dev-tools\Python312`
- `D:\tibia-dev-tools\mariadb-10.11.17`
- `D:\tibia-dev-tools\VisualStudio\2022\BuildTools`
- `D:\tibia-dev-tools\installers`
- `D:\tibia-dev-tools\logs`
- `D:\tibia-dev-tools\temp`

### Downloads validados

- `vs_BuildTools.exe`
- `cmake-4.3.2-windows-x86_64.zip`
- `mariadb-10.11.17-winx64.zip`
- `python-3.12.10-amd64.exe`
- `PortableGit-2.54.0-64-bit.7z.exe`

### Componentes externos e versoes registradas

- Visual Studio Build Tools 2022 `17.14.37328.6`
- MSBuild `17.14.40.60911`
- MSVC compiler `19.44.35228`
- Windows SDK `10.0.26100.0`
- CMake `4.3.2`
- MariaDB `10.11.17`
- Git for Windows `2.54.0.windows.1`
- Python `3.12.10`
- Pillow `12.2.0`

### Variaveis e caminhos importantes

- `PATH` recebeu entradas para:
  - `D:\tibia-dev-tools\cmake-4.3.2\bin`
  - `D:\tibia-dev-tools\Git\cmd`
  - `D:\tibia-dev-tools\Python312`
  - `D:\tibia-dev-tools\Python312\Scripts`
  - `D:\tibia-dev-tools\mariadb-10.11.17\bin`
- O alias quebrado da Microsoft Store em `C:\Users\guisu\AppData\Local\Microsoft\WindowsApps\python.exe` deixou de ser a referencia principal do Python para o projeto.

## Fase 3. MariaDB Portatil E Banco De Teste

### Decisao principal

- Em vez de depender de instalacao tradicional com servico Windows, foi usada a distribuicao ZIP oficial do MariaDB em `D:\tibia-dev-tools\mariadb-10.11.17`.
- O banco passou a rodar como instancia local portatil, sem servico, ouvindo somente em `127.0.0.1:3306`.

### Estrutura criada fora do projeto

- `D:\tibia-dev-tools\Initialize-MariaDB.ps1`
- `D:\tibia-dev-tools\Start-MariaDB.ps1`
- `D:\tibia-dev-tools\Stop-MariaDB.ps1`
- `D:\tibia-dev-tools\Start-Tibia-Server.cmd`
- `D:\tibia-dev-tools\mariadb-data\my.ini`
- `D:\tibia-dev-tools\mariadb-data\data`
- `D:\tibia-dev-tools\mariadb-data\logs`
- `D:\tibia-dev-tools\mariadb-data\root-credential.xml`

### Variaveis lidas do projeto

O bootstrap do banco nao fixou credenciais em novo documento; ele leu do proprio `server\config.lua` os campos:

- `mysqlHost`
- `mysqlUser`
- `mysqlPass`
- `mysqlDatabase`
- `mysqlPort`

### Dump e validacao funcional

- Dump usado: `D:\tibia-oldschool\backup-extras\database\oldschool772db-2026-06-10.sql`
- O banco de teste ativo durante a restauracao foi `oldschool772db_backup_test_20260610`.
- Validacao final do banco:
  - `players=6`
  - `accounts=1`

### Decisoes de seguranca

- A senha root do MariaDB foi gerada aleatoriamente.
- A credencial foi exportada via DPAPI para `root-credential.xml`, sem ser copiada para changelog ou relatorio.
- Documentos de backup e relatorio foram higienizados para nao expor logins/senhas diretamente.

## Fase 4. Orquestracao De Build

### Script central

O build consolidado ficou encapsulado em:

- `D:\tibia-oldschool\tools\backup\Build-All.ps1`

### Variaveis principais de `Build-All.ps1`

- `$Root`
- `$CMake`
- `$Toolchain`
- `$ClientInstalled`
- `$StaticInstalled`
- `$ClientStaticInstalled`
- `$BuildRoot`
- `$Logs`
- `$Results`

### Diretorios operacionais usados pelo script

- `D:\tibia-oldschool\build-validation`
- `D:\tibia-oldschool\build-results\logs`
- `D:\tibia-oldschool\build-results\executables`

### Configuracoes de build por componente

#### TFS

- gerador: `Visual Studio 17 2022`
- plataforma: `x64`
- triplet: `x64-windows`
- flag relevante: `-DSKIP_GIT=ON`

#### RME

- gerador: `Visual Studio 17 2022`
- plataforma: `x64`
- triplet: `x64-windows-static`
- biblioteca fixada explicitamente:
  - `-DLibArchive_LIBRARY=.../lib/archive.lib`

#### OTClient

- gerador: `Visual Studio 17 2022`
- plataforma: `x64`
- triplet alvo: `x64-windows-static`
- triplet host: `x64-windows-static`
- `-DVCPKG_INSTALLED_DIR`
- `-DVCPKG_MANIFEST_MODE=OFF`
- `-DVCPKG_BUILD_TYPE=release`
- `-DBUILD_STATIC_LIBRARY=ON`
- `-DOTCLIENT_BUILD_TESTS=OFF`
- `-DSPEED_UP_BUILD_UNITY=ON`
- `-DOPTIONS_ENABLE_SCCACHE=OFF`
- bibliotecas fixadas explicitamente:
  - `-DLUAJIT_LIBRARY=.../lib/lua51.lib`
  - `-DVORBISFILE_LIBRARY=.../lib/vorbisfile.lib`
  - `-DVORBIS_LIBRARY=.../lib/vorbis.lib`

## Fase 5. Correcao De Problemas Reais De Build

### 1. `CMP0167` com `FindBoost`

#### Causa

- O CMake moderno removeu o modulo legado `FindBoost`.
- TFS e RME ainda chamavam `find_package(Boost ...)` sem explicitar modo `CONFIG`.

#### Arquivos corrigidos

- `D:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\CMakeLists.txt`
- `D:\tibia-oldschool\sources\rme-otacademy\CMakeLists.txt`

#### Mudancas centrais

- TFS:
  - `find_package(Boost 1.66.0 CONFIG REQUIRED COMPONENTS date_time system filesystem iostreams)`
- RME:
  - `find_package(Boost 1.34.0 CONFIG REQUIRED COMPONENTS thread system)`
  - link explicitamente com `Boost::thread` e `Boost::system`

#### Resultado

- `CMP0167` saiu dos logs atuais do TFS e do RME.

### 2. `LNK4098 defaultlib 'LIBCMTD' conflicts`

#### Causa

- O gerador multi-configuracao podia misturar bibliotecas `debug\lib` com builds `Release` e `RelWithDebInfo`.
- O problema atingia principalmente RME e OTClient.

#### Arquivo central corrigido

- `D:\tibia-oldschool\tools\backup\Build-All.ps1`

#### Decisao tecnica

- As bibliotecas corretas de Release foram fixadas explicitamente no comando CMake.
- Nao foi usado `/NODEFAULTLIB`, porque isso mascararia o problema em vez de resolver a origem da mistura de runtimes.

#### Resultado

- `LNK4098` deixou de aparecer nos logs atuais.

### 3. `MSB8027` no OTClient

#### Causa

Dois arquivos-fonte diferentes geravam potencialmente o mesmo objeto intermediario:

- `framework/luafunctions.cpp`
- `client/luafunctions.cpp`

#### Arquivo corrigido

- `D:\tibia-oldschool\sources\otclient-redemption\src\CMakeLists.txt`

#### Linha conceitualmente importante

Foi definido:

- `PROPERTY VS_SETTINGS "ObjectFileName=$(IntDir)framework_luafunctions.obj"`

#### Resultado

- O objeto do fonte de framework passou a usar nome exclusivo.
- `MSB8027` deixou de aparecer nos logs atuais.

### 4. Warning de placeholders globais do Boost Bind no RME

#### Causa

- O parser JSON legado do RME (`json_spirit`) depende da semantica antiga dos placeholders globais `_1`, `_2`, etc.

#### Arquivo corrigido

- `D:\tibia-oldschool\sources\rme-otacademy\CMakeLists.txt`

#### Mudanca central

- `target_compile_definitions(rme PRIVATE BOOST_BIND_GLOBAL_PLACEHOLDERS)`

#### Resultado

- O comportamento antigo do parser foi preservado.
- O warning de deprecacao correspondente deixou de ser pendencia do build limpo validado.

## Fase 6. Ajustes De Runtime E Operacao Local

### RME

- `D:\tibia-oldschool\sources\rme-otacademy\rme.cfg` foi corrigido para apontar para a raiz real `D:\tibia-oldschool`.
- O mapa ativo permaneceu:
  - `D:\tibia-oldschool\server\data\world\world.otbm`
- Os assets ativos do editor/client permaneceram:
  - `D:\tibia-oldschool\sources\otclient-redemption\data\things\772`

### Atalhos

O script de atalhos foi ajustado para a realidade restaurada:

- `D:\tibia-oldschool\tools\backup\Create-Desktop-Shortcuts.ps1`

Atalhos validados:

- `01 - Servidor TFS (banco de teste).lnk`
- `02 - OTClient Redemption.lnk`
- `03 - RME OTAcademy.lnk`

## Fase 7. Smoke Tests E Verificacao Funcional

### TFS

- iniciou em primeiro plano
- conectou no banco
- carregou scripts, monsters, outfits e mapa
- chegou a `Tibia Oldschool 7.72 Test Server Online!`
- validou portas `7171` e `7172`

### OTClient

- iniciado no proprio diretorio de runtime
- permaneceu aberto por `15` segundos sem crash imediato

### RME

- iniciado com o mapa do projeto
- permaneceu aberto por `20` segundos sem crash imediato

### Hashes preservados antes da recompilacao

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

### Hashes validados depois da recompilacao local

- `D:\tibia-oldschool\server\tfs.exe`
  - `1FC42E564E671FF2694596CA2ACB2FC47CC89412C3B8806FB2E7882B4012C306`
- `D:\tibia-oldschool\sources\rme-otacademy\rme.exe`
  - `5DBE250A70DE16BC761B3997E28B4C1343894E1896DBB9CA43B3B97D45B907A0`
- `D:\tibia-oldschool\sources\otclient-redemption\otclient.exe`
  - `73D765504A2397E83F4C3F1690AF3B146716CC2250C5662937F5EC7254E6142B`

## Fase 8. Consolidacao Documental

### Documento mais importante desta fase no workspace atual

- `D:\tibia-oldschool\RELATORIO_RESTAURACAO_AMBIENTE_2026-06-10.md`

Esse relatorio passou a registrar:

- estado inicial da maquina apos formatacao;
- versoes baixadas;
- estrutura de `D:\tibia-dev-tools`;
- bootstrap do MariaDB;
- comandos de build;
- explicacao das correcoes de `CMP0167`, `LNK4098` e `MSB8027`;
- hashes antes/depois;
- smoke tests;
- diferencas praticas entre documentacao antiga e procedimento real.

### Higienizacao de credenciais

- O pedido explicito do usuario foi remover login e senha do relatorio por causa de backup.
- O relatorio final foi revisado para nao manter credenciais operacionais em texto explicito.

## Fase 9. Preparacao De Backup Recompilavel Para Nuvem

### Base escolhida

- A base do backup para nuvem foi a copia historica `D:\tibia-oldschool-backup-teste-2026-06-10`.
- Essa copia foi preferida como materia-prima porque representava o snapshot original antes das alteracoes locais feitas durante a restauracao.

### Objetivo desse segundo fluxo

- gerar uma pasta autocontida do projeto;
- nao depender de incluir `D:\tibia-dev-tools` na nuvem;
- deixar apenas sources, dados, dependencias preservadas em `tools` e scripts suficientes para recriar a toolchain depois da formatacao;
- remover executaveis, `build-validation`, `build-results` e outros artefatos regeneraveis.

### Scripts adicionados no backup limpo preparado durante a conversa

No backup separado de upload foram criados ou consolidados scripts como:

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

### Documentos principais gerados no backup limpo

- `LEIA-ME-BACKUP.md`
- `docs\RESTAURACAO_COMPLETA_POS_FORMATACAO.md`
- `docs\VALIDACAO_FINAL_BACKUP_ONLINE_2026-06-10.md`

### Principio operacional consolidado

O backup final para nuvem nao deveria conter:

- `server\tfs.exe`
- `sources\rme-otacademy\rme.exe`
- `sources\otclient-redemption\otclient.exe`
- `sources\otclient-redemption\RelWithDebInfo`
- `build-validation`
- `build-results`
- `backup-extras\pre-rebuild-binaries`

Mas deveria manter:

- `tools\vcpkg\installed\x64-windows`
- `tools\vcpkg\installed\x64-windows-static`
- `tools\dependencies\otclient-vcpkg-installed\x64-windows-static`
- `backup-extras\database`
- `backup-extras\Sprites Permanentes`

### Validacao do backup limpo

Durante a conversa, o backup limpo preparado para upload foi:

- recompilado do zero;
- submetido novamente a smoke tests;
- validado contra reaparecimento de `CMP0167`, `LNK4098`, `MSB8027` e warning do Boost Bind;
- testado com inicializacao fresca de MariaDB em diretorio separado;
- limpo novamente de artefatos regeneraveis;
- validado por script antes de ser considerado apto para nuvem.

## Fase 10. Conversa Sobre Codex Cloud

### Assuntos tratados

- diferenca entre usar o Codex local/IDE e a opcao `Hand off to Codex in the cloud`
- tipos de tarefa mais adequados para handoff
- implicacoes de custo/creditos baseadas em processamento, modelo e tokens

### Resultado pratico

- Nenhum arquivo do projeto foi alterado por essa discussao.
- O entendimento consolidado foi operacional: usar o cloud para tarefas longas, fechadas e autonomas, como build, limpeza, relatorio e validacao em background.

## Arquivos Mais Importantes Desta Conversa No Workspace Atual

- `D:\tibia-oldschool\RELATORIO_RESTAURACAO_AMBIENTE_2026-06-10.md`
- `D:\tibia-oldschool\tools\backup\Build-All.ps1`
- `D:\tibia-oldschool\tools\backup\Create-Desktop-Shortcuts.ps1`
- `D:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\CMakeLists.txt`
- `D:\tibia-oldschool\sources\rme-otacademy\CMakeLists.txt`
- `D:\tibia-oldschool\sources\rme-otacademy\rme.cfg`
- `D:\tibia-oldschool\sources\otclient-redemption\src\CMakeLists.txt`

## Regras E Decisoes Consolidadas

- `D:\tibia-oldschool` e a raiz de trabalho principal.
- `D:\tibia-dev-tools` e o local preferencial para toolchain externa.
- O `C:` so deve ser usado quando o proprio ecossistema Microsoft ou Windows obrigar.
- O TFS, o RME e o OTClient devem ser recompilados com build limpo quando o objetivo for validar o ambiente.
- O servidor nao deve depender de um servico MariaDB tradicional para a restauracao descrita nesta conversa.
- `server\config.lua` continua sendo arquivo operacional privado e nao deve ser replicado em changelogs com valores de senha.
- `Tibia.dat`, `Tibia.spr` e `Tibia.cwm` devem sempre ser tratados como conjunto.
- As dependencias preservadas em `tools\vcpkg\installed` e `tools\dependencies\otclient-vcpkg-installed` nao sao "lixo de build"; elas fazem parte do backup recompilavel.

## Pendencias E Limites

- A instalacao do VS Code e o uso do Codex a partir dele foram mencionados como proximo passo, mas nao foram o centro tecnico deste changelog.
- Parte da automacao de backup limpo foi produzida em uma copia separada do projeto, e nao na raiz atual `D:\tibia-oldschool`.
- Este changelog documenta o processo completo da conversa, inclusive a preparacao do backup para nuvem, mesmo quando alguns artefatos dessa copia separada nao estao mais presentes no workspace atual.
