param(
    [ValidateRange(1000, 60000)]
    [int]$IntervalMilliseconds = 5000
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$serverDirectory = Join-Path $root "server"
$tfsExecutable = Join-Path $serverDirectory "tfs.exe"
$resultDirectory = Join-Path $root "performance-results\dispatcher"
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$metricsPath = Join-Path $resultDirectory "$timestamp-dispatcher.csv"

if (-not (Test-Path -LiteralPath $tfsExecutable -PathType Leaf)) {
    throw "TFS executable not found: $tfsExecutable"
}

New-Item -ItemType Directory -Path $resultDirectory -Force | Out-Null
$env:TFS_DISPATCHER_METRICS_PATH = $metricsPath
$env:TFS_DISPATCHER_METRICS_INTERVAL_MS = $IntervalMilliseconds.ToString()

Write-Host "Dispatcher metrics: $metricsPath"
Write-Host "Aggregated measurements only; no per-event logging."

Push-Location $serverDirectory
try {
    & $tfsExecutable
}
finally {
    Pop-Location
    Remove-Item Env:TFS_DISPATCHER_METRICS_PATH -ErrorAction SilentlyContinue
    Remove-Item Env:TFS_DISPATCHER_METRICS_INTERVAL_MS -ErrorAction SilentlyContinue
}

