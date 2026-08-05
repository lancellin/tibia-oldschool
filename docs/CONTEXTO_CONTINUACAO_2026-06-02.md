# Contexto tecnico completo - Tibia Oldschool Modern

Gerado em: 2026-06-02  
Workspace principal: `C:\tibia-oldschool`

Este documento existe para permitir continuar o desenvolvimento em uma conversa nova do Codex sem depender do historico longo do chat anterior.

## Como usar em uma nova conversa

Prompt recomendado para a nova conversa:

```text
Estou continuando o projeto Tibia Oldschool Modern em C:\tibia-oldschool.
Leia primeiro o documento:
C:\Users\guisu\Desktop\CONTEXTO_TIBIA_OLDSCHOOL_2026-06-02.md

Depois verifique git status, entenda as mudancas locais e continue a partir do estado descrito. Nao reverta nada sem minha autorizacao.
```

Se a conversa nova conseguir ler o workspace, tambem ha uma copia em:

```text
C:\tibia-oldschool\docs\CONTEXTO_CONTINUACAO_2026-06-02.md
```

## Resumo executivo

O projeto e um OTServer oldschool/hibrido baseado em protocolo 7.72, com servidor Nekiro TFS 1.5 downgrade e client custom OTClient Redemption/Mehah. A direcao atual e manter o feeling oldschool, mas com client moderno, movimentacao boa, UI customizavel, sistema grafico HD opcional, formulas de combate reequilibradas, economia controlada e sistemas futuros como bestiary, groundloot persistente, CAM, compensation auditavel e protecoes anti-clone.

O projeto ja passou por varias validacoes importantes:

- servidor compila e roda localmente;
- client Redemption compila e conecta;
- walking ficou muito superior ao OTCv8 anterior;
- click map foi desacelerado para nao dominar PvP;
- formulas de mage, knight e paladin foram testadas e consideradas OK;
- renderizador do client ja aceita override parcial de sprites por `Tibia.cwm`;
- HD graphics toggle funciona;
- pipeline inicial de assets 64x64/128x128 esta em andamento;
- Map Editor/RME foi compilado/testado, com ajustes de dat/otb;
- backup privado no GitHub ja existe, mas ZIP completo/dump SQL ainda precisam sair do PC para nuvem/HD externo.

## Arquitetura

Raiz do projeto:

```text
C:\tibia-oldschool
```

Principais pastas:

```text
C:\tibia-oldschool\server
C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72
C:\tibia-oldschool\builds\nekiro-tfs-1.5-7.72
C:\tibia-oldschool\sources\otclient-redemption
C:\tibia-oldschool\sources\remeres-map-editor
C:\tibia-oldschool\sources\rme-otacademy
C:\tibia-oldschool\tools\assets
C:\tibia-oldschool\docs
C:\tibia-oldschool\backups
```

Servidor:

- Source: `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72`
- Build: `C:\tibia-oldschool\builds\nekiro-tfs-1.5-7.72`
- Pacote de execucao: `C:\tibia-oldschool\server`
- Executavel usado: `C:\tibia-oldschool\server\tfs.exe`
- Banco local: `oldschool772db`
- MariaDB: MariaDB 10.11 local

Client:

- Source: `C:\tibia-oldschool\sources\otclient-redemption`
- Executavel: `C:\tibia-oldschool\sources\otclient-redemption\otclient.exe`
- Protocolo usado: 772
- Arquivos de coisas:
  - `C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.dat`
  - `C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.spr`
  - `C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.cwm` opcional para overrides HD

Map Editor:

- RME/OTAcademy testado.
- O mapa original parecia grande demais, com muito espaco vazio.
- RME abre, mas exige apontar diretorios corretos de client/dat/spr/otb.
- Ainda ha curva de aprendizado e decisao futura sobre reduzir mapa/limpar areas inutilizadas.

## Versao de protocolo

Protocolo alvo: `7.72` / `772`.

Decisao importante:

- A tentativa com base 8.60 downgrade foi abandonada.
- A base atual e Nekiro TFS 1.5 downgrade branch 7.72.
- O client oficial do projeto, por enquanto, e OTClient Redemption/Mehah compilado localmente.
- OTCv8 antigo foi util para comparar, mas tinha stutter grafico/walking pior e problemas de compilacao/manutencao.

## Estado do Git e backup

O repositorio Git raiz esta em:

```text
C:\tibia-oldschool\.git
```

Backup privado GitHub ja criado:

```text
https://github.com/lancellin/tibia-oldschool-private-backup.git
```

Branch: `main`  
Commit remoto limpo citado anteriormente: `06bf965bea9e52ee119d75f28ff0be75a39e46bd`

Arquivos grandes foram removidos do historico Git para permitir push privado no GitHub. O ZIP bruto completo e dump SQL ainda precisam ser copiados para nuvem/HD externo:

```text
C:\tibia-oldschool\tibia-oldschool-full-20260601-083301.zip
C:\tibia-oldschool\oldschool772db-20260601-081706.sql
```

Estado atual do working tree em 2026-06-02: ha muitas mudancas locais importantes ainda nao commitadas. A proxima conversa deve rodar:

```powershell
git status --short
git diff --stat
```

Nao reverter nada sem autorizacao.

Arquivos modificados relevantes detectados:

```text
docs/architecture.md
server/data/items/items.xml
server/data/monster/rat.xml
server/data/spells/scripts/conjuring/conjure_power_bolt.lua
server/data/spells/scripts/healing/heal_friend.lua
server/data/spells/spells.xml
server/tfs.exe
sources/nekiro-tfs-1.5-7.72/src/const.h
sources/nekiro-tfs-1.5-7.72/src/creature.cpp
sources/nekiro-tfs-1.5-7.72/src/creature.h
sources/nekiro-tfs-1.5-7.72/src/game.cpp
sources/nekiro-tfs-1.5-7.72/src/player.cpp
sources/nekiro-tfs-1.5-7.72/src/player.h
sources/nekiro-tfs-1.5-7.72/src/protocolgame.cpp
sources/nekiro-tfs-1.5-7.72/src/weapons.cpp
sources/otclient-redemption/modules/client_options/data_options.lua
sources/otclient-redemption/modules/client_options/options.lua
sources/otclient-redemption/modules/client_options/styles/graphics/graphics.otui
sources/otclient-redemption/modules/client_terminal/commands.lua
sources/otclient-redemption/modules/game_features/features.lua
sources/otclient-redemption/modules/game_shaders/shaders.lua
sources/otclient-redemption/src/client/luafunctions.cpp
sources/otclient-redemption/src/client/mapview.cpp
sources/otclient-redemption/src/client/spritemanager.cpp
sources/otclient-redemption/src/client/spritemanager.h
sources/otclient-redemption/src/client/thing.cpp
sources/otclient-redemption/src/client/thingtype.cpp
sources/otclient-redemption/src/client/thingtype.h
sources/otclient-redemption/src/client/thingtypemanager.cpp
sources/otclient-redemption/src/client/thingtypemanager.h
sources/otclient-redemption/src/framework/graphics/image.cpp
```

Arquivos/pastas novos relevantes:

```text
docs/assets-workflow.md
server/data/spells/scripts/healing/light_heal_friend.lua
sources/opentibia_sprite_pack/
sources/remeres-map-editor/
sources/rme-otacademy/
tools/assets/
sources/otclient-redemption/modules/game_shaders/shaders/fragment/crisp_aa_test.frag
sources/otclient-redemption/modules/game_shaders/shaders/fragment/crisp_aa_strong_test.frag
sources/otclient-redemption/modules/game_shaders/shaders/fragment/crisp_aa_ultra_test.frag
sources/otclient-redemption/modules/game_shaders/shaders/fragment/crisp_aa_clarity_test.frag
```

## Decisoes de gameplay

Identidade do servidor:

- Oldschool/hibrido, nao copia cega de Tibia antigo.
- Low rate como foco principal.
- Pode existir high rate futuramente, mas balanceamento principal deve priorizar long-term progression.
- Client custom obrigatorio e desejavel para reduzir bots/clientes genericos.
- PvP precisa favorecer controle manual por WASD/setas, nao click map super rapido.
- Economia deve ser controlada desde cedo.
- Itens raros devem ser realmente raros, ja que muitas quests do global nao existirao ou serao refeitas.

Sistemas futuros desejados:

- Bestiary.
- Charm points.
- Enchanted spear.
- Skill nova `Alchemy` para money sink e controle de inflacao.
- Sistemas fortes de controle de inflacao.
- Spells compraveis, exceto `utevo lux` livre.
- Death penalty ajustada.
- Groundloot persistente em SQMs selecionados.
- Compensation auditavel.
- Sistema CAM/Tibiacam para auditoria, anticheat e marketing.
- Protecao contra disconnect contra criaturas.
- Logs fortes de mortes/perdas.
- Itens valiosos rastreaveis por identificador/log.
- DDoS/rate-limit/protecao de spam mais robustos futuramente.

## Cliente utilizado

Client atual:

```text
OTClient Redemption / Mehah
C:\tibia-oldschool\sources\otclient-redemption
```

Motivos para preferir o Redemption ao OTCv8 anterior:

- walking visualmente mais fluido;
- menos stutter grafico;
- WASD e Enter funcionam melhor;
- click map expoe a velocidade real sem o stutter do OTCv8;
- base mais promissora para render HD;
- efeitos/shaders e debug tools melhores;
- `Ctrl+Y` revelou varios efeitos/weather prontos, como chuva, neve, calor etc.

Pontos inferiores/pendentes:

- UI original nao e bonita; ideia futura e misturar/copiar visual do OTCv8 antigo mantendo funcionalidades do Redemption.
- Alguns delays do client ainda precisam ser monitorados.
- Follow/follow attack ainda precisa ser analisado quando houver outro player.
- Client pode estar usando Intel UHD 630 em vez da GTX 1660 Ti em alguns testes; verificar em Windows/NVIDIA settings para performance real.

## Sistema de movimentacao

Historico:

- OTCv8 anterior tinha stutter visual forte, principalmente em areia/chao lento/char rapido.
- Houve sincronia inicial entre client e server para reduzir delays.
- Foi testado throttle muito baixo, dash, filas e new walking formula.
- No OTCv8, algumas alteracoes melhoraram muito a areia, mas o stutter visual persistiu.
- A decisao foi trocar para OTClient Redemption.

Estado atual no Redemption:

- WASD/setas estao bons e fluidos.
- Click map estava rapido demais, chegando a ficar incontrolavel para PvP.
- Foi implementado/desenvolvido um atraso no auto-walk/click map para desestimular uso em PvP.
- Valor final escolhido para continuar: `AUTO_WALK_CONTINUE_DELAY_MS = 80ms`.
- Teste radical com `5000ms` confirmou que o controle estava funcionando.
- O delay de envio do client foi testado em valores baixos; depois foi ajustado para evitar excesso de pacotes.
- Em um momento o client deu disconnect por packet-per-second limit com velocidade muito alta/GM:
  - `127.0.0.1 disconnected for exceeding packet per second limit.`
- A solucao foi nao buscar 1ms em tudo, e sim um equilibrio.

Comportamento desejado:

- Movimento manual deve ser o padrao competitivo.
- Click map deve ser confortavel para caminhada longa, mas levemente inferior ao manual.
- Dash custom foi testado e removido/abandonado por nao resolver de forma limpa e poder causar imprecisao.
- Nao forcar passos extras quando o jogador solta a tecla, especialmente em level baixo.

Pontos a lembrar:

- Foi cogitado que servidores modernos usam fila/pre-walk de 2 ou 3 passos.
- No nosso caso, a fila/dash nao trouxe ganho perceptivel e podia prejudicar precisao.
- O click map continua como ferramenta de caminhada, nao de PvP.
- Follow/follow attack pode virar forma de andar mais rapido; precisa ser testado com dois players.

## Sistema de exaustao

Problema encontrado:

- Ao subir/descer escada ou mudar de andar, parecia aplicar exaust automatico.
- Nao deve existir exaust nova por trocar de andar.
- Deve permanecer apenas a exaust natural caso o jogador tenha acabado de usar magia/runa antes da troca de andar.

Estado:

- Problema foi investigado, mas nao virou foco final depois que a movimentacao geral ficou boa.
- Ainda deve ser revalidado em chars de level baixo, rampas/escadas e uso de runas logo apos floor change.

Runas e cooldowns:

- Cooldowns de runas/magias foram ajustados e testados em boa parte.
- Rune spells foram testadas e consideradas OK.
- Attack spells foram testadas e consideradas OK, exceto anotacoes especificas abaixo.
- Heal spells ainda nao foram todas testadas por causa de regen alta/dificuldade de simular dano.

Spells aprendidas:

- Futuramente, spells devem exigir compra/aprendizado.
- Excecao: `utevo lux` deve continuar disponivel.
- Nao implementar isso agora porque atrapalha testes.

## Bugs corrigidos ou melhorados

### Boot errors

No inicio havia muitos erros:

- NPC XML faltando.
- Monsters com spells desconhecidas.
- Warnings de spawns.
- Falta de NPCs.

Foi feita uma rodada de limpeza/adaptacao:

- boot passou a compilar/rodar mais limpo;
- alguns conteudos faltantes foram removidos/adaptados;
- ainda e necessario manter lista do que foi removido:
  - seguro;
  - precisa validar in game;
  - conteudo faltante.

### NPC shopmodule

Problema:

- NPC Yberius respondia somente a `buy backpack`.
- `trade`, `shop`, `offer` nao abriam/acionavam corretamente.
- Resposta do NPC tinha delay perceptivel.

Correcoes:

- Shopmodule foi ajustado para comandos `trade`, `shop`, `offer` e `buy`.
- Resposta do NPC ficou imediata.
- Testado comprando backpack.

Pendencia:

- Muitos NPCs/spawns ainda podem estar faltando ou incompletos.
- O mapa parecia quase sem NPCs.
- Revalidar NPCs cidade por cidade.

### Nome de monstros

Problema:

- Monstros apareciam com letra minuscula: `demon`, `rat`, `cave rat`.

Correcoes:

- Foi feita alteracao em massa para nomes com inicial maiuscula.
- Rat atual aparece como `Rat`.

### Follow de criaturas

Problema:

- Criaturas demoravam 1-2 segundos para perseguir o player apos encostar e o player se afastar.
- Dava para bater e correr 1 sqm repetidamente, evitando contato.

Correcoes:

- FollowCreature/AI foi ajustado.
- Usuario testou e reportou: "ficou perfeito o followcreature".

### Haste/Strong Haste de criaturas

Problemas:

- Monk estava sem haste.
- Giant Spider estava sem strong haste.
- Algumas criaturas com speed spells poderiam estar sem efeito visual.

Correcoes/testes:

- Monk haste adicionado.
- Giant Spider strong haste adicionado.
- Efeito visual de haste corrigido para aparecer.

Pendencia:

- Varredura de outras criaturas com haste/strong haste.
- Testar Orc Berserker e outras.
- Lista de testes inclui paralyze, speed, djinn electrify, skills/spells de monstros.

### Magias de monstros

Problema:

- Algumas spells de monstro vinham como desconhecidas, ex.: `paralyze_dipthrah`.

Decisao:

- Nao assumir que a spell deve ser removida so porque a base nao entende.
- Verificar se a spell existia/qual comportamento correto.

Estado:

- Paralyzes foram testados e considerados OK.
- Djinn electrify foi analisado: deve ser dano pontual tipo `exori vis`, nao dano constante.
- Usuario reportou target/area/frequencia/efeito/dano OK.

### Efeitos de energy/beam/wave

Problema:

- `exevo mort hur` estava com efeito de death/invalid effect id 38.
- Efeitos de energy beam/wave nao apareciam corretamente no caminho; so no alvo.
- `exori vis` tinha efeito diferente do beam e estava incorreto em um momento.

