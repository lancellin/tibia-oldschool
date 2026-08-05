# FLOOR_PERSISTENCE_CHANGELOG

Registro técnico consolidado da implementação de persistência incremental do
chão, recuperação pós-crash, quarentena, auditoria de itens e controles
operacionais no TFS em `D:\tibia-oldschool`.

Este documento possui uma versão equivalente em inglês:
`FLOOR_PERSISTENCE_CHANGELOG_EN.md`.

> Status deste registro: implementação e validação local concluídas nas etapas
> funcionais descritas abaixo. Isto **não** declara o sistema pronto para abrir
> ao público: closed beta, configuração definitiva e preparação operacional
> permanecem como etapas posteriores.

## Resumo Executivo

Foi implementado um sistema de persistência incremental para itens móveis no
chão. O objetivo é preservar bagloots e itens elegíveis entre server saves e
reduzir perdas após crash, sem substituir o mapa OTBM, o salvamento normal de
personagens, houses, depots ou a lógica própria de respawn do servidor.

O sistema registra apenas tiles modificados, serializa seu estado de forma
versionada e cria checkpoints coordenados entre jogadores e tiles. Em um
encerramento limpo, os snapshots adequados são reaplicados automaticamente no
próximo início. Em crash, o servidor seleciona uma fonte de recuperação,
valida tudo antes de alterar o mapa, bloqueia jogadores comuns e exige ação
explícita de GOD para aplicar e confirmar a recuperação.

Os principais resultados são:

- itens móveis elegíveis no chão podem sobreviver a reinícios limpos e a
  recuperação confirmada de um crash;
- stackables de risco são separados em quarentena durante crash, em vez de
  serem restaurados automaticamente;
- corpses de jogadores e `death_bundle` recebem tratamento especial;
- houses, depots e inventários continuam sob seus mecanismos normais;
- itens do OTBM continuam sendo reconstruídos pelo mapa; o replay recuperado é
  acrescentado sem sobrescrever a base do mapa;
- o sistema possui fluxo manual de weekly reset, clean save, crash recovery e
  emergency;
- há um painel web privado e somente leitura para investigar quarentenas;
- `instance_id`, GUID do último manipulador e evidência assinada em CAM
  auxiliam investigação, sem serem tratados como prova automática de fraude.

## Atualização Consolidada — 19 A 27 De Julho De 2026

Este changelog consolida as etapas implementadas e testadas nesse período. As
datas de build, hashes do executável final e decisão de abertura pública não
foram fixadas aqui porque ainda dependem da preparação para produção e do
closed beta.

## Escopo E Contrato De Segurança

O sistema foi construído com as seguintes fronteiras deliberadas:

- não persiste o mapa inteiro a cada alteração;
- não altera houses pelo mecanismo de chão;
- não substitui o save nativo de jogadores, locker, depot, inbox ou market;
- não restaura parcialmente um conjunto de snapshots inválido;
- não reaplica automaticamente uma recuperação classificada como crash;
- não restaura stackables de crash diretamente no mapa;
- não tenta preservar corpses de criaturas ou o conteúdo que permaneceu dentro
  deles;
- não impede que OTBM recrie itens de mapa no startup;
- não usa a CAM, GUID ou `instance_id` como única fonte de decisão punitiva.

O sistema é adicional ao OTBM. O OTBM continua sendo a fonte da geometria,
itens base e respawns do mapa. Os snapshots representam somente alterações
persistíveis feitas após o carregamento do mundo.

## Arquitetura Implementada

### 1. Identidade, Classificação E Origem Dos Itens

Itens móveis não stackables recebem `instance_id` persistente depois que sua
criação ou inserção foi concluída com sucesso. A identidade é usada para
reconciliação, prevenção de duplicação durante recuperação e investigação.

Os fluxos cobertos incluem, entre outros:

- criação por GM de item móvel não stackable;
- compra em loja;
- obtenção efetiva de item de quest com AID 2000/2001;
- item retirado de criatura, container ou corpse;
- inserção em container móvel;
- movimento de container móvel com conteúdo;
- backpack entregue ao jogador ao morrer, quando aplicável.

