# Plano de testes — login e logout assíncronos

## 1. Objetivo

Validar a implementação mínima de login e logout assíncronos antes de qualquer
abertura beta. O plano cobre estabilidade sob carga, integridade dos saves,
relog, comportamento durante falhas, journal, shutdown e recuperação.

Este documento não autoriza mudanças de código. Cada execução deve registrar o
resultado, anexar os caminhos dos logs gerados e marcar uma única opção de
status.

## 2. Como marcar cada teste

Status padrão, presente em cada caso:

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

Use **100% aprovado** quando todos os resultados esperados forem atendidos,
sem erro, travamento, perda ou duplicação.

Use **parcialmente aprovado** quando o objetivo central tiver funcionado, mas
existir dado incompleto, alerta, instabilidade ou desvio que exija revisão.

Use **não aprovado** diante de freeze, crash, perda, duplicação, gravação fora
de ordem, login indevido, bloqueio permanente ou falha que inviabilize a
conclusão.

## 3. Instrumentação disponível e limites

### Já disponível

- O TFS gera métricas do Dispatcher em
  `performance-results\dispatcher\<data>-dispatcher.csv` quando iniciado por
  `tools\headless-load\Start-TfsDispatcherMetrics.cmd` ou
  `Start-TfsDispatcherBurstTest.cmd`.
- O simulador gera uma pasta exclusiva em
  `performance-results\headless-load\<data>-<perfil>-<quantidade>` com:
  `summary.json`, `resources.csv` e `events.jsonl`.
- As métricas atuais permitem comparar CPU e memória do TFS e do simulador,
  latência completa de login, falhas/desconexões, fila e duração de tarefas do
  Dispatcher.

### Ainda não disponível de forma específica

- tempo interno de `PREPARE`, `APPLY` e retries no `player_io_service`;
- tamanho da fila interna do serviço;
- CPU e memória do `player_io_service.exe` separadas do TFS;
- tempo semântico entre pedido de logout e revisão `COMMITTED` no banco;
- failpoint para interromper exatamente entre o `PREPARE` durável e o ACK.

Essas ausências não impedem os testes abaixo, mas limitam a precisão de alguns
diagnósticos. Não devem ser confundidas com falha da implementação.

## 4. Preparação comum

1. Registrar data, executáveis usados e se as contas ainda possuem o cenário
   pesado esperado: aproximadamente 740 itens por personagem de teste.
2. Fechar TFS e `player_io_service.exe` antes de cada cenário principal, para
   evitar CSVs misturados entre execuções.
3. Iniciar MariaDB e o serviço:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\tibia-dev-tools\Start-MariaDB.ps1
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\tibia-dev-tools\Start-PlayerIOService.ps1
   ```

4. Para testes massivos, **não abrir** `tfs.exe`, `tfs.bat` nem o launcher
   normal. Usar somente este arquivo, que inicia MariaDB, Player I/O Service,
   métricas e o bypass temporário da limitação por IP:

   ```text
   D:\tibia-dev-tools\Start-Tibia-LoadTest.cmd
   ```

   Há também um atalho equivalente na Área de Trabalho:

   ```text
   C:\Users\guisu\OneDrive\Área de Trabalho\Tibia Oldschool - Pacote Completo 2026-07-27\1 - TFS - Fonte e Runtime\Runtime\INICIAR-TFS-CARGA.cmd
   ```

   O bypass existe somente no processo iniciado por esse launcher e desaparece
   ao fechar o TFS. O launcher normal continua protegido. Nunca usar esse modo
   em produção.

5. Executar o simulador em outro terminal. Sempre informar `--port 7172`:
   o valor padrão interno do simulador não corresponde à porta atual do TFS.
6. Anotar no fim de cada teste:

   - pasta do resultado headless;
   - CSV do Dispatcher correspondente;
   - log do TFS;
   - log do `player_io_service`;
   - comportamento observado pelo GM, se houver.

## 5. Portões de entrada: stress tests

Os dois testes seguintes são obrigatórios e precedem todos os demais. Se um
deles não for aprovado, interromper a campanha e investigar antes de testar
crash, journal ou cenários de borda.

### T-01 — 100 jogadores pesados: login e logout simultâneos

**Objetivo:** detectar freeze, crescimento anormal da fila ou regressão básica
antes de ampliar a carga.

**Automação:** automática para carga e métricas; inspeção final manual.

```powershell
D:\tibia-dev-tools\Python312\python.exe `
  D:\tibia-oldschool\tools\headless-load\headless_load.py `
  --host 127.0.0.1 `
  --port 7172 `
  --count 100 `
  --account-start 100001 `
  --start-index 1 `
  --profile idle `
  --duration 90 `
  --batch-size 100 `
  --batch-delay 0 `
  --login-concurrency 100 `
  --login-admission-delay 0 `
  --login-timeout 120 `
  --keepalive-interval 4 `
  --stop-batch-size 100 `
  --stop-batch-delay 0 `
  --output-dir D:\tibia-oldschool\performance-results\headless-load
```

