# Métricas do Dispatcher: login e logout

## Inicialização

Com o TFS fechado, execute:

```powershell
.\tools\headless-load\Start-TfsDispatcherMetrics.cmd
```

O launcher cria um CSV em:

```text
performance-results\dispatcher\<data>-dispatcher.csv
```

As métricas são agregadas em janelas de cinco segundos. Não existe log por
item, jogador ou tarefa individual. A escrita do CSV ocorre em uma thread
separada.

## Indicadores gerais

- `task_max_us`: maior tarefa individual executada pelo Dispatcher.
- `queue_wait_max_us`: maior tempo que uma tarefa esperou antes de executar.
- `queue_depth_max`: maior quantidade de tarefas aguardando.
- `batch_max_us`: maior duração de um lote completo do Dispatcher.
- `dispatcher_busy_pct`: percentual da janela consumido executando tarefas.
- `login_*`: duração total dos `ProtocolGame::login` executados na janela.

Cada fase de login e logout possui:

- `_count`: quantidade de execuções;
- `_total_us`: tempo acumulado;
- `_avg_us`: média;
- `_p95_us` e `_p99_us`: percentis aproximados por histograma;
- `_max_us`: maior execução.

## Fases do login

- `login_preload`: consulta inicial usada para identificar o personagem.
- `login_policy`: bloqueios, bans, fila e demais validações.
- `login_full_load`: carregamento completo, incluindo todas as subfases abaixo.
- `login_player_row_query`: consulta da linha principal do personagem.
- `login_load_core`: conta, atributos básicos, condições e skills.
- `login_load_social`: guild e magias aprendidas.
- `login_load_inventory`: inventário e árvore de containers.
- `login_inventory_query`: `SELECT` e recebimento das linhas de inventário.
- `login_inventory_decode`: criação dos itens e desserialização dos atributos.
- `login_inventory_attach`: montagem de slots, backpacks e subcontainers.
- `login_load_locker`: lockers.
- `login_load_depot`: depot chests.
- `login_load_storage`: storages.
- `login_load_charms`: Bestiary/charms.
- `login_load_vip`: lista VIP.
- `login_load_finalize`: peso, velocidade e luz dos itens.
- `login_place_creature`: inserção no mapa e notificações iniciais.
- `login_post_place`: eventos do client, CAM, charms e estado final do protocolo.
- `login_vip_notify`: busca de jogadores que devem receber mudança de VIP.

`login_full_load` contém as fases `login_load_*`; não some o total com as
subfases, pois isso contaria o mesmo tempo duas vezes.

## Fases do logout

- `logout_accepted_total`: logout aceito pelo protocolo até o fim da remoção.
- `logout_remove_creature_total`: remoção completa do jogador do mundo.
- `logout_map_remove_notify`: espectadores, remoção do tile e pacotes visuais.
- `logout_callbacks`: callbacks de remoção; contém cleanup e save.
- `logout_final_detach`: retirada das listas, referências e summons.
- `logout_cleanup`: trade, party, chat e guild.
- `logout_online_status`: atualização de `players_online`.
- `logout_async_snapshot_build`: construção completa do snapshot imutável no Dispatcher.
- `logout_async_statements_finalize`: finalização/cópia do lote de instruções SQL coletadas.
- `logout_async_prepare_handoff`: espera pelo canal e confirmação durável do PREPARE no serviço. Esta fase agora roda no worker e não compõe `dispatcher_busy_pct`.
- `logout_save_total`: salvamento completo do personagem.
- `logout_save_checkpoint`: caminho de checkpoint coordenado, quando aplicável.
- `logout_save_transaction_begin`: início da transação comum.
- `logout_save_core`: linha principal do personagem e condições.
- `logout_save_spells`: magias aprendidas.
- `logout_save_inventory`: inventário e containers.
- `logout_inventory_prepare`: estado dos containers e lista de raízes.
- `logout_inventory_delete`: `DELETE` das linhas antigas.
- `logout_inventory_serialize`: serialização dos atributos dos itens.
- `logout_inventory_build_rows`: escape dos blobs e montagem das linhas SQL.
- `logout_inventory_insert`: execução do `INSERT`.
- `logout_save_depot`: locker e depot, quando sujos.
- `logout_save_storage`: storages.
- `logout_save_commit`: commit da transação comum.
- `logout_vip_notify`: busca de jogadores que devem receber mudança de VIP.

