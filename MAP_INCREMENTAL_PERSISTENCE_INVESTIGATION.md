# Investigação de persistência incremental das alterações do mapa

**Projeto analisado:** TFS 1.5 downgrade 7.72 deste workspace  
**Data da investigação:** 18 de julho de 2026  
**Escopo:** investigação e desenho técnico; nenhuma implementação, alteração de banco ou mudança de configuração foi realizada.

## 1. Resumo executivo

O servidor não possui hoje persistência geral do estado mutável do mapa. Em cada inicialização, o mapa base é carregado novamente do arquivo `.otbm`; depois disso, o servidor aplica a persistência específica de casas. O método chamado `Map::save()` não salva o mapa geral nem reescreve o `.otbm`: ele salva somente informações e itens de casas (`sources/nekiro-tfs-1.5-7.72/src/map.cpp:31-75`).

As mutações normais de itens passam principalmente por `Game::internalAddItem`, `Game::internalRemoveItem`, `Game::internalMoveItem` e `Game::transformItem`. Porém, interceptar cegamente esses métodos seria incorreto. Eles também processam drops, loot, sangue, cadáveres, fields de combate, decay, portas, traps, paredes temporárias, conteúdo de casas e operações de inventário. O projeto não carrega no próprio `Tile` uma distinção entre “estado permanente”, “estado temporário” e “estado base”.

A recomendação é um sistema **opt-in, exclusivo para alterações intencionalmente permanentes e, inicialmente, proibido em `HouseTile`**. A melhor arquitetura para este checkout é um sistema híbrido em MariaDB:

1. uma API exclusiva de mutação permanente;
2. gravação transacional imediata de uma projeção persistente do estado final dos tiles afetados;
3. journal com sequência e identificador de transação;
4. checkpoints/compactação periódicos;
5. replay idempotente depois do carregamento do OTBM e das casas, mas antes de spawns e scripts de startup;
6. validação contra a versão/fingerprint do mapa base e contra a definição atual de itens.

O journal recomendado não deve ser uma repetição literal de `add/remove/move/transform` baseada em ponteiros ou stack positions. Deve registrar o **estado persistente desejado após a transação**, por tile, sem capturar a camada temporária presente naquele instante. Isso torna o replay idempotente e reduz os riscos causados por empilhamento, autostack, decay, timers perdidos e várias transformações sucessivas.

Uma operação permanente só deve ser confirmada ao chamador depois que sua transação durável for confirmada. Shutdown normal continua útil para saves existentes e compactação oportunista, mas não é o mecanismo de durabilidade do novo sistema.

Conclusões de segurança:

- não persistir automaticamente toda mudança observada em um tile;
- não incorporar houses ao novo domínio de persistência;
- não salvar snapshots completos do tile vivo sem separar itens temporários;
- não permitir, na primeira versão, decay de objetos persistentes nem transferência de objetos persistentes para inventário de jogador;
- não reaplicar estado incremental antigo cegamente sobre um OTBM atualizado;
- não depender de logout, hourly save ou shutdown limpo para confirmar uma mudança permanente.

## 2. Funcionamento atual do mapa

### 2.1 Carregamento do OTBM

O fluxo principal é:

1. `Map::loadMap` chama `IOMap::loadMap` para carregar o OTBM (`src/map.cpp:31-38`).
2. O servidor carrega os arquivos externos de spawns e houses (`src/map.cpp:39-47`; `src/iomap.cpp:171-217`).
3. Se o carregamento é o principal, `IOMapSerialize::loadHouseInfo` e `loadHouseItems` sobrepõem o estado salvo das casas (`src/map.cpp:45-49`).
4. Só depois, durante a inicialização geral, o estado do jogo avança para `GAME_STATE_INIT`, carrega spawns e executa eventos de startup (`src/otserv.cpp:289-337`).

`IOMap::loadMap` valida identificador, versão OTBM e versão do mapa de itens antes de percorrer a árvore binária (`src/iomap.cpp:71-169`). Durante o parsing de tiles:

- coordenadas e atributos de tile são lidos;
- tiles com house ID viram `HouseTile` (`src/iomap.cpp:264-279`);
- flags OTBM são convertidas em flags de tile (`src/iomap.cpp:282-305`);
- itens e seus atributos são desserializados (`src/iomap.cpp:307-390`);
- itens móveis existentes no OTBM de uma house são descartados para não duplicar o conteúdo salvo separadamente (`src/iomap.cpp:314-317` e `369-371`);
- itens carregados do mapa recebem `loadedFromMap = true` (`src/iomap.cpp:325`, `333`, `380` e `388`);
- containers aninhados são lidos recursivamente;
- o tile é finalmente instalado em `Map::setTile` (`src/iomap.cpp:399`).

`IOMap::createTile` escolhe `StaticTile` se o tile não tem ground ou se o ground bloqueia movimento; caso contrário, cria `DynamicTile` (`src/iomap.cpp:52-68`). Essa classificação é uma otimização de alocação, não uma política de imutabilidade ou persistência.

O arquivo base observado durante a investigação foi `server/data/world/world.otbm`, com 55.726.741 bytes e SHA-256 `60E78C4A7D10A091093BE865838233F1E429D52885696783D44A775F03D93E63`. Esse hash é apenas a fingerprint do checkout atual; uma implementação futura deve calculá-la no startup e não gravá-la como constante.

### 2.2 Estrutura em memória

`Map` mantém tiles numa quadtree; `Map::getTile` percorre os nós e o andar correspondente (`src/map.cpp:78-94`; API em `src/map.h:173-217`).

`Tile` é um `Cylinder` e contém:

- `ground`;
- posição;
- flags derivadas;
- altura;
- vetor de criaturas;
- vetor de itens dividido em top items e down items (`src/tile.h:78-147` e `149-302`).

`DynamicTile` aloca seus vetores diretamente, enquanto `StaticTile` os aloca sob demanda (`src/tile.h:304-391`). Ambos aceitam alterações em runtime. Os nomes não significam “persistente” e “temporário”, nem “imutável” e “mutável”.

O ordenamento observado pelo cliente e pelas APIs de stack segue ground, top items, criaturas e down items (`src/tile.cpp:1131-1317`). `TileItemVector::downItemCount` separa categorias dentro do vetor (`src/tile.h:78-147`). Isso torna stack position um identificador instável: adicionar uma criatura, um splash, um field ou um item de outra categoria pode deslocar a posição de outros objetos.

### 2.3 Criação, mesclagem e remoção de tiles

`Map::setTile` cria as folhas da quadtree e o floor se necessário. Quando a posição já contém um tile, ele não substitui atomicamente o tile antigo: transfere os itens/ground do tile recebido usando `tile->addThing` e apaga o objeto temporário (`src/map.cpp:96-155`). Consequências:

- carregamento de uma região sobreposta é uma **mesclagem**, não uma substituição;
- a mesclagem pode disparar notifications e movement events;
- um interceptor genérico poderia registrar como “mudança permanente” o próprio carregamento da região ou o replay.

`Map::removeTile` remove ou teleporta criaturas e usa `Game::internalRemoveItem` sobre itens e ground (`src/map.cpp:157-197`). Ele limpa o conteúdo, mas não elimina a célula da quadtree. Assim, “remover tile” no modelo atual é, na prática, esvaziar seu conteúdo.

Lua expõe `Game.createTile` e `Tile:remove`; a implementação cria `StaticTile` ou `DynamicTile`, chama `Map::setTile`, ou chama `Map::removeTile` (`src/luascript.cpp:4681-4710` e `5052-5063`). A busca nos dados ativos não encontrou uso de `Game.createTile`.

