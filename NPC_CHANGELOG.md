# NPC_CHANGELOG

Registro cronologico das alteracoes, testes, decisoes e correcoes relacionadas
a NPCs em `D:\tibia-oldschool`.

Este documento possui uma versao equivalente em ingles:
`NPC_CHANGELOG_EN.md`.

## Atualizacao De 21 De Junho De 2026

### Escopo

Esta atualizacao registra o trabalho iniciado na loja do NPC Xodet e as
correcoes relacionadas a fluids que foram necessarias ate a validacao final em
jogo.

O objetivo foi manter a arquitetura atual do TFS 1.5 e do OTClient, sem portar
o sistema antigo do RealOTX e sem adicionar uma camada de compatibilidade.

### NPC Xodet

O Xodet foi implementado como comerciante simples usando o sistema atual:

- XML com `script="default.lua"`;
- `module_shop=1`;
- nenhum script Lua exclusivo;
- nenhuma venda de runas prontas;
- venda apenas de `blank rune` entre os itens de runa;
- nenhuma venda de wand ou rod;
- nenhuma venda de Amulet of Loss.

Arquivo principal:

- `server/data/npc/Xodet.xml`

Spawn:

- arquivo: `server/data/world/world-spawn.xml`;
- posicao final: `{x = 32397, y = 32222, z = 7}`;
- cidade: Thais.

Itens disponiveis na loja:

| Item | Item ID | Preco | Subtype |
| --- | ---: | ---: | ---: |
| blank rune | 2260 | 10 gp | - |
| life fluid | 2006 | 50 gp | 10 |
| magic lightwand | 2163 | 400 gp | - |
| mana fluid | 2006 | 40 gp | 7 |
| spellbook | 2175 | 150 gp | - |

Os nomes reais foram informados explicitamente nos parametros de `life fluid`
e `mana fluid`. Isso impede que os dois produtos sejam exibidos apenas como
`vial` na janela de comercio.

### Subtypes De Mana Fluid E Life Fluid

O formato correto da shop foi confirmado no parser existente em:

- `server/data/npc/lib/npcsystem/modules.lua`

O formato usado para itens com subtype ficou:

`name,itemid,cost,subType,realName`

Valores adotados no servidor:

- `mana fluid`: subtype `7`;
- `life fluid`: subtype `10`.

Esses valores correspondem a `FLUID_MANA` e `FLUID_LIFE` na base atual. Nao
foram copiados diretamente do RealOTX.

### Correcao Visual Da Shop Legada

Mesmo com nomes, precos e itens recebidos corretos, a janela legada da shop
mostrava as cores erradas para os fluids.

Arquivo alterado:

- `sources/otclient-redemption/modules/game_npctrade/controllers/npc_legacy_ui.lua`

Funcoes envolvidas:

- `onOpenNpcTrade(items)`;
- `refreshTradeItems()`.

Solucao final:

- a correcao e limitada ao client version `772`;
- apenas itens reconhecidos como fluid containers sao considerados;
- apenas os nomes `mana fluid` e `life fluid` recebem o tratamento;
- um clone do item e criado em `newItem.displayPtr`;
- o clone recebe o valor visual correto para representar o fluid;
- `refreshTradeItems()` usa `item.displayPtr or item.ptr` somente para desenhar
  o item na shop.

A compra continua usando:

- `selectedItem.ptr`;
- `g_game.buyItem(...)`.

Portanto, a correcao visual nao altera o item real enviado ao servidor, seu
preco ou seu subtype de compra.

### Dessincronizacao Visual Em Containers

Durante os testes com mana fluid e life fluid, foi identificado um problema
mais amplo em backpacks:

- itens mudavam visualmente de slot;
- o fluid usado podia parecer continuar cheio;
- outro fluid podia aparecer vazio no lugar dele;
- alguns itens deixavam de responder ate o relog;
- o relog reconstruia corretamente o estado do container.

A causa nao estava no NPC, no item `2006`, no protocolo ou nas sprites. A
ordenacao automatica reorganizava apenas os widgets visuais do container,
enquanto os updates incrementais continuavam usando os slots reais enviados
pelo servidor.

Arquivo alterado:

- `sources/otclient-redemption/modules/game_containers/containers.lua`

