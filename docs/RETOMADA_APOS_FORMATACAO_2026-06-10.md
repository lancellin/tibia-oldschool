# Retomada apos formatacao - Tibia Oldschool

Este e o documento principal para restaurar o ambiente e continuar o projeto
depois de formatar o computador.

## Ordem de leitura

1. `LEIA-ME-BACKUP.md`
2. `docs/RETOMADA_APOS_FORMATACAO_2026-06-10.md`
3. `docs/BACKUP_RECOMPILAVEL_VALIDADO_2026-06-10.md`
4. `docs/CONTEXTO_CONTINUACAO_2026-06-02.md`
5. `docs/architecture.md`
6. `docs/assets-workflow.md`

Os documentos antigos ainda possuem caminhos em `C:\tibia-oldschool`.
Considere-os historicos. O snapshot validado atual esta em:

```text
D:\tibia-oldschool-backup-teste-2026-06-10
```

## Resumo do projeto

- Servidor: Nekiro TFS 1.5 downgrade, protocolo 7.72.
- Objetivo de gameplay e identidade: Tibia 7.4.
- Client oficial: OTClient Redemption.
- Map editor: RME OTAcademy.
- Mapa principal: `server\data\world\world.otbm`.
- O protocolo 7.72 foi mantido porque a base Nekiro/TFS 1.5 e util, mas a
  direcao visual e de gameplay continua oldschool 7.4.

Ja foram trabalhados e documentados:

- formulas de dano e defesa;
- movimentacao de players e criaturas;
- sincronizacao de walking entre TFS e client;
- exaustao de runas, spells e actions;
- persistencia e protecoes contra clone/rollback;
- bugs de stack, sangue, bordas e decay;
- NPC trade 7.72;
- renderizador HD opcional;
- pipeline de sprites, mosaicos, CWM e SPR classico.

## O que significa "recompilar offline"

O backup contem:

- todos os tres sources ativos;
- assets e datapack;
- executaveis validados;
- bibliotecas vcpkg ja instaladas;
- dependencias estaticas exatas do OTClient;
- scripts de build;
- dump do banco;
- sprites permanentes e snapshot exato dos assets ativos.

Com Windows, Visual Studio, CMake e MariaDB ja instalados, TFS, OTClient e RME
podem ser recompilados sem baixar pacotes.

O backup nao transforma um Windows vazio em uma maquina pronta totalmente
offline. Os instaladores externos nao estao incluidos.

Tambem nao ha garantia de reconstruir do zero todas as bibliotecas de
terceiros sem internet, porque downloads, packages e buildtrees regeneraveis
do vcpkg foram removidos. As bibliotecas ja instaladas para os triplets atuais
foram preservadas e foram suficientes para os tres builds validados.

## Software necessario depois da formatacao

### Obrigatorio para recompilar

- Windows x64.
- Visual Studio 2022 com workload `Desktop development with C++`.
- MSVC v143.
- Windows SDK. O ambiente validado usou `10.0.26100.0`.
- CMake no `PATH`. O ambiente validado usou `4.3.2`.

### Obrigatorio para executar o servidor completo

- MariaDB Community Server x64.
- A validacao usou MariaDB `10.11.17`.

O `libmariadb.dll` ao lado do `tfs.exe` e apenas a biblioteca cliente usada
pelo TFS. Ele nao substitui o MariaDB Server.

### Necessario para desenvolver sprites

- Python 3 x64.
- Pacote Python `Pillow`.
- Upscayl, caso o mesmo fluxo de upscale seja mantido.
- ComfyUI, somente para os fluxos que realmente dependam dele.

Ambiente de assets usado antes da formatacao:

```text
Python 3.12.13
Pillow 12.2.0
Upscayl com backend Vulkan
GPU selecionada anteriormente: NVIDIA GTX 1660 Ti, indice 1
```

Instalacao minima de Pillow:

```powershell
py -m pip install Pillow
```

### Opcional

- Git for Windows. Nao e necessario para compilar o snapshot.
- 7-Zip para criar uma segunda copia compactada.
- Visual C++ Redistributable para maquinas que apenas executarao os binarios e
  nao terao Visual Studio instalado.

## MariaDB no backup

O backup contem:

- dump SQL completo;
- schema do TFS;
- `config.lua`;
- `libmariadb.dll`.