### 2.4 Regiões carregadas em runtime

Existe `Game::loadMap(path)`, que chama o carregador com `loadHouses = false` e mescla o conteúdo no mapa corrente (`src/game.cpp:261-269`). Lua também expõe `Game.loadMap(path)` por uma tarefa no dispatcher (`src/luascript.cpp:4324-4337`). Não foi encontrado uso dessa API nos scripts ativos deste servidor, nem um ciclo correspondente de unload de regiões.

Portanto, o checkout tem uma capacidade de merge de mapa em runtime, mas não um sistema ativo de streaming de regiões. Se essa API passar a ser utilizada, o novo sistema terá de definir escopo/fingerprint por região e manter uma guarda que impeça o carregamento de produzir entradas no journal.

## 3. Fluxos e métodos encontrados

### 3.1 Adição de item

`Game::internalAddItem` (`src/game.cpp:1355-1443`) realiza consulta de destino, lida com empilhamento, adiciona o item, envia notifications e inicia decay. Uma pilha pode ser fundida a outra e deixar remainder, portanto “o item C++ passado à chamada” não é necessariamente a identidade final persistível.

No nível de tile, `Tile::addThing` (`src/tile.cpp:822-937`):

- substitui ground anterior;
- ordena top items;
- substitui splashes;
- substitui magic fields incompatíveis/substituíveis;
- insere down items no início da faixa correspondente;
- atualiza flags e parent;
- depois dispara notifications, movement scripts e mecanismos especiais (`src/tile.cpp:1320-1397`).

`Tile::internalAddThing` faz a montagem estrutural sem notifications/movement events e é usado por loaders (`src/tile.cpp:1400-1457`). Um futuro replay deve usar uma rota controlada equivalente, não a API pública normal.

Lua pode criar/adicionar itens via `Game.createItem`, `Game.createContainer`, `Tile:addItem` e `Tile:addItemEx` (`src/luascript.cpp:4545-4629` e `5628-5696`). Se não houver posição, o item criado fica temporariamente em `VirtualCylinder`; com posição, ele entra por `internalAddItem`.

### 3.2 Remoção de item

`Game::internalRemoveItem` (`src/game.cpp:1445-1492`) consulta se a remoção é permitida, suporta remoção parcial de stack, chama `onRemoved`, desvincula decay, libera referência e envia notifications.

`Tile::removeThing` (`src/tile.cpp:1032-1123`) trata ground, itens completos e redução de count. A posição da pilha é recalculada para notificação; não é um ID durável.

Lua expõe `Item:remove` (`src/luascript.cpp:6415-6425`). O talkaction de GM `server/data/talkactions/scripts/removething.lua:1-32` remove o top thing, mas hoje não carrega intenção de persistência.

### 3.3 Movimento de item

`Game::internalMoveItem` (`src/game.cpp:1152-1353`) inclui:

- hooks pré e pós-movimento do jogador;
- resolução do destination cylinder;
- troca com o item de destino;
- remoção parcial de stacks;
- clone/remainder e autostack;
- notifications de remoção e adição;
- início de decay.

Um movimento entre dois tiles permanentes é uma transação de dois tiles. Movimento entre mapa e inventário cruza dois domínios de persistência hoje independentes. Não pode ser representado com segurança por duas gravações isoladas.

`Game::internalTeleport` usa `internalMoveItem` para itens (`src/game.cpp:1811-1835`). Lua expõe `Item:moveTo` (`src/luascript.cpp:6807-6865`).

### 3.4 Transformação

`Game::transformItem` (`src/game.cpp:1670-1809`) possui vários caminhos:

- no-op;
- update in-place quando o tipo não muda;
- remoção e readição quando muda a categoria de ordenação;
- remoção quando count chega a zero;
- encadeamento de decay;
- troca do objeto quando muda a classe/tipo.

Logo, interceptar apenas `Tile::updateThing` não cobre todas as transformações, e registrar ponteiro ou stack position não produz uma identidade estável. Lua expõe `Item:transform` (`src/luascript.cpp:6868-6919`).

### 3.5 Atributos modificados diretamente

Lua expõe leitura/alteração/remoção de atributos, e várias mudanças são feitas diretamente no `Item` sem passar por `transformItem` (`src/luascript.cpp:6659-6804`). Um sistema que só observe add/remove/move/transform perderia texto, writer, custom attributes, action IDs permitidos, charges e outros estados.

Isso favorece uma API explícita que, ao confirmar a transação, captura a projeção persistente final, em vez de tentar reconstruir intenção por todos os setters de baixo nível.

### 3.6 Decay

`Game::startDecay` insere itens nos buckets de decay; o scheduler reduz duração e transforma ou remove itens expirados (`src/game.cpp:4927-5003` e `5062-5084`). `Item::startDecaying` delega ao `Game` (`src/item.cpp:1736-1739`), e Lua expõe `Item:decay` (`src/luascript.cpp:6922-6936`).

Transformações podem redefinir a duração conforme o novo tipo (`src/item.cpp:265-283`). Clones copiam atributos e podem voltar ao scheduler (`src/item.cpp:182-201`). Persistir apenas o número de milissegundos restantes faria o timer reiniciar por inteiro após cada downtime. Persistência correta de decay exigiria um `expires_at` absoluto e uma política explícita para aplicar a cadeia vencida durante o startup.

Os itens chamados `*_PERSISTENT` em fields não significam persistência em banco/journal. O carregador converte IDs de magic walls, wild growth e fields de mapa para variantes apropriadas (`src/item.cpp:105-155`), enquanto o combate converte/cria variantes de runtime e inicia decay (`src/combat.cpp:777-852`). São semânticas de field/mapa, não a solução discutida aqui.

### 3.7 Combate, cadáveres e efeitos de chão

Sangue, splash e cadáveres são criados no tile e entram em decay (`src/creature.cpp:733-787`). Fields de combate também são criados e decaem; magic walls e wild growth podem ser removidos ao pisar (`src/combat.cpp:777-852` e `1704-1720`).

Esses fluxos passam pelas mesmas primitivas de tile usadas por alterações administrativas. Persistir automaticamente essas chamadas faria lixo de combate sobreviver ao restart e produziria escrita intensa no game loop.

### 3.8 Lua, alavancas, portas, paredes, pedras e quests

O inventário por busca textual nos dados do servidor encontrou, aproximadamente:

| Padrão | Ocorrências | Arquivos |
|---|---:|---:|
| `Game.createItem` | 69 | 41 |
| `:addItem` / `:addItemEx` | 70 | 26 |
| `:remove` | 92 | 59 |
| `:moveTo` | 9 | 6 |
| `:transform` | 140 | 64 |
| `:decay` | 28 | 16 |
| `addEvent` | 51 | 13 |

Esses números são um inventário bruto: incluem bibliotecas, backups e operações de inventário, não somente tiles. Ainda assim, demonstram a amplitude dos fluxos. Exemplos concretos:

- `server/data/actions/scripts/quests/banshee_magic_wall_levers.lua:1-75`: remove walls e agenda restauração após 120/180 segundos;
- `server/data/actions/scripts/rook/lever_wall_1.lua:1-32`: alterna uma parede de sessão;
- `server/data/actions/scripts/quests/behemoth_stones_lever.lua:1-54`: remove e recria pedras;
- `server/data/actions/scripts/quests/banshee_entrance_lever.lua:1-31`: alterna wall/floor/stair;
- `server/data/scripts/actions/others/doors.lua:56-129`: transforma estados de portas;
- `server/data/movements/scripts/trap.lua:1-51`: transforma traps ao entrar/sair;
- `server/data/actions/scripts/tools/shovel.lua:35-86`: transforma holes, inicia decay e usa timer;
- `server/data/actions/scripts/tools/machete.lua:1-7`: transforma e inicia decay;
- `server/data/movements/scripts/closingdoor.lua:1-30`: remove itens móveis ao fechar e transforma a porta.

