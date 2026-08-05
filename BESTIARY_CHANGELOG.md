# BESTIARY_CHANGELOG

Registro cronologico das alteracoes, testes, decisoes, reversoes e pendencias do projeto de Bestiary em `D:\tibia-oldschool`.

## Resumo Executivo

O desenvolvimento do Bestiary partiu de uma base parcial ja existente no client, mas sem um fluxo totalmente compativel no servidor e sem persistencia adequada no banco. O trabalho avancou por etapas, com foco inicial em reaproveitamento de protocolo e interface, seguido por modelagem de dados, kill tracking, adaptacoes de parse, correcoes visuais, segmentacao entre criaturas comuns e bosses, e preparacao da interface de charms para uma logica inteiramente controlada pelo servidor.

Ao longo do projeto, foram consolidados os seguintes principios:

- o servidor e a fonte de verdade para kills, thresholds, estagios e charm points;
- o client recebe dados prontos e apenas interpreta/renderiza o estado enviado;
- o progresso persistido relevante e o numero de kills por jogador e criatura;
- estagio e charm points sao valores derivados, nao a principal referencia persistida;
- a interface da Cyclopedia foi reduzida para o escopo necessario ao client antigo;
- compatibilidade com o OTClient usado no projeto teve prioridade sobre paridade com implementacoes modernas.

O fluxo implementado nesta etapa cobre:

1. cadastro e persistencia das criaturas do Bestiary;
2. contagem de kills por jogador;
3. derivacao de estagio em tempo real;
4. envio de overview e detalhe pelo protocolo;
5. desbloqueio progressivo de visualizacao e loot;
6. separacao entre `Creatures` e `Bosses`;
7. exibicao de charm points calculados no servidor;
8. habilitacao da aba `Charms` no modo reduzido da Cyclopedia.

## Fase 1. Levantamento Tecnico E Reaproveitamento Da Base

### Escopo inicial

- Foi feita uma analise para identificar o que ja existia no TFS e no OTClient em relacao a Bestiary, Cyclopedia e Charms.
- O objetivo inicial nao era replicar a Cyclopedia completa, mas construir um Bestiary funcional dentro das limitacoes da base atual.
- Tambem foi definido que o fluxo deveria priorizar consulta e progresso, deixando a ativacao detalhada de charms para uma fase posterior.

### Ferramentas e arquivos inspecionados

- Servidor:
  - `sources/nekiro-tfs-1.5-7.72/src/protocolgame.cpp`
  - `sources/nekiro-tfs-1.5-7.72/src/protocolgame.h`
  - `sources/nekiro-tfs-1.5-7.72/src/game.cpp`
  - `sources/nekiro-tfs-1.5-7.72/src/game.h`
- Client:
  - `sources/otclient-redemption/src/client/protocolgameparse.cpp`
  - `sources/otclient-redemption/src/client/protocolgamesend.cpp`
  - `sources/otclient-redemption/src/client/staticdata.h`
  - `sources/otclient-redemption/modules/game_cyclopedia/game_cyclopedia.lua`
  - `sources/otclient-redemption/modules/game_cyclopedia/game_cyclopedia.otui`
  - `sources/otclient-redemption/modules/game_cyclopedia/tab/bestiary/bestiary.lua`
  - `sources/otclient-redemption/modules/game_cyclopedia/tab/bestiary/bestiary.otui`
  - `sources/otclient-redemption/modules/game_cyclopedia/tab/charms/charms.lua`
  - `sources/otclient-redemption/modules/game_cyclopedia/tab/charms/charms.otui`
- Busca textual com `rg`.

### Conclusoes iniciais

- O client ja possuia estrutura de parse e interface para Bestiary, mesmo que parte dela estivesse oculta ou incompleta para a versao usada.
- O servidor possuia pontos de extensao suficientes para montar o fluxo de envio.
- A implementacao deveria ser guiada pelo protocolo e pela compatibilidade real do client, e nao apenas por leitura de nomes de funcoes.

### Decisoes tomadas

- O projeto seria implementado em camadas, primeiro com persistencia e kill tracking, depois protocolo, depois UI.
- O fluxo de charms nao ficaria dependente de interacao direta do client para validacao de pontos.
- O banco nao armazenaria um saldo agregado definitivo de charm points; o total seria calculado pelo servidor a partir do progresso.

## Fase 2. Modelagem De Dados E Estrutura Inicial Do Bestiary

### Tabelas introduzidas

- `bestiary_monsters`
- `player_bestiary_progress`

### Objetivos da modelagem

- Manter um cadastro central de criaturas do Bestiary com thresholds e recompensa.
- Separar configuracao de criatura do progresso individual do jogador.
- Permitir ajustes futuros de thresholds sem depender de estrutura acoplada ao client.

### Estrutura consolidada

`bestiary_monsters` passou a armazenar:

- `creature_id`
- `name`
- `kills_stage_1`
- `kills_stage_2`
- `kills_stage_3`
- `charm_points`
- `enabled`

`player_bestiary_progress` passou a armazenar:

- `player_id`
- `creature_id`
- `kills`
- `last_stage_reached`
- `created_at`
- `updated_at`

