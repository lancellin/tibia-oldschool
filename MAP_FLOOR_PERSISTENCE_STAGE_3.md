# Persistência do chão — Etapa 3 coordenada

## O que esta versão faz

A Etapa 3 continua sem replay no startup, mas agora produz checkpoints consistentes entre o chão e os jogadores. O objetivo é não deixar uma bag salva simultaneamente no tile antigo e no inventário/depot depois de um crash.

Quando uma movimentação relaciona tiles e jogadores, o servidor cria um grupo temporário. Esse grupo acompanha:

- todos os tiles pelos quais os itens passaram;
- todos os jogadores que receberam ou retiraram itens;
- o `instance_id` do item móvel não stackable, quando existir.

Se outro jogador tocar a mesma bag antes do checkpoint, ele entra no mesmo grupo. Se dois grupos se encontram por um tile, jogador ou `instance_id`, eles são fundidos. O grupo usa o mesmo debounce já configurado: 15 segundos após a última modificação, com tentativa forçada no máximo em 60 segundos.

No checkpoint, todos os jogadores participantes e todos os estados finais dos tiles são gravados em uma única transação MariaDB. Ou tudo é confirmado, ou nada do grupo é confirmado. A saída de um jogador também funciona como barreira: o logout tenta concluir o grupo antes de confirmar o save desse jogador.

## Cidade e houses

- House continua integralmente fora desse mecanismo e usa o sistema existente.
- `DepotLocker`, a box `Depot` e seus subcontainers pertencem ao save do personagem. Alterações dentro deles não marcam o tile físico do depot. Um item solto colocado sobre esse mesmo tile continua sendo chão normal e é persistido pelo mecanismo incremental.
- Durante o funcionamento normal, os três tiles de teste da cidade são marcados e salvos como qualquer outro tile. Esses dados servem para recuperação de crash.
- Em um server save limpo, os tiles configurados como cidade são reserializados com o filtro de limpeza. Itens comuns da cidade não entram no snapshot limpo; corpses de jogadores continuam sendo preservados, inclusive depois de perderem `death_bundle`.
- O mapa físico não precisa ser limpo enquanto o processo ainda está aberto: no próximo startup, o OTBM já nasce limpo. A linha persistida da cidade contém apenas as exceções que realmente devem sobreviver.

## Server save diário

O evento continua avisando às 09:55 e entra na fase de save às 10:00.

1. Ativa a barreira de login.
2. Desconecta todos os jogadores.
3. Cada logout salva o jogador e o grupo de chão relacionado, quando houver.
4. Salva account storage e houses.
5. Faz um checkpoint síncrono de todos os tiles ainda dirty.
6. Regrava os tiles de cidade com o filtro de limpeza.
7. Só registra `CLEAN_COMMITTED` se todos os passos tiverem sucesso. A regravação filtrada da cidade e esse estado são confirmados na mesma transação MariaDB; um crash ou falha não pode deixar `CLEAN_PREPARING` com os snapshots de cidade já limpos.
8. Mantém o processo fechado e agenda o shutdown real para 10:10.
9. O retorno às 10:30 continua manual.

Se qualquer save de jogador, chão, account storage ou house falhar, o estado fica `CLEAN_FAILED` e o shutdown automático das 10:10 é cancelado. Nesse caso, jogadores comuns continuam bloqueados, mas a execução não é apresentada como um server save confirmado.

## Banco de dados

O banco ativo está em `db_version = 32`.

- `floor_persistence_snapshots`: estado serializado de cada tile, agora com grupo, sessão e indicador de filtro da cidade.
- `floor_persistence_checkpoints`: comprovante de cada transação conjunta confirmada, com quantidade de tiles e jogadores.
- `floor_persistence_save_sessions`: estado da execução do servidor (`RUNNING`, `CLEAN_PREPARING`, `CLEAN_COMMITTED` ou `CLEAN_FAILED`).

Um UPSERT assíncrono antigo não pode substituir uma `tile_version` mais nova já confirmada por um checkpoint conjunto.

## Comandos de GOD

### `/floorsnapshot status`