Os totais de logout também se sobrepõem às subfases. Use as subfases para
explicar o total, não para somar todos os campos indiscriminadamente.

## Fases da persistência de itens/floor (baseline)

- `item_move_total`: execução completa de `Game::internalMoveItem`.
- `item_move_persistence`: soma dos trechos de persistência dentro do
  `internalMoveItem` (stamps de actor, identificação de containers,
  atribuição e registro de checkpoint group). Está contida em
  `item_move_total`; não some as duas.
- `item_move_stamp`: `stampFloorPersistenceActorAfterPlayerMutation`
  (qualquer caller).
- `item_move_identify`: `identifyFloorPersistenceMovableContainerAfterPlayerMutation`
  — inclui a re-identificação recursiva da subtree do container móvel
  (`markAsPlayerMovedForFloorPersistence`).
- `item_move_attr_endpoint`: `attributeSuccessfulItemEndpoint` — inclui
  varreduras `isHoldingItem` e enfileiramento de atribuição de actor.
- `item_move_attr_path`: `attributeContainerPathAfterMutation` — idem.
- `item_move_checkpoint_reg`: `registerFloorCheckpointTransfer` (resolução de
  endpoints e merge de checkpoint groups).
  As cinco subfases medem as funções onde quer que sejam chamadas (move,
  mail, trade); em janelas sem mail/trade a soma aproxima
  `item_move_persistence`.
- `floor_snapshot_tick`: execução de `Game::processFloorSnapshots` (tick de
  1 s e flushes forçados). Contém `floor_checkpoint_group` e
  `floor_snapshot_prepare`.
- `floor_snapshot_prepare`: preparação de um snapshot no Dispatcher
  (serialização do tile, escape do blob e montagem do UPSERT). Medida nos
  dois caminhos: `queueFloorSnapshot` (tile isolado, execução async via
  `g_databaseTasks`) e `prepareFloorSnapshot` (checkpoint group, execução
  síncrona na transação).
- `floor_checkpoint_group`: execução completa de
  `Game::executeFloorCheckpointGroup`, incluindo a transação síncrona no
  Dispatcher. Contém `floor_snapshot_prepare` e todas as fases
  `floor_checkpoint_*` abaixo.
- `floor_checkpoint_player_save`: cada `IOLoginData::savePlayerData`
  executado dentro da transação de checkpoint. Em contexto de logout também
  aparece dentro das fases `logout_save_*`. Quando chamado por checkpoint,
  as subfases `logout_save_core`, `logout_save_spells`,
  `logout_save_inventory`, `logout_inventory_*`, `logout_save_depot` e
  `logout_save_storage` também são gravadas (contexto de medição generalizado
  para logout OU checkpoint; cada save é medido uma única vez, sem dupla
  contagem). Janelas com saves de logout e de checkpoint misturam amostras
  nas mesmas fases.
- `floor_checkpoint_tx_begin`: begin da transação do checkpoint.
- `floor_checkpoint_house_save`: cada `IOMapSerialize::saveHouseData`
  executado dentro da transação de checkpoint.
- `floor_checkpoint_tile_sql`: execução dos UPSERTs dos snapshots dos tiles
  (e o DELETE do reset semanal de floor, quando esse caminho raro ocorre).
- `floor_checkpoint_marker_sql`: INSERT em `floor_persistence_checkpoints`.
- `floor_checkpoint_clean_save_sql`: UPDATE de CLEAN_COMMITTED da sessão de
  clean save, quando esse caminho existe.
