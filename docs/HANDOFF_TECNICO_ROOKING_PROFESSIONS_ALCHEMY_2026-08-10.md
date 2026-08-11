# Handoff tecnico: Rooking, Professions/Alchemy e Gold Converter

Data da consolidacao: 2026-08-10  
Workspace auditado: `D:\tibia-oldschool`  
Destinatario: outra IA trabalhando em uma implementacao paralela  
Estado do repositorio: arvore local com alteracoes nao commitadas e historico anterior compartilhado; nao assumir que todo o diff de um arquivo pertence a estas duas etapas.

## 1. Objetivo e regras de integracao

Este documento descreve o estado efetivamente implementado no workspace local para:

1. rooking e a correcao especial do level 6 -> 7 para Sorcerer/Druid;
2. dominio de Professions, primeira profession Alchemy, persistencia, protocolo e UI;
3. Gold Converter, origem do gold de criatura e assets 7.72;
4. correcoes feitas depois dos testes, incluindo reset integral de itens, preservacao do historico de mortes, crash de login e ajuste final da velocidade de Alchemy.

Regras importantes para a IA receptora:

- A arvore de fonte do servidor e `sources/nekiro-tfs-1.5-7.72`.
- A arvore executada e `server`. Scripts, migrations, schema e dados relevantes foram espelhados nas duas arvores.
- Alguns pares nao sao byte a byte identicos por divergencias preexistentes. Portar apenas os hunks/simbolos descritos neste documento; nao substituir arquivos inteiros cegamente.
- `src/game.cpp`, `src/iologindata.cpp` e `default_onLook.lua` contem outras alteracoes de persistencia/diagnostico que nao pertencem a esta frente e devem ser preservadas.
- O root `D:\tibia-oldschool` nao deve ser tratado como um Git worktree limpo ou como patch isolado.
- Nenhuma alteracao de Alchemy deve ser aplicada a mortes normais. O unico reset de Alchemy implementado em morte e o rooking.

## 2. Resumo do estado final

### 2.1 Rooking

- Um personagem com vocation e perda de skills ativa morre normalmente ate o level 6.
- Se uma morte reduzir o personagem de level >= 6 para level < 6, o rooking e disparado.
- O reset coloca o personagem em level 1, experience 0, vocation 0, town 11 (Rookgaard), posicao do templo da town, atributos/skills iniciais, outfit inicial e sem itens persistidos.
- Inventario, depot, inbox/store inbox, storage, spells, bestiary, charms, outfits em memoria, bank, blessings, skull e outros campos basicos sao resetados.
- `player_deaths` e preservado deliberadamente para investigacao de hacking/abuso.
- O login seguinte reutiliza o mecanismo existente de first login para entregar os itens iniciais.
- Somente Sorcerer/Druid, incluindo promoted vocations cuja base e 1 ou 2, usam os ganhos de no vocation na transicao exata 6 -> 7.
- O loop de level-up processa cada level individualmente. Assim, 6 -> 9 aplica: 6 -> 7 no vocation; 7 -> 8 e 8 -> 9 com a vocation real.
- Knight e Paladin nao entram na excecao.

### 2.2 Professions/Alchemy

- Primeira profession: `Alchemy`.
- Estado persistido: apenas `level` e `tries`.
- Percentual nao e persistido; e derivado para evitar estado redundante.
- Estado inicial: level 10, tries 0.
- Curva final: base 50, multiplicador 1.1 por level e divisor/rate 2.5.
- O ajuste solicitado de 2.5x foi aplicado na velocidade de progressao do skill, nao na chance do conversor.
- Alchemy e resetada para 10/0 dentro de `Player::resetToRookgaard()` e nao em `Player::death()` normal.

### 2.3 Gold Converter

- Server item ID: 26378.
- Client item ID/appearance: 5095.
- Sprite nova adicionada ao `Tibia.spr`: 16115.
- Cargas iniciais: 100; peso: 3.00 oz; `showcharges=1`.
- Converte qualquer stack completo de 100 gold, independentemente da origem.
- Somente gold integralmente originado de criatura concede 1 try de Alchemy em uma conversao bem-sucedida.
- Sucesso: transforma em 1 platinum common, consome uma carga e mostra `CONST_ME_MAGIC_BLUE` sobre o jogador.
- Falha: preserva o gold, consome uma carga e mostra `CONST_ME_POFF` sobre o jogador.
- Chance final permaneceu na formula original; nao recebeu multiplicador 2.5.
- Gold de criatura e marcado com custom attribute `creaturestack=true`.
- Somente account type exatamente GM ou GOD ve `Gold origin: creature/common` no look.
- Mistura entre gold de criatura e gold common torna o stack resultante common, de forma conservadora.
- O platinum resultante tem `creaturestack` removido explicitamente, mesmo que `transformItem`/`Item::transform` preserve os custom attributes da instancia.

## 3. Rooking: implementacao detalhada

