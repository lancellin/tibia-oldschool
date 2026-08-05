# MANUAL OPERACIONAL — PERSISTÊNCIA INCREMENTAL DO CHÃO

Manual de operação para GODs e staff técnica do sistema de persistência
incremental do chão em `D:\tibia-oldschool`.

Documentos relacionados:

- `FLOOR_PERSISTENCE_CHANGELOG.md` — registro técnico e decisões de projeto;
- `FLOOR_PERSISTENCE_CHANGELOG_EN.md` — versão equivalente em inglês;
- `MAP_FLOOR_PERSISTENCE_ROADMAP.md` — estado das etapas e itens adiados;
- `admin\floor-quarantine-web\README.md` — painel web de quarentena.

> Este manual é para operação. Não use comandos de teste ou recuperação por
> tentativa e erro em produção. Em caso de dúvida após crash, mantenha o
> servidor fechado, preserve os dados e peça revisão técnica antes de confirmar
> qualquer recuperação.

## 1. Objetivo E Limites Do Sistema

O sistema registra alterações de itens móveis em tiles do chão, salva somente
tiles modificados e permite recuperar o último estado durável após um crash.
Ele não substitui o OTBM nem o save nativo de personagens.

O que continua sendo responsabilidade de outros sistemas:

- mapa base, paredes, terreno, itens e respawns OTBM;
- inventário, locker, depot, inbox e store inbox dos personagens;
- houses;
- market;
- respawn de criaturas;
- save normal de personagens.

O que este sistema protege:

- itens móveis elegíveis que foram colocados ou alterados no chão;
- bags e containers móveis elegíveis, inclusive conteúdo interno;
- corpses de jogadores e `death_bundle` conforme a política de recuperação;
- estado coordenado entre jogador e chão quando a movimentação envolve ambos.

O que ele deliberadamente não recupera:

- corpses de criaturas e loot deixado dentro deles;
- stackables de crash de forma automática; eles vão para quarentena;
- houses pelo mecanismo de chão;
- containers fixos do OTBM preenchidos por jogadores;
- alterações cuja recuperação tenha sido bloqueada por validação inválida.

## 2. Regras Operacionais Essenciais

1. Apenas GOD com acesso administrativo deve usar os comandos deste manual.
2. Nunca confirme uma recuperação sem antes inspecionar `recovery`, `dryrun`,
   `reconcile`, `quarantine` e os tiles relevantes.
3. Nunca use `/openserver` para contornar um bloqueio de crash recovery ou
   emergency.
4. Não use `/save` ou `/saveall` como substituto de clean save coordenado.
5. Não use `failnext`, `assign`, `clear`, `clear all confirm` ou `flush` como
   rotina de produção. Eles existem para diagnóstico controlado.
6. Em crash, primeiro preserve a situação. Não mova itens no mapa, não limpe
   tiles e não deixe GMs reorganizarem loot antes da decisão.
7. GUID, CAM e `instance_id` são evidências de investigação. Não são uma
   autorização automática para punir ou devolver item.
8. Antes da produção, a lista de tiles de cidade de teste deve ser substituída
   pela lista real e as credenciais do painel devem deixar de ser temporárias.

## 3. Estados Principais

### 3.1 Estado de snapshots

O status mostra `dirty`, `in_flight` e `groups`:

- `dirty`: tiles alterados que aguardam debounce ou checkpoint;
- `in_flight`: serialização/gravação em andamento;
- `groups`: grupos coordenados de jogador/tile ainda em processamento;
- `queued`, `succeeded`, `failed`: histórico de serializações na sessão;
- `checkpoint ... committed/failed`: resultado dos grupos coordenados;
- `Database available=yes`: banco de snapshots acessível.

Um tile pode ficar `dirty=yes` por até o debounce normal ou enquanto um grupo
coordenado espera uma parte. Isso não é erro por si só.

### 3.2 Estado de recuperação

No startup, `/floorsnapshot recovery` mostra um modo:

| Modo | Significado operacional |
| --- | --- |
| `NOTHING_TO_RECOVER` | Não existe sessão/snapshot útil anterior. O servidor pode operar normalmente. |
| `CLEAN_RESTART` | O último clean save foi validado. O replay é automático antes do login comum. |
| `CRASH_RECOVERY` | A sessão anterior terminou sem clean commit. Login comum fica bloqueado até apply e confirmação. |
| `RECOVERY_BLOCKED` | Há inconsistência ou validação falhou. Não aplique, não abra o servidor e preserve os dados. |

