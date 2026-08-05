# SPRITES_CHANGELOG

Registro cronologico das alteracoes, testes, decisoes e pendencias do fluxo de sprites HD em `D:\tibia-oldschool`.

## Resumo Executivo

O trabalho de sprites HD evoluiu de testes pontuais de mosaico para um processo de producao por familias de sprites, com tres caminhos principais:

- sprites isoladas sem continuidade, tratadas com upscale direto;
- mosaicos de terreno com continuidade simples ou borda;
- atlases de continuidade para paredes, fachadas, piramides e estruturas com mais de um andar.

Ao longo do processo, ficou claro que a qualidade nao dependia apenas do upscale. O fator determinante passou a ser o tipo de continuidade visual da sprite:

- tile unico sem borda nem transicao podia ser tratado como candidato simples;
- tiles com borda, quina ou alinhamento entre repeticoes exigiam mosaico;
- paredes e construcoes com `Z` precisavam de atlas com projecao vertical;
- sprites com multiplos contextos compartilhando os mesmos sprite IDs exigiam selecao de recorte por familia.

O fluxo consolidado ficou assim:

1. identificar o `Client ID`;
2. estudar a continuidade da sprite;
3. montar mosaico ou atlas adequado;
4. fazer upscale;
5. recortar e restaurar alpha original;
6. subir para teste no `Tibia.cwm`;
7. validar no cliente;
8. aprovar, reverter ou ajustar;
9. copiar o resultado aprovado para `Sprites Permanentes`.

## Fase 1. Entendimento Da Base E Das Ferramentas

### Leitura de docs e inspecao do workspace

- Foram analisados os docs em `D:\tibia-oldschool\docs`.
- Tambem foi inspecionada a pasta `D:\tibia-oldschool\tools\assets`.
- A logica de mosaico foi associada ao comportamento do RME.
- Foi percebido que o problema central nao era o mosaico simples, mas a continuidade nas bordas e nos encaixes entre tiles.

### Ferramentas usadas