O backup nao contem:

- instalador do MariaDB;
- servico Windows do MariaDB;
- diretorio de dados operacional do MariaDB.

Copiar `C:\Program Files\MariaDB...` nao e uma restauracao confiavel, pois o
servidor usa servico, configuracao, permissoes e diretorio de dados.

Se for desejada restauracao totalmente offline de uma maquina formatada, o
ideal e guardar separadamente o instalador oficial x64 do MariaDB 10.11, com
seu SHA256. O mesmo vale para Visual Studio, CMake, Python e Upscayl. Isso nao
foi feito neste backup.

## Alerta antes de formatar

Este backup esta no mesmo disco fisico D que a arvore de trabalho. Ele protege
contra erro de arquivos, mas nao protege contra:

- exclusao ou formatacao do disco D;
- falha fisica do disco;
- erro ao apagar particoes durante a instalacao do Windows;
- ransomware ou corrupcao da unidade.

Antes da formatacao:

1. copie a pasta inteira para outro disco fisico;
2. copie tambem `D:\AI\ComfyUI` se quiser preservar o ambiente de IA;
3. confirme que o destino externo abre e possui tamanho coerente;
4. nao apague nem formate a particao D sem duas copias verificadas;
5. preserve senhas e credenciais fora de documentos publicos.

## Dados externos que nao fazem parte dos 7,7 GiB

No momento da criacao deste documento existiam:

```text
D:\AI\ComfyUI
D:\AI\ComfyUI\input\tibia-740-all-sprites
D:\AI\ComfyUI\input\tibia-772-all-sprites
```

Os dois arquivos completos de sprites extraidos ocupavam pouco espaco, mas
estavam fora da pasta do backup. O ComfyUI inteiro tinha aproximadamente
16,7 GiB.

As sprites finais aprovadas necessarias para restaurar o client estao dentro
do backup em `backup-extras\Sprites Permanentes`. O ComfyUI e os arquivos
completos extraidos sao material de desenvolvimento, nao requisito para rodar
o client.

## Restauracao recomendada

### 1. Restaurar a pasta

Preferencialmente mantenha:

```text
D:\tibia-oldschool-backup-teste-2026-06-10
```

O script de build descobre a raiz automaticamente e aceita outro caminho.
Entretanto, `sources\rme-otacademy\rme.cfg` contem caminhos absolutos e deve
ser revisado se a pasta for movida.

### 2. Instalar as ferramentas

Instale Visual Studio C++, CMake e MariaDB. Para assets, instale Python,
Pillow, Upscayl e ComfyUI conforme a necessidade.

### 3. Restaurar o banco

Dump:

```text
backup-extras\database\oldschool772db-2026-06-10.sql
```

O `server\config.lua` deste snapshot aponta para o banco isolado:

```text
oldschool772db_backup_test_20260610
```

Crie o banco e importe o dump. Exemplo, em PowerShell:

```powershell
$Maria = "C:\Program Files\MariaDB 10.11\bin\mariadb.exe"
$Dump = "D:\tibia-oldschool-backup-teste-2026-06-10\backup-extras\database\oldschool772db-2026-06-10.sql"

& $Maria --user=root --password `
  --execute="CREATE DATABASE IF NOT EXISTS oldschool772db_backup_test_20260610 CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;"

& $Maria --user=root --password `
  oldschool772db_backup_test_20260610 `
  --execute="source $($Dump.Replace('\','/'))"
```

Depois crie ou ajuste o usuario do banco conforme as credenciais de
`server\config.lua`. Nao publique esse arquivo sem remover credenciais.

O config anterior ao banco de teste esta em:

```text
backup-extras\config.production-original.lua
```

Nao o restaure automaticamente. Primeiro revise banco, usuario, senha, IP e
portas.

### 4. Recompilar

```powershell
powershell.exe -ExecutionPolicy Bypass -File `
  "D:\tibia-oldschool-backup-teste-2026-06-10\tools\backup\Build-All.ps1" `
  -Clean
```

O script compila TFS, RME e OTClient e instala os executaveis nos diretorios
de runtime.

### 5. Recriar atalhos

```powershell
powershell.exe -ExecutionPolicy Bypass -File `
  "D:\tibia-oldschool-backup-teste-2026-06-10\tools\backup\Create-Desktop-Shortcuts.ps1"
```

