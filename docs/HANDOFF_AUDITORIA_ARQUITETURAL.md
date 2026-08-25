# HANDOFF — Auditoria arquitetural e correções (tibia-oldschool)

_Atualizado em 2026-08-25 — versão atual **v0.3.0** (branch `agent/rollback-v0-2-3-sha1`, GitHub lancellin/tibia-oldschool)._

**Para continuar em conversa nova:** leia este documento + a memória automática do
projeto (audit status + working style). O histórico da conversa anterior NÃO é
necessário — tudo que importa está aqui e na memória.

---

## 1. O projeto

- OT server "old school": fork do **TFS 1.5 (nekiro)** para protocolo **7.72**.
- Fonte C++: `sources/nekiro-tfs-1.5-7.72/src` (186+ arquivos).
- Scripts Lua do servidor: `server/data` (NPCs em `server/data/npc`).
- Sistemas custom do fork: floor persistence + CheckpointWorker, login/logout
  assíncrono via `player_io_service.exe` externo, CAM (gravação/reprodução de
  pacotes), bestiário, Elite Creatures, dispatcher/autosend metrics.
  (A camada "CAM forensics" foi REMOVIDA — commitada na v0.3.0; ver fila/decisões.)
- Sistemas com sistemas de métricas existentes (NÃO inchar colunas):
  DispatcherPhaseMetrics, AutosendMetrics.

## 2. Estado atual

