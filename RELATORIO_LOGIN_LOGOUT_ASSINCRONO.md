# Relatorio tecnico - Login e logout assincronos

## 1. Objetivo e escopo

Este documento descreve as alteracoes realizadas a partir da autorizacao para implementar a arquitetura minima de login e logout assincronos ate o estado atual do projeto.

O levantamento foi reconstruido pelo historico exato das edicoes e conferido contra o codigo presente no workspace. A raiz `D:\tibia-oldschool` nao possui um historico Git confiavel para todo esse intervalo; por isso, este documento nao deve ser interpretado como um `git diff` convencional.

Foram separados deste escopo os sistemas antigos de persistencia de tiles, houses, CAM forense, GUID investigativo, quarentena, weekly reset e combate.

## 2. Resumo da arquitetura implementada

Foi criado um processo externo chamado `player_io_service.exe`.

### Login

```text
Cliente solicita login
  -> TFS reserva o personagem
  -> worker de login consulta o servico externo
  -> servico consulta o MariaDB em transacao consistente
  -> resultados SQL imutaveis retornam ao TFS
  -> Dispatcher cria e materializa o Player
  -> personagem entra no mapa
```

### Logout

```text
Player ainda esta valido no Dispatcher
  -> TFS serializa seu estado em comandos SQL imutaveis
  -> snapshot e gravado no journal duravel do servico
  -> Player pode ser removido do jogo
  -> worker solicita ao servico a aplicacao do job
  -> servico executa a transacao no MariaDB
  -> revisao do personagem e confirmada
```

### Propriedades da arquitetura

- Nenhum `Player`, `Tile` ou `Container` vivo e enviado ao worker.
- O `Player` do login e criado e materializado somente no Dispatcher.
- O snapshot de logout e criado enquanto o `Player` ainda esta valido no Dispatcher.
- Workers recebem somente IDs, strings, revisoes, resultados copiados e comandos SQL imutaveis.
- O relog fica bloqueado enquanto houver save pendente.
- O servico usa journal, versao monotonicamente crescente, hash do payload e operacoes idempotentes.
- Tiles, houses, mail e checkpoints coordenados nao foram transferidos para esse servico.
- A serializacao do snapshot e o `PREPARE` duravel ainda ocorrem antes de liberar o objeto `Player`.
- A aplicacao pesada do SQL no MariaDB ocorre de forma assincrona.

## 3. Arquivos novos

### 3.1. `sources/nekiro-tfs-1.5-7.72/src/playerioprotocol.h`

Define o protocolo binario entre o TFS e o servico:

- magic `PIO1`;
- versao 2;
- frame maximo de 64 MiB;
- opcodes de ping, consultas, preparacao, aplicacao e consulta de jobs;
- estados dos jobs;
- estruturas de resultados SQL;
- serializadores `Reader` e `Writer`.

### 3.2. `sources/nekiro-tfs-1.5-7.72/src/playerioprotocol.cpp`

Implementa:

- framing das mensagens;
- envelopes com `requestId`;
- serializacao de comandos SQL e resultados em memoria;
- validacao de magic, versao, opcode e tamanho;
- codificacao e decodificacao de requests e responses.

### 3.3. `sources/nekiro-tfs-1.5-7.72/src/playerioclient.h`

Declara o cliente TCP utilizado pelo TFS para conversar com o servico externo.

### 3.4. `sources/nekiro-tfs-1.5-7.72/src/playerioclient.cpp`

Implementa:

- conexoes persistentes;
- reconexao;
- limites de tempo para conectar, ler e escrever;
- validacao de opcode e `requestId`;
- `ping`;
- consulta simples;
- consulta em lote consistente;
- verificacao de prontidao do jogador para relog;
- `prepareSaveJob`;
- `applySaveJob`;
- operacao combinada legada `submitSaveJob`.

### 3.5. `sources/nekiro-tfs-1.5-7.72/src/playeriodatabase.h`

Define `PlayerIORemoteDatabaseScope` e seus modos:

- leituras remotas;
- coleta de escritas em uma lista imutavel;
- replay de resultados SQL capturados anteriormente.

### 3.6. `sources/nekiro-tfs-1.5-7.72/src/playeriodatabase.cpp`

Implementa:

- escopo `thread_local`;
- redirecionamento de `storeQuery`;
- coleta de `executeQuery`;
- simulacao de begin, commit e rollback durante a coleta;
- replay rigoroso das consultas e respostas;
- validacao da ordem e do texto das consultas;
- escape local de strings e blobs.