### 3.1 Constantes de estado inicial

As constantes estao no namespace anonimo de `src/player.cpp` e foram alinhadas aos defaults ativos de criacao e ao destino do Account Clerk:

| Campo | Valor |
|---|---:|
| Limite de rooking | 6 |
| Level inicial | 1 |
| Experience inicial | 0 |
| HP/max HP inicial | 150 |
| Mana/max mana inicial | 0 |
| Capacity interna | 40000, equivalente a 400.00 oz |
| Soul | 0 |
| Stamina | 2520 minutos |
| Offline training time | 12 horas em milissegundos |
| Offline training skill | -1 |
| Looktype female | 136 |
| Looktype male | 128 |
| Town de Rookgaard | 11 |

Os skills tradicionais sao resetados por `skill = Skill{}`. Nesse fork, o default de `Skill` corresponde a level 10 e tries 0.

### 3.2 Correcao de mana/atributos em 6 -> 7

Funcao: `shouldUseNoVocationLevelGain(uint32_t currentLevel, const Vocation* vocation)`.

Condicoes simultaneas:

- `currentLevel == 6`;
- vocation valida;
- `vocation->getFromVocation()` e Sorcerer (1) ou Druid (2).

Em `Player::addExperience`, o `while (experience >= nextLevelExp)` escolhe a vocation de ganho antes de cada incremento. Apenas nessa fronteira usa `VOCATION_NONE`; os demais passos usam a vocation real. HP, mana e capacity sao incrementados usando `getHPGain()`, `getManaGain()` e `getCapGain()` da vocation escolhida para aquele passo.

Consequencias:

- Mage 6 -> 7: +5 HP, +5 mana e cap de no vocation.
- Mage 7 -> 8: ganhos normais de mage.
- Mage 6 -> 9 em um unico ganho: a excecao ocorre uma vez, sem multiplicacao global incorreta.
- Knight/Paladin: fluxo anterior integralmente preservado.

O caso de referencia 8 -> 7 -> 6 -> 7 -> 8 volta a 35 mana no level 8, evitando que o personagem termine com 60 mana.

### 3.3 Deteccao antes do drop e dentro da morte

`Player::willBeRookedOnDeath()` e uma preditiva usada pelo Lua de drop. Ela retorna falso se:

- nao ha perda de skills;
- nao ha vocation valida;
- a vocation e 0;
- o level atual e menor que 6;
- a perda calculada de experience e zero.

Caso contrario, calcula a experience restante com a mesma perda de morte e reduz um `resultingLevel` temporario. Retorna verdadeiro quando o resultado e menor que 6.

No fluxo efetivo de `Player::death()`:

1. perdas normais de magic level e skills sao processadas;
2. a experience e removida;
3. o loop existente reduz level, HP, mana e capacity por level perdido;
4. depois do loop, `shouldRook` exige vocation != 0, `oldLevel >= 6` e `level < 6`;
5. `resetToRookgaard()` executa o reset;
6. o jogador recebe `You have been rooked and returned to Rookgaard.`.

O creaturescript `droploot.lua` chama a funcao preditiva antes do fluxo normal de Amulet of Loss/red skull/drop. Em caso de rooking, remove todos os slots de `CONST_SLOT_HEAD` ate `CONST_SLOT_AMMO` e retorna. Isso impede que os itens antigos sejam preservados ou enviados ao corpse por regras normais de morte.

### 3.4 Reset em memoria

`Player::resetToRookgaard()` executa:

- resolve town ID 11 e vocation 0; falha com log se qualquer uma estiver ausente;
- muda town e `loginPosition` para o templo de Rookgaard;
- marca `rookgaardResetPending=true`;
- remove todos os itens de inventario/equipamento;
- esvazia depot lockers e depot chests ja carregados;
- limpa `storageMap`;
- limpa `learnedInstantSpellList`;
- limpa `charmStates`;
- limpa `outfits`;
- reseta level, experience e level percent;
- reseta HP, mana, capacity e soul;
- zera bank balance;
- limpa blessings, skull e skull ticks;
- reseta offline training, stamina, last login, last IP e last logout;
- define direcao sul;
- recria outfit base conforme sexo, sem cores/addons anteriores;
- zera magic level, mana spent e magic percent;
- reseta todos os skills tradicionais;
- chama `resetAlchemy()`;
- recalcula velocidade base.

### 3.5 Reset persistente

`rookgaardResetPending` altera o save em `IOLoginData::savePlayer`:

- persistent conditions nao sao serializadas;
- `lastlogin=0`, `lastip=0`, `lastlogout=0` e `onlinetime=0`;
- tabelas explicitamente apagadas: `player_bestiary_progress`, `player_charms`, `player_inboxitems`, `player_storeinboxitems`, `guild_invites`;
- `player_spells` e apagada e a lista vazia e persistida;
- `player_storage` segue o save da estrutura vazia;
- `player_items` e apagada e o inventario vazio e persistido;
- `player_depotlockeritems` e `player_depotitems` sao forcosamente apagadas mesmo que o dirty flag normal nao exigisse save;
- durante o reset pendente, nenhum item de depot e reinserido.