Correcoes:

- Foi feita varredura de efeitos.
- O efeito certo ficou proximo da animacao de teleport/energy logo a frente do char.
- Usuario testou `!fxscan 11,18` e identificou a primeira da esquerda.
- Efeito aplicado tambem ao `exori vis`.
- Usuario reportou: "ficou muito melhor do que eu esperava".

### Sangue/decay bug no stack/tile

Problema serio:

- Ao bater em rat no mesmo tile, bordas/ground eram cobertos visualmente pelo chao de baixo.
- Sangue parecia nao atualizar corretamente ate sair/voltar da tela.
- Decay 2019 -> 2020 -> 2021 -> 0 mostrava bug visual.
- Em tentativas de correcao houve erro temporario:
  - criaturas invisiveis ao `/m rat`;
  - client crashava ao sair/voltar;
  - erro `ProtocolGame::getThing: invalid thing id`.

Correcoes:

- Protocolo/stack handling foi ajustado.
- Bug final corrigido: sangue, creature e bordas passaram a funcionar.
- Usuario testou monstros e stack normal e reportou funcionando.

Riscos/observacoes:

- Mudancas podem quebrar clients 7.72 comuns, mas isso e ate desejado parcialmente contra bots.
- Testar com player real em tela futuramente.
- Tiles muito cheios costumam ocultar visualmente o primeiro item do stack; importante testar se nao ha perda real.
- Teste futuro anotado: macro jogando moedas infinitas em tile com um item marcador no inicio, para medir limite, crash e perda visual/real.

### Persistencia/rollback/anticlone

Problema:

- Logout nao salvava estado do player em algumas condicoes.
- Fechar/matar TFS podia gerar clone player/house:
  - item no inventario salvo;
  - item movido para house;
  - crash antes de salvar ambos;
  - depois item reaparecia no player e ficava na house.

Correcoes:

- Implementada persistencia local mais robusta em movimentos entre player/house.
- Testes matando task indicaram:
  - em um cenario inicial houve clone;
  - depois o item voltou para o player e sumiu da house, correto;
  - varios testes anti-clone passaram.
- Logs `Persistence` apareciam no TFS.

Decisoes:

- Nao salvar globalmente a cada movimento entre player/house porque seria pesado.
- Preferir salvar escopo afetado/transacao pequena.
- Trade ficou anotado para depois.
- Groundloot persistente tera apenas SQMs selecionados por hunt, nao tudo do mapa.

Pendencias:

- Testar clone via depot.
- Testar trade.
- Definir arquitetura de persistencia para groundloot persistente.
- Considerar banco separado para groundloot/itens persistentes.
- Implementar/definir rollback curto.
- Implementar logs de morte/perda/itens raros.

## Estado de teste atual do Rat

`C:\tibia-oldschool\server\data\monster\rat.xml` esta alterado para laboratorio, nao para producao:

```xml
<health now="10000" max="10000"/>
<attack name="melee" min="-500" max="-500"/>
<defenses armor="200" defense="100"/>
```

Motivo:

- Testar armor/shield/distance com dano fixo e defesa alta.

Antes de qualquer teste real de hunting/loot, voltar Rat para valores normais.

## Formulas atuais

Regra geral definida:

- Arredondar tudo para baixo (`floor`).
- Nao usar `ceil` ou `round` aproximado em formulas novas.
- Mage, knight, paladin e defense seguem isso nas formulas novas.

### Mage

As formulas de mage foram ajustadas para feeling oldschool, com base comum:

Arquivo:

```text
C:\tibia-oldschool\server\data\spells\lib\spells.lua
```

Funcoes:

```lua
function getOldschoolMageBase(level, magicLevel)
    return math.floor((level / 5) + (magicLevel * 0.3))
end

function getOldschoolMageRange(level, magicLevel, minFactor, maxFactor)
    local base = getOldschoolMageBase(level, magicLevel)
    return math.floor(base * minFactor), math.floor(base * maxFactor)
end

function getOldschoolMageFixed(level, magicLevel, factor)
    return math.floor(getOldschoolMageBase(level, magicLevel) * factor)
end
```

Testes:

- Mage formulas foram testadas com GM/Lancellin em level 100 e ML 60.
- Rune spells: todas OK.
- Attack spells: todas OK.
- Healing spells: ainda nao completamente testadas; confiar parcialmente ate testar druid.
- `exevo gran mas pox` esta anotado como errado e deve ser revisado futuramente.

Importante:

- Dano de runas/magias nao deve depender de vocation, apenas de level/magic level e requisito minimo da spell/runa.
- Knight/paladin devem bater igual com runa se tiverem ML minimo exigido.

### Knight melee

Arquivo:

```text
C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\weapons.cpp
```

Formula nova so para armas com `attackValue > 16`.

Formula de max:

```text
max = floor(((level / 5) + (atk * 1.5) + (((skill * skill) / 1620) * atk)) * attackModeFactor)
```

Formula de min:

```text
min = floor(((level / 4) + (maxBase * 0.18)) * attackModeFactor)
```

Onde:

```text
full attack  -> attackModeFactor = 1.0
balanced     -> attackModeFactor = 0.75
full defense -> attackModeFactor = 0.5
```

Observacao:

- Armas de treino/fracas devem usar formula antiga para nao confundir treino.
- Limiar escolhido: armas acima de mace; na implementacao atual, o codigo usa `attackValue > 16`.

Testes:

- Lancellin/knight com skill alto testado.
- Stonecutter/atk 50 em skill 180 fez hits na faixa esperada.
- Mace atk 16 caiu na formula antiga, como desejado.
- Usuario considerou formulas de knight OK.

### Paladin / Distance

Arquivo:

```text
C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\weapons.cpp
```

Formula nova para distance com `attackValue > 10`.

Max:

```text
max = floor(((level / 4) + 10 + atk * (skill / 15 + skill^(3/2) / 3100)) * attackModeFactor)
```

Min:

```text
min = floor(((level / 3) + atk) * attackModeFactor)
```

Attack mode:

```text
full attack  -> 1.0
balanced     -> 0.75
full defense -> 0.5
```

Armas com `attackValue <= 10` usam formula antiga, sem ponto flutuante novo, para nao atrapalhar itens fracos/treino.

Distance:

- Ignora shield/defense.
- Deve ser bloqueado apenas por armor, tanto PvP quanto PvE.
- Teste com rat shield 100 indicou que paladin/distance ignorava shield corretamente.
- Teste com rat armor 200 indicou armor funcionando corretamente.

Chance de acerto:

- O codigo usa maxHitChance e distancia.
- Lanca/spear pode ter chance diferente de arrows/bolts por maxHitChance e distancia.
- Hunting spear ficou anotada para depois remover/comentar se nao for usada no servidor.

### Armor

Arquivo:

```text
C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\player.cpp
```

Formula nova:

```text
minBlock = floor(TotalArmorValue^(4/3) * 0.24)
maxBlock = floor(TotalArmorValue^(3/2) * 0.22)
```

Se armor <= 0:

```text
block = 0
```

Armor inclui:

- helmet;
- necklace;
- armor;
- legs;
- boots;
- ring.

Teste:

- Armor 0 com crossbow deu dano cravado 500 do rat.
- Armor 25 ficou bloqueando faixa esperada.
- Armor 45 junto com shield mediano foi testado e pareceu OK.
- Usuario considerou armor perfeita.

### Shield / Defense

Arquivo:

```text
C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\player.cpp
```

Decisao:

- Formula nova de shield/defense funciona apenas:
  - contra criaturas;
  - em full defense;
  - para knight e mage;
  - summons podem entrar nessa regra futuramente.
- Paladin usa formula antiga de defesa.
- PvP usa formula antiga.
- Balanced/full attack usam formula antiga.

Formula custom:

```text
maxBlock = floor(defenseValue * ((defenseSkill * defenseSkill) / 800) * vocationFactor + 10)
block = random(0, maxBlock)
```

Fatores:

```text
Knight / Elite Knight: 1.5
Sorcerer / Master Sorcerer / Druid / Elder Druid: 1.3
Paladin / Royal Paladin: formula antiga, nao custom
```

Logica shield vs arma:

- Se houver escudo de verdade equipado, defesa da arma deve ser ignorada.
- Se nao houver escudo, defesa da arma conta.
- Tocha/offhand sem defesa real nao deve anular defesa da arma.
- Sem shield/arma, defesa base fica em 5 em alguns caminhos antigos.

Implementacao atual:

- `getShieldAndWeapon()` identifica shield e weapon.
- `isShieldDefenseItem()` considera item com defense > 0 e weaponType shield/none como shield-like.
- Pode precisar revisar se algum item `WEAPON_NONE` com defense causa falso positivo como shield.

Testes:

- Wooden shield def 14 full def funcionou pela formula antiga/esperada no paladin/mage conforme cenario.
- Dwarven shield def 26, demon shield def 35, armor 45 foram testados.
- Skill 180 gerou blocks enormes, explicado por skill editada.
- Com skill 90, valores ficaram mais razoaveis.

## Spells e ajustes pontuais

### Light Heal Friend

Novo arquivo:

```text
C:\tibia-oldschool\server\data\spells\scripts\healing\light_heal_friend.lua
```

Spell:

```xml
<instant name="Light Heal Friend" words="exana sio" mana="30" maglv="25" ...>
    <vocation name="Elder Druid" />
</instant>
```

Decisao:

- Mana: 30.
- ML necessario: 25.
- Apenas Elder Druid.

### Heal Friend

`exura sio` continua:

- mana 70;
- maglv 7;
- Druid e Elder Druid.

### Conjure Power Bolt

Arquivo:

```text
C:\tibia-oldschool\server\data\spells\scripts\conjuring\conjure_power_bolt.lua
```

Decisao:

- `exevo con vis` deve conjurar somente 1 power bolt.
- Mana: 200.
- Royal Paladin.

Implementacao atual:

```lua
return creature:conjureItem(0, 2547, 1, CONST_ME_MAGIC_BLUE)
```

## Death penalty

Estado:

- Morte foi testada.
- Usuario caiu de level 300 para 296 e achou dolorido demais.

Decisao futura:

- Alterar percentuais de perda.
- Compensation auditavel deve poder restaurar level, skill, bless e AOL quando morte por bug for confirmada.

## Loot e monster XML

Chance de loot:

- Nos XMLs do TFS, chance aparenta usar base 100000.
- Exemplo: `30000` = 30%.
- Confirmar com teste real de loot em amostra maior.