### Ferramentas usadas

- SQL em `server/schema_bestiary.sql`
- comandos diretos em MariaDB para carga e ajustes de teste

### Decisoes de modelagem

- `creature_id` foi adotado como identificador interno do Bestiary, independente de sprite `Client ID`.
- A lista inicial de criaturas foi organizada em ordem alfabetica, com espaco para inserir novas entradas depois sem exigir renumeracao global.
- A configuracao de thresholds ficou no banco, e nao hardcoded no client.
- Algumas entradas customizadas ou tecnicas foram mantidas na base, mas desabilitadas quando necessario.

### Ajustes posteriores

- O conjunto inicial de thresholds `25 / 250 / 1000` foi substituido por `100 / 1000 / 2500`.
- Criaturas de teste receberam configuracoes especificas, como `dragon` com `charm_points = 10`.
- Entradas indevidas, variantes de raid, clones tecnicos e criaturas sem suporte visual confiavel foram filtradas ou desabilitadas ao longo do processo.

## Fase 3. Kill Tracking, Thresholds E TalkActions De Diagnostico

### Implementacao inicial

- O hook de kill do servidor passou a registrar progresso por jogador na tabela `player_bestiary_progress`.
- A atualizacao foi vinculada ao monstro morto e ao `creature_id` correspondente do Bestiary.

### Debate de estrategia

Durante a fase inicial, foi avaliado se o servidor deveria:

- validar thresholds a cada kill;
- validar em intervalos;
- ou deixar a verificacao apenas para abertura da interface.

### Logica adotada

- As kills sao sempre persistidas no momento da morte.
- A promocao de estagio nao depende apenas do client abrir a interface.
- A verificacao passou a respeitar a ideia de thresholds relevantes, evitando checagens desnecessarias apos o ultimo estagio.

### Problemas encontrados

- Em testes iniciais, `last_stage_reached` apresentou valores incorretos ou inconsistentes.
- Houve um caso visivel em que o campo alternava entre valores improprios como `49` e `1`.
- Isso mostrou que o estado persistido de estagio nao poderia ser tratado como referencia absoluta para exibicao.

### Ferramenta temporaria de apoio

- Foi criada uma TalkAction de diagnostico `!bestiary <monster name>`.
- Essa TalkAction permitia confirmar:
  - `creature_id`
  - kills atuais
  - thresholds configurados
  - estagio persistido
  - reward/charm points

### Decisao consolidada

- `kills` passou a ser o dado persistido realmente relevante.
- `last_stage_reached` foi mantido apenas como dado auxiliar.
- O estagio exibido e as recompensas deveriam ser recalculados com base nas kills e nos thresholds atuais.

## Fase 4. Primeiro Fluxo De Protocolo Do Bestiary

### Objetivo

- Ligar banco e kill tracking ao client usando o fluxo de Bestiary ja existente no OTClient.

### Funcoes centrais ajustadas no servidor

- `sendBestiaryRaces()`
- `sendBestiaryOverview(...)`
- `sendBestiaryMonsterData(...)`
- `sendBestiaryCharmsData()`

### Parse ja existente no client

- `GameServerBestiaryRaces`
- `GameServerBestiaryOverview`
- `GameServerBestiaryMonsterData`
- `GameServerBestiaryCharmsData`

### Dados enviados no overview

- `creature_id`
- progresso atual
- nome da criatura
- outfit para renderizacao

### Dados enviados no detalhe

- kills
- thresholds
- classe/dificuldade
- loot
- informacoes textuais e complementares

### Decisao de protocolo

- O servidor deveria respeitar rigorosamente a ordem e a presenca condicional de campos esperados pelo client 7.72/OTClient usado no projeto.
- A compatibilidade foi tratada como parte funcional do sistema, nao apenas como detalhe de parse.

## Fase 5. Correcao De Parse E Semantica De Progresso

### Erro critico observado

- Ao abrir o Bestiary ou entrar na aba de criaturas, o client chegou a disparar erro de parse:
  - `InputMessage eof reached`

### Causa

- O payload enviado pelo servidor nao correspondia exatamente ao que o parse do client esperava quando a criatura ainda estava oculta ou sem progresso revelado.

### Correcao aplicada

- O envio do byte de ocorrencia foi ajustado para acontecer apenas quando `currentLevel > 0`.

### Impacto

- O parse voltou a alinhar leitura de overview e detalhe.
- O client deixou de quebrar ao abrir a janela com criaturas ainda nao descobertas.

### Logica consolidada de revelacao

- `0 kills` -> criatura oculta
- `1+ kill` -> criatura revelada
- thresholds posteriores controlam liberacao de loot por faixa de raridade e demais estados de progressao

### Reinterpretacao de estagios

- A primeira kill passou a ser tratada como revelacao visual e nao como estagio de threshold finalizado.
- O fluxo de desbloqueio de loot foi ajustado em torno dessa semantica, separando descoberta de conclusao de stage formal.

## Fase 6. Depuracao De Sprites, Outfits E ThingTypes

### Sintomas observados