`player_deaths` nao aparece no array de tabelas de reset. Ela chegou a ser considerada na primeira versao do full reset, mas foi removida deliberadamente para preservar auditoria/historico.

### 3.6 Itens iniciais reutilizados

Arquivo consultado, nao modificado: `server/data/creaturescripts/scripts/firstitems.lua`.

Como o rooking persiste `lastlogin=0`, o proximo login reutiliza o evento existente e entrega:

- item 2050;
- item 2382;
- item 2651 para sex 0 ou 2650 para o outro sexo;
- `ITEM_BAG` contendo item 2674.

Nao foi duplicada uma segunda lista de starter items dentro do C++.

### 3.7 Estados deliberadamente preservados ou ainda sem decisao

O reset atual nao remove ou normaliza automaticamente:

- `player_deaths`: preservado por decisao explicita;
- `guild_membership`: membership/rank/nick continuam existentes;
- propriedade de casa em `houses.owner` e dados de bid relacionados;
- `market_offers` e `market_history`;
- `player_namelocks`;
- entradas de VIP de outras contas que apontam para esse player;
- `group_id`/privilegios administrativos;
- `account_id`, nome, sexo, save/deletion flags e outros metadados de identidade;
- dados account-wide, naturalmente fora do escopo do personagem.

Antes de chamar o reset de equivalente absoluto a excluir e recriar um personagem, e necessario decidir especialmente guild membership, house ownership e ofertas ativas de market. Historicos de morte/market e metadados antiabuso provavelmente devem continuar preservados.

## 4. Professions/Alchemy: implementacao detalhada

### 4.1 Dominio e curva

Novos arquivos `src/professions.h` e `src/professions.cpp` concentram a regra de progressao.

API:

- `professions::Progress { uint32_t level; uint64_t tries; }`;
- `getAlchemyRequiredTries(level)`;
- `getAlchemyPercent(progress)`;
- `addAlchemyTries(progress, count)`;
- `sanitizeAlchemyProgress(progress)`.

Formula final:

```text
required(level) = max(1, trunc((50 * 1.1^(level - 10)) / 2.5))
percent = floor(tries * 100 / required), limitado a 99
```

Primeiros requirements finais:

| Level atual | Tries para o proximo |
|---:|---:|
| 10 | 20 |
| 11 | 22 |
| 12 | 24 |
| 13 | 26 |
| 14 | 29 |
| 15 | 32 |
| 16 | 35 |
| 17 | 38 |
| 18 | 42 |
| 19 | 47 |
| 20 | 51 |

`addAlchemyTries` suporta carry e multiplos level-ups. `sanitizeAlchemyProgress` garante level >= 10; tries invalidos maiores/iguais ao requirement sao zerados. A migration 39 evita perda abrupta de percentual para dados ja existentes ao trocar a curva.

### 4.2 Integracao no Player e Lua

`Player` ganhou `alchemyProgress`, getters de level/tries/percent, `addAlchemyTries`, `resetAlchemy` e `sendProfessionData`.

Bindings Lua adicionados:

- `Player:getAlchemyLevel()`;
- `Player:getAlchemyTries()`;
- `Player:getAlchemyPercent()`;
- `Player:addAlchemyTries(count)`;
- binding de rooking separado: `Player:willBeRookedOnDeath()`.

Quando tries mudam ou Alchemy e resetada, o servidor envia o novo estado ao cliente.

### 4.3 Persistencia e custo SQL

Alternativa implementada: duas colunas em `players`:

```sql
alchemy_level INT UNSIGNED NOT NULL DEFAULT 10
alchemy_tries BIGINT UNSIGNED NOT NULL DEFAULT 0
```

O percentual nao e persistido.

As colunas foram adicionadas ao SELECT central de player e ao UPDATE central de `IOLoginData::savePlayer`. Nao foi criada a tabela `player_professions`.

Contagem incremental de queries/statements causada por Alchemy:

| Contexto | Colunas em `players` implementadas | SQL sincrono adicional no Dispatcher |
|---|---:|---|
| Login assincrono pelo Player I/O | 0 statements adicionais; +2 colunas no SELECT existente | Nao por causa de Alchemy |
| Logout pelo caminho assincrono existente | 0 statements adicionais; +2 assignments no UPDATE existente | Nao por causa de Alchemy |
| Logout que ja cai no caminho dirty/sincrono | 0 statements adicionais; +2 assignments no UPDATE existente | O save existente continua sincrono; Alchemy nao adiciona outro statement |
| Checkpoint player+floor atual | 0 statements adicionais; +2 assignments no UPDATE existente | Sim, os valores de Alchemy executam dentro do `savePlayerData()` sincrono que o checkpoint ja chama no Dispatcher |