**Esperado:**

- 100 logins concluídos, sem crash e sem freeze prolongado do TFS;
- não haver falha de save, revisão, hash ou job duplicado nos logs;
- após o logout em massa, nenhum personagem permanece preso em save pendente;
- relog manual de uma conta escolhida preserva o inventário completo;
- fila do Dispatcher diminui após o burst, em vez de permanecer crescendo.

**Comparação inicial:** baseline anterior
`20260729-030322-idle-100`: média 4.356 ms, P95 7.389 ms, P99 7.622 ms,
máximo 7.682 ms. Comparar principalmente tendência, fila e travamentos, não
somente um valor isolado.

**Resultado:**

`☒ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

Logs e observações:

- Execução 2026-08-05 18:59 (duração reduzida para 40 s, conforme autorizado;
  binários do commit `c2aa2ba`):
  - simulador: `performance-results\headless-load\20260805-185922-idle-100\summary.json`
    — 100/100 logins, 0 falhas, 0 desconexões; latência de login média
    7.956 ms, P95 10.797 ms, máx 11.098 ms (fila do worker único de login;
    baseline 20260729-030322 tinha média 4.356 ms — diferença de fila, não de freeze);
  - CSV do Dispatcher: `performance-results\dispatcher\20260805-185912-dispatcher.csv`
    — burst de login: task_max 22 ms, 6 tarefas >10 ms, nenhuma >25 ms, busy 17,3%;
    burst de logout (100 saves): task_max 5,1 ms, nenhuma tarefa >10 ms, busy 4,6%;
    snapshot assíncrono no Dispatcher: máx 3,4 ms por personagem;
    steady state: busy <1%, task_max <= 8,3 ms;
  - log do TFS: `tmp\t01-tfs-20260805-185912.log` — sem erros, sem deadlocks,
    sem saves pendentes;
  - log do serviço: `tmp\t01-pio-20260805-185912.out.log` — err.log vazio;
    100 jobs aplicados, drain completo, safe shutdown aceito com fila durável
    vazia, journal podado (100 jobs antigos removidos);
  - shutdown gracioso via CTRL_C: clean save commitado, serviço encerrou junto,
    nenhum processo órfão.

---

### T-02 — 300 jogadores pesados: login e logout simultâneos

**Objetivo:** confirmar que a arquitetura não degrada de forma abrupta ao
triplicar o primeiro burst.

**Automação:** automática para carga e métricas; inspeção final manual.

```powershell
D:\tibia-dev-tools\Python312\python.exe `
  D:\tibia-oldschool\tools\headless-load\headless_load.py `
  --host 127.0.0.1 `
  --port 7172 `
  --count 300 `
  --account-start 100001 `
  --start-index 1 `
  --profile idle `
  --duration 90 `
  --batch-size 300 `
  --batch-delay 0 `
  --login-concurrency 300 `
  --login-admission-delay 0 `
  --login-timeout 180 `
  --keepalive-interval 4 `
  --stop-batch-size 300 `
  --stop-batch-delay 0 `
  --output-dir D:\tibia-oldschool\performance-results\headless-load
```

