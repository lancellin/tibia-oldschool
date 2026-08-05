# Mosaico de bordas RME - fluxo paralelo

Este fluxo nao substitui os scripts existentes. Ele existe para comparar a
aproximacao anterior de marching squares com a tabela e a selecao de brushes
usadas pelo RME.

## Diferencas

O script `build_rme_native_border_mosaic.py`:

- le os grounds e borders do RME 7.72;
- traduz server ID para client ID pelo `items.otb`;
- le patterns e sprite IDs do `Tibia.dat`;
- extrai pixels do `Tibia.spr`;
- escolhe os grounds pelas chances do brush;
- usa `z-order`, `align` e `to` para selecionar a borda entre dois brushes;
- extrai do source do RME a tabela completa de 256 mascaras de oito vizinhos;
- permite mais de uma peca de borda no mesmo tile;
- usa o fallback de diagonais do proprio `GroundBrush::doBorders`.

O fluxo anterior `build_connected_border_mosaic.py` continua preservado e usa
uma tabela manual de 12 pecas baseada em quatro cantos.

## Gerar teste grass sobre dirt

```powershell
$Python = "C:\Users\guisu\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"

& $Python tools\assets\build_rme_native_border_mosaic.py `
  --foreground-brush grass `
  --background-brush dirt `
  --out-root tools\assets\work\rme-native-border-mosaics `
  --grid 24 `
  --seed 772
```

Saidas principais:

- mosaico conectado 24x24 para processamento;
- atlas 16x16 contendo as 256 mascaras do RME;
- folha de contato das bordas;
- `_manifest.json` com cada ground, mascara, direcao e sprite aplicada;
- `source-tiles` com os alphas originais.

## Recortar upscale e montar CWM

Coloque o PNG processado dentro da pasta do brush. O nome automatico deve
conter `upscayl_4x`, ou informe `--image-name`.

```powershell
& $Python tools\assets\build_rme_native_border_cwm_from_upscayl.py `
  --brush-dir tools\assets\work\rme-native-border-mosaics\grass-over-dirt `
  --image-name grass-over-dirt-rme-native-mosaic-24x24-1x_upscayl_4x.png `
  --upscale-factor 4 `
  --out-root tools\assets\work\rme-native-border-cwm
```

O recorte escolhe para cada sprite a ocorrencia mais central e com menos
outras bordas sobrepostas. O alpha original e reaplicado antes do CWM.

## Comparacao

Para comparar os dois caminhos:

1. use a mesma familia de ground e borda;
2. processe os dois mosaicos com o mesmo modelo e configuracao;
3. gere CWMs temporarios separados;
4. compare retas, cantos concavos, cantos convexos e encontros diagonais;
5. confira no manifest se todas as 12 direcoes aparecem;
6. valide no client antes de mesclar com o CWM ativo.

## Limites atuais

Esta primeira versao simula um par de brushes por vez. Ela ainda nao executa:

- regras `<specific>` de substituicao ou exclusao;
- optional borders de montanhas;
- interacao simultanea de tres ou mais brushes;
- fases animadas alem do frame zero;
- pattern Z diferente de zero.

Esses casos aparecem como warnings no manifest. O script nao deve ser tratado
como implementacao completa do `GroundBrush::doBorders` enquanto esses avisos
existirem.