Comparacao com a alternativa rejeitada `player_professions`:

| Contexto | Statements adicionais esperados | Efeito no Dispatcher sem refatoracao adicional |
|---|---:|---|
| Login | +1 SELECT | Poderia acompanhar o login assincrono |
| Logout | +1 UPSERT | Poderia acompanhar o logout assincrono, mas o caminho dirty continuaria conforme a arquitetura atual |
| Checkpoint player+floor | +1 UPSERT | Seria sincrono no Dispatcher porque o checkpoint atual executa o save ali |

Portanto, o checkpoint nao se tornou assincrono. A decisao apenas evitou criar uma nova query e incluiu Alchemy no checkpoint e no logout ja existentes.

### 4.4 Migrations

- Migration 38: cria as duas colunas usando `ADD COLUMN IF NOT EXISTS`.
- Migration 39: converte tries antigos para a curva 2.5x preservando aproximadamente o percentual atual, com clamp em `newRequired - 1`.

Para uma implementacao paralela que ainda nao tenha dados de Alchemy, a migration 39 continuara segura, mas a IA pode consolidar as migrations conforme a politica daquele branch. Nao persistir `percent`.

### 4.5 Protocolo e UI

Extended opcode reservado: 11 (`ExtendedIds.Professions`).

Payload atual:

```text
1|alchemy|LEVEL|PERCENT
```

O servidor envia o payload:

- no login inicial;
- no reconnect;
- depois de alteracao de progresso/reset.

O OTClient:

- registra/desregistra o opcode no ciclo do modulo;
- faz parse estrito da versao 1 e profession `alchemy`;
- mantem cache default 10/0;
- mostra uma linha Alchemy no skills window com level e barra percentual;
- oferece toggle de visibilidade no menu do skills window;
- limpa o cache no fim da sessao.

## 5. Gold Converter e rastreamento de origem

### 5.1 Registro e identificadores

O action e RevScriptSys e registra a si proprio no fim de `gold_converter.lua`:

```lua
goldConverter:id(26378)
goldConverter:register()
```

Nao existe alteracao em `actions.xml`.

O ID 26378 ja era referenciado pelo script e pela game shop, mas nao existia no `items.otb` ativo do protocolo 7.72. Ele foi criado/reutilizado nesse OTB e associado ao client ID 5095. O `Tibia.dat` foi ampliado ate o client item 5095 e o `Tibia.spr` recebeu a sprite 16115.

Nao foi criada versao HD. O PNG entregue pelo usuario foi importado nos assets classicos, tratando o fundo branco conectado as bordas como transparente.

### 5.2 Definicao do item

`items.xml`:

```xml
<item id="26378" article="a" name="gold converter">
    <attribute key="weight" value="300"/>
    <attribute key="charges" value="100"/>
    <attribute key="showcharges" value="1"/>
</item>
```

O engine compoe o look com quantidade restante de charges e peso, atendendo ao formato funcional esperado: `That has 100 charges left. It weighs 3.00 oz.`

### 5.3 Origem do gold

Na geracao de loot:

1. `default_onDropLoot.lua` chama `corpse:createLootItem(monsterLoot[i], true)`;
2. `Container::createLootItem` em Lua recebe/propaga `creatureLoot`;
3. se o item criado for gold, aplica custom attribute `creaturestack=true`;
4. criacoes recursivas em containers de loot tambem propagam o booleano.

Em C++:

- a chave foi centralizada como `ITEM_CUSTOM_ATTRIBUTE_CREATURE_STACK`;
- `Item::equals` ignora essa chave na comparacao de stackability;
- antes de merge, `Game::internalMoveItem` e `Game::internalAddItem` capturam a origem da fonte e do destino;
- se as origens diferem e houve merge, `creaturestack` e removido do destino e o item e marcado dirty para persistencia.

Politica resultante:

| Composicao do stack | Estado final |
|---|---|
| creature + creature | creature |
| common + common | common |
| creature + common, em qualquer ordem | common |

Essa escolha impede fabricar artificialmente 100% de gold treinavel a partir de uma pilha mista.

### 5.4 Regra final do conversor

Pre-condicoes:

- target existe;
- target e `ITEM_GOLD_COIN`;
- count e exatamente 100;
- conversor tem charges > 0.

Chance, em basis points:

```text
chance = min(5000, 400 + alchemyLevel * 60)
roll = random(1..10000)
```

Exemplos: level 10 = 1000 bp = 10%; cap = 50%. O multiplicador 2.5 nao participa dessa formula.

Sucesso:

1. memoriza se o gold original tinha `creaturestack=true`;
2. `target:transform(ITEM_PLATINUM_COIN, 1)`;
3. remove explicitamente `creaturestack`;
4. verifica se o resultado e exatamente 1 platinum sem o atributo;
5. se o gold original era de criatura, chama `player:addAlchemyTries(1)`;
6. envia `CONST_ME_MAGIC_BLUE` na posicao do jogador;
7. consome uma charge.

