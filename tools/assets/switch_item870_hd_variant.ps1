param(
    [string]$Variant
)

$ErrorActionPreference = "Stop"

$root = "C:\tibia-oldschool"
$variantsRoot = Join-Path $root "tools\assets\tests\tibia-sprites\outputs\item-870-model-tests"
$thingsDir = Join-Path $root "sources\otclient-redemption\data\things\772"
$target = Join-Path $thingsDir "Tibia.cwm"

if ([string]::IsNullOrWhiteSpace($Variant)) {
    Write-Host "Available item 870 HD variants:"
    Get-ChildItem -LiteralPath $variantsRoot -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName "Tibia.cwm") } |
        Sort-Object Name |
        ForEach-Object { Write-Host ("  " + $_.Name) }
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File C:\tibia-oldschool\tools\assets\switch_item870_hd_variant.ps1 <variant>"
    exit 0
}

$source = Join-Path (Join-Path $variantsRoot $Variant) "Tibia.cwm"
if (!(Test-Path -LiteralPath $source)) {
    throw "Variant '$Variant' was not found at $source"
}

if (!(Test-Path -LiteralPath $target)) {
    throw "Active Tibia.cwm was not found at $target"
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backup = Join-Path $thingsDir "Tibia.before-item870-switch-$stamp.cwm"
Copy-Item -LiteralPath $target -Destination $backup -Force
Copy-Item -LiteralPath $source -Destination $target -Force

Write-Host "Activated item 870 HD variant: $Variant"
Write-Host "Backup created: $backup"
Write-Host "Now toggle HD off/on in the client to reload the CWM."