- Criaturas validas sem sprite no Bestiary.
- Nomes com capitalizacao incorreta, por exemplo letras maiusculas internas indevidas.
- Erros em Lua e no client ao tentar renderizar outfits invalidos.
- Casos em que a criatura aparecia no jogo normal, mas nao no Bestiary.

### Diagnostico realizado

Foram comparados:

- o outfit enviado pelo fluxo normal de criaturas no mapa;
- o outfit enviado especificamente pelo Bestiary;
- o `lookType` originado do `MonsterType`;
- o objeto `Outfit` montado pelo parse do client;
- o ponto de chamada de `widget.Sprite:setOutfit(...)` em `bestiary.lua`.

### Descobertas relevantes

- Algumas criaturas passavam a renderizar no Bestiary somente depois de aparecerem no mapa.
- Isso indicou dependencia do carregamento de `ThingType` ou disponibilidade de outfit no client.
- Houve casos de `outfit.type == 0`, que naturalmente quebravam o fluxo grafico.
- O problema nao se limitava ao `Elf Scout`; outras entradas tambem possuíam inconsistencias.

### Ferramentas usadas

- Logs temporarios no servidor e no client.
- Validacoes temporarias em `protocolgame.cpp`, `protocolgameparse.cpp` e `bestiary.lua`.
- Comparacao entre nomes cadastrados e nomes reais dos monstros.

### Correcoes aplicadas

- Ajuste de nomes exibidos.
- Desabilitacao ou limpeza de entradas indevidas.
- Validacao defensiva antes de `setOutfit(...)` no Lua.
- Ajustes pontuais/fallbacks durante o diagnostico, removidos ou reduzidos depois que o fluxo principal foi estabilizado.

### Reversoes e conclusoes

- Fallbacks excessivamente simplificados que zeravam estrutura de outfit foram descartados.
- Foi mantido o principio de preservar a estrutura original do outfit sempre que possivel.
- O Bestiary passou a depender do mesmo tipo de outfit valido que o client ja aceita no fluxo normal de criatura.

## Fase 7. Tratamento De Nomes, Lista De Criaturas E Entradas Invalidas

### Problemas observados

- Inclusao de entradas que nao deveriam fazer parte do Bestiary jogavel.
- Variantes de raid, clones tecnicos e criaturas experimentais apareciam junto com a lista real.
- Algumas criaturas legitimas haviam sido removidas indevidamente durante a limpeza inicial.

### Diretrizes adotadas

- Remover apenas entradas efetivamente invalidas, tecnicas ou nao desejadas.
- Recolocar bosses e criaturas reais que tinham sido removidos apenas por falha de sprite ou nomenclatura.
- Tratar exceptions conhecidas sem comprometer a estrutura global da lista.

### Casos tratados

- Remocao de variantes como:
  - `orcraid`
  - `orcwarlordraid`
  - `slime2`
  - `demongoblin`
- Remocao das `butterflys` do Bestiary.
- Reavaliacao de bosses e criaturas especiais como:
  - `Demodras`
  - `Necropharus`
  - `The Horned Fox`
  - `The Old Widow`
  - `Yeti`
  - `Orshabaal`
  - `Elf Scout`

### Logica final dessa etapa

- O fato de um boss usar sprite semelhante a uma criatura comum nao justifica sua exclusao.
- O criterio principal passou a ser: criatura existe no servidor, possui identidade jogavel valida e pode ser suportada pelo fluxo visual do client.

## Fase 8. Loot, Client IDs, Containers E Classificacao De Raridade

### Problema inicial

- O loot do Bestiary aparecia com sprites erradas.

### Causa

- O servidor estava enviando `server item id`.
- O client precisava do `clientId` para renderizar corretamente a sprite do item no OTClient.

### Correcao aplicada

- O envio do loot em `sendBestiaryMonsterData(...)` passou a usar `Item::items[loot.id].clientId`.

### Problema adicional

- Bags e containers definidos no XML apenas como invólucro estavam sendo exibidos como se fossem loot real.

### Decisao de exibicao

- O Bestiary nao deve mostrar o wrapper `bag/container` quando ele serve apenas para agrupar drops internos.
- O loot exibido deve refletir os itens contidos no container e nao o container tecnico em si.

### Classificacao de raridade consolidada

- `chance >= 20000` -> `Common`
- `chance >= 7100` -> `Uncommon`
- `chance >= 2000` -> `Semi-Rare`
- `chance >= 500` -> `Rare`
- `chance >= 100` -> `Very Rare`
- `chance < 100` -> `Extremely Rare`

### Observacoes

- A classificacao foi ajustada para refletir melhor a chance real enviada pelo servidor.
- A definicao de `Extremely Rare` foi adicionada depois, sem alterar as loot tables dos monstros.

## Fase 9. Progresso De Descoberta, Silhueta E Bloqueio De Informacoes

### Objetivo funcional

- Criaturas ainda nao descobertas deveriam aparecer de forma limitada no Bestiary.
- O player so deveria ver informacoes completas apos descobrir ou progredir naquela criatura.

### Regras consolidadas

- Antes da primeira kill:
  - nome exibido como `Unknown Creature`
  - visual oculto/silhueta
  - informacoes detalhadas nao exibidas