Falha:

- nao remove nem transforma o gold;
- envia `CONST_ME_POFF` na posicao do jogador;
- consome uma charge.

Target invalido retorna `false` e nao consome charge.

### 5.5 `transformItem` e custom attributes

`transform` atua sobre a mesma instancia e, por padrao, custom attributes podem sobreviver a transformacao. Logo, sem tratamento explicito, o fluxo abaixo poderia produzir platinum ainda marcado:

```text
100 creature gold -> transform(..., platinum) -> platinum com creaturestack
```

A implementacao atual nao depende desse comportamento implicito: logo apos a transformacao chama `removeCustomAttribute("creaturestack")` e valida que o atributo e `nil`. Assim o platinum e sempre common.

Alternativa arquitetural considerada: remover os 100 gold e criar explicitamente 1 platinum common. Ela tambem garantiria a origem neutra, mas introduziria uma operacao destrutiva seguida de criacao e exigiria tratamento de falha/rollback de insercao. O codigo atual preserva a instancia/posicao e neutraliza o atributo explicitamente.

### 5.6 Look restrito

Em `default_onLook.lua`, somente:

- `ACCOUNT_TYPE_GAMEMASTER`; ou
- `ACCOUNT_TYPE_GOD`

ve uma linha adicional `Gold origin: creature` ou `Gold origin: common` para gold coins. Community Manager e jogadores comuns nao entram por comparacao `>=`; a verificacao e exata conforme solicitado.

Esse arquivo ja continha diagnosticos de floor persistence para GOD e informacoes de access. Preservar essas secoes.

## 6. Crash de login e rebuild limpo

Depois da primeira integracao de Professions/Alchemy, login de personagem causou crash. O estado encontrado era um build incremental/stale inconsistente depois de alteracoes de headers/layout e da projecao de Player I/O.

Correcao executada:

- o `player_io_service` foi encerrado pelo usuario antes da substituicao;
- o build CMake/Ninja anterior foi arquivado para forensics;
- foi feito clean configure/full rebuild;
- `tfs.exe` e `player_io_service.exe` foram reconstruidos como par compativel;
- ambos foram copiados para `server`;
- o binario TFS foi recompilado novamente em 2026-08-10 apenas para a correcao final da curva 2.5x; o Player I/O nao precisava mudar nessa ultima correcao.

Hashes finais atualmente presentes:

| Artefato | SHA-256 |
|---|---|
| `server/tfs.exe` | `299643C780EABB1EA82D92130B042BB8CA8F25E885A72968E5EDE955ED70D987` |
| `sources/nekiro-tfs-1.5-7.72/build-cmake/tfs.exe` | mesmo hash acima |
| `server/player_io_service.exe` | `3A673EA9991B836DCD59797347B04B63D81D6A005EE6097925EF99423EBC7E9D` |
| `sources/nekiro-tfs-1.5-7.72/build-cmake/player_io_service.exe` | mesmo hash acima |

O servidor e o Player I/O estavam parados no momento desta consolidacao.

## 7. Todos os arquivos de produto modificados

Legenda: R = Rooking; A = Alchemy/Professions; G = Gold Converter/origem; D = deployment/build.

### 7.1 Codigo-fonte C++ e build definition

| Escopo | Arquivo | Alteracao relevante |
|---|---|---|
| A | `sources/nekiro-tfs-1.5-7.72/src/CMakeLists.txt` | inclui `professions.cpp` no target |
| A | `sources/nekiro-tfs-1.5-7.72/src/professions.h` | novo dominio, estado e API |
| A | `sources/nekiro-tfs-1.5-7.72/src/professions.cpp` | curva, percent derivado, add/sanitize; rate final 2.5 |
| R,A | `sources/nekiro-tfs-1.5-7.72/src/player.h` | API/flag de rooking e estado/API de Alchemy |
| R,A | `sources/nekiro-tfs-1.5-7.72/src/player.cpp` | 6 -> 7 especial, deteccao/reset de rooking, integracao/reset/envio de Alchemy |
| R,A | `sources/nekiro-tfs-1.5-7.72/src/luascript.h` | declaracoes dos bindings |
| R,A | `sources/nekiro-tfs-1.5-7.72/src/luascript.cpp` | registros e implementacoes dos bindings Lua |
| A | `sources/nekiro-tfs-1.5-7.72/src/protocolgame.h` | declaracao do envio de Professions |
| A | `sources/nekiro-tfs-1.5-7.72/src/protocolgame.cpp` | opcode/payload e envio em login/reconnect |
| R,A | `sources/nekiro-tfs-1.5-7.72/src/iologindata.cpp` | reset persistente do rooking; load/save das colunas de Alchemy |
| G | `sources/nekiro-tfs-1.5-7.72/src/item.h` | constante de `creaturestack` |
| G | `sources/nekiro-tfs-1.5-7.72/src/item.cpp` | atributo excluido da igualdade de stacks |
| G | `sources/nekiro-tfs-1.5-7.72/src/game.cpp` | neutralizacao de origem em merges mistos e dirty marking |