`addEvent` é apenas scheduler em memória (`src/luascript.cpp:3701-3816`). Os timers não são persistidos. Se houver crash entre a mudança temporária e o callback, o callback se perde; hoje o OTBM restaura a base no próximo startup, o que costuma ser precisamente o resultado seguro.

### 3.9 Limpeza automática

Um item é `cleanable` se não veio do mapa, é removível/pickupable e não tem unique ID nem action ID (`src/item.h:1029-1037`). Quando um item cleanable é adicionado fora de house, o tile entra em `Game::tilesToClean` (`src/tile.cpp:335-369`; conjunto em `src/game.h:558-568` e `613`).

`Map::clean` percorre esses tiles e remove os itens cleanable (`src/map.cpp:1041-1081`). A limpeza pode ser chamada por `cleanMap` Lua (`src/luascript.cpp:3852-3855`), pelo talkaction `server/data/talkactions/scripts/clean.lua:1-14` ou pelo server save. No checkout atual, `cleanProtectionZones = false` e `serverSaveCleanMap = false` (`server/config.lua:96` e `113-119`).

Registrar a limpeza no journal seria um erro: drops já ignorados não precisam de tombstones persistentes, e uma varredura poderia apagar indevidamente uma projeção persistente mal classificada.

## 4. Sistemas de persistência já existentes

### 4.1 Casas

`Map::save()` chama somente `IOMapSerialize::saveHouseInfo()` e `saveHouseItems()`, com até três tentativas (`src/map.cpp:54-75`). Não há serialização do mapa geral.

O estado de itens de casas é armazenado em `tile_store`. A tabela atual contém `house_id` e um `longblob`, com foreign key para `houses`, sem primary key ou colunas de coordenadas (`server/schema.sql:355-359`).

No carregamento, cada blob contém posição e quantidade de itens e só é aplicado se já existir um tile naquela coordenada (`src/iomapserialize.cpp:30-67`). No salvamento geral:

- inicia transação;
- apaga todo `tile_store`;
- reinsere os tiles das houses;
- confirma a transação (`src/iomapserialize.cpp:69-114`).

O salvamento de uma única house também substitui todas as linhas daquela house numa transação (`src/iomapserialize.cpp:364-402`).

`IOMapSerialize::saveTile` não salva o ground. Salva itens moveable/force-serialize, doors, containers não vazios, writables e beds, em ordem reversa para reconstrução correta (`src/iomapserialize.cpp:217-269`). A leitura:

- cria itens móveis;
- encontra objetos fixos do OTBM, como door/bed, e aplica seus atributos/transformação;
- reconstrói containers recursivamente;
- consome dados com um dummy se o OTBM mudou e o objeto fixo deixou de existir (`src/iomapserialize.cpp:116-214`).

`HouseTile` deriva de `DynamicTile` e registra doors/beds ao adicionar itens (`src/housetile.cpp:30-75`). A house também controla doors e beds (`src/house.cpp:287-307`). Beds serializam sleeper/start time de forma especializada (`src/bed.cpp:34-82`) e mudam aparência ao dormir/acordar (`src/bed.cpp:140-259`). Door access é tratado pela house (`src/house.cpp:524-590`).

Conclusão: house persistence é um domínio completo e já possui regras específicas. O sistema novo deve rejeitar `HouseTile` por padrão, antes de qualquer mutação persistente, para evitar duas autoridades restaurando o mesmo tile.

### 4.2 Camada Lua de “dirty houses”

Existe uma camada customizada em `server/data/lib/core/persistence.lua:1-46`, que mantém IDs de houses dirty. Movimentos de itens feitos por jogadores identificam a house de origem/destino e marcam-na (`server/data/events/scripts/player.lua:1-69` e `110-135`). O save dirty ocorre no logout (`server/data/creaturescripts/scripts/logout.lua:1-8`), além do hourly save e saves globais (`server/data/globalevents/scripts/hourly_save.lua:1-6`; registro em `server/data/globalevents/globalevents.xml:3-6`).

Limitações relevantes:

- não é crash-safe entre movimento e logout/hourly save;
- somente movimentos observados do jogador marcam dirty; transform, decay e mutações de script não passam necessariamente por essa marcação;
- `saveDirtyHouses` limpa a marca mesmo se `house:save()` retornar falha;
- `house:save()` apenas chama o salvador de house (`src/luascript.cpp:11964-11974`, registro em `2735`);
- o hourly/full save reduz a janela, mas não fornece confirmação durável próxima da operação.

Essa camada não é um journal reutilizável para mapa geral. Ela apenas reforça a necessidade de manter houses fora do novo sistema.

### 4.3 Jogadores e containers

O save de jogador usa banco e transação. Inventário e depots são regravados com `pid`, `sid`, item type, count e blob de atributos; containers são reconstruídos pela relação de parent IDs (`src/iologindata.cpp:420-555`, `584-638` e `641-902`). No logout há três tentativas de `savePlayer` (`src/player.cpp:1651-1663`).

Essa infraestrutura demonstra que o projeto já lida com árvores de containers e transações, mas é outro domínio. Um item permanente movido do mapa para inventário não fica atomicamente coordenado com o save do jogador. Na primeira versão, objetos persistentes do mundo devem ser não-pickupable/não-movíveis por jogadores.

### 4.4 Serialização de itens reaproveitável

`Item::readAttr` e `Item::serializeAttr` cobrem count/charges, action, texto, writer, data, descrição, duração, decay state e custom attributes (`src/item.cpp:384-706` e `728-867`). `Container` trata filhos recursivamente (`src/container.cpp:105-147`). `Teleport` serializa destino (`src/teleport.cpp:27-46`).

Entretanto, a serialização atual não deve ser reutilizada sem um envelope e sem auditoria de subclasses:

- unique ID não é emitido por `Item::serializeAttr`; IDs duplicados são rejeitados pelo registro global (`src/item.cpp:1583-1609`);
- action ID só é emitido pelo base serializer quando o tipo é moveable (`src/item.cpp:742-748`);
- `Door::serializeAttr` é vazio (`src/house.h:53-82`);
- `Bed::serializeAttr` é especializado e não chama o serializer base (`src/bed.cpp:34-82`);
- tipos especiais têm semântica própria: depot, mailbox, trash holder, field, door, bed e teleport (`src/item.cpp:41-90`).

`PropWriteStream` é apenas um vetor de bytes em memória; não fornece versão de schema, framing, checksum, fsync, rename atômico ou recuperação de escrita parcial (`src/fileloader.h:72-165`). É um building block, não uma solução crash-safe.

### 4.5 Banco, transações e fila assíncrona

O projeto já possui `Database`, transações com rollback/commit e `DBTransaction` com rollback no destrutor (`src/database.cpp:67-125`; `src/database.h:203-240`). Também há worker assíncrono de banco e callbacks devolvidos ao dispatcher (`src/databasetasks.cpp:53-95`), além de migrations (`src/databasemanager.cpp:80-125`).

Essa base favorece MariaDB como armazenamento. A durabilidade real deverá ser validada na implementação, inclusive `innodb_flush_log_at_trx_commit = 1`, flush do dispositivo e backup. Essas configurações não foram alteradas nem confirmadas nesta investigação.

### 4.6 Save geral e shutdown