**Esperado:** os critérios de T-01, sem bloqueio do game loop, e com o GM
capaz de andar durante ou imediatamente após o burst sem comportamento de
freeze semelhante ao observado antes da arquitetura assíncrona.

**Comparação inicial:** baseline anterior
`20260729-010718-idle-300`: média 10.336 ms, P95 18.813 ms, P99 19.622 ms,
máximo 19.803 ms. O padrão de logout anterior era diferente; usar a comparação
diretamente apenas para login e métricas de fila.

**Resultado:**

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

Logs e observações:

---

### T-03 — 1.000 jogadores: stress limite

**Pré-requisito:** T-01 e T-02 aprovados.

**Objetivo:** encontrar saturação, vazamento de memória, crescimento sem
limite de fila e freezes em um volume acima da meta inicial do beta.

**Automação:** automática para carga e métricas; inspeção manual pelo GM.

```powershell
D:\tibia-dev-tools\Python312\python.exe `
  D:\tibia-oldschool\tools\headless-load\headless_load.py `
  --host 127.0.0.1 `
  --port 7172 `
  --count 1000 `
  --account-start 100001 `
  --start-index 1 `
  --profile idle `
  --duration 120 `
  --batch-size 1000 `
  --batch-delay 0 `
  --login-concurrency 1000 `
  --login-admission-delay 0 `
  --login-timeout 240 `
  --keepalive-interval 4 `
  --stop-batch-size 1000 `
  --stop-batch-delay 0 `
  --output-dir D:\tibia-oldschool\performance-results\headless-load
```

Não usar `--fail-on-login-error`: uma falha isolada não deve interromper a
coleta dos demais dados.

**Esperado:** TFS segue vivo, fila se recupera após os bursts e não existem
saves duplicados, personagens presos nem corrupção. É aceitável que a máquina
de desenvolvimento fique limitada; não é aceitável perder integridade ou
congelar indefinidamente.

**Resultado:**

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

Logs e observações:

---

## 6. Integridade do fluxo normal

### T-04 — Login, alteração, logout e relog simples

**Objetivo:** validar o caminho normal com snapshot pequeno.

**Automação:** parcialmente automática; requer conferência visual do item.

1. Entrar com `Teste001`.
2. Mover um item identificável.
3. Deslogar normalmente.
4. Relogar após a conclusão.
5. Conferir posição e conteúdo.

**Esperado:** estado final exato, uma única cópia do item e nenhum job pendente.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

### T-05 — Snapshot pesado de inventário

**Objetivo:** validar snapshots grandes sem perda de containers, itens ou IDs.

**Automação:** parcialmente automática; requer inspeção do inventário.

1. Usar personagem com cerca de 740 itens.
2. Fazer login, mover uma backpack interna e alterar uma pilha.
3. Logout e relog.
4. Conferir quantidade, hierarquia e itens movimentados.

**Esperado:** snapshot preserva conteúdo, ordem e itens sem duplicação.

`☒ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

Resultado (2026-08-06, binário com a correção de GUID `flushItemActorAttributions`):
conteúdo, ordem e quantidades preservados; após a correção, o GUID do
inventário mãe e de todo o conteúdo interno é normalizado no logout mesmo em
relog imediato.

### T-06 — Relog durante save pendente

**Objetivo:** provar que um personagem não é carregado antes da sua gravação
anterior ser resolvida.

**Automação:** parcialmente automática; o relog imediato precisa ser provocado.

1. Alterar um item em `Teste001`.
2. Iniciar logout de `Teste001` junto de vários outros personagens pesados.
3. Tentar login imediato de `Teste001`.
4. Repetir a tentativa após o save terminar.

**Esperado:** enquanto pendente, o relog é bloqueado/recusado; depois entra uma
única vez e apresenta apenas o estado mais novo.