### 7.2 Dados/scripts do source server

| Escopo | Arquivo | Alteracao relevante |
|---|---|---|
| A | `sources/nekiro-tfs-1.5-7.72/schema.sql` | colunas Alchemy em `players` |
| A | `sources/nekiro-tfs-1.5-7.72/data/migrations/38.lua` | nova migration das colunas |
| A | `sources/nekiro-tfs-1.5-7.72/data/migrations/39.lua` | nova migration de rescale para 2.5x |
| R | `sources/nekiro-tfs-1.5-7.72/data/creaturescripts/scripts/droploot.lua` | remove todos os slots quando a morte vai rookar |
| G | `sources/nekiro-tfs-1.5-7.72/data/items/items.otb` | adiciona server ID 26378 -> client ID 5095 |
| G | `sources/nekiro-tfs-1.5-7.72/data/items/items.xml` | nome, peso, charges e showcharges do item 26378 |
| G | `sources/nekiro-tfs-1.5-7.72/data/lib/core/container.lua` | marca gold de creature loot e propaga origem |
| G | `sources/nekiro-tfs-1.5-7.72/data/scripts/eventcallbacks/monster/default_onDropLoot.lua` | passa `true` para creature loot |
| A,G | `sources/nekiro-tfs-1.5-7.72/data/scripts/actions/tools/gold_converter.lua` | conversao, chance, training, efeitos e charges |
| G | `sources/nekiro-tfs-1.5-7.72/data/scripts/eventcallbacks/player/default_onLook.lua` | origem visivel somente para GM/GOD |

### 7.3 Copias runtime em `server`

| Escopo | Arquivo |
|---|---|
| A | `server/schema.sql` |
| A | `server/data/migrations/38.lua` |
| A | `server/data/migrations/39.lua` |
| R | `server/data/creaturescripts/scripts/droploot.lua` |
| G | `server/data/items/items.otb` |
| G | `server/data/items/items.xml` |
| G | `server/data/lib/core/container.lua` |
| G | `server/data/scripts/eventcallbacks/monster/default_onDropLoot.lua` |
| A,G | `server/data/scripts/actions/tools/gold_converter.lua` |
| G | `server/data/scripts/eventcallbacks/player/default_onLook.lua` |

Observacao de sincronizacao: migrations 38/39, `items.otb`, `container.lua`, `gold_converter.lua` e `default_onLook.lua` estavam byte a byte identicos entre source/runtime na auditoria. `schema.sql`, `items.xml`, `default_onDropLoot.lua` e `droploot.lua` possuem divergencias preexistentes fora dos hunks desta frente, embora as alteracoes relevantes acima estejam presentes nos dois lados.

### 7.4 OTClient e assets

| Escopo | Arquivo | Alteracao relevante |
|---|---|---|
| A | `sources/otclient-redemption/modules/gamelib/const.lua` | `ExtendedIds.Professions = 11` |
| A | `sources/otclient-redemption/modules/game_skills/skills.lua` | handler, cache, lifecycle e UI de Alchemy |
| A | `sources/otclient-redemption/modules/game_skills/skills.otui` | linha/barra e toggle Alchemy |
| G | `sources/otclient-redemption/data/things/772/Tibia.dat` | client item 5095 |
| G | `sources/otclient-redemption/data/things/772/Tibia.spr` | sprite 16115 |
| G | `Gold Converter.png` | PNG de entrada fornecido pelo usuario; mantido no workspace |

Hashes dos assets finais:

| Arquivo | SHA-256 |
|---|---|
| `Gold Converter.png` | `210A007708BA65542784BDAC0563C8073E88C1A410F68A4ADC8AC0935BDDE952` |
| `Tibia.dat` | `0940A016A2BD8D60642016C661302B2E856898578D9DF4F340572A55AA3B93C0` |
| `Tibia.spr` | `78807D1A864A99CC66FB96AFEB19065AD3B0410424820606E85E75DD4FD04FAB` |
| runtime/source `items.otb` | `B8CB945B161927DDC8DA4994426E37857B5CB4C569FEA203E546512B56C8A2B6` |

### 7.5 Binarios reconstruidos/substituidos

| Escopo | Arquivo |
|---|---|
| D | `sources/nekiro-tfs-1.5-7.72/build-cmake/tfs.exe` |
| D | `sources/nekiro-tfs-1.5-7.72/build-cmake/player_io_service.exe` |
| D | `server/tfs.exe` |
| D | `server/player_io_service.exe` |

Esses binarios sao artefatos de validacao/deployment. Uma implementacao paralela deve portar os fontes e reconstruir; nao deve usar os `.exe` como substituto do merge.

### 7.6 Documento criado nesta consolidacao

- `docs/HANDOFF_TECNICO_ROOKING_PROFESSIONS_ALCHEMY_2026-08-10.md`.