### 3.7. `sources/nekiro-tfs-1.5-7.72/src/playeriomanager.h`

Declara:

- fila e worker de login;
- fila e worker de logout;
- reservas por nome e ID;
- controle de saves pendentes;
- snapshots imutaveis;
- callbacks ao Dispatcher;
- inicializacao, drain e shutdown.

### 3.8. `sources/nekiro-tfs-1.5-7.72/src/playeriomanager.cpp`

Implementa:

- validacao do servico no startup;
- um worker de login e um worker de logout;
- canais separados para login, logout e handoff duravel;
- reservas durante carregamento e save;
- bloqueio de relog durante job pendente;
- IDs unicos para jobs;
- captura do snapshot de logout;
- envio sincronizado do `PREPARE` duravel antes da destruicao do `Player`;
- aplicacao posterior do job pelo worker;
- callbacks sem ponteiros para objetos vivos;
- recuperacao de saves que ficaram sob responsabilidade do servico;
- espera controlada no shutdown.

Funcoes centrais:

- `PlayerIOManager::start()`;
- `PlayerIOManager::shutdown()`;
- `PlayerIOManager::enqueueLogin()`;
- `PlayerIOManager::enqueueLogout()`;
- `PlayerIOManager::drain()`.

### 3.9. `sources/nekiro-tfs-1.5-7.72/src/playerioservice.cpp`

Novo processo responsavel pelo acesso pesado ao MariaDB.

Implementa:

- leitura da configuracao;
- listener TCP restrito ao loopback;
- conexao propria com MariaDB;
- consultas individuais e em lote;
- transacao `REPEATABLE READ` para snapshot de login;
- preparacao duravel dos saves;
- aplicacao transacional;
- revisoes monotonicas por personagem;
- hash do payload;
- idempotencia;
- rejeicao de revisoes velhas ou conflitantes;
- retries para falhas transitorias;
- recuperacao de jobs `PENDING` e `APPLYING`;
- comandos administrativos de inspecao e retry.

O servico cria e migra dinamicamente as tabelas:

- `player_io_jobs`;
- `player_io_state`.

Nao houve alteracao correspondente em `schema.sql`.

### 3.10. `sources/nekiro-tfs-1.5-7.72/player-io-service.conf.dist`

Template generico contendo endereco, porta e configuracoes de conexao ao MariaDB.

### 3.11. `server/player-io-service.conf`

Configuracao do ambiente atual de testes:

- `127.0.0.1`;
- porta `7180`;
- MariaDB local;
- banco de testes.

### 3.12. `server/start-player-io-service.bat`

Inicializador manual simples do servico.

### 3.13. `D:\tibia-dev-tools\Start-PlayerIOService.ps1`

Inicializador robusto utilizado pelo atalho correto:

- valida executavel e configuracao;
- inicia o servico oculto;
- registra stdout e stderr;
- aguarda a porta `127.0.0.1:7180` por ate 20 segundos;
- apresenta erro claro se o servico nao ficar pronto.

## 4. Arquivos existentes modificados

### 4.1. `sources/nekiro-tfs-1.5-7.72/src/database.h`

- mutex do banco adaptado para uso em metodos constantes;
- `DBResult` passou a suportar resultados copiados em memoria;
- adicionadas colunas, valores opcionais e indice da linha atual.

### 4.2. `sources/nekiro-tfs-1.5-7.72/src/database.cpp`

- `beginTransaction`, `commit`, `rollback`, `executeQuery`, `storeQuery` e escape de blobs reconhecem o escopo remoto;
- com escopo ativo, as operacoes sao remotas, coletadas ou reproduzidas;
- sem escopo, permanece o comportamento original do MariaDB;
- `DBResult` pode consumir tanto `MYSQL_RES` quanto resultados em memoria;
- valores SQL `NULL` foram preservados.

### 4.3. `sources/nekiro-tfs-1.5-7.72/src/iologindata.h`

Novas interfaces:

- `buildPlayerPreloadQuery`;
- `buildPlayerLoadQueries`;
- `materializePlayerLoginSnapshot`;
- `buildPlayerSaveSnapshot`;
- `savePlayerDirect`;
- opcoes para adiar carregamento de guild.

### 4.4. `sources/nekiro-tfs-1.5-7.72/src/iologindata.cpp`