`Game::saveGameState` salva account storage, jogadores online, houses por `Map::save` e aguarda tarefas de banco (`src/game.cpp:235-258`). No shutdown, o jogo executa global event, expulsa jogadores, salva MOTD/estado e então para os serviços (`src/game.cpp:158-233` e `5041-5059`). Signals pedem save/shutdown pelo dispatcher; no Windows, o sistema operacional ainda pode encerrar o processo após uma janela limitada (`src/signals.cpp:64-82` e `146-220`). Scheduler e DB dispatcher também são encerrados (`src/scheduler.cpp:70-80`).

Esse caminho atende shutdown limpo, mas não protege contra kill, crash, falta de energia ou corrupção no meio de uma escrita. O novo mecanismo deve confirmar cada alteração permanente perto de sua ocorrência e considerar shutdown apenas uma oportunidade de checkpoint/flush.

## 5. Tipos de alterações de tile

### 5.1 Persistentes por intenção explícita

| Tipo/origem | Duração esperada | Decisão sugerida | Risco se persistir | Risco se não persistir | Restauração |
|---|---|---|---|---|---|
| construção/remoção administrativa permanente | indefinida | persistir somente pela API permanente | GM marcar alvo errado; conflito com OTBM futuro | alteração confirmada desaparece no crash | projeção after-state transacional |
| decoração permanente criada por GM | indefinida | persistir com comando/API exclusivos | duplicação, IDs especiais, objeto pickupable | decoração some | checkpoint + journal |
| parede/pedra/obstáculo removido permanentemente | indefinida | persistir tombstone/projeção explícita | capturar remoção temporária semelhante | barreira volta | estado final do tile, não stack position |
| quest global permanente | até mudança global posterior | persistir em transação lógica | dessincronizar storage global e mapa | mundo regride | uma autoridade/txn para estado global e overlay |
| sistema futuro de construção | indefinida | whitelist de sistema + API permanente | abuso ou volume excessivo | perda de progresso mundial | transações multi-tile idempotentes |
| transformação declarada permanente | indefinida | persistir resultado final | capturar decay/porta temporária se API errada | tipo anterior volta | projection record com sequência |

Origem administrativa ou script não é suficiente por si só. A intenção permanente deve estar na chamada autorizada.

### 5.2 Temporárias por padrão

| Tipo/origem | Duração esperada | Decisão | Risco de persistir | Risco de ignorar | Restauração correta |
|---|---|---|---|---|---|
| magic wall/wild growth de combate ou alavanca | segundos/minutos | ignorar | wall fica presa após restart | timer recomeça do base antes do previsto | recarregar OTBM; evento persistente separado somente se prazo exato for requisito |
| fire/energy/poison field de combate | curta/decay | ignorar | hazards eternos | field desaparece no restart, comportamento seguro atual | OTBM/base ou nenhum field |
| item com decay temporário | limitada | ignorar | ressurreição/reinício de timer | desaparece/regride ao base | base; deadline explícito apenas em feature própria |
| cadáver, sangue, splash, lixo | curta | ignorar | poluição e log massivo | limpeza antecipada no restart | não restaurar |
| drop/loot no chão | até clean/recolhimento | ignorar fora de house | duplicação/economia explorável | jogador perde item no crash, igual ao modelo de mapa atual | não restaurar; houses continuam próprias |
| item movido em combate | sessão | ignorar | persiste estado acidental | volta ao base | OTBM/estado permanente anterior |
| porta aberta | segundos/sessão | ignorar por padrão | porta nasce aberta permanentemente | fecha no restart | base OTBM; house pelo sistema de house |
| lever/trap temporária | segundos/sessão | ignorar | puzzle fica travado | reset antecipado | estado base/startup script |
| objeto de quest com timer | segundos/minutos | ignorar | atalho/obstáculo incorreto | reset antecipado | estado base; timer durável separado se necessário |
| criatura/summon/effect | sessão | fora do escopo | entidades inválidas/duplicadas | desaparecem | spawn/eventos existentes |
| alteração restaurada no startup | sessão | ignorar | compete com startup e inverte estado | startup cumpre a regra | OTBM + script de startup |

### 5.3 Ambíguas e regra de desempate

| Caso | Regra segura |
|---|---|
| item movido por jogador | ignorar fora de house; house usa `tile_store`; proibir que jogador mova objeto do novo overlay persistente |
| parcel/container no chão | ignorar fora de house; não transformar mapa geral em save de chão |
| item/móvel em house | somente persistência atual de house |
| porta | base por padrão; house pela house; somente uma API global explícita torna uma porta permanente |
| cama | sistema de house; excluir do novo sistema inicialmente |
| writable item | somente opt-in fora de house e com serializer de atributos auditado |
| action ID | preservar apenas se explicitamente suportado; não duplicar anchors do OTBM |
| unique ID | não criar/copiar no overlay inicial; unique deve continuar ancorado no mapa base |
| custom attributes | permitidos apenas com schema/versionamento e limites explícitos |
| item criado/removido por script | a API chamada decide; `Game.createItem`/`remove` normais continuam temporários |
| transformação com `addEvent` de reset | temporária; se o prazo tiver de sobreviver, persistir evento/deadline, não snapshot vivo |
| reset perdido no crash | o OTBM restaura imediatamente; só reconstruir tempo restante se a regra de negócio exigir |
| quest individual | guardar progresso no player storage, não alterar permanentemente um tile global compartilhado |
| quest global | API permanente + estado global coordenado/idempotente |
| região carregada em runtime | guard de load/replay, identidade/fingerprint de região e aplicação do overlay após a base daquela região |

O critério é: **na dúvida, não persistir**. A permanência precisa ser declarada pelo sistema responsável, não inferida pelo item, por quem o moveu ou pela duração observada.

## 6. Matriz “persistir / não persistir / ambíguo”

| Classe | Exemplos | Estado padrão | Mecanismo futuro permitido |
|---|---|---|---|
| permanente intencional | construção global, decoração GM, barreira global removida | não acontece por API genérica | API permanente exclusiva e transação durável |
| base OTBM | grounds, walls, doors, teleports, UIDs | fonte base; não duplicar | referenciação/diff contra base |
| house | furniture, beds, doors, containers, writables | persistência existente | somente `IOMapSerialize`/`tile_store` |
| player | inventário/depots | persistência de player | somente save de player |
| combate | fields, blood, corpses, moved items | não persistir | nenhum |
| decay | splashes, holes, temporary transforms | não persistir | feature especial com deadline absoluto, se aprovada |
| scripts temporários | levers, doors, traps, timed walls | não persistir | evento durável separado apenas se indispensável |
| drops/loot | itens pickupable no chão | não persistir | house quando aplicável; nunca mapa geral automático |
| objetos especiais | teleport, depot, bed, mailbox | ambíguo/rejeitar | allowlist futura por subtipo e serializer auditado |
| atributos | text/custom/action/unique | ambíguo | opt-in; UID rejeitado; schema e validação obrigatórios |
| containers aninhados | decoração/baú mundial | ambíguo | opt-in, árvore recursiva validada e limitada |
| tiles criados em runtime | nova célula | ambíguo | operação administrativa explícita, incluindo ground/flags |

## 7. Riscos técnicos

### 7.1 Duplicação e múltiplas autoridades

- **House + overlay:** restaurar o mesmo item por `tile_store` e pelo novo journal duplica conteúdo ou faz uma restauração sobrescrever a outra.
- **Player + overlay:** um objeto persistente coletado pode ser salvo no player e recriado no mapa no restart.
- **OTBM + overlay:** armazenar uma cópia de um item base sem representar que é uma modificação desse anchor cria dois itens.
- **Replay recursivo:** usar APIs normais durante replay pode gerar novas entradas e crescimento/duplicação a cada startup.