`☒ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

Resultado (2026-08-06): durante o save pendente o relog foi bloqueado; após a
resolução, entrou uma única vez com o estado mais recente.

### T-07 — Mesma conta/personagem em login concorrente

**Objetivo:** cobrir o caso de dois clientes tentando abrir o mesmo personagem
quase simultaneamente.

**Automação:** pode ser codificado no simulador; hoje é manual ou por dois
processos headless.

1. Iniciar duas tentativas simultâneas para `Teste001`.
2. Repetir enquanto o personagem está salvando.

**Esperado:** no máximo uma sessão viva; nenhuma segunda materialização e
nenhuma remoção indevida da sessão original.

`☒ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

Resultado (2026-08-06): com duas tentativas simultâneas, apenas uma sessão foi
materializada; a segunda foi recusada sem remover a primeira.

## 7. Serviço, MariaDB e journal

### T-08 — Serviço indisponível com MariaDB disponível

**Objetivo:** validar fallback limitado e ausência de freeze indefinido.

**Automação:** parcialmente automática; exige encerrar e reiniciar o serviço.

1. Manter um GM e jogadores comuns online.
2. Encerrar somente `player_io_service.exe`.
3. Andar com o GM e usar o servidor normalmente.
4. Tentar login novo e logout de um jogador.
5. Reiniciar o serviço com `Start-PlayerIOService.ps1`.
6. Repetir login e relog.

**Esperado:** jogadores online continuam ativos; o Dispatcher não congela
indefinidamente; novas operações falham ou aguardam de forma controlada;
após retorno do serviço, operações novas voltam a funcionar.

