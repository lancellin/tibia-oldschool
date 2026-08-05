param(
    [ValidateRange(1000, 60000)]
    [int]$IntervalMilliseconds = 5000
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$serverDirectory = Join-Path $root "server"
$tfsExecutable = Join-Path $serverDirectory "tfs.exe"
$resultDirectory = Join-Path $root "performance-results\autosend"
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$metricsPath = Join-Path $resultDirectory "$timestamp-autosend.csv"

if (-not (Test-Path -LiteralPath $tfsExecutable -PathType Leaf)) {
    throw "TFS executable not found: $tfsExecutable"
}

New-Item -ItemType Directory -Path $resultDirectory -Force | Out-Null
$env:TFS_AUTOSEND_METRICS_PATH = $metricsPath
$env:TFS_AUTOSEND_METRICS_INTERVAL_MS = $IntervalMilliseconds.ToString()

Write-Host "Autosend metrics: $metricsPath"
Write-Host "The current autosend vector scan remains unchanged."

Push-Location $serverDirectory
try {
    & $tfsExecutable
}
finally {
    Pop-Location
    Remove-Item Env:TFS_AUTOSEND_METRICS_PATH -ErrorAction SilentlyContinue
    Remove-Item Env:TFS_AUTOSEND_METRICS_INTERVAL_MS -ErrorAction SilentlyContinue
}