### 3.3 Política resumida de itens

- item móvel não stackable: normalmente `PERSIST_ALWAYS`;
- food configurada: normalmente `PERSIST_FOOD`;
- outro stackable: `PERSIST_CLEAN_ONLY`; em crash vai para quarentena;
- `death_bundle`: `PERSIST_DEATH_BUNDLE`;
- corpse de criatura: `CREATURE_CORPSE_EXCLUDED`;
- item OTBM/base: `OTBM_BASE`;
- house: `HOUSE_OWNED`;
- item imóvel: `DO_NOT_PERSIST`.

## 4. Referência Completa De Comandos

Todos os comandos abaixo exigem GOD, salvo onde indicado. `front` significa o
tile à frente do personagem; `here` significa o tile sob o personagem;
`x,y,z` aceita coordenadas explícitas.

### 4.1 `/floorinspect` — inspeção de conteúdo e política

```text
/floorinspect
/floorinspect front
/floorinspect here
/floorinspect 32339,32213,7
/floorinspect assign
/floorinspect assign,32339,32213,7
```

Uso normal: inspeção somente leitura de todos os itens do tile e containers
internos. O resultado mostra ID, nome, quantidade, mobilidade, stackability,
food, origem OTBM, estado de persistência e `instance_id`.

Analise:

- se o tile é `city=yes` ou `house=yes`;
- se o item esperado está em `PERSIST_ALWAYS`, `PERSIST_FOOD`,
  `PERSIST_CLEAN_ONLY` ou estado de exclusão correto;
- se bags e sub-bags exibem o conteúdo esperado;
- se item não stackable elegível possui `instance_id`;
- se um item do OTBM aparece como `otbm=yes` e não foi tratado como alteração
  persistível sem movimento real.

`assign` é exceção de diagnóstico: cria `instance_id` ausente somente para
itens `PERSIST_ALWAYS` presentes naquele tile. Ele **altera estado do jogo**.
Use apenas em ambiente de teste ou para corrigir manualmente um caso aprovado.
Não execute em tiles amplos ou em produção sem registrar motivo, posição e
resultado.

### 4.2 `/instancecheck` — conferir identidade antes de compensar item

```text
/instancecheck <32 caracteres hexadecimais minúsculos>
```

Use para verificar se uma identidade ainda existe em storage durável ou estado
live antes de uma compensação manual.

Interpretação:

- `ABSENT`: nenhuma ocorrência durável/live foi encontrada. Uma compensação
  manual pode ser considerada, mas deve criar um item novo com novo
  `instance_id`; nunca reutilize o ID antigo.
- `PRESENT/RESERVED`: existe ocorrência no banco ou no mundo. Não recrie.
- `INCONCLUSIVE`: banco indisponível ou erro. Não recrie até resolver.

Antes de compensar, cruze com CAM válida, logs, posição, horário, owner/GUID e
quarentena. A ausência de uma identidade não prova que o jogador perdeu item.

### 4.3 `/floordirty` — inspeção do rastreamento de alterações

```text
/floordirty status
/floordirty list
/floordirty list 50
/floordirty front
/floordirty here
/floordirty 32339,32213,7
/floordirty clear here
/floordirty clear 32339,32213,7
/floordirty clear all confirm
```

Uso normal: `status`, `list`, `front`, `here` e coordenadas são leitura de
diagnóstico. Eles ajudam a saber se uma alteração de chão foi percebida antes
do snapshot.

Analise em `status`:

- `enabled=yes` deve permanecer ativo;
- `tiles` e `events` crescem com atividade de jogador;
- `ignored_system` pode crescer muito em mapas com fields, decays ou scripts
  de sistema; isso é esperado se os eventos forem filtrados;
- `city_positions` deve coincidir com a configuração de cidade aprovada.

Analise em tile/lista:

- `events` e intervalo de `sequence` mostram quantas alterações chegaram;
- `last`, `reasons` e `origins` distinguem item/container e origem de jogador,
  morte ou ação explícita;
- `snapshot_in_flight=yes`, retries ou `error` exigem acompanhar
  `/floorsnapshot status`;
- tile de house/cidade deve aparecer com classificação coerente.

`clear <posição>` e `clear all confirm` removem apenas o **registro dirty**,
não itens do mapa. Mesmo assim, podem retirar uma alteração da fila antes de
snapshot. São comandos de teste/diagnóstico e não devem ser usados em produção
para “limpar o status”.

### 4.4 `/floorsnapshot` — status, save e recuperação