- Apos a primeira kill:
  - sprite normal
  - nome real
  - informacoes basicas liberadas

### Regras de loot por progresso

- descoberta sem stage completo:
  - criatura revelada
  - loot mais raro continua oculto
- thresholds seguintes:
  - liberacao gradual por faixa de raridade

### Problemas observados

- A silhueta preta nao funcionou corretamente na primeira tentativa.
- Em outra iteracao, o loot chegou a desaparecer apos a primeira kill.
- O campo `mitigation` podia vir ausente, causando erro em Lua por concatenacao com `nil`.

### Correcoes aplicadas

- Tratamento seguro para `mitigation == nil`.
- Revisao da logica que filtrava loot para nao apagar a lista valida apos descoberta.
- Ajustes na revelacao da criatura apos a primeira kill sem depender do primeiro threshold formal.

### Decisao de interface

- Quando bloqueado, o loot nao deve revelar nome nem sprite real.
- Placeholders devem manter a estrutura da grade, sem entregar informacao visual indevida.
- Para criaturas desconhecidas, o placeholder principal da criatura foi mantido como elemento suficiente, sem excesso de ruido visual.

## Fase 10. Recalculo De Estagio E Charm Points

### Motivo da mudanca

- Ao alterar thresholds ou charm points no banco, o sistema precisava refletir o novo estado sem depender de historico antigo salvo.

### Problema observado

- Um jogador com kills intermediarias poderia permanecer com charm points que so deveriam existir apos completar o stage final antigo.

### Correcao consolidada

- O estagio passou a ser recalculado a partir de `kills` e dos thresholds atuais.
- O total de charm points passou a ser somado em tempo real com base apenas nas criaturas efetivamente completas nas regras vigentes.
- `sendBestiaryCharmsData()` e a logica de progresso foram alinhados a esse principio.

### Consequencias positivas

- Alteracoes futuras de thresholds passam a ser refletidas corretamente.
- Alteracoes futuras de `charm_points` por criatura tambem sao recalculadas.
- O banco nao precisa armazenar um saldo agregado que ficaria sujeito a divergencia.

### Testes e dados auxiliares

- Foram aplicados ajustes temporarios em progresso de personagem para acelerar testes de thresholds altos.
- Exemplo: progressos de `dragon` foram elevados artificialmente para validar transicao proxima ao stage final.

## Fase 11. Segmentacao Entre Creatures E Bosses

### Problema

- Todas as criaturas apareciam em uma unica lista.
- Bosses precisavam sair da lista comum e aparecer em categoria separada.

### Fonte de classificacao

- `data/monster/monsters.xml`
- pasta `data/monster/Bosses`
- informacoes carregadas em `MonsterType`

### Logica adotada

- O servidor passou a classificar bosses principalmente por `monsterType->info.isBoss`.
- Foi mantido fallback por caminho/listagem de XML em casos em que a origem ajudasse a preservar a classificacao.

### Resultado

- `Creatures` ficou restrita a criaturas comuns.
- `Bosses` passou a listar apenas bosses.
- A duplicacao entre abas foi eliminada.

### Ajuste complementar

- A busca textual do Bestiary foi expandida para considerar ambas as abas, e nao apenas a ultima categoria visitada.
- Mesmo criaturas ainda nao descobertas podem ser encontradas por nome, surgindo como `Unknown Creature` quando necessario.

## Fase 12. Ajustes De Interface Da Lista E Navegacao

### Problemas tratados

- botao textual `Back` redundante;
- total incorreto de criaturas na tela inicial;
- contador/paginacao voltando para `1/x` ao sair do detalhe, mesmo permanecendo na pagina anterior;
- necessidade de ocultar criaturas nao descobertas.

### Correcoes aplicadas

- O botao textual `Back` foi removido/ocultado, mantendo apenas a seta ja existente.
- O total da tela inicial deixou de usar valor fixo indevido e passou a refletir a contagem real das criaturas habilitadas.
- O indice da pagina atual foi preservado ao abrir detalhe e retornar.
- Foi criada a opcao `Hide Unknown` para esconder criaturas ainda nao descobertas na aba atual.

### Observacoes de implementacao

- A ocultacao por `Hide Unknown` atua sobre a lista mostrada, sem alterar os dados reais do progresso.
- A busca e a navegacao precisaram conviver com estados diferentes de pagina, filtro e aba ativa.

## Fase 13. Preparacao Da Interface De Charms E Remocao De Elementos Antigos

### Diretriz de projeto

- O projeto deixou de usar charm por criatura como regra principal.
- Isso tornou desnecessaria a exibicao do bloco antigo de selecao de charm na tela de detalhe do Bestiary.

### Ajustes aplicados

- O bloco de `Charm Selection` da area de criatura foi removido/ocultado.
- A area que antes mostrava `gold` no rodape da Cyclopedia reduzida foi reaproveitada visualmente para exibir charm points.
- A logica antiga de gold nao foi apagada, apenas deixada separada para possivel reaproveitamento futuro.

### Decisao tecnica importante

- A exibicao de charm points na UI deve consumir um total calculado no servidor.
- O client nao deve decidir quantos pontos o jogador possui.

### Estado dessa etapa