Mitigações: ownership exclusivo por domínio, rejeição de `HouseTile`, objetos permanentes não-movíveis por jogador, identidade/diff relativo à base, `ReplayGuard` e replay por rota interna.

### 7.2 Estado temporário capturado

Snapshot do tile vivo pode conter fire field, corpse, loot, porta aberta, parede temporariamente removida ou remainder de stack. Blacklist por item ID não resolve todos os casos: uma pedra comum pode ser temporária numa quest e permanente numa ação administrativa. É indispensável manter uma **projeção persistente separada** da composição viva.

### 7.3 Timers, decay e resets perdidos

`addEvent` e buckets de decay vivem em memória. Persistir apenas o efeito, sem o evento que o desfaz, eterniza o estado. Persistir duração relativa reinicia o relógio após cada restart. Se uma feature realmente exigir continuidade temporal, deve salvar deadline absoluto, timezone/clock policy e a transição a executar quando vencida.

### 7.4 Ordem, stack position e stacks

Stack position muda com ground, top/down items e criaturas. Autostack pode consumir um objeto e produzir remainder. Um log literal “remove stackpos 4” é não idempotente e pode remover outro item no replay. A serialização precisa definir ordem canônica e árvores de container; a reconciliação deve usar estado/anchors estáveis, nunca ponteiros ou stackpos gravados.

### 7.5 Containers e subclasses

Containers exigem ordem dos filhos, profundidade/quantidade máxima, detecção de dados inválidos e recursão segura. Teleports, doors, depots, beds, mailboxes, trash holders e fields possuem comportamentos diferentes. Uma lista inicial de tipos aceitos é mais segura que uma blacklist incompleta.

### 7.6 Unique ID e action ID

Unique IDs são globais e não podem ser duplicados; o serializer base deliberadamente não os emite. O overlay inicial deve rejeitar itens com UID e tratar UIDs do OTBM apenas como anchors. Action IDs também precisam de política explícita, inclusive para itens imóveis que o serializer base não cobre.

### 7.7 Concorrência e game loop

Operações de protocolo são enfileiradas no dispatcher (`src/protocolgame.cpp:779-850`); o dispatcher executa tarefas serialmente (`src/tasks.cpp:37-83`), e timers do scheduler voltam a ele (`src/scheduler.cpp:26-52`). Isso permite atribuir sequência consistente no game thread.

Por outro lado, observar e gravar toda mutação bloquearia o game loop ou criaria um grande backlog assíncrono. Como alterações permanentes devem ser raras e explícitas, uma confirmação síncrona da transação durável é aceitável na primeira versão. Se futuramente houver alto volume, será necessário um WAL local durável ou pipeline dedicado, mantendo a semântica de confirmação.

### 7.8 Crash no meio da gravação

Sem framing/transação, uma escrita parcial pode parecer válida. Em banco, todas as linhas de uma mutação multi-tile precisam estar na mesma transação. Em arquivo, seria obrigatório cabeçalho com tamanho, versão, sequence, transaction ID e checksum, além de flush (`FlushFileBuffers` no Windows), detecção/truncamento do tail incompleto e rename atômico de checkpoints.

### 7.9 Crescimento e compactação

Journal infinito piora startup, auditoria e storage. Apagar registros antes de confirmar um checkpoint completo causa perda. Compactação precisa de generations e high-watermark: escrever nova geração completa, marcá-la `COMPLETE` atomicamente e só depois remover entradas antigas em outra etapa recuperável.

### 7.10 Atualizações do OTBM/items.xml

Uma coordenada pode passar a ter outro ground, outra door ou nenhum tile; IDs podem mudar ou desaparecer. Reaplicar cegamente um snapshot antigo pode desfazer uma correção manual de mapa. É necessário fingerprint global, digest base por tile e uma estratégia de conflito/rebase. Transações multi-tile devem ser rejeitadas por inteiro se um de seus componentes for inválido.

### 7.11 Rollback parcial

Editar ou apagar journal histórico para “voltar atrás” destrói auditabilidade e pode deixar tiles relacionados em versões diferentes. Rollback operacional deve ser uma nova transação compensatória contendo o novo after-state desejado; compactação posterior elimina história antiga quando seguro.

## 8. Comparação das arquiteturas

Escala: **alta/médio/baixa** descreve a qualidade da propriedade; em “complexidade” e “custo em jogo”, alta significa mais difícil/caro.

| Abordagem | Crash safety | Complexidade | Custo em jogo | Debug | Duplicação | Compactação | Houses | Temporários | Containers/attrs | Startup |
|---|---|---:|---:|---|---|---|---|---|---|---|
| 1. log literal de operações | alta com WAL/txn | alta | baixo a alto conforme volume | boa como auditoria, difícil para replay sem identidade | alto risco | média | conflito se não filtrado | ruim sem opt-in | difícil | proporcional ao log inteiro |
| 2. snapshot do tile vivo | alta com txn | média | alto por serializar tile | fácil de inspecionar | médio | fácil | alto conflito | **muito ruim**, captura lixo | boa estruturalmente | proporcional aos tiles |
| 3. diff contra OTBM | média/alta | muito alta | médio | difícil, mas conflito é explícito | baixo/médio | média | pode excluir houses | boa apenas com intenção separada | complexa | precisa carregar base e aplicar diff |
| 4. DB por tile/operação | alta com InnoDB correto | média | latência de commit | boa via consultas/metadados | baixo com constraints/txn | boa | fácil separar | depende da API | blobs versionados suportam | indexável/gerenciável |
| 5. arquivo próprio/WAL | alta se implementado rigorosamente | alta | append rápido | requer ferramentas próprias | baixo com seq/checksum | complexa | separável | depende da API | possível | leitura sequencial/checkpoint |
| 6. híbrido journal + checkpoint | **alta** | alta | commit curto em mudanças raras | **boa** | **baixo** com after-state idempotente | **boa** | separável | **boa com opt-in** | boa com serializer correto | rápido após checkpoint |

### 8.1 Log de operações

Vantagem: preserva intenção e oferece auditoria. Problema: `add/remove/move/transform` de baixo nível não têm identidade durável; stackpos, autostack e transformações que substituem o objeto tornam replay literal frágil. Seria viável somente se os registros fossem emitidos por uma API permanente de alto nível e contivessem estado/precondições suficientes.

### 8.2 Snapshot por tile

É simples substituir o último estado conhecido, mas snapshot do tile **vivo** é semanticamente errado. Para ser seguro, ele deve ser snapshot apenas da projeção persistente, e não de creatures/fields/drops/porta temporária. Nesse momento, deixa de ser a abordagem ingênua e se aproxima da recomendação híbrida.

### 8.3 Diferencial contra o OTBM

Representa bem “remover item base” e evita cópias redundantes. Entretanto, matching de objetos iguais, mudanças de ordem e atualizações do OTBM tornam o algoritmo difícil. Um digest da base e uma projeção after-state podem obter boa parte da segurança sem implementar inicialmente um diff estrutural mínimo perfeito.

### 8.4 Banco de dados

É a opção de storage preferida neste projeto: MariaDB já é requisito, migrations e transações já existem, blobs de itens já são utilizados e há ferramentas operacionais conhecidas. Uma tabela de transações + registros por tile, combinada com checkpoint generations, oferece atomicidade e consulta. A configuração de durabilidade precisa ser validada antes de produção.

### 8.5 Arquivo próprio