Containers móveis são tratados como subárvores: quando recebem conteúdo ou são
movidos, o container e seu conteúdo elegível são normalizados para que itens
sem identidade recebam `instance_id`. Isso cobre backpacks dentro de
backpacks sem depender de um limite artificial de profundidade no jogo.

Itens stackables não recebem `instance_id` individual. Para investigação, itens
móveis também podem carregar o GUID do último personagem que os manipulou. A
atualização imediata cobre o item participante da ação; propagação de uma
subárvore de container usa debounce de 8 segundos, com limite máximo de 24
segundos. Esse GUID é pista de investigação, não prova de propriedade.

### 2. Política De Persistência Do Chão

A classificação implementada diferencia, em alto nível:

- houses: excluídas do mecanismo de chão;
- `death_bundle` e corpses de jogadores protegidos: persistência especial;
- corpses de criaturas: excluídos;
- itens base do OTBM e itens imóveis: não serializados como alteração de chão;
- itens móveis não stackables: persistência normal;
- foods configurados: persistência normal;
- demais stackables: `PERSIST_CLEAN_ONLY`, sujeitos a quarentena em crash.

Os foods atualmente reconhecidos são os IDs `2666..2691`, `2695`, `2696` e
`2787..2796`. A lista de posições de cidade existente no código é de teste e
deve ser substituída pela lista real antes da produção.

Itens colocados em cidade são mantidos para proteção em crash conforme a
política de recuperação, mas são tratados pela política de limpeza no save
limpo/weekly reset. O weekly reset remove snapshots persistidos de chão; não
limpa houses, inventários, locker ou depot.

### 3. Dirty Tracking, Serialização E Checkpoints

Tiles são marcados como `dirty` por mudanças relevantes de item ou container,
incluindo adição, remoção, atualização, substituição e alteração interna de
container. Eventos de sistema recorrentes sem origem de jogador são filtrados
para não transformar efeitos, fields ou decays do mapa em fila infinita de
snapshots.

Configuração local atual:

- debounce normal: 15 segundos;
- atraso máximo: 60 segundos;
- retry após falha: 5 segundos;
- lote máximo: 32 tiles;
- mundo e geração: `1`/`1`.

Quando uma movimentação envolve jogadores e tiles, o sistema agrupa os
participantes em um checkpoint coordenado. O objetivo é evitar que o estado de
um item seja salvo no personagem e no chão em momentos incompatíveis. O
checkpoint só é considerado durável após as partes necessárias serem gravadas.

Cada snapshot contém versão de formato, versão de política, posição, contadores
de itens, tamanho e checksum SHA-256. Na leitura, formato, política, tamanho,
checksum, blob, posição, contadores e identidades obrigatórias são validados.

### 4. Clean Save, Reinício Limpo E Weekly Reset

O clean save coordenado fecha o acesso comum, salva jogadores e snapshots de
chão de forma coordenada e mantém o login bloqueado até o reinício. Depois de
um checkpoint limpo válido, o próximo startup entra em `CLEAN_RESTART` e
reaplica automaticamente o estado de chão selecionado antes de liberar o
jogo.

O weekly reset é manual e explícito. Ele executa um clean save que esvazia
atomicamente os snapshots persistidos de chão não-house. O mapa base OTBM é
carregado normalmente no próximo início; itens persistidos de chão não são
reaplicados.

O server save programado usa o mesmo caminho coordenado. O processo pode
permanecer vivo enquanto o checkpoint é validado, com acesso público fechado,
antes do shutdown efetivo configurado.

### 5. Seleção E Recuperação Pós-Crash

No startup, uma sessão anterior é classificada como uma das seguintes opções:

- `NOTHING_TO_RECOVER`;
- `CLEAN_RESTART`;
- `CRASH_RECOVERY`;
- `RECOVERY_BLOCKED`.

Um reinício limpo exige checkpoint limpo, atômico e compatível. Sessões vazias
criadas pouco antes de um novo startup são ignoradas para não esconder uma
fonte útil anterior.