- **v0.3.0** commitada, taggeada (tag anotada) e publicada no GitHub em
  2026-08-25 (branch `agent/rollback-v0-2-3-sha1`): consolida as correções
  A4+A5+A6+A8+A10+A11+M3 (auditoria arquitetural) e P1+P2+P4 (auditoria de
  segurança do protocolo de 2026-08-22), servidor e client. Binário
  deployado em `server\tfs.exe` após rebuild com a tag (SHA256 registrado
  no fim desta seção).
  Backups em `server\backup_executables\`:
  `tfs.before-cam-forensics-removal-20260821.exe` (estado v0.2.9),
  `tfs.before-depot-leak-fix-20260821.exe` (v0.2.10 sem A5),
  `tfs.before-browsefield-fix-20260821.exe` (v0.2.10+A5 sem A6),
  `tfs.before-deferred-save-fix-20260821.exe` (v0.2.10+A5+A6 sem A8),
  `tfs.before-lifecycle-race-fix-20260821.exe` (v0.2.10+A5+A6+A8 sem A10),
  `tfs.before-output-queue-cap-20260821.exe` (v0.2.10+A5+A6+A8+A10 sem A11),
  `tfs.before-charm-cache-fix-20260821.exe` (v0.2.10+A5+A6+A8+A10+A11 sem M3),
  `tfs.before-a11-accounting-fix-20260821.exe` (A11 com o bug de underflow,
  pré-correção da contabilidade) e
  `tfs.before-shop-async-save-20260824.exe` (v0.2.10+A5+A6+A8+A10+A11+M3,
  pré-correção P1 do PlayerShop),
  `tfs.before-house-vip-hardening-20260824.exe` (estado com P1-shop,
  pré-hardening P2/P4) e
  `tfs.before-v0.3.0-rebuild-20260825.exe` (estado P2/P4 pré-rebuild da
  tag v0.3.0 — mesmo código, banner ainda sem a tag).
- Banner de versão no terminal do servidor: `tibia-oldschool vX.Y.Z[z] [git sha]`
  — sufixo `z` + aviso "BUILD DE TESTE" quando há mudanças tracked não commitadas
  (gerado a cada build por `cmake/gen_git_version.cmake`, automático).
- Working tree limpo após a v0.3.0 (servidor e client commitados).

## 3. O que já foi feito (commits no GitHub)

| Versão | Conteúdo |
|---|---|
| v0.2.7 | C2 bestiário em memória (flush em savePlayerData); C3 isolamento de scripts de NPC (lua_setfenv por script) + NpcHandler:story + contexto de NPC em timers (npcId); N1+N2+N3 lifecycle de shop (ponteiros pendurados Player<->Npc); compat legacy de NPCs (guards, NPCSay, vials FLUID_NONE, bless, getTibiaTime, questpoints) |
| v0.2.8 | A2 invalidação REGIONAL do cache de spectators + instrumentação A/B + banner de versão. Experimento: hit rate ~30%→~90%, scans ~3x menor, ~12 mil shadow-checks com 0 divergências |
| v0.2.9 | Remoção da instrumentação A/B (mantém correção A2 + banner) |
| v0.2.10 | Correção A4: remoção completa da camada CAM Forense (server + client). SHA-256 por pacote, assinatura Ed25519 sob mutex global e evidência por item saem do caminho de envio; CAM de gravação/reprodução permanece. PEMs deletados. Detalhes em `docs/releases/v0.2.10-cam-forensics-removal.md` |
| v0.2.10+A5 (working tree) | Correção A5: leak do depot inteiro a cada logout. `Player::getDepotChest` incrementa a referência do chest criado fora de `CreateItem`; `~Player` libera chests órfãos (sem locker) por último no destrutor. Conteúdo do depot (refcount 1 via `CreateItem`) agora é liberado corretamente em cascata |
| v0.2.10+A5+A6 (working tree) | Correção A6: browse fields. Ramo ITEM_BROWSEFIELD de `~Container` restaurado (erase do mapa `browseFields` + parents dos itens do tile restaurados sem decremento) e refcount de abrir/fechar browse field restaurado em `Player::addContainer`/`closeContainer`. Recurso segue dormente no 7.72, porém sem use-after-free/underflow latente |
| v0.2.10+A5+A6+A8 (working tree) | Correção A8: save de logout com checkpoint em voo deixa de congelar o Dispatcher — é capturado como SQL imutável e enfileirado atrás do checkpoint no worker (ordem garantida pelo FIFO), com retry limitado, replay síncrono se o worker morrer e legacy reservation segurando o relog até o commit. Barreira de drain antiga mantida como fallback (clean save/shutdown/worker doente/fila cheia/player_io desabilitado) |
| v0.2.10+A5+A6+A8+A10 (working tree) | Correção A10: race de lifecycle Player*/release. `ProtocolGame::release()` adquire `connectionLock` como barreira antes de destruir o player — após `close()` marcar `closed=true` sob o mesmo lock, nenhum parse pode estar rodando/iniciar; elimina use-after-free e data race no logout/desconexão |
| v0.2.10+A5+A6+A8+A10+A11 (working tree) | Correção A11: backpressure na fila de envio por conexão (caps 256 msgs / 1 MiB) com desconexão forçada e métricas de profundidade/pico/overflow no `ConnectionManager`; impede crescimento ilimitado de RAM com cliente lento/malicioso |
| v0.2.10+A5+A6+A8+A10+A11+M3 (working tree) | Correção M3: charm states viram cache-em-memória na sessão — removidas as 4 releituras síncronas de `player_charms` no Dispatcher (janela de bestiary/charms e caminhos de unlock/activate); login segue sendo a única leitura |
| v0.2.10+A5+A6+A8+A10+A11+M3+P1-shop (working tree) | Correção P1 (auditoria de protocolo 2026-08-22): PlayerShop `buy` deixa de fazer 2× `savePlayer` síncrono no Dispatcher — save vira checkpoint conjunto assíncrono (`Game::savePlayerShopPurchase` → `registerTradeCheckpoint` + `enqueueFloorCheckpointGroup`), commitado pelo CheckpointWorker em uma única transação; fallback síncrono legado quando o mecanismo está indisponível |
| v0.2.10+…+P1-shop+P2/P4 (working tree) | Hardening P2/P4 (auditoria de protocolo): access list de house sem suporte a guild (`@` ignorado — eliminadas até 3 queries DB síncronas por linha + o leak de `Guild` de `getGuildByName`) e com teto de 40 resoluções de nome por parse; rate limit de 1 s em 0x9A/0xDC/0xDE (campos `next*Request` em protocolgame.h) + descrição VIP limitada a 255 chars; cooldown de 2 s no `!deathlist` (Lua). Cache name→guid ficou DEFERIDO (risco de stale entries; residual aceitável com os caps) |
| **v0.3.0** (commit + tag anotada + push em 2026-08-25) | Release que consolida TODAS as linhas "(working tree)" acima, servidor e client: A4 (remoção CAM forense server+client, PEMs deletados), A5 (leak do depot), A6 (browseFields dormente correto), A8 (save de logout diferido no checkpoint worker), A10 (barreira de lifecycle connectionLock), A11 (caps da fila de envio), M3 (charms cache-em-memória), P1 (PlayerShop buy → checkpoint conjunto assíncrono atômico), P2/P4 (hardening house access list + VIP/private/deathlist), CWM desacoplado do fast-pack no client. Inclusos: docs (handoff + release notes da CAM forense), CSVs do experimento A/B de spectators e `tools/map/replace_item_ids.py` |

Backups de binário: `server\backup_executables\` (convenção
`tfs.before-<mudanca>-<data>.exe` ou `tfs.<versao>-before-<proxima>-<data>.exe`).

## 4. Decisões do usuário (NÃO revisitar)

- **C1** (retry de DB dormindo sob lock global): INTENCIONAL, não mexer.
- **A1** (auth síncrona na thread de rede): INTENCIONAL.
- **A3** (limpeza de chão): manual semanal, exceto no servidor de teste.
- **A4 (CAM forense): REMOVIDA por decisão do usuário (2026-08-21).** Racional:
  arquivo local é impossível de proteger 100%; a camada não impede fraude de
  quem quer fraudar e impunha custo criptográfico a todos. Não reintroduzir
  assinaturas/hash por pacote no caminho de envio.
- Bestiário aceita rollback de alguns minutos em crash (consciente).
- `talk_state` por NPC (não por player) é aceitável por ora.
- **Sem múltiplos agentes** — economizar recursos; análise com buscas diretas.
- Usar **CodeMemory** nas análises (CLI, ver §6).
- Explicar/planejar antes de implementar; o usuário aprova cada etapa.
- Instrumentação de métricas sempre em ARQUIVO SEPARADO (não inchar colunas).

## 5. Fila pendente (próximos trabalhos)

1. ~~**A4 — CAM forensics**~~: RESOLVIDO na v0.2.10 com a REMOÇÃO completa da
   camada (decisão do usuário; ver §4 e `docs/releases/v0.2.10-cam-forensics-removal.md`).
2. ~~**A5 — leak do depot inteiro a cada logout**~~: RESOLVIDO (working tree,
   2026-08-21, junto da A4): `Player::getDepotChest` incrementa a referência do
   chest ao criá-lo (invariante "container dono segura 1 referência", liberada
   pelo `~Container` do locker) e `~Player` libera chests órfãos (criados sem
   locker via load de `player_depotitems` ou Lua `getDepotChest(id, true)`).
   Auditoria dos demais criadores de Item fora de `CreateItem`: todos os outros
   incrementam corretamente (game.cpp new Container(tile) ×2, CreateItemAsContainer).
3. ~~**A6 — browseFields: mapa pendurado + refcounts desbalanceados**~~:
   RESOLVIDO (working tree, 2026-08-21): restaurados os dois blocos originais
   que o downport 7.72 havia comentado — ramo ITEM_BROWSEFIELD de `~Container`
   (erase de `g_game.browseFields` + restaurar parents dos itens do tile SEM
   decrementar) e refcount de browse field em `Player::addContainer`/
   `closeContainer`. Caminho permanece DORMENTE no 7.72 (opcode 0xCB comentado
   no dispatch, events.xml onBrowseField enabled=0), mas agora correto caso
   seja reativado. Blocos cosméticos de display continuam comentados de propósito.
4. ~~**A8 — drainCheckpointWorker congelava o Dispatcher até 30s e podia
   descartar o save do logout**~~: RESOLVIDO (working tree, 2026-08-21).
   Quando um checkpoint de floor do jogador está em voo no logout, o save agora
   é **capturado como SQL imutável no Dispatcher e enfileirado no checkpoint
   worker ATRÁS do checkpoint em voo** (`Game::enqueueDeferredPlayerSave`,
   `CheckpointJob.playerSaveOnly`): a ordem FIFO do worker garante a ordenação
   sem bloquear o Dispatcher; a legacy reservation do player_io é transferida
   para o job e mantém o relog bloqueado até o commit; falhou → retry limitado
   com o mesmo snapshot idempotente; worker morreu → replay síncrono dos
   statements (`replayDeferredPlayerSave`). Gates: só em GAME_STATE_NORMAL, sem
   clean save em andamento/janela, player_io habilitado e worker saudável —
   caso contrário mantém a barreira antiga (drain). Também corrigido o gap de
   recovery do job EM EXECUÇÃO quando o worker morria (antes só os enfileirados
   eram recuperados; grupos ficavam wedged e reservas presas).
5. ~~**A10 — race de lifecycle: Player* lido na thread de rede enquanto
   `release()` destrói no Dispatcher**~~: RESOLVIDO (working tree, 2026-08-21).
   `ProtocolGame::release()` agora adquire `connectionLock` uma vez (barreira)
   antes de tocar no player: como `Connection::close()` marca `closed=true`
   sob esse MESMO lock antes de postar o release, e todo parse checa `closed`
   sob o mesmo lock, após a barreira nenhum parse pode estar rodando nem
   iniciar — elimina use-after-free e data race no ponteiro. Invariante
   documentada em `connection.h`/`protocolgame.cpp`: código segurando
   `connectionLock` NUNCA pode bloquear esperando o Dispatcher (parse só
   enfileira tarefas), senão a barreira vira deadlock. Solução 1 da auditoria
   (capturar só o playerId na rede) fica como hardening futuro opcional.
6. ~~**A11 — fila de envio por conexão sem limite (cliente lento → RAM sem
   teto)**~~: RESOLVIDO (working tree, 2026-08-21). Caps por conexão:
   `CONNECTION_OUTPUT_QUEUE_MAX_MESSAGES = 256` e
   `CONNECTION_OUTPUT_QUEUE_MAX_BYTES = 1 MiB` (connection.h); excedeu →
   log + contador + `close(FORCE_CLOSE)` (as mensagens enfileiradas são
   liberadas pelo `onWriteOperation` quando o write em voo falha — NÃO limpar
   a fila direto no send, o async_write em voo referencia o buffer da frente).
   Métricas em `ConnectionManager`: bytes em fila agregados (com pico) e
   contador de desconexões por overflow, via getters.
   **Nota da correção (mesmo dia):** a contabilidade de bytes guarda o tamanho
   capturado NO ENQUEUE junto com a mensagem (`QueuedOutputMessage.bytes`):
   `onSendMessage` cresce o frame na criptografia antes do write completar,
   então subtrair o `getLength()` do pop causava underflow (bug real detectado
   em teste: desconexão ~4 s após login). Subtrações têm clamp em zero como
   defesa contra qualquer drift futuro.
7. **A2 parte 2 (opcional)**: reuso dos vetores de spectators já calculados no
   `Map::moveCreature` para as `postRemove/postAddNotification`.
8. **N4** — `account_clerk.lua` faz DB síncrona no diálogo de criação de personagem.
9. **N5** — `expressTravelCooldowns[cid]` nunca é limpo (npchandler.lua).
10. Demais itens médios/baixos da auditoria (M1-M23; ver memória do projeto).
    ~~**M3** — bestiary/charms com DB síncrona no Dispatcher~~: RESOLVIDO
    (working tree, 2026-08-21). `charmStates` é autoritativo em memória durante
    a sessão (único escritor `Player::setCharmState` atualiza memória+DB
    atomicamente no Dispatcher; rooking limpa os dois; login carrega uma vez).
    Removidas as 4 releituras defensivas `loadCharmStatesFromDatabase()`
    (abertura da janela de charms/bestiary, `Game::playerUnlockCharm`,
    `Player::unlockCharm`/`activateCharm`); abertura de janela agora é 100%
    em memória.
11. Preenchimento de andares no client: regra de protocolo (z<=7 envia 8 andares
   7→0; z>7 envia z±2) — investigado, NÃO mexer sem decisão (risco de compat).
   Nota: a amplificação de custo que a CAM forense causava nesse caminho deixou
   de existir na v0.2.10.
12. ~~**P1 (auditoria de protocolo de 2026-08-22) — PlayerShop `buy` com 2×
    `savePlayer` síncrono no Dispatcher (podendo cair em `drainCheckpointWorker`
    de até 30 s por compra)**~~: RESOLVIDO (working tree, 2026-08-24, teste
    in-game pendente). `buy()` agora chama
    `Game::savePlayerShopPurchase(seller, buyer, sellerContainer, movedItem)`
    (game.cpp, cluster de checkpoint): os dois participantes são registrados
    num único grupo (`registerTradeCheckpoint`) e o save é capturado como SQL
    imutável no Dispatcher e commitado pelo CheckpointWorker em UMA transação
    (`enqueueFloorCheckpointGroup`) — atomicidade buyer+seller preservada,
    ordenação FIFO com saves de logout pela machinery da A8, retry por backpressure
    já existente no tick. Fallback: grupo 0 (floor persistence inativa) ou
    enqueue recusado → saves síncronos antigos (comportamento anterior).
    Passa `movedItem` (não `offer.item`): sobrevive a merge de stack pós-move.
    Auditoria completa na memória do projeto (`protocol-security-audit.md`).
13. ~~**P2/P4 (auditoria de protocolo) — DB síncrona disparada por pacote: house
    access list (queries por linha + leak de Guild) e opcodes VIP/private sem
    cooldown**~~: RESOLVIDO (working tree, 2026-08-24, teste in-game pendente).
    P2: `AccessList::parseList` (house.cpp) ignora linhas com `@` (guild deixa
    de ser suportada — texto bruto preservado nas listas persistidas) e limita
    a 40 as resoluções de nome por parse (elimina o leak de `Guild` e corta o
    pior caso de ~300 para ≤40 queries por pacote). P4: rate limit de 1 s em
    0x9A/0xDC/0xDE (`checkClientRequestRateLimit` + campos `next*Request` em
    protocolgame.h) e descrição VIP limitada a 255 chars; cooldown de 2 s no
    `!deathlist` (Lua). **Impacto em jogo**: houses com `@guild` perdem essas
    entradas no próximo restart/re-save; listas >40 nomes truncam; ops VIP/private
    acima de 1/s descartadas. Cache name→guid DEFERIDO (risco de stale entries).

## 6. Ferramentas e comandos

**Build** (Ninja + MSVC, leva minutos se CMakeLists mudou):
```
call "D:\tibia-dev-tools\VisualStudio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && "D:\tibia-dev-tools\cmake-4.3.2\bin\cmake.exe" --build "D:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\build-cmake"
```
**Deploy**: `copy /y build-cmake\tfs.exe server\tfs.exe` + comparar SHA256 dos dois.

**Servidor**: atalho "01 - Servidor TFS (banco de teste)" →
`D:\tibia-dev-tools\Start-Tibia-Server.cmd` (MariaDB + player_io_service + tfs,
cwd = `D:\tibia-oldschool\server`).

**CodeMemory** (MCP não registrado; usar CLI):
```
c:\users\guisu\.local\bin\codebase-memory-mcp.exe cli query_graph --project tibia-oldschool-nekiro-current --query "<CYPHER>"
```
Grafo com nodes `Method/Function/Class/Field`, edges `CALLS/DEFINES/USAGE`
(props: name, file_path, start_line, line). Outros comandos: search_graph,
trace_path, index_status, detect_changes. O índice pode defasar do working tree.

**LuaJIT para testes isolados**:
`D:\tibia-oldschool\tools\vcpkg\installed\x64-windows\tools\luajit\luajit.exe`
(harness com stubs, rodar com cwd = `D:\tibia-oldschool\server`).

## 7. Pontos de atenção técnica (aprendidos na prática)

- `Player::closeShopWindow` no logout e `closeAllShopWindows` na remoção/reload
  do NPC foram reativados na v0.2.7 — não re-comentar.
- NPC scripts rodam em env privado por script (C3) mas a LIB npc.lua/npcsystem
  roda em _G; wrappers legacy vivem em `server/data/npc/lib/npc.lua`.
- `addEvent` captura função por valor no momento da chamada; para o NPC o
  contexto (npcId) é restaurado no timer via LuaTimerEventDesc.npcId.
- Blessings: índices 1-5 = bitset (`Player::addBlessing(n)` faz `set(n)`).
- Vial = item 2006; vazio = FLUID_NONE.
- Convenção de commit/tag: mensagem longa documentando o quê/porquê/resultado;
  tag `vX.Y.Z` anotada; push branch + tag.
- `git status` na raiz (worktree): cuidado com LF→CRLF warnings (normais).