### 6. Testar

1. Inicie o TFS e confirme `Server Online`.
2. Inicie o OTClient no proprio diretorio.
3. Entre no servidor e confira agua, grama, bordas e CID 104.
4. Abra o RME pelo atalho e confirme o mapa e os assets 7.72.
5. Somente depois retome alteracoes.

## Estado visual exato

Assets ativos:

```text
sources\otclient-redemption\data\things\772\Tibia.dat
sources\otclient-redemption\data\things\772\Tibia.spr
sources\otclient-redemption\data\things\772\Tibia.cwm
```

Snapshot separado para restauracao:

```text
backup-extras\Sprites Permanentes\estado-ativo-2026-06-10
```

Estado binario:

- `Tibia.spr`: 16114 sprites no header.
- `Tibia.cwm`: versao 1, header 16114, 170 entradas.
- `Tibia.dat` SHA256:
  `D4DBACCC3C4994F00A08C77E7E7FBD77F58606C34328C3A6337D2F4DACAF6F86`.
- `Tibia.spr` SHA256:
  `BA2415DAA7F02BD62A04BD4E1F6B92E5407A38EB58864C6672F1F6C2E074726D`.
- `Tibia.cwm` SHA256:
  `60B6B052629F646B3B50D7B64537697A1239D864E3A1ED47EE316780EE33D20D`.

Sempre restaure os tres juntos. O pacote antigo em
`aprovadas-2026-06-09\Pacotes-prontos` e anterior a agua/CID 104 finais e nao
deve substituir sozinho o estado ativo.

## Regras atuais de sprites

- O tile logico do jogo continua 32x32.
- O modo HD projeta imagens 64x64 no footprint logico 32x32.
- `Tibia.spr` guarda o visual classico, disponivel sem ativar HD.
- `Tibia.cwm` e um override parcial para o modo HD.
- Se um sprite nao existe no CWM, o client usa o SPR classico.
- Alteracoes 7.4 classicas devem ir ao SPR quando precisam aparecer sem HD.
- Assets HD aprovados devem ir ao CWM.

## Workflow que funcionou

- Trabalhar uma familia por vez.
- Montar mosaicos de 24x24 tiles.
- Fazer upscale do mosaico inteiro para preservar continuidade.
- O resultado aprovado recente foi mantido em 2x; nao houve necessidade de
  downscale final nesse conjunto.
- Recortar de volta respeitando pattern X/Y/Z, layers e frames.
- Validar antes/depois visual.
- Empacotar em CWM ou SPR, conforme classic/HD.
- Testar ingame antes de marcar permanente.
- Copiar o resultado aprovado para `Sprites Permanentes`.

Nao voltar ao processamento em massa de todos os grounds. Essa tentativa
gerou resultados inconsistentes e foi revertida. A decisao e continuar um
ground/familia por vez.

## Grama e bordas 7.4

- Ground fonte 7.4: sprite 43.
- Bordas fonte 7.4: sprites 76 ate 87.
- Foram mapeadas para as direcoes reais usadas pelo client 7.72.
- Uma primeira versao ficou com direcoes erradas.
- A versao corrigida foi aprovada ingame.
- A versao classica 32x32 esta aplicada ao SPR e nao depende do modo HD.

O mapeamento completo e os hashes estao em:

```text
backup-extras\Sprites Permanentes\aprovadas-2026-06-09\manifest.json
```

## Agua e bordas

Conjunto aprovado:

- agua principal: server IDs 4608 ate 4625;
- bordas com agua: server IDs 4632 ate 4663;
- aguas adicionais: server IDs 4664 ate 4666;
- animacao preservada em 16 fases;
- padroes espaciais preservados.

O primeiro teste de agua pareceu girar porque a ordem de patterns/frames foi
montada incorretamente. Nao tratar frames como sprites independentes sem
preservar a ordem do DAT.

Para as bordas, a solucao correta nao foi substituir grama ou areia por agua.
Foi remover somente a regiao azul da agua antiga e recompor o fundo com a agua
nova sincronizada, preservando:

- grama;
- areia;
- contorno;
- espuma branca/bolhas;
- transparencia.

Nao usar `borderize map` global para essa conversao. Existem varias familias
de solo e borda; uma substituicao global por grama destruiria areia e outros
grounds.

