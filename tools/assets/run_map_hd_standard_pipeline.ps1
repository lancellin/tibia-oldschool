param(
    [string]$BatchRoot = "C:\tibia-oldschool\tools\assets\tests\map-hd-standard",
    [int]$GpuId = 1
)

$ErrorActionPreference = "Stop"

$scriptsRoot = "C:\tibia-oldschool\tools\assets"
$logDir = Join-Path $BatchRoot "logs"
$log = Join-Path $logDir ("pipeline-" + (Get-Date -Format "yyyyMMdd-HHmmss") + ".log")

New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Write-Log {
    param([string]$Message)
    $line = "[" + (Get-Date -Format "yyyy-MM-dd HH:mm:ss") + "] " + $Message
    $line | Tee-Object -FilePath $log -Append
}

function Invoke-LoggedCommand {
    param([string[]]$CommandArgs)

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $CommandArgs[0] @($CommandArgs[1..($CommandArgs.Length - 1)]) 2>&1 | Tee-Object -FilePath $log -Append
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    if ($exitCode -ne 0) {
        throw "$($CommandArgs[0]) failed with exit code $exitCode"
    }
}

try {
    Write-Log "Starting map HD standard pipeline."
    Write-Log "BatchRoot: $BatchRoot"
    Write-Log "GpuId: $GpuId"

    Invoke-LoggedCommand @(
        "powershell",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $scriptsRoot "run_map_hd_upscayl_standard_tta.ps1"),
        "-InputDir", (Join-Path $BatchRoot "inputs-flat"),
        "-OutputDir", (Join-Path $BatchRoot "upscayl-standard-4x-tta"),
        "-GpuId", "$GpuId"
    )

    Write-Log "Upscayl finished. Building CWM."

    Invoke-LoggedCommand @(
        "powershell",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $scriptsRoot "build_map_hd_cwm_from_upscayl.ps1"),
        "-UpscaylOutputDir", (Join-Path $BatchRoot "upscayl-standard-4x-tta"),
        "-DownscaledDir", (Join-Path $BatchRoot "processed-standard-64"),
        "-SpritesDir", (Join-Path $BatchRoot "built\sprites-64"),
        "-CwmOut", (Join-Path $BatchRoot "built\Tibia.map-hd-standard-64.cwm"),
        "-Index", (Join-Path $BatchRoot "original\index.json")
    )

    Write-Log "Pipeline finished successfully."
    Write-Log "CWM: $(Join-Path $BatchRoot 'built\Tibia.map-hd-standard-64.cwm')"
} catch {
    Write-Log "Pipeline failed: $($_.Exception.Message)"
    throw
}
