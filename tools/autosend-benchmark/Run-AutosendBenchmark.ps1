param(
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$source = Join-Path $PSScriptRoot "autosend_benchmark.cpp"
$buildDirectory = Join-Path $PSScriptRoot "build"
$executable = Join-Path $buildDirectory "autosend_benchmark.exe"
$vcvars = "D:\tibia-dev-tools\VisualStudio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
    throw "Visual Studio environment script not found: $vcvars"
}

New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $resultDirectory = Join-Path $root "performance-results\autosend-microbenchmark"
    New-Item -ItemType Directory -Path $resultDirectory -Force | Out-Null
    $OutputPath = Join-Path $resultDirectory "$(Get-Date -Format 'yyyyMMdd-HHmmss')-autosend-current.csv"
}

$compile = "call `"$vcvars`" && cl.exe /nologo /std:c++17 /O2 /EHsc `"$source`" /Fe:`"$executable`""
& cmd.exe /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
    throw "Microbenchmark compilation failed with exit code $LASTEXITCODE"
}

& $executable --output $OutputPath
if ($LASTEXITCODE -ne 0) {
    throw "Microbenchmark failed with exit code $LASTEXITCODE"
}
