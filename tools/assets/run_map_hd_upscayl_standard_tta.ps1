param(
    [string]$InputDir = "C:\tibia-oldschool\tools\assets\tests\map-hd-standard\inputs-flat",
    [string]$OutputDir = "C:\tibia-oldschool\tools\assets\tests\map-hd-standard\upscayl-standard-4x-tta",
    [int]$GpuId = 1,
    [string]$Model = "upscayl-standard-4x",
    [int]$Scale = 4,
    [switch]$EnableTta
)

$ErrorActionPreference = "Stop"

$upscayl = "C:\Program Files\Upscayl\resources\bin\upscayl-bin.exe"
$models = "C:\Program Files\Upscayl\resources\models"

if (!(Test-Path -LiteralPath $upscayl)) {
    throw "Upscayl binary was not found at $upscayl"
}
if (!(Test-Path -LiteralPath $InputDir)) {
    throw "Input folder was not found at $InputDir"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Write-Host "Running Upscayl..."
Write-Host "Input : $InputDir"
Write-Host "Output: $OutputDir"
Write-Host "GPU   : $GpuId"
Write-Host "Model : $Model"
Write-Host "Scale : $Scale"
Write-Host "TTA   : $($EnableTta.IsPresent)"
Write-Host ""

$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    $upscaylArgs = @(
        "-i", $InputDir,
        "-o", $OutputDir,
        "-m", $models,
        "-n", $Model,
        "-z", "$Scale",
        "-s", "$Scale",
        "-g", "$GpuId",
        "-f", "png",
        "-v"
    )
    if ($EnableTta) {
        $upscaylArgs += "-x"
    }

    & $upscayl @upscaylArgs 2>&1 | ForEach-Object { Write-Host $_ }
    $exitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousErrorActionPreference
}

if ($exitCode -ne 0) {
    throw "upscayl-bin.exe failed with exit code $exitCode"
}

Write-Host ""
Write-Host "Upscayl batch finished."
