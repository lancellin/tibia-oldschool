param(
    [string]$UpscaylOutputDir = "C:\tibia-oldschool\tools\assets\tests\map-hd-standard\upscayl-standard-4x-tta",
    [string]$DownscaledDir = "C:\tibia-oldschool\tools\assets\tests\map-hd-standard\processed-standard-64",
    [string]$SpritesDir = "C:\tibia-oldschool\tools\assets\tests\map-hd-standard\built\sprites-64",
    [string]$CwmOut = "C:\tibia-oldschool\tools\assets\tests\map-hd-standard\built\Tibia.map-hd-standard-64.cwm",
    [string]$Index = "C:\tibia-oldschool\tools\assets\tests\map-hd-standard\original\index.json",
    [int]$SpritesCount = 10962
)

$ErrorActionPreference = "Stop"

$root = "C:\tibia-oldschool"
$python = "C:\Users\guisu\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
$resize = Join-Path $root "tools\assets\resize_images.py"
$prepare = Join-Path $root "tools\assets\build_map_hd_cwm_mixed.py"

if (!(Test-Path -LiteralPath $UpscaylOutputDir)) {
    throw "Upscayl output folder was not found at $UpscaylOutputDir"
}
if (!(Test-Path -LiteralPath $Index)) {
    throw "Batch index was not found at $Index"
}

New-Item -ItemType Directory -Force -Path $DownscaledDir, $SpritesDir, (Split-Path $CwmOut) | Out-Null

Write-Host "Downscaling Upscayl 4x output to 64x64 sprite assets..."
& $python $resize --input $UpscaylOutputDir --output $DownscaledDir --scale 0.5 --suffix=
if ($LASTEXITCODE -ne 0) {
    throw "resize_images.py failed with exit code $LASTEXITCODE"
}

Write-Host ""
Write-Host "Building numeric sprite folder and CWM..."
& $python $prepare `
    --index $Index `
    --processed-root $DownscaledDir `
    --out-sprites $SpritesDir `
    --cwm $CwmOut `
    --sprites-count $SpritesCount `
    --clean-output
if ($LASTEXITCODE -ne 0) {
    throw "build_map_hd_cwm_mixed.py failed with exit code $LASTEXITCODE"
}

Write-Host ""
Write-Host "Built CWM: $CwmOut"