- consultas de login organizadas em sequencia deterministica;
- worker executa a sequencia em transacao consistente;
- Dispatcher reproduz a sequencia com resultados imutaveis;
- materializacao continua reutilizando a logica original;
- guild foi separada por acessar objetos vivos/globais;
- IDs de guild e rank sao guardados e finalizados no Dispatcher;
- save pode ser coletado como lista imutavel de SQL;
- criado caminho direto para saves sincronizados necessarios;
- integracao com reservas do `PlayerIOManager`;
- corrigido o predicado do replay para coincidir com a consulta capturada: `id = X AND deletion = 0`;
- removidos logs temporarios de diagnostico.

O snapshot inclui o estado principal, inventario, depot, locker, inbox, store inbox, storages, spells e demais dados processados pelo save original.

### 4.5. `sources/nekiro-tfs-1.5-7.72/src/player.h`

Novos campos:

- `deferredGuildId`;
- `deferredGuildRankId`;
- controle de guild adiada;
- `databaseSaveEnabled`;
- `playerIOReservationId`.

### 4.6. `sources/nekiro-tfs-1.5-7.72/src/player.cpp`

- destrutor libera reserva restante;
- logout normal pode utilizar o fluxo assincrono;
- clean save, checkpoint coordenado e estados nao normais continuam sincronizados;
- snapshot e construido antes de destruir o objeto;
- apos handoff duravel, nao ocorre um segundo save concorrente;
- callback captura somente nome e dados imutaveis;
- falha anterior ao handoff pode usar fallback sincronizado;
- falha posterior ao handoff nao cria outro escritor.

### 4.7. `sources/nekiro-tfs-1.5-7.72/src/protocolgame.h`

Adicionada `finishNewPlayerLogin`, que concentra a parte final comum do login.

### 4.8. `sources/nekiro-tfs-1.5-7.72/src/protocolgame.cpp`

- novo fluxo de login assincrono;
- reserva do nome antes do worker;
- callback volta ao Dispatcher;
- valida se o protocolo ainda esta conectado;
- verifica account ID;
- cria `Player` somente no Dispatcher;
- materializa resultados copiados;
- finaliza guild no Dispatcher;
- libera reservas em sucesso ou falha;
- reutiliza a finalizacao do login original;
- mantem fallback sincronizado se o servico estiver desativado.

Alteracoes antigas de CAM presentes nesse arquivo nao pertencem a este intervalo.

### 4.9. `sources/nekiro-tfs-1.5-7.72/src/configmanager.h`

Adicionados indices para habilitacao, host e porta do servico.

### 4.10. `sources/nekiro-tfs-1.5-7.72/src/configmanager.cpp`

Carrega:

- `playerIoServiceEnabled`;
- `playerIoServiceHost`;
- `playerIoServicePort`.

### 4.11. `sources/nekiro-tfs-1.5-7.72/config.lua.dist`

Valores padrao:

```lua
playerIoServiceEnabled = false
playerIoServiceHost = "127.0.0.1"
playerIoServicePort = 7180
```

### 4.12. `server/config.lua`

No ambiente de teste:

```lua
playerIoServiceEnabled = true
playerIoServiceHost = "127.0.0.1"
playerIoServicePort = 7180
```

### 4.13. `sources/nekiro-tfs-1.5-7.72/src/otserv.cpp`

- inicializa `PlayerIOManager` depois da preparacao do banco;
- impede startup incompleto se o servico habilitado estiver indisponivel;
- encerra o manager em caminhos de falha.

### 4.14. `sources/nekiro-tfs-1.5-7.72/src/game.cpp`

Alteracoes especificas deste recurso:

- clean save aguarda por ate 30 segundos o drain dos saves assincronos;
- falha de drain participa do resultado do clean save;
- shutdown encerra `PlayerIOManager` antes de scheduler, database tasks e Dispatcher.

As grandes rotinas de floor persistence presentes no arquivo sao anteriores.

### 4.15. `sources/nekiro-tfs-1.5-7.72/src/dispatchermetrics.h`

Adicionado `DispatcherMetricsSuppressionScope`.

### 4.16. `sources/nekiro-tfs-1.5-7.72/src/dispatchermetrics.cpp`

Implementada supressao `thread_local` para que funcoes reutilizadas pelo worker nao sejam contabilizadas como trabalho do Dispatcher.

A instrumentacao geral de login/logout ja existia; somente a supressao pertence a este intervalo.

### 4.17. `sources/nekiro-tfs-1.5-7.72/src/CMakeLists.txt`

Adicionou ao TFS:

- `playerioclient.cpp`;
- `playeriodatabase.cpp`;
- `playeriomanager.cpp`;
- `playerioprotocol.cpp`.

### 4.18. `sources/nekiro-tfs-1.5-7.72/CMakeLists.txt`