Além dos contadores anteriores, mostra grupos ativos, grupos criados/fundidos, checkpoints confirmados/falhos, jogadores e tiles gravados em conjunto, e a sessão atual.

### `/floorsnapshot front`, `here` ou `x,y,z`

Valida checksum, blob e comparação com o tile vivo, sem aplicar replay.

### `/floorsnapshot flush`

Força um lote. Grupos coordenados são confirmados de forma síncrona; tiles isolados continuam usando a escrita assíncrona da Etapa 3.

### `/floorsnapshot failnext [quantidade]`

Simula falha nas próximas gravações para testar retry e cancelamento seguro.

### `/floorsnapshot cleansave`

Executa manualmente a fase limpa das 10:00. O comando desconecta inclusive o GOD que o executou e bloqueia todos os logins até o processo ser reiniciado. Use somente no servidor de teste quando estiver pronto para encerrá-lo/reiniciá-lo.

- `/floorsnapshot cleansave 30` agenda a mesma fase para 30 segundos depois. O intervalo aceito é de 5 a 3600 segundos.
- `/floorsnapshot cleansave status` mostra o tempo aproximado restante.
- `/floorsnapshot cleansave cancel` cancela um agendamento que ainda não iniciou.

## Testes recomendados

### 1. Movimento por muitos tiles

1. O jogador A joga uma bag no tile A.
2. Arrasta a mesma bag por B, C e D, adicionando/removendo itens durante o caminho.
3. Aguarda 15 segundos ou usa `/floorsnapshot flush`.
4. Confirma em `/floorsnapshot status` que um grupo foi confirmado e que vários tiles foram salvos.
5. Inspeciona o destino e os tiles anteriores: o destino contém a bag; os anteriores têm snapshots vazios ou com os demais itens que realmente ficaram neles.

### 2. Dois jogadores na mesma bagloot

1. O jogador A movimenta a bag.
2. Antes do debounce, o jogador B retira um item, adiciona outro e move a bag novamente.
3. Usa `/floorsnapshot status` antes e depois do flush.
4. Esperado: um único grupo relacionado e pelo menos dois jogadores contabilizados no checkpoint confirmado.

### 3. Chão para inventário/depot e logout

1. Coloca a bag no chão e confirma um snapshot.
2. Recolhe a bag ou parte dela, guarda no inventário/depot e desloga antes dos 15 segundos.
3. Esperado: o logout conclui o checkpoint conjunto; o tile antigo é salvo sem o item e o estado do jogador é confirmado na mesma transação.
4. Repete tanto com `chão -> inventário -> DepotLocker` quanto com `chão -> DepotLocker` direto. Em ambos os casos somente o tile de origem entra no checkpoint; o tile físico do depot não entra.

### 4. Cidade durante crash

1. Coloca uma bag em cada um dos três tiles de cidade configurados.
2. Aguarda o debounce.
3. Esperado: agora os tiles ficam dirty e geram snapshots normais; cidade não é mais ignorada durante o dia.

### 5. Server save limpo

1. Deixa item comum e corpse de jogador na área de cidade.
2. Executa `/floorsnapshot cleansave` em ambiente de teste.
3. Esperado no log: `clean save committed`, todos desconectados e login bloqueado.
4. No banco, a sessão fica `CLEAN_COMMITTED`; snapshots da cidade ficam com `city_cleanup_filtered = 1`.
5. O corpse permanece no blob filtrado; o item comum da cidade não.

### 6. Falha antes do clean save

1. Executa `/floorsnapshot failnext 1`.
2. Em seguida executa `/floorsnapshot cleansave`.
3. Esperado: `CLEAN_FAILED`; o processo não agenda/confirma o shutdown automático como se o save tivesse sido concluído.

## Limites preservados

- Serializer iterativo, sem recursão C++.
- Máximo de 100.000 itens, 4.096 níveis e 8 MiB por tile.
- Houses excluídas.
- OTBM não movimentado e itens imóveis excluídos.
- Food permitida; demais stackables continuam `PERSIST_CLEAN_ONLY`.
- Replay/restauração automática permanece desativado nesta etapa.
