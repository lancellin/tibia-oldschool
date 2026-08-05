# Persistencia incremental do chao - Etapa 2

Esta etapa adiciona rastreamento dirty em memoria e em modo sombra. Ela **nao grava banco, nao cria checkpoint e nao restaura o chao**.

O rastreamento e orientado pela origem da mutacao. Alteracoes internas do mundo, como decay, magic fields, ashes de quests, corpses de monstros e scripts de cenario, sao contadas em `ignored_system`, mas nao criam tiles dirty.

## O que torna um tile dirty

- Item adicionado ou removido do tile.
- Alteracao de ID, subtipo, quantidade ou substituicao de item.
- Item adicionado, removido ou atualizado dentro de qualquer nivel de container que esteja no chao.
- Atributo serializavel alterado durante uma movimentacao de jogador, escrita de texto ou atribuicao explicita de `instance_id`.
- Morte de jogador cria um registro `DEATH_BUNDLE`, inclusive nos tiles de cidade.
- Adicionar, remover ou alterar conteudo dentro do corpse protegido, ou movimentar manualmente o proprio corpse, remove o atributo `death_bundle` e converte o corpse restante para o fluxo normal de identidades. Movimentacoes de outros itens no mesmo tile nao removem a protecao, e o decay automatico do corpse preserva o `death_bundle` enquanto ainda existir um corpse para persistir.

Movimento entre tiles gera pelo menos dois registros: remocao na origem e adicao no destino.

O carregamento original do OTBM usa a rota interna e nao marca o mapa inteiro no startup.

Containers validados mantem um estado transitorio `subtree_identified`. Movimentacoes posteriores do container validam somente o item externo; a arvore interna volta a ser percorrida apenas quando recebe conteudo ainda nao identificado. A primeira validacao usa iteracao, sem recursao por nivel.

Itens elegiveis comprados em lojas NPC recebem `instance_id` no momento da criacao, inclusive os containers usados para entrega. O limite normal de compra continua sendo 100 unidades por transacao. Stackables continuam sem identidade individual porque podem se fundir e dividir.

Somente movimentacoes executadas por jogador, morte de jogador e alteracoes explicitamente identificadas entram no registro. Morte de monstro, fire field, ashes e outros ciclos do sistema nao iniciam persistencia.

## Exclusoes antecipadas

- Houses nao entram no registro dirty do chao.
- Os tres tiles de cidade configurados nao entram no registro.
- Containers no inventario de jogadores, depot e inbox nao marcam o tile onde o jogador esta.

## Estado registrado

Cada tile dirty possui:

- sequencia da primeira e da ultima alteracao;
- horario da primeira e da ultima alteracao;
- quantidade de eventos observados;
- ultimo motivo;
- mascara acumulada de motivos.

Os motivos sao `ITEM_ADD`, `ITEM_REMOVE`, `ITEM_UPDATE`, `ITEM_REPLACE`, `CONTAINER_ADD`, `CONTAINER_REMOVE`, `CONTAINER_UPDATE`, `ATTRIBUTE_UPDATE` e `DEATH_BUNDLE`.

As origens aceitas sao `PLAYER_MOVE`, `PLAYER_DEATH` e `EXPLICIT`. O contador `ignored_system` permite observar o volume descartado sem poluir a lista de tiles.

## Comando administrativo

- `/floordirty status`
- `/floordirty list 20`
- `/floordirty front`
- `/floordirty here`
- `/floordirty 32338,32213,7`
- `/floordirty clear here`
- `/floordirty clear 32338,32213,7`
- `/floordirty clear all confirm`

Os comandos `clear` apagam somente o registro sombra. Eles nao removem ou alteram itens.

## Roteiro de teste

1. Execute `/floordirty clear all confirm` e `/floordirty status`.
2. Jogue um item em um tile normal: o destino deve mostrar `ITEM_ADD` e possivelmente `ATTRIBUTE_UPDATE` pela identidade.
3. Mova o item para outro tile: origem com `ITEM_REMOVE`, destino com `ITEM_ADD`.
4. Deixe uma bag no chao e adicione/remova itens dentro dela: deve aparecer `CONTAINER_ADD`/`CONTAINER_REMOVE`.
5. Junte ou separe um stack: deve aparecer `ITEM_UPDATE` ou `CONTAINER_UPDATE` conforme o local.
6. Faça as mesmas operacoes nos tres tiles de cidade: eles devem continuar `dirty=no`.
7. Faça as mesmas operacoes em uma house: os tiles devem continuar `dirty=no`.
8. Mova itens apenas dentro do inventario: o tile do jogador nao deve ser marcado.

O registro existe somente na memoria e desaparece no restart. Essa perda e intencional nesta etapa; a persistencia do checkpoint sera validada separadamente.
