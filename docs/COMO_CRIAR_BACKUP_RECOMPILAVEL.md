# Como criar um backup recompilavel

Este documento descreve o procedimento para uma conversa futura repetir este
backup com seguranca.

## Objetivo

O backup deve conter:

- servidor executavel e datapack;
- source do TFS;
- source e assets do client;
- source e dados do RME;
- assets aprovados que estejam fora da arvore principal;
- dump do banco;
- dependencias necessarias para build offline;
- documentos e ferramentas;
- executaveis recompilados;
- logs e hashes de validacao.
- uma lista explicita de software externo que nao foi incorporado;
- um snapshot conjunto do DAT/SPR/CWM realmente ativo.

Ele nao precisa conter:

- objetos de compilacao;
- CMake caches;
- buildtrees e downloads do vcpkg;
- logs de runtime antigos;
- caches de IDE;
- testes e mosaicos descartados;
- executaveis historicos ja substituidos.

## Regra principal

Sempre copie primeiro e limpe depois.

Nunca execute a limpeza na origem.

## 1. Preparacao

1. Feche TFS, OTClient, RME e compilacoes.
2. Confirme o caminho da origem.
3. Confirme espaco livre no destino.
4. Defina um nome com data:

```text
D:\tibia-oldschool-backup-teste-AAAA-MM-DD
```

5. Registre quantidade de arquivos e tamanho da origem.

## 2. Copia integral

Exemplo:

```powershell
$SRC = "D:\tibia-oldschool"
$DST = "D:\tibia-oldschool-backup-teste-AAAA-MM-DD"
$LOG = "$DST-copy.log"

robocopy $SRC $DST /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 /MT:16 /XJ /NP /LOG:$LOG
```

Interpretacao do codigo do Robocopy:

- `0` a `7`: sucesso ou sucesso com diferencas;
- `8` ou maior: falha.

Confirme no log:

- zero arquivos com falha;
- quantidade de arquivos copiados;
- quantidade de bytes.

## 3. Incorporar dados externos

Liste tudo que o projeto usa fora da raiz.

Neste projeto:

```text
C:\Users\guisu\OneDrive\Area de Trabalho\Sprites Permanentes
```

Copie para:

```text
backup-extras\Sprites Permanentes
```

Outros exemplos:

- modelos de IA realmente necessarios;
- certificados;
- configuracoes privadas;
- dumps;
- arquivos de mapa mantidos fora da raiz.

Registre tambem ferramentas que nao serao copiadas, por exemplo:

```text
Visual Studio
CMake
MariaDB Server
Python/Pillow
Upscayl
ComfyUI
```

Nao diga apenas "funciona offline". Especifique se significa:

- executar binarios com o ambiente ja instalado;
- recompilar o projeto com toolchain ja instalada;
- ou preparar uma maquina vazia sem internet.

Esses tres cenarios sao diferentes.

## 4. Dump do banco

Use `mariadb-dump` com transacao:

```powershell
$env:MYSQL_PWD = "SENHA"

& "C:\Program Files\MariaDB 10.11\bin\mariadb-dump.exe" `
  --host=127.0.0.1 `
  --port=3306 `
  --user=USUARIO `
  --single-transaction `
  --routines --triggers --events `
  --default-character-set=utf8mb4 `
  NOME_DO_BANCO |
  Set-Content "$DST\backup-extras\database\banco-AAAA-MM-DD.sql" `
    -Encoding utf8

Remove-Item Env:\MYSQL_PWD
```

Valide se o arquivo nao esta vazio e calcule SHA256.

## 5. Preservar dependencias escondidas em builds

Antes de apagar builds, procure:

```text
vcpkg_installed
installed
conan
third_party
deps
```

No OTClient deste projeto, as dependencias exatas estavam em:

```text
sources\otclient-redemption\build\windows-release-msbuild\vcpkg_installed
```

Elas foram movidas para:

```text
tools\dependencies\otclient-vcpkg-installed
```

Sem essa etapa, o client exigiria novo download de dependencias.

## 6. Limpeza segura

Valide todos os caminhos resolvidos antes de remover:

```powershell
$ROOT = (Resolve-Path $DST).Path
$TARGET = (Resolve-Path "$DST\caminho").Path

if (-not $TARGET.StartsWith($ROOT + "\")) {
    throw "Destino inseguro"
}
```

Categorias normalmente removiveis:

```text
.vs
build
builds antigos
Debug
Release
RelWithDebInfo
vcpkg\buildtrees
vcpkg\downloads
vcpkg\packages
__pycache__
logs
PDB/OBJ/ILK antigos
testes temporarios
conversas brutas ja consolidadas
```

Nao remover:

```text
server\data
server\config.lua
server\*.dll
sources
client data/modules/mods
Tibia.dat
Tibia.spr
Tibia.cwm
RME data
items.otb
tools\vcpkg\installed
tools\dependencies\otclient-vcpkg-installed
backup-extras
```

Registre cada caminho, quantidade de arquivos, bytes e justificativa.

## 7. Recompilacao

Crie diretorios novos:

```text
build-validation\tfs
build-validation\otclient
build-validation\rme
```

Nunca reutilize build cache copiado.

Compile os tres projetos usando somente paths dentro do backup. Use:

```text
tools\backup\Build-All.ps1
```

## 8. Banco isolado para smoke test

Nao teste o backup contra o banco principal.

1. Crie um banco com nome de teste.
2. Importe o dump.
3. Copie o config original.
4. Altere somente o `mysqlDatabase` na copia.
5. Inicie o TFS.
6. Aguarde `Server Online`.
7. Encerre o processo.
8. Guarde stdout e stderr.

## 9. Testes de executaveis

TFS:

- deve chegar a `Server Online`;
- deve carregar banco, scripts, monsters e mapa.

OTClient:

- deve iniciar no diretorio que contem `data`, `modules` e `mods`;
- deve permanecer aberto sem crash imediato;
- deve carregar os assets 772.

RME:

- deve iniciar no diretorio que contem `data`;
- deve encontrar `Tibia.dat`, `Tibia.spr` e `items.otb`;
- deve abrir o mapa da copia.

## 10. Atalhos

Crie atalhos somente depois da validacao.

Use:

```powershell
& "$DST\tools\backup\Create-Desktop-Shortcuts.ps1"
```

Cada atalho precisa de:

- TargetPath correto;
- WorkingDirectory correto;
- argumentos do mapa, quando aplicavel.

## 11. Validacao final

Registre:

- tamanho final;
- quantidade final de arquivos;
- hashes dos executaveis;
- hash do dump;
- versoes de Visual Studio, MSVC, SDK, CMake, MariaDB e Git;
- resultado dos tres builds;
- resultado dos smoke tests;
- lista do que foi removido.

## 12. Limpeza depois da validacao

Depois de copiar os executaveis finais e preservar os logs, remova os
diretorios de build gerados durante a validacao:

```text
build-validation
sources\otclient-redemption\RelWithDebInfo
```

Esses diretorios sao reproduziveis por `tools\backup\Build-All.ps1`. Nao
remova:

```text
build-results
tools\vcpkg\installed
tools\dependencies\otclient-vcpkg-installed
```

Registre essa segunda limpeza no manifesto e recalcule a quantidade de
arquivos e o tamanho final.

## 13. Depois do backup

Opcional:

1. copie a pasta para outro disco fisico;
2. compacte com 7-Zip;
3. gere SHA256 do arquivo;
4. teste a extracao;
5. nao dependa de uma unica unidade fisica.

Um backup no mesmo disco protege contra erro humano, mas nao contra falha do
disco.