Estruturas e funcoes relevantes:

- `automaticContainerSortingEnabled = false`;
- `enforceManualContainerOrder()`;
- `init()`;
- `sortContainerItems(container, sortMode)`;
- `onContainersMenuAction(actionId)`;
- `onContainerOpen(container, previousContainer)`;
- `toggleContainerPages(containerWindow, pages)`.

Comportamento final:

- ordenacao automatica desativada no source;
- `useManualSortMode` forcado para `1`;
- `currentSortMode` forcado para `none`;
- `sortContainersFirst` forcado para `0`;
- `sortNestedContainers` forcado para `0`;
- configuracoes antigas sao migradas durante `init()`;
- usuario novo inicia sem ordenacao automatica;
- acoes do menu nao podem reativar a ordenacao;
- o botao `contextMenuButton` de ordenacao fica oculto;
- os botoes restantes foram reposicionados para nao deixar um espaco vazio.

A configuracao somente e gravada quando algum desses valores precisa ser
corrigido.

### Decisoes Sobre O Patch De Containers

Nao foi aplicada uma correcao de protocolo nem um refresh completo para cada
update incremental.

Em especial:

- `onContainerUpdateItem(...)` continua atualizando diretamente o slot
  informado pelo servidor;
- `refreshContainerItems(container)` continua sendo usado no fluxo normal de
  mudanca de tamanho do container;
- nao foi adicionada conversao especial de fluid no protocolo;
- nao houve alteracao nos arquivos C++ do TFS ou do OTClient.

A solucao adotada remove a causa da dessincronizacao em vez de mascarar o
problema com atualizacoes completas de container.

### Erro Ao Derramar Fluid

Depois da correcao dos containers, foi encontrado o seguinte erro ao usar um
fluid em um piso ou objeto comum:

`data/global.lua:101: bad argument #1 to 'pairs' (table expected, got nil)`

A origem era este fluxo em `fluids.lua`:

- o script chamava `table.contains(distillery, target.itemid)`;
- a tabela `distillery` nunca era declarada;
- qualquer alvo comum que nao fosse jogador, fluid container ou fonte de
  fluid podia chegar a esse trecho;
- `table.contains` recebia `nil` e falhava dentro de `pairs`.

Arquivos corrigidos:

- `server/data/actions/scripts/other/fluids.lua`;
- `sources/nekiro-tfs-1.5-7.72/data/actions/scripts/other/fluids.lua`.

A correcao removeu apenas o ramo legado de destilaria. Os itens e a mecanica
desse ramo nao existem na datapack atual, portanto ele era codigo morto e
inaplicavel ao projeto.

Comportamento preservado:

- beber mana fluid;
- beber life fluid;
- transformar o recipiente usado em vial vazio;
- transferir fluid entre recipientes;
- encher um recipiente em uma fonte de fluid;
- derramar fluid no piso ou em um objeto comum;
- criar o splash correspondente;
- informar `It is empty.` quando um recipiente vazio e usado em alvo comum.

As copias de runtime e source de `fluids.lua` ficaram byte a byte identicas.

### Validacoes Realizadas

- Xodet abriu a interface grafica de comercio.
- Os nomes `mana fluid` e `life fluid` foram exibidos corretamente.
- Os precos finais foram validados: `40 gp` e `50 gp`.
- Os itens comprados chegaram na backpack com fluid e cor corretos.
- A previa visual da shop legada passou a distinguir mana fluid e life fluid.
- A ordenacao automatica foi desativada e o comportamento dos containers foi
  validado em jogo.
- O botao de ordenacao deixou de aparecer.
- Configuracoes antigas com `sortAscByName` sao neutralizadas no startup.
- O TFS carregou os scripts e chegou a
  `Tibia Oldschool 7.72 Test Server Online!`.
- Foi validado em jogo que o fluid pode ser derramado no chao sem erro Lua.

### Persistencia Em Source E Distribuicao

As correcoes permanentes estao nos arquivos de source:

- `sources/otclient-redemption/modules/game_npctrade/controllers/npc_legacy_ui.lua`;
- `sources/otclient-redemption/modules/game_containers/containers.lua`;
- `sources/nekiro-tfs-1.5-7.72/data/actions/scripts/other/fluids.lua`.

