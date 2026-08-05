# Tibia Oldschool Modern

Data da reestruturacao: 2026-05-21

## Objetivo

Criar um OTServer com alma oldschool, mas experiencia moderna:

- client custom obrigatorio;
- WASD, smooth walking e UI refinada;
- protocolo alvo 7.72;
- combate, vocacoes, runas, loot, economia e progressao com feeling antigo;
- sistemas modernos futuros, como Hunt Analyzer, Loot Statistics, Bestiary, Charm, Market custom e Relic Box;
- economia desenhada com foco forte em controle de inflacao.

O projeto deve separar claramente:

- protocolo: 7.72;
- core: o mais saudavel possivel;
- ruleset: oldschool/hibrido;
- client: moderno/custom;
- graficos: old sprites agora, HD depois.

## Decisao Atual

A tentativa anterior com base 8.60 downgrade foi encerrada. O protocolo 8.60 nao sera a identidade tecnica do projeto.

Nova direcao de teste:

- usar `Nekiro/TFS-1.5-Downgrades`, branch `7.72`, como laboratorio tecnico;
- validar primeiro se o servidor compila, sobe e conversa com client 7.72 sem crash/dump;
- manter `OTCv8/otclientv8` precompilado como client de teste inicial;
- decidir depois se Nekiro vira base real ou apenas referencia para portar 7.72 para um TFS mais novo.

## Estrutura Atual

- `C:\tibia-oldschool\otclientv8-master`: client preservado pelo usuario.
- `C:\tibia-oldschool\client`: pasta de client preservada.
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72`: source Nekiro 7.72.
- `C:\tibia-oldschool\builds\nekiro-tfs-1.5-7.72`: build CMake/Visual Studio.
- `C:\tibia-oldschool\server`: pacote montado para teste local.
- `C:\tibia-oldschool\tools\vcpkg`: vcpkg local recriado para dependencias.

## Base Testada

Repositorio:

- `https://github.com/nekiro/TFS-1.5-Downgrades.git`

Branch:

- `7.72`

Commit clonado:

- `a467543`

Observacao do proprio README:

- nao e uma distribuicao "download and run";
- monsters e spells provavelmente nao estao 100% corretos.

## Ambiente

Ferramentas detectadas/usadas:

- Visual Studio 2022 Professional;
- CMake em `C:\Program Files\CMake\bin\cmake.exe`;
- MariaDB 10.11 em `C:\Program Files\MariaDB 10.11`;
- vcpkg local em `C:\tibia-oldschool\tools\vcpkg`.

## Bancos

Bancos removidos nesta rodada:

- `oldschooldb`;
- `otservdb`;
- `otserv`.

Banco preservado:

- `otserver`.

Motivo: o usuario citou `otservdb` ou `otserv`; `otserver` nao bate exatamente com os nomes pedidos, entao foi deixado intacto por seguranca.

## Dependencias Instaladas

Via vcpkg `x64-windows`:

- `boost-date-time`;
- `boost-filesystem`;
- `boost-iostreams`;
- `boost-system`;
- `boost-asio`;
- `boost-variant`;
- `boost-lockfree`;
- `cryptopp`;
- `fmt`;
- `libmariadb`;
- `pugixml`;
- `luajit`.

## Ajustes De Build

Foram feitos ajustes pequenos para compilar a base de 2021 com dependencias atuais:

- troca de `boost::asio::io_service` para `boost::asio::io_context`;
- troca de `expires_from_now` para `expires_after`;
- troca de `address_v4::from_string` por `boost::asio::ip::make_address`;
- troca de `to_ulong` por `to_uint`;
- `io_context::work` atualizado para `executor_work_guard`;
- casts explicitos em alguns enums usados com `fmt`;
- CMake ajustado para targets modernos `cryptopp::cryptopp` e `unofficial::libmariadb`.
- conexao MariaDB local ajustada para nao exigir TLS no ambiente de desenvolvimento Windows.

Arquivos alterados:

- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\CMakeLists.txt`;
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\connection.h`;
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\connection.cpp`;
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\scheduler.h`;
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\scheduler.cpp`;
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\server.h`;
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\server.cpp`;
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\signals.h`;
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\signals.cpp`;
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\iomapserialize.cpp`;
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\iomarket.cpp`.
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\database.cpp`.

## Comandos Usados

Limpeza inicial preservando client:

```bat
cd /d C:\tibia-oldschool
for %D in (builds docs experiments server sources tools) do @if exist %D rmdir /s /q %D
if exist PASSWORDALL.txt del /q PASSWORDALL.txt
```

Remocao de bancos antigos:

```bat
"C:\Program Files\MariaDB 10.11\bin\mysql.exe" -uroot -p123456 -e "DROP DATABASE IF EXISTS oldschooldb; DROP DATABASE IF EXISTS otservdb; DROP DATABASE IF EXISTS otserv; SHOW DATABASES;"
```

Clone do Nekiro:

```bat
cd /d C:\tibia-oldschool\sources
git clone --branch 7.72 --single-branch https://github.com/nekiro/TFS-1.5-Downgrades.git nekiro-tfs-1.5-7.72
```

vcpkg:

```bat
cd /d C:\tibia-oldschool\tools
git clone https://github.com/microsoft/vcpkg.git
cd /d C:\tibia-oldschool\tools\vcpkg
bootstrap-vcpkg.bat
vcpkg.exe install boost-date-time boost-filesystem boost-iostreams boost-system cryptopp fmt libmariadb pugixml luajit --triplet x64-windows
vcpkg.exe install boost-asio boost-variant boost-lockfree --triplet x64-windows
```

CMake/build:

```bat
cd /d C:\tibia-oldschool\builds
cmake -S C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72 -B C:\tibia-oldschool\builds\nekiro-tfs-1.5-7.72 -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\tibia-oldschool\tools\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DSKIP_GIT=ON
cmake --build C:\tibia-oldschool\builds\nekiro-tfs-1.5-7.72 --config Release --parallel
```

Montagem do pacote de teste:

```bat
cd /d C:\tibia-oldschool
if not exist server mkdir server
xcopy /E /I /Y sources\nekiro-tfs-1.5-7.72\data server\data
copy /Y sources\nekiro-tfs-1.5-7.72\config.lua.dist server\config.lua
copy /Y sources\nekiro-tfs-1.5-7.72\schema.sql server\schema.sql
copy /Y sources\nekiro-tfs-1.5-7.72\key.pem server\key.pem
copy /Y builds\nekiro-tfs-1.5-7.72\Release\tfs.exe server\tfs.exe
copy /Y C:\tibia-oldschool\tools\vcpkg\installed\x64-windows\bin\*.dll C:\tibia-oldschool\server\
```

## Resultado

Build Release concluido com sucesso:

- `C:\tibia-oldschool\builds\nekiro-tfs-1.5-7.72\Release\tfs.exe`
- pacote de teste em `C:\tibia-oldschool\server\tfs.exe`

Ainda nao foi criado banco novo para o teste 7.72.

Ainda nao foi iniciado o servidor Nekiro.

## Proximo Teste

1. Definir nome do novo banco de teste, recomendado: `oldschool772db`.
2. Criar banco e usuario ou reutilizar usuario local temporario.
3. Importar `C:\tibia-oldschool\server\schema.sql`.
4. Ajustar `C:\tibia-oldschool\server\config.lua`.
5. Colocar `Tibia.dat` e `Tibia.spr` 7.72 no client OTCv8.
6. Rodar `C:\tibia-oldschool\server\tfs.exe` em primeiro plano.
7. Testar login, walking, containers, look/use, summon, attack/follow, loot, escadas e estabilidade.

## Andamento 2026-05-21 - Banco E Config

Banco criado para o teste atual:

- banco: `oldschool772db`
- usuario: `oldschool772`
- senha temporaria: `123456`
- host: `127.0.0.1`
- porta: `3306`

Schema importado:

- `C:\tibia-oldschool\server\schema.sql`

Config ajustada:

- `C:\tibia-oldschool\server\config.lua`

Campos atualizados:

- `motd = "Welcome to Tibia Oldschool 7.72 test!"`
- `serverName = "Tibia Oldschool 7.72 Test"`
- `mysqlUser = "oldschool772"`
- `mysqlPass = "123456"`
- `mysqlDatabase = "oldschool772db"`
- `classicAttackSpeed = true`

## Smoke Test 2026-05-21 - Boot

Resultado:

- o servidor passou por config, banco, scripts, items, outfits e map load;
- o boot chegou ate `Tibia Oldschool 7.72 Test Server Online!`;
- o erro inicial de TLS/SSL do MariaDB foi resolvido no source.

Arquivo de log do smoke test:

- `C:\tibia-oldschool\server\boot-smoke.log`

Comando para subir em primeiro plano:

```bat
cd /d C:\tibia-oldschool\server
tfs.exe
```

## Conta De Teste 2026-05-21

Conta criada para validar login sem Account Manager:

- conta: `100000`
- senha temporaria: `123456`
- tipo da conta: `6` (`ACCOUNT_TYPE_GOD`)

Personagens criados:

- `Lancellin`
  grupo `1`, level `8`, vocation `4` (Knight)
- `GM Lancellin`
  grupo `6`, level `8`, vocation `4` (Knight)

Posicao inicial:

- town `1` (`Nekiro's Town`)
- `1022, 1026, 7`

## Ajuste 2026-05-21 - Cadencia De Ataque

Observacao de teste:

- andando continuamente com tecla pressionada, ataques a distancia pediam pausa ou atrasavam demais;
- o feeling de walking ja estava muito bom, entao a prioridade passou a ser desacoplar melhor a cadencia do ataque da fila de actions comuns.

Ajuste aplicado:

- `classicAttackSpeed = true` em `C:\tibia-oldschool\server\config.lua`

Motivo tecnico:

- nessa base, `classicAttackSpeed = false` faz armas interrompiveis respeitarem `canDoAction()` e a fila de action task;
- com `classicAttackSpeed = true`, o agendamento do ataque fica regular e menos dependente dessa fila, o que tende a melhorar ranged enquanto o jogador anda.

## Ajuste 2026-05-21 - Use Action Durante Walking

Observacao de teste:

- `follow`, melee andando, ranged andando, diagonal, loot e containers estavam bons;
- o problema remanescente era `use item` durante walking continuo: abrir backpack, comer food e usos pequenos pediam uma micro-pausa.

Leitura tecnica:

- `Player::onWalk` estava limpando `actionTaskEvent` e empurrando `nextAction` para o fim de cada passo;
- isso fazia a action generica concorrer com o walking, e podia cancelar ou postergar usos pequenos enquanto o player seguia andando.

Ajuste aplicado no source:

- arquivo: `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\player.cpp`
- funcao: `Player::onWalk(Direction& dir)`
- remocao de:
  - `setNextActionTask(nullptr);`
  - `setNextAction(OTSYS_TIME() + getStepDuration(dir));`

Objetivo:

- desacoplar walking da fila generica de actions;
- manter o passo regulado pelo sistema de movement, sem bloquear uso de item simples durante caminhada.

Build:

- recompilado com sucesso em `C:\tibia-oldschool\builds\nekiro-tfs-1.5-7.72\Release\tfs.exe`
- como o servidor de teste estava aberto no momento da troca, a nova build foi copiada provisoriamente para `C:\tibia-oldschool\server\tfs-next.exe`

## Ajuste 2026-05-21 - Torch

Observacao de teste:

- a tocha nao estava acendendo ao usar no jogo.

Diagnostico:

- os itens da tocha existiam em `C:\tibia-oldschool\server\data\items\items.xml`, com cadeia de decay pronta;
- faltava apenas a action Lua correspondente em `data/actions`.

Ajuste aplicado:

- criado `C:\tibia-oldschool\server\data\actions\scripts\other\torch.lua`
- registradas actions para os itens:
  - `2050 -> 2051`
  - `2052 -> 2053`
  - `2054 -> 2055`
- depois, ampliado para comportamento de toggle manual:
  - `2051 -> 2050`
  - `2053 -> 2052`
  - `2055 -> 2054`
- depois, ampliado para preservar a carga restante ao apagar:
  - atributo interno custom usado: `__torchRemainingDuration`
  - ao apagar, a duracao restante e salva nesse atributo;
  - ao reacender, a duracao e restaurada e o atributo temporario e limpo;
  - nada disso e exibido visualmente ao jogador.

Observacao:

- esse ajuste nao exige recompilacao; basta reiniciar o servidor para recarregar `actions.xml` e o script Lua.
- o teste visual de luz ficou inconclusivo nesta rodada porque:
  - o client possui opcao de `full light`;
  - mesmo desativando, o mapa de teste atual e aberto, pequeno e sem cavernas;
  - isso dificulta separar `action da tocha` de `luz global do mundo` em um ambiente realmente escuro.

## Ajuste 2026-05-21 - Noite Forcada Para Teste

Objetivo:

- validar luz global, tocha e legibilidade visual sem depender do horario automatico do mundo nem de caverna.

Ajuste aplicado:

- `C:\tibia-oldschool\server\config.lua`
  - `defaultWorldLight = false`
- `C:\tibia-oldschool\server\data\globalevents\scripts\startup.lua`
  - `setWorldLight(40, 215)` no startup

Observacao:

- esse modo deixa o servidor sempre em noite forcada apos reiniciar;
- e um ajuste de teste, facil de reverter depois.

## Importacao 2026-05-22 - Tibia Legacy World

Objetivo:

- testar mapa real 7.72 com o core Nekiro atual, sem ainda substituir o datapack inteiro.

Fonte usada:

- `https://github.com/peonso/tibialegacyserver`

Arquivos importados:

- `C:\tibia-oldschool\server\data\world\world.otbm`
- `C:\tibia-oldschool\server\data\world\world-spawn.xml`
- `C:\tibia-oldschool\server\data\world\world-house.xml`

Backup criado antes da troca:

- `C:\tibia-oldschool\experiments\backups\world-20260522-131323`

Observacoes tecnicas:

- o mapa veio compactado em `world.7z` no repositório doador;
- foi extraido para `C:\tibia-oldschool\experiments\tibialegacy-world-extract\world.otbm`;
- nossa base ja tinha praticamente o mesmo conjunto de monsters do doador, entao a primeira tentativa foi `mapa + houses + spawns`, sem substituir o diretório inteiro de monsters.

Resultado do smoke test:

- o servidor subiu com sucesso e chegou em `Tibia Oldschool 7.72 Test Server Online!`
- log salvo em `C:\tibia-oldschool\server\boot-import-test.log`

Pendencias observadas no boot:

- muitos NPCs referenciados no spawn nao existem ainda no nosso `data/npc`;
- varios monsters carregaram, mas com spells faltando, por exemplo:
  - `paralyze_marid`
  - `djinn electrify`
  - `haste_monk`
  - `haste_scarab`
  - `paralyze_banshee`
  - e outros semelhantes;
- varios `empty spawn` apareceram no `world-spawn.xml`;
- alguns poucos spawn warnings na inicializacao:
  - `fire elemental`
  - `tiger`

Leitura:

- a compatibilidade estrutural do mapa foi validada;
- a proxima camada de trabalho passa a ser conteudo:
  - NPCs faltando
  - spells de monster faltando
  - revisao de spawns vazios/problematicos

## Ajuste 2026-05-22 - Personagem De Teste

- Lancellin ajustado diretamente no banco para testes de mapa e progressao.
- Novos valores: level 300, exp 107015000, health/max 4565, mana/max 1510, cap 7770, maglevel 10.
- Skills: club 100, sword 100, axe 100, shielding 100, dist 90, fist 80.
- Vocation mantida como Knight (4).
- Observacao: requer relog para refletir na sessao do player.

## Ajuste 2026-05-22 - Reversao Do Feeling E Char De Teste

- revertido `Player::onWalk(Direction& dir)` em `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\player.cpp`:
  - restaurado `setNextActionTask(nullptr);`
  - restaurado `setNextAction(OTSYS_TIME() + getStepDuration(dir));`
- revertido `classicAttackSpeed` em `C:\tibia-oldschool\server\config.lua`:
  - `true -> false`
- ajuste temporario para teste de combate:
  - `C:\tibia-oldschool\server\data\monster\rat.xml`
  - `health now/max: 20 -> 5000`
- novo personagem criado para testes controlados:
  - `Lancellin Pala`
  - vocation `3` (`Paladin`)
  - level `70`
  - exp `5246300`
  - health/max `805`
  - mana/max `1020`
  - maglevel `20`
  - cap `1675`
  - skills: dist `90`, shielding `85`, melee `40`
- observacao:
  - o servidor foi encerrado a forca para remover processo orfao em `7171/7172`;
  - `C:\tibia-oldschool\server\tfs.exe` foi atualizado com a nova build apos a reversao.


## Ajuste 2026-05-22 - Corpse Owner E Tocha Curta

- Tocha reduzida para 1 minuto de teste em C:\tibia-oldschool\server\data\actions\scripts\other\torch.lua.
- corpse owner desabilitado no source:
  - removido bloqueio de abertura em src\actions.cpp`r
  - removida atribuicao de owner em src\monster.cpp`r
- Objetivo: alinhar com a proposta oldschool/PvP normal, sem travar corpos por ownership.

## Observacoes De Teste 2026-05-22 - Pala 70

- Regressao confirmada apos a reversao do feeling:
  - comer andando voltou a falhar ou exigir timing muito preciso;
  - paladin andando continuamente para a mesma direcao tende a nao soltar flecha sem mini pausa;
- Observacao do usuario sobre o client:
  - dash em   parece piorar o stutter;
  - com pequeno delay acima de  , o walking ficou mais liso;
  - isso sugere forte influencia do pacing do client na sensacao final de movimento.
- Observacao adicional:
  - speed percebida do paladin level 70 parece abaixo do esperado, mas a investigacao foi adiada para nao misturar variaveis.


## Ajuste 2026-05-22 - Reativacao De ClassicAttackSpeed, Tocha Nativa E Moeda

- classicAttackSpeed reativado em C:\tibia-oldschool\server\config.lua (	rue).
- Tocha mantida com liga/desliga manual, mas sem forcar duracoes customizadas:
  - C:\tibia-oldschool\server\data\actions\scripts\other\torch.lua`r
  - ao reacender sem estado salvo, a tocha volta a usar a duracao nativa do item do items.xml;
  - ao apagar manualmente, continua guardando a carga restante sem mostrar isso ao jogador.
- Conversao automatica de moeda por clique direito desativada:
  - C:\tibia-oldschool\server\data\scripts\actions\others\change_gold.lua`r
  - conversao futura sera feita por item/sistema proprio do projeto.
- Observacao:
  - tochas antigas criadas sob a logica anterior podem manter estado antigo de duracao;
  - para teste limpo, ideal usar tochas novas apos este restart.


## Ajuste 2026-05-22 - Experimento De Duracao Da Tocha

- Mantida a primeira fase acesa da tocha em 600s (2051).
- Reduzidas as fases seguintes para 30s cada em C:\tibia-oldschool\server\data\items\items.xml:
  - 2053: 300 -> 30`r
  - 2055: 300 -> 30`r
- Objetivo: confirmar na pratica se a fase inicial esta sendo pulada pelo script atual e medir a cadeia restante com mais rapidez.
- Observacao: testar com tocha nova criada apos este restart.


- Experimento refinado: 2053 e 2055 ajustados de 30s para 5s cada, mantendo 2051 = 600s, para diagnostico mais rapido da cadeia de decay da tocha.


## Ajuste 2026-05-22 - Curva Nova Da Tocha

- Nova curva definida em C:\tibia-oldschool\server\data\items\items.xml:
  - 2051 (fase forte): 100s`r
  - 2053 (fase media): 200s`r
  - 2055 (fase final): 200s`r
- A fase final agora nao possui mais decayTo; ao terminar, a tocha desaparece em vez de virar 2056 (burnt down torch).
- Correcao no script C:\tibia-oldschool\server\data\actions\scripts\other\torch.lua:
  - na primeira ativacao, se nao houver carga salva, a duracao padrao do item nao e mais removida;
  - isso permite que a primeira fase forte realmente exista e dure.
- Observacao: testar com tocha nova criada apos este restart.

- Escala de teste reduzida por 10 para validacao rapida da tocha: 2051 = 10s, 2053 = 20s, 2055 = 20s.
- Correcao do ultimo estagio da tocha: 2055 recebeu decayTo = 0 em items.xml, pois no core do TFS isso significa remover o item ao fim da duracao.


## Ajuste 2026-05-22 - Normalizacao Parcial Pos-Testes

- `classicAttackSpeed = true` mantido como decisao permanente em `C:\tibia-oldschool\server\config.lua`.
- `change_gold.lua` mantido desativado como decisao permanente:
  - `C:\tibia-oldschool\server\data\scripts\actions\others\change_gold.lua`
- Rat restaurado para vida normal em:
  - `C:\tibia-oldschool\server\data\monster\rat.xml`
  - HP atual: 20/20.
- Tocha saiu da escala de teste rapido e passou para uma curva provisoria jogavel em:
  - `C:\tibia-oldschool\server\data\items\items.xml`
  - `2051 = 200s`
  - `2053 = 200s`
  - `2055 = 150s`
  - `2055 -> decayTo = 0`
- Ciclo de luz de teste alterado para alternar automaticamente:
  - noite inicial no startup em `C:\tibia-oldschool\server\data\globalevents\scripts\startup.lua`
  - alternancia a cada 60s por `C:\tibia-oldschool\server\data\globalevents\scripts\daynight_test.lua`
  - registro em `C:\tibia-oldschool\server\data\globalevents\globalevents.xml`
- Conteudo do mapa:
  - ate este ponto, o mapa importado segue sem NPCs funcionais visiveis nas areas testadas;
  - continua pendente revisar/importar camada de NPCs do doador ou substituir por uma base propria auditada.


## Ajuste 2026-05-23 - Regeneracao Alta Para Testes

- Regeneracao de vida e mana elevada para `500` em todas as vocacoes, apenas para acelerar a rodada atual de testes:
  - `C:\tibia-oldschool\server\data\XML\vocations.xml`
- Mantidos os `ticks` originais de cada vocation; alteramos apenas:
  - `gainhpamount`
  - `gainmanaamount`
- Motivo:
  - facilitar testes de runas, cooldowns, mana consumption e action queue sem depender de longas pausas de recuperacao.
- Observacao aberta:
  - runas de ataque ainda estao com quantidade/custo incorretos em relacao a referencia informada pelo usuario (`https://vlt.nostalther.com/spells.php`);
  - ajuste fino de cooldown entre runas sera tratado depois.


## Ajuste 2026-05-23 - Sincronizacao Parcial De Mana/Level Com Nostalther

- Arquivo ajustado:
  - `C:\tibia-oldschool\server\data\spells\spells.xml`
- Fonte de referencia:
  - `https://vlt.nostalther.com/spells.php`
- Nesta rodada foram sincronizados principalmente:
  - `mana`
  - `level/lvl`
  - alguns requisitos de `premium`
  - alguns `maglv` de uso em runas que estavam claramente fora do alvo
- Importante:
  - **nao** alteramos ainda a quantidade de cargas produzidas pelas conjuracoes;
  - essa parte ficara para uma tabela posterior do usuario.
- Diferencas locais encontradas em relacao ao Nostalther:
  - `Blank Rune` existe localmente e nao apareceu na pagina de referencia;
  - house spells (`House Door List`, `House Guest List`, `House Kick`, `House Subowner List`) sao spells de sistema e nao fazem parte da lista de gameplay do Nostalther;
  - `Stalagmite Rune` local usa as palavras de `Envenom` (`adevo res pox`), indicando legado divergente da referencia;
  - `Cure Poison Rune` local corresponde funcionalmente ao `Antidote Rune` da referencia.


## Ajuste 2026-05-23 - Exaust De Runas E Spells Ofensivas

- Runas ofensivas/uso `useItemEx` passaram a respeitar `2000ms` via:
  - `C:\tibia-oldschool\server\config.lua`
  - `timeBetweenExActions = 2000`
- Spells ofensivas instant foram padronizadas para `2000ms` em:
  - `C:\tibia-oldschool\server\data\spells\spells.xml`
- Ajustadas explicitamente nesta rodada:
  - `Energy Strike`
  - `Flame Strike`
  - `Force Strike`
  - `Fire Wave`
  - `Energy Beam`
  - `Great Energy Beam`
  - `Berserk`
  - `Ultimate Explosion`
  - `Poison Storm`
  - `Energy Wave`
- Observacao:
  - o comportamento de "segurar e soltar depois" nas runas continua existindo no fluxo do core (`playerUseItemEx/playerUseWithCreature`) quando a acao chega antes do `nextAction`;
  - nesta rodada alteramos apenas a duracao-base do exaust/cooldown, nao a politica de fila.


## Ajuste 2026-05-23 - Sincronizacao Tibiantis

- Referencia principal para spells/runas trocada para:
  - `https://tibiantis.info/library/spells`
- Ferramenta criada:
  - `C:\tibia-oldschool\tools\sync_tibiantis_spells.py`
- Relatorio gerado em:
  - `C:\tibia-oldschool\tools\tibiantis_sync_report.txt`
- Sincronizados com o Tibiantis:
  - `mana`
  - `premium`
  - `maglv` de cast (`instant`)
  - `maglv` de uso (`rune`)
  - `charges` das runas
  - quantidades de conjuracao das runas nos scripts `conjuring/*.lua`
- Observacao:
  - o caso `Summon Creature` exigiu cuidado especial por conta da forma como o Tibiantis apresenta o parametro (`utevo res para`);
  - localmente mantivemos `words="utevo res"` e alinhamos `mana="0"` e `maglv="16"`.
- Entradas vistas no Tibiantis e ausentes/divergentes na nossa base local:
  - `Desintegrate Spell (exito tera)`
  - `Discharge (exana vis)`
  - `Extinguish (exana flam)`


## Ajuste 2026-05-23 - Runas Sem Fila De Exaust

- Arquivo alterado:
  - `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\game.cpp`
- Mudanca:
  - `playerUseItemEx` e `playerUseWithCreature` nao agendam mais uma task quando o player tenta usar a runa/uso ofensivo antes do `nextAction`;
  - agora retornam imediatamente `RETURNVALUE_YOUAREEXHAUSTED` com `CONST_ME_POFF`.
- Motivo:
  - eliminar o comportamento de "apertar cedo e a runa sair depois sozinha", que estava deixando o uso ofensivo buffered demais e pouco oldschool.
- Escopo:
  - alteracao focada no caminho de `useItemEx/useWithCreature`;
  - nao mexe, por enquanto, na politica de fila de outros usos mais comuns.


## Ajuste 2026-05-23 - Tolerancia Curta Para Runas E Volta Do Ciclo Normal De Luz

- Ciclo de luz voltou ao comportamento normal do core:
  - `C:\tibia-oldschool\server\config.lua`
    - `defaultWorldLight = true`
  - removido o teste de alternancia por minuto de:
    - `C:\tibia-oldschool\server\data\globalevents\globalevents.xml`
  - removido `setWorldLight(...)` forcado no startup:
    - `C:\tibia-oldschool\server\data\globalevents\scripts\startup.lua`
- Tolerancia curta para runas:
  - `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\game.cpp`
  - janela de tolerancia `100ms` no ajuste normal anterior;
  - posteriormente, para rodada de teste mais facil, essa janela foi elevada temporariamente para `2000ms` apenas para runas ofensivas e runas de cura (`IH/UH`);
  - se a runa entrar antes do exaust por ate essa margem, a base aceita reagendar so esse resto pequeno;
  - acima disso, continua retornando `YOUAREEXHAUSTED`.