#### Status e verificação de tile

```text
/floorsnapshot status
/floorsnapshot front
/floorsnapshot here
/floorsnapshot 32339,32213,7
```

`status` é a primeira consulta para operação normal. Observe:

- `dirty=0`, `in_flight=0` e `groups=0` antes de um teste de consistência;
- `failed=0`, `serialize_failed=0` e `checkpoint ... failed=0`;
- `Database available=yes`;
- `last_success` recente quando houve atividade;
- erro diferente de `-` requer investigação antes de server save/recovery.

Verificação de tile compara blob salvo com estado live:

- `row=yes`, `dirty=no`, `valid=yes`, `match=yes`: estado salvo e live iguais;
- `dirty=yes` ou `match=no`: a alteração ainda aguarda debounce/checkpoint;
- `row=no`: tile ainda não teve snapshot no mundo/geração atual;
- checksum diferente após movimentação recente é esperado até o commit;
- checksum diferente após o prazo máximo ou depois de `flush` é alerta.

#### Recuperação — consultas somente leitura

```text
/floorsnapshot recovery
/floorsnapshot dryrun
/floorsnapshot reconcile
/floorsnapshot quarantine
/floorsnapshot apply
/floorsnapshot confirmation
```

Esses comandos não aplicam recuperação. Use-os na ordem acima depois de crash.

`recovery`:

- confira `mode`, `source`, `source state` e razão da decisão;
- `invalid > 0`, `Validation error`, `Source error` ou
  `mode=RECOVERY_BLOCKED` impedem recuperação automática/manual;
- confirme que a source é a sessão esperada, não um ID anotado de processo
  anterior.

`dryrun`:

- `ready=yes` é obrigatório para apply;
- compare `restore`, `quarantine`, `rejected` e contadores por política;
- `duplicates > 0` ou `rejected > 0` exigem análise antes de prosseguir;
- nenhum item é colocado no mapa por esse comando.

`reconcile`:

- confira identidades de jogadores e `player_matches`;
- match de identidade impede/ajusta replay para não duplicar item que já está
  em inventário, locker, depot, inbox ou store inbox;
- `invalid`, `duplicates`, `ambiguous` ou erro exigem pausa e investigação;
- houses e market estão fora da política atual.

`quarantine`:

- confirme `ready=yes`;
- stackables de crash devem aparecer em `planned_rows` e `persisted_rows`;
- `player_matches` acrescenta motivo de atenção;
- abertura do painel web é recomendada quando a quantidade é grande;
- quarentena por si só não muda o mapa.

`apply`:

- apenas exibe o estado do apply;
- antes de aplicar, deve mostrar source correta e plano pronto;
- depois do apply, mostra quantidade restaurada, suprimida e em quarentena;
- em crash, login comum continua bloqueado após apply.

`confirmation`:

- antes da confirmação, deve informar que o mapa está aplicado mas bloqueado;
- depois, deve mostrar `completed=yes`, GOD que confirmou, data/hora e número
  de rows/itens de quarentena estabilizados;
- também informa decays retomados e corpses de jogadores estendidos.

#### Recuperação — comandos que alteram mapa/acesso

```text
/floorsnapshot apply confirm <source>
/floorsnapshot recoveryconfirm <source>
```

`apply confirm <source>`:

- aplique somente depois de `recovery`, `dryrun`, `reconcile` e `quarantine`
  estarem prontos;
- `<source>` deve ser o ID exibido no startup atual e em `/floorsnapshot
  recovery`;
- o comando revalida o plano; se a fonte mudou, ele recusa;
- não pode ser repetido no mesmo processo;
- restaura itens ao lado da base OTBM sem disparar scripts de movimento;
- após sucesso em crash, não abra o servidor ainda.

Depois do apply, inspecione ao menos:

- tiles críticos conhecidos;
- bags e conteúdo interno;
- tiles de cidade;
- corpses de jogadores/duration de decay;
- resultados de quarentena;
- CAMs e GUIDs quando houver suspeita de duplicação.

`recoveryconfirm <source>`:

- execute somente depois de inspeção humana do mapa aplicado;
- usa a mesma source do apply;
- grava confirmação durável no banco;
- libera login comum e clean save neste processo;
- retoma decays pausados e acrescenta 50 minutos apenas a corpses de jogadores
  recuperados.

Se a confirmação for recusada, não force com `/openserver`, não execute clean
save e não reinicie repetidamente. Preserve logs/banco e investigue a razão.

