# Simulador headless de carga 7.72

Este simulador conecta diretamente à porta de jogo do TFS usando sessões reais
do protocolo 7.72. Ele autentica, recebe e descarta pacotes e envia ações com
taxa limitada. Não carrega sprites, mapa gráfico, áudio, interface ou gravação
de CAM.

## Contas

- Contas: `100001` a `100200`
- Personagens: `Teste001` a `Teste200`
- Senha: `123456`
- Limite local do simulador: 200 sessões

Os personagens estão distribuídos em posições únicas com espaçamento de três
tiles entre `32678,32054,7` e `32711,32102,7`.

## Primeiro teste aprovado para execução

Primeiro abra o TFS com a telemetria opcional do `autosend`:

```powershell
cd D:\tibia-oldschool
.\tools\headless-load\Start-TfsAutosendMetrics.cmd
```

Essa execução grava agregados de cinco segundos em
`performance-results\autosend`. Sem essas variáveis de ambiente, a telemetria
fica desligada. O vetor e a varredura atual do `autosend` não foram trocados.

Com o TFS aberto e sem um estado de recovery bloqueando jogadores comuns:

```powershell
cd D:\tibia-oldschool
.\tools\headless-load\Run-HeadlessLoad.cmd -Count 10 -Profile idle -DurationSeconds 300 -FailOnLoginError
```

O wrapper abre as dez sessões em um lote, mantém cada conexão por cinco
minutos e fecha todas com logout normal. Ele detecta o processo `tfs.exe`
automaticamente; `-TfsPid` permite selecionar um processo explicitamente.

Internamente, os sockets de cada lote são admitidos individualmente, separados
por 600 ms. Isso respeita todos os estados possíveis do contador anti-flood
nativo `Ban::acceptConnection` sem desativá-lo para o teste local. A entrada
de 200 personagens leva aproximadamente dois a três minutos; esse tempo de
rampa não faz parte da duração configurada do cenário.

O keepalive começa assim que cada personagem autentica, ainda durante a rampa.
As ações dos perfis começam somente depois de todos os logins, para que a carga
ativa tenha uma janela de medição comum.

Não execute 50, 100 ou 200 antes de o teste de 10 terminar sem falha de login,
desconexão inesperada ou crescimento contínuo de memória.

## Perfis

- `idle`: somente keepalive a cada quatro segundos.
- `movement`: passos limitados a 0,8 ação/s por personagem, alternando direções.
- `follow`: renova follow em um personagem próximo a cada dez segundos.
- `attack`: renova ataque em um personagem próximo a cada dez segundos.
- `mixed`: distribui os bots entre idle, movement, follow e attack.

Movimentação de itens ficou conscientemente fora desta primeira versão.

O limite operacional desta máquina continua sendo 200, correspondente às
contas existentes. O código aceita uma faixa maior para reutilização futura
com 500 ou 1.000 contas em outra máquina, sem mudança de arquitetura.

As taxas podem ser alteradas chamando `headless_load.py` diretamente:

```powershell
D:\tibia-dev-tools\Python312\python.exe `
  .\tools\headless-load\headless_load.py `
  --count 10 --profile mixed --duration 300 `
  --movement-rate 0.8 --follow-rate 0.1 --attack-rate 0.1 `
  --batch-size 5 --batch-delay 3
```

## Resultados

Cada execução cria uma pasta exclusiva em:

`D:\tibia-oldschool\performance-results\headless-load`

Arquivos:

- `events.jsonl`: login, falha e desconexão com personagem e motivo.
- `resources.csv`: CPU e memória do simulador e do TFS em colunas separadas.
- `summary.json`: totais e latências de login e resposta às ações.

`cpu_one_core_pct` usa um núcleo como 100%. `cpu_host_pct` divide esse valor
pelo número de processadores lógicos e se aproxima da visão global do Windows.

A latência de ação é um indicador barato: mede do envio da ação até o próximo
frame recebido do servidor. Não afirma que esse frame seja uma confirmação
semântica daquela ação.

O CSV do `autosend` contém, por janela:

- média, P95, P99 e máxima de duração da varredura;
- quantidade de execuções;
- protocolos examinados e protocolos com buffer pendente;
- percentual de protocolos pendentes;
- tempo ocupado do Dispatcher;
- percentual do tempo ocupado do Dispatcher consumido pelo `autosend`.

## Encerramento

Com duração configurada, o encerramento é automático e em lotes. Para uma
execução com `--duration 0`, use `Ctrl+C`; o simulador tenta enviar logout e
fechar as sessões em lotes antes de sair.