Voices:

- Foi feita varredura para reduzir spam.
- Criaturas que falam agora devem usar:

```xml
<voices interval="10000" chance="10">
```

Motivo:

- Evitar fala excessiva em massa.
- Possivel ganho pequeno de performance/ruido.

Pendencia:

- Validar se todas as criaturas com voices foram padronizadas.
- Validar se interval alto nao deixa criaturas silenciosas demais.

## Persistencia e groundloot

Direcao:

- Nao salvar todo mapa sempre.
- Groundloot persistente deve usar SQMs selecionados por hunt.
- Cidade limpa no server save, exceto houses.
- Fora das cidades, alguns tiles/zonas persistem ate limpeza semanal.
- Clean semanal para evitar acumulo de lixo.

Risco principal:

- Clone de item por diferenca de save entre player/house/depot/ground/trade.

Regras futuras:

- Itens valiosos devem ter rastreabilidade.
- Itens dropados por morte podem receber marca/log para saber onde foram parar.
- Evitar duplicar item em compensation.

Pendencias:

- Trade anti-clone.
- Depot anti-clone.
- Groundloot persistente.
- Logs de item raro.
- Rollback curto.
- Save global periodico talvez 1h, mas combinado com saves transacionais.

## Renderizador e sistema HD

Objetivo:

- Criar modo grafico HD opcional, mantendo grafico original para quem preferir ou tiver PC fraco.
- Inspiracao visual: Miracle/RubinOT, mas sem copiar client/arte fechada.
- Ideia: sprites mais nitidas/desenhadas, luz obrigatoria, contraste melhor, sem "oculos embacados".

Estado:

- OTClient Redemption foi alterado para aceitar `Tibia.cwm` como override parcial de sprites.
- O client carrega `Tibia.spr` normal e, quando HD esta ativo, carrega tambem `Tibia.cwm`.
- O `.cwm` nao substitui tudo; ele sobrescreve somente sprite IDs presentes nele.
- Se sprite nao existe no `.cwm`, usa o sprite original do `.spr`.
- O toggle HD nas options funciona sem crash.

Arquivos principais:

```text
C:\tibia-oldschool\sources\otclient-redemption\src\client\spritemanager.cpp
C:\tibia-oldschool\sources\otclient-redemption\src\client\spritemanager.h
C:\tibia-oldschool\sources\otclient-redemption\src\client\thingtype.cpp
C:\tibia-oldschool\sources\otclient-redemption\src\client\thingtype.h
C:\tibia-oldschool\sources\otclient-redemption\src\client\thingtypemanager.cpp
C:\tibia-oldschool\sources\otclient-redemption\src\client\thingtypemanager.h
C:\tibia-oldschool\sources\otclient-redemption\src\client\luafunctions.cpp
C:\tibia-oldschool\sources\otclient-redemption\modules\client_options\data_options.lua
C:\tibia-oldschool\sources\otclient-redemption\modules\client_options\styles\graphics\graphics.otui
```

Correcoes feitas:

- Adicionado `g_sprites.reload` no Lua binding.
- Adicionado `g_things.unloadTextures()` para forcar recarregamento de texturas ao alternar HD.
- Adicionado guard contra sprite ID invalido/negativo.
- Resolvido erro:

```text
attempt to call field 'reload' (a nil value)
```

- Resolvidos logs de `Failed to get sprite id` para IDs invalidos.

## 64x64 / 128x128

Historico dos testes:

- Primeiro teste trocou sprites 32x32 por imagens upscaled/downscaled.
- Melhorou o visual, mas ainda havia divisao entre tiles e sensacao de borrado.
- Teste errado com sprite grande causou zoom por tile.
- Depois o renderizador foi ajustado para projetar fonte HD dentro do footprint classico do SQM.
- O teste atual com HD ativo funciona corretamente.

Estado atual:

- Teste ativo aceitou sprites HD reais em `Tibia.cwm`.
- O client desenha fonte maior no espaco classico do SQM.
- O usuario perguntou se estava vendo 64x64 ou 128x128:
  - o teste funcional inicial era 64x64 real para o pavimento da cidade;
  - 128x128 ainda e caminho futuro/experimental.

Importante:

- 64x64 e 128x128 nao devem virar o tamanho logico do tile no mapa.
- O tile continua sendo 32x32 em coordenadas/gameplay.
- A arte HD e renderizada/projetada visualmente no footprint do tile.
- 128x128 deve consumir mais VRAM/memoria/tempo de upload e precisa ser testado com muitas sprites.

Meta:

- Manter opcao `HD Graphics` ou equivalente.
- Grafico normal deve continuar disponivel.
- HD pode ser 64x64 primeiro.
- 128x128 pode ser testado em lote pequeno antes de tentar massa.

## Shaders

Shaders adicionados:

```text
C:\tibia-oldschool\sources\otclient-redemption\modules\game_shaders\shaders\fragment\crisp_aa_test.frag
C:\tibia-oldschool\sources\otclient-redemption\modules\game_shaders\shaders\fragment\crisp_aa_strong_test.frag
C:\tibia-oldschool\sources\otclient-redemption\modules\game_shaders\shaders\fragment\crisp_aa_ultra_test.frag
C:\tibia-oldschool\sources\otclient-redemption\modules\game_shaders\shaders\fragment\crisp_aa_clarity_test.frag
```

Registrados em:

```text
C:\tibia-oldschool\sources\otclient-redemption\modules\game_shaders\shaders.lua
```

Testes:

- `None`: nitido mas pixelado/cerrilhado.
- `Smooth Retro`: menos serrilhado, mas sensacao de oculos embacado.
- `Antialiasing`: melhora pouco e borra.
- `Crisp AA Clarity`: contraste bom; usuario gostou do contraste anterior, sem exagerar.
- `Crisp AA Ultra`: junto com Smooth Retro ficou interessante em alguns testes.

Decisao atual:

- Manter opcoes default para comparativo.
- Nao depender so de shader para atingir qualidade Miracle/RubinOT.
- O salto real precisa vir dos assets HD.

## Pipeline de assets

Docs:

```text
C:\tibia-oldschool\docs\assets-workflow.md
```

Ferramentas:

```text
C:\tibia-oldschool\tools\assets\extract_sprites.py
C:\tibia-oldschool\tools\assets\build_cwm.py
C:\tibia-oldschool\tools\assets\tile_mosaic.py
C:\tibia-oldschool\tools\assets\resize_images.py
C:\tibia-oldschool\tools\assets\slice_sheet.py
C:\tibia-oldschool\tools\assets\extract_thing_assets.py
```

Comando no client para descobrir sprites:

```lua
asset_info 724
asset_info 103
```

O comando mostra:

- item/thing id;
- tamanho;
- layers;
- patterns;
- frames;
- sprite IDs;
- comando de extracao.

Pasta de batch atual:

```text
C:\tibia-oldschool\tools\assets\work\batch-city-pass-01
```

IDs extraidos no batch:

```text
103
4536
4535
4527
4515
4526
4518
4531
2109
2597
```

Arquivos de entrada para Upscayl:

```text
C:\tibia-oldschool\tools\assets\tests\tibia-sprites\inputs\batch-city-pass-01
```

Itens/pastas importantes:

```text
item-103      dirt/chao, sprites 536-547, tem mosaic-input.png
item-4536     grama, tem mosaic-input.png
item-4535     grama, tem mosaic-input.png
item-4527     grama, tem mosaic-input.png
item-4515     grama, tem mosaic-input.png
item-4526     grama, tem mosaic-input.png
item-4518     grama, tem mosaic-input.png
item-4531     grama, tem mosaic-input.png
item-2109     poste da cidade, sheet.png, sem mosaic
item-2597     blackboard, sheet.png, sem mosaic
item-870      pavimento/city floor testado, sprites 589-596
item-724      outro ID do chao da cidade, item com layers/patterns/frames
```

Workflow de piso/grama:

1. Usar `mosaic-input.png` como entrada da IA/upscale.
2. Fazer upscale do mosaico inteiro, nao sprite por sprite.
3. Recortar de volta para PNGs numericos por sprite ID.
4. Rodar `build_cwm.py`.
5. Copiar/gerar `Tibia.cwm` em `data\things\772`.
6. Abrir client, ligar HD graphics e testar.

Observacao sobre mosaico:

- O mosaico ajuda a IA a enxergar continuidade entre tiles e reduz marcas de divisao.
- Ainda e necessario recortar de volta, pois `Tibia.cwm` espera arquivos `589.png`, `590.png` etc.

Workflow de objetos como poste/blackboard:

- `sheet.png` serve para preview/referencia.
- Para empacotar final, ainda precisam existir PNGs individuais por sprite ID.
- Pode ser mais facil fazer upscale dos PNGs numericos individuais ou criar um slicer automatico.

Upscayl:

- Instalado/testado em:

```text
C:\tibia-oldschool\tools\assets\tests\tibia-sprites\upscayl
```

- GPU detectadas:
  - Intel UHD 630
  - NVIDIA GTX 1660 Ti
- Backend Vulkan do Upscayl reconheceu GTX 1660 Ti como GPU 1.
- Usar `-g 1` quando rodar backend direto para garantir NVIDIA.

Hipotese visual:

- 4x seguido de downscale 50% com Lanczos pode melhorar qualidade final 64x64.
- Para 128x128, 4x direto a partir de 32x32 gera 128.
- Testar pequenos batches antes de massa.

## Luzes e clima

Decisao:

- Luz ligada deve ser obrigatoria no client.
- Remover/bloquear opcao de desligar luz futuramente.
- GM atualmente pode nao mostrar efeito de luz igual player normal, entao comparar com personagem comum.

Ctrl+Y:

- O client possui efeitos prontos interessantes:
  - chuva;
  - neve;
  - calor;
  - outros efeitos visuais.
- Registrar para implementar depois como sistemas visuais/eventos.

## Conteudo, mapa e RME

Mapa:

- Mapa parece enorme e pouco usado; possivelmente menos de 10% utilizado.
- Reduzir/remover areas vazias pode melhorar organizacao e possivelmente carregamento, mas nao necessariamente walking.
- Nao fazer reducao automatica sem backup e sem entender referencias/spawns/houses.

RME:

- Clonado/compilado.
- Inicialmente houve erro de OTBM/itens:
  - "Unsupported items.otb version (version 3)"
  - "Client directory is not a valid path"
- Ajustado para abrir.

Pendencias:

- Usar map editor para validar hunts/spawns.
- Prioridade: verificar hunts de Venore/dragon, spawns, NPCs, rotas, depot/templo.
- Aprender melhor RME.