- Diferenciacao de exaust para IH/UH:
  - `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\actions.cpp`
  - no ajuste normal anterior:
    - `Intense Healing Rune (2265)` e `Ultimate Healing Rune (2273)` usavam `1000ms`
    - demais runas continuavam em `2000ms`
  - para a rodada de teste atual:
    - runas ofensivas usam `5000ms`
    - `Intense Healing Rune (2265)` e `Ultimate Healing Rune (2273)` tambem usam `5000ms`
- Verificacao das magias de cura instant:
  - `exura`
  - `exura gran`
  - `exura vita`
  - `exura sio`
  - `exura gran mas res`
  - nesta base, elas continuam sem tolerancia adicional e usam o cooldown default do sistema de spells (`1000ms`) enquanto nao houver valor explicito diferente.


## Ajuste 2026-05-23 - Separacao De Exaust De Runas E Actions Comuns

- Problema observado:
  - usar uma runa colocava o jogador em `nextAction`;
  - enquanto esse tempo nao acabava, outras actions como mover item, acender tocha, comer food, arrastar a propria runa ou organizar bagloot entravam na mesma fila;
  - como `Player::onWalk` tambem atualiza `nextAction` com a duracao do passo, segurar WASD/seta podia atrasar actions comuns.
- Decisao:
  - runas passam a usar um relogio separado no player: `nextRuneAction`;
  - mover item e usar item comum deixam de depender do `canDoAction()` global;
  - a tolerancia de reenvio curto das runas usa uma task propria (`setNextRuneActionTask`) e nao a task generica de action.
- Arquivos alterados:
  - `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\player.h`
  - `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\player.cpp`
  - `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\game.cpp`
  - `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\actions.cpp`
- Resultado esperado:
  - andar usando runas;
  - andar atacando;
  - andar movendo itens/bagloot;
  - usar comida, tocha e itens de backpack sem esperar o exhaust da runa;
  - arrastar runas e itens sem aguardar a janela de aceitacao de runa.
- Shovel na areia:
  - `C:\tibia-oldschool\server\data\actions\scripts\tools\shovel.lua`
  - recebeu exaust proprio de `1000ms` apenas para o uso em areia (`groundId == 231`);
  - esse exaust nao bloqueia runa, tocha, food, movimento de item ou walking.
- Estado de teste atual:
  - runas ofensivas e IH/UH continuam temporariamente com `5000ms`;
  - tolerancia de teste continua temporariamente em `2000ms`;
  - objetivo e facilitar a validacao visual antes de voltar aos tempos finais.
- Build:
  - comando usado: `cmake --build C:\tibia-oldschool\builds\nekiro-tfs-1.5-7.72 --config Release`
  - binario copiado para: `C:\tibia-oldschool\server\tfs.exe`


## Ajuste 2026-05-23 - Comando Manual De Save

- Problema observado:
  - logout nao estava salvando corretamente o estado do personagem durante os testes;
  - usar `/closeserver shutdown` para salvar era funcional, mas ruim para iteracao.
- Solucao:
  - criado comando GM `/save` e alias `/saveall`;
  - ambos chamam a funcao nativa `saveServer()`, que salva players online, mapa e flush de database tasks pelo fluxo do core.
- Arquivos:
  - `C:\tibia-oldschool\server\data\talkactions\scripts\save.lua`
  - `C:\tibia-oldschool\server\data\talkactions\talkactions.xml`
- Como ativar em servidor ja aberto:
  - usar `/reload talkactions` com personagem GOD;
  - depois usar `/save` sempre que quiser persistir o estado sem desligar o servidor.


## Ajuste 2026-05-23 - IP De Teste Em Rede Local

- Para teste dentro da mesma casa/rede local, o servidor nao deve anunciar `127.0.0.1`.
- Arquivo:
  - `C:\tibia-oldschool\server\config.lua`
- Alteracao:
  - `ip = "192.168.2.119"`
- Motivo:
  - `127.0.0.1` funciona apenas na propria maquina;
  - para outro notebook na mesma rede Wi-Fi, o login precisa devolver o IP local da maquina host.
- Observacao:
  - se o IP local do host mudar no roteador/DHCP, esse valor deve ser atualizado antes de novo teste LAN.


## Ajuste 2026-05-23 - IP Publico Para Teste Externo

- Para teste com amigo fora da rede local, o servidor deve anunciar o IP publico atual da conexao.
- Arquivo:
  - `C:\tibia-oldschool\server\config.lua`
- Alteracao:
  - `ip = "170.246.210.181"`
- Observacoes:
  - esse valor pode mudar se o provedor renovar o IP publico;
  - para teste externo real, ainda e necessario que as portas `7171` e `7172` estejam liberadas no firewall/roteador.


## Ajuste 2026-05-23 - Retorno Ao Modo Local

- Encerrado o teste de IP publico por enquanto.
- Arquivo:
  - `C:\tibia-oldschool\server\config.lua`
- Alteracao:
  - `ip = "127.0.0.1"`
- Motivo:
  - continuar o desenvolvimento local sem depender de NAT loopback, 4G/5G, amigo externo ou roteador;
  - a parte de teste externo fica pausada ate retomarmos com ambiente/client mais adequado.


## Ajuste 2026-05-23 - Tempos De Exaust Apos Validar Fluidez

- Actions comuns ficaram fluidas apos separar `nextRuneAction` do `nextAction` global.
- Tempos ajustados:
  - runas ofensivas: `2000ms`;
  - `Intense Healing Rune (2265)` e `Ultimate Healing Rune (2273)`: `1000ms`;
  - tolerancia curta de reenvio para runas ofensivas e IH/UH: `70ms`;
  - shovel na areia: `300ms`, proprio do script da shovel;
  - fishing rod: `500ms`, proprio do script de fishing.
- Arquivos:
  - `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\actions.cpp`
  - `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\game.cpp`
  - `C:\tibia-oldschool\server\data\actions\scripts\tools\shovel.lua`
  - `C:\tibia-oldschool\server\data\actions\scripts\tools\fishing.lua`
- Operacao:
  - `tfs.exe` sera deixado offline por padrao;
  - usuario prefere abrir o servidor manualmente em primeiro plano a partir deste ponto.


## Ajuste 2026-05-23 - Pesca Sem Worm E Cooldown Por SQM

- Pesca ajustada em `C:\tibia-oldschool\server\data\actions\scripts\tools\fishing.lua`:
  - removida a exigencia de worm;
  - corrigido o item do peixe comum para `2667` (antes estava entregando `2267`);
  - mantido o exaust proprio da vara em `500ms`;
  - adicionado cooldown por coordenada de `10 minutos` para spots normais de pesca;
  - se o SQM ja tiver entregue peixe e ainda estiver em cooldown, novas tentativas nao dao peixe e nao sobem `SKILL_FISHING`;
  - mesmo com o SQM em cooldown, a animacao visual da pesca continua acontecendo para preservar o comportamento oldschool e nao facilitar macro trivial.
- Observacao:
  - o cooldown por SQM fica em memoria durante a execucao do servidor;
  - spots especiais que ja se auto-exaurem por transformacao (`15401`, `7236`) continuam usando a propria mecanica deles.


## Ajuste 2026-05-23 - Remocao De Worms Do Servidor

- Fontes futuras removidas:
  - drops de worm (`3976`) removidos dos monsters que ainda entregavam esse item.
- Limpeza de persistencia e mundo carregado:
  - `C:\tibia-oldschool\server\data\globalevents\scripts\startup.lua`
  - no startup, o servidor agora:
    - apaga worms das tabelas `player_depotlockeritems`, `player_depotitems`, `player_inboxitems`, `player_storeinboxitems` e `player_items`;
    - percorre `Game.getHouses()` e remove worms de tiles e containers carregados nas houses.
- Limpeza de inventario ao entrar:
  - `C:\tibia-oldschool\server\data\creaturescripts\scripts\login.lua`
  - qualquer worm remanescente carregado no personagem e removido no login.
- Protecao de runtime:
  - `C:\tibia-oldschool\server\data\events\scripts\player.lua`
  - se um player tentar mover um worm que tenha escapado de alguma carga antiga, o item e removido.
- Risco residual conhecido:
  - `C:\tibia-oldschool\server\data\items\items.xml` ainda define o item `3976`;
  - `C:\tibia-oldschool\server\data\monster\serpent spawn.xml` ainda usa `3976` em um ataque de outfit, que nao e uma fonte de loot/bait;
  - se quisermos eliminar tambem essa referencia visual no futuro, sera uma decisao separada de balance/theme e nao de economia.
- Procedimento pratico:
  - como parte da limpeza acontece no startup, estas mudancas pedem reinicio do servidor;
  - depois do primeiro boot limpo, vale usar `/save` para persistir a limpeza das houses no proximo ciclo.


## Build 2026-05-24 - OTAcademy OTCv8

- Fonte testada:
  - `https://github.com/OTAcademy/otclientv8`
- Source local:
  - `C:\tibia-oldschool\sources\otacademy-otclientv8`
- Commit clonado:
  - `08d348b`

Pacote de dependencias usado nesta rodada:

- arquivo baixado:
  - `C:\tibia-oldschool\tools\downloads\vcpkg-otacademy.rar`
- hash SHA256:
  - `80ea38b8aaf03b9d5528186bcfe53152b22edfca9ae35818a8bbb88bbf0831b8`
- extraido em:
  - `C:\tibia-oldschool\tools\vcpkg-otacademy\vcpkg`

Diagnostico:

- o projeto possui configuracoes `OpenGL|Win32`, `OpenGL|x64`, `DirectX|Win32` e `DirectX|x64`;
- o pacote extraido continha dependencias prontas para `x86-windows-static`, mas nao para `x64-windows-static`;
- o `vcxproj` definia `VcpkgTriplet`, porem nao importava `vcpkg.props` e `vcpkg.targets`, o que impedia o MSBuild de enxergar os includes do vcpkg sem `vcpkg integrate install` global.

Ajuste local aplicado:

- arquivo:
  - `C:\tibia-oldschool\sources\otacademy-otclientv8\vc17\otclient.vcxproj`
- imports adicionados:
  - `$(VcpkgRoot)scripts\buildsystems\msbuild\vcpkg.props`
  - `$(VcpkgRoot)scripts\buildsystems\msbuild\vcpkg.targets`

Tentativas relevantes:

- tentativa 1:
  - `OpenGL|x64` com manifest habilitado
  - falhou por falta de includes do vcpkg
- tentativa 2:
  - `OpenGL|Win32` com manifest habilitado
  - falhou por `builtin-baseline` divergente entre o `vcpkg.json` do repo e o `vcpkg` do pacote
- tentativa 3:
  - `OpenGL|Win32` com `VcpkgEnableManifest=false`
  - compilou com sucesso usando o pacote `x86-windows-static` ja instalado

Comando que funcionou:

```bat
"C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" "C:\tibia-oldschool\sources\otacademy-otclientv8\vc17\otclient.sln" /m /p:Configuration=OpenGL /p:Platform=Win32 /p:VcpkgRoot=C:\tibia-oldschool\tools\vcpkg-otacademy\vcpkg\ /p:VcpkgEnableManifest=false /p:VcpkgInstalledDir=C:\tibia-oldschool\tools\vcpkg-otacademy\vcpkg\installed\ /v:minimal
```

Resultado:

- binario gerado:
  - `C:\tibia-oldschool\sources\otacademy-otclientv8\otclient_gl.exe`
- tamanho observado:
  - `11.374.080` bytes
- nao houve `.dll` adicionais geradas ao lado do executavel nesta rodada.

Leitura tecnica:

- a compilacao do client finalmente foi validada no nosso ambiente;
- por enquanto, o caminho comprovado e `OpenGL|Win32`;
- ainda vale investigar um fluxo limpo para `x64`, idealmente com:
  - `vcpkg` alinhado ao `builtin-baseline` do repo, ou
  - um novo conjunto `x64-windows-static` construido localmente.


## Ajuste 2026-05-24 - OTCv8 Step Duration 7.72

Problema observado:

- em velocidade alta, haste, GM ou dash, o personagem fazia micro-pausas entre tiles;
- o problema era mais perceptivel na areia `231`;
- clicando no minimapa/mapa tambem acontecia, o que indicou que nao era apenas input continuo de teclado.

Investigacao:

- `items.otb` confirmou estes `groundSpeed`:
  - dirt `103`: `110`
  - sand `231`: `160`
  - cobbled pavement `724`: `100`
  - grass `4526`: `150`
- o servidor arredonda `getStepDuration()` para multiplos de `50ms` em:
  - `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\creature.cpp`
- o OTCv8 so fazia esse arredondamento para clients `>= 900`, deixando o protocolo `7.72` animar pelo tempo cru.

Exemplo em speed alto `1500`:

- areia `231`:
  - client antes: aproximadamente `106ms`
  - servidor: `150ms`
  - resultado esperado: pausa visual ate a confirmacao do servidor
- grass `4526`:
  - client antes: `100ms`
  - servidor: `100ms`
  - resultado esperado: muito mais liso

Ajuste aplicado:

- arquivo:
  - `C:\tibia-oldschool\sources\otacademy-otclientv8\src\client\creature.cpp`
- mudanca:
  - o client agora arredonda o step duration para `serverBeat` sempre que `GameNewWalking` nao estiver ativo, inclusive no protocolo `7.72`.

Build:

```bat
"C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" "C:\tibia-oldschool\sources\otacademy-otclientv8\vc17\otclient.sln" /m /p:Configuration=OpenGL /p:Platform=Win32 /p:VcpkgRoot=C:\tibia-oldschool\tools\vcpkg-otacademy\vcpkg\ /p:VcpkgEnableManifest=false /p:VcpkgInstalledDir=C:\tibia-oldschool\tools\vcpkg-otacademy\vcpkg\installed\ /v:minimal
```

Resultado:

- build concluido com sucesso;
- binario atualizado:
  - `C:\tibia-oldschool\sources\otacademy-otclientv8\otclient_gl.exe`

Proximo teste manual:

- testar `GM Lancellin` em areia `231`, dirt `103`, pavement `724` e grass `4526`;
- testar com e sem dash;
- testar click-to-walk/map click;
- testar haste em personagem normal.