Pode reduzir latência com append, mas transfere ao projeto toda a responsabilidade de WAL: framing, checksum, versionamento, flush, truncamento de tail, atomic rename, lock, recuperação e ferramentas. Não há infraestrutura pronta suficiente em `PropWriteStream` para isso.

### 8.6 Híbrido

Journal imediato evita depender de shutdown; checkpoint limita replay; after-state por tile dá idempotência; transactions preservam mudanças multi-tile; metadados de base permitem conflito controlado. É a melhor combinação desde que o **filtro opt-in venha antes da gravação**.

## 9. Recomendação

Recomenda-se criar futuramente um `PersistentMapService` com MariaDB, usando um modelo híbrido de **transações incrementais de after-state + checkpoint por geração**.

Modelo conceitual:

```text
OTBM base (imutável durante a execução)
        +
house overlay existente (somente HouseTile)
        +
projeção permanente explícita (novo sistema, não-house)
        +
overlay temporário de runtime (nunca salvo pelo novo sistema)
        =
tile vivo entregue pelo Game
```

O `Tile` atual não materializa essas camadas separadamente. O serviço futuro deve manter sua própria projeção permanente por tile. Ao concluir uma operação marcada como permanente, ele recalcula/serializa somente essa projeção e não enumera indiscriminadamente tudo que está no tile vivo.

Cada transação conceitualmente deve incluir:

- `world_id`/namespace;
- versão do schema de persistência;
- fingerprint do OTBM e versão dos itens;
- transaction ID e sequence monotônica;
- coordenadas dos tiles envolvidos;
- digest/precondição do tile base;
- projeção permanente final serializada;
- checksum do payload;
- reason/system/actor;
- timestamp de commit.

Não se propõe schema nesta fase; os campos acima são requisitos de desenho.

### Por que after-state em vez de operações literais

- aplicar o mesmo registro duas vezes produz o mesmo resultado;
- várias transformações podem ser compactadas no último estado confirmado;
- remove dependência de stackpos e ponteiro;
- containers podem ser validados como árvore completa;
- permite checkpoint direto;
- uma remoção permanente pode ser expressa mesmo sem existir um `Item` vivo com flag.

O journal ainda conserva transaction ID, sequência, autor e motivo para auditoria. Ele é incremental porque registra apenas tiles explicitamente alterados, próximo da operação, e não salva o mapa inteiro.

## 10. Política sugerida de persistência

### 10.1 Política principal

**Default deny:** todas as APIs existentes continuam temporárias. Só uma API exclusiva pode criar uma alteração permanente.

Possíveis superfícies Lua/C++:

- `Game.createPersistentItem(...)`;
- `Game.removePersistentItem(...)`;
- `Game.transformPersistentItem(...)`;
- `Game.movePersistentItem(...)` para movimentos estritamente entre tiles permanentes permitidos;
- `Game.setPersistentTileState(...)` somente para tooling administrativo controlado;
- uma transação `PersistentMapTransaction` para alterações multi-tile.

Essas APIs devem exigir `reason/system`, validar escopo e retornar sucesso somente após commit durável.

### 10.2 Comparação das políticas

| Política | Vantagem | Falha principal | Veredito |
|---|---|---|---|
| toda alteração automática | zero esforço dos scripts | persiste combate, drops, decay, houses e timers incompletos | rejeitada |
| blacklist de itens temporários | reduz alguns casos óbvios | intenção não está no ID; lista sempre incompleta | insuficiente |
| região/tile marcado persistente | útil como limite operacional | captura conteúdo temporário que passa pela região | apenas guard adicional |
| flag no item | simples para criação | não representa remoção; pode acompanhar item até player/house | não usar como autoridade |
| boolean opcional em add/remove/transform | pequena mudança de API | propagação acidental e chamadas esquecidas | menos seguro |
| `Tile:markPersistent()` | simples | snapshot vivo captura drops/fields/porta aberta | rejeitada como API normal |
| APIs permanentes exclusivas | intenção explícita e auditável | exige migração consciente de cada feature | **recomendada** |
| híbrida: regras + override | flexível | regras implícitas podem voltar a capturar temporários | usar somente com allowlists rígidas |

### 10.3 Guardrails iniciais

- rejeitar `HouseTile`;
- rejeitar item com UID;
- rejeitar creature/effect e qualquer cylinder de player;
- rejeitar itens com decay ativo;
- rejeitar tipos especiais não allowlisted;
- limitar profundidade, número de filhos e bytes de container;
- exigir transação única para múltiplos tiles;
- impedir pickup/move por jogador de objetos do overlay;
- permitir opcionalmente regiões allowlisted, mas nunca inferir persistência apenas pela região;
- registrar actor/reason e métricas;
- manter `ReplayGuard`/`LoadGuard` para proibir journal durante load, replay e reconciliação.

## 11. Compatibilidade com houses e demais saves

### Houses

`HouseTile` deve permanecer sob autoridade exclusiva de `IOMapSerialize` e `tile_store`. A API permanente nova deve falhar antes de mutar se origem ou destino for house. Não se deve “deduplicar depois”; a exclusão precisa ser uma invariável de entrada.

O novo replay deve ocorrer depois de `loadHouseItems`, mas não deve tocar houses. Essa ordem garante que a topologia final de houses já existe e evita que uma futura região/alteração sobreposta ignore o domínio.

### Jogadores

Não permitir que itens do overlay sejam coletados ou enviados a inventário/depot na primeira versão. Se o produto exigir transferência econômica do mundo persistente para jogador, será necessário coordenar remoção do overlay e inclusão no inventário numa transação durável única. O save atual de player não fornece essa atomicidade no momento do pickup.

### Doors, beds, containers e writables

- doors/beds de house continuam em house persistence;
- doors globais permanentes só após serializer/reconciliation específico;
- beds fora de house devem ser rejeitadas inicialmente;
- containers podem ser suportados por allowlist com árvore completa e limites;
- writables exigem serializer explícito de texto/writer/date/custom attrs;
- teleports/depots/mailboxes/trash holders/fields devem começar rejeitados até uma decisão por subtipo.

### Save geral e camada dirty

`saveServer`, hourly save, logout e dirty houses não devem limpar, confirmar ou substituir o journal do mapa. Checkpoint pode ser solicitado no save normal como otimização, mas a alteração permanente já precisa estar durável antes disso.

## 12. Estratégia de crash safety e atomicidade

### 12.1 Protocolo recomendado de commit

Para uma operação permanente rara e explícita:

1. executar no dispatcher e abrir uma transação lógica;
2. validar todos os tiles, domínios, itens e precondições;
3. calcular a projeção after-state sem ainda expor a mudança no tile vivo;
4. serializar e validar novamente o payload completo;
5. gravar transaction header e todos os tile records numa transação MariaDB;
6. confirmar o banco;
7. materializar exatamente a projeção confirmada no tile vivo por uma rota controlada;
8. somente então disparar notifications e retornar sucesso ao script/GM/sistema externo.

Esse protocolo evita expor uma mudança que só existe na memória. Há duas janelas inevitáveis, ambas tratáveis:

- crash antes do commit: no restart prevalece o estado antigo; a operação não tinha sido confirmada nem materializada;
- crash depois do commit e antes da materialização/resposta: no restart o replay aplica o estado novo; uma repetição do comando deve usar transaction/request ID idempotente.

Se a materialização falhar depois do commit sem que o processo caia, o servidor não deve continuar aceitando jogo naquele estado divergente: deve tentar reconciliar o record confirmado e, se não conseguir, entrar em safe mode/encerrar para que o replay restaure a autoridade durável. Uma variante apply-first só seria aceitável se todos os efeitos fossem isolados/bufferizados e houvesse rollback in-memory comprovadamente completo; ela é mais arriscada para a primeira versão porque `Tile::addThing` pode disparar notifications e movement scripts.