## Testes ja realizados

Movimentacao:

- Areia ID 231 melhorou muito no OTCv8 apos sincronizar delays.
- Redemption resolveu melhor stutter visual geral.
- Click map desacelerado para 80ms.
- GM/level muito alto pode gerar preto na borda em velocidade absurda, mas em level 500 ja quase nao aparece.

Combat:

- Mage damage OK.
- Knight damage OK.
- Paladin damage OK.
- Armor OK.
- Shield OK no escopo testado.
- Distance ignora shield de criatura em teste.
- Distance sofre armor de criatura em teste.

Spells:

- Rune spells OK.
- Attack spells OK.
- Cura ainda precisa teste completo com Druid.
- `exevo gran mas pox` anotado como errado.
- `exevo con vis` corrigido para 1 power bolt.
- `exana sio` criado/ajustado.

Persistence:

- Testes anti-clone player/house OK apos ajustes.
- Trade/depot pendentes.

Render:

- HD toggle funciona.
- CWM override parcial funciona.
- City pavement 870 HD testado.
- Client nao crasha no teste atual.

## Personagens/testes

Conta de teste:

- Existe conta/personagens locais usados no projeto.
- Nao depender do historico: verificar `PASSWORDALL.txt` e banco `oldschool772db` para credenciais/personagens.

Personagens citados:

- `Lancellin` / `GM Lancellin`: usado para testes, pode estar editado.
- `Lance Mage`: mage level 100/ML 60 usado em testes.
- `Druid`: criado para testar curas, level 100/ML 60, HP aumentado para 2000.

Edicoes feitas durante testes:

- Lancellin foi colocado em level alto, skill 180, depois usado para formulas.
- Lancellin/GM tambem foi usado em level 100/ML 60 para testar spells.
- Rat esta alterado para bater 500 e ter defesa/armor alta.

Antes de qualquer teste real de gameplay, revisar estado do banco/personagens e `rat.xml`.

## Decisoes de seguranca/protecao

Nao implementado ainda, mas discutido:

- DDoS protection.
- Rate limiting mais robusto.
- Protecao contra spam que crasha servidor.
- Client custom obrigatorio.
- Criptografia/obfuscacao do client futuramente.
- Log forte de morte/perda.
- CAM para auditoria.
- Compensation auditavel.
- Itens raros rastreaveis.

Observacao:

- Um client 7.72 comum poder quebrar ao conectar nao e necessariamente ruim; pode atrapalhar bots prontos.

## Limitacoes conhecidas

Tecnicas:

- Muitas mudancas locais ainda nao commitadas.
- O documento `docs/assets-workflow.md` pode estar com encoding quebrado em acentos; nao e critico.
- `server/tfs.exe` esta modificado/compilado localmente.
- Alguns arquivos grandes estao fora do GitHub por `.gitignore`.
- Client pode usar GPU Intel em vez da NVIDIA se Windows escolher assim.
- 128x128 HD ainda nao validado em massa.
- `Tibia.cwm` atual e experimental.

Gameplay:

- NPCs ainda incompletos.
- Spawns ainda incompletos/alguns warnings.
- Boot errors antigos foram reduzidos, mas conteudo faltante precisa catalogo.
- Loot precisa validacao real.
- Death penalty precisa ajuste.
- Spells aprendidas ainda nao implementadas.
- Follow/follow attack contra players ainda precisa teste.
- Depot/trade anti-clone pendente.
- Groundloot persistente pendente.
- Bestiary/charm/alchemy pendentes.

Assets:

- Upscale via IA pode criar artefatos.
- Pisos precisam continuidade; usar mosaicos.
- Objetos precisam preservar transparencia, offsets e footprint.
- 64x64 parece caminho inicial mais seguro.
- 128x128 pode ficar mais bonito, mas precisa medir FPS/VRAM.

## Proximo passo recomendado

Como a ultima frente ativa era assets HD:

1. Finalizar pipeline automatico para recortar mosaicos upscaled de volta em PNGs numericos.
2. Gerar `Tibia.cwm` combinando:
   - item 870 city floor ja testado;
   - dirt 103;
   - gramas 4536/4535/4527/4515/4526/4518/4531;
   - poste 2109;
   - blackboard 2597.
3. Testar com HD on/off no client.
4. Se ficar estavel, documentar o fluxo final no `docs/assets-workflow.md`.
5. Depois voltar para gameplay:
   - restaurar rat para normal ou criar monstro `Training Rat`;
   - testar curas com Druid;
   - revisar `exevo gran mas pox`;
   - iniciar validacao de Venore/dragon hunt/NPCs.

## Regras para a proxima conversa/Codex

- Sempre ler `git status` antes de editar.
- Nao reverter arquivos sem autorizacao.
- Tratar `rat.xml` como estado de laboratorio.
- Se mexer no client, recompilar `otclient.exe` e copiar/usar o exe correto.
- Se mexer no TFS, recompilar e copiar `tfs.exe` para `server`.
- Evitar grandes refactors.
- Antes de alterar formulas, confirmar exemplos numericos com o usuario.
- Para assets, trabalhar em batch pequeno e testar visualmente.
- Para qualquer mudanca em persistencia/anticlone, pensar primeiro em cenarios de duplicacao.
- Priorizar sistemas que evitam retrabalho futuro: client/render, persistencia, formulas, map editor.