Observacao futura:

- se ainda houver sensacao de "steps em blocos", uma linha de pesquisa e reduzir o beat de walking de `50ms` para `25ms`, mas isso precisa ser feito de forma sincronizada no servidor e no client para evitar voltar a dessincronizar a animacao.

## Ajuste 2026-05-24 - Venore NPCs Minimos

Objetivo deste passe:

- destravar o primeiro loop jogavel em Venore antes de tentar consertar centenas de NPCs/spawns do mapa inteiro;
- permitir compra de itens basicos para teste de cidade, morte e hunt;
- manter a regra permanente de nao permitir troca facil de moedas.

Arquivos criados:

- `C:\tibia-oldschool\server\data\npc\Yberius.xml`
- `C:\tibia-oldschool\server\data\npc\Hugo.xml`

Arquivos alterados:

- `C:\tibia-oldschool\server\data\npc\scripts\bank.lua`

Decisao:

- `Yberius` foi reaproveitado como vendedor de suprimentos basicos em Venore.
- `Hugo` foi reaproveitado como banker em Venore.
- a troca de moedas pelo dialogo do banker ficou desativada, alinhada com a decisao economica do projeto.

Inventario inicial do `Yberius`:

- bag
- backpack
- rope
- shovel
- pick
- machete
- torch
- fishing rod

Observacoes:

- este passe nao tenta restaurar todos os NPCs ausentes do mapa;
- este passe reduz apenas parte do spam de boot relacionado a `Yberius` e `Hugo`;
- o proximo passo natural e testar Venore no jogo e decidir se adicionamos um terceiro NPC local para magia/runas ou se primeiro auditamos morte, depot e rota para dragon.

## Ajuste 2026-05-24 - Nomes de Monstros e Resposta de NPC

Monstros:

- capitalizacao em massa do atributo `name` nos XMLs de monstros em `C:\tibia-oldschool\server\data\monster`;
- exemplos apos ajuste:
  - `Rat`
  - `Cave Rat`
  - `Dragon`
  - `Demon`

Motivo:

- o jogo estava exibindo nomes como `rat`, `demon` e `cave rat`;
- a lookup interna do servidor normaliza para lowercase, entao a mudanca visual nao quebra os spawns.

NPCs:

- removido o delay global de resposta do `npcsystem`;
- arquivos alterados:
  - `C:\tibia-oldschool\server\data\npc\lib\npcsystem\npchandler.lua`
  - `C:\tibia-oldschool\server\data\npc\lib\npcsystem\npcsystem.lua`

Detalhe tecnico:

- antes: `NPCHANDLER_TALKDELAY = TALKDELAY_ONTHINK` e `talkDelayTime = 1`
- agora: `NPCHANDLER_TALKDELAY = TALKDELAY_NONE` e `talkDelayTime = 0`

Observacao:

- a interacao de compra ainda esta no modo antigo do `ShopModule` desta base, entao alguns NPCs continuam exigindo frases mais especificas do que o ideal. Isso e separado do lag de resposta e pode ser tratado em um passe proprio depois.

## Ajuste 2026-05-24 - ShopModule em Modo Hibrido

Arquivos alterados:

- `C:\tibia-oldschool\server\data\npc\lib\npcsystem\modules.lua`
- `C:\tibia-oldschool\server\data\npc\lib\npcsystem\npcsystem.lua`

Mudanca:

- `SHOPMODULE_MODE` mudou de `SHOPMODULE_MODE_TALK` para `SHOPMODULE_MODE_BOTH`
- `SHOP_TRADEREQUEST` foi ampliado de `trade` para:
  - `trade`
  - `offer`
  - `shop`

Efeito esperado:

- continua funcionando o modo antigo de frases como `buy backpack`;
- passa a funcionar tambem a janela moderna de NPC trade no OTCv8 ao falar `trade`, `offer` ou `shop`;
- isso reduz a friccao sem exigir reescrita imediata de todos os NPCs do mapa.

Leitura tecnica:

- esta base tinha suporte nativo no servidor para `openShopWindow`, `sendShop` e `sendSaleItemList`;
- o OTCv8 tambem possui o modulo `game_npctrade`, entao a limitacao principal estava na configuracao do Lua `npcsystem`, nao na stack tecnica.

## Ajuste 2026-05-25 - Compatibilidade de Pacote NPC Trade para 7.72

Problema encontrado:

- mesmo com `SHOPMODULE_MODE_BOTH`, falar `trade`, `offer` ou `shop` nao abria a janela;
- causa real: o servidor estava enviando o pacote de NPC trade em formato mais novo do que o client `7.72` espera.

Desalinhamentos identificados:

- `sendShop` sempre enviava nome do NPC, mas o client `7.72` nao consome esse campo;
- `sendShop` sempre enviava quantidade de itens em `uint16`, mas o client `7.72` espera `uint8`;
- `sendSaleItemList` sempre enviava dinheiro em `uint64`, mas o client `7.72` espera `uint32`.

Arquivo alterado:

- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\protocolgame.cpp`

Regra aplicada:

- protocolo `< 910`: nao envia nome do NPC no pacote `0x7A`;
- protocolo `< 986`: envia quantidade de itens do shop em `uint8`;
- protocolo `< 972`: envia dinheiro do `player goods` em `uint32`.

Build:

```bat
"C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" "C:\tibia-oldschool\builds\nekiro-tfs-1.5-7.72\tfs.sln" /m /p:Configuration=Release /p:Platform=x64 /t:tfs /v:minimal
```

Saida:

- `C:\tibia-oldschool\builds\nekiro-tfs-1.5-7.72\Release\tfs.exe`
- copiado para:
  - `C:\tibia-oldschool\server\tfs.exe`

## Pesquisa 2026-05-25 - Persistencia, Rollback Curto e Recuperacao

Objetivo:

- reduzir perda de progresso em crash ou queda;
- evitar dupe/clone causado por saves inconsistentes;
- separar claramente:
  - persistencia operacional de curto prazo;
  - backup e disaster recovery;
  - auditoria e compensacao.

Leitura da base atual:

- `Game::saveGameState()`:
  - salva storages de conta;
  - salva todos os players online;
  - chama `Map::save()`;
  - faz `g_databaseTasks.flush()`.
- `Map::save()` hoje grava itens de casa via `tile_store`.
- `tile_store` hoje e usado para casas, nao para ground loot geral.
- logout de player ja chama `IOLoginData::savePlayer(this)` com retry.
- `signals.cpp` ja chama `g_game.saveGameState()` em sinais trataveis, mas isso nao protege contra queda brusca, energia, `SIGKILL` ou crash fatal sem chance de flush limpo.

Conclusoes praticas:

1. So mexer no MariaDB nao resolve rollback de OT.
   O banco melhora durabilidade, mas se o servidor ainda mantem estado importante so em memoria entre saves, a perda continua existindo.

2. `saveServer()` frequente nao e estrategia suficiente.
   Alem de peso e travada, historicamente isso facilita cenarios de clone quando combinado com crash e estados de trade/logout mal sincronizados.

3. O melhor desenho e em camadas:
   - camada A: durabilidade do banco;
   - camada B: salvamento frequente de dados criticos do player;
   - camada C: eventos especiais com save imediato;
   - camada D: backup fisico/logico + PITR;
   - camada E: auditoria para compensation.

Plano recomendado:

### Fase 1 - Curto Prazo

- manter `saveServer()` apenas para server save e uso administrativo;
- criar autosave leve por player em intervalo curto, por exemplo `2` a `5` minutos, sem global save;
- salvar imediatamente em eventos de alto risco:
  - logout;
  - trade concluido;
  - deposito/retirada de itens sensiveis;
  - morte;
  - mudancas economicas importantes;
- registrar fila de dirty players para nao salvar o mundo todo a cada evento;
- evitar spam de save por player com debounce simples.

### Fase 2 - Durabilidade do Banco

- revisar configuracao do MariaDB para durabilidade real:
  - `innodb_flush_log_at_trx_commit=1`
  - `sync_binlog=1`
- isso aumenta seguranca em crash, com custo de I/O;
- se o host/disco for fraco, medir impacto antes de relaxar qualquer parametro.

### Fase 3 - Recuperacao Operacional

- backup fisico com `mariadb-backup`;
- retenção curta de binlogs para point-in-time recovery;
- rotina de backup:
  - full diario;
  - incremental se fizer sentido no host;
  - binlog retido por alguns dias;
- objetivo:
  - restaurar ate um horario especifico quando necessario, sem depender so do ultimo dump.

### Fase 4 - Auditoria e Anti-Dupe

- log forte de mortes, perdas, trades, drops e picks de itens relevantes;
- itens criticos de morte podem receber rastreio de evento:
  - owner original;
  - posicao de drop;
  - quem pegou;
  - quando saiu do chao;
- isso ajuda a compensar sem duplicar item indevidamente.

### Fase 5 - Ground Loot Persistente

- expandir o modelo de `tile_store` para tiles persistentes fora de house;
- nao salvar o mapa inteiro;
- salvar so tiles marcados por regra:
  - fora de cidade;
  - zonas selecionadas;
  - talvez so quando houver item elegivel;
- server save diario:
  - cidades limpas;
  - tiles persistentes preservados;
- clean semanal:
  - limpa o acumulado persistido fora de casa.

Decisao de arquitetura sugerida:

- tratar rollback curto e PITR como problemas distintos:
  - rollback curto = salvar estado operacional com granularidade alta;
  - PITR = restauracao via backup fisico + binlog;
- nao usar autosave global agressivo como substituto de estabilidade;
- nao confiar em “salvar no crash” como estrategia principal;
- investir primeiro em:
  - player autosave incremental;
  - save imediato em eventos sensiveis;
  - durabilidade correta do MariaDB;
  - log de auditoria.

Observacao:

- quando implementarmos CAM/replay, a compensation auditavel fica muito mais forte;
- sem CAM, ainda vale construir logs estruturados desde ja, porque isso alimenta tanto anti-dupe quanto investigacao de bug.

Estado atual do MariaDB neste ambiente:

- `innodb_flush_log_at_trx_commit=1` -> bom para durabilidade por transacao;
- `log_bin=OFF` -> hoje nao ha binlog ativo;
- `sync_binlog=0` -> mesmo que o binlog fosse ligado, ainda nao estaria na configuracao mais segura;
- `binlog_format=MIXED`.

Leitura pratica:

- para rollback curto de player/progresso, ja da para avancar pelo lado do TFS + saves incrementais;
- para point-in-time recovery real, ainda faltara ligar binlog e ajustar a politica de backup/retenção.

## Implementacao 2026-05-25 - Persistencia Fase 1

Objetivo:

- reduzir rollback sem criar janela facil de clone de itens;
- tratar house/mapa e player como um conjunto quando houver itens relevantes envolvidos;
- evitar salvar player isolado apos trade/house antes de termos auditoria e rastreio de itens.

Mudancas:

- `ProtocolGame::release()` agora tenta remover/salvar o player quando a conexao cai e o personagem pode deslogar com seguranca;
- adicionado globalevent `Hourly Save`, com `saveServer()` a cada 1 hora;
- o save horario salva players online e houses/mapa persistente da base no mesmo ciclo.

Decisoes anti-clone:

- nao salvar player isoladamente apos trade neste momento;
- nao salvar player isoladamente apos mover item de/para house neste momento;
- motivo: se salvar apenas um lado, e o servidor cair antes do outro lado ser persistido, pode surgir cenario de clone.

Limites conhecidos:

- fechar o processo do TFS de forma bruta pode interromper qualquer save em andamento;
- save horario limita o rollback operacional, mas nao substitui shutdown limpo;
- item tracking e auditoria forte serao a proxima camada antes de saves parciais mais agressivos.

Teste de clone 2026-05-25:

- cenario testado:
  - item raro salvo na house;
  - item removido da house para o player;
  - player desloga, salvando inventario;
  - item volta para a house sem novo logout;
  - processo `tfs.exe` morto com `taskkill /F`.
- resultado: item apareceu no inventario e na house apos restart.
- causa: player e house estavam persistidos em momentos diferentes.

Correcao aplicada:

- `Player:onItemMoved` foi habilitado;
- qualquer movimento que toque house chama `saveServer()` imediatamente apos o movimento;
- objetivo: alinhar player + house no mesmo ciclo quando item entra ou sai de house.

Observacao:

- esta solucao e conservadora e pode pesar se houver muitos movimentos em house;
- e aceitavel para fechar a porta de clone nesta fase;
- a evolucao correta sera item tracking + dirty house/player + fila de persistencia auditavel.

Pesquisa expandida 2026-05-25:

- OTLand tem discussoes recentes sobre salvar player em `onMoveItem` com cooldown para reduzir rollback, mas a propria sugestao alerta para monitorar CPU/performance.
- Outra discussao sobre otimizacao de saves aponta exatamente o nosso problema: se o player e salvo em um momento e house/depot em outro, crash pode virar clone.
- Discussões antigas de house saving reforcam que house items dependem de save do servidor/house, nao apenas logout do player.

Decisao apos pesquisa:

- nao usar `saveServer()` a cada movimento de item em house;
- usar `onItemMoved` apenas para marcar house/world como dirty;
- antes de logout de player, se houver house dirty, salvar somente as houses sujas com `house:save()`;
- manter save horario como camada de rollback curto;
- save global manual/horario limpa o estado dirty;
- futura melhoria: persistir tambem players dirty de forma controlada e criar item tracking.

Motivo:

- salva suficiente para fechar o clone testado;
- evita travar o servidor a cada bagloot jogada dentro de casa;
- preserva um caminho de evolucao para tracking/auditoria de item raro.

Implementacao revisada:

- `Persistence.dirtyHouseIds` guarda os ids das houses alteradas;
- `Player:onItemMoved` marca as houses afetadas;
- `PlayerLogout` chama `Persistence.saveDirtyHouses("player logout")`;
- `saveServer()` manual/horario limpa a lista dirty, porque ja salva todo o estado de house.

## Limpeza de Boot 2026-05-25

Objetivo:

- reduzir o ruido de inicializacao do `tfs.exe`;
- separar erro estrutural da base de simples referencia faltando no pacote importado;
- manter o recorte 7.72 jogavel sem carregar centenas de warnings irrelevantes.

Problemas encontrados no primeiro boot auditado:

- spell reservada `Invisible` em `data/spells/spells.xml`;
- centenas de referencias de NPC no `world-spawn.xml` sem XML correspondente em `data/npc`;
- spells custom de monstros importados com nomes nao suportados pela base, como `paralyze_*`, `haste_*`, `skills_*` e `djinn electrify`;
- centenas de spawns vazios;
- alguns warnings pontuais restantes de radius, summon chance e monsters que nao conseguiam nascer.

Correcao aplicada:

- spell `Invisible` renomeada para `Invisibility`;
- em `src/monsters.cpp`, adicionados aliases de compatibilidade:
  - `paralyze_*` -> `speed` com padrao conservador de paralyze;
  - `haste_*` -> `speed` com padrao conservador de haste;
  - `skills_*` -> `strength`;
  - `djinn electrify` / `djinn electrify area` -> `energy`;
- `world-spawn.xml` limpo de referencias para NPCs inexistentes;
- removidos spawns vazios apos a limpeza de NPCs;
- ajustados dois spawns com `radius="100"` para `radius="30"`;
- corrigido `chance="3000"` do summon de `Mahrdis` para `chance="30"`;
- removidos os spawns especificos que falhavam para `Tiger` e dois `Fire Elemental`.

Seguranca operacional:

- backup do spawn original desta rodada salvo em `experiments/world-spawn.pre-boot-clean-2026-05-25.xml`.

Resultado:

- boot auditado novamente sem linhas `Error -` nem `Warning -` no log capturado;
- servidor chegou ate `Tibia Oldschool 7.72 Test Server Online!` com inicializacao limpa.

## Matriz Pos-Limpeza de Boot 2026-05-25

### Seguro

- renome da magia `Invisible` para `Invisibility` em `data/spells/spells.xml`;
- limpeza de referencias para NPCs inexistentes no `world-spawn.xml`;
- remocao de spawns vazios gerados pelo pacote importado;
- ajuste de dois `radius="100"` para `radius="30"` no spawn;
- correcao do summon `chance="3000"` -> `chance="30"` em `data/monster/bosses/mahrdis.xml`;
- remocao dos spawns pontuais que nunca conseguiam nascer (`Tiger` e dois `Fire Elemental`).

### Precisa validar in game

- compatibilidade adicionada em `src/monsters.cpp` para:
  - `paralyze_*` -> `speed` negativo;
  - `haste_*` -> `speed` positivo;
  - `skills_*` -> `strength`;
  - `djinn electrify` / `djinn electrify area` -> `energy`.
- risco: o monstro carrega e usa uma spell funcional, mas o comportamento pode nao ser identico ao pack original importado.

### Conteudo faltante

- 334 NPCs que o mapa referenciava e nao existem hoje em `data/npc`;
- cidades, ilhas, quests e fluxos que dependiam desses NPCs continuam incompletos ate reintroducao dirigida;
- bosses/custom content herdados do mapa importado ainda precisam de auditoria individual.

## Achados de Gameplay 2026-05-25

- `utana vid` validado in game, funcionando corretamente;
- troca de andar aplicava exaust artificial via `stairJumpExhaustion`;
- spears estavam quebrando ao usar;
- magias custom de monstros precisam de validacao pratica, mesmo com o boot limpo.

Correcao aplicada:

- `stairJumpExhaustion = 0` em `config.lua`;
- `breakchance="0"` para spear (`id 2389`) em `data/weapons/weapons.xml`.

Observacao:

- a troca de andar ainda deve respeitar qualquer exaust real ja existente de spell/runa usada antes;
- o ajuste remove apenas a pacify artificial aplicada pela base ao subir/descer andar.

## Checklist de Testes - Player Spells e Runas

- `exura`, `exura gran`, `exura vita`, `exura sio`;
- `utevo lux`, `utevo gran lux`, `utana vid`, `utamo vita`, `utani hur`;
- `sudden death rune`, `heavy magic missile rune`, `ultimate healing rune`, `intense healing rune`;
- conjuracao:
  - `blank rune`
  - `conjure arrow`
  - `conjure bolt`
  - `conjure power bolt`
- validacoes por teste:
  - mana correta;
  - exaust correta;
  - nao entrar em fila depois da janela de tolerancia;
  - funcionar andando;
  - funcionar igual antes/depois de subir ou descer escada.

## Checklist de Testes - Magias de Monstros

- `Hydra`: validar se o paralyze entra com efeito coerente;
- `Marid` / `Blue Djinn` / `Efreet`: validar `djinn electrify` e `djinn electrify area`;
- `Giant Spider`, `Tarantula`, `Monk`, `Orc Rider`, `Orc Berserker`, `Tiger`: validar `haste_*`;
- `Banshee`, `Warlock`, `Lich`, `Mummy`, `Elder Beholder`, `Serpent Spawn`, `Ancient Scarab`: validar `paralyze_*`;
- bosses que usam `skills_*`: validar por ultimo, depois do mundo base estar confiavel.

Metodo sugerido:

- testar primeiro em criaturas comuns e repetiveis;
- usar GM apenas para observacao, mas confirmar em char normal quando o efeito envolver ritmo de combate;
- anotar por criatura:
  - spell saiu;
  - efeito visual saiu;
  - dano/condicao saiu;
  - intensidade parece correta;
  - precisa ajuste fino ou nao.

## Ajuste de Follow de Monstros 2026-05-26

Problema observado:

- monstros encostavam no player, mas demoravam cerca de 1-2 segundos para perseguir novamente quando o player se afastava 1 SQM;
- isso permitia abusar de `hit and run` em melee, evitando contato de forma artificial.

Causas encontradas:

- o follow path de criaturas recalculava naturalmente em janelas longas quando nada forçava atualizacao;
- em `Creature::getStepDuration()`, monstros perto do alvo recebiam um multiplicador artificial de `* 3` no tempo de passo.

Correcao aplicada:

- `Monster::onCreatureMove()` agora agenda `Game::updateCreatureWalk()` quando o alvo seguido se move;
- removido o multiplicador `* 3` de movimento para monstros perto do alvo.

Arquivos:

- `src/monster.cpp`
- `src/creature.cpp`

Resultado tecnico:

- TFS recompilado em `Release`;
- `server/tfs.exe` atualizado;
- boot auditado sem `Error -` e sem `Warning -`.

Validacao pendente:

- testar com criaturas melee lentas e rapidas;
- testar player andando 1 SQM repetidamente;
- testar criatura em diagonal;
- confirmar que o monstro segue imediatamente sem alterar attack speed.

## Ajuste de Haste de Monstros 2026-05-26

Problema observado:

- `Monk` nao aparentava usar haste;
- `Giant Spider` nao aparentava usar strong haste.

Correcao aplicada:

- `Monk`: trocado `haste_monk` por spell nativa `speed` com `speedchange="500"` e `duration="10000"`;
- `Giant Spider`: trocado `haste_giantspider` por spell nativa `speed` com `speedchange="900"` e `duration="10000"`.
- ambas receberam `areaEffect="greenshimmer"` para exibir a animacao visual equivalente a `CONST_ME_MAGIC_GREEN`.

Arquivos:

- `data/monster/monk.xml`
- `data/monster/giant spider.xml`

Observacao:

- `speedchange` usa formula proporcional ao `baseSpeed` da criatura;
- valores escolhidos para teste: Monk com haste normal perceptivel, Giant Spider com haste forte;
- pode precisar ajuste fino apos teste in game.

Resultado tecnico:

- alteracao somente em XML, sem necessidade de recompilar;
- boot auditado sem `Error -` e sem `Warning -`.

Varredura completa aplicada:

- substituidos todos os `haste_*` restantes por `speed` nativo com `greenshimmer`;
- incluidos monstros comuns e bosses que ainda dependiam do alias generico em source.

Monstros cobertos na rodada:

- `Ancient Scarab`, `Assassin`, `Behemoth`, `Dark Monk`, `Dworc Voodoomaster`, `Gazer`, `Kongra`, `Orc Berserker`, `Orc Rider`, `Scarab`, `Serpent Spawn`, `Sibang`, `Tarantula`, `Tiger`, `Wild Warrior`;
- bosses: `Dharalion`, `Fernfang`, `Grorlam`, `Morguthis`, `Orshabaal`, `The Old Widow`.

Escala usada nesta rodada:

- haste normal perceptivel: `speedchange` entre `500` e `700`;
- haste forte: `speedchange` `900`.

Observacao:

- esta rodada prioriza fazer a skill acontecer e aparecer visualmente;
- o ajuste fino de intensidade continua dependente de teste in game, principalmente para kite de paladin e chase de criaturas rapidas.

## Lista Viva de Testes Pendentes

- `speed` de player:
  - comparar sensacao de velocidade por level;
  - validar `Boots of Haste` e conferir se a escala numerica esta coerente ou apenas dobrada internamente.
- `speed` de monstros:
  - testar `Monk`, `Giant Spider`, `Orc Berserker`, `Orc Rider`, `Serpent Spawn`, `Behemoth`, `Tarantula`, `Tiger`;
  - observar chase real, kite de paladin e se a haste parece normal ou forte demais.
- `paralyze_*`:
  - validar intensidade, duracao e frequencia em `Hydra`, `Banshee`, `Warlock`, `Lich`, `Mummy`, `Elder Beholder`, `Serpent Spawn`, `Ancient Scarab`, `Marid`, `Efreet`, `Vampire`, `Dworc Voodoomaster`, `Carniphila`.
- `djinn electrify`:
  - validar dano, efeito visual e area em `Marid`, `Blue Djinn`, `Green Djinn`, `Efreet`.
- `skills_*`:
  - validar por ultimo em bosses como `Dipthrah` e `Morguthis`.
- `player spells / runas`:
  - revisar mana, exaust, tolerancia, funcionamento andando e comportamento apos subir/descer andar.
- `stairs / floor change`:
  - confirmar novamente que subir/descer escada nao cria exaust artificial, mantendo apenas exaust real ja existente.
- `spears`:
  - confirmar em hunt real que nao quebram mais.
- `follow / chase`:
  - confirmar com monstros melee diferentes que nao ha mais janela de `hit and run` de 1 SQM.

## Ideia de Referencia Externa

- montar um comparativo com um TFS oldschool mais antigo, em mapa limpo e controlado, pode ser muito util para medir:
  - velocidade de player por level;
  - impacto real de `Boots of Haste`;
  - chase de criaturas;
  - sensacao de paralyze / haste.

Uso sugerido:

- nao como base para copiar implementacao cegamente;
- sim como regua pratica de feeling oldschool para comparar com o nosso 7.72.

## Validacao de Monstros 2026-05-27

- `paralyze_*`: validado in game pelo teste manual; comportamento aprovado nesta rodada.
- `djinn electrify` / `djinn electrify area`: validado in game em frequencia, efeito visual, dano e comportamento pontual; aprovado nesta rodada.

Observacao:

- `speed` continua sendo a parte mais dificil de validar sem uma referencia oldschool confiavel;
- proximo passo recomendado para speed: usar uma regua externa controlada antes de ajustar formula, `Boots of Haste` ou escalas numericas.

## Ajuste Temporario de Teste 2026-05-27

- `pzLocked = 0` em `config.lua` foi usado temporariamente para agilizar troca de personagens durante testes locais;
- objetivo: evitar espera de infight/PZ lock depois de fogo, energy ou combate comum;
- restaurado para `pzLocked = 60000`, valor padrao da base, apos o teste nao resolver o fluxo de logout.

## Experimento de Walking 10ms 2026-05-27

Objetivo:

- reduzir o micro stuttering residual do walking;
- manter client e server sincronizados, agora em granularidade de `10ms`.

Backups criados antes da alteracao:

- `C:\tibia-oldschool\experiments\backup-creature.cpp-pre-10ms-2026-05-27`
- `C:\tibia-oldschool\experiments\backup-otc-game.cpp-pre-10ms-2026-05-27`
- `C:\tibia-oldschool\experiments\backup-walking.lua-pre-10ms-2026-05-27`
- `C:\tibia-oldschool\experiments\tfs-pre-10ms-2026-05-27.exe`

Arquivos alterados:

- `sources\nekiro-tfs-1.5-7.72\src\creature.cpp`
  - arredondamento do `stepDuration` de multiplos de `50ms` para multiplos de `10ms`.
- `sources\otacademy-otclientv8\src\client\game.cpp`
  - `m_serverBeat` alterado de `50` para `10`.
- `sources\otacademy-otclientv8\modules\game_walking\walking.lua`
  - throttle do dash trocado de fixo `50ms` para `g_game.getServerBeat()`.

Build:

- server recompilado com sucesso;
- client recompilado com sucesso (`otclient_gl.exe`).

Checklist de validacao desta rodada:

- comparar walking normal em `dirt`, `grass`, `sand` e `pavement`;
- comparar com dash ligado e desligado;
- sentir se o micro stutter residual diminuiu ou se apareceu comportamento nervoso demais;
- observar se follow, diagonal e click-to-walk continuam estaveis.

## Experimento Visual GameNewUpdateWalk 2026-05-27

Motivacao:

- usuario percebeu uma especie de tripidacao visual durante o walking;
- FPS estava bom e o TFS parecia fluido nas acoes;
- suspeita principal passou a ser atualizacao visual/camera do client, nao atraso real do servidor.

Backup criado antes da alteracao:

- `C:\tibia-oldschool\experiments\backup-features.lua-pre-newupdatewalk-2026-05-27`

Arquivo alterado:

- `sources\otacademy-otclientv8\modules\game_features\features.lua`
  - habilitado `GameNewUpdateWalk` para protocolo `>= 770`.

Racional tecnico:

- sem `GameNewUpdateWalk`, o OTCv8 atualiza o walking em passos baseados em pixel;
- como a camera segue o `walkOffset` do personagem, a movimentacao do cenario pode parecer vibrar/tripidar mesmo quando o servidor esta fluido;
- `GameNewUpdateWalk` aumenta a frequencia visual de atualizacao do walking em funcao do FPS.

Build:

- client recompilado com sucesso (`otclient_gl.exe`).

Checklist de validacao:

- testar level baixo, onde a tripidacao parecia mais perceptivel;
- testar level alto/GM para garantir que nao ficou nervoso demais;
- comparar dash ligado/desligado;
- observar se o cenario parece deslizar melhor entre tiles.

## Pesquisa de Clients 2026-05-27

Contexto:

- o client atual compila e conecta, mas ainda ha uma tripidacao visual no walking;
- usuario considera voltar ao client OTCv8 anterior, que visualmente agradava mais, mas ainda nao tinhamos conseguido compilar naquela rodada inicial.

Candidatos principais:

- `OTCv8/otclientv8`
  - pronto para uso, muito popular, aceita `dat/spr`, tem binarios prontos e visual conhecido;
  - possui bot integrado por padrao, exigindo limpeza/controle para distribuicao;
  - source oficial historica pode exigir libs antigas para compilar.
- `OTAcademy/otclientv8`
  - fork mais recente do OTCv8, com Visual Studio 2022 e vcpkg mais viaveis;
  - ja foi compilado localmente neste projeto;
  - mantem heranca visual/arquitetural do OTCv8, mas ainda apresenta tripidacao visual a investigar.
- `OpenTibiaBR/otclient` / `OTClient Redemption` / `Mehah`
  - base mais moderna, C++20, suporte amplo de protocolos e features modernas;
  - tem releases recentes e suporte a protocolos 7.6 ate versoes modernas;
  - pode exigir mais trabalho para casar `dat/spr` oldschool e fluxo de assets 7.72;
  - melhor candidato tecnico de longo prazo, mas maior risco de configuracao inicial.
- `edubart/otclient`
  - base original e historica do OTClient;
  - bom para referencia arquitetural, mas antigo para uso direto em producao nova.
- client CipSoft original + IP changer
  - melhor fidelidade visual oldschool;
  - ruim para sistemas modernos, UI custom, extended opcodes, HD assets e client obrigatorio.
- web client baseado em Redemption/WebGL2
  - interessante para futuro marketing/acesso rapido;
  - nao deve ser client principal agora por exigir WebSocket/WSS/proxy/infra web.

Leitura inicial:

- melhor opcao imediata: continuar testando OTCv8/OTAcademy porque ja compila e conversa com nosso TFS;
- melhor opcao tecnica para reavaliar com calma: OTClient Redemption;
- melhor opcao de feeling visual antigo: OTCv8 pronto/anterior ou CipSoft original, mas o CipSoft limita demais o projeto;
- decisao ainda aberta: testar se a tripidacao visual e especifica do OTAcademy ou comum ao OTCv8 moderno.

## Build OTClient Redemption 2026-05-27

Objetivo:

- testar `OTClient Redemption/Mehah` como alternativa ao OTCv8 por causa da tripidacao visual do walking;
- validar especialmente o caminho recomendado pelo proprio projeto para `Nekiro TFS-1.5-Downgrades-7.72`.

Fonte:

- `https://github.com/opentibiabr/otclient`