Documentacao e scripts do conjunto:

```text
backup-extras\Sprites Permanentes\aprovadas-2026-06-09\Agua-740-e-bordas-772-sincronizadas
tools\assets\build_water_animation_cross_version.py
tools\assets\build_water_border_before_after.py
tools\assets\rebase_water_borders.py
tools\assets\patch_rebased_water_borders.py
tools\assets\patch_water_dat_spr_exact.py
```

## Ground CID 104

- ID informado e tratado como client ID.
- Layout: pattern X 4, pattern Y 4, 1 frame.
- Sprite IDs: 9879 ate 9894.
- Mosaico: 24x24 tiles, 768x768 em 1x.
- Foi mantido como permanente junto com a agua.

Arquivos:

```text
backup-extras\Sprites Permanentes\aprovadas-2026-06-09\grounds-agua-e-cid104\cid-104
```

## Agua CID 4597

- ID informado e tratado como client ID.
- Layout: pattern X 4, pattern Y 2, 16 frames.
- Base de sprites adicionada: 10963 ate 10994.
- Cada fase precisa manter a permutacao espacial registrada no manifest.

Arquivos:

```text
backup-extras\Sprites Permanentes\aprovadas-2026-06-09\grounds-agua-e-cid104\cid-4597
```

## RME OTAcademy

O RME ativo esta em:

```text
sources\rme-otacademy
```

O `rme.cfg` aponta para:

- mapa dentro deste backup;
- assets 7.72 do OTClient dentro deste backup.

O `open-world.cmd` usa caminho relativo. Se mover a raiz, atualize o
`rme.cfg` ou recrie os atalhos.

Ao estudar brushes e bordas:

- CID nao e sprite ID;
- server ID, client ID e sprite ID sao camadas diferentes;
- a ordem direcional do brush deve vir do XML/brush do RME;
- nao assumir que sequencia numerica representa norte/leste/sul/oeste.

Ferramenta principal:

```text
tools\assets\extract_rme_brush_logic.py
```

## Arquivos completos extraidos

Antes da formatacao existiam:

```text
D:\AI\ComfyUI\input\tibia-740-all-sprites
D:\AI\ComfyUI\input\tibia-772-all-sprites
```

Eles servem para pesquisa visual e comparacao de IDs. Nao sao os arquivos
ativos do client. Os ativos sao DAT/SPR/CWM em `data\things\772`.

## Armadilhas conhecidas

- Nao confundir server ID, client ID e sprite ID.
- Nao trocar somente o SPR quando o DAT tambem mudou.
- Nao misturar DAT/SPR/CWM de snapshots diferentes.
- Nao apagar `tools\vcpkg\installed`.
- Nao apagar `tools\dependencies\otclient-vcpkg-installed`.
- Nao restaurar o pacote antigo de sprites sobre o snapshot ativo.
- Nao repetir lote massivo de grounds sem teste individual.
- Nao usar borderize global para todas as bordas de agua.
- Nao assumir que copiar a pasta do MariaDB restaura o servidor de banco.
- Nao considerar backup no mesmo disco como protecao contra falha fisica.
- O historico `.git` foi removido; o backup e um snapshot de source, nao um
  clone Git completo.

## Proximo passo recomendado

Depois da restauracao e do smoke test:

1. validar ingame agua, bordas, grama 7.4 e CID 104;
2. escolher apenas uma nova familia de ground;
3. extrair a logica do brush no RME;
4. montar mosaico 24x24;
5. fazer upscale;
6. recortar com patterns/frames corretos;
7. testar em CWM temporario;
8. aprovar e arquivar antes de seguir.

## Mensagem para uma nova conversa Codex

Use esta orientacao:

```text
Leia primeiro docs/RETOMADA_APOS_FORMATACAO_2026-06-10.md.
Depois leia docs/CONTEXTO_CONTINUACAO_2026-06-02.md e architecture.md apenas
como historico tecnico. O projeto ativo e TFS 1.5 Nekiro 7.72, OTClient
Redemption e RME OTAcademy. O estado visual ativo deve ser preservado.
Agua, grama/bordas 7.4 e CID 104 estao aprovados. Grounds novos devem ser
feitos um por vez com mosaicos 24x24, nunca em lote massivo.
```