Em `CLEAN_RESTART`, o replay é automático. Em `CRASH_RECOVERY`, jogadores
comuns permanecem bloqueados. O fluxo obrigatório é:

1. inspecionar a seleção e o dry-run;
2. conferir reconciliação e quarentena;
3. aplicar explicitamente a fonte escolhida;
4. inspecionar o mapa restaurado;
5. confirmar de forma durável a recuperação;
6. somente então liberar login comum e novo clean save.

O apply acrescenta os itens recuperados ao lado de itens do OTBM, sem disparar
scripts de movimento ou criar novos eventos dirty de jogador. O apply não pode
ser repetido no mesmo processo.

### 6. Reconciliação E Quarentena

Antes de aplicar uma recuperação de crash, o sistema compara identidades do
chão com inventário, locker, depot, inbox e store inbox de jogadores. Houses e
market permanecem fora dessa reconciliação por decisão explícita.

Stackables de crash são materializados em quarentena com o blob de origem
retido como contexto. A quarentena não remove nem restaura itens por si só.
Ela mantém itens de risco fora do replay automático até investigação humana.

Foi criado o painel privado `admin\floor-quarantine-web`, somente leitura,
para navegar por tipo de stackable, tile, container, fonte, risco temporal e
GUID do último manipulador. O painel não devolve, descarta, altera itens ou
modifica o banco de jogo.

### 7. Corpses, Decay E Emergency

Corpses de jogadores recuperados de crash têm o decay pausado enquanto a
recuperação está bloqueada. Após confirmação, somente esses corpses recebem
mais 50 minutos; os demais itens retomam o tempo restante sem extensão.

Corpses de criaturas não participam da persistência de chão. Se um corpse de
criatura ou seu loot não recolhido desaparecer após crash, isso é comportamento
aceito. Ao retirar um item do corpse, o item passa pelo fluxo normal de
identidade e persistência.

O comando GOD `!emergency` fecha acesso comum, desconecta jogadores normais,
pausa decay e bloqueia server saves. `!emergency finish` retoma decays,
acrescenta 50 minutos apenas a corpses de jogadores e inicia um clean save
coordenado. O gatilho automático por ausência de pacotes foi deliberadamente
adiado.

### 8. Evidência De CAM

O cliente recebeu suporte de CAM forense para inspeção de itens registrados,
inclusive por GOD. A evidência é assinada no fluxo de pacote e o client recusa
look quando a prova estiver ausente, incompatível ou adulterada. A CAM oferece
uma ferramenta de investigação; ela não permite editar o replay nem substituir
o processo de confirmação do servidor.

Medição específica de frequência e bytes dos pacotes adicionais de CAM para um
personagem parado permanece pendente antes do closed beta.

## Comandos Administrativos Relevantes

Os comandos abaixo são GOD-only e fazem parte do diagnóstico/controle técnico.
Este changelog não substitui o manual operacional que será produzido depois.

- `/floorsnapshot status`, `here`, `front` ou `x,y,z`: inspeção de runtime e
  snapshots;
- `/floorsnapshot recovery`, `dryrun`, `reconcile`, `quarantine`: inspeção da
  recuperação sem alterar o mapa;
- `/floorsnapshot apply confirm <source>`: aplica uma recuperação de crash;
- `/floorsnapshot recoveryconfirm <source>`: confirma a recuperação após
  inspeção e libera acesso comum;
- `/floorsnapshot cleansave [5..3600]`, `cleansave status` e `cleansave
  cancel`: clean save imediato ou agendado;
- `/floorsnapshot weeklyreset [5..3600] confirm`: weekly reset explícito;
- `/floorsnapshot flush` e `failnext`: diagnóstico e teste controlado, não
  destinados à operação normal;
- `!emergency` e `!emergency finish`: emergência manual.

## Banco, Código E Artefatos Principais

Componentes centrais no source do TFS:

- `sources\nekiro-tfs-1.5-7.72\src\floorpersistence.cpp`;
- `sources\nekiro-tfs-1.5-7.72\src\floorpersistence.h`;
- `game.cpp`, `game.h`, `item.cpp`, `item.h`, `container.cpp`, `container.h`,
  `protocolgame.cpp`, `luascript.cpp` e `luascript.h` no mesmo diretório.