Pasta:

- `C:\tibia-oldschool\sources\otclient-redemption`

Comandos executados:

- clone:
  - `git clone --depth 1 https://github.com/opentibiabr/otclient.git C:\tibia-oldschool\sources\otclient-redemption`
- tentativa inicial com preset oficial:
  - `set VCPKG_ROOT=C:\tibia-oldschool\tools\vcpkg&& cmake --preset windows-release`
  - resultado: dependencias instaladas com sucesso, mas o configure nao gerou `build.ninja` porque `ninja.exe` nao estava no PATH.
- configure com Visual Studio/MSBuild:
  - `set VCPKG_ROOT=C:\tibia-oldschool\tools\vcpkg&& cmake -S C:\tibia-oldschool\sources\otclient-redemption -B C:\tibia-oldschool\sources\otclient-redemption\build\windows-release-msbuild -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/tibia-oldschool/tools/vcpkg/scripts/buildsystems/vcpkg.cmake -DBUILD_STATIC_LIBRARY=ON -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_HOST_TRIPLET=x64-windows-static -DVCPKG_BUILD_TYPE=release -DOTCLIENT_BUILD_TESTS=OFF -DSPEED_UP_BUILD_UNITY=ON`
- build:
  - `cmake --build C:\tibia-oldschool\sources\otclient-redemption\build\windows-release-msbuild --config RelWithDebInfo -- /m`

Resultado:

- build concluido com sucesso;
- executavel gerado em `C:\tibia-oldschool\sources\otclient-redemption\RelWithDebInfo\otclient.exe`;
- copia colocada em `C:\tibia-oldschool\sources\otclient-redemption\otclient.exe` para facilitar execucao com `data`, `modules` e `mods` na raiz.

Warnings observados:

- `MSB8027`: dois arquivos `luafunctions.cpp` produzem saidas com mesmo nome intermediario;
- `LNK4098`: `LIBCMTD` conflita com outras bibliotecas;
- ambos foram warnings, nao impediram gerar o executavel.

Configuracoes aplicadas para 7.72:

- `data\setup.otml`
  - `force-new-walking-formula: true`
  - `item-ticks-per-frame: 500`
- criada pasta `data\things\772` para receber `Tibia.dat` e `Tibia.spr`.

Proximo teste:

- colocar `Tibia.dat` e `Tibia.spr` 7.72 em `C:\tibia-oldschool\sources\otclient-redemption\data\things\772`;
- abrir `C:\tibia-oldschool\sources\otclient-redemption\otclient.exe`;
- conectar em `127.0.0.1`, versao `772`, e comparar walking com level 8, level 70 e GM.

## Pesquisa de Walking/Stutter OTCv8 2026-05-27

Fontes avaliadas:

- `https://otland.net/threads/otclientv8-classic-ui-player-stutters-in-high-speed.303474/`
- `https://otland.net/threads/tfs-1-5-8-60-walk-delay.289889/#post-2761380`
- `https://github.com/OTCv8/forgottenserver/commit/286e747`

Achados relevantes:

- problema muito parecido foi relatado com `OTClientv8 Classic UI`, `TFS 1.5 Downgrade Nekiro 7.72` e high speed;
- `GameNewUpdateWalk` melhora ou resolve o walking normal, mas pode nao resolver high speed;
- `GameSlowerManualWalking` foi citado como melhoria parcial para manual walking;
- `GameNewWalking` nao e a mesma coisa que `GameNewUpdateWalk`;
- `GameNewWalking` usa protocolo custom do OTCv8, permitindo renderizar ate 2 passos sem confirmacao do servidor;
- `GameNewWalking` exige alteracoes C++ no servidor, incluindo awareness/view distance maior, e nao foi validado diretamente para protocolos abaixo de 8.6;
- um usuario relatou que `Mehah/OTClient Redemption` com `force-new-walking-formula: true` ficou perfeito no mesmo tipo de cenario;
- outro topico aponta `checkDecay` em `game.cpp` como possivel fonte de stutter em TFS 1.5, mas nosso caso parece mais visual/client-side porque as acoes do servidor estao fluidas.

Estado local apos leitura:

- ja habilitamos `GameNewUpdateWalk` no client atual;
- habilitamos `GameSlowerManualWalking` para experimento curto;
- ainda nao portamos `GameNewWalking` server-side;
- ainda nao testamos `OTClient Redemption` com `force-new-walking-formula`.

Proxima ordem recomendada:

- testar o client atual com `GameNewUpdateWalk`;
- se ainda houver tripidacao, fazer experimento pequeno com `GameSlowerManualWalking`;
- em paralelo, preparar teste limpo com `OTClient Redemption` e `force-new-walking-formula`;
- deixar `GameNewWalking` para depois, pois e mudanca grande de protocolo/server.

## Experimento GameSlowerManualWalking 2026-05-27

Backup criado antes da alteracao:

- `C:\tibia-oldschool\experiments\backup-features.lua-pre-slowermanualwalking-2026-05-27`

Arquivo alterado:

- `sources\otacademy-otclientv8\modules\game_features\features.lua`
  - habilitado `GameSlowerManualWalking` junto do bloco `version >= 770`.

Racional tecnico:

- o recurso adiciona `25ms` ao intervalo quando o player nao esta em server walking;
- pode suavizar parte do manual walking, mas pode deixar a resposta levemente mais pesada;
- objetivo e teste de feeling, nao decisao final.

Build:

- client recompilado com sucesso (`otclient_gl.exe`).

Checklist:

- testar char level baixo sem dash;
- testar char level baixo com dash;
- comparar responsividade contra o build anterior com apenas `GameNewUpdateWalk`;
- se o walking ficar mais pesado sem resolver a tripidacao, reverter este experimento.

## Ajuste WASD OTClient Redemption 2026-05-27

Contexto:

- o OTClient Redemption apresentou render/walking visualmente superior ao OTCv8/OTAcademy;
- clique no mapa/autowalk mostrou a velocidade real do personagem, inclusive em GM e level alto;
- WASD ainda tinha atrasos perceptiveis, principalmente ao trocar direcao e apos mudanca de andar;
- apos digitar e apertar Enter, o chat continuava ativo e exigia outro Enter para voltar ao WASD.

Backups criados:

- `C:\tibia-oldschool\experiments\backup-redemption-walk.lua-pre-wasd-10ms-2026-05-27`
- `C:\tibia-oldschool\experiments\backup-redemption-console.lua-pre-enter-wasd-2026-05-27`
- `C:\tibia-oldschool\experiments\backup-redemption-data_options.lua-pre-wasd-10ms-2026-05-27`
- `C:\tibia-oldschool\experiments\backup-redemption-game.cpp-pre-serverbeat-10ms-2026-05-27`
- `C:\tibia-oldschool\experiments\backup-redemption-game.h-pre-serverbeat-10ms-2026-05-27`

Arquivos alterados:

- `sources\otclient-redemption\modules\game_walk\walk.lua`
  - atrasos internos de WASD/turn/cancel/floor-change reduzidos para filosofia de `10ms`;
  - `setAutoRepeatDelay` do painel principal reduzido de `200ms` para `10ms`;
  - troca de direcao e key-up deixam de aplicar locks longos.
- `sources\otclient-redemption\modules\client_options\data_options.lua`
  - `returnDisablesChat = true`;
  - defaults de `walkTurnDelay`, `walkTeleportDelay` e `walkStairsDelay` para `10ms`.
- `sources\otclient-redemption\modules\game_console\console.lua`
  - `sendCurrentMessage()` agora volta para modo WASD apos enviar mensagem quando `returnDisablesChat` esta ativo.
- `sources\otclient-redemption\src\client\game.cpp`
- `sources\otclient-redemption\src\client\game.h`
  - `m_serverBeat` default alterado de `50` para `10`.

Build:

- comando usado: `cmake --build C:\tibia-oldschool\sources\otclient-redemption\build\windows-release-msbuild --config RelWithDebInfo -- /m`
- resultado: sucesso;
- executavel atualizado em `C:\tibia-oldschool\sources\otclient-redemption\otclient.exe`.

Warnings observados:

- `MSB8027`: dois arquivos `luafunctions.cpp` produzem saidas intermediarias com mesmo nome;
- `LNK4098`: conflito de runtime `LIBCMTD`;
- warnings ja observados antes, nao impediram gerar o executavel.

Checklist de teste:

- testar WASD com level 8, level 70, level 300 e GM;
- testar clique no mapa versus WASD em Carlin, grama, dirt e areia;
- testar troca rapida de direcao;
- testar subir/descer escada e andar imediatamente;
- testar digitar mensagem, apertar Enter e continuar andando com WASD sem segundo Enter;
- validar se os sliders antigos do cliente foram sobrescritos por configuracao salva; se necessario, ajustar manualmente nas Options ou limpar a config local do client.

## Experimento WASD 1ms OTClient Redemption 2026-05-27

Contexto:

- o ajuste anterior para `10ms` melhorou bastante o feeling do Redemption;
- foi solicitado um teste mais radical apenas no client, sem alterar o TFS.

Backups criados:

- `C:\tibia-oldschool\experiments\backup-redemption-walk.lua-pre-wasd-1ms-2026-05-27`
- `C:\tibia-oldschool\experiments\backup-redemption-data_options.lua-pre-wasd-1ms-2026-05-27`
- `C:\tibia-oldschool\experiments\backup-redemption-game.cpp-pre-clientbeat-1ms-2026-05-27`
- `C:\tibia-oldschool\experiments\backup-redemption-game.h-pre-clientbeat-1ms-2026-05-27`

Arquivos alterados:

- `sources\otclient-redemption\modules\game_walk\walk.lua`
  - constantes internas de input/cancel/turn reduzidas de `10ms` para `1ms`.
- `sources\otclient-redemption\modules\client_options\data_options.lua`
  - defaults de `walkTurnDelay`, `walkTeleportDelay` e `walkStairsDelay` reduzidos de `10ms` para `1ms`.
- `sources\otclient-redemption\src\client\game.cpp`
- `sources\otclient-redemption\src\client\game.h`
  - `m_serverBeat` default do client reduzido de `10` para `1`.

Build:

- comando usado: `cmake --build C:\tibia-oldschool\sources\otclient-redemption\build\windows-release-msbuild --config RelWithDebInfo -- /m`
- resultado: sucesso;
- executavel atualizado em `C:\tibia-oldschool\sources\otclient-redemption\otclient.exe`.

Observacao:

- o servidor/TFS permaneceu com o ajuste anterior de `10ms`;
- este e um experimento de feeling, especialmente para WASD em level alto/GM e mudanca rapida de direcao.

## Investigacao WASD vs Click OTClient Redemption 2026-05-28