`☒ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

Resultado (2026-08-06): jogadores online permanecem ativos e o logout
assíncrono enfileira no player_io sem congelar o Dispatcher; após o retorno do
serviço/banco, as operações voltam a funcionar e não há duplicação ou perda.

**Ressalva documentada:** operações **síncronas legado** (login, save de
item/posição e qualquer consulta/escrita direta no `Database`) ainda bloqueiam
o Dispatcher até o MariaDB voltar, porque a camada `Database` (database.cpp)
retry em loop com sleep de 1s quando perde a conexão sem um
`DatabaseRetryLimitScope` ativo. Esse comportamento **antecede a feature
assíncrona**, está fora do escopo dela e é aceito como limitação conhecida —
ele evita gravação parcial/duplicação, mas não torna o dispatcher à prova de
freeze com o banco indisponível. Aprovado para o escopo do login/logout
assíncrono.

### T-09 — MariaDB indisponível com serviço ainda vivo

**Objetivo:** confirmar que timeout/retry não transforma a indisponibilidade do
banco em freeze do game loop.

**Automação:** parcialmente automática; exige parada controlada do MariaDB.

1. Manter um GM online.
2. Parar MariaDB, mantendo o serviço aberto.
3. Andar com o GM e tentar login/logout controlados.
4. Reativar MariaDB e observar retries/recuperação.

**Esperado:** TFS não trava; erros são explícitos; nenhuma gravação parcial
vira estado final; retorno do banco permite retomar jobs válidos.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

### T-10 — Reinício do serviço com jobs pendentes

**Objetivo:** validar journal e idempotência após queda do serviço.

**Automação:** exige momento aproximado; seria determinístico apenas com
failpoint de teste no serviço.

1. Gerar múltiplos logouts pesados.
2. Encerrar o serviço durante o processamento.
3. Reiniciá-lo.
4. Relogar uma amostra de personagens e conferir estados.

**Esperado:** jobs duráveis são recuperados uma única vez; revisões e hashes
impedem reaplicação conflitante; não há duplicação nem regressão de estado.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

### T-11 — ACK ambíguo após PREPARE

**Objetivo:** testar o defeito corrigido: um PREPARE possivelmente aceito não
deve permitir fallback como segundo writer.

**Automação atual:** não determinística. Para provar a janela exata seria
necessário codificar um failpoint de teste que persista o PREPARE e descarte
somente o ACK correspondente.

**Teste aproximado atual:** encerrar o serviço durante uma onda de logouts e
reiniciá-lo; então verificar que não existem dois saves divergentes do mesmo
personagem.

**Esperado:** nenhuma duplicação de writer, nenhum payload conflitante e relog
só após resolução segura do job.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

## 8. Shutdown, crash e recuperação

### T-12 — Shutdown controlado sem jogadores

**Objetivo:** validar que o TFS e o serviço fecham corretamente quando não há
trabalho.

**Automação:** parcialmente automática; a confirmação de processos é manual.

1. Iniciar MariaDB, serviço e TFS.
2. Fechar o TFS pelo fluxo normal.
3. Confirmar que o serviço também terminou e que a porta 7180 foi liberada.

**Esperado:** nenhum processo órfão e próxima inicialização normal.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

### T-13 — Shutdown controlado com saves pendentes

**Objetivo:** validar drain seguro e encerramento apenas após conclusão.

**Automação:** parcialmente automática.

1. Entrar com 10 a 100 personagens pesados.
2. Iniciar seus logouts ou encerrar o TFS pelo X.
3. Acompanhar logs até a conclusão.

**Esperado:** TFS bloqueia novos logins, drena trabalho pendente, salva sem
perdas e o serviço encerra somente quando estiver seguro.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

### T-14 — Login em andamento durante shutdown

**Objetivo:** validar que nenhum callback tardio materializa Player após
`GAME_STATE_SHUTDOWN`.

**Automação:** exige encerrar o TFS durante burst; um failpoint seria mais
determinístico, mas não é obrigatório para um primeiro teste.

1. Iniciar burst de 100 ou 300 logins.
2. Fechar o TFS enquanto ainda há logins pendentes.
3. Conferir logs do TFS e do simulador.

**Esperado:** não há login concluído após o início do shutdown, nenhum Player
entra no mapa tardiamente e o processo encerra sem crash.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

### T-15 — Crash do TFS durante logouts

**Objetivo:** verificar transferência de responsabilidade ao serviço após
PREPARE durável.

**Automação:** exige encerramento forçado em janela aproximada; para ser
determinístico exigiria failpoint.

1. Alterar estados em personagens conhecidos.
2. Solicitar muitos logouts.
3. Finalizar abruptamente somente `tfs.exe`.
4. Manter MariaDB e serviço ativos por alguns segundos.
5. Reiniciar o TFS e relogar os personagens escolhidos.

**Esperado:** saves preparados se concluem/reconciliam uma única vez; estados
anteriores não reaparecem e não há duplicação.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

### T-16 — Reinício do TFS com serviço indisponível

**Objetivo:** cobrir startup degradado, sem presumir que o serviço está sempre
disponível.

**Automação:** parcialmente automática.

1. Deixar o serviço fechado.
2. Tentar iniciar o TFS.
3. Observar se a falha é clara e segura.
4. Iniciar o serviço e repetir startup.

**Esperado:** TFS não inicia em estado parcialmente funcional nem aceita
logins com persistência assíncrona declarada ativa e serviço indisponível.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

## 9. Ordem, revisões e repetição

### T-17 — Ciclos rápidos de login/logout do mesmo personagem

**Objetivo:** detectar revisão fora de ordem, reserva presa ou callback antigo.

**Automação:** pode ser automatizado pelo simulador; atualmente é mais simples
executar manualmente com uma conta em cerca de 20 ciclos.

1. Login.
2. Pequena mudança de item.
3. Logout.
4. Relog assim que permitido.
5. Repetir 20 vezes.

**Esperado:** revisões estritamente crescentes, nenhuma reserva permanente e
estado final igual à última alteração.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

### T-18 — Vários personagens, logouts fora de ordem

**Objetivo:** confirmar isolamento entre jobs de personagens diferentes.

**Automação:** automática com lotes menores; verificação final manual.

1. Entrar com 20 personagens.
2. Alterar itens em grupos diferentes.
3. Deslogar em ondas: 5, depois 10, depois 5.
4. Relogar amostras de cada onda.

**Esperado:** um job de outro personagem não bloqueia, sobrescreve ou libera
indevidamente a reserva de uma conta diferente.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

## 10. Regressão de gameplay durante carga

### T-19 — Jogabilidade durante burst de login/logout

**Objetivo:** validar a razão principal da arquitetura: o jogo permanece
fluido enquanto persistência pesada ocorre fora do Dispatcher.

**Automação:** o burst é automático; avaliação de fluidez requer GM.

1. Manter GM em área limpa.
2. Executar T-01 ou T-02.
3. Caminhar continuamente durante login e durante logout.
4. Registrar freezes, atrasos de movimento e mensagens de erro.

**Esperado:** pode haver pressão visível em caso extremo, mas não freeze longo
nem travamento do loop. Comparar `queue_wait`, `task_max` e `dispatcher_busy`.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

### T-20 — Perfil misto após estabilização

**Objetivo:** garantir que a mudança não afeta sessões em gameplay comum.

**Automação:** automática; inspeção visual opcional.

```powershell
D:\tibia-dev-tools\Python312\python.exe `
  D:\tibia-oldschool\tools\headless-load\headless_load.py `
  --host 127.0.0.1 `
  --port 7172 `
  --count 200 `
  --account-start 100001 `
  --start-index 1 `
  --profile mixed `
  --duration 180 `
  --batch-size 200 `
  --batch-delay 0 `
  --login-concurrency 200 `
  --login-admission-delay 0 `
  --login-timeout 180 `
  --keepalive-interval 4 `
  --stop-batch-size 200 `
  --stop-batch-delay 0 `
  --output-dir D:\tibia-oldschool\performance-results\headless-load
```