- `floor_checkpoint_tx_commit`: commit da transação do checkpoint.
- `floor_checkpoint_db_lock_wait`: tempo gasto aguardando o lock da conexão
  única de banco (`Database::databaseLock`) durante o caminho de checkpoint
  (preparação + transação). Está contido nas demais fases; distingue espera
  por contenção (ex.: thread assíncrona de snapshots) de tempo de execução
  no MariaDB.
- `item_actor_attribution`: processamento debounced das atribuições de actor
  pendentes (normalização de subtree + certificação de ancestrais). Medido
  apenas quando há atribuições pendentes.

Contadores agregados adicionais por janela:

- `floor_dirty_marks`: eventos de dirty-marking de tiles aceitos.
- `floor_dirty_tiles_max`: maior quantidade de tiles dirty pendentes.
- `floor_checkpoint_groups_saved`: checkpoint groups concluídos.
- `floor_checkpoint_max_tiles`: maior número de tiles salvos em um único
  checkpoint group na janela.
- `floor_checkpoint_max_players`: maior número de players salvos em um único
  checkpoint group na janela.
- `floor_checkpoint_tile_queries`: UPSERTs de tiles executados dentro de
  transações de checkpoint.
- `actor_attributions_pending_max`: maior fila de atribuições de actor
  pendentes.
- `actor_attributions_resolved`: atribuições de actor resolvidas.
- `checkpoint_group_failures_total`: falhas de checkpoint group na janela
  (qualquer causa). Um valor persistente > 0 indica grupo preso/degradado.
- `checkpoint_group_failures_participant_unavailable`: falhas porque um
  participante do grupo não está mais em memória (causa típica de grupo
  preso: saves mesclados ficam retidos até todos os participantes salvarem
  juntos ou o servidor reiniciar).
- `checkpoint_group_failures_house_unavailable`: falhas por house do grupo
  indisponível em memória.
- `checkpoint_group_failures_capture_failed`: falhas ao capturar o SQL de
  save de player/house no Dispatcher.
- `checkpoint_group_failures_serialization`: falhas de serialização de tile
  (prepareFloorSnapshot).
- `checkpoint_group_failures_transaction`: falhas na transação do checkpoint
  síncrono (inclui falha simulada via `/floorsnapshot failnext`).
- `checkpoint_group_failures_worker`: falhas de execução no checkpoint
  worker.
- `checkpoint_group_failures_worker_aborted`: jobs descartados porque a
  thread do worker morreu antes de executá-los.
- `checkpoint_stuck_groups_max`: maior número de checkpoint groups com 3+
  falhas consecutivas (threshold de "stuck") em qualquer tick da janela.

## Instrumentação removida

As medições internas de movimento e espectadores foram removidas depois de
confirmado que não eram gargalo. `Map::moveCreature` voltou a executar sem
cronômetros ou registros adicionais.

## Teste controlado com 100 personagens pesados

Abra o TFS com o launcher de burst apenas no ambiente local:

```powershell
.\tools\headless-load\Start-TfsDispatcherBurstTest.cmd
```

Em outro terminal, execute:

```powershell
D:\tibia-dev-tools\Python312\python.exe `
  .\tools\headless-load\headless_load.py `
  --host 127.0.0.1 `
  --port 7172 `
  --count 100 `
  --account-start 100001 `
  --start-index 1 `
  --profile idle `
  --duration 90 `
  --batch-size 100 `
  --batch-delay 0 `
  --login-concurrency 100 `
  --login-admission-delay 0 `
  --login-timeout 120 `
  --keepalive-interval 4 `
  --stop-batch-size 100 `
  --stop-batch-delay 0 `
  --output-dir .\performance-results\headless-load
```

Esse cenário mede um burst de 100 logins e, após 90 segundos, um pedido de
logout para os 100 personagens. Aguarde todos saírem e encerre o TFS de forma
controlada para garantir o flush final do CSV.

O launcher de burst define `TFS_LOAD_TEST_BYPASS_CONNECTION_THROTTLE=1`.
Nunca o use em produção ou exponha esse processo ao público.