Sintoma:

- apos reduzir o client para `1ms`, clique no mapa ainda ficou cerca de `20%` a `25%` mais rapido que WASD em personagens muito rapidos;
- em level baixo a diferenca quase nao aparece.

Achados:

- clique no mapa usa `player:autoWalk(autoWalkPos)`;
- `LocalPlayer::autoWalk()` calcula caminho e chama `g_game.autoWalk(path, start)`;
- `g_game.autoWalk()` envia um pacote `ClientAutoWalk` (`0x64`) com uma lista de direcoes;
- o TFS recebe essa lista em `ProtocolGame::parseAutoWalk()` e executa via `Game::playerAutoWalk()`;
- WASD usa `g_game.walk(dir)`, que envia um pacote simples por passo (`0x65` a `0x6D`);
- no client, WASD passa por `LocalPlayer::canWalk()`, que bloqueia novo passo enquanto ha `preWalk` pendente;
- no TFS, `Game::playerMove()` chama `player->startAutoWalk(direction)` com apenas uma direcao, entao nao cria a mesma fila longa do clique;
- o teclado em si nao parece mais ser o gargalo principal: `setKeyDelay`, `setAutoRepeatDelay` e constantes de walking ja estao em `1ms`.

Interpretacao:

- a diferenca restante provavelmente vem do fato de que mouse/autowalk enfileira passos no servidor, enquanto WASD trabalha passo a passo;
- o TFS nao esta necessariamente "travando"; ele esta respeitando o modelo de movimento por passo unico;
- para WASD ter a mesma velocidade do clique em chars muito rapidos, o melhor experimento deve ser transformar WASD segurado em uma mini-fila de autowalk controlada.

Proximos testes candidatos:

- testar `force-new-walking-formula: false` em `data\setup.otml` para validar o "sem o true";
- experimentar WASD usando `g_game.autoWalk({dir, dir, dir}, {x=0,y=0,z=0})` com uma fila curta de 2 a 4 passos quando a tecla estiver segurada;
- manter step unico para toque rapido na tecla, para nao perder precisao oldschool;
- se o experimento do client nao ficar bom, avaliar fila server-side para `playerMove`, com limite pequeno e anti-spam.

## Teste force-new-walking-formula false 2026-05-28

Backup criado:

- `C:\tibia-oldschool\experiments\backup-redemption-setup.otml-pre-force-new-walking-false-2026-05-28`

Alteracao:

- `sources\otclient-redemption\data\setup.otml`
  - `force-new-walking-formula: false`

Objetivo:

- testar rapidamente se a formula nova de walking do Redemption esta contribuindo para diferenca entre WASD e clique no mapa;
- nao exige recompilar, apenas fechar e abrir o client.

Proximo passo se nao resolver:

- voltar para `true`;
- implementar experimento de WASD segurado com mini-fila de `autoWalk`, equivalente ao dash.

## Experimento Client DASH via autoWalk 2026-05-28

Contexto:

- `force-new-walking-formula: false` resolveu a maior parte da diferenca de feeling;
- ainda restou diferenca estimada de ate `10%` entre WASD e clique no mapa em personagens muito rapidos;
- clique no mapa usa `autoWalk` com lista de direcoes, enquanto WASD normal usa um pacote por passo.

Backups criados:

- `C:\tibia-oldschool\experiments\backup-redemption-walk.lua-pre-client-dash-2026-05-28`
- `C:\tibia-oldschool\experiments\backup-redemption-data_options.lua-pre-client-dash-2026-05-28`
- `C:\tibia-oldschool\experiments\backup-redemption-general.otui-pre-client-dash-2026-05-28`

Arquivos alterados:

- `sources\otclient-redemption\modules\game_walk\walk.lua`
  - adicionado DASH via `g_game.autoWalk({dir, dir, ...}, startPos)`;
  - dash so ativa quando a tecla esta segurada por pelo menos `max(120ms, stepDuration do player)`;
  - toque unico continua usando `g_game.walk(dir)`;
  - ao soltar a tecla, se havia fila de dash, o client manda `g_game.stop()` para cancelar o restante;
  - novo input na mesma direcao nao cancela a fila de dash em andamento;
  - fila limitada entre 2 e 4 passos.
- `sources\otclient-redemption\modules\client_options\data_options.lua`
  - adicionado `dashWalk` desligado por padrao;
  - adicionado `dashWalkSteps` com default `2`.
- `sources\otclient-redemption\modules\client_options\styles\controls\general.otui`
  - adicionada opcao `Enable fast walking (DASH)`;
  - adicionada opcao `DASH queue steps`.

Observacoes tecnicas:

- nao precisa recompilar, pois a mudanca e Lua/OTUI;
- fechar e abrir o client deve carregar o novo modulo;
- manter `force-new-walking-formula: false` durante o teste;
- testar primeiro com `DASH queue steps: 2`;
- se overshoot aparecer em personagem lento, manter dash desligado para esse tipo de teste ou aumentar a protecao por velocidade.

Resultado:

- teste nao mudou o feeling de forma relevante;
- decisao: remover a funcao de DASH por enquanto;
- direcao futura: controlar a velocidade excessiva do clique/autowalk e revisar a formula de speed dos personagens.

## Remocao do DASH e Client MS 3 2026-05-28

Backups criados:

- `C:\tibia-oldschool\experiments\backup-redemption-walk.lua-pre-remove-dash-ms3-2026-05-28`
- `C:\tibia-oldschool\experiments\backup-redemption-data_options.lua-pre-remove-dash-ms3-2026-05-28`
- `C:\tibia-oldschool\experiments\backup-redemption-general.otui-pre-remove-dash-ms3-2026-05-28`
- `C:\tibia-oldschool\experiments\backup-redemption-game.cpp-pre-client-ms3-2026-05-28`
- `C:\tibia-oldschool\experiments\backup-redemption-game.h-pre-client-ms3-2026-05-28`

Alteracoes:

- removida a opcao `Enable fast walking (DASH)` do client;
- removida a opcao `DASH queue steps`;
- removida a logica de mini-fila `autoWalk` no `modules\game_walk\walk.lua`;
- `WALK_INPUT_DELAY`, `WALK_CANCEL_DELAY` e delays de turn alterados para `3ms`;
- defaults de `walkTurnDelay`, `walkTeleportDelay` e `walkStairsDelay` alterados para `3ms`;
- `m_serverBeat` default do client alterado para `3`;
- `force-new-walking-formula` mantido em `false`.

Build:

- comando usado: `cmake --build C:\tibia-oldschool\sources\otclient-redemption\build\windows-release-msbuild --config RelWithDebInfo -- /m`
- resultado: sucesso;
- executavel atualizado em `C:\tibia-oldschool\sources\otclient-redemption\otclient.exe`.

Observacao sobre click/autowalk:

- click no mapa usa pacote `autoWalk` com uma lista de direcoes, por isso pode ficar rapido demais em personagem muito veloz;
- para reduzir isso, ha tres caminhos provaveis:
  - limitar a formula de speed do personagem em curva, reduzindo ganho marginal em levels/skills/equipamentos muito altos;
  - limitar o tamanho da fila de `autoWalk` enviada pelo client ou aceita pelo TFS;
  - aplicar um pequeno throttle/cap apenas para `player:autoWalk`, sem afetar WASD manual.

## Throttle de click/autowalk 2026-05-28

Teste concluido: `AUTO_WALK_CONTINUE_DELAY_MS` foi elevado temporariamente para `5000ms` e confirmou que a continuacao do click/autowalk passa pelo throttle implementado em `src/client/localplayer.cpp`. Valor atual ajustado para `80ms` para deixar o click no mapa mais lento que WASD/setas e desestimular uso de click map em PvP.

Objetivo:

- desestimular uso de click no mapa para PvP;
- deixar WASD/setas como metodo mais responsivo;
- manter click no mapa funcional para deslocamento, mas menos explosivo em personagens muito rapidos.

Backups criados:

- `C:\tibia-oldschool\experiments\backup-redemption-localplayer.cpp-pre-autowalk-throttle-2026-05-28`
- `C:\tibia-oldschool\experiments\backup-redemption-localplayer.h-pre-autowalk-throttle-2026-05-28`

Arquivo alterado:

- `sources\otclient-redemption\src\client\localplayer.cpp`

Ponto exato para ajuste futuro:

- arquivo: `C:\tibia-oldschool\sources\otclient-redemption\src\client\localplayer.cpp`;
- constante: `AUTO_WALK_CONTINUE_DELAY_MS`;
- posicao atual no arquivo: bloco `namespace` logo apos os includes, junto de `AUTO_WALK_MAX_STEPS_PER_PACKET`;
- valor atual aprovado em teste: `80ms`;
- para deixar o click no mapa um pouco mais rapido, testar `60ms` ou `50ms`;
- para deixar o click no mapa ainda menos util em PvP, testar `100ms` ou `120ms`;
- depois de alterar, recompilar o client e copiar `RelWithDebInfo\otclient.exe` para `C:\tibia-oldschool\sources\otclient-redemption\otclient.exe`.

Implementacao:

- adicionados limites internos:
  - `AUTO_WALK_MAX_STEPS_PER_PACKET = 2`;
  - `AUTO_WALK_CONTINUE_DELAY_MS = 80`.
- `LocalPlayer::autoWalk()` agora corta o caminho calculado em blocos de no maximo 2 passos antes de chamar `g_game.autoWalk()`;
- quando o player chega ao fim do bloco, `LocalPlayer::onPositionChange()` agenda a proxima chamada de `autoWalk()` depois de `80ms`;
- WASD/setas nao usam esse caminho, pois continuam chamando `g_game.walk(dir)`.

Build:

- comando usado: `cmake --build C:\tibia-oldschool\sources\otclient-redemption\build\windows-release-msbuild --config RelWithDebInfo -- /m`
- resultado: sucesso;
- executavel atualizado em `C:\tibia-oldschool\sources\otclient-redemption\otclient.exe`.

Critério de teste:

- testar click no mapa com level 8, level 70, level 300 e GM;
- comparar com WASD/setas;
- se click ainda estiver rapido demais, reduzir `AUTO_WALK_MAX_STEPS_PER_PACKET` para `1` ou subir `AUTO_WALK_CONTINUE_DELAY_MS` para `100-120`;
- se click ficar travado demais, subir fila para `3` ou reduzir delay para `50-60`.

## Limite de pacotes por segundo

Durante os testes com OTClient Redemption ajustado para input mais agressivo, o TFS passou a desconectar personagens com a mensagem `disconnected for exceeding packet per second limit.`.

Ajuste aplicado para ambiente de desenvolvimento:

- arquivo: `C:\tibia-oldschool\server\config.lua`
- chave: `maxPacketsPerSecond`
- valor antigo: `25`
- valor atual: `100`

Motivo:

- `25` ficou baixo demais para o client custom com walking mais responsivo;
- o objetivo foi evitar kick falso por flood durante movimentacao e testes locais;
- esse valor deve ser reavaliado antes do online, junto de medidas melhores de antispam/antilag no protocolo.

## Walking input 15ms 2026-05-30

Problema observado:

- com OTClient Redemption em `3ms`, o client podia enviar pacotes demais e causar kick por `exceeding packet per second limit`;
- ao mudar de direcao, houve casos do personagem "voltar" um sqm, indicando previsao do client agressiva demais em relacao ao servidor;
- com GM muito rapido, a borda da tela podia ficar preta por alguns instantes, provavelmente por render/map update tentando acompanhar velocidade extrema.

Ajuste aplicado para teste:

- `C:\tibia-oldschool\sources\otclient-redemption\modules\game_walk\walk.lua`
- `WALK_INPUT_DELAY = 15`
- `WALK_CANCEL_DELAY = 15`
- `WALK_TURN_DELAY_REPEATED = 15`
- `WALK_TURN_DELAY_DEFAULT = 15`
- turn/teleport usam no minimo `15ms`, mas floor change/stairs usam `0ms` para evitar exaust artificial ao subir/descer;
- `g_game.setWalkMaxSteps(0)` no inicio do jogo para impedir que o client acumule pre-walk pendente alem do passo atual; isso reduz correcao visual/rollback ao virar com personagem muito rapido;
- `C:\tibia-oldschool\sources\otclient-redemption\modules\client_options\data_options.lua` defaults de `walkTurnDelay` e `walkTeleportDelay` em `15`, `walkStairsDelay` em `0`;
- `C:\tibia-oldschool\sources\otclient-redemption\src\client\game.cpp` e `game.h`: fallback de `m_serverBeat` em `10`;
- `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\protocolgame.cpp`: TFS envia `serverBeat = 10` no login, alinhado com o arredondamento real em `Creature::getStepDuration()`.

Notas:

- nao foram encontrados restos da funcao de dash/fila experimental que havíamos removido;
- ainda existem textos/traducoes antigas com a palavra `dash`, mas nao participam do walking atual;
- o throttle de `AUTO_WALK_MAX_STEPS_PER_PACKET = 2` afeta somente click/autowalk, nao WASD/setas;
- se ainda houver rollback ao virar em personagem rapido, testar desativar pre-walk por completo para 7.72 ou criar regra que evita pre-walk apenas em troca brusca de direcao;
- se ficar lento demais e sem kick/correcao de posicao, testar `12ms` no input mantendo `serverBeat = 10`.

## Floor Change Movement 2026-05-30

Problema observado:

- mesmo com `walkStairsDelay = 0` no client e `stairJumpExhaustion = 0` no servidor, personagens baixos ainda ficavam com delay perceptivel apos subir/descer escadas ou rampas;
- o problema era mais visivel em level baixo porque o custo extra escalava com o tempo normal do passo;
- a animacao de floor change as vezes aparece parcial/rapida demais, indicando tambem uma diferenca visual client-side.

Causa server-side encontrada:

- arquivo: `C:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src\creature.cpp`
- em `Creature::onCreatureMove`, quando `oldPos.z != newPos.z`, o TFS aplicava `lastStepCost = 2`;
- `Creature::getWalkDelay()` e `Creature::getEventStepTicks()` multiplicam o step duration por `lastStepCost`, entao o proximo passo apos mudar de andar ficava artificialmente dobrado.

