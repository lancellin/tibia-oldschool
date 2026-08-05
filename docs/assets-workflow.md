# Assets Workflow

## O que temos hoje

- O servidor mapeia items para `clientId` via `C:\tibia-oldschool\server\data\items\items.otb`.
- O client 7.72 desenha sprites clássicos a partir de:
  - `C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.dat`
  - `C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.spr`
- O OTClient Redemption foi ajustado para aceitar `Tibia.cwm` como **override parcial**, em vez de substituição total.

## O que isso nos permite

Podemos testar poucos sprites por vez:

1. extrair um conjunto pequeno do `Tibia.spr`
2. editar os PNGs
3. empacotar só esses PNGs num `Tibia.cwm`
4. abrir o client e comparar com `Map - Default`

Isso é ideal para protótipos visuais.

## Ferramentas locais

### Descobrir os sprites de um item pelo próprio client

No terminal do client, use:

```lua
asset_info 724
```

Ou com categoria explícita:

```lua
asset_info 35 1
```

Categorias:

- `0 = item`
- `1 = creature`
- `2 = effect`
- `3 = missile`

O comando mostra:

- nome do thing type
- tamanho/layers/patterns/frames
- sprite IDs usados
- um comando pronto de extração

### Extrair sprites do `Tibia.spr`

Arquivo:

- `C:\tibia-oldschool\tools\assets\extract_sprites.py`

Exemplo:

```powershell
C:\Users\guisu\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe `
  C:\tibia-oldschool\tools\assets\extract_sprites.py `
  --spr C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.spr `
  --ids 724,103,4526 `
  --out C:\tibia-oldschool\tools\assets\work\city-test `
  --sheet C:\tibia-oldschool\tools\assets\work\city-test\sheet.png
```

### Montar um `Tibia.cwm`

Arquivo:

- `C:\tibia-oldschool\tools\assets\build_cwm.py`

Exemplo:

```powershell
C:\Users\guisu\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe `
  C:\tibia-oldschool\tools\assets\build_cwm.py `
  --input C:\tibia-oldschool\tools\assets\work\city-test `
  --output C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.cwm
```

### Preparar um batch inteiro para CWM

Arquivo:

- `C:\tibia-oldschool\tools\assets\prepare_cwm_batch.py`

Esse script le o `index.json` criado pelo `extract_thing_assets.py`, procura assets processados por item e monta uma pasta unica com PNGs numericos (`536.png`, `537.png`, etc.) pronta para `build_cwm.py`.

Ele aceita tres formatos de entrada processada:

- PNGs numericos dentro de `item-103\536.png`, `item-103\537.png`, etc.
- mosaico processado como `item-103-mosaic-input.png`, `item-103-mosaic.png` ou dentro da pasta do item
- sheet processado como `item-2109-sheet.png`, `sheet.png` ou dentro da pasta do item

Validacao sem sobrescrever o client ativo:

```powershell
C:\Users\guisu\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe `
  C:\tibia-oldschool\tools\assets\prepare_cwm_batch.py `
  --index C:\tibia-oldschool\tools\assets\work\batch-city-pass-01\index.json `
  --processed-root C:\tibia-oldschool\tools\assets\work\batch-city-pass-01 `
  --out-sprites C:\tibia-oldschool\tools\assets\work\batch-city-pass-01-cwm-sprites `
  --cwm C:\tibia-oldschool\tools\assets\work\batch-city-pass-01\Tibia.cwm `
  --fallback-original `
  --clean-output
```

Uso esperado apos processar mosaicos no Upscayl:

```powershell
C:\Users\guisu\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe `
  C:\tibia-oldschool\tools\assets\prepare_cwm_batch.py `
  --index C:\tibia-oldschool\tools\assets\work\batch-city-pass-01\index.json `
  --processed-root C:\tibia-oldschool\tools\assets\tests\tibia-sprites\outputs\batch-city-pass-01 `
  --out-sprites C:\tibia-oldschool\tools\assets\work\batch-city-pass-01-cwm-sprites `
  --cwm C:\tibia-oldschool\tools\assets\work\batch-city-pass-01\Tibia.cwm `
  --fallback-original `
  --clean-output
```

Depois de conferir o arquivo gerado, copiar o CWM para o client:

```powershell
Copy-Item `
  -LiteralPath C:\tibia-oldschool\tools\assets\work\batch-city-pass-01\Tibia.cwm `
  -Destination C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.cwm `
  -Force
```

## Fluxo recomendado

1. Escolher uma cena curta:
   - piso da cidade
   - parede
   - placa
   - vegetação
2. Extrair poucos sprites.
3. Testar contraste/nitidez no shader atual.
4. Editar somente os PNGs realmente importantes.
5. Gerar `Tibia.cwm`.
6. Abrir o client e comparar:
   - `Map - Default`
   - `Map - Crisp AA Clarity`

## Observação sobre packs externos

O arquivo `C:\tibia-oldschool\server\client for yomee.zip` **não** é um pack clássico `spr/dat`.
Ele veio em milhares de arquivos `.obd` separados por:

- `items`
- `effects`
- `outfits`

Isso indica um pipeline de client mais novo/modular. Ele pode servir de referência visual ou de fonte de arte, mas não entra diretamente no nosso client 7.72 sem conversão.
