# CLIENT_PERFORMANCE_CHANGELOG

Registro cronologico das investigacoes, decisoes, testes e correcoes
relacionadas a performance do OTClient Redemption em `D:\tibia-oldschool`.

Este documento possui uma versao equivalente em ingles:
`CLIENT_PERFORMANCE_CHANGELOG_EN.md`.

## Atualizacao De 3 De Julho De 2026

### Escopo

Esta atualizacao registra a primeira correcao aplicada durante a auditoria de
performance do client, com foco em reduzir microstutters sem remover recursos
visuais importantes do jogo.

O alvo desta etapa foi o sistema de luz do mapa. A luz deve permanecer ativa,
pois o servidor usa efeitos visuais de RPG como tochas, magias de luz e luz
ambiente por andar.

### Diagnostico

Foi identificado que o `LightView` recalculava pixels do lightmap com base em
um hash que incluia o `src` visual da camera:

- `sources/otclient-redemption/src/client/lightview.cpp`
- `sources/otclient-redemption/src/client/mapview.cpp`

Durante o smooth walk, o `src` muda por causa do deslocamento visual da camera.
Isso podia invalidar o lightmap mesmo quando nenhuma fonte real de luz havia
mudado.

O comportamento era seguro visualmente, mas caro: o client podia recalcular os
pixels da luz apenas porque a camera se deslocou visualmente.

### Correcao Implementada

Foi separada a atualizacao de coordenadas da atualizacao de pixels:

- `updateCoords(dest, src)` continua sendo executado para manter o recorte da
  luz alinhado com a camera.
- `updatePixels()` passou a depender de um hash de conteudo real da luz.
- O hash de conteudo inclui tamanho do lightmap, tile size, luz global, fontes
  de luz, sombras e dados relevantes dos tiles de luz.
- O `src` visual da camera deixou de forcar recalculo de pixels no modo novo.
- Resize do lightmap continua forcando atualizacao imediata.

Arquivos principais alterados:

- `sources/otclient-redemption/src/client/lightview.cpp`
- `sources/otclient-redemption/src/client/lightview.h`

### Rollback

A alteracao foi deixada com rollback simples em:

`sources/otclient-redemption/src/client/lightview.cpp`

Constante:

`LIGHTVIEW_CONTENT_CACHE_ENABLED`

Com `true`, o client usa o cache por conteudo de luz.
Com `false`, o client volta ao comportamento antigo baseado tambem no `src` da
camera, mantendo os logs de performance.

### Logs De Performance

Foram adicionados logs com a tag:

`[LightViewPerf]`

Os logs registram:

- modo ativo;
- quantidade de frames analisados;
- quantidade de `pixelUpdates`;
- quantidade de `pixelSkips`;
- quantidade de `coordUpdates`;
- quantidade de luzes no ultimo frame;
- quantidade de sombras no ultimo frame;
- tamanho do lightmap;
- tile size;
- `src` visual usado no frame.

Exemplo:

`[LightViewPerf] mode=content frames=720 pixelUpdates=0 pixelSkips=720 coordUpdates=0 lastLights=2 lastShades=252 map=18x14 tileSize=64 src=(64,64 959x704)`

### Resultados Observados

Nos testes iniciais, o comportamento visual permaneceu correto e nenhum bug
visual foi observado.

O ganho foi maior quando o personagem estava parado ou andando lentamente:

- parado, o client passou a evitar quase todos os `updatePixels`;
- andando lentamente, os logs mostraram reducao relevante de recalculos;
- andando com personagem artificialmente muito rapido e luz presa ao player, os
  `pixelUpdates` continuaram altos, como esperado, porque a propria fonte de
  luz muda de posicao quase todo frame.

Foi confirmado que `pixelUpdates` acompanha `coordUpdates` nos cenarios em que a
luz esta presa ao personagem. Esse comportamento e esperado no modelo atual,
pois a posicao real da fonte de luz muda durante o smooth walk.

### Decisoes

- Nao desativar luz do client.
- Nao aplicar throttle temporal de luz por `ms` neste momento.
- Nao classificar tocha equipada ou `utevo lux` como luz estatica, pois ambas
  acompanham o personagem.
- Manter a implementacao atual por ser segura, pequena e reversivel.
- Investigar uma separacao futura entre luz estatica e luz dinamica somente se
  novas medicoes mostrarem necessidade real.

### Validacao

O client foi recompilado com sucesso usando:

`cmake --build build-validation\otclient --config RelWithDebInfo --target otclient --parallel 8`

O executavel validado foi copiado para:

`sources/otclient-redemption/otclient.exe`

Hash SHA256 registrado no momento da build:

`5229583912ED70A86CA4056143E4A48B80410F75D8D7B362549101C3A41AEF27`

## Atualizacao De 3 De Julho De 2026 - Battle List

### Escopo

Esta atualizacao registra uma otimizacao pequena e localizada na Battle List do
client, focada no caso em que a lista esta ordenada por distancia.

O objetivo foi reduzir trabalho repetido durante movimento, sem alterar eventos
criticos da lista e sem tornar a UI perceptivelmente lenta.

### Diagnostico

Foi identificado que `onCreaturePositionChange` em:

`sources/otclient-redemption/modules/game_battle/battle.lua`

executava trabalho pesado quando a Battle List estava ordenada por distancia:

- recalculo de distancia para criaturas na lista;
- ordenacao da `binaryTree`;
- chamada de `correctBattleButtons`;
- ajustes de visibilidade de widgets.

Esse caminho podia executar com muita frequencia durante movimento do player ou
de criaturas, principalmente em areas com muitas criaturas visiveis.

### Correcao Implementada

Foi adicionado um batch simples para atualizacoes de distancia:

- com menos de 10 criaturas, a atualizacao por distancia e agrupada em `70ms`;
- com 10 ou mais criaturas, a atualizacao por distancia e agrupada em `120ms`;
- chamadas repetidas durante a janela do batch apenas incrementam um contador
  pendente;
- quando o batch executa, as distancias sao recalculadas uma vez, a lista e
  ordenada uma vez e os widgets sao corrigidos uma vez.

Eventos criticos continuam imediatos:

- criatura aparecer;
- criatura desaparecer;
- filtros e ordenacao alterados pelo jogador;
- troca de andar do player;
- rebuild completo da Battle List.

### Logs De Performance

Foi adicionada a tag:

`[BattleListPerf]`

O log agregado registra, a cada janela de performance:

- quantidade de atualizacoes agendadas;
- quantidade de batches executados;
- quantidade de ordenacoes;
- quantidade de chamadas de `correctBattleButtons`;
- quantidade de batches pequenos e grandes;
- quantidade de eventos de movimento agrupados;
- tempo acumulado gasto nos batches.

### Validacao

A sintaxe Lua foi validada com:

`tools\dependencies\otclient-vcpkg-installed\x64-windows-static\tools\luajit\luajit.exe -b sources\otclient-redemption\modules\game_battle\battle.lua NUL`

### Decisoes

- Nao aplicar atraso em modos de ordenacao que nao dependem de distancia.
- Nao mexer nos eventos criticos da Battle List.
- Usar `70ms` e `120ms` por serem valores conservadores para UI.
- Manter logs agregados em vez de logar cada batch, evitando custo extra de I/O.
