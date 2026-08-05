[CmdletBinding()]
param(
    [string]$XmlPath = '',
    [string]$OutputPath = '',
    [int]$Threshold = 600
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$workspace = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrWhiteSpace($XmlPath)) {
    $XmlPath = Join-Path $workspace 'server\data\world\world-spawn.xml'
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $workspace 'audit\spawn_respawns_below_600.md'
}
if ($Threshold -le 0) {
    throw 'Threshold deve ser maior que zero.'
}
if (-not (Test-Path -LiteralPath $XmlPath)) {
    throw "XML nao encontrado: $XmlPath"
}

[xml]$document = Get-Content -LiteralPath $XmlPath
$records = [System.Collections.Generic.List[object]]::new()
$monsterCount = 0
$npcCount = 0

foreach ($spawn in $document.spawns.spawn) {
    $centerX = [int]$spawn.centerx
    $centerY = [int]$spawn.centery
    $centerZ = [int]$spawn.centerz

    foreach ($monster in @($spawn.SelectNodes('monster'))) {
        if ($null -eq $monster) {
            continue
        }

        $monsterCount++
        $spawnTime = [int]$monster.spawntime
        if ($spawnTime -lt $Threshold) {
            $records.Add([pscustomobject]@{
                Name = [string]$monster.name
                X = $centerX + [int]$monster.x
                Y = $centerY + [int]$monster.y
                Z = $centerZ
                SpawnTime = $spawnTime
            })
        }
    }

    foreach ($npc in @($spawn.SelectNodes('npc'))) {
        if ($null -ne $npc) {
            $npcCount++
        }
    }
}

$records = @($records | Sort-Object Name, Z, X, Y, SpawnTime)
$sourceHash = (Get-FileHash -LiteralPath $XmlPath -Algorithm SHA256).Hash
$builder = [System.Text.StringBuilder]::new()

[void]$builder.AppendLine("# Respawns locais abaixo de $Threshold segundos")
[void]$builder.AppendLine()
[void]$builder.AppendLine("- Fonte exclusiva: ``$XmlPath``")
[void]$builder.AppendLine("- SHA-256 do XML: ``$sourceHash``")
[void]$builder.AppendLine("- Criterio: slots ``monster`` com ``spawntime < $Threshold``")
[void]$builder.AppendLine('- Unidade: segundos')
[void]$builder.AppendLine('- Coordenada: centerx + x, centery + y, centerz')
[void]$builder.AppendLine("- Total de slots monster no XML: $monsterCount")
[void]$builder.AppendLine("- Total abaixo de $Threshold segundos: $($records.Count)")
[void]$builder.AppendLine("- Criaturas distintas abaixo do limite: $(@($records | Group-Object Name).Count)")
[void]$builder.AppendLine("- NPCs excluidos: $npcCount")
[void]$builder.AppendLine()

[void]$builder.AppendLine('## Resumo por tempo')
[void]$builder.AppendLine()
[void]$builder.AppendLine('| Spawntime | Slots |')
[void]$builder.AppendLine('|---:|---:|')
foreach ($group in ($records | Group-Object SpawnTime | Sort-Object { [int]$_.Name })) {
    [void]$builder.AppendLine("| $($group.Name) | $($group.Count) |")
}
[void]$builder.AppendLine()

[void]$builder.AppendLine('## Resumo por criatura')
[void]$builder.AppendLine()
[void]$builder.AppendLine('| Criatura | Slots | Menor tempo | Maior tempo |')
[void]$builder.AppendLine('|---|---:|---:|---:|')
foreach ($group in ($records | Group-Object Name | Sort-Object Name)) {
    $minimum = ($group.Group | Measure-Object SpawnTime -Minimum).Minimum
    $maximum = ($group.Group | Measure-Object SpawnTime -Maximum).Maximum
    [void]$builder.AppendLine("| $($group.Name) | $($group.Count) | $minimum | $maximum |")
}
[void]$builder.AppendLine()

[void]$builder.AppendLine('## Todos os slots')
[void]$builder.AppendLine()
foreach ($group in ($records | Group-Object Name | Sort-Object Name)) {
    [void]$builder.AppendLine("<details>")
    [void]$builder.AppendLine("<summary>$($group.Name) - $($group.Count) slots</summary>")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Coordenada | Spawntime |')
    [void]$builder.AppendLine('|---|---:|')
    foreach ($record in ($group.Group | Sort-Object Z, X, Y, SpawnTime)) {
        [void]$builder.AppendLine("| $($record.X), $($record.Y), $($record.Z) | $($record.SpawnTime) |")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('</details>')
    [void]$builder.AppendLine()
}

$outputDirectory = Split-Path $OutputPath -Parent
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
[System.IO.File]::WriteAllText($OutputPath, $builder.ToString(), [System.Text.UTF8Encoding]::new($false))

[pscustomobject]@{
    XmlPath = $XmlPath
    OutputPath = $OutputPath
    Threshold = $Threshold
    MonsterSlots = $monsterCount
    NpcSlotsExcluded = $npcCount
    MatchingSlots = $records.Count
    CreatureNames = @($records | Group-Object Name).Count
    SourceHash = $sourceHash
}
