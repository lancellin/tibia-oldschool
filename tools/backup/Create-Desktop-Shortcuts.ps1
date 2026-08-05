param(
    [string]$FolderName = "Tibia Oldschool - Backup Teste 2026-06-10"
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Desktop = [Environment]::GetFolderPath("Desktop")
$ShortcutFolder = Join-Path $Desktop $FolderName
$Shell = New-Object -ComObject WScript.Shell

New-Item -ItemType Directory -Path $ShortcutFolder -Force | Out-Null

function New-Shortcut {
    param(
        [string]$Name,
        [string]$Target,
        [string]$WorkingDirectory,
        [string]$Arguments = "",
        [string]$Icon = ""
    )

    if (-not (Test-Path -LiteralPath $Target)) {
        throw "Shortcut target not found: $Target"
    }

    $Shortcut = $Shell.CreateShortcut((Join-Path $ShortcutFolder "$Name.lnk"))
    $Shortcut.TargetPath = $Target
    $Shortcut.WorkingDirectory = $WorkingDirectory
    $Shortcut.Arguments = $Arguments
    if ($Icon) {
        $Shortcut.IconLocation = "$Icon,0"
    } else {
        $Shortcut.IconLocation = "$Target,0"
    }
    $Shortcut.Save()
}

$ServerDir = Join-Path $Root "server"
$ClientDir = Join-Path $Root "sources\otclient-redemption"
$RmeDir = Join-Path $Root "sources\rme-otacademy"
$MapPath = Join-Path $Root "server\data\world\world.otbm"
$ServerLauncher = "D:\tibia-dev-tools\Start-Tibia-Server.cmd"
$ServerTarget = Join-Path $ServerDir "tfs.exe"
$ServerWorkingDirectory = $ServerDir

if (Test-Path -LiteralPath $ServerLauncher) {
    $ServerTarget = $ServerLauncher
    $ServerWorkingDirectory = Split-Path $ServerLauncher
}

New-Shortcut "01 - Servidor TFS (banco de teste)" `
    $ServerTarget $ServerWorkingDirectory "" (Join-Path $ServerDir "tfs.exe")
New-Shortcut "02 - OTClient Redemption" `
    (Join-Path $ClientDir "otclient.exe") $ClientDir
New-Shortcut "03 - RME OTAcademy" `
    (Join-Path $RmeDir "rme.exe") $RmeDir "`"$MapPath`""

Write-Host "Shortcuts created in: $ShortcutFolder"
