param(
    [ValidateRange(1, 10000)]
    [int]$Count = 10,

    [ValidateSet("idle", "movement", "follow", "attack", "attack-follow", "mixed")]
    [string]$Profile = "idle",

    [ValidateRange(0, 86400)]
    [int]$DurationSeconds = 300,

    [ValidateRange(1, 200)]
    [int]$BatchSize = 10,

    [double]$BatchDelaySeconds = 2.0,

    [switch]$FollowChain,

    [string]$FollowTerminalName = "",

    [int]$TfsPid = 0,

    [switch]$FailOnLoginError
)

$ErrorActionPreference = "Stop"
$python = "D:\tibia-dev-tools\Python312\python.exe"
$script = Join-Path $PSScriptRoot "headless_load.py"

if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
    throw "Python runtime not found: $python"
}

$arguments = @(
    $script,
    "--count", $Count,
    "--profile", $Profile,
    "--duration", $DurationSeconds,
    "--batch-size", $BatchSize,
    "--batch-delay", $BatchDelaySeconds
)

if ($FollowChain) {
    if ([string]::IsNullOrWhiteSpace($FollowTerminalName)) {
        throw "-FollowChain requires -FollowTerminalName."
    }
    $arguments += @("--follow-chain", "--follow-terminal-name", $FollowTerminalName)
}

if ($TfsPid -gt 0) {
    $arguments += @("--tfs-pid", $TfsPid)
}

if ($FailOnLoginError) {
    $arguments += "--fail-on-login-error"
}

& $python @arguments
exit $LASTEXITCODE
