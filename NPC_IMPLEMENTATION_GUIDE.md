# NPC Implementation Guide

Guia pratico para criar e portar NPCs neste servidor TFS 1.5 Nekiro 7.72.

Base de referencia:

- Modelos oficiais do TFS 1.5 em `D:\tibia-oldschool-build-backup-20260702-170415\sources\nekiro-tfs-1.5-7.72\data\npc`.
- Implementacoes ja estabilizadas no nosso servidor em `server/data/npc`.
- Experiencia dos ports de Rookgaard, Thais, Carlin e Kazordoon.

## Regra principal

Todo NPC deve ser tratado como um conjunto de 3 partes:

1. `server/data/npc/Nome.xml`
2. `server/data/npc/scripts/Nome.lua`
3. entrada em `server/data/world/world-spawn.xml`

Nao considere um NPC pronto se apenas o XML/script existir. Ele precisa carregar no TFS, aparecer no spawn correto, responder ao `hi`, e executar sua funcao principal.

## Estrutura XML recomendada

Modelo base para NPC comum:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<npc name="Example" script="Example.lua" walkinterval="1500" floorchange="0" pushable="0">
	<health now="100" max="100"/>
	<look type="128" head="78" body="88" legs="88" feet="88" addons="0"/>
	<parameters>
		<parameter key="idletime" value="30"/>
		<parameter key="message_greet" value="Hello, |PLAYERNAME|."/>
		<parameter key="message_farewell" value="Good bye, |PLAYERNAME|."/>
		<parameter key="message_walkaway" value="Good bye."/>
		<parameter key="message_idletimeout" value="Good bye."/>
		<parameter key="message_placedinqueue" value="Please wait, |PLAYERNAME|."/>
	</parameters>
</npc>
```

Notas:

- Use `pushable="0"` por padrao. A source carrega esse atributo e o default de NPC pode ser empurravel.
- `access`, `level` e `maglevel` aparecem em alguns NPCs antigos, mas a nossa source nao usa isso como regra importante para NPC. Nao dependa desses atributos.
- A source so passa para `getNpcParameter(...)` os filhos de `<parameters>`. Atributos colocados direto na tag `<npc>`, como `module_keywords="1"`, nao devem ser usados como base de comportamento. Se precisar de `module_keywords`, use `<parameter key="module_keywords" value="1"/>`.
- Placeholders validos nesta base incluem `|PLAYERNAME|` e `|TIME|`. Nao use formatos inventados como `%N`, porque nao serao resolvidos pelo NpcSystem.
- GM/access player pode atravessar criaturas pelo core. Teste colisao com personagem comum.
- `message_placedinqueue` funciona na nossa base porque temos alias para `MESSAGE_ALREADYFOCUSED`.
- Use `floorchange="0"` salvo caso real de NPC que precise mudar andar.
- Nao use `talkradius` como tentativa de corrigir NPC mudo. O core do NPC tambem usa `Npc::canSee(..., 3, 3)`, entao o jogador ainda precisa estar no alcance real de fala/visao do NPC.
- Salve XML/Lua em UTF-8. Muitos scripts legados vieram com acentos quebrados (`isnÂ´t`, `canÂ´t`) por encoding antigo; isso nao costuma quebrar a logica, mas polui dialogo e dificulta manutencao.

## NPC sem logica especial

Use `default.lua` ou um script pequeno com keywords. O modelo oficial do TFS para NPC simples e:

```lua
local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

npcHandler:addModule(FocusModule:new())
```

Para NPC com falas:

```lua
dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'My name is Example.'})
keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'I work here.'})
keywordHandler:addKeyword({'time'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'It is |TIME|.'})

npcHandler:addModule(FocusModule:new())
```

Use `dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')` nos scripts portados que dependem de constantes/posicoes comuns, rotas ou padroes locais. Para scripts puramente genericos, o NpcSystem ja e carregado por `data/npc/lib/npc.lua`.

Nao use a presenca ou ausencia desse `dofile` como diagnostico principal de `hi` nao respondendo. Ele pode ser necessario para constantes auxiliares, mas o greet basico vem do NpcSystem e do `FocusModule:new()`.

## NPC de shop

Ha dois modelos seguros.

Modelo declarativo em XML, bom para lojistas simples:

```xml
<npc name="Furniture Seller" script="default.lua" walkinterval="2000" floorchange="0" pushable="0">
	<health now="100" max="100"/>
	<look type="128" head="97" body="58" legs="105" feet="120"/>
	<parameters>
		<parameter key="message_greet" value="Hello. Ask me for an offer."/>
		<parameter key="message_sendtrade" value="Of course, take a look."/>
		<parameter key="module_shop" value="1"/>
		<parameter key="shop_buyable" value="rope,2120,50;shovel,2554,10;backpack,2003,10"/>
		<parameter key="shop_sellable" value="mace,2398,23;sword,2376,15"/>
	</parameters>
</npc>
```

Modelo Lua, melhor quando ha falas, missoes ou logica junto:

```lua
local shopModule = ShopModule:new()
npcHandler:addModule(shopModule)

shopModule:addBuyableItem({'rope'}, 2120, 50)
shopModule:addBuyableItem({'shovel'}, 2554, 10)
shopModule:addSellableItem({'mace'}, 2398, 23)
```

Notas:

- Na nossa base, `trade`, `offer` e `shop` abrem a janela de trade.
- Evite criar keyword manual para `offer`, `trade` ou `shop` em lojista se isso impedir o `ShopModule` de abrir a janela.
- Se precisar responder a `offer`, prefira uma fala curta que nao conflite com o trade, ou confie no `ShopModule`.
- Sempre teste: `hi`, `trade`, comprar 1 item, vender 1 item, comprar com backpack se aplicavel.

## NPC de travel

Modelo recomendado:

```lua
local node = keywordHandler:addKeyword({'city'}, StdModule.say, {
	npcHandler = npcHandler,
	onlyFocus = true,
	text = 'Do you seek a passage to City for 100 gold?'
})
node:addChildKeyword({'yes'}, StdModule.travel, {
	npcHandler = npcHandler,
	premium = false,
	level = 0,
	cost = 100,
	destination = Position(32000, 32000, 7)
})
node:addChildKeyword({'no'}, StdModule.say, {
	npcHandler = npcHandler,
	onlyFocus = true,
	reset = true,
	text = 'We would like to serve you some time.'
})
```

Notas:

- Use `Position(x, y, z)` ou constantes definidas em `npc/scripts/lib/greeting.lua`, como `BOATPOS_*`, `FERRYPOS_*`, `CARPETPOS_*`, quando existirem.
- Confirme as coordenadas no mapa antes de implementar.
- O `StdModule.travel` ja checa premium, level, PZ lock, dinheiro e teleporta.
- Quando um NPC de transporte cobra, encerra o dialogo normal e nao teleporta, a primeira verificacao deve ser em `server/data/npc/scripts/lib/greeting.lua`: rotas como `BOATPOS_*`, `FERRYPOS_*`, `STEAMPOS_*` e `CARPETPOS_*` precisam existir ali.
- Nao use `|TRAVELCOST|` em texto livre a menos que o fluxo realmente passe por um handler que substitua esse placeholder. Para ports locais, prefira texto explicito como `for 80 gold?`.

Checklist minimo para NPC de transporte:

1. Confirmar se o NPC usa `StdModule.travel` ou um fluxo local equivalente.
2. Confirmar se a `destination` existe de verdade:
   - `Position(x, y, z)` inline; ou
   - constante valida em `greeting.lua`.
3. Testar `keyword -> yes -> teleport`.
4. Testar tambem:
   - sem dinheiro;
   - premium quando aplicavel;
   - cancelamento com `no`;
   - retorno apos viagem para garantir que o topico/reset nao ficou preso.
5. Se o NPC cobra mas nao teleporta:
   - revisar `destination`;
   - revisar constantes `BOATPOS_*`, `CARPETPOS_*`, `FERRYPOS_*`, `STEAMPOS_*`;
   - revisar se o texto do prompt nao depende de placeholder nao resolvido.

Heuristica pratica:

- barco/carpete/ferry quase sempre deve ser modelado como `StdModule.travel`;
- se o legacy usa um fluxo muito customizado, portar primeiro para travel simples e estavel;
- so depois reintroduzir falas adicionais, desconto, lore ou excecoes.

## NPC de banco

Para banqueiro comum, use o nosso `bank.lua` atual. Ele ja tem log, deposito, saque, transferencia e persistencia.

XML recomendado:

```xml
<npc name="Eva" script="bank.lua" walkinterval="2000" floorchange="0" pushable="0">
	<health now="100" max="100"/>
	<look type="136" head="78" body="88" legs="88" feet="88"/>
	<parameters>
		<parameter key="message_greet" value="Welcome |PLAYERNAME|! What can I do for you?"/>
		<parameter key="message_farewell" value="Good bye."/>
	</parameters>
</npc>
```