- O total exibido passou a vir do fluxo do servidor.
- A existencia de mais de um elemento visual com icone de charm exigiu limpeza para evitar duplicidade.

## Fase 14. Habilitacao Da Aba Charms Na Cyclopedia Reduzida

### Objetivo

- Manter o modo reduzido da Cyclopedia, mas sem limitar a navegacao apenas ao Bestiary.
- Expor tambem a aba `Charms` para avaliacao e adaptacao futura.

### Arquivos centrais

- `modules/game_cyclopedia/game_cyclopedia.lua`
- `modules/game_cyclopedia/game_cyclopedia.otui`
- `modules/game_cyclopedia/tab/charms/charms.lua`

### Correcoes visuais e comportamentais

- A aba `Charms` foi habilitada no fluxo reduzido.
- Somente `Bestiary` e `Charms` permaneceram visiveis nesse modo.
- O estado visual de aba ativa/inativa passou a ser controlado explicitamente.
- Os dois botoes passaram a ter largura clicavel cobrindo icone e texto.
- Foram corrigidos problemas em que:
  - apenas o icone era clicavel;
  - o texto sumia no hover;
  - as duas abas pareciam pressionadas ao mesmo tempo;
  - a aba inativa encolhia ou voltava para visual de icone isolado;
  - surgia uma divisao visual entre icone e texto na aba inativa.

### Detalhes de implementacao

- Estados automaticos do `UIButton` foram reduzidos para essas abas especificas.
- O visual passou a depender de overlays e controle via Lua.
- Foi necessario ajustar fontes, clipping e fundos auxiliares para estabilizar o layout no OTClient usado.

### Pendencia funcional

- A aba `Charms` ainda exige revisao de conteudo e possivel simplificacao, porque a regra final de charms do projeto diverge do fluxo classico por criatura.

## Fase 15. Reestruturacao Inicial Da Aba Charms

### Objetivo desta etapa

- Substituir o fluxo antigo de charms por criatura por um layout inicial focado no modelo novo do projeto.
- Preparar a interface para consulta, selecao visual e desbloqueio futuro sem ainda integrar combate, banco ou logica final de aplicacao.

### Decisoes de escopo

- A aba `Charms` passou a usar dados mockados/hardcoded no client para validar o layout.
- O saldo de charm points visivel continuou vindo do servidor, preservando a regra de autoridade do backend.
- O fluxo antigo de `Select Creature` deixou de ter relevancia funcional nesta fase e foi removido da interface.

### Mudancas estruturais na interface

- Foi removido o bloco de selecao de criatura da aba `Charms`, incluindo:
  - rótulo `Select Creature`;
  - lista de criaturas;
  - campo de busca;
  - interacoes ligadas a selecao por criatura.
- O painel esquerdo passou a concentrar:
  - nome do charm;
  - icone/runa selecionada;
  - status;
  - efeito;
  - chance;
  - reducao de dano em area, quando existir;
  - custo de desbloqueio;
  - texto descritivo.
- O painel direito passou a exibir uma grade de charms com quatro colunas visuais.

### Cards de charm

- Cada charm passou a ser exibido em um card proprio com:
  - nome no topo;
  - icone central;
  - custo na base;
  - estado visual distinto para `locked`, `unlocked` e `selected`.
- A selecao de card passou a atualizar o painel esquerdo em tempo real.
- O botao `Unlock` passou a respeitar o estado do charm selecionado e a disponibilidade de pontos.

### Dados mockados criados

- Foram mantidos charms baseados em nomes reais como:
  - `Wound`
  - `Enflame`
  - `Freeze`
  - `Mana Spring`
  - `Savage Blow`
- Depois foram adicionados charms ficticios adicionais (`Charm 2` ate `Charm 11`) para preencher a grade e validar quebra de linha, selecao e scrollbar.
- O `Wound` foi fixado com custo de `600`.

### Ajustes de layout posteriores

- O texto `Lore` foi substituido por `Description`.
- A linha `Cost:` do painel esquerdo foi removida.
- O bloco `Points` foi convertido para `Unlock Cost`, exibindo o custo do charm atualmente selecionado.
- O tamanho total da janela `Cyclopedia` foi aumentado verticalmente para abrir mais espaco util sem alterar a estrutura principal da interface.
- A grade de `Available Charms` recebeu centralizacao visual propria dentro do painel de charms.

### Problemas tecnicos encontrados

- O layout inicial produziu erros por divergencia entre IDs declarados no OTUI e acessos no Lua.
- Houve casos em que widgets existiam, mas estavam aninhados em containers diferentes e eram acessados como se fossem filhos diretos.
- O uso de strings com `%` em contexto inadequado levou ao erro `invalid option '%' to 'format'`.
- O bloco descritivo do charm exigiu varias iteracoes ate que largura, quebra de linha e area rolavel passassem a funcionar corretamente.

### Correcao de contrato OTUI/Lua

- Foi introduzida uma validacao explicita de contrato de UI ao carregar a aba.
- Em vez de deixar o erro aparecer tardiamente em acesso `nil`, a tela passou a exigir a presenca dos widgets esperados no carregamento.
- IDs genericos e ambiguos foram renomeados para IDs especificos do layout de charms.