Camadas Lua e runtime:

- `server\data\lib\core\floor_persistence.lua`;
- `server\data\talkactions\scripts\floor_snapshot.lua`;
- `server\data\talkactions\scripts\floor_dirty.lua`;
- `server\data\talkactions\scripts\floor_inspect.lua`;
- `server\data\talkactions\scripts\emergency.lua`;
- `server\data\globalevents\scripts\serversave.lua`;
- `server\data\migrations\30.lua` até as migrações posteriores da família de
  persistência;
- `server\schema.sql`.

No client, a integração de CAM forense está concentrada em:

- `sources\otclient-redemption\modules\game_cam_forensics\camforensics.lua`.

Também foram preservados executáveis de referência antes de etapas críticas em
`server\backup_executables` e no diretório `server`.

## Validação Local Realizada

Foram testados manualmente, entre outros:

- identificação de itens, criação por GM, loja, quest e morte de jogador;
- movimentação de bags, sub-bags, itens entre jogadores, chão, inventário,
  locker, depot, mailbox e parcel;
- checkpoints coordenados entre players e tiles;
- falhas simuladas de escrita e retry;
- clean save, cancelamento, reinício limpo e weekly reset;
- crash recovery, apply explícito, confirmação durável e bloqueio de login;
- quarentena de stackables e painel web somente leitura;
- replay junto a itens do OTBM, incluindo ordem de altura e containers;
- city tiles, houses, player corpses, death bundle, decays e emergency;
- CAM forense e look de item com evidência válida.

Medições locais de carga no TFS, usando 12 processadores lógicos, mostraram:

| Cenário | CPU média total do TFS | Equivalente médio de um núcleo |
| --- | ---: | ---: |
| 0 jogadores | 0,027% | 0,32% |
| 1 jogador ocioso | 0,205% | 2,46% |
| 5 jogadores ociosos | 0,195% | 2,34% |
| 5 em follow/movimento | 0,527% | 6,33% |
| 5 arqueiros em movimento e combate | 0,586% | 7,03% |

Não houve crescimento de memória privada do TFS durante as janelas de teste.
A tentativa de iniciar 10 OTClients completos localmente consumiu cerca de
11,2 GiB de memória privada e foi encerrada com segurança; carga de 50–200
jogadores será medida no closed beta, não por dezenas de clients completos
nesta máquina.

## Limites Conhecidos E Pontos Adiados

Os itens abaixo são deliberadamente externos ao escopo concluído e não devem
ser confundidos com falha silenciosa:

- gatilho automático do emergency por interrupção global de pacotes;
- integração do GUID investigativo com Player Shop;
- containers fixos OTBM preenchidos por jogadores;
- reconciliação de houses e market;
- recuperação administrativa de corpses por comando;
- lista especial de rares para investigação;
- decisão, devolução ou descarte de quarentena pelo painel web;
- medição detalhada dos pacotes extras de CAM em estado imóvel;
- carga de 50–200 jogadores em ambiente de closed beta.

## Próximos Passos Antes Da Produção

1. Substituir tiles de cidade de teste pela lista real e revisar a política
   final de limpeza.
2. Trocar credenciais temporárias do painel e limitar o acesso por rede,
   HTTPS/VPN e usuário MariaDB somente leitura.
3. Produzir manual operacional para staff, incluindo clean save, weekly reset,
   crash recovery, emergência, quarentena e rollback.
4. Criar telemetria repetível para o closed beta.
5. Executar closed beta com carga real e validar os limites de CPU, RAM,
   banco, rede, snapshots e CAM.
6. Fazer build Release limpo, backup, plano de rollback e aprovação final.

## Decisão De Rollback

O rollback não deve ser feito apagando rows de snapshot ou executáveis sem
backup. Antes de qualquer reversão, preservar banco e binário atuais,
interromper novos logins e decidir se o retorno será para executável anterior,
geração nova de persistência ou restauração de backup. As cópias preservadas
antes de etapas críticas existem justamente para suportar esse procedimento.