#### Flush, falha simulada e clean save

```text
/floorsnapshot flush
/floorsnapshot failnext
/floorsnapshot failnext 3
/floorsnapshot cleansave
/floorsnapshot cleansave 30
/floorsnapshot cleansave status
/floorsnapshot cleansave cancel
```

`flush` força um lote protegido de snapshots. Grupos coordenados são
commitados de forma síncrona; tiles isolados podem completar de forma
assíncrona. Após usar, confira `status` e o tile: não presuma que `flush`
eliminou tudo sem observar `dirty`, `in_flight`, `groups` e `match`.

`failnext [1..100]` faz as próximas escritas de snapshot falharem antes do
banco. É exclusivamente para teste de retry em ambiente descartável. Nunca
use em produção.

`cleansave` inicia clean save imediato. Ele desconecta todos, inclusive o GOD
que emitiu o comando, e bloqueia login até reinício. Use apenas quando não
precisar de aviso prévio.

`cleansave <segundos>` agenda clean save entre 5 e 3600 segundos, anuncia aos
jogadores e permite acompanhar/cancelar antes do início. É a opção preferida
para manutenção planejada.

Antes de clean save:

1. confirme que não existe crash recovery pendente;
2. confira `/floorsnapshot status` e erros;
3. confira se nenhum emergency está ativo;
4. anuncie janela suficiente para logout;
5. não mova itens depois da decisão final de save.

Depois:

1. confirme no log que o clean checkpoint foi committed;
2. reinicie o TFS;
3. confirme `mode=CLEAN_RESTART` e replay automático concluído;
4. confira alguns snapshots críticos antes de abrir testes amplos.

#### Weekly reset manual

```text
/floorsnapshot weeklyreset 30 confirm
```

Aceita atraso entre 5 e 3600 segundos e exige a palavra `confirm`. O comando
agenda aviso, desconecta jogadores no momento de execução, salva players e
houses pelos caminhos normais e esvazia atomicamente snapshots persistidos de
chão não-house.

Antes de usar:

- confirme que é realmente a janela semanal aprovada;
- confirme que o mapa não contém teste/recuperação que deva ser preservado;
- confirme que não há crash recovery pendente ou emergency;
- avise jogadores claramente que itens no chão serão removidos.

Depois do restart, o esperado é:

- `CLEAN_RESTART`;
- dry-run e apply com `restore=0` para snapshots resetados;
- itens OTBM retornam normalmente pelo mapa;
- houses, depot, locker e inventário continuam intactos;
- tiles de chão persistidos aparecem vazios até nova alteração elegível.

### 4.5 `!emergency` — contenção manual

```text
!emergency
!emergency start
!emergency finish
```

Use somente em incidente real: lag extremo, necessidade de preservar estado
antes de decisão, problema operacional próximo ao server save ou investigação
que exige impedir novas alterações.

Ao iniciar:

- login comum é bloqueado;
- jogadores comuns são desconectados e seguem o save normal de logout;
- GODs com permissão podem permanecer/entrar;
- decays são pausados, inclusive novos decays iniciados durante emergency;
- server saves automáticos/manuais são recusados;
- `/openserver` não libera acesso comum.

Analise durante emergency:

- se login comum realmente está bloqueado;
- se o GOD consegue operar sem alterar itens desnecessariamente;
- se decays não diminuem;
- logs e snapshots necessários para a decisão;
- se o incidente é crash recovery, não substitua o fluxo de recovery por
  emergency sem justificativa técnica.

`!emergency finish`:

- retoma decays pausados;
- adiciona 50 minutos somente a corpses de jogadores;
- inicia clean save coordenado;
- desconecta inclusive o GOD que concluiu;
- mantém acesso fechado até reinício.

Se o clean save de finalização falhar, não reabra o servidor. Preserve o
estado fechado e trate como incidente de recuperação.

### 4.6 `/openserver` e `/closeserver`

Esses comandos já existiam, mas interagem com a persistência:

- `/openserver` é recusado enquanto emergency estiver ativo;
- `/openserver` não deve ser usado para ignorar bloqueio de crash recovery;
- `/closeserver` pode fechar entrada, mas não substitui clean save, emergency
  ou weekly reset.

Para reabrir após crash, a única sequência válida é apply, inspeção e
`recoveryconfirm` bem-sucedido. Para reabrir após emergency, a sequência válida
é `!emergency finish`, clean save concluído e restart.

## 5. Fluxos Operacionais Passo A Passo

### 5.1 Operação normal durante o dia