### Widgets e referencias ajustados

Foram alinhados ou introduzidos IDs como:

- `SelectedCharmIcon`
- `SelectedLockedMask`
- `StatusValue`
- `EffectValue`
- `ChanceValue`
- `AoeValue`
- `UnlockCostValue`
- `HistoryText`, posteriormente substituido por `DescriptionText`
- `CardName`
- `CardIcon`
- `CardCostValue`
- `CardLockedMask`
- `CardStatusBar`
- `CardStatusLabel`

### Saldo de charm points

- O saldo total exibido na aba `Charms` nao ficou preso ao mock.
- A tela voltou a consumir `charmsData.points` e `Cyclopedia.StoredBestiaryCharmPoints`, preservando o calculo real do servidor.
- O desbloqueio local permaneceu apenas como simulacao visual de layout, sem redefinir a regra de autoridade do servidor.

### Relacao com o Bestiary

- A centralizacao da grade nao foi aplicada ao painel inicial do `Bestiary`.
- Posteriormente, ficou definida a seguinte regra:
  - tela principal do `Bestiary` permanece com alinhamento original;
  - listas internas de `Creatures` e `Bosses` podem ser centralizadas dentro do painel da lista;
  - a centralizacao de `Available Charms` permanece restrita ao painel de `Charms`.

### Reversoes e refinamentos

- Houve uma tentativa inicial de centralizar grades do Bestiary de forma ampla demais; essa alteracao foi revertida e restringida aos contextos corretos.
- Tambem houve tentativas intermediarias de descricao em `Label` e em estruturas rolaveis nao definitivas, depois substituidas por um bloco textual com largura controlada.

### Resultado desta etapa

- A aba `Charms` passou a ter um layout inicial funcional para evolucao.
- O fluxo antigo de selecao de criatura deixou de interferir visualmente.
- O client passou a ter uma base clara para futura integracao do sistema real de charms com servidor e combate.

## Fase 16. Primeira Integracao Real Do Sistema De Charms

### Escopo adotado

- Foi implementada somente a primeira versao funcional de Tier 1.
- Tier 2, Tier 3 e upgrades de chance permaneceram fora desta etapa.
- O primeiro charm real recebeu o nome `Savage Blow`.
- O custo inicial foi definido provisoriamente em 10 charm points para permitir teste com a economia atual do Bestiary.

### Autoridade e persistencia

- O client deixou de desbloquear charms localmente.
- O servidor passou a validar saldo, custo, estado e transicoes.
- Foi criada a tabela `player_charms`, com chave por jogador e charm.
- Os estados persistidos sao:
  - `0`: locked;
  - `1`: unlocked, ainda inativo;
  - `2`: active.
- O saldo disponivel e recalculado como pontos obtidos em Bestiaries concluidos menos o custo dos charms desbloqueados.
- O saldo agregado continua sem coluna fixa no jogador.

### Estrutura central de bonus

- Foi criada uma estrutura central de `CharacterBonuses` no objeto `Player`.
- A estrutura separa bonus de charm e bonus de equipamento.
- Foram preparados campos para critical chance, critical damage, skills, hit chance e mana leech.
- O recalculo ocorre no carregamento do jogador, em mudancas de estado do charm e em alteracoes de special skills por equipamentos.

### Charm critico Tier 1

- `Savage Blow` ativo concede 4% de chance de critico.
- O dano critico usa o hit maximo normal como base.
- A variacao adicional e sorteada entre 0% e 15% do hit maximo.
- O efeito e aplicado apenas contra criaturas PvE.
- Players e summons controlados por players sao excluidos.
- Condition ticks, fields, poison, burning, reflect, dano ambiental e origens sem ataque direto nao ativam o charm.
- As origens aceitas sao melee, ranged, wand e spell.
- O critico entra antes da mitigacao do alvo, preservando armor, defense, imunidades e resistencias.
- O critico de charms permanece separado do sistema preexistente de critical special skills de equipamentos.

### Protocolo client/servidor

- O request existente `0xE4` passou a tratar desbloqueio de charm.
- O pacote `0xD8` passou a enviar:
  - charm points disponiveis;
  - definicoes reais de charms;
  - estado locked, unlocked ou active;
  - custo, icone e chance;
  - lista extensivel de bonus reais do personagem.
- O servidor envia atualizacao no login, em requests do Bestiary, em mudancas de charm e em alteracoes relevantes de equipamento.

### NPC de ativacao

- Foi preparado o NPC `Charm Master`.
- O NPC exige que o charm ja esteja desbloqueado com charm points.
- O custo inicial de ativacao foi configurado em 10.000 gold e 1 crystal coin.
- A cobranca, ativacao, recalculo e atualizacao do client ocorrem no servidor.
- O NPC foi adicionado aos arquivos de dados, mas ainda precisa ser posicionado no mapa.

### Character Bonuses

- Foi criada a area `Character Bonuses` abaixo dos equipamentos.
- O painel consome somente bonus enviados pelo servidor.
- Quando o charm esta ativo, sao exibidos:
  - `Critical Chance: 4%`;
  - `Critical Damage: Max Hit + 0-15%`.