Nao copie scripts antigos de banco por cidade. Isso ja quebrou antes e o `bank.lua` atual e a fonte correta.

## NPC de promocao

Use o modelo oficial atual:

```lua
local node = keywordHandler:addKeyword({'promot'}, StdModule.say, {
	npcHandler = npcHandler,
	onlyFocus = true,
	text = 'I can promote you for 20000 gold coins. Do you want me to promote you?'
})
node:addChildKeyword({'yes'}, StdModule.promotePlayer, {
	npcHandler = npcHandler,
	cost = 20000,
	level = 20,
	text = 'Congratulations! You are now promoted.'
})
node:addChildKeyword({'no'}, StdModule.say, {
	npcHandler = npcHandler,
	onlyFocus = true,
	text = 'Alright then, come back when you are ready.',
	reset = true
})
```

Nao use o formato antigo:

```lua
promotions = {[1] = 5, [2] = 6, [3] = 7, [4] = 8}
```

A nossa `StdModule.promotePlayer` usa `player:getVocation():getPromotion()`.

## NPC de bless

Modelo oficial:

```lua
local node = keywordHandler:addKeyword({'first bless'}, StdModule.say, {
	npcHandler = npcHandler,
	onlyFocus = true,
	text = 'Do you want to buy the first blessing for 10000 gold?'
})
node:addChildKeyword({'yes'}, StdModule.bless, {
	npcHandler = npcHandler,
	bless = 1,
	premium = true,
	cost = 10000
})
node:addChildKeyword({'no'}, StdModule.say, {
	npcHandler = npcHandler,
	onlyFocus = true,
	reset = true,
	text = 'Too expensive, eh?'
})
```

## NPC com quest ou fluxo de conversa

Use `npcHandler.topic[cid]`, nao variavel global `topic` ou `talk_state`, para fluxos novos. Variaveis globais misturam conversas quando dois jogadores falam com o mesmo NPC.

Modelo:

```lua
local topicList = {
	NONE = 0,
	ASK_CONFIRM = 1
}

local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	local player = Player(cid)
	if not player then
		return false
	end

	local topic = npcHandler.topic[cid] or topicList.NONE

	if msgcontains(msg, 'mission') then
		npcHandler:say('Do you accept this mission?', cid)
		npcHandler.topic[cid] = topicList.ASK_CONFIRM
		return true
	elseif topic == topicList.ASK_CONFIRM and msgcontains(msg, 'yes') then
		player:setStorageValue(12345, 1)
		npcHandler:say('Good. Return when it is done.', cid)
		npcHandler.topic[cid] = topicList.NONE
		return true
	elseif topic == topicList.ASK_CONFIRM and msgcontains(msg, 'no') then
		npcHandler:say('Maybe another time.', cid)
		npcHandler.topic[cid] = topicList.NONE
		return true
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
```

Para scripts antigos ja existentes, podemos manter `talk_state` temporariamente se o NPC for simples e pouco critico. Para novo NPC complexo, use `npcHandler.topic[cid]`.

Em codigo novo, tambem prefira `npcHandler:isFocused(cid)` ao inves de `npcHandler.focus ~= cid`. A nossa base suporta lista de focos em `npcHandler.focuses`; `npcHandler.focus` e uma compatibilidade antiga que pode ficar fragil em scripts portados.

## Classificacao pratica por tipo

Ao portar ou auditar NPCs, classifique primeiro. Isso reduz erro de escopo.

- `lore`: fala, ambientacao, historia, rumores.
- `shop`: vende/compra itens.
- `travel`: barco, carpete, ferry, teleport de servico.
- `spell teacher`: ensina magias.
- `heal/bless`: remove condicoes, cura, bless.
- `bank`: deposito, saque, transferencia, saldo.
- `quest/stateful`: depende de storage, item, palavra-chave em sequencia, recompensa ou progresso.
- `hostile/special reaction`: pune keyword, invoca criatura, reage uma vez por jogador, etc.

Muitos NPCs sao mistos. Nesses casos, implemente e teste por prioridade:

1. greet e foco
2. funcao principal
3. quest/storage
4. falas secundarias/lore

Isso evita gastar tempo refinando dialogo enquanto a parte util ainda esta quebrada.

## Receita de modernizacao de script legado

Quando portar NPC antigo, siga esta ordem minima:

1. Copie XML, script e spawn do legado.
2. Ajuste o XML para o modelo local:
   - adicionar `<?xml version="1.0" encoding="UTF-8"?>`;
   - adicionar `pushable="0"` por padrao;
   - mover parametros relevantes para dentro de `<parameters>`;
   - trocar placeholders invalidos por `|PLAYERNAME|` e afins.
3. No Lua, normalize o cabecalho:
   - `KeywordHandler`, `NpcHandler`, `NpcSystem.parseParameters(npcHandler)`;
   - os 4 eventos `onCreatureAppear`, `onCreatureDisappear`, `onCreatureSay`, `onThink`;
   - `npcHandler:addModule(FocusModule:new())` no fim.
4. Troque guardas antigos:
   - `npcHandler.focus ~= cid` -> `not npcHandler:isFocused(cid)` em codigo novo;
   - `talk_state` global -> `npcHandler.topic[cid]` em fluxo novo ou quando tocar no script;
   - `hasCondition` -> `getCreatureCondition` ou API OO;
   - APIs de item/metodo nao confirmadas -> validar antes em script que ja funciona nesta base.
5. Preserve a logica de quest/shop/travel, mas sem reintroduzir `FocusModule:init` customizado.
6. Revise keywords reservadas:
   - lojista: nao bloquear `trade`, `offer`, `shop`;
   - banker: usar `bank.lua`;
   - travel: preferir `StdModule.travel`.
7. Suba o TFS e teste com personagem comum, nao apenas GM.

Meta pratica: portar primeiro para um estado estavel e previsivel, depois refinar falas ou detalhes secundarios.

Sinais de alerta que costumam quebrar port:

- placeholder legado no texto (`%N`, `|TRAVELCOST|` sem handler, etc.);
- dependencia oculta de `greeting.lua` para rotas ou constantes;
- callback registrado apontando para funcao que nao existe;
- `talk_state` global em NPC com fluxo de confirmacao;
- keyword manual colidindo com `ShopModule`;
- funcao global do `NpcSystem` sobrescrita no proprio script;
- mensagem bonita demais para um fluxo que ainda nao foi validado in game.

## Condicoes, vida e cura

Nao use:

```lua
hasCondition(cid, CONDITION_FIRE)
```

Essa funcao nao existe na nossa base.

Use:

```lua
if getCreatureCondition(cid, CONDITION_FIRE) then
	doRemoveCondition(cid, CONDITION_FIRE)
	doSendMagicEffect(getCreaturePosition(cid), CONST_ME_MAGIC_RED)
elseif getCreatureCondition(cid, CONDITION_POISON) then
	doRemoveCondition(cid, CONDITION_POISON)
	doSendMagicEffect(getCreaturePosition(cid), CONST_ME_MAGIC_GREEN)
elseif getCreatureHealth(cid) < 40 then
	doCreatureAddHealth(cid, 40 - getCreatureHealth(cid))
	doSendMagicEffect(getCreaturePosition(cid), CONST_ME_MAGIC_BLUE)
end
```

Tambem e valido usar API OO quando o script ja estiver nesse estilo:

```lua
local player = Player(cid)
if player and player:getCondition(CONDITION_FIRE) then
	player:removeCondition(CONDITION_FIRE)
end
```

## Focus e greet

Padrao normal:

```lua
npcHandler:addModule(FocusModule:new())
```

Nao sobrescreva `FocusModule:init` dentro de script de NPC. O `FocusModule` e global no ambiente Lua do NpcSystem; quando um NPC redefine `FocusModule:init`, ele altera o comportamento de greet dos NPCs carregados depois. Isso ja causou NPCs aparentemente mudos, incluindo `Kevin`, `Jimbin`, `Wally` e `Talphion`.

Todos os NPCs comuns devem responder a `hi` e `hello`.

Evite:

```lua
function FocusModule:init(handler)
	FOCUS_GREETSWORDS = {'hi npcname'}
	...
end
```

Isso altera o comportamento global do modulo e e proibido neste datapack.

Se um NPC realmente precisa de cumprimento especial, como `hi king` ou `hail general`, nao redefina `FocusModule`. Em vez disso, trate a frase no callback do proprio NPC e converta para o fluxo normal de greet, ou use keywords/callbacks locais sem modificar a tabela global `FocusModule`.

## Diagnostico rapido para NPC que nao responde `hi`

Nao comece alterando fala aleatoria ou `talkradius`. Siga esta ordem:

1. Confirmar spawn:
   - XML existe;
   - script existe;
   - entrada correta em `world-spawn.xml`;
   - TFS subiu sem erro desse NPC.