## 8. Arquivos consultados, mas nao modificados por estas etapas

- `server/data/creaturescripts/scripts/firstitems.lua`: fonte autoritativa dos starter items reutilizada via `lastlogin=0`.
- `sources/otclient-redemption/modules/game_shop/serverSIDE/data/scripts/game_shop.lua`: ja referenciava item 26378; nao foi alterado.
- `sources/nekiro-tfs-1.5-7.72/schema_bestiary.sql`: consultado para tabelas de progresso/charm; nao alterado.
- arquivos de vocation/town/map usados para confirmar IDs e ganhos; nao receberam mudancas nesta frente.

## 9. Backups criados

### Rooking

- `D:\tibia-oldschool\backups\rooking-20260809-implementation`
- `D:\tibia-oldschool\backups\rooking-full-reset-20260809`
- `D:\tibia-oldschool\backups\rooking-keep-death-history-20260809`

O ultimo backup registra especificamente a versao anterior que ainda apagava death history, antes da preservacao de `player_deaths`.

### Professions/Alchemy/Gold Converter

- `C:\Users\guisu\.codex\backups\tibia-oldschool\professions-alchemy-20260809`
- `C:\Users\guisu\.codex\backups\tibia-oldschool\professions-alchemy-clean-rebuild-20260809`
- `C:\Users\guisu\.codex\backups\tibia-oldschool\gold-converter-assets-20260809`
- `C:\Users\guisu\.codex\backups\tibia-oldschool\gold-converter-adjustments-20260810`
- `C:\Users\guisu\.codex\backups\tibia-oldschool\alchemy-skill-rate-correction-20260810`

O backup de clean rebuild tambem preserva o build stale associado ao crash/heap corruption para analise forense.

## 10. Alteracoes operacionais fora de arquivos

O personagem de teste chamado `Druid` foi alterado diretamente no banco em diferentes momentos para:

- level 9, vocation Druid, town Thais e experience/HP/mana coerentes para teste;
- inspecao do retorno 9/8 apos morte;
- novo preparo em level 9 depois de um rooking.

Esses valores eram fixture operacional temporaria, nao migration nem parte do produto. O personagem foi usado em testes depois das alteracoes; portanto, nao assumir que o estado atual da row ainda corresponde ao ultimo preparo sem consultar o banco com o personagem offline.

As migrations 38/39 alteram schema/dados de `players`. A implementacao paralela deve confirmar o valor atual de `server_config.db_version` no seu proprio banco antes de reaplicar.

## 11. Validacao realizada e limites

### 11.1 Rooking

- Build C++ concluido e binario implantado.
- Smoke startup registrado com Player I/O ativo ate `Tibia Oldschool 7.72 Test Server Online!`.
- O usuario testou morte e confirmou o comportamento de level/mana intermediario.
- O usuario disparou rooking e confirmou que a primeira versao estava quase correta; o defeito observado era preservacao de itens/estado.
- Depois desse feedback, foi implementado o full reset de inventario/depot/tabelas e o reaproveitamento de starter items.
- O historico de mortes foi removido da lista de reset depois de decisao explicita.
- A estrutura do loop de level-up foi verificada para 6 -> 9 e para nao afetar Knight/Paladin.

Limite: nao ha neste workspace uma suite automatizada permanente cobrindo os cinco cenarios de gameplay. Nao foi registrada uma confirmacao manual final, posterior ao ultimo full-reset patch, cobrindo simultaneamente inventario, todos os depots e todos os estados persistentes. Essa deve ser a principal regressao end-to-end da implementacao paralela.

### 11.2 Professions/Alchemy/Gold Converter

- Compilacao completa final concluida.
- `tfs.exe` de build e runtime tem hash identico.
- `player_io_service.exe` de build e runtime tem hash identico.
- Login crash foi tratado com clean rebuild e deployment do par coerente.
- Duplicatas principais de runtime/source foram comparadas; divergencias de arquivo inteiro foram identificadas em vez de ocultadas.
- Assets DAT/SPR/OTB foram inspecionados durante a implementacao e hashes finais foram registrados.
- Curva final 2.5x foi recalculada; chance do conversor permaneceu inalterada.
- Fluxos de success/fail, consumo de charge, origem, merge misto e remocao do custom attribute no platinum foram auditados no codigo.

Limites:

- nao ha teste automatizado permanente para RNG/charges/efeitos/progresso;
- migration 39 precisa ser executada no ambiente paralelo e validada com dados reais;
- o client precisa usar exatamente o DAT/SPR correspondentes ao servidor/OTB;
- o servidor estava parado ao final desta consolidacao.

## 12. Riscos e pendencias para a implementacao paralela

