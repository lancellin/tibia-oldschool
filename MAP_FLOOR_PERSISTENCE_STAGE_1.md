# Persistencia incremental do chao - Etapa 1

Esta etapa implementa somente a fundacao verificavel. Ela **nao salva nem restaura tiles**.

## Classificacao central

A ordem aplicada e:

1. `HOUSE_OWNED`: house continua integralmente no sistema atual de houses.
2. `PERSIST_DEATH_BUNDLE`: estado reservado para o corpse protegido, a ser ligado na etapa de mortes.
3. `CITY_EXCLUDED`: tiles de teste `32339,32213,7`, `32340,32213,7` e `32341,32213,7`.
4. `OTBM_BASE`: item ainda pertencente ao mapa base.
5. `DO_NOT_PERSIST`: item nao movel.
6. `PERSIST_FOOD`: stackable nos IDs 2666-2691, 2695, 2696 ou 2787-2796.
7. `PERSIST_CLEAN_ONLY`: qualquer outro stackable. No desenho final ele podera voltar em restart limpo, mas ira para quarentena apos crash.
8. `PERSIST_ALWAYS`: demais itens moveis e nao stackables.

`ItemType:isStackable()` e a fonte da caracteristica stackable. Nao existe uma lista manual paralela.

## Identidade desta etapa

- O atributo invisivel e `floor_persistence_instance_id`.
- Somente itens moveis e nao stackables podem recebe-lo.
- Foods e demais stackables nao recebem `instance_id`.
- Quando um jogador move um item do OTBM, a marcacao `loadedFromMap` e removida do item e de todo o conteudo do container.
- Nesse movimento, os itens elegiveis recebem identidade automaticamente.
- Clones reais recebem identidades novas em vez de copiar a identidade do original.
- IDs existentes validos sao preservados pela serializacao normal de atributos do item.

## Comando administrativo de teste

Somente GOD pode usar:

- `/floorinspect`: inspeciona o tile em frente.
- `/floorinspect here`: inspeciona o tile atual.
- `/floorinspect 32339,32213,7`: inspeciona uma coordenada.
- `/floorinspect assign`: inspeciona e preenche IDs ausentes elegiveis no tile em frente.
- `/floorinspect assign,32339,32213,7`: faz o mesmo em uma coordenada.

O comando percorre containers recursivamente e mostra estado, mobilidade, stackable, food, origem OTBM e `instance_id`.

## Roteiro minimo

1. Crie no chao uma plate armor, uma bag, uma food permitida e um gold coin.
2. Execute `/floorinspect assign`.
3. Confirme: armor/bag = `PERSIST_ALWAYS` com ID; food = `PERSIST_FOOD` sem ID; gold = `PERSIST_CLEAN_ONLY` sem ID.
4. Repita dentro de uma bag e confira a classificacao recursiva.
5. Repita em cada um dos tres tiles de cidade e confirme `CITY_EXCLUDED` para toda a arvore.
6. Mova um item originalmente carregado do OTBM e confirme que `otbm=no` depois do movimento.
7. Reinicie normalmente e confirme que um ID salvo no inventario permanece igual.

Nenhum resultado desta etapa deve reaparecer no chao apos restart, porque o checkpoint e o replay ainda nao foram habilitados.
