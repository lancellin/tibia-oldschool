param(
    [switch]$Clean,
    [int]$Parallel = 8
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$CMake = (Get-Command cmake.exe -ErrorAction Stop).Source
$Toolchain = Join-Path $Root "tools\vcpkg\scripts\buildsystems\vcpkg.cmake"
$ClientInstalled = Join-Path $Root "tools\dependencies\otclient-vcpkg-installed"
$StaticInstalled = Join-Path $Root "tools\vcpkg\installed\x64-windows-static"
$ClientStaticInstalled = Join-Path $ClientInstalled "x64-windows-static"
$BuildRoot = Join-Path $Root "build-validation"
$Logs = Join-Path $Root "build-results\logs"
$Results = Join-Path $Root "build-results\executables"

New-Item -ItemType Directory -Path $Logs, $Results -Force | Out-Null

if ($Clean -and (Test-Path -LiteralPath $BuildRoot)) {
    $Resolved = (Resolve-Path -LiteralPath $BuildRoot).Path
    if (-not $Resolved.StartsWith($Root + "\", [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe build cleanup path: $Resolved"
    }
    Remove-Item -LiteralPath $Resolved -Recurse -Force
}

function Invoke-BuildStep {
    param(
        [string]$Name,
        [string[]]$Arguments
    )

    Write-Host "[$Name] $CMake $($Arguments -join ' ')"
    $PreviousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $Output = & $CMake @Arguments 2>&1
    $ExitCode = $LASTEXITCODE
    $ErrorActionPreference = $PreviousErrorAction
    $Output |
        ForEach-Object { $_.ToString() } |
        Tee-Object -FilePath (Join-Path $Logs "$Name.log")
    if ($ExitCode -ne 0) {
        throw "$Name failed with exit code $ExitCode"
    }
}

$TfsBuild = Join-Path $BuildRoot "tfs"
Invoke-BuildStep "tfs-configure" @(
    "-S", (Join-Path $Root "sources\nekiro-tfs-1.5-7.72"),
    "-B", $TfsBuild,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_TOOLCHAIN_FILE=$($Toolchain.Replace('\', '/'))",
    "-DVCPKG_TARGET_TRIPLET=x64-windows",
    "-DVCPKG_MANIFEST_MODE=OFF",
    "-DSKIP_GIT=ON"
)
Invoke-BuildStep "tfs-build" @(
    "--build", $TfsBuild,
    "--config", "Release",
    "--parallel", "$Parallel"
)

$RmeBuild = Join-Path $BuildRoot "rme"
Invoke-BuildStep "rme-configure" @(
    "-S", (Join-Path $Root "sources\rme-otacademy"),
    "-B", $RmeBuild,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_TOOLCHAIN_FILE=$($Toolchain.Replace('\', '/'))",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
    "-DVCPKG_MANIFEST_MODE=OFF",
    "-DLibArchive_LIBRARY=$((Join-Path $StaticInstalled 'lib\archive.lib').Replace('\', '/'))"
)
Invoke-BuildStep "rme-build" @(
    "--build", $RmeBuild,
    "--config", "Release",
    "--parallel", "$Parallel"
)

$ClientBuild = Join-Path $BuildRoot "otclient"
Invoke-BuildStep "otclient-configure" @(
    "-S", (Join-Path $Root "sources\otclient-redemption"),
    "-B", $ClientBuild,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_TOOLCHAIN_FILE=$($Toolchain.Replace('\', '/'))",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
    "-DVCPKG_HOST_TRIPLET=x64-windows-static",
    "-DVCPKG_INSTALLED_DIR=$($ClientInstalled.Replace('\', '/'))",
    "-DVCPKG_MANIFEST_MODE=OFF",
    "-DVCPKG_BUILD_TYPE=release",
    "-DBUILD_STATIC_LIBRARY=ON",
    "-DOTCLIENT_BUILD_TESTS=OFF",
    "-DSPEED_UP_BUILD_UNITY=ON",
    "-DOPTIONS_ENABLE_SCCACHE=OFF",
    "-DLUAJIT_LIBRARY=$((Join-Path $ClientStaticInstalled 'lib\lua51.lib').Replace('\', '/'))",
    "-DVORBISFILE_LIBRARY=$((Join-Path $ClientStaticInstalled 'lib\vorbisfile.lib').Replace('\', '/'))",
    "-DVORBIS_LIBRARY=$((Join-Path $ClientStaticInstalled 'lib\vorbis.lib').Replace('\', '/'))"
)
Invoke-BuildStep "otclient-build" @(
    "--build", $ClientBuild,
    "--config", "RelWithDebInfo",
    "--parallel", "$Parallel"
)

$TfsExe = Join-Path $TfsBuild "Release\tfs.exe"
$RmeExe = Join-Path $RmeBuild "Release\rme.exe"
$ClientExe = Join-Path $Root "sources\otclient-redemption\RelWithDebInfo\otclient.exe"

Copy-Item -LiteralPath $TfsExe -Destination (Join-Path $Root "server\tfs.exe") -Force
Copy-Item -LiteralPath $RmeExe -Destination (Join-Path $Root "sources\rme-otacademy\rme.exe") -Force
Copy-Item -LiteralPath $ClientExe -Destination (Join-Path $Root "sources\otclient-redemption\otclient.exe") -Force

Copy-Item -LiteralPath $TfsExe -Destination (Join-Path $Results "tfs.exe") -Force
Copy-Item -LiteralPath $RmeExe -Destination (Join-Path $Results "rme.exe") -Force
Copy-Item -LiteralPath $ClientExe -Destination (Join-Path $Results "otclient.exe") -Force

Get-Item -LiteralPath (Join-Path $Results "tfs.exe"),
    (Join-Path $Results "rme.exe"),
    (Join-Path $Results "otclient.exe") |
    Select-Object Name, Length, LastWriteTime,
        @{Name = "SHA256"; Expression = { (Get-FileHash $_.FullName -Algorithm SHA256).Hash }}