1. **Descricao divergente na game shop.** `game_shop.lua` ainda diz que o Gold Converter tambem converte 100 platinum em crystal. O action atual aceita somente 100 gold -> 1 platinum. Esse arquivo nao foi alterado; decidir se a descricao deve ser corrigida ou se a segunda conversao deve existir.
2. **Rooking ainda nao equivale a delete/recreate em relacoes externas.** Guild membership, house ownership, market offers e namelock nao sao apagados. Decidir explicitamente antes de ampliar o reset.
3. **Historicos.** `player_deaths` deve continuar preservado. Provavelmente `market_history` e outros dados antiabuso tambem devem ser tratados como historicos, nao como estado jogavel.
4. **Falha parcial de save no rooking.** O reset envolve o UPDATE principal e multiplos DELETE/INSERT dentro da transacao existente. A IA paralela deve confirmar os limites transacionais do seu branch e comportamento em falha/rollback.
5. **Itens nao carregados.** A implementacao forca DELETE de depot locker/chest e tabelas de inbox durante `rookgaardResetPending`, cobrindo dados nao carregados. Qualquer nova tabela futura de item do player precisara entrar nessa lista.
6. **Custom attribute e merges.** Nao remover a neutralizacao em `game.cpp`. Ignorar `creaturestack` em `Item::equals` sem limpar merges mistos permitiria contaminacao de origem; impedir merge, por outro lado, degradaria a experiencia normal de stacking.
7. **`transform` preserva instancia.** Manter a remocao explicita de `creaturestack` apos transformar em platinum, ou substituir por remove+create com rollback robusto.
8. **Opcode 11.** Confirmar que o branch paralelo nao ocupou `ExtendedIds.Professions = 11` com outro recurso.
9. **Persistencia no Dispatcher.** As colunas nao adicionam statements, mas o checkpoint player+floor continua fazendo o save central sincrono no Dispatcher. Nao documentar esse checkpoint como assincrono.
10. **Build coherence.** Mudancas em `player.h`, query projection ou layouts compartilhados exigem rebuild limpo de TFS e Player I/O. O crash observado mostrou que misturar binarios stale e novos nao e seguro.
11. **Dual tree.** Nao atualizar apenas `sources/.../data` ou apenas `server/data`.
12. **Assets HD.** Nenhum HD asset foi criado por decisao atual. Nao inventar fallback HD sem novo asset/aprovacao.

## 13. Checklist minimo de regressao recomendado

### Rooking

- Mage 8 -> 7 -> 6 -> 7 -> 8 termina com 35 mana no level 8.
- Mage 6 -> 9 em um ganho usa apenas 6 -> 7 como no vocation.
- Knight e Paladin mantem ganhos anteriores em todas as fronteiras.
- Vocationado cuja morte cruza 6 -> 5 sofre rooking; no vocation nao.
- Depois de salvar/relogar: level 1, exp 0, vocation 0, town 11, temple position, HP 150, mana 0, cap 400, skills 10/0, magic 0/0, Alchemy 10/0.
- Inventario, depot locker, depot chest, inbox e store inbox antigos ausentes.
- Starter items entregues uma unica vez pelo `firstitems.lua`.
- Storage, spells, bestiary e charms vazios.
- `player_deaths` anterior e a morte de rooking continuam consultaveis.
- Verificar explicitamente guild/house/market conforme a decisao de produto.

### Alchemy/Gold Converter

- Login/reconnect recebe `1|alchemy|level|percent` no opcode 11.
- Logout assincrono, logout dirty/sincrono e checkpoint preservam level/tries.
- 100 common gold pode converter, mas nunca ganha try.
- 100 creature gold bem-sucedido converte e ganha exatamente 1 try.
- Falha consome uma charge, preserva gold e mostra POFF.
- Sucesso consome uma charge e mostra MAGIC_BLUE sobre o player.
- Target com 99 gold, platinum ou outro item nao consome charge.
- Ultima charge remove o conversor.
- Platinum final nunca possui `creaturestack`.
- Merge creature+common resulta common nos dois sentidos de movimentacao/add.
- Jogador/CM nao ve origem; somente GM e GOD veem.
- Level 10 requer 20 sucessos treinaveis, com chance ainda em 10% por tentativa.
- Migration 39 preserva aproximadamente o percentual de progresso anterior e nunca deixa tries >= required.

## 14. Ordem sugerida de leitura/porte pela outra IA

1. `src/professions.h/.cpp`.
2. Hunks de `src/player.h/.cpp` para Alchemy e Rooking.
3. Hunks de `src/iologindata.cpp`, com atencao ao save transacional e ao checkpoint sincrono.
4. Bindings em `src/luascript.h/.cpp`.
5. Protocolo em `src/protocolgame.h/.cpp` e opcode no cliente.
6. `container.lua`, `default_onDropLoot.lua`, `item.h/.cpp` e `game.cpp` como uma unidade indivisivel de origem/merge.
7. `gold_converter.lua` e `default_onLook.lua`.
8. Migrations/schema.
9. OTB/XML/DAT/SPR.
10. Rebuild limpo de ambos os executaveis e execucao integral do checklist.
