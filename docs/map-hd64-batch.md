# Map HD 64x64 Batch

Este documento registra o fluxo usado para gerar `Tibia.cwm` com overrides 64x64 dos sprites de item usados pelo mapa principal.

## Cadeia de IDs

O fluxo respeita a traducao correta:

```text
world.otbm serverId -> items.otb clientId -> Tibia.dat spriteIds -> Tibia.spr PNGs -> Upscayl 64x64 -> Tibia.cwm
```

O CWM substitui sprite IDs do client, entao nao basta usar server item id diretamente.

## Script

Script criado:

```text
C:\tibia-oldschool\tools\assets\extract_map_sprites.py
```

Ele gera:

- `map-server-to-client.csv`
- `client-items-to-sprites.csv`
- `sprite-ids.txt`
- `summary.json`
- `sprites-32\*.png`

## Comando de Extracao

```powershell
C:\Users\guisu\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe `
  C:\tibia-oldschool\tools\assets\extract_map_sprites.py `
  --map C:\tibia-oldschool\server\data\world\world.otbm `
  --otb C:\tibia-oldschool\server\data\items\items.otb `
  --dat C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.dat `
  --spr C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.spr `
  --out-root C:\tibia-oldschool\tools\assets\work\map-hd64-pass-01 `
  --extract `
  --sheet `
  --clean-output
```

Resultado do primeiro batch:

```text
server item ids no mapa: 3208
client item ids traduzidos: 3208
sprite ids do client: 6149
PNGs 32x32 extraidos: 6149
PNGs 64x64 gerados pelo Upscayl: 6149
```

## Upscayl

Backend usado:

```text
C:\Program Files\Upscayl\resources\bin\upscayl-bin.exe
```

Comando:

```powershell
C:\Program Files\Upscayl\resources\bin\upscayl-bin.exe `
  -i C:\tibia-oldschool\tools\assets\work\map-hd64-pass-01\sprites-32 `
  -o C:\tibia-oldschool\tools\assets\work\map-hd64-pass-01\sprites-64-upscayl-standard `
  -s 2 `
  -m "C:\Program Files\Upscayl\resources\models" `
  -n upscayl-standard-4x `
  -f png `
  -g 1
```

`-g 1` seleciona a NVIDIA GTX 1660 Ti neste PC.

## Build CWM

O `Tibia.spr` original tem `10962` sprites no header. O CWM foi montado com essa contagem para manter o header alinhado ao SPR base:

```powershell
C:\Users\guisu\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe `
  C:\tibia-oldschool\tools\assets\build_cwm.py `
  --input C:\tibia-oldschool\tools\assets\work\map-hd64-pass-01\sprites-64-upscayl-standard `
  --output C:\tibia-oldschool\tools\assets\work\map-hd64-pass-01\Tibia.map-hd64-pass-01.cwm `
  --sprites-count 10962
```

Arquivo ativo:

```text
C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.cwm
```

Backup do CWM anterior:

```text
C:\tibia-oldschool\tools\assets\work\map-hd64-pass-01\Tibia.previous-before-map-hd64-pass-01.cwm
```

Validacao binaria do CWM ativo:

```text
version: 1
sprites_count: 10962
entries: 6149
min sprite id: 1
max sprite id: 10961
unique entries: 6149
```