2. Confirmar erro global de NpcSystem:
   - procurar `function FocusModule:init(` em `server/data/npc/scripts`;
   - se existir, isso pode contaminar NPCs carregados depois.
3. Confirmar callback:
   - se usa `CALLBACK_MESSAGE_DEFAULT` ou `CALLBACK_GREET`, a funcao precisa existir;
   - se o callback retorna cedo, conferir `isFocused`, storage e topicos.
4. Confirmar se o problema e so com GM:
   - GM pode atravessar NPC;
   - colisao e alguns testes de proximidade devem ser feitos com personagem comum.
5. Confirmar se o problema e XML antigo:
   - `module_keywords="1"` na tag `<npc>` nao e diagnostico nem correcao;
   - `talkradius` nao corrige greet quebrado;
   - placeholder errado (`%N`) nao quebra `hi`, mas denuncia XML portado sem adaptacao.
6. So depois revisar a logica especifica do NPC.

Esse fluxo evita repetir o erro classico de "consertar" um NPC isolado quando a causa real era um script globalmente contaminando os outros.

## Diagnostico rapido para NPC que fala mas nao executa a funcao

Se o NPC responde `hi`, `name`, `job`, mas falha na funcao principal:

1. `shop`
   - conferir se `ShopModule` foi adicionado;
   - conferir colisao com `trade`, `offer`, `shop`;
   - testar compra e venda reais.
2. `travel`
   - conferir `StdModule.travel`;
   - conferir `destination`/constantes;
   - conferir se cobrou dinheiro mas nao moveu.
3. `quest`
   - conferir `npcHandler.topic[cid]`;
   - conferir storage;
   - conferir item exigido/recompensa.
4. `heal/bless`
   - conferir API real da base (`getCreatureCondition`, API OO, etc.);
   - nao assumir helpers de forks antigos.

Em outras palavras: se o greet funciona, o problema normalmente nao e mais de focus/spawn, e sim da logica especifica.

## Callbacks

Se registrar callback, a funcao precisa existir.

Correto:

```lua
local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end
	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
```

Errado:

```lua
npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
```

sem declarar `creatureSayCallback`.

Isso ja deixou NPC sem responder ou gerou erro em runtime.

## Falas e `npcHandler:say`

Para novo codigo, prefira:

```lua
npcHandler:say('Text.', cid)
```

Evite usar numero como segundo argumento para jogador:

```lua
npcHandler:say('Text.', 1)
```

A nossa base tem compatibilidade para alguns usos antigos de delay, mas isso deve ser preservado apenas em script legado que ja foi testado. Em codigo novo, use `cid`.

Se o NPC precisar mudar a mensagem de despedida dinamicamente, use callback local e altere `MESSAGE_FAREWELL` ali. O caso de `A Prisoner` e a referencia atual dessa tecnica.

## API antiga vs API atual

Evite copiar scripts antigos sem adaptar. Erros ja encontrados:

- `hasCondition(...)`: substituir por `getCreatureCondition(...)` ou API OO.
- `item:getContainer()`: nao existe nesta base.
- `item:setText(...)`: nao existe nesta base; em item script use `setAttribute(ITEM_ATTRIBUTE_TEXT, text)`.
- `promotions = {...}` em `StdModule.promotePlayer`: formato antigo, nao usar.
- `CALLBACK_MESSAGE_DEFAULT` apontando para funcao inexistente.
- `FocusModule:init` customizado sem necessidade.
- `FocusModule:init` redefinido dentro de NPC: contamina globalmente o greet de outros NPCs.
- `npcHandler.focus ~= cid` em NPC novo: usar `not npcHandler:isFocused(cid)`.
- `module_keywords="1"` direto na tag `<npc>` como se fosse parametro: se precisar, usar `<parameter key="module_keywords" value="1"/>`.
- `message_needmoremoney`: chave antiga/errada para esta base; prefira `message_needmoney` ou `message_missingmoney`, conforme o caso.
- `%N` ou placeholders inventados em XML: usar `|PLAYERNAME|`, `|TIME|` e afins.
- Arquivo salvo em encoding antigo/ANSI com texto quebrado: preferir UTF-8 ao tocar no arquivo.
- `XML` apontando para script ausente.

## Spawn

Exemplo:

```xml
<spawn centerx="32651" centery="31888" centerz="9" radius="1">
	<npc name="Kroox" x="0" y="0" z="9" spawntime="60" direction="1"/>
</spawn>
```

Regras:

- O `name` no spawn deve bater exatamente com o arquivo XML: `Kroox` -> `Kroox.xml`.
- O XML deve apontar exatamente para o script: `script="Kroox.lua"`.
- Cuidado com maiusculas, acentos e espacos.
- Se o NPC ja existia antigo, mover o antigo para `NPCS ANTIGOS/<regiao>` antes de substituir, quando aplicavel.

## Checklist antes de considerar pronto

1. XML existe em `server/data/npc/Nome.xml`.
2. Script existe em `server/data/npc/scripts/Nome.lua`.
3. `world-spawn.xml` tem o NPC na coordenada correta.
4. XML tem `pushable="0"`, salvo excecao consciente.
5. Script tem os 4 eventos: `onCreatureAppear`, `onCreatureDisappear`, `onCreatureSay`, `onThink`.
6. Script termina com `npcHandler:addModule(FocusModule:new())`.
7. Se usa `CALLBACK_MESSAGE_DEFAULT`, a funcao existe.
8. Se usa `CALLBACK_GREET`, a funcao existe.
9. Nao redefine `FocusModule:init` ou qualquer funcao global do NpcSystem.
10. Em script novo, usa `npcHandler:isFocused(cid)` em vez de `npcHandler.focus ~= cid`.
11. Se usa `module_keywords`, esta dentro de `<parameters>`, nao como atributo solto da tag `<npc>`.
12. Nao usa `hasCondition`, `item:getContainer`, `item:setText` ou APIs antigas nao confirmadas.
13. XML usa placeholders validos e encoding legivel.
14. Lojista abre janela com `trade`.
15. Travel teleporta para coordenada validada.
16. Quest/missao usa storage correto e salva comportamento esperado.
17. TFS sobe sem erro novo.
18. Teste in game minimo: `hi`, `name`, `job`, funcao principal, `bye`.

## Auditoria rapida recomendada

Comandos uteis:

```powershell
rg -n "function\s+FocusModule:init\(|CALLBACK_MESSAGE_DEFAULT|CALLBACK_GREET|hasCondition\(|:getContainer\(|:setText\(|npcHandler\.focus\s*[~=]" server/data/npc/scripts
```

```powershell
rg -n "module_keywords=\"|message_needmoremoney|talkradius" server/data/npc
```

```powershell
rg -n "%N|isnÂ|canÂ|wonÂ|letÂ|itÂ|IÂ|youÂ" server/data/npc server/data/npc/scripts
```

```powershell
Select-String -Path "server/data/npc/*.xml" -Pattern "script="
```

```powershell
Select-String -Path "server/data/world/world-spawn.xml" -Pattern '<npc name="Nome"'
```

Smoke test:

```powershell
cd D:\tibia-oldschool\server
.\tfs.exe
```

Verifique se aparece erro de NPC/script/XML no terminal. Warnings antigos de `empty spawn` nao indicam, por si so, falha do NPC novo.

## Ordem recomendada para portar NPCs por regiao

1. Listar NPCs no `world-spawn.xml` do servidor legado/reference.
2. Separar por funcao: lore, shop, travel, banco, quest, spell, bless, promotion.
3. Para lore simples, usar `default.lua` ou script curto.
4. Para shop simples, preferir `module_shop` declarativo ou `ShopModule` limpo.
5. Para quest/travel, portar manualmente e adaptar API local.
6. Rodar auditoria estatica.
7. Subir TFS.
8. Entregar roteiro de teste por NPC.

## Decisoes locais deste projeto

- Banqueiros usam o nosso `bank.lua`; nao criar banco separado por cidade.
- `trade`, `offer` e `shop` devem abrir loja quando o NPC e comerciante.
- Nenhum NPC deve redefinir `FocusModule:init`. A excecao antiga de `Bezil`, `Nezil` e `Maryza` foi removida porque contaminava NPCs carregados depois.
- Todos os NPCs comuns devem responder a `hi`; cumprimentos especiais devem ser tratados localmente sem alterar o `FocusModule` global.
- NPCs devem ser nao empurraveis por padrao via `pushable="0"`.
- Para personagem GM, atravessar NPC pode ser comportamento do core por `group->access`; validar colisao com personagem comum.
- Ao portar do legado, nao assumir que a API antiga existe. Conferir contra `server/data/lib/compat/compat.lua`, `server/data/npc/lib`, e scripts atuais que ja funcionam.
- O alvo para novos ports e: XML limpo, callbacks locais, `npcHandler:isFocused(cid)`, `npcHandler.topic[cid]`, sem remendos globais no NpcSystem.