### 12.2 Transações multi-tile

Move, construção e quest global podem afetar vários tiles. Todos os after-states devem compartilhar transaction ID e commit. Replay valida o conjunto inteiro antes de aplicar. Nunca se deve confirmar origem sem destino ou metade de uma barreira.

### 12.3 Durabilidade

Antes de produção, validar:

- engine InnoDB em todas as tabelas;
- `innodb_flush_log_at_trx_commit = 1` ou requisito equivalente aprovado;
- comportamento do storage/UPS;
- timeout/retry sem duplicar request ID;
- backup e restauração consistentes do OTBM, items e banco;
- métricas/alertas de falha de commit.

### 12.4 Corrupção

Cada blob deve ter versão, tamanho esperado e checksum. Payload inválido deve ser colocado em quarentena lógica e impedir aplicação silenciosa. Nunca desserializar árvore arbitrária sem limites.

## 13. Estratégia de replay

### 13.1 Ponto na inicialização

O replay deve ocorrer:

1. após `Map::loadMap` terminar OTBM, houses XML, house info e house items (`src/map.cpp:31-50`);
2. antes de `GAME_STATE_INIT`, spawns, raids e global startup (`src/otserv.cpp:289-337`).

Isso garante que a base e o overlay de houses já existem, mas criaturas/scripts ainda não alteraram os tiles. Se a arquitetura de startup for refatorada, a invariável deve permanecer.

### 13.2 Passos do replay

1. habilitar `ReplayGuard` e bloquear emissão de journal;
2. ler a última checkpoint generation marcada `COMPLETE`;
3. validar schema, world ID, fingerprint e checksums;
4. validar todos os itens e tiles de cada transação;
5. aplicar checkpoint em ordem canônica;
6. aplicar journal posterior ao high-watermark, por sequence;
7. materializar por API interna sem movement events/decay/notifications normais;
8. verificar a projeção resultante/digest;
9. desabilitar guard e prosseguir com init.

Não é necessário gravar “aplicado” no banco durante o replay: o mapa base nasce limpo a cada processo. A combinação de after-state + sequence faz cada reaplicação idempotente.

### 13.3 Falha durante replay

O servidor não deve entrar em estado normal se uma transação obrigatória foi parcialmente aplicada. Deve validar antes, aplicar atomicamente em memória por transaction ID e, se falhar, desfazer a transação ou abortar startup em safe mode. No próximo processo, o OTBM é recarregado do zero e o mesmo replay pode ser executado novamente.

### 13.4 Sem efeitos colaterais

Usar `Tile::internalAddThing` ou um reconciliador equivalente, com atualização deliberada de flags/subclasses, e não `Map::setTile`/`addThing` genéricos que disparam movement scripts (`src/tile.cpp:1320-1457`). Não iniciar decay nem disparar scripts como consequência acidental.

## 14. Estratégia de compactação

### 14.1 Gerações e high-watermark

1. escolher sequence `H` como high-watermark;
2. montar a projeção permanente mais recente para todos os tiles alterados até `H`;
3. gravar uma nova generation com todos os blobs/checksums;
4. marcar a generation `COMPLETE` e seu `H` na mesma transação;
5. startup passa a preferir essa generation;
6. em uma transação posterior, apagar/arquivar journal `<= H` e generations antigas.

Se houver crash antes de `COMPLETE`, a geração anterior continua válida. Se houver crash depois de `COMPLETE` e antes da limpeza, há dados redundantes, não perda.

### 14.2 Gatilhos

Compactar por limite de rows, bytes, idade e tempo estimado de replay, não somente no shutdown. Um save normal pode solicitar compactação, mas não é o único gatilho.

### 14.3 Várias transformações

O journal mantém as transações confirmadas para auditoria até a compactação; o checkpoint retém somente a última projeção por tile em `H`. Isso evita reaplicar centenas de transformações intermediárias sem perder consistência do estado final.

## 15. Estratégia para atualização do `.otbm`

### 15.1 Não aplicar cegamente

O checkpoint/journal deve conhecer:

- fingerprint global do OTBM;
- versão OTBM/item map;
- digest base dos tiles tocados;
- IDs/tipos esperados dos anchors modificados.

Fingerprint diferente não significa que todo overlay é inválido, mas deve impedir replay cego.

### 15.2 Reconciliação por tile

Para cada tile alterado:

- se o digest base é igual, aplicar normalmente;
- se a base mudou mas a alteração ainda é reconciliável por anchor estável, aplicar somente mediante regra/migração conhecida;
- se há conflito, colocar a transação inteira em quarentena e gerar relatório;
- nunca aplicar parcialmente uma transação multi-tile.

### 15.3 Rebase operacional

Uma ferramenta futura deve mostrar base antiga esperada, base nova e projeção persistente, permitindo:

- manter a alteração;
- descartar o overlay daquele tile;
- migrar IDs/anchors;
- incorporar a mudança ao novo OTBM e aposentar o overlay.

Esse processo deve criar nova transação/generation auditável. Não editar blobs manualmente em produção.

### 15.4 Item removido do `items.xml`

Se um ID não existe ou mudou de subtipo, não criar item 0, não ignorar só o filho inválido e não aplicar o restante do container. Quarentenar a transação/tile, registrar coordenada/ID/schema e iniciar em safe mode ou abortar conforme política aprovada.

## 16. Casos de teste necessários

### 16.1 Os 15 cenários solicitados

1. **Magic wall temporária removida por alavanca:** a operação usa API normal, não entra no journal. Crash antes dos 3 minutos recarrega o OTBM e a wall volta imediatamente. Se “tempo restante exato” for requisito, testar um journal de eventos/deadline separado.
2. **Parede removida permanentemente por administração:** comando usa transação permanente; commit antes de sucesso; kill imediato e restart mantêm a remoção.
3. **Jogador joga item no chão:** não cria record. Após crash o item não volta. Testar stack parcial e PZ.
4. **Móvel dentro de casa:** somente dirty house/`tile_store`. Confirmar que nenhuma tabela/entrada do novo sistema é tocada e que o móvel volta pela house.
5. **GM cria decoração permanente:** API exclusiva, actor/reason auditados. Confirmar restart, bloqueio de pickup e rejeição de UID/tipo não permitido.
6. **Quest transforma pedra por 30 segundos:** API temporária; crash deixa a pedra no estado base. Replay não preserva o estado intermediário.
7. **Quest global remove barreira:** uma transação engloba todos os tiles e o estado global. Kill entre etapas não deixa metade da barreira removida.
8. **Fire field ativo:** não entra no journal e não existe após restart, salvo field pertencente ao OTBM base.
9. **Container aninhado em tile persistente:** serializar ordem, attrs, counts e filhos; testar várias profundidades, limites, checksum e rejeição atômica de filho inválido.
10. **Item persistente entra em decay:** na primeira versão, operação é rejeitada. Numa versão futura, testar `expires_at`, downtime maior que prazo e cadeia de transformações vencidas.
11. **Várias transformações antes do save/checkpoint:** cada commit recebe sequência; replay do journal e replay do checkpoint convergem para o último after-state.
12. **OTBM atualizado com overlay antigo:** fingerprint diverge; tile inalterado pode ser validado por digest, tile alterado entra em conflito; nenhuma aplicação cega.
13. **Item deixou de existir no `items.xml`:** transação inteira é quarentenada; servidor não cria item inválido nem aplica metade do container.
14. **Crash no exato momento da gravação:** injetar kill antes/durante/depois do commit. A transação deve estar inteira ausente ou inteira presente; request ID impede duplicação após retry.
15. **Crash durante replay:** matar após cada record/transaction. Novo processo recarrega base e replay idempotente converge sem gerar journal novo.