Ajuste aplicado:

- para players, floor change agora usa `lastStepCost = 1`;
- para criaturas, mantido `lastStepCost = 2` por cautela ate teste especifico;
- diagonal continua com custo antigo: player `2`, creature `3`.

Experimento posterior:

- como ainda havia delay perceptivel, `lastStepCost` de player em floor change foi reduzido de `1` para `0`;
- objetivo: testar se qualquer custo residual de escada/rampa ainda vem do multiplicador de passo do servidor;
- se gerar comportamento estranho, rollback recomendado: voltar player floor change para `lastStepCost = 1`.
- teste extremo posterior: `lastStepCost` de player em floor change alterado para `20` apenas para medir impacto perceptivel;
- objetivo: se o delay aumentar muito, confirmar que esse caminho ainda influencia; se quase nao mudar, o atraso principal esta no client/floor update.
- resultado informado em teste: `lastStepCost = 20` nao mudou perceptivelmente o delay da rampa/escada;
- decisao: restaurado comportamento original `lastStepCost = 2` para floor change, pois o problema observado nao parece vir desse multiplicador.

Pendente visual:

- o OTClient Redemption trata movimento entre andares como map/floor update e nem sempre como walk visual completo;
- investigar depois se vale criar animacao client-side especifica para floor change, sem mexer no delay real do servidor.

Experimento client-side:

- arquivo: `C:\tibia-oldschool\sources\otclient-redemption\modules\game_walk\walk.lua`
- antes, quando o player estava em `serverWalking`, o client executava `player:lockWalk(player:getStepDuration() + WALK_INPUT_DELAY)`;
- em floor change, como o client nem sempre consegue usar pre-walk normal, isso criava uma espera do passo inteiro antes de aceitar o proximo WASD;
- ajuste: `serverWalking` agora apenas guarda `nextWalkDir` e deixa `onWalkFinish` enviar o proximo passo;
- `stopSmartWalk()` tambem limpa `nextWalkDir` para evitar um passo extra quando o jogador solta a tecla antes do fim da animacao.
- ajuste adicional: quando `player:canWalk()` retorna falso porque o personagem ainda esta em animacao de walk, o client agora tambem guarda `nextWalkDir` mesmo se a direcao for a mesma;
- motivo: em escada/rampa, segurar a mesma tecla podia nao enfileirar o proximo passo, causando uma pausa ate o proximo repeat do teclado.

## Regra de Diagnostico 2026-05-30

Quando uma alteracao pequena deveria causar impacto perceptivel mas o teste in game fica inconclusivo:

- fazer um teste radical e temporario no mesmo ponto do codigo;
- exemplo: alterar `lastStepCost` de floor change para `20` para verificar se o delay muda muito;
- se o comportamento quase nao mudar, a hipotese provavelmente esta errada ou o fluxo testado nao passa por aquele ponto;
- nesses casos, reverter a alteracao para o valor original antes de continuar;
- documentar o resultado para nao manter mudancas "fantasmas" que podem afetar outra parte do jogo no futuro.

## Voices de Criaturas 2026-05-30

- feita varredura em todas as criaturas XML com bloco `<voices ...>`;
- 146 arquivos continham falas;
- padrao aplicado: `interval="10000"` e `chance="10"`;
- objetivo: reduzir spam global de falas e diminuir checks muito frequentes em milhares de criaturas;
- o unico `.lua` em `data\monster\lua` era `#example.lua`, sem impacto no conteudo real.

## Mage Oldschool 2026-06-01

- decidimos abandonar as formulas "modernas TFS" das spells principais de mage e voltar para a logica oldschool extraida das tabelas de referencia;
- helper criado em `C:\tibia-oldschool\server\data\spells\lib\spells.lua`:
  - `getOldschoolMageBase(level, magicLevel) = floor((level / 5) + (magicLevel * 0.3))`
  - `getOldschoolMageRange(level, magicLevel, minFactor, maxFactor)`
  - `getOldschoolMageFixed(level, magicLevel, factor)`
- politica de arredondamento definida neste ponto do projeto:
  - formulas novas passam a arredondar sempre para baixo (`floor`);
  - nada de `ceil` ou `round` nas formulas customizadas daqui para frente;
- spells/runes ajustadas para a familia oldschool:
  - strikes (`energy`, `flame`, `force`) = `3.5x ~ 5.5x`
  - `fire wave` = `2x ~ 4x`
  - `energy beam` = `4x ~ 8x`
  - `great energy beam` = `4x ~ 20x`
  - `ultimate explosion` = `20x ~ 30x`
  - `light healing` = `1x ~ 3x`
  - `intense healing` = `2x ~ 6x`
  - `ultimate healing` = `20x ~ 30x`
  - `heal friend` = `8x ~ 16x`
  - `mass healing` = `16x ~ 24x`
  - `light magic missile` = `1x ~ 2x`
  - `heavy magic missile` = `2x ~ 4x`
  - `fireball` = `1.5x ~ 2.5x`
  - `great fireball` = `3.5x ~ 6.5x`
  - `explosion rune` = `2x ~ 10x`
  - `sudden death` = `13x ~ 17x`
  - `intense healing rune` = `4x ~ 10x`
  - `ultimate healing rune` = `25x` fixo
- ainda ficaram fora desse pacote, por falta de referencia confiavel da tabela original:
  - `poison_storm.lua`
- ajuste complementar apos teste in game:
  - `force_wave.lua` (registrado em `spells.xml` como `exevo mort hur`) foi alinhado com a familia oldschool de `Energy Wave`;
  - passou de formula linear moderna para `10x ~ 20x` da base oldschool;
  - efeito visual deixou de usar `CONST_ME_ENERGYAREA`, porque o client/protocolo 7.72 acusa `invalid effect id 38`;
  - tipo de dano corrigido de `COMBAT_PHYSICALDAMAGE` para `COMBAT_ENERGYDAMAGE`.
- varredura visual com `!fxscan 11,18`:
  - o effect correto para o visual oldschool das magias de energy ficou sendo o primeiro da esquerda, isto e, `CONST_ME_TELEPORT`;
  - aplicado em `energy_strike.lua`, `energy_beam.lua`, `great_energy_beam.lua` e `force_wave.lua`.
- observacao importante:
  - armor/shield do core C++ ainda nao foram migrados para as formulas novas discutidas depois;
  - esta rodada atacou primeiro o pacote principal de mage em Lua e depois iniciou a migracao do melee de knight no core.

## Knight Melee 2026-06-01

- implementada a primeira versao da formula customizada de melee para `Knight` e `Elite Knight` em [weapons.cpp](C:/tibia-oldschool/sources/nekiro-tfs-1.5-7.72/src/weapons.cpp:31);
- escopo atual:
  - so ativa para vocacoes `4` e `8`;
  - so ativa para armas com `attack > 16`;
  - armas de ataque menor continuam no caminho antigo do TFS;
- arredondamento:
  - todos os valores novos usam `floor`;
- fator de stance aplicado no melee de knight:
  - `attack = 1.0`
  - `balanced = 0.75`
  - `defense = 0.5`
- formulas implementadas:
  - `max = floor(((level / 5) + (atk * 1.5) + ((skill * skill) / 1620) * atk) * factor)`
  - `min = floor(((level / 4) + (maxBase * 0.18)) * factor)`
- o dano passou a sortear entre `min` e `max`, em vez de `0 .. max`, para esse caso especifico de knight;
- compilado com sucesso em `C:\tibia-oldschool\builds\nekiro-tfs-1.5-7.72\Release\tfs.exe` e copiado para `C:\tibia-oldschool\server\tfs.exe`.

## Blood Border Visual Bug 2026-06-01

- sintoma observado no client:
  - ao acertar duas vezes a mesma criatura parada no mesmo sqm, a borda de grama podia "sumir" e o chao base do tile cobria a sprite da borda;
  - ao sair da tela, relogar ou forcar redescricao completa do tile, o visual voltava ao normal;
- o teste in game mostrou mais claramente que:
  - o estado real do tile no servidor continua correto;
  - o bug aparece em updates incrementais de sangue/splash;
  - sair da tela corrige porque o client reconstrui o tile inteiro;
- leitura mais forte depois da investigacao:
  - `splash` no TFS fica entre os top items (`alwaysOnTop`) e portanto aparece antes de criaturas no stack do tile;
  - o TFS ja calculava o `stackpos` do item em [player.h](C:/tibia-oldschool/sources/nekiro-tfs-1.5-7.72/src/player.h:702), mas [protocolgame.cpp](C:/tibia-oldschool/sources/nekiro-tfs-1.5-7.72/src/protocolgame.cpp:2462) nao enviava esse byte no pacote `0x6A`;
  - sem esse byte, o client precisava adivinhar onde inserir o sangue e podia deixar o `splash` em um stack diferente do servidor;
  - no primeiro decay/segundo hit, o servidor mandava `update/remove` por `stackpos`, mas o client podia aplicar esse update no thing errado;
- correcao adotada:
  - o TFS agora envia `stackpos` no pacote `0x6A` tanto para itens quanto para criaturas;
  - o OTClient Redemption agora habilita `GameTileAddThingWithStackpos` para `version == 772`;
  - fallback client-side: em `772`, `splash` usa prioridade `ON_TOP` quando precisar ser inserido sem `stackpos`;
- experimentos revertidos por nao resolverem:
  - remapeamento manual de `splash` em `protocolgameparse.cpp`;
  - invalidacao ampla de tiles visiveis em `mapview.cpp`;
  - alteracao de `ITEM_SMALLSPLASH` para nascer como `2020`;
- diagnostico temporario no conteudo:
  - houve testes temporarios alterando a cadeia de `pool` em [items.xml](C:/tibia-oldschool/server/data/items/items.xml:3227);
  - neste ponto, a cadeia foi restaurada ao original: `2019 -> 2020 -> 2021 -> 0`;

## Distance Formula 2026-06-01

- implementada a formula nova de `distance` em [weapons.cpp](C:/tibia-oldschool/sources/nekiro-tfs-1.5-7.72/src/weapons.cpp:893);
- escopo atual:
  - ativa apenas para `attack > 10`;
  - armas de `attack <= 10` continuam usando a formula antiga do TFS;
  - arredondamento em todos os valores novos usa `floor`;
- formulas aplicadas:
  - `maxBase = floor((level / 4) + 10 + atk * ((skill / 15) + (skill^(3/2) / 3100)))`
  - `minBase = floor((level / 3) + atk)`
  - `max = floor(maxBase * factor)`
  - `min = floor(minBase * factor)`
  - `factor`: `full attack = 1.0`, `balanced = 0.75`, `full defense = 0.5`
- observacao:
  - o `attackFactor` do core foi convertido para esse fator discreto, igual ao ajuste de knight.

## Teste Pendente - Stack Alto 2026-06-01

- deixar anotado para depois:
  - criar macro que jogue moedas infinitamente no mesmo tile, com um item ancora colocado primeiro;
  - validar comportamento visual, relog, restart e recuperacao do item ancora no fundo da pilha;
  - repetir o teste em tile comum e em house para confirmar que nao ha perda real nem clone acidental.

## Nota de Conteudo - Hunting Spear 2026-06-01

- a `hunting spear` em [items.xml](C:/tibia-oldschool/server/data/items/items.xml:9420) esta como `weaponType="axe"`;
- por decisao de design atual, ela nao sera relevante para o servidor;
- deixar anotado para depois:
  - remover do conteudo ou desativar/comentar o caminho relacionado, em vez de gastar tempo ajustando seu comportamento agora.

## Armor Shield Test Pass 2026-06-01

- `rat.xml` foi ajustado para usar `melee min="-500" max="-500"` como alvo de teste de mitigacao fixa;
- em rodada posterior de teste, o mesmo `rat.xml` tambem recebeu `defense="100"` para validar se `distance` de player ignora ou nao a defesa de criaturas;
- em nova rodada, o rato recebeu tambem `armor="200"` para validar separadamente o quanto `distance` ainda respeita armor de criatura;
- armor nova ficou so para player:
  - `min = floor(armor^(4/3) * 0.24)`
  - `max = floor(armor^(3/2) * 0.22)`
  - o block real sorteia entre `min .. max`;
- shield/defense nova ficou so para player, somente em `full defense` e somente contra criaturas/summons:
  - vocacoes novas: `Sorcerer`, `Druid`, `Knight` e promocoes;
  - `Paladin` e `Royal Paladin` continuam na formula antiga;
  - `max = floor(defenseValue * ((shieldSkill * shieldSkill) / 800) * factor + 10)`
  - `factor`: knight `1.5`, mage `1.3`
  - o block real sorteia entre `0 .. max`;
- regra oldschool de defesa da arma restaurada:
  - se houver escudo/item defensivo valido na mao secundaria, a defesa da arma principal e ignorada;
  - se nao houver escudo defensivo, a arma pode usar sua propria defesa normalmente;
  - tocha ou offhand sem defesa nao desliga a defesa da arma;
- `distance` passou a ignorar defense/shield de forma forcada pelo `origin == ORIGIN_RANGED`, preservando apenas armor como mitigacao.

## Backlog Curto 2026-06-01

- `exevo gran mas pox` ficou anotado para bateria futura de testes; o dano atual nao esta correto, mas a formula ainda nao sera definida agora.
- `Map editor` entrou como prioridade para revisar hunts, spawns e fluxo de expansao do projeto.
- melhoria grafica futura: avaliar opcao de graficos HD no client.
- `enchanted spear` ficou explicitamente adiada para implementacao futura.
- `death penalty` sera recalibrada.
- spells deverao ser compradas futuramente, com excecao de `utevo lux`; nao aplicar isso agora para nao atrapalhar os testes.
- implementar sistemas de controle de inflacao.
- implementar a skill nova `Alchemy` como sink de gold.
- implementar `bestiary`.