O runtime do servidor tambem foi atualizado:

- `server/data/actions/scripts/other/fluids.lua`.

Um rebuild do executavel do OTClient preserva as alteracoes porque os modulos
Lua permanecem na arvore de source e fazem parte da distribuicao do client.

Nao existe uma segunda copia desses modulos em pasta de build que precise ser
sincronizada. Uma distribuicao nova deve continuar incluindo `modules`,
`data`, `mods` e `init.lua`.

A alteracao feita no `config.otml` do usuario atual serviu apenas para o teste
local. A garantia para usuarios novos e configuracoes antigas esta no codigo
de `containers.lua`.

### Limites Mantidos

- Nao houve alteracao no NpcSystem.
- Nao houve alteracao em `modules.lua`.
- Nao houve alteracao no protocolo.
- Nao houve alteracao em DAT, SPR ou CWM.
- Nao houve alteracao nas sprites.
- Nao foi criada camada de compatibilidade com RealOTX.
- Nao foi portado codigo de NPC do RealOTX.
- Nao foi criado script Lua exclusivo para Xodet.
- Nenhum outro NPC recebeu a correcao visual de forma individual.

## Atualizacao De 22 De Junho De 2026

### Keywords Clicaveis Em Falas De NPC

Foi registrada e consolidada a logica para reutilizar keywords clicaveis nas
falas dos proximos NPCs sem depender do modo legado de chat privado.

Arquivos alterados:

- `sources/otclient-redemption/modules/game_console/console.lua`;
- `sources/otclient-redemption/modules/game_npctrade/controllers/npc_dialog.lua`.

Arquivo documental criado:

- `docs/NPC_KEYWORDS_CLICKAVEIS.md`.

Regra funcional adotada:

- qualquer palavra ou expressao envolvida por `{}` em uma fala de NPC pode ser
  exibida como keyword clicavel no console;
- ao clicar, o client envia a keyword como fala normal do jogador;
- no `7.72`, o envio ocorre via `SAY`, nao via `NpcTo`;
- a deteccao de fala de NPC no fluxo `SAY` usa o speaker real presente no mapa;
- a verificacao final considera `isNpc()`, nome e posicao;
- o texto acima da cabeca continua sendo exibido sem as chaves.

Motivacao tecnica:

- o TFS atual fala com o jogador usando `TALKTYPE_SAY`;
- o fluxo legado `NpcFrom/NpcTo` nao e a base correta do projeto;
- a solucao precisava preservar a arquitetura atual e evitar camada de
  compatibilidade.

Padrao para proximos NPCs:

- marcar apenas as keywords relevantes com `{keyword}`;
- reutilizar a mesma infraestrutura atual do client;
- nao criar protocolo novo para isso;
- nao alterar o NpcSystem para cada NPC individualmente.

Validacao executada:

- o client iniciou sem erro Lua apos a correcao final;
- a deteccao anterior baseada em `g_creatures` foi descartada porque esse
  global nao existe nesse runtime Lua;
- a versao final passou a usar espectadores do `g_map` para confirmar o NPC no
  fluxo `SAY`.

## Atualizacao De 22 De Junho De 2026 - Keywords Clicaveis

Foi iniciado o trabalho de marcar falas de NPC com `{keyword}` para ativar
links clicaveis no client.

Arquivos ajustados nesta etapa:

- `server/data/npc/Benjamin.xml`;
- `server/data/npc/Captain.xml`;
- `server/data/npc/Gamon.xml`;
- `server/data/npc/Luna.xml`;
- `server/data/npc/Quentin.xml`;
- `server/data/npc/Quero.xml`;
- `server/data/npc/Suzy.xml`;
- `server/data/npc/Wyat.xml`;
- `server/data/npc/Lynda.xml`;
- `server/data/npc/Oswald.xml`;
- `server/data/npc/scripts/Lynda.lua`;
- `server/data/npc/scripts/Quentin.lua`.

Regra aplicada:

- apenas palavras ou expressoes relevantes receberam `{}` no texto final da
  fala;
- nomes tecnicos, caminhos e identificadores nao foram alterados;
- a ideia e reutilizar a mesma logica de keywords clicaveis nos proximos NPCs.