### 16.2 Testes adicionais obrigatórios

- add/remove/move/transform permanente, incluindo movimento multi-tile;
- rollback em memória quando banco falha;
- autostack, remainder e transformação que troca classe C++;
- top/down item ordering e tiles com criaturas presentes;
- ground replacement e criação/limpeza de tile vazio;
- atributos: text, writer, date, count, charges, action e custom;
- rejeição de UID duplicado;
- teleport/door/bed/depot/mailbox/trash/field por allowlist/denylist;
- container com muitos filhos, profundidade maliciosa e blob truncado;
- origem/destino `HouseTile`, incluindo bordas/doors;
- tentativa de mover persistente para player/container/house;
- clean map, server save, logout e hourly save sem interferência;
- `Game.loadMap` durante runtime com `LoadGuard`;
- startup script que altera o mesmo tile depois do replay;
- duas solicitações com mesmo request ID;
- duas transações concorrentes no mesmo tile e em tiles disjuntos;
- checkpoint concorrendo com novos commits;
- crash em cada fase da compactação;
- checksum inválido, generation incompleta e sequence faltante;
- OTBM alterado só fora dos tiles tocados;
- rebase/rollback por transação compensatória;
- medição de latência p50/p95/p99 de commit no dispatcher;
- volume de journal/checkpoint e tempo de startup;
- restart normal, kill do processo e simulação de power loss/storage fault.

O checkout não apresenta uma suíte de testes unitários integrada no `CMakeLists.txt`; uma futura implementação deverá acrescentar testes de serializer/reconciliation e um harness de integração com banco/process kill, em vez de depender somente de teste manual no servidor.

## 17. Arquivos que provavelmente precisariam ser modificados numa futura implementação

Esta lista é planejamento, não uma mudança realizada:

| Arquivo/área | Papel provável |
|---|---|
| novos `sources/nekiro-tfs-1.5-7.72/src/persistentmap.h` e `persistentmap.cpp` (nomes provisórios) | serviço, transaction model, journal/checkpoint/replay/reconciliation |
| `sources/nekiro-tfs-1.5-7.72/src/CMakeLists.txt` | incluir os novos fontes |
| `sources/nekiro-tfs-1.5-7.72/src/game.h` e `game.cpp` | APIs permanentes, transação multi-tile, rollback e integração no dispatcher |
| `sources/nekiro-tfs-1.5-7.72/src/map.h` e `map.cpp` | lookup/reconciliação de tiles e ponto de integração, sem mudar `Map::save` para mapa geral |
| `sources/nekiro-tfs-1.5-7.72/src/tile.h` e `tile.cpp` | materialização controlada/projeção e guardas, se estritamente necessário |
| `sources/nekiro-tfs-1.5-7.72/src/item.h` e `item.cpp` | serializer versionado e validação de attrs/proveniência |
| `sources/nekiro-tfs-1.5-7.72/src/container.h` e `container.cpp` | árvore canônica, limites e ordenação |
| `sources/nekiro-tfs-1.5-7.72/src/teleport.cpp`, `house.cpp` e `bed.cpp` | auditoria/serialização de subtipos; houses continuam excluídas |
| `sources/nekiro-tfs-1.5-7.72/src/iomapserialize.h` e `iomapserialize.cpp` | possível extração de serializer reutilizável, sem compartilhar ownership de house |
| `sources/nekiro-tfs-1.5-7.72/src/luascript.h` e `luascript.cpp` | APIs Lua exclusivas e `ReplayGuard`/`LoadGuard` |
| `sources/nekiro-tfs-1.5-7.72/src/otserv.cpp` | replay entre load completo do mapa/houses e `GAME_STATE_INIT` |
| `sources/nekiro-tfs-1.5-7.72/src/database.cpp` e `databasemanager.cpp` | operações/transactions e migrations, se a API atual for insuficiente |
| `sources/nekiro-tfs-1.5-7.72/src/configmanager.cpp` e `server/config.lua` | enable flag, world ID, políticas/limites e safe mode |
| novo migration numerado em `server/data/migrations` e `server/schema.sql` | schema versionado de transações, records e checkpoints |
| `server/data/talkactions/scripts/...` | comandos GM explícitos, status, rebase e rollback controlados |
| nova área de testes | unitários de serializer/diff e integração de crash/replay/DB |

Não se recomenda espalhar um booleano por todas as chamadas existentes de `internalAddItem`, `internalRemoveItem`, `internalMoveItem` e `transformItem`. Esses métodos devem continuar sendo primitivas gerais; a camada permanente deve envolvê-los intencionalmente.

## 18. Dúvidas ou decisões antes da implementação

As seguintes decisões precisam de resposta explícita:

1. Quais sistemas concretos terão autorização para criar mudanças permanentes na primeira versão: apenas GM, quests globais específicas, construção, ou todos?
2. O escopo inicial será uma allowlist de regiões/coordenadas além do opt-in por operação?
3. Mudanças permanentes poderão afetar ground/flags/criação de tile, ou somente itens sobre tiles existentes?
4. Objetos persistentes serão sempre imóveis para jogadores? A recomendação inicial é sim.
5. Containers aninhados entram na primeira versão ou serão adiados?
6. Quais subtipos especiais entram na allowlist inicial? A recomendação é começar com itens comuns, sem UID, sem decay e sem comportamento especial.
7. Action IDs e custom attributes podem ser criados pelo overlay? Como impedir colisões de scripts?
8. O que fazer no startup diante de conflito de OTBM: abortar, safe mode sem overlay, ou quarentenar só a transação afetada? Para alterações globais críticas, abortar/safe mode é mais seguro.
9. Qual será o `world_id` quando houver cópias do mesmo banco/mapa para test e produção?
10. Qual SLA máximo de latência para uma operação permanente? Isso define commit síncrono direto versus WAL dedicado.
11. É necessário guardar histórico completo para auditoria ou apenas até N checkpoints/dias?
12. Rollback será por transaction ID, por tile ou por release de mapa?
13. Uma atualização de OTBM poderá incorporar overlays e zerar o journal? Qual será o fluxo de rebase/deploy?
14. É aceitável que mudanças temporárias com timer resetem imediatamente no restart? Para os scripts atuais, essa é a política segura; exceções precisam de uma feature de evento durável.
15. Haverá itens permanentes com decay? A recomendação inicial é não; se sim, definir deadline absoluto e semântica durante downtime.
16. Quest global terá uma tabela/estado canônico próprio, ou o overlay será a autoridade? Duas autoridades independentes devem ser evitadas.
17. O banco de produção está configurado para confirmação realmente durável e possui storage/backup compatíveis?
18. Quais ferramentas operacionais são obrigatórias antes de ativar: inspect, verify, dry-run replay, conflict report, rollback, compact e rebase?

## Conclusão

O código atual oferece bons blocos básicos — serialização de itens, transações MariaDB, execução serial pelo dispatcher e persistência especializada de houses — mas não oferece um journal de mapa geral. A unidade segura não é “qualquer operação de tile” e tampouco “o tile vivo inteiro”; é uma **transação permanente intencional que atualiza uma projeção persistente separada**.

O próximo passo correto, antes de código, é aprovar a política de escopo e responder às decisões da seção 18. Só então vale especificar schema, formato binário versionado, contrato exato das APIs Lua/C++, máquina de estados de commit/replay e plano de testes de falha.
