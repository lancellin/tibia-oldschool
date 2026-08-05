# Correções no login/logout assíncrono — 2026-08-05

## Contexto

Auditoria independente do estado atual da arquitetura de login/logout
assíncrono (`player_io_service` + `PlayerIOManager`), confrontando:

- o relatório técnico (`RELATORIO_LOGIN_LOGOUT_ASSINCRONO.md`);
- o plano de testes (`PLANO_TESTES_LOGIN_LOGOUT_ASSINCRONO.md`);
- a auditoria crítica registrada nas sessões do Codex de 29/07/2026;
- os logs de runtime de 01/08/2026 (`server/player-io-service-launch.*.log`);
- o código-fonte atual (últimas modificações em 03/08/2026).

## O que foi confirmado como JÁ CORRIGIDO no código atual

1. **Worker não recebe `Player*` vivo**: `LogoutTask` transporta apenas
   jobId, revisão e comandos SQL imutáveis; o snapshot é construído no
   Dispatcher (`buildPlayerSaveSnapshot`).
2. **Erro de consulta no login é fatal**: o worker valida identidade,
   contagem de linhas e resultados; a materialização verifica
   `replay.getError()` e `replayComplete()` — divergência de ordem/texto de
   query aborta o login.
3. **Corrida de relog coberta nos dois níveis**: reserva local
   (`savingPlayers`/`loadingPlayerIds`/`legacyPlayers`) e verificação de jobs
   pendentes no serviço dentro de transação `REPEATABLE READ` com
   `FOR UPDATE` (`QUERY_BATCH`).
4. **Escritores legados sob reserva central**: mailbox, house, market,
   player shop, bed e Lua passam por `loadPlayerById`/`savePlayer`, que
   usam `reserveLegacyOperation` (consulta também o journal remoto via
   `CHECK_PLAYER_READY`).
5. **Clean save drena saves assíncronos antes de commitar**
   (`beginFloorPersistenceCleanSave` chama `drain` antes de `saveGameState`).
6. **Sem reconexão dentro de transação no serviço**: `MYSQL_OPT_RECONNECT`
   desativado; `connect()` recusa com transação ativa; rollback não
   reconecta.
7. **Timeouts limitados em todas as operações TCP do cliente** e conexão
   fechada após cada resposta (elimina o ruído de timeout ocioso visto em
   01/08).
8. **Recuperação contínua de jobs PENDING** (thread de recuperação a cada
   ~1s, com janela de 30s para jobs novos) + recuperação completa no
   startup.
9. **Ferramentas administrativas**: `inspect-job` e `retry-failed` para jobs
   FAILED; poda do journal no safe shutdown.

## Defeitos remanescentes corrigidos nesta data

### 1. UB em `PropStream::init` com coluna SQL NULL (`src/fileloader.h`)

`init(nullptr, 0)` calculava `nullptr + size` (comportamento indefinido).
Ocorre sempre que uma coluna binária está NULL (`attributes` em
`loadItems`, `conditions` no load, `data` no `iomapserialize`).
Correção: ponteiro nulo ancora `p`/`end` em sentinela não nula; toda leitura
falha graciosamente. (Correção mínima recomendada pela auditoria de 29/07.)

### 2. Callback de login publicado após início do shutdown (`src/playeriomanager.cpp`)

Se `stopping` fosse marcado durante o I/O do worker de login, o callback era
postado ao Dispatcher — que pode já estar em `CLOSING` e descartar a tarefa,
vazando as reservas de nome/id. Correção: após o I/O, o worker revalida
`stopping`; se ativo, libera as reservas localmente e não publica callback.

### 3. Deadlocks MariaDB (1213) e lock wait timeout (1205) sem retry no serviço (`src/playerioservice.cpp`)

Evidência de runtime: `Failed to recover player save job ... Deadlock found`
em 01/08. Deadlocks são esperados quando `QUERY_BATCH` (login) e `applyJob`
(save) contendem pelas mesmas linhas; a recomendação do MariaDB é repetir a
transação. Correção: retry limitado (3 tentativas, backoff 50/100/150 ms)
para `QUERY`, `QUERY_BATCH` e `PREPARE_SAVE_JOB` somente quando o erro é
1205/1213. Antes, um deadlock virava falha de login visível ao jogador ou
ruído de recuperação. `APPLY` mantém o tratamento próprio (job volta a
PENDING e é retomado pelo worker/recovery).

### 4. Timeout do canal de handoff durável curto demais (`src/playeriomanager.cpp`)

`handoffClient` tinha 1s — pouco para PREPARE de personagens pesados sob
contenção de locks, convertendo latência comum em "resultado ambíguo" e
churn de recuperação. Correção: 5s (igual ao padrão dos demais canais).
O health monitor continua com 1s (ping é leve e precisa detectar queda
rápido).

## Limitações conhecidas e aceitas (não alteradas)

- **Janela sem journal durável**: o snapshot é construído no Dispatcher e o
  PREPARE chega ao serviço de forma assíncrona. Se o **processo do TFS
  cair** (crash/kill) nesse intervalo, o save pendente só existe na memória
  do TFS e é perdido. Shutdown normal drena tudo; a janela só se aplica a
  queda abrupta. Tornar o PREPARE síncrono reintroduziria o freeze do
  Dispatcher que a arquitetura elimina.
- Jobs **FAILED** bloqueiam o relog do personagem até resolução
  administrativa (`player_io_service.exe <conf> retry-failed <job> <rev>`).
- Persistência de tiles, houses, mail, checkpoints e demais sistemas
  permanecem fora do serviço (escopo deliberado do relatório).

## Validação

- Compilação Release de `tfs` e `player_io_service` (Visual Studio 2022,
  toolchain vcpkg do projeto).
- Campanha de testes funcional continua regida pelo
  `PLANO_TESTES_LOGIN_LOGOUT_ASSINCRONO.md` (T-01 a T-20 ainda não
  executados/marcados).

## Arquivos alterados

- `sources/nekiro-tfs-1.5-7.72/src/fileloader.h`
- `sources/nekiro-tfs-1.5-7.72/src/playeriomanager.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/playerioservice.cpp`