- `Upscayl` para gerar upscale 2x e, em casos especificos, 4x.
- Scripts locais em `D:\tibia-oldschool\tools\assets\`.
- Leitura de `Tibia.dat`, `Tibia.spr` e `Tibia.cwm`.
- Recorte e recomposicao de PNGs.
- Merge de overlays parciais em `Tibia.cwm`.
- Inspecao visual no RME.
- Validacao final com prints do cliente em jogo.

### Regras operacionais consolidadas

- O processo sempre trabalhou com `Client ID`, nao com `Server ID`.
- Itens aprovados foram copiados para `D:\tibia-oldschool\backup-extras\Sprites Permanentes`.
- O client nao precisava ser reiniciado para a maioria das mudancas de `CWM`; a troca era valida sem restart.
- Se a alteracao envolvia o `dat`, ai sim o comportamento podia exigir mais cuidado.
- Em conteudo de parede, o `Z` passou a ser tratado como parte da continuidade, nao como detalhe opcional.

## Fase 2. Primeiros Testes Em Mosaicos E Terrenos

### Chao e mosaicos simples

- Foram feitos testes em sprites de chao sem borda.
- A primeira validacao veio de mosaicos simples, usados como prova de que o pipeline de input, upscale, corte e teste funcionava.
- A partir disso, varios CIDs de terreno foram processados e aprovados.

### Decisoes tomadas nessa fase

- Tiles sem continuidade podiam seguir para upscale direto.
- Tiles com continuidade precisavam de atlas ou mosaico, dependendo do tipo de encaixe.
- Quando o mosaico estava correto, o resultado podia ir direto para permanentes.

### Problemas observados

- Algumas bordas ficaram invertidas.
- Algumas quinas apareceram em orientacao errada.
- Em certos terrenos, a parte externa era mais delicada do que o miolo.
- O usuario precisou validar visualmente se o encaixe final estava mesmo correto no cliente.

## Fase 3. Conversao De Sprites Antigas Do Servidor

### Trocas de IDs antigos por sprites do 7.4

- Foi identificado que parte das sprites de `7.72` estava visualmente moderna demais para o pacote classico.
- Em vez de criar novos IDs, foi feito um mapeamento de substituicao dentro dos IDs antigos do servidor.
- O objetivo foi manter os numeros antigos e trocar apenas o conteudo visual.

### Substituicoes registradas

- `351` foi substituida pela sprite `461`.
- `352` foi substituida pela sprite `462`.
- `353` foi substituida pela sprite `463`.
- `354` foi substituida pela sprite `464`.
- `355` foi substituida pela sprite `465`.
- `386` foi substituida pela sprite `530`.

### Observacoes tecnicas

- Essas trocas foram aplicadas dentro das sprites do servidor, sem relacao com o upscale HD.
- O criterio foi preservar o ID numerico original.
- A alteracao serviu para alinhar o visual do servidor com o estilo classico.

## Fase 4. Mosaicos E Bordas Especificas

### Terreno de cavernas e bordas relacionadas

- Foi validada uma logica de mosaico para o chao das cavernas.
- O centro do mosaico continuava simples, mas o problema estava nas bordas e nos complementos.
- O trabalho incluiu teste com bordas, sincronizacao do chao central e correcao de quinas.

### Resultados e conclusoes

- Em alguns casos, o mosaico saiu invertido e precisou de correcao.
- Em outros, o usuario aprovou mesmo com pequenas imperfeicoes herdadas da sprite original.
- Ficou consolidado que a sprite pode parecer correta em mosaico e ainda assim falhar quando o contorno de borda nao acompanha a continuidade real.

## Fase 5. Paredes E Atlases De Continuidade

### Mudanca de abordagem

- Foi percebido que varias paredes nao deveriam ser tratadas como um mosaico grande qualquer.
- A melhor leitura passou a ser a de uma parede composta por segmentos conectados, as vezes com um andar sobre o outro.
- Isso levou ao uso de atlas de continuidade em vez de simples repeat de tile.

### Famlias e familias auxiliares

- Foram trabalhadas paredes de pedra, paredes de areas tematicas, paredes com janela, postes e estruturas de fachada.
- Foram observadas familias com:
  - parte horizontal;
  - parte vertical;
  - quinas;
  - polares/cantos;
  - transicoes entre pisos;
  - componentes compartilhados por mais de um contexto.

### Decisao de projeto

- Para paredes, o upscale isolado de uma sprite unica nao era suficiente.
- O atlas passou a funcionar como um contexto extendido da parede.
- Quando o mesmo sprite ID aparecia em mais de um contexto, a escolha do recorte passou a depender do tipo de parede.

## Fase 6. Paredes Com Multeiplos Andares E Uso De Z

### Consideracao de Z

- Foi incorporada a leitura de `Z` para construcoes com varios andares.
- O reconhecimento disso foi importante para estruturas que repetem a mesma parede em pisos diferentes.
- A projeccao passou a incluir piso alvo, piso acima e piso abaixo.

### Impacto pratico

- Eliminou boa parte da confusao visual em paredes com divisao vertical fraca.
- Ajudou a explicar marcas horizontais que pareciam falhas, mas eram parte da continuidade entre andares.
- Tornou possivel tratar transicoes entre floors sem depender apenas de um plano 2D.

### Resultado no processo

- Depois dessa mudanca, foram aprovados atlases de parede que antes haviam sido considerados problematicos.
- A logica de `Z` ficou registrada como obrigatoria para qualquer parede que atravesse mais de um andar.

## Fase 7. Estruturas Que Foram Revertidas Ou Reprocessadas

### Casos com resultado visual ruim

Foram revertidos ou marcados para reprocessamento:

- mesas;
- counters;
- partes de piramide em tentativas anteriores;
- corrimoes de barco;
- paredes tematicas com divisao muito forte;
- portas;
- pedras grandes;
- rampas;
- algumas paredes com corte visivel entre blocos;
- algumas facades que ficaram deformadas no upscale.

### Licao tecnica

- Nem toda sprite que funciona no geral deve ser mantida no mesmo tipo de upscale.
- O modelo ou a estrategia precisava variar conforme a textura.
- Algumas sprites lisas ficaram melhores em `high fidelity`.
- Outras, com relevo forte ou continuidade, precisaram de uma logica contextual mais forte.

## Fase 8. Pyramids

### Testes iniciais

- Houve tentativas anteriores com piramides que nao funcionaram bem.
- O trabalho foi retomado depois com atlas de continuidade, em vez de tratar a piramide como sprite unica.

### Parte frontal

- Foi montado um atlas para a parte frontal da piramide.
- A qualidade inicial do upscale foi ruim em uma tentativa, mas a logica foi confirmada como correta.
- As linhas horizontais visiveis no resultado indicaram falha de continuidade do atlas, nao erro da geometria da piramide.

### Demais faces

- Foram tratados os lados norte, sul, leste e oeste da piramide em familias separadas.
- A leitura de direcao precisou considerar que o andar de cima, em algumas faces, nao seguia a intuicao ortogonal comum.
- Isso resolveu o fluxo para as outras faces sem depender de uma unica interpretacao visual.

### Resultado

- A familia de piramide acabou sendo aprovada por partes.
- Os resultados aprovados foram copiados para permanentes.

## Fase 9. Paredes De Pedra Cinza / Family 1305-1315

### Identificacao da family

- Foi identificada uma familia de paredes e rubbles cinza associada aos CIDs:
  - `1305, 1306, 1307, 1308, 1310, 1311, 1312, 1313, 1314, 1315`
- Os sprite IDs envolvidos ficaram entre `4525` e `4540`.

### Analise de estrutura

- A familia nao era uma unica imagem.
- Ela tinha variantes de:
  - horizontal;
  - vertical;
  - diagonal para frente;
  - diagonal reversa;
  - repeticoes com duas variantes internas para os retangulos principais.

### Logica adotada

- Foi usado atlas com projeccao de `Z`.
- Foram incluidos contexto acima, abaixo e no piso alvo.
- Foram criados paines separados para variantes principais e variantes secundarias.
- A recortagem final foi feita com prioridade para o melhor contexto de cada sprite ID.

### Resultado

- Um teste parcial foi subido e validado.
- Depois foi gerado o atlas completo.
- A family completa foi subida para teste final.
- Em seguida, foi enviada para `Sprites Permanentes`.

## Fase 10. Outras Familias E Sprites Registradas Como Aprovadas

### Grounds e mosaicos aprovados

- `CID 923, 924, 925, 926, 927, 928, 929, 931, 934, 935`
- `CID 231` e variacoes de areia
- `CID 417`
- `CID 1128`
- `CID 422`
- `CID 415`

### Walls e estruturas aprovadas em iteracoes anteriores

- `CID 351` e complementos de chao/borda conexa
- `CID 373` e `374`
- `CID 408, 439, 440, 441, 442, 447, 448, 450`
- `CID 436`
- `CID 4405, 4409, 4396`
- `CID 1771`
- `CID 452`
- `CID 870`
- `CID 429`
- `CID 106, 109`

### Paredes de pedra e familias internas

- `CID 1295`
- `CID 1294`
- `CID 1298`
- `CID 1299`
- `CID 2162, 2164, 2166, 2168`
- `CID 1281, 1282, 1283, 1289, 1290, 1735`
- `CID 1345, 1346, 1347, 1349, 2203`

### Observacao

- Nem todos esses itens foram tratados no mesmo tipo de fluxo.
- Alguns passaram por atlas de continuidade.
- Outros passaram por upscale direto.
- Outros ainda foram revertidos e deixados para reprocessamento posterior.

## Fase 11. Pendencias E Reversoes

### Pendencias reconhecidas

- Outfits e creatures foram deixados para uma etapa posterior.
- Algumas paredes ainda podiam ganhar refinamento caso novas familias fossem identificadas.
- Sprites que ficaram boas no geral ainda podiam conter excecoes locais a ajustar.

### Reversoes

- Foram revertidas tentativas que deixaram visualmente ruim:
  - certas mesas;
  - determinados corrimoes;
  - portas com distorcao;
  - pedras grandes;
  - algumas paredes com corte evidente;
  - um conjunto de paredes que apareceu com linha de separacao estranha entre andares.

### Regra consolidada

- Se a sprite nao resistia ao teste visual em jogo, ela voltava para o estado anterior.
- O fato de o upscale existir nao era criterio suficiente para permanencia.

## Problemas Encontrados

- Continuidades com borda e quina foram o ponto mais dificil.
- Algumas sprites mudavam muito de qualidade dependendo do modelo de upscale.
- Certos objetos pareciam bons em 2D, mas quebravam ao serem inseridos no cliente.
- A separacao entre andares podia parecer uma falha, quando na verdade era a transicao correta do contexto.
- Algumas familias tinham sprite IDs compartilhados por mais de um contexto, exigindo recorte criterioso.
- Em varios momentos, o que parecia uma sprite simples era na verdade uma familia completa.

## Decisoes Tomadas

- Upscale simples ficou reservado para sprites sem continuidade.
- Mosaico/atlas ficou reservado para qualquer caso com borda, encaixe ou continuidade lateral/vertical.
- `Z` passou a ser parte obrigatoria da analise em construcao multilayer.
- O que ficou feio foi revertido, mesmo quando a logica geral parecia correta.
- O que foi aprovado foi imediatamente copiado para `Sprites Permanentes`.
- Sempre que possivel, a forma correta foi buscar a fidelidade visual da estrutura, nao apenas o aumento de resolucao.

## Estrutura De Entrega Que Ficou Padronizada

1. Identificar o CID ou familia.
2. Classificar o caso como simples, mosaico ou atlas.
3. Criar o mosaico ou atlas.
4. Fazer upscale no input correto.
5. Cortar e restaurar alpha original.
6. Gerar overlay parcial de `CWM`.
7. Subir para teste no `Tibia.cwm`.
8. Validar no cliente e, se preciso, ajustar.
9. Aprovar ou reverter.
10. Se aprovado, copiar para `Sprites Permanentes`.

## Conclusao

O projeto consolidou uma metodologia pratica para sprites HD:

- terreno simples -> upscale direto;
- terreno com continuidade -> mosaico;
- parede ou estrutura multilayer -> atlas com `Z`;
- sprite ruim -> rollback;
- sprite aprovada -> permanentes.

Esse changelog deve servir como referencia tecnica para novas familias de sprites, novas paredes, novas bordas e novas estruturas com continuidade.