- A lista utiliza linhas dinamicas e pode receber novas fontes no futuro.

### Validacao executada

- A tabela `player_charms` foi aplicada e verificada no banco ativo.
- TFS e OTClient foram recompilados com sucesso.
- Os executaveis foram publicados nos caminhos operacionais e em `build-results`.
- O TFS iniciou completamente sem erros e o processo de smoke test foi encerrado sem permanecer ativo.
- O client iniciou sem novos erros de Lua ou OTUI.

## Fase 17. Estado Atual E Pendencias

### Estado funcional atual

- Bestiary com persistencia por jogador e criatura.
- Kills sendo contabilizadas no servidor.
- Estagio derivado a partir das kills atuais.
- Loot com sprite correta via `clientId`.
- Containers tecnicos removidos da exibicao de loot.
- Raridade ajustada e expandida com `Extremely Rare`.
- Descoberta inicial por primeira kill.
- Separacao entre `Creatures` e `Bosses`.
- Busca considerando ambas as abas.
- `Hide Unknown` funcional.
- Charm points totais calculados no servidor e exibidos no client.
- Aba `Charms` habilitada no modo reduzido.
- Aba `Charms` consumindo definicao, custo e estado reais enviados pelo servidor.
- Primeiro charm critico Tier 1 integrado ao combate PvE.
- Painel `Character Bonuses` recebendo bonus reais do servidor.

### Reversoes e descartes registrados

- Nao foi adotada persistencia definitiva de saldo agregado de charm points.
- Nao foi mantida a ideia de charm vinculado por criatura na tela do Bestiary.
- Nao foi mantido o uso cego de `last_stage_reached` como fonte de verdade.
- Fallbacks simplificados de outfit que destruíam a estrutura original foram descartados.

### Pendencias abertas

- Posicionar o NPC `Charm Master` no mapa.
- Validar dentro do jogo as tres transicoes locked, unlocked e active.
- Medir uma amostra de ataques para confirmar a taxa pratica de 4% e a faixa de dano maximo mais 0-15%.
- Definir os custos definitivos de desbloqueio e ativacao.
- Implementar novos charms apenas depois de estabilizar o primeiro caso real.
- Revisar se todas as criaturas especiais restantes possuem suporte visual consistente no Bestiary.
- Avaliar se a interface de bosses precisa de indicadores extras distintos das criaturas comuns.
- Decidir se o total de charm points tambem deve aparecer fora da Cyclopedia.

### Logica de projeto consolidada ao fim desta etapa

- O servidor calcula, valida e autoriza.
- O client consulta, busca e renderiza.
- O banco persiste configuracao e progresso bruto.
- Recompensas e estados derivados devem ser recalculaveis sempre que thresholds ou valores mudarem.

## Fase 18. Charms Reais, Critico, Bônus Agregados E Ferramentas De Operacao

### Escopo desta fase

- Integracao real do primeiro charm com persistencia de estado `locked`, `unlocked` e `active`.
- Entrada real do critico no pipeline de combate do servidor.
- Exibicao de bonus reais do personagem em uma MiniWindow dedicada.
- Ajustes de UX no desbloqueio do charm e documentacao das variaveis centrais.
- Inclusao de utilitarios operacionais para testes repetitivos e teleportes administrativos.

### Arquivos principais do servidor

- `sources/nekiro-tfs-1.5-7.72/src/player.h`
- `sources/nekiro-tfs-1.5-7.72/src/player.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/combat.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/protocolgame.cpp`
- `sources/nekiro-tfs-1.5-7.72/src/iologindata.cpp`
- `sources/nekiro-tfs-1.5-7.72/data/npc/scripts/charm_master.lua`
- `sources/nekiro-tfs-1.5-7.72/data/talkactions/scripts/activate_charm.lua`
- `sources/nekiro-tfs-1.5-7.72/data/talkactions/scripts/teleport_home.lua`
- `sources/nekiro-tfs-1.5-7.72/data/talkactions/scripts/teleport_to_town.lua`
- `sources/nekiro-tfs-1.5-7.72/data/talkactions/scripts/teleport_to_temple.lua`

### Arquivos principais do client

- `sources/otclient-redemption/modules/game_cyclopedia/tab/charms/charms.lua`
- `sources/otclient-redemption/modules/game_cyclopedia/tab/charms/charms.otui`
- `sources/otclient-redemption/modules/game_inventory/inventory.lua`
- `sources/otclient-redemption/modules/game_inventory/characterbonuses.otui`

### Estruturas e variaveis relevantes

#### Servidor

- `PlayerCharmState` em `player.h`
  - enum responsavel por distinguir `LOCKED`, `UNLOCKED` e `ACTIVE`.
- `CharmDefinition` em `player.h`
  - definicao estatica de cada charm.
  - campos importantes:
    - `id`
    - `name`
    - `description`
    - `unlockCost`
    - `iconIndex`
    - `chance`
    - `criticalDamageMinPercent`
    - `criticalDamageMaxPercent`
