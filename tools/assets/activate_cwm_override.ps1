param(
    [Parameter(Mandatory = $true)]
    [string]$Cwm
)

$ErrorActionPreference = "Stop"

$thingsDir = "C:\tibia-oldschool\sources\otclient-redemption\data\things\772"
$target = Join-Path $thingsDir "Tibia.cwm"

if (!(Test-Path -LiteralPath $Cwm)) {
    throw "CWM file was not found at $Cwm"
}
if (!(Test-Path -LiteralPath $target)) {
    throw "Active Tibia.cwm was not found at $target"
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backup = Join-Path $thingsDir "Tibia.before-cwm-activate-$stamp.cwm"
Copy-Item -LiteralPath $target -Destination $backup -Force
Copy-Item -LiteralPath $Cwm -Destination $target -Force

Write-Host "Activated CWM override: $Cwm"
Write-Host "Backup created: $backup"
Write-Host "Toggle HD off/on in the client to reload the override."