Adicionou o executavel `player_io_service`, ligado a Boost System, Threads e MariaDB/MySQL client.

Opcoes temporarias de AddressSanitizer e de desativacao de IPO/LTO utilizadas no diagnostico foram posteriormente removidas.

### 4.19. `server/tfs.bat`

- verifica se `player_io_service.exe` esta rodando;
- inicia o servico quando necessario;
- aguarda brevemente antes de iniciar o TFS.

### 4.20. `D:\tibia-dev-tools\Start-Tibia-Server.cmd`

Passou a iniciar, nesta ordem:

1. MariaDB;
2. `player_io_service`;
3. TFS.

Isso corrigiu o fechamento do TFS pelo atalho correto quando o servico ainda nao estava aberto.

## 5. Correcoes posteriores a auditoria independente

### 5.1. Objeto `Player` atravessando threads

O fluxo final transporta somente resultados SQL e dados imutaveis. O `Player` e criado e modificado apenas no Dispatcher.

### 5.2. Callback usando `Player*` destruido

Callbacks de logout capturam somente nome, IDs e estado imutavel.

### 5.3. Relog durante save pendente

Foram adicionadas reservas locais e verificacao do journal remoto.

### 5.4. Dois escritores apos handoff duravel

Depois que o servico aceita o `PREPARE`, o TFS nao retorna ao save direto.

### 5.5. Versionamento insuficiente

O servico passou a manter revisoes monotonicas por personagem.

### 5.6. Idempotencia incompleta

Job ID, revisao e hash do payload sao validados. Reaplicar o mesmo job confirmado nao duplica a operacao.

### 5.7. Falha entre preparacao e aplicacao

O payload permanece no journal e pode ser recuperado pelo servico.

### 5.8. Snapshot de login inconsistente

As consultas passaram a executar em batch com transacao `REPEATABLE READ`.

### 5.9. Guild acessada fora do Dispatcher

O worker captura somente IDs. A resolucao de objetos ocorre no Dispatcher.

### 5.10. Shutdown com saves pendentes

Foram adicionados drain, recuperacao dos jobs duraveis e ordem explicita de shutdown.

### 5.11. Divergencia entre consulta capturada e replay

Corrigido o predicado exato da consulta principal do personagem.

### 5.12. Metricas falsas do Dispatcher

O trabalho executado pelo worker deixou de contaminar essas metricas.

## 6. Limites deliberados do escopo

Nao foram transferidos ao novo processo:

- persistencia de tiles;
- houses;
- mail;
- checkpoints coordenados;
- death bundles;
- decay;
- CAM forense;
- GUID investigativo;
- quarentena;
- weekly reset;
- playershop;
- combate;
- movimentacao de criaturas;
- autosend;
- coleta de espectadores.

Esses sistemas podem interagir com login/logout, mas continuam sob a arquitetura anterior.

## 7. Estado atual do runtime

Executaveis instalados:

```text
D:\tibia-oldschool\server\tfs.exe
D:\tibia-oldschool\server\player_io_service.exe
```

Atalho correto:

```text
C:\Users\guisu\OneDrive\Area de Trabalho\01 - Servidor TFS (banco de teste).lnk
```

Destino do atalho:

```text
D:\tibia-dev-tools\Start-Tibia-Server.cmd
```

Na verificacao final, o lancador abriu:

- MariaDB;
- servico de I/O na porta `7180`;
- TFS nas portas `7171` e `7172`.

Uma copia antiga de runtime em outro pacote da area de trabalho recebeu um executavel durante a investigacao. Isso foi um artefato de implantacao, nao uma alteracao adicional no codigo-fonte.

## 8. Pontos recomendados para revisao independente

Para uma segunda auditoria, recomenda-se examinar principalmente:

1. se o `PREPARE` sincronizado no logout ainda pode causar freeze perceptivel;
2. se todas as rotas de save respeitam as reservas do manager;
3. se os caminhos de fallback nunca iniciam um segundo escritor depois do handoff;
4. se todas as consultas do login estao presentes no snapshot consistente;
5. se a politica de retry diferencia corretamente erros transitorios e permanentes;
6. se revisao e hash impedem aplicacao fora de ordem;
7. se o journal e recuperado corretamente depois de crash do TFS ou servico;
8. se o shutdown garante propriedade clara para cada job pendente;
9. se a coleta do snapshot no Dispatcher tem custo aceitavel com inventarios grandes;
10. se a restricao ao loopback e suficiente para o ambiente futuro em Linux.