1. Não execute comandos se não há incidente.
2. Para investigar uma bag ou tile, use `/floorinspect here` e
   `/floorsnapshot here`.
3. Logo após alteração, espere o debounce; `dirty=yes` e `match=no` são
   esperados temporariamente.
4. Depois de 15–60 segundos, confirme `dirty=no`, `match=yes` e ausência de
   erro.
5. Use `/floordirty status` somente para diagnóstico de volume, não como
   alarme isolado. `ignored_system` alto pode ser normal.

### 5.2 Manutenção / clean save planejado

1. Use `/floorsnapshot cleansave 300` para cinco minutos de aviso, ou outro
   prazo aprovado.
2. Confira `/floorsnapshot cleansave status` durante a contagem.
3. Se houver motivo legítimo, use `cleansave cancel` antes do início.
4. Quando iniciar, não tente logar; o bloqueio é esperado.
5. Aguarde o processo terminar e reinicie o TFS.
6. Confirme no log `CLEAN_RESTART` e o replay automático.
7. Faça inspeção pontual antes de anunciar disponibilidade pública.

### 5.3 Weekly reset

1. Garanta que a staff aprovou a limpeza semanal.
2. Agende `/floorsnapshot weeklyreset <segundos> confirm`.
3. Confirme que o aviso público foi emitido.
4. Não cancele por hábito; cancelar é apenas antes da execução e por motivo
   operacional claro.
5. Após o restart, confirme que snapshots de chão foram esvaziados e que OTBM,
   houses e storage de personagens estão corretos.

### 5.4 Crash recovery

**Regra principal: jogadores comuns bloqueados após crash é comportamento
correto. Não abra o servidor antes da confirmação.**

1. Inicie o TFS e leia o log de startup.
2. Entre somente com GOD autorizado.
3. Execute `/floorsnapshot recovery`.
4. Se `mode=RECOVERY_BLOCKED`, pare aqui. Faça backup do banco/logs e peça
   análise técnica. Não aplique nada.
5. Se `mode=CLEAN_RESTART`, confirme que o replay automático terminou em
   `/floorsnapshot apply`; não é necessária confirmação de crash.
6. Se `mode=CRASH_RECOVERY`, execute, nesta ordem:

   ```text
   /floorsnapshot recovery
   /floorsnapshot dryrun
   /floorsnapshot reconcile
   /floorsnapshot quarantine
   /floorsnapshot apply
   ```

7. Verifique `ready=yes` nas etapas necessárias e anote o ID `source` atual.
8. Abra o painel de quarentena se houver stackables ou GUIDs relevantes.
9. Execute:

   ```text
   /floorsnapshot apply confirm <source_atual>
   ```

10. Inspecione posições relevantes com `/floorsnapshot x,y,z`,
    `/floorinspect x,y,z`, CAM e logs.
11. Confira `/floorsnapshot confirmation`.
12. Se tudo estiver coerente, execute:

    ```text
    /floorsnapshot recoveryconfirm <source_atual>
    ```

13. Confirme `completed=yes`, login comum liberado e decays retomados.
14. Registre source, GOD, hora, anomalias, itens em quarentena e decisão.

Se encontrar divergência após apply:

- não confirme;
- não use `/openserver`;
- não mova itens para “corrigir visualmente”;
- preserve CAM, logs, coordenadas, checksums e captura do painel;
- escale para análise técnica.

### 5.5 Emergency

1. Confirme que existe incidente real e que emergency é apropriado.
2. Execute `!emergency`.
3. Confirme bloqueio de login e pausa de decay.
4. Investigue sem modificar tiles, salvo necessidade absoluta documentada.
5. Quando houver decisão de encerrar, execute `!emergency finish`.
6. Aguarde clean save, shutdown/restart e retorno controlado.

## 6. Painel Web De Quarentena

O painel é privado e somente leitura. Ele não devolve ou exclui itens.

Fluxo recomendado:

1. Abra `/` ou `/items` para agrupamento inicial por tipo de stackable.
2. Abra o tipo relevante, por exemplo gold coin, para ver ocorrências, pilhas,
   coordenadas, containers e atualização.
3. Use `/players` para agrupar pelo último manipulador GUID.
4. Use a página do jogador para cruzar tipo, fonte, risco temporal e busca.
5. Use `/quarantine` para visão técnica por tile e
   `/quarantine/{id}` para árvore/manifesto de origem.
6. Trate GUID como pista para localizar CAM e horário; não como conclusão.

Sinais de maior atenção:

- item movido muito próximo do momento do crash;
- grande quantidade de stackable em uma mesma origem;
- GUID associado a múltiplas ocorrências incomuns;
- item stackable cuja origem também aparece em storage de jogador;
- CAM ou logs incompatíveis com a história do item.

Sinais de menor atenção, mas que ainda devem ser registrados:

- snapshot antigo e estável muito antes do crash;
- valores muito baixos sem outro indício;
- item sem GUID em origem que não envolve jogador.

Antes da produção, troque credenciais temporárias, mantenha o painel atrás de
VPN/HTTPS e use usuário MariaDB somente leitura conforme o README do painel.

## 7. CAM Forense E Compensação Manual

Para investigar item em CAM:

1. Use CAM nova, gravada depois da implementação forense.
2. Use GOD e dê look no item gravado.
3. Se houver evidência assinada válida, registre item, propriedades,
   `instance_id`, posição e horário.
4. Se a CAM informar evidência ausente, incompatível ou adulterada, não use
   aquele look como prova de compensação.
5. Antes de criar qualquer compensação, execute `/instancecheck <id>`.
6. Se a compensação for aprovada, crie item novo com identidade nova; nunca
   force/reutilize a identidade histórica.

Não existe compensação automática por CAM. Casos de corpse muito próximo do
crash, loot especial ou suspeita de duplicação devem ser analisados um a um.

## 8. Sinais De Alerta E Resposta

| Sinal | Resposta |
| --- | --- |
| `Database available=no` | Não faça clean save, weekly reset, apply ou confirmação. Investigue banco/conexão. |
| `RECOVERY_BLOCKED` | Não abra servidor. Preserve banco e logs. |
| `dryrun ready=no` | Não aplique recuperação. Leia o erro. |
| checksum `match=no` pouco depois de alteração | Aguarde debounce/checkpoint e confira novamente. |
| checksum `match=no` após prazo/flush | Verifique dirty, in-flight, groups, retries e erro. |
| `checkpoint ... failed>0` | Não trate como save saudável; investigue antes de shutdown. |
| `duplicates`, `ambiguous` ou identidade inválida | Pare recuperação e investigue reconciliação. |
| quarentena grande | Abra painel, registre source e não devolva em massa. |
| login bloqueado depois de crash | Esperado até apply + recoveryconfirm. |
| login bloqueado depois de emergency | Esperado até finish, clean save e restart. |
| corpse recuperado não decai durante bloqueio | Esperado; decay está pausado. |

## 9. Itens Deliberadamente Fora Da Operação Atual

Não tente suprir manualmente estes itens por comandos improvisados:

- gatilho automático do emergency por interrupção global de pacotes;
- integração de GUID com Player Shop;
- containers fixos OTBM preenchidos por jogador;
- reconciliação de houses/market;
- comando de recuperação de corpse;
- devolução/descarte de quarentena pelo painel;
- lista especial de rares;
- teste local de 50–200 players completos;
- medição detalhada de pacotes CAM em personagem imóvel.

Esses itens estão no roadmap e exigem decisão/implementação própria.

## 10. Checklist Antes De Abrir Ao Público

- [ ] Lista real de tiles de cidade configurada e revisada.
- [ ] Política final de weekly reset aprovada.
- [ ] Credenciais do painel substituídas e acesso restrito.
- [ ] Usuário MariaDB do painel limitado a `SELECT`.
- [ ] Backup do banco testado e plano de rollback aprovado.
- [ ] Executável Release final identificado e preservado.
- [ ] Teste de clean restart aprovado na configuração final.
- [ ] Teste de crash recovery e confirmação aprovado na configuração final.
- [ ] Teste de emergency aprovado na configuração final.
- [ ] Procedimento de weekly reset aprovado.
- [ ] Telemetria do closed beta preparada.
- [ ] Staff treinada neste manual e no fluxo de decisão pós-crash.

## 11. Registro Obrigatório De Incidente

Para cada crash, emergency, weekly reset fora da rotina ou compensação manual,
registre no mínimo:

- data/hora e motivo;
- session/source da recuperação;
- modo selecionado;
- GOD responsável;
- comandos executados e resultados;
- número de itens restaurados, suprimidos e em quarentena;
- tiles/CAMs/logs analisados;
- decisões de compensação ou não compensação;
- hora de confirmação e de reabertura;
- qualquer anomalia observada.

Esse registro é essencial para auditoria futura e para ajustar o sistema sem
perder o contexto de decisões humanas.