- `CharacterBonuses` em `player.h`
  - agregado central de bonus derivados do personagem.
  - campos relevantes nesta fase:
    - `totalCriticalChance`
    - `equipmentCriticalChance`
    - `equipmentCriticalDamagePercent`
    - `charmCriticalChance`
    - `charmCriticalDamageMinPercent`
    - `charmCriticalDamageMaxPercent`
    - `manaLeechChance`
    - `manaLeechAmount`
- `charmStates` em `Player`
  - mapa em memoria dos estados persistidos em `player_charms`.

#### Client

- `Cyclopedia.Charms.data` em `charms.lua`
  - cache local das definicoes enviadas pelo servidor.
- `Cyclopedia.Charms.points`
  - saldo disponivel ja descontado pelo servidor.
- `unlockConfirmWindow`
  - popup temporario de confirmacao antes do unlock.
- `characterBonuses`
  - lista de bonus recebida pela MiniWindow `Character Bonuses`.

### Persistencia e recalculo

- A tabela `player_charms` passou a ser a fonte persistida do estado de unlock/activation.
- O saldo de charm points continuou derivado:
  - `earnedPoints = bestiary concluido`
  - `spentPoints = soma dos charms desbloqueados/ativos`
  - `availablePoints = earnedPoints - spentPoints`
- O carregamento de estado ocorre em `Player::loadCharmStatesFromDatabase()`.
- Foi corrigido um problema de leitura em que `uint8_t` vindo do banco podia ser interpretado como caractere ASCII.
- A correcao aplicada foi ler `charm_id` e `state` como `uint16_t` e converter depois, evitando estado invalido em memoria.

### Combate e critico

- O primeiro charm real foi `Savage Blow`.
- O ponto central de aplicacao ficou em `applyCharmCritical(...)` em `combat.cpp`.
- A elegibilidade basica continua passando por `canApplyCharmCritical(...)`.
- A separacao correta entre PvE e PvP foi feita com a logica ja existente `Combat::isPlayerCombat(const Creature* target)`.
- Essa funcao considera como combate de player:
  - `target->getPlayer()`
  - `target->isSummon() && target->getMaster()->getPlayer()`
- Stage I real consolidado:
  - chance: `4%`
  - PvE: `max hit + 5-25%`
  - PvP: mesmo proc, mas apenas `50%` do bonus extra
- Formula final em PvP:
  - sorteia `bonusPercent`
  - calcula `extraDamage = floor(maxHit * bonusPercent / 100)`
  - reduz para `floor(extraDamage * 0.5)`
  - dano final = `maxHit + extraDamage reduzido`

### Exibicao e bonus agregados

- A janela `Character Bonuses` deixou de depender de mock e passou a consumir apenas dados do servidor.
- O envio atual e montado em `ProtocolGame::sendBestiaryCharmsData()` em `protocolgame.cpp`.
- A lista agora inclui:
  - `Critical Chance`
  - `Crit. Damage`
  - `Equipment Critical Damage`
  - `Mana Leech`
- O valor `Critical Chance` passou a usar `CharacterBonuses.totalCriticalChance`, criado para centralizar futuras somas de outras fontes.
- A MiniWindow do client pinta o valor de `Critical Chance` em verde quando `> 0`.

### UX da aba Charms

- Antes de gastar pontos em `Unlock`, o client agora abre um popup de confirmacao em `charms.lua`.
- O unlock so chama `g_game.BuyCharmRune(...)` depois da confirmacao.
- O campo `Effect` do `Savage Blow` foi desacoplado visualmente do texto longo da descricao e passou a exibir:
  - `Critical chance / damage.`
- A descricao longa foi atualizada com:
  - escopo de ataques suportados;
  - ajuste de PvP;
  - previsao textual de Stage I, II e III;
  - lore do charm.

### Indicadores temporarios e testes

- Foi adicionado um log temporario em `combat.cpp`:
  - `[CharmCritical] player=... target=... maxHit=... bonusPercent=... extraDamage=...`
- Foi adicionada exibicao flutuante `CRITICAL!` via `ColoredText` no alvo quando o proc ocorre.
- O fluxo usa a infraestrutura ja existente do client para `AnimatedText`, sem novo protocolo.

### Ferramentas operacionais adicionadas

- `reset_gm_lancellin_2490_dragons.bat`
  - arquivo externo de apoio para repetir testes de unlock/activation do dragon.
- `!activatecharm savage blow`
  - TalkAction de teste que ativa o charm e faz `player:save()` logo em seguida.
- `/temple <destino>`
  - nova TalkAction administrativa para teleporte rapido por alias.
  - aliases implementados nesta fase:
    - `ab`
    - `kazz`
    - `thais`
    - `venore`
    - `carlin`
    - `ank`
    - `edron`
    - `rook`
  - a implementacao tambem faz fallback para `Town(param)` quando houver nome de cidade valido cadastrado no servidor.

### Decisoes e limites mantidos

- Nao houve alteracao de protocolo.
- Nao houve alteracao de banco fora do uso de `player_charms` ja introduzido.
- Nao houve alteracao na logica de Charm Points do Bestiary.
- Stage II e Stage III continuam apenas descritos na UI, sem implementacao real de combate.
- `Character Bonuses` continua sendo somente exibicao; toda a validacao permanece no servidor.