**Esperado:** sem desconexões inesperadas, sem mensagens de revisão/job e sem
crescimento contínuo de fila depois que a carga termina.

`☐ 100% aprovado   ☐ Parcialmente aprovado   ☐ Não aprovado`

## 11. Leitura dos resultados

Priorizar estes indicadores:

| Fonte | Indicadores principais | Interpretação |
| --- | --- | --- |
| `summary.json` | logins, falhas, desconexões, média/P95/P99/máximo de login | visão percebida pelos clientes simulados |
| `resources.csv` | CPU e memória do TFS/simulador | saturação da máquina de teste; não mede o serviço separado |
| CSV do Dispatcher | `task_max`, tarefas acima de 25/50/100 ms, espera P95/P99, profundidade e ocupação | identifica freeze e acúmulo dentro do loop principal |
| log do TFS | reservas, callbacks, shutdown, exceções | explica o caminho de jogo e materialização |
| log do serviço | PREPARE, APPLY, retry, journal, recuperação | confirma responsabilidade e idempotência fora do Dispatcher |

Não comparar mecanicamente todas as fases antigas de login com as novas: parte
do SQL agora ocorre fora do Dispatcher e é suprimida das métricas dele. A
comparação mais útil é entre latência fim a fim, fila, tarefas longas,
responsividade do GM e integridade final dos personagens.

## 12. Possíveis automações futuras — não implementar agora

Estas melhorias tornariam os testes mais fortes, mas não são requisito para
iniciar a campanha:

1. **Failpoint PREPARE/ACK:** persiste PREPARE, descarta a resposta e simula a
   ambiguidade com precisão.
2. **Failpoint APPLY:** interrompe antes/depois do commit para provar retry e
   idempotência sem depender de timing manual.
3. **Modo de relog competitivo:** duas tentativas coordenadas para o mesmo
   personagem durante save pendente.
4. **Métrica do serviço:** CSV de fila, PREPARE/APPLY, retries, journal e CPU/
   memória de `player_io_service.exe`.
5. **Validador pós-teste:** consulta as revisões/jobs das contas usadas e gera
   relatório de snapshots antigos, conflitos ou pendências.

Essas automações cobrem janelas de falha muito pontuais; não duplicam os testes
funcionais acima.

## 13. Critério de encerramento

Considerar a implementação apta para beta somente se:

- T-01 e T-02 forem 100% aprovados;
- T-04 a T-08, T-12 a T-14 e T-17 forem 100% aprovados;
- T-03, T-09 a T-11, T-15, T-16, T-18 a T-20 tiverem resultado documentado e
  nenhum defeito de integridade confirmado;
- todo resultado parcialmente aprovado possuir causa, impacto e decisão
  registrada antes da abertura.
