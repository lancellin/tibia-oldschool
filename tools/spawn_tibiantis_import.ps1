[CmdletBinding()]
param(
    [ValidateSet('DryRun', 'Apply')]
    [string]$Mode = 'DryRun',

    [ValidateSet('Strict', 'Relaxed', 'Rebuild')]
    [string]$Policy = 'Strict',

    [string]$XmlPath = '',

    [string]$ReportDirectory = '',

    [string]$TibiantisUrl = 'https://usa.michal.es/tibiantis/map/7.7/spawns.json.gz'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($XmlPath)) {
    $XmlPath = Join-Path (Split-Path $PSScriptRoot -Parent) 'server\data\world\world-spawn.xml'
}
if ([string]::IsNullOrWhiteSpace($ReportDirectory)) {
    $ReportDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'audit\spawn_tibiantis'
}

$MinimumSpawnTime = 10
$MaximumSpawnTime = 86400
$MaximumDistance = 10
$RebuildPropagationDistance = 17
$RebuildGlobalTimeOverrides = @{
    'deathslicer' = 86400
}
$RebuildDefaultMinimumSpawnTime = 500
$RebuildMinimumSpawnTimeOverrides = @{
    'bug' = 300
    'cave rat' = 300
    'hero' = 900
    'hydra' = 900
    'rat' = 300
    'wolf' = 300
}
$RebuildMinimumSpawnTimeExemptNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($name in @(
    'ashmunrah',
    'dipthrah',
    'flamethrower',
    'magicthrower',
    'mahrdis',
    'morguthis',
    'omruc',
    'plaguethrower',
    'rahemos',
    'thalas',
    'vashresamun'
)) {
    [void]$RebuildMinimumSpawnTimeExemptNames.Add($name)
}
$RebuildRemainingTimeOverrides = @{
    'bat' = 600
    'bug' = 600
    'cyclops' = 600
    'dwarf' = 600
    'elf' = 600
    'frost troll' = 600
    'ghoul' = 500
    'minotaur' = 600
    'minotaur archer' = 750
    'minotaur guard' = 600
    'minotaur mage' = 900
    'orc' = 600
    'orc spearman' = 600
    'orc warrior' = 600
    'polar bear' = 600
    'priestess' = 750
    'rabbit' = 600
    'rat' = 600
    'rotworm' = 600
    'skeleton' = 600
    'slime' = 750
    'smuggler' = 600
    'snake' = 600
    'spider' = 600
    'troll' = 600
    'warlock' = 1200
    'wasp' = 600
    'wolf' = 600
}
$RebuildManualResolvedNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($name in @(
    'amazon',
    'ashmunrah',
    'bandit',
    'banshee',
    'beholder',
    'demon skeleton',
    'dipthrah',
    'dragon',
    'dragon lord',
    'elder beholder',
    'fire devil',
    'fire elemental',
    'flamethrower',
    'hero',
    'hydra',
    'magicthrower',
    'mahrdis',
    'morguthis',
    'mummy',
    'omruc',
    'plaguethrower',
    'rahemos',
    'thalas',
    'vashresamun'
)) {
    [void]$RebuildManualResolvedNames.Add($name)
}
$RemainingReportExcludedNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($name in @('', 'bear', 'chicken', 'deer', 'dog', 'butterfly', 'flamingo', 'pig', 'poison spider', 'rabbit', 'rat', 'sheep')) {
    [void]$RemainingReportExcludedNames.Add($name)
}

function Normalize-CreatureName {
    param([AllowNull()][string]$Name)

    if ([string]::IsNullOrWhiteSpace($Name)) {
        return ''
    }

    return ((($Name -replace '_', ' ') -replace '-', ' ').Trim().ToLowerInvariant() -replace '\s+', ' ')
}

function Get-ChebyshevDistance {
    param(
        [int]$X1,
        [int]$Y1,
        [int]$X2,
        [int]$Y2
    )

    return [Math]::Max([Math]::Abs($X1 - $X2), [Math]::Abs($Y1 - $Y2))
}

function Get-AttributeMap {
    param([string]$AttributeText)

    $attributes = @{}
    foreach ($attribute in [regex]::Matches($AttributeText, '(?<name>[A-Za-z0-9_-]+)="(?<value>[^"]*)"')) {
        $attributes[$attribute.Groups['name'].Value] = $attribute.Groups['value'].Value
    }

    return $attributes
}

function Get-FileTextPreservingEncoding {
    param([string]$Path)

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    $offset = 0
    $preamble = [byte[]]@()
    $encoding = [System.Text.UTF8Encoding]::new($false)

    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $encoding = [System.Text.UTF8Encoding]::new($true)
        $offset = 3
        $preamble = [byte[]](0xEF, 0xBB, 0xBF)
    } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        $encoding = [System.Text.UnicodeEncoding]::new($false, $true)
        $offset = 2
        $preamble = [byte[]](0xFF, 0xFE)
    } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF) {
        $encoding = [System.Text.UnicodeEncoding]::new($true, $true)
        $offset = 2
        $preamble = [byte[]](0xFE, 0xFF)
    }

    return [pscustomobject]@{
        Text = $encoding.GetString($bytes, $offset, $bytes.Length - $offset)
        Encoding = $encoding
        Preamble = $preamble
        Bytes = $bytes
    }
}

function Write-FileTextPreservingEncoding {
    param(
        [string]$Path,
        [string]$Text,
        [System.Text.Encoding]$Encoding,
        [byte[]]$Preamble
    )

    [byte[]]$body = $Encoding.GetBytes($Text)
    [byte[]]$output = New-Object byte[] ($Preamble.Length + $body.Length)
    if ($Preamble.Length -gt 0) {
        [Array]::Copy($Preamble, 0, $output, 0, $Preamble.Length)
    }
    [Array]::Copy($body, 0, $output, $Preamble.Length, $body.Length)
    [System.IO.File]::WriteAllBytes($Path, $output)
}

function Get-LocalMonsterRecords {
    param([string]$XmlText)

    $records = [System.Collections.Generic.List[object]]::new()
    $nodeIndex = 0
    $spawnPattern = [regex]'(?s)<spawn\b(?<attributes>[^>]*)>(?<content>.*?)</spawn>'
    $monsterPattern = [regex]'(?s)<monster\b(?<attributes>[^>]*)/\s*>'
    $spawnMatches = $spawnPattern.Matches($XmlText)

    foreach ($spawnMatch in $spawnMatches) {
        $spawnAttributes = Get-AttributeMap $spawnMatch.Groups['attributes'].Value
        if (-not $spawnAttributes.ContainsKey('centerx') -or -not $spawnAttributes.ContainsKey('centery') -or -not $spawnAttributes.ContainsKey('centerz')) {
            continue
        }

        $centerX = [int]$spawnAttributes['centerx']
        $centerY = [int]$spawnAttributes['centery']
        $centerZ = [int]$spawnAttributes['centerz']
        $contentOffset = $spawnMatch.Groups['content'].Index

        foreach ($monsterMatch in $monsterPattern.Matches($spawnMatch.Groups['content'].Value)) {
            $attributes = Get-AttributeMap $monsterMatch.Groups['attributes'].Value
            if (-not $attributes.ContainsKey('name')) {
                continue
            }

            foreach ($required in 'x', 'y', 'spawntime') {
                if (-not $attributes.ContainsKey($required)) {
                    throw "Monster sem atributo '$required' no indice $nodeIndex."
                }
            }

            $tag = $monsterMatch.Value
            $spawnTimeMatch = [regex]::Match($tag, '\bspawntime="(?<value>\d+)"')
            if (-not $spawnTimeMatch.Success) {
                throw "Nao foi possivel localizar o valor de spawntime no indice $nodeIndex."
            }

            $records.Add([pscustomobject]@{
                Id = $nodeIndex
                Name = Normalize-CreatureName $attributes['name']
                OriginalName = $attributes['name']
                X = $centerX + [int]$attributes['x']
                Y = $centerY + [int]$attributes['y']
                Z = $centerZ
                CurrentSpawnTime = [int]$attributes['spawntime']
                TagIndex = $contentOffset + $monsterMatch.Index
                SpawnTimeValueIndex = $contentOffset + $monsterMatch.Index + $spawnTimeMatch.Groups['value'].Index
                SpawnTimeValueLength = $spawnTimeMatch.Groups['value'].Length
                NearestDistance = $null
                NearestMarkerIds = [System.Collections.Generic.List[int]]::new()
                AssignedMarkerId = $null
                CandidateMarkerIds = [System.Collections.Generic.List[int]]::new()
                ManualReason = ''
                RebuildStatus = ''
                RebuildTargetTime = $null
                RebuildReferenceDistance = $null
                RebuildReferenceLocalIds = [System.Collections.Generic.List[int]]::new()
            })
            $nodeIndex++
        }
    }

    return $records
}

function Get-TibiantisData {
    param([string]$Url)

    $response = Invoke-WebRequest -UseBasicParsing -Uri $Url
    [byte[]]$wireBytes = $response.Content
    $wireHash = [System.BitConverter]::ToString(
        [System.Security.Cryptography.SHA256]::Create().ComputeHash($wireBytes)
    ).Replace('-', '')

    # O endpoint entrega uma string binaria comprimida codificada em UTF-8.
    # Reverter para ISO-8859-1 recupera os bytes zlib esperados pelo pako do mapa.
    $encodedString = [System.Text.Encoding]::UTF8.GetString($wireBytes)
    [byte[]]$zlibBytes = [System.Text.Encoding]::GetEncoding(28591).GetBytes($encodedString)
    if ($zlibBytes.Length -lt 6) {
        throw 'Resposta de spawns do TibiAntis curta demais para conter um stream zlib valido.'
    }

    [byte[]]$deflateBytes = $zlibBytes[2..($zlibBytes.Length - 5)]
    $inputStream = [System.IO.MemoryStream]::new($deflateBytes)
    $deflateStream = [System.IO.Compression.DeflateStream]::new($inputStream, [System.IO.Compression.CompressionMode]::Decompress)
    $reader = [System.IO.StreamReader]::new($deflateStream)
    try {
        $json = $reader.ReadToEnd()
    } finally {
        $reader.Dispose()
        $deflateStream.Dispose()
        $inputStream.Dispose()
    }

    return [pscustomobject]@{
        Data = ($json | ConvertFrom-Json)
        WireHash = $wireHash
    }
}

function Get-TibiantisMarkers {
    param($TibiantisData)

    $markers = [System.Collections.Generic.List[object]]::new()
    $markerId = 0
    foreach ($zProperty in $TibiantisData.spawns.psobject.Properties) {
        foreach ($yProperty in $zProperty.Value.psobject.Properties) {
            foreach ($xProperty in $yProperty.Value.psobject.Properties) {
                $value = @($xProperty.Value)
                if ($value.Count -lt 3) {
                    throw "Marcador TibiAntis invalido em $($xProperty.Name), $($yProperty.Name), $($zProperty.Name)."
                }

                $raceProperty = $TibiantisData.races.psobject.Properties[[string]$value[0]]
                $name = if ($null -eq $raceProperty) { '' } else { Normalize-CreatureName ([string]$raceProperty.Value) }
                $markers.Add([pscustomobject]@{
                    Id = $markerId
                    Name = $name
                    X = [int]$xProperty.Name
                    Y = [int]$yProperty.Name
                    Z = [int]$zProperty.Name
                    Count = [int]$value[1]
                    Regen = [int]$value[2]
                    CandidateLocalIds = [System.Collections.Generic.List[int]]::new()
                    AssignedLocalIds = [System.Collections.Generic.List[int]]::new()
                    AmbiguousLocalIds = [System.Collections.Generic.List[int]]::new()
                    ConflictingLocalIds = [System.Collections.Generic.List[int]]::new()
                    ResolvedSameRegenConflictLocalIds = [System.Collections.Generic.List[int]]::new()
                    ResolvedSameRegenTieLocalIds = [System.Collections.Generic.List[int]]::new()
                    ResolvedLowestRegenTieLocalIds = [System.Collections.Generic.List[int]]::new()
                    UnresolvedConflictLocalIds = [System.Collections.Generic.List[int]]::new()
                    Classification = ''
                    ClassificationReason = ''
                })
                $markerId++
            }
        }
    }

    return $markers
}

function Add-ToListMap {
    param(
        [hashtable]$Map,
        [string]$Key,
        $Value
    )

    if (-not $Map.ContainsKey($Key)) {
        $Map[$Key] = [System.Collections.Generic.List[object]]::new()
    }
    $Map[$Key].Add($Value)
}

function Get-GroupKey {
    param($Entry)
    return "$($Entry.Name)|$($Entry.Z)"
}

function Associate-LocalMonsters {
    param(
        [System.Collections.Generic.List[object]]$LocalRecords,
        [System.Collections.Generic.List[object]]$Markers,
        [bool]$AllowDistanceOver10
    )

    $markersByGroup = @{}
    foreach ($marker in $Markers) {
        Add-ToListMap $markersByGroup (Get-GroupKey $marker) $marker
    }

    $localsByGroup = @{}
    foreach ($local in $LocalRecords) {
        Add-ToListMap $localsByGroup (Get-GroupKey $local) $local
    }

    foreach ($groupKey in $localsByGroup.Keys) {
        $groupLocals = $localsByGroup[$groupKey]
        if (-not $markersByGroup.ContainsKey($groupKey)) {
            foreach ($local in $groupLocals) {
                $local.ManualReason = 'Nenhum marcador TibiAntis da mesma criatura e andar.'
            }
            continue
        }

        $groupMarkers = $markersByGroup[$groupKey]
        foreach ($local in $groupLocals) {
            $nearestDistance = [int]::MaxValue
            $nearestMarkers = [System.Collections.Generic.List[object]]::new()
            foreach ($marker in $groupMarkers) {
                $distance = Get-ChebyshevDistance $local.X $local.Y $marker.X $marker.Y
                if ($distance -le $MaximumDistance) {
                    $marker.CandidateLocalIds.Add($local.Id)
                    $local.CandidateMarkerIds.Add($marker.Id)
                }

                if ($distance -lt $nearestDistance) {
                    $nearestDistance = $distance
                    $nearestMarkers.Clear()
                    $nearestMarkers.Add($marker)
                } elseif ($distance -eq $nearestDistance) {
                    $nearestMarkers.Add($marker)
                }
            }

            $local.NearestDistance = $nearestDistance
            foreach ($marker in $nearestMarkers) {
                $local.NearestMarkerIds.Add($marker.Id)
            }

            if (-not $AllowDistanceOver10 -and $nearestDistance -gt $MaximumDistance) {
                $local.ManualReason = "Marcador mais proximo acima de $MaximumDistance tiles."
                continue
            }

            if ($nearestMarkers.Count -ne 1) {
                $tieRegen = $nearestMarkers[0].Regen
                $sameValidRegen = $tieRegen -ge $MinimumSpawnTime -and $tieRegen -le $MaximumSpawnTime
                foreach ($marker in $nearestMarkers) {
                    if ($marker.Regen -ne $tieRegen) {
                        $sameValidRegen = $false
                        break
                    }
                }

                if ($AllowDistanceOver10 -and $sameValidRegen) {
                    $canonicalMarker = $nearestMarkers | Sort-Object Id | Select-Object -First 1
                    $local.AssignedMarkerId = $canonicalMarker.Id
                    $canonicalMarker.AssignedLocalIds.Add($local.Id)
                    foreach ($marker in $nearestMarkers) {
                        $marker.ResolvedSameRegenTieLocalIds.Add($local.Id)
                    }
                    continue
                }

                $validNearestMarkers = @($nearestMarkers | Where-Object { $_.Regen -ge $MinimumSpawnTime -and $_.Regen -le $MaximumSpawnTime })
                if ($AllowDistanceOver10 -and $validNearestMarkers.Count -gt 0) {
                    $lowestRegen = ($validNearestMarkers | Measure-Object Regen -Minimum).Minimum
                    $canonicalMarker = $validNearestMarkers | Where-Object Regen -eq $lowestRegen | Sort-Object Id | Select-Object -First 1
                    $local.AssignedMarkerId = $canonicalMarker.Id
                    $canonicalMarker.AssignedLocalIds.Add($local.Id)
                    foreach ($marker in $validNearestMarkers) {
                        $marker.ResolvedLowestRegenTieLocalIds.Add($local.Id)
                    }
                    continue
                }

                foreach ($marker in $nearestMarkers) {
                    $marker.AmbiguousLocalIds.Add($local.Id)
                }
                $local.ManualReason = 'Empate entre marcadores TibiAntis mais proximos.'
                continue
            }

            $local.AssignedMarkerId = $nearestMarkers[0].Id
            $nearestMarkers[0].AssignedLocalIds.Add($local.Id)
        }
    }

    $localById = @{}
    foreach ($local in $LocalRecords) {
        $localById[$local.Id] = $local
    }

    foreach ($marker in $Markers) {
        foreach ($localId in $marker.CandidateLocalIds) {
            $local = $localById[$localId]
            if ($null -ne $local.AssignedMarkerId -and $local.AssignedMarkerId -ne $marker.Id) {
                $marker.ConflictingLocalIds.Add($localId)
            }
        }
    }

    return [pscustomobject]@{
        LocalById = $localById
        MarkersByGroup = $markersByGroup
        LocalsByGroup = $localsByGroup
    }
}

function Classify-Markers {
    param(
        [System.Collections.Generic.List[object]]$Markers,
        [hashtable]$LocalById,
        [hashtable]$LocalsByGroup,
        [hashtable]$MarkerById,
        [ValidateSet('Strict', 'Relaxed')][string]$Policy
    )

    foreach ($marker in $Markers) {
        $hasValidRegen = $marker.Regen -ge $MinimumSpawnTime -and $marker.Regen -le $MaximumSpawnTime
        $hasSameCreatureAndFloorLocal = $LocalsByGroup.ContainsKey((Get-GroupKey $marker))

        foreach ($localId in $marker.ConflictingLocalIds) {
            $local = $LocalById[$localId]
            $assignedMarker = $MarkerById[$local.AssignedMarkerId]
            if ($Policy -eq 'Relaxed' -and $marker.ResolvedLowestRegenTieLocalIds.Contains($localId)) {
                continue
            } elseif ($Policy -eq 'Relaxed' -and $assignedMarker.Regen -eq $marker.Regen) {
                $marker.ResolvedSameRegenConflictLocalIds.Add($localId)
            } else {
                $marker.UnresolvedConflictLocalIds.Add($localId)
            }
        }

        if (-not $hasValidRegen) {
            $marker.Classification = 'InvalidRegen'
            $marker.ClassificationReason = "Regen fora da faixa aceita: $($marker.Regen)."
        } elseif ($marker.AmbiguousLocalIds.Count -gt 0) {
            $marker.Classification = 'Ambiguous'
            $marker.ClassificationReason = 'Empate de distancia entre marcadores proximos da mesma criatura.'
        } elseif ($marker.UnresolvedConflictLocalIds.Count -gt 0) {
            $marker.Classification = 'Ambiguous'
            $marker.ClassificationReason = 'Existem criaturas locais proximas atribuidas exclusivamente a outro marcador mais proximo.'
        } elseif (-not $hasSameCreatureAndFloorLocal) {
            $marker.Classification = 'MissingLocal'
            $marker.ClassificationReason = 'Nenhuma criatura local da mesma especie e andar.'
        } elseif ($Policy -eq 'Strict' -and $marker.CandidateLocalIds.Count -eq 0) {
            $marker.Classification = 'DistanceOver10'
            $marker.ClassificationReason = "A criatura local mais proxima da mesma especie e andar esta acima de $MaximumDistance tiles."
        } elseif ($Policy -eq 'Strict' -and $marker.AssignedLocalIds.Count -ne $marker.Count) {
            $marker.Classification = 'CountMismatch'
            $marker.ClassificationReason = 'Quantidade local atribuida difere do Count do TibiAntis.'
        } elseif ($marker.AssignedLocalIds.Count -eq 0) {
            if ($Policy -eq 'Relaxed' -and $marker.ResolvedLowestRegenTieLocalIds.Count -gt 0) {
                $marker.Classification = 'ResolvedTie'
                $marker.ClassificationReason = 'Empate de distancia resolvido pelo menor Regen valido entre os marcadores empatados.'
            } else {
                $marker.Classification = 'Unassigned'
                $marker.ClassificationReason = 'Nenhuma criatura local foi atribuida exclusivamente a este marcador.'
            }
        } else {
            $marker.Classification = 'Auto'
            if ($Policy -eq 'Relaxed') {
                $marker.ClassificationReason = 'Mesmo nome e andar com atribuicao exclusiva; quantidade e distancia foram aceitas pela politica Relaxed.'
            } else {
                $marker.ClassificationReason = 'Mesmo nome, andar, quantidade e atribuicao exclusiva dentro do raio permitido.'
            }
        }
    }
}

function Get-RebuildAssignments {
    param(
        [System.Collections.Generic.List[object]]$LocalRecords,
        [array]$DirectMarkers,
        [hashtable]$LocalById
    )

    $directTargetTimes = @{}
    $seedByGroup = @{}
    $seedRecords = [System.Collections.Generic.List[object]]::new()

    foreach ($marker in $DirectMarkers) {
        foreach ($localId in $marker.AssignedLocalIds) {
            $local = $LocalById[$localId]
            $directTime = if ($RebuildGlobalTimeOverrides.ContainsKey($local.Name)) { [int]$RebuildGlobalTimeOverrides[$local.Name] } else { [int]$marker.Regen }
            if ($directTargetTimes.ContainsKey($localId) -and $directTargetTimes[$localId] -ne $directTime) {
                throw "Criatura local $localId recebeu tempos diretos divergentes na reconstrucao."
            }

            $directTargetTimes[$localId] = $directTime
            $local.RebuildStatus = 'Direct'
            $local.RebuildTargetTime = $directTime
            $seed = [pscustomobject]@{
                LocalId = $localId
                MarkerId = $marker.Id
                Name = $local.Name
                X = $local.X
                Y = $local.Y
                Z = $local.Z
                TargetTime = $directTime
            }
            $seedRecords.Add($seed)
            Add-ToListMap $seedByGroup (Get-GroupKey $local) $seed
        }
    }

    $propagatedTargetTimes = @{}
    $propagatedRecords = [System.Collections.Generic.List[object]]::new()
    $suspiciousRecords = [System.Collections.Generic.List[object]]::new()
    $noReferenceRecords = [System.Collections.Generic.List[object]]::new()

    foreach ($local in $LocalRecords) {
        if ($directTargetTimes.ContainsKey($local.Id)) {
            continue
        }

        $groupKey = Get-GroupKey $local
        if (-not $seedByGroup.ContainsKey($groupKey)) {
            $local.RebuildStatus = 'NoReference'
            $local.ManualReason = 'Nenhuma referencia direta confiavel da mesma criatura e andar.'
            $noReferenceRecords.Add($local)
            continue
        }

        $references = @(
            $seedByGroup[$groupKey] |
                ForEach-Object {
                    $distance = Get-ChebyshevDistance $local.X $local.Y $_.X $_.Y
                    if ($distance -le $RebuildPropagationDistance) {
                        [pscustomobject]@{
                            Seed = $_
                            Distance = $distance
                        }
                    }
                }
        )

        if ($references.Count -eq 0) {
            $local.RebuildStatus = 'NoReference'
            $local.ManualReason = "Nenhuma referencia direta confiavel em ate $RebuildPropagationDistance tiles."
            $noReferenceRecords.Add($local)
            continue
        }

        foreach ($reference in $references) {
            $local.RebuildReferenceLocalIds.Add($reference.Seed.LocalId)
        }

        $minimumTime = ($references | ForEach-Object { $_.Seed.TargetTime } | Measure-Object -Minimum).Minimum
        $maximumTime = ($references | ForEach-Object { $_.Seed.TargetTime } | Measure-Object -Maximum).Maximum
        if ([int64]$maximumTime -gt ([int64]$minimumTime * 2)) {
            $local.RebuildStatus = 'SuspiciousTimeSpread'
            $local.ManualReason = "Referencias confiaveis na regiao divergem acima da razao 2x: minimo $minimumTime, maximo $maximumTime."
            $suspiciousRecords.Add($local)
            continue
        }

        $nearestDistance = ($references | Measure-Object Distance -Minimum).Minimum
        $nearestReferences = @($references | Where-Object Distance -eq $nearestDistance)
        $targetTime = ($nearestReferences | ForEach-Object { $_.Seed.TargetTime } | Measure-Object -Minimum).Minimum

        $local.RebuildStatus = 'Propagated'
        $local.RebuildTargetTime = [int]$targetTime
        $local.RebuildReferenceDistance = [int]$nearestDistance
        $propagatedTargetTimes[$local.Id] = [int]$targetTime
        $propagatedRecords.Add($local)
    }

    $globalOverrideTargetTimes = @{}
    $globalOverrideRecords = [System.Collections.Generic.List[object]]::new()
    foreach ($local in $LocalRecords) {
        if (-not $RebuildGlobalTimeOverrides.ContainsKey($local.Name)) {
            continue
        }

        $targetTime = [int]$RebuildGlobalTimeOverrides[$local.Name]
        $local.RebuildStatus = 'GlobalOverride'
        $local.RebuildTargetTime = $targetTime
        $globalOverrideTargetTimes[$local.Id] = $targetTime
        $globalOverrideRecords.Add($local)
        [void]$suspiciousRecords.Remove($local)
        [void]$noReferenceRecords.Remove($local)
        [void]$propagatedRecords.Remove($local)
        [void]$propagatedTargetTimes.Remove($local.Id)
    }

    $manualOverrideTargetTimes = @{}
    $manualOverrideRecords = [System.Collections.Generic.List[object]]::new()
    $manualResolvedRecords = [System.Collections.Generic.List[object]]::new()
    $unresolvedSnapshot = @($suspiciousRecords + $noReferenceRecords)
    foreach ($local in $unresolvedSnapshot) {
        if ($RebuildRemainingTimeOverrides.ContainsKey($local.Name)) {
            $targetTime = [int]$RebuildRemainingTimeOverrides[$local.Name]
            $local.RebuildStatus = 'ManualOverride'
            $local.RebuildTargetTime = $targetTime
            $manualOverrideTargetTimes[$local.Id] = $targetTime
            $manualOverrideRecords.Add($local)
            [void]$suspiciousRecords.Remove($local)
            [void]$noReferenceRecords.Remove($local)
        } elseif ($RebuildManualResolvedNames.Contains($local.Name)) {
            $local.RebuildStatus = 'ManualResolved'
            $local.ManualReason = 'Resolvido manualmente; spawntime atual preservado.'
            $manualResolvedRecords.Add($local)
            [void]$suspiciousRecords.Remove($local)
            [void]$noReferenceRecords.Remove($local)
        }
    }

    $minimumPolicyTargetTimes = @{}
    foreach ($local in $LocalRecords) {
        if ($RebuildMinimumSpawnTimeExemptNames.Contains($local.Name)) {
            continue
        }

        $plannedTime = $local.CurrentSpawnTime
        foreach ($targetMap in @($directTargetTimes, $propagatedTargetTimes, $globalOverrideTargetTimes, $manualOverrideTargetTimes)) {
            if ($targetMap.ContainsKey($local.Id)) {
                $plannedTime = [int]$targetMap[$local.Id]
            }
        }

        $minimumTime = if ($RebuildMinimumSpawnTimeOverrides.ContainsKey($local.Name)) {
            [int]$RebuildMinimumSpawnTimeOverrides[$local.Name]
        } else {
            $RebuildDefaultMinimumSpawnTime
        }
        if ($plannedTime -lt $minimumTime) {
            $minimumPolicyTargetTimes[$local.Id] = $minimumTime
        }
    }

    return [pscustomobject]@{
        DirectTargetTimes = $directTargetTimes
        PropagatedTargetTimes = $propagatedTargetTimes
        GlobalOverrideTargetTimes = $globalOverrideTargetTimes
        ManualOverrideTargetTimes = $manualOverrideTargetTimes
        MinimumPolicyTargetTimes = $minimumPolicyTargetTimes
        Seeds = $seedRecords
        Propagated = $propagatedRecords
        GlobalOverrides = $globalOverrideRecords
        ManualOverrides = $manualOverrideRecords
        ManualResolved = $manualResolvedRecords
        Suspicious = $suspiciousRecords
        NoReference = $noReferenceRecords
    }
}

function Get-NearestLocalCandidates {
    param(
        $Marker,
        [hashtable]$LocalsByGroup,
        [int]$Limit = 5
    )

    $groupKey = Get-GroupKey $Marker
    if (-not $LocalsByGroup.ContainsKey($groupKey)) {
        return @()
    }

    return @(
        $LocalsByGroup[$groupKey] |
            ForEach-Object {
                [pscustomobject]@{
                    Local = $_
                    Distance = Get-ChebyshevDistance $_.X $_.Y $Marker.X $Marker.Y
                }
            } |
            Sort-Object Distance, @{ Expression = { $_.Local.X } }, @{ Expression = { $_.Local.Y } } |
            Select-Object -First $Limit
    )
}

function Get-MarkerByIdMap {
    param([System.Collections.Generic.List[object]]$Markers)

    $map = @{}
    foreach ($marker in $Markers) {
        $map[$marker.Id] = $marker
    }
    return $map
}

function Format-Coordinate {
    param($Entry)
    return "$($Entry.X), $($Entry.Y), $($Entry.Z)"
}

function Add-Index {
    param(
        [System.Text.StringBuilder]$Builder,
        [array]$Entries,
        [string]$Title
    )

    [void]$Builder.AppendLine('## Indice')
    [void]$Builder.AppendLine()
    foreach ($group in ($Entries | Group-Object Name | Sort-Object Name)) {
        [void]$Builder.AppendLine("- $($group.Name) - $($group.Count) casos")
    }
    [void]$Builder.AppendLine()
}

function Write-MarkdownFile {
    param(
        [string]$Path,
        [System.Text.StringBuilder]$Builder
    )

    [System.IO.File]::WriteAllText($Path, $Builder.ToString(), [System.Text.UTF8Encoding]::new($false))
}

function Write-AutoAppliedReport {
    param(
        [string]$Path,
        [array]$AutomaticMarkers,
        [hashtable]$LocalById,
        [string]$Mode,
        [string]$Policy
    )

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine('# Spawns TibiAntis aplicados automaticamente')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("Execucao: **$Mode**.")
    [void]$builder.AppendLine()
    Add-Index $builder $AutomaticMarkers 'Indice'

    foreach ($group in ($AutomaticMarkers | Sort-Object Name, Z, X, Y | Group-Object Name)) {
        [void]$builder.AppendLine('<details>')
        [void]$builder.AppendLine("<summary>$($group.Name) - $($group.Count) regioes</summary>")
        [void]$builder.AppendLine()
        foreach ($marker in ($group.Group | Sort-Object Z, X, Y)) {
            [void]$builder.AppendLine("## $($marker.Name) - $(Format-Coordinate $marker)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine("- TibiAntis Count: $($marker.Count)")
            [void]$builder.AppendLine("- TibiAntis Regen: $($marker.Regen)")
            [void]$builder.AppendLine("- Quantidade local: $($marker.AssignedLocalIds.Count)")
            [void]$builder.AppendLine("- Status: aplicado automaticamente pela politica $Policy")
            if ($marker.ResolvedSameRegenConflictLocalIds.Count -gt 0) {
                [void]$builder.AppendLine("- Conflitos de proximidade aceitos por Regen identico: $($marker.ResolvedSameRegenConflictLocalIds.Count)")
            }
            if ($marker.ResolvedSameRegenTieLocalIds.Count -gt 0) {
                [void]$builder.AppendLine("- Empates de distancia aceitos por Regen identico: $($marker.ResolvedSameRegenTieLocalIds.Count)")
            }
            [void]$builder.AppendLine()
            [void]$builder.AppendLine('### Criaturas locais modificadas')
            [void]$builder.AppendLine()
            [void]$builder.AppendLine('| Coordenada local | Distancia Chebyshev | Spawntime anterior | Spawntime novo |')
            [void]$builder.AppendLine('|---|---:|---:|---:|')
            foreach ($localId in $marker.AssignedLocalIds) {
                $local = $LocalById[$localId]
                $distance = Get-ChebyshevDistance $local.X $local.Y $marker.X $marker.Y
                [void]$builder.AppendLine("| $(Format-Coordinate $local) | $distance | $($local.CurrentSpawnTime) | $($marker.Regen) |")
            }
            [void]$builder.AppendLine()
        }
        [void]$builder.AppendLine('</details>')
        [void]$builder.AppendLine()
    }

    Write-MarkdownFile $Path $builder
}

function Write-DistanceOver10Report {
    param(
        [string]$Path,
        [array]$Markers,
        [hashtable]$LocalsByGroup
    )

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine('# Spawns TibiAntis acima de 10 tiles')
    [void]$builder.AppendLine()
    Add-Index $builder $Markers 'Indice'
    foreach ($group in ($Markers | Sort-Object Name, Z, X, Y | Group-Object Name)) {
        [void]$builder.AppendLine('<details>')
        [void]$builder.AppendLine("<summary>$($group.Name) - $($group.Count) casos</summary>")
        [void]$builder.AppendLine()
        foreach ($marker in ($group.Group | Sort-Object Z, X, Y)) {
            [void]$builder.AppendLine("## $($marker.Name) - $(Format-Coordinate $marker)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine("- TibiAntis Count: $($marker.Count)")
            [void]$builder.AppendLine("- TibiAntis Regen: $($marker.Regen)")
            [void]$builder.AppendLine("- Motivo: $($marker.ClassificationReason)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine('| Possivel criatura local | Coordenada | Distancia Chebyshev | Spawntime atual |')
            [void]$builder.AppendLine('|---|---|---:|---:|')
            foreach ($candidate in (Get-NearestLocalCandidates $marker $LocalsByGroup)) {
                [void]$builder.AppendLine("| $($candidate.Local.OriginalName) | $(Format-Coordinate $candidate.Local) | $($candidate.Distance) | $($candidate.Local.CurrentSpawnTime) |")
            }
            [void]$builder.AppendLine()
        }
        [void]$builder.AppendLine('</details>')
        [void]$builder.AppendLine()
    }
    Write-MarkdownFile $Path $builder
}

function Write-MissingLocalReport {
    param(
        [string]$Path,
        [array]$Markers
    )

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine('# Spawns TibiAntis sem correspondencia local segura')
    [void]$builder.AppendLine()
    Add-Index $builder $Markers 'Indice'
    foreach ($group in ($Markers | Sort-Object Name, Z, X, Y | Group-Object Name)) {
        [void]$builder.AppendLine('<details>')
        [void]$builder.AppendLine("<summary>$($group.Name) - $($group.Count) casos</summary>")
        [void]$builder.AppendLine()
        foreach ($marker in ($group.Group | Sort-Object Z, X, Y)) {
            [void]$builder.AppendLine("## $($marker.Name) - $(Format-Coordinate $marker)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine("- Andar: $($marker.Z)")
            [void]$builder.AppendLine("- TibiAntis Count: $($marker.Count)")
            [void]$builder.AppendLine("- TibiAntis Regen: $($marker.Regen)")
            [void]$builder.AppendLine("- Motivo: $($marker.ClassificationReason)")
            [void]$builder.AppendLine()
        }
        [void]$builder.AppendLine('</details>')
        [void]$builder.AppendLine()
    }
    Write-MarkdownFile $Path $builder
}

function Write-CountMismatchReport {
    param(
        [string]$Path,
        [array]$Markers,
        [hashtable]$LocalById
    )

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine('# Regioes com quantidade divergente')
    [void]$builder.AppendLine()
    Add-Index $builder $Markers 'Indice'
    foreach ($group in ($Markers | Sort-Object Name, Z, X, Y | Group-Object Name)) {
        [void]$builder.AppendLine('<details>')
        [void]$builder.AppendLine("<summary>$($group.Name) - $($group.Count) casos</summary>")
        [void]$builder.AppendLine()
        foreach ($marker in ($group.Group | Sort-Object Z, X, Y)) {
            [void]$builder.AppendLine("## $($marker.Name) - $(Format-Coordinate $marker)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine("- Count esperado: $($marker.Count)")
            [void]$builder.AppendLine("- Quantidade local atribuida: $($marker.AssignedLocalIds.Count)")
            [void]$builder.AppendLine("- Diferenca: $($marker.AssignedLocalIds.Count - $marker.Count)")
            [void]$builder.AppendLine("- Regen: $($marker.Regen)")
            [void]$builder.AppendLine("- Motivo: $($marker.ClassificationReason)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine('| Coordenada local atribuida | Distancia Chebyshev | Spawntime atual |')
            [void]$builder.AppendLine('|---|---:|---:|')
            foreach ($localId in $marker.AssignedLocalIds) {
                $local = $LocalById[$localId]
                $distance = Get-ChebyshevDistance $local.X $local.Y $marker.X $marker.Y
                [void]$builder.AppendLine("| $(Format-Coordinate $local) | $distance | $($local.CurrentSpawnTime) |")
            }
            [void]$builder.AppendLine()
        }
        [void]$builder.AppendLine('</details>')
        [void]$builder.AppendLine()
    }
    Write-MarkdownFile $Path $builder
}

function Write-AmbiguousReport {
    param(
        [string]$Path,
        [array]$Markers,
        [hashtable]$LocalById,
        [hashtable]$MarkerById
    )

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine('# Associacoes ambiguas ou conflitantes')
    [void]$builder.AppendLine()
    Add-Index $builder $Markers 'Indice'
    foreach ($group in ($Markers | Sort-Object Name, Z, X, Y | Group-Object Name)) {
        [void]$builder.AppendLine('<details>')
        [void]$builder.AppendLine("<summary>$($group.Name) - $($group.Count) casos</summary>")
        [void]$builder.AppendLine()
        foreach ($marker in ($group.Group | Sort-Object Z, X, Y)) {
            [void]$builder.AppendLine("## $($marker.Name) - $(Format-Coordinate $marker)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine("- Count TibiAntis: $($marker.Count)")
            [void]$builder.AppendLine("- Regen TibiAntis: $($marker.Regen)")
            [void]$builder.AppendLine("- Motivo: $($marker.ClassificationReason)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine('| Criatura local | Coordenada | Distancia ao marcador | Marcador atribuido |')
            [void]$builder.AppendLine('|---|---|---:|---|')
            $allLocalIds = @($marker.AmbiguousLocalIds + $marker.UnresolvedConflictLocalIds | Sort-Object -Unique)
            foreach ($localId in $allLocalIds) {
                $local = $LocalById[$localId]
                $distance = Get-ChebyshevDistance $local.X $local.Y $marker.X $marker.Y
                $assigned = if ($null -eq $local.AssignedMarkerId) { 'nenhum (empate)' } else { "$(Format-Coordinate $MarkerById[$local.AssignedMarkerId])" }
                [void]$builder.AppendLine("| $($local.OriginalName) | $(Format-Coordinate $local) | $distance | $assigned |")
            }
            [void]$builder.AppendLine()
        }
        [void]$builder.AppendLine('</details>')
        [void]$builder.AppendLine()
    }
    Write-MarkdownFile $Path $builder
}

function Write-MissingReferenceReport {
    param(
        [string]$Path,
        [array]$LocalRecords,
        [hashtable]$MarkerById
    )

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine('# Criaturas locais sem marcador TibiAntis seguro')
    [void]$builder.AppendLine()
    Add-Index $builder $LocalRecords 'Indice'
    foreach ($group in ($LocalRecords | Sort-Object Name, Z, X, Y | Group-Object Name)) {
        [void]$builder.AppendLine('<details>')
        [void]$builder.AppendLine("<summary>$($group.Name) - $($group.Count) casos</summary>")
        [void]$builder.AppendLine()
        foreach ($local in ($group.Group | Sort-Object Z, X, Y)) {
            [void]$builder.AppendLine("## $($local.OriginalName) - $(Format-Coordinate $local)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine("- Spawntime atual: $($local.CurrentSpawnTime)")
            $nearestMarkers = @($local.NearestMarkerIds | ForEach-Object { $MarkerById[$_] } | Where-Object Classification -ne 'InvalidRegen')
            if ($nearestMarkers.Count -gt 0) {
                $nearest = $nearestMarkers[0]
                [void]$builder.AppendLine("- Marcador mais proximo: $(Format-Coordinate $nearest) (Count $($nearest.Count), Regen $($nearest.Regen))")
                [void]$builder.AppendLine("- Distancia Chebyshev: $($local.NearestDistance)")
                [void]$builder.AppendLine("- Status do marcador: $($nearest.Classification)")
            } else {
                [void]$builder.AppendLine('- Marcador mais proximo: inexistente para a mesma criatura e andar')
            }
            $reason = if ([string]::IsNullOrWhiteSpace($local.ManualReason)) { 'O marcador associado nao foi elegivel para aplicacao automatica.' } else { $local.ManualReason }
            [void]$builder.AppendLine("- Motivo: $reason")
            [void]$builder.AppendLine()
        }
        [void]$builder.AppendLine('</details>')
        [void]$builder.AppendLine()
    }
    Write-MarkdownFile $Path $builder
}

function Write-RemainingReport {
    param(
        [string]$Path,
        [array]$Markers,
        [hashtable]$LocalById,
        [hashtable]$LocalsByGroup
    )

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine('# Spawns restantes apos a importacao')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('Este arquivo contem somente marcadores que permaneceram sem aplicacao automatica.')
    [void]$builder.AppendLine()
    Add-Index $builder $Markers 'Indice'
    foreach ($group in ($Markers | Sort-Object Name, Z, X, Y | Group-Object Name)) {
        [void]$builder.AppendLine('<details>')
        [void]$builder.AppendLine("<summary>$($group.Name) - $($group.Count) casos</summary>")
        [void]$builder.AppendLine()
        foreach ($marker in ($group.Group | Sort-Object Z, X, Y)) {
            [void]$builder.AppendLine("## $($marker.Name) - $(Format-Coordinate $marker)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine("- Status: $($marker.Classification)")
            [void]$builder.AppendLine("- Motivo: $($marker.ClassificationReason)")
            [void]$builder.AppendLine("- Count TibiAntis: $($marker.Count)")
            [void]$builder.AppendLine("- Regen TibiAntis: $($marker.Regen)")
            [void]$builder.AppendLine("- Criaturas locais atribuidas: $($marker.AssignedLocalIds.Count)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine('| Possivel criatura local | Coordenada | Distancia Chebyshev | Spawntime atual |')
            [void]$builder.AppendLine('|---|---|---:|---:|')
            $candidates = @()
            foreach ($localId in $marker.AssignedLocalIds) {
                $local = $LocalById[$localId]
                $candidates += [pscustomobject]@{ Local = $local; Distance = (Get-ChebyshevDistance $local.X $local.Y $marker.X $marker.Y) }
            }
            if ($candidates.Count -eq 0) {
                $candidates = Get-NearestLocalCandidates $marker $LocalsByGroup
            }
            foreach ($candidate in $candidates | Sort-Object Distance, @{ Expression = { $_.Local.X } }, @{ Expression = { $_.Local.Y } }) {
                [void]$builder.AppendLine("| $($candidate.Local.OriginalName) | $(Format-Coordinate $candidate.Local) | $($candidate.Distance) | $($candidate.Local.CurrentSpawnTime) |")
            }
            [void]$builder.AppendLine()
        }
        [void]$builder.AppendLine('</details>')
        [void]$builder.AppendLine()
    }
    Write-MarkdownFile $Path $builder
}

function Write-RebuildPropagationReport {
    param(
        [string]$Path,
        $Rebuild,
        [hashtable]$LocalById
    )

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine('# Reconstrucao por referencias locais confiaveis')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("- Referencias diretas congeladas: $($Rebuild.Seeds.Count)")
    [void]$builder.AppendLine("- Criaturas propagadas em ate $RebuildPropagationDistance tiles: $($Rebuild.Propagated.Count)")
    [void]$builder.AppendLine('- Referencias propagadas nesta execucao nao foram reutilizadas como sementes.')
    [void]$builder.AppendLine()
    Add-Index $builder $Rebuild.Propagated 'Indice'

    foreach ($group in ($Rebuild.Propagated | Sort-Object Name, Z, X, Y | Group-Object Name)) {
        [void]$builder.AppendLine('<details>')
        [void]$builder.AppendLine("<summary>$($group.Name) - $($group.Count) casos</summary>")
        [void]$builder.AppendLine()
        [void]$builder.AppendLine('| Criatura local | Spawntime anterior | Novo spawntime | Distancia vencedora | Referencias confiaveis no raio |')
        [void]$builder.AppendLine('|---|---:|---:|---:|---|')
        foreach ($local in ($group.Group | Sort-Object Z, X, Y)) {
            $referenceDescriptions = @()
            foreach ($referenceId in $local.RebuildReferenceLocalIds) {
                $reference = $LocalById[$referenceId]
                $distance = Get-ChebyshevDistance $local.X $local.Y $reference.X $reference.Y
                $referenceDescriptions += "$(Format-Coordinate $reference)=$($reference.RebuildTargetTime)s (d$distance)"
            }
            [void]$builder.AppendLine("| $(Format-Coordinate $local) | $($local.CurrentSpawnTime) | $($local.RebuildTargetTime) | $($local.RebuildReferenceDistance) | $($referenceDescriptions -join '; ') |")
        }
        [void]$builder.AppendLine()
        [void]$builder.AppendLine('</details>')
        [void]$builder.AppendLine()
    }

    Write-MarkdownFile $Path $builder
}

function Write-RebuildRemainingReport {
    param(
        [string]$Path,
        $Rebuild,
        [hashtable]$LocalById
    )

    $remaining = @($Rebuild.Suspicious + $Rebuild.NoReference | Sort-Object Name, Z, X, Y)
    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine('# Criaturas locais restantes apos a reconstrucao')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("- Divergencia regional acima de 2x: $($Rebuild.Suspicious.Count)")
    [void]$builder.AppendLine("- Sem referencia direta confiavel em ate $RebuildPropagationDistance tiles: $($Rebuild.NoReference.Count)")
    [void]$builder.AppendLine()
    Add-Index $builder $remaining 'Indice'

    foreach ($group in ($remaining | Group-Object Name | Sort-Object Name)) {
        [void]$builder.AppendLine('<details>')
        [void]$builder.AppendLine("<summary>$($group.Name) - $($group.Count) casos</summary>")
        [void]$builder.AppendLine()
        foreach ($local in ($group.Group | Sort-Object Z, X, Y)) {
            [void]$builder.AppendLine("## $($local.OriginalName) - $(Format-Coordinate $local)")
            [void]$builder.AppendLine()
            [void]$builder.AppendLine("- Status: $($local.RebuildStatus)")
            [void]$builder.AppendLine("- Spawntime atual: $($local.CurrentSpawnTime)")
            [void]$builder.AppendLine("- Motivo: $($local.ManualReason)")
            if ($local.RebuildReferenceLocalIds.Count -gt 0) {
                [void]$builder.AppendLine()
                [void]$builder.AppendLine('| Referencia direta confiavel | Distancia Chebyshev | Tempo |')
                [void]$builder.AppendLine('|---|---:|---:|')
                foreach ($referenceId in $local.RebuildReferenceLocalIds) {
                    $reference = $LocalById[$referenceId]
                    $distance = Get-ChebyshevDistance $local.X $local.Y $reference.X $reference.Y
                    [void]$builder.AppendLine("| $(Format-Coordinate $reference) | $distance | $($reference.RebuildTargetTime) |")
                }
            }
            [void]$builder.AppendLine()
        }
        [void]$builder.AppendLine('</details>')
        [void]$builder.AppendLine()
    }

    Write-MarkdownFile $Path $builder
}

function Write-RebuildManualRulesReport {
    param(
        [string]$Path,
        $Rebuild
    )

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine('# Regras manuais aplicadas na reconstrucao')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("- Overrides globais: $($Rebuild.GlobalOverrides.Count)")
    [void]$builder.AppendLine("- Overrides aplicados somente aos pontos restantes: $($Rebuild.ManualOverrides.Count)")
    [void]$builder.AppendLine("- Pontos encerrados preservando o ajuste manual: $($Rebuild.ManualResolved.Count)")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Tipo | Criatura | Coordenada | Tempo anterior | Tempo final |')
    [void]$builder.AppendLine('|---|---|---|---:|---:|')
    foreach ($local in ($Rebuild.GlobalOverrides | Sort-Object Name, Z, X, Y)) {
        [void]$builder.AppendLine("| GlobalOverride | $($local.OriginalName) | $(Format-Coordinate $local) | $($local.CurrentSpawnTime) | $($local.RebuildTargetTime) |")
    }
    foreach ($local in ($Rebuild.ManualOverrides | Sort-Object Name, Z, X, Y)) {
        [void]$builder.AppendLine("| ManualOverride | $($local.OriginalName) | $(Format-Coordinate $local) | $($local.CurrentSpawnTime) | $($local.RebuildTargetTime) |")
    }
    foreach ($local in ($Rebuild.ManualResolved | Sort-Object Name, Z, X, Y)) {
        [void]$builder.AppendLine("| ManualResolved | $($local.OriginalName) | $(Format-Coordinate $local) | $($local.CurrentSpawnTime) | $($local.CurrentSpawnTime) |")
    }
    [void]$builder.AppendLine()
    Write-MarkdownFile $Path $builder
}

function Get-XmlSignature {
    param([System.Collections.Generic.List[object]]$LocalRecords)

    $builder = [System.Text.StringBuilder]::new()
    foreach ($record in $LocalRecords) {
        [void]$builder.AppendLine("$($record.Id)|$($record.OriginalName)|$($record.X)|$($record.Y)|$($record.Z)")
    }
    [byte[]]$bytes = [System.Text.Encoding]::UTF8.GetBytes($builder.ToString())
    return [System.BitConverter]::ToString([System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)).Replace('-', '')
}

function Apply-SpawnTimeUpdates {
    param(
        [string]$XmlText,
        [hashtable]$Updates,
        [hashtable]$LocalById
    )

    $builder = [System.Text.StringBuilder]::new($XmlText)
    $patches = @()
    foreach ($localId in $Updates.Keys) {
        $local = $LocalById[$localId]
        $patches += [pscustomobject]@{
            Index = $local.SpawnTimeValueIndex
            Length = $local.SpawnTimeValueLength
            Value = [string]$Updates[$localId]
        }
    }

    foreach ($patch in ($patches | Sort-Object Index -Descending)) {
        [void]$builder.Remove($patch.Index, $patch.Length)
        [void]$builder.Insert($patch.Index, $patch.Value)
    }
    return $builder.ToString()
}

function Write-SummaryReport {
    param(
        [string]$Path,
        [string]$Mode,
        [string]$Policy,
        [string]$XmlPath,
        [string]$SourceHash,
        [System.Collections.Generic.List[object]]$Markers,
        [System.Collections.Generic.List[object]]$LocalRecords,
        [array]$AutomaticMarkers,
        [hashtable]$Updates,
        [hashtable]$CategoryCounts,
        [hashtable]$Validation,
        [string]$ReportDirectory
    )

    $builder = [System.Text.StringBuilder]::new()
    $totalTibiantisCount = ($Markers | Measure-Object Count -Sum).Sum
    $automaticLocalCount = ($AutomaticMarkers | ForEach-Object { $_.AssignedLocalIds.Count } | Measure-Object -Sum).Sum
    $regenValues = @($AutomaticMarkers | ForEach-Object Regen)
    $minimumApplied = if ($regenValues.Count -gt 0) { ($regenValues | Measure-Object -Minimum).Minimum } else { 'n/a' }
    $maximumApplied = if ($regenValues.Count -gt 0) { ($regenValues | Measure-Object -Maximum).Maximum } else { 'n/a' }

    [void]$builder.AppendLine('# Auditoria de importacao de spawns do TibiAntis')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("- Execucao: **$Mode**")
    [void]$builder.AppendLine("- Politica de associacao: **$Policy**")
    [void]$builder.AppendLine("- XML ativo: $XmlPath")
    [void]$builder.AppendLine("- Fonte TibiAntis: $TibiantisUrl")
    [void]$builder.AppendLine("- SHA-256 da resposta da fonte: $SourceHash")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Metodologia')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('- Nome normalizado: minusculas, underscore e hifen convertidos para espaco e espacos repetidos removidos.')
    [void]$builder.AppendLine('- Associacao somente entre a mesma criatura e o mesmo andar z.')
    [void]$builder.AppendLine("- Distancia: Chebyshev, max(abs(x1-x2), abs(y1-y2)); referencia de proximidade de $MaximumDistance tiles.")
    if ($Policy -eq 'Strict') {
        [void]$builder.AppendLine('- Cada criatura local recebe somente o marcador mais proximo; empates e grupos disputados sao classificados como ambiguos.')
        [void]$builder.AppendLine("- Politica Strict: aplicacao somente quando a quantidade atribuida e igual ao Count e a distancia esta dentro de $MaximumDistance tiles.")
    } elseif ($Policy -eq 'Rebuild') {
        [void]$builder.AppendLine('- Politica Rebuild: as referencias diretas sao recalculadas pela politica Relaxed ja aceita nas importacoes anteriores e congeladas antes da propagacao.')
        [void]$builder.AppendLine("- Os pontos que ainda nao vieram do TibiAntis usam somente essas referencias comprovadas da mesma criatura e andar em ate $RebuildPropagationDistance tiles.")
        [void]$builder.AppendLine('- A menor distancia vence; em empate de distancia, vence o menor tempo.')
        [void]$builder.AppendLine('- Se o maior tempo regional for maior que o menor tempo multiplicado por 2, o ponto permanece para analise.')
        [void]$builder.AppendLine('- O spawntime atual nao participa da decisao e referencias propagadas nao geram propagacao em cadeia.')
    } else {
        [void]$builder.AppendLine('- Cada criatura local recebe somente o marcador mais proximo; empates e grupos disputados permanecem ambiguos, exceto quando todos os marcadores empatados possuem o mesmo Regen valido.')
        [void]$builder.AppendLine('- Politica Relaxed: diferencas de quantidade e distancia nao bloqueiam a aplicacao. Conflitos e empates de proximidade tambem sao aceitos quando todos os marcadores empatados ou o marcador que recebeu a criatura local possuem o mesmo Regen valido.')
    }
    [void]$builder.AppendLine("- Todo Regen aplicado permanece entre $MinimumSpawnTime e $MaximumSpawnTime segundos.")
    [void]$builder.AppendLine('- A edicao substitui somente os digitos do atributo spawntime, sem reserializar o XML.')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Totais')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Item | Quantidade |')
    [void]$builder.AppendLine('|---|---:|')
    [void]$builder.AppendLine("| Marcadores TibiAntis | $($Markers.Count) |")
    [void]$builder.AppendLine("| Criaturas representadas pelo Count TibiAntis | $totalTibiantisCount |")
    [void]$builder.AppendLine("| Pontos locais monster | $($LocalRecords.Count) |")
    [void]$builder.AppendLine("| Regioes automaticas | $($AutomaticMarkers.Count) |")
    [void]$builder.AppendLine("| Criaturas locais elegiveis | $automaticLocalCount |")
    [void]$builder.AppendLine("| Atributos spawntime efetivamente alterados | $($Updates.Count) |")
    [void]$builder.AppendLine("| Acima de $MaximumDistance tiles | $($CategoryCounts['DistanceOver10']) |")
    [void]$builder.AppendLine("| Sem local correspondente | $($CategoryCounts['MissingLocal']) |")
    [void]$builder.AppendLine("| Quantidade divergente | $($CategoryCounts['CountMismatch']) |")
    [void]$builder.AppendLine("| Ambiguos ou conflitantes | $($CategoryCounts['Ambiguous']) |")
    [void]$builder.AppendLine("| Sem criatura atribuida exclusivamente | $($CategoryCounts['Unassigned']) |")
    [void]$builder.AppendLine("| Pontos locais sem aplicacao segura | $($Validation['ManualLocalCount']) |")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Regens aplicados')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("- Menor Regen aplicado: **$minimumApplied** segundos")
    [void]$builder.AppendLine("- Maior Regen aplicado: **$maximumApplied** segundos")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Regen | Criaturas locais elegiveis | Atributos alterados nesta execucao | Regioes |')
    [void]$builder.AppendLine('|---:|---:|---:|---:|')
    foreach ($group in ($AutomaticMarkers | Group-Object Regen | Sort-Object { [int]$_.Name })) {
        $localCount = ($group.Group | ForEach-Object { $_.AssignedLocalIds.Count } | Measure-Object -Sum).Sum
        $changedCount = @($Updates.Values | Where-Object { $_ -eq [int]$group.Name }).Count
        [void]$builder.AppendLine("| $($group.Name) | $localCount | $changedCount | $($group.Count) |")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Validacoes')
    [void]$builder.AppendLine()
    foreach ($key in $Validation.Keys | Sort-Object) {
        [void]$builder.AppendLine("- $($key): $($Validation[$key])")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Limitacoes')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('- O TibiAntis representa grupos por marcador e Count; o XML local guarda cada criatura como um slot independente.')
    if ($Policy -eq 'Strict') {
        [void]$builder.AppendLine('- Correspondencias acima de 10 tiles, disputadas, com empate ou quantidade diferente permanecem inalteradas.')
    } elseif ($Policy -eq 'Rebuild') {
        [void]$builder.AppendLine('- Pontos sem referencia direta em 17 tiles e regioes com divergencia acima de 2x permanecem para analise.')
    } else {
        [void]$builder.AppendLine('- Na politica Relaxed, distancia e quantidade podem divergir. Permanecem inalterados os empates ou conflitos com Regen diferente, ausencia de local e falta de atribuicao exclusiva.')
    }
    [void]$builder.AppendLine('- O resultado e idempotente para as mesmas fontes: uma nova execucao mantem a classificacao e nao altera atributos ja iguais ao Regen aprovado.')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Relatorios detalhados')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('- [Regioes aplicadas automaticamente](spawn_tibiantis_auto_applied.md)')
    [void]$builder.AppendLine('- [Possiveis correspondencias acima de 10 tiles](spawn_tibiantis_distance_over_10.md)')
    [void]$builder.AppendLine('- [Marcadores sem correspondencia local segura](spawn_tibiantis_missing_local.md)')
    [void]$builder.AppendLine('- [Pontos locais sem marcador seguro](spawn_tibiantis_missing_reference.md)')
    [void]$builder.AppendLine('- [Regioes com quantidade divergente](spawn_tibiantis_count_mismatch.md)')
    [void]$builder.AppendLine('- [Associacoes ambiguas ou conflitantes](spawn_tibiantis_ambiguous.md)')
    if ($Policy -eq 'Rebuild') {
        [void]$builder.AppendLine('- [Criaturas locais restantes para analise](spawn_tibiantis_remaining.md)')
        [void]$builder.AppendLine('- [Criaturas reconstruidas por referencias locais](spawn_tibiantis_rebuild_propagated.md)')
        [void]$builder.AppendLine('- [Overrides e resolucoes manuais](spawn_tibiantis_rebuild_manual_rules.md)')
    } else {
        [void]$builder.AppendLine('- [Todos os marcadores restantes sem aplicacao](spawn_tibiantis_remaining.md)')
    }
    if ($Mode -eq 'DryRun') {
        [void]$builder.AppendLine('- [Registro desta simulacao](spawn_tibiantis_dry_run.md)')
    }

    Write-MarkdownFile $Path $builder
}

if (-not (Test-Path -LiteralPath $XmlPath)) {
    throw "Arquivo XML ativo nao encontrado: $XmlPath"
}

New-Item -ItemType Directory -Force -Path $ReportDirectory | Out-Null

$sourceFile = Get-FileTextPreservingEncoding $XmlPath
$localRecords = Get-LocalMonsterRecords $sourceFile.Text
$beforeSignature = Get-XmlSignature $localRecords
$tibiantis = Get-TibiantisData $TibiantisUrl
$markers = Get-TibiantisMarkers $tibiantis.Data
$classificationPolicy = if ($Policy -eq 'Rebuild') { 'Relaxed' } else { $Policy }
$association = Associate-LocalMonsters $localRecords $markers ($classificationPolicy -eq 'Relaxed')
$localById = $association.LocalById
$markerById = Get-MarkerByIdMap $markers
Classify-Markers $markers $localById $association.LocalsByGroup $markerById $classificationPolicy

$automaticMarkers = @($markers | Where-Object Classification -eq 'Auto' | Sort-Object Name, Z, X, Y)
$resolvedSameRegenConflictCount = ($markers | ForEach-Object { $_.ResolvedSameRegenConflictLocalIds.Count } | Measure-Object -Sum).Sum
$resolvedSameRegenTieCount = ($markers | ForEach-Object { $_.ResolvedSameRegenTieLocalIds.Count } | Measure-Object -Sum).Sum
$categoryCounts = @{}
foreach ($category in 'DistanceOver10', 'MissingLocal', 'CountMismatch', 'Ambiguous', 'InvalidRegen', 'Unassigned') {
    $categoryCounts[$category] = @($markers | Where-Object Classification -eq $category).Count
}

$updates = @{}
$automaticLocalIds = @{}
$duplicateAssignments = [System.Collections.Generic.List[string]]::new()
foreach ($marker in $automaticMarkers) {
    foreach ($localId in $marker.AssignedLocalIds) {
        $automaticLocalIds[$localId] = $true
        if ($updates.ContainsKey($localId) -and $updates[$localId] -ne $marker.Regen) {
            $duplicateAssignments.Add("Local $localId recebeu mais de um Regen: $($updates[$localId]) e $($marker.Regen).")
            continue
        }
        if ($localById[$localId].CurrentSpawnTime -ne $marker.Regen) {
            $updates[$localId] = $marker.Regen
        }
    }
}

if ($duplicateAssignments.Count -gt 0) {
    throw "Conflito de atribuicao detectado: $($duplicateAssignments -join ' ' )"
}

$rebuild = $null
if ($Policy -eq 'Rebuild') {
    $rebuild = Get-RebuildAssignments $localRecords $automaticMarkers $localById
    foreach ($targetMap in @($rebuild.DirectTargetTimes, $rebuild.PropagatedTargetTimes, $rebuild.GlobalOverrideTargetTimes, $rebuild.ManualOverrideTargetTimes, $rebuild.MinimumPolicyTargetTimes)) {
        foreach ($localId in $targetMap.Keys) {
            $targetTime = [int]$targetMap[$localId]
            if ($localById[$localId].CurrentSpawnTime -ne $targetTime) {
                $updates[$localId] = $targetTime
            } else {
                [void]$updates.Remove($localId)
            }
        }
    }
}

$manualLocals = if ($Policy -eq 'Rebuild') {
    @($rebuild.Suspicious + $rebuild.NoReference)
} else {
    @($localRecords | Where-Object { -not $automaticLocalIds.ContainsKey($_.Id) })
}
foreach ($local in $manualLocals) {
    if ([string]::IsNullOrWhiteSpace($local.ManualReason) -and $null -ne $local.AssignedMarkerId) {
        $local.ManualReason = $markerById[$local.AssignedMarkerId].ClassificationReason
    }
}

$reportableManualLocals = @(
    $manualLocals | Where-Object {
        if ($_.NearestMarkerIds.Count -eq 0) {
            return $true
        }

        @($_.NearestMarkerIds | ForEach-Object { $markerById[$_] } | Where-Object Classification -ne 'InvalidRegen').Count -gt 0
    }
)

$automaticDistanceViolations = 0
foreach ($marker in $automaticMarkers) {
    foreach ($localId in $marker.AssignedLocalIds) {
        $local = $localById[$localId]
        if ((Get-ChebyshevDistance $local.X $local.Y $marker.X $marker.Y) -gt $MaximumDistance) {
            $automaticDistanceViolations++
        }
    }
}

$validation = @{
    'Assinaturas de nome/coordenada antes da aplicacao' = $beforeSignature
    'Conflitos de dupla atribuicao' = $duplicateAssignments.Count
    'Conflitos de proximidade aceitos por Regen identico' = $resolvedSameRegenConflictCount
    'Empates de distancia aceitos por Regen identico' = $resolvedSameRegenTieCount
    'Violacoes de distancia em regioes automaticas' = $automaticDistanceViolations
    'ManualLocalCount' = $reportableManualLocals.Count
    'Mudancas planejadas' = $updates.Count
}
if ($Policy -eq 'Strict') {
    $validation['Todas as regioes automaticas dentro de 10 tiles'] = ($automaticDistanceViolations -eq 0)
} elseif ($Policy -eq 'Rebuild') {
    $validation['Referencias diretas acima de 10 tiles herdadas da politica aceita'] = $automaticDistanceViolations
} else {
    $validation['Regioes automaticas acima de 10 tiles aceitas pela politica Relaxed'] = $automaticDistanceViolations
}
if ($Policy -eq 'Rebuild') {
    $validation['Referencias diretas congeladas'] = $rebuild.Seeds.Count
    $validation['Criaturas reconstruidas por propagacao'] = $rebuild.Propagated.Count
    $validation['Overrides globais aplicados'] = $rebuild.GlobalOverrides.Count
    $validation['Overrides manuais aplicados ao relatorio'] = $rebuild.ManualOverrides.Count
    $validation['Pontos encerrados por ajuste manual'] = $rebuild.ManualResolved.Count
    $validation['Divergencias regionais acima de 2x'] = $rebuild.Suspicious.Count
    $validation['Criaturas sem referencia direta em 17 tiles'] = $rebuild.NoReference.Count
    $validation['Propagacao em cadeia'] = $false
}

$autoReport = Join-Path $ReportDirectory 'spawn_tibiantis_auto_applied.md'
$distanceReport = Join-Path $ReportDirectory 'spawn_tibiantis_distance_over_10.md'
$missingLocalReport = Join-Path $ReportDirectory 'spawn_tibiantis_missing_local.md'
$missingReferenceReport = Join-Path $ReportDirectory 'spawn_tibiantis_missing_reference.md'
$countMismatchReport = Join-Path $ReportDirectory 'spawn_tibiantis_count_mismatch.md'
$ambiguousReport = Join-Path $ReportDirectory 'spawn_tibiantis_ambiguous.md'
$remainingReport = Join-Path $ReportDirectory 'spawn_tibiantis_remaining.md'
$summaryReport = Join-Path $ReportDirectory 'spawn_tibiantis_summary.md'

Write-AutoAppliedReport $autoReport $automaticMarkers $localById $Mode $Policy
Write-DistanceOver10Report $distanceReport @($markers | Where-Object Classification -eq 'DistanceOver10') $association.LocalsByGroup
Write-MissingLocalReport $missingLocalReport @($markers | Where-Object Classification -eq 'MissingLocal')
Write-CountMismatchReport $countMismatchReport @($markers | Where-Object Classification -eq 'CountMismatch') $localById
Write-AmbiguousReport $ambiguousReport @($markers | Where-Object Classification -eq 'Ambiguous') $localById $markerById
Write-MissingReferenceReport $missingReferenceReport $reportableManualLocals $markerById
if ($Policy -eq 'Rebuild') {
    $propagationReport = Join-Path $ReportDirectory 'spawn_tibiantis_rebuild_propagated.md'
    $manualRulesReport = Join-Path $ReportDirectory 'spawn_tibiantis_rebuild_manual_rules.md'
    Write-RebuildPropagationReport $propagationReport $rebuild $localById
    Write-RebuildManualRulesReport $manualRulesReport $rebuild
    Write-RebuildRemainingReport $remainingReport $rebuild $localById
} else {
    $remainingMarkers = @($markers | Where-Object {
        $_.Classification -ne 'Auto' -and
        $_.Classification -ne 'InvalidRegen' -and
        $_.Classification -ne 'ResolvedTie' -and
        -not $RemainingReportExcludedNames.Contains($_.Name)
    })
    Write-RemainingReport $remainingReport $remainingMarkers $localById $association.LocalsByGroup
}

if ($Mode -eq 'Apply' -and $updates.Count -gt 0) {
    $updatedText = Apply-SpawnTimeUpdates $sourceFile.Text $updates $localById
    Write-FileTextPreservingEncoding $XmlPath $updatedText $sourceFile.Encoding $sourceFile.Preamble

    $afterFile = Get-FileTextPreservingEncoding $XmlPath
    [xml]$validatedXml = $afterFile.Text
    $afterRecords = Get-LocalMonsterRecords $afterFile.Text
    $afterSignature = Get-XmlSignature $afterRecords
    $afterById = @{}
    foreach ($record in $afterRecords) {
        $afterById[$record.Id] = $record
    }

    $unexpectedChanges = 0
    foreach ($beforeRecord in $localRecords) {
        $afterRecord = $afterById[$beforeRecord.Id]
        if ($null -eq $afterRecord -or $afterRecord.OriginalName -ne $beforeRecord.OriginalName -or $afterRecord.X -ne $beforeRecord.X -or $afterRecord.Y -ne $beforeRecord.Y -or $afterRecord.Z -ne $beforeRecord.Z) {
            $unexpectedChanges++
            continue
        }
        $expected = if ($updates.ContainsKey($beforeRecord.Id)) { [int]$updates[$beforeRecord.Id] } else { $beforeRecord.CurrentSpawnTime }
        if ($afterRecord.CurrentSpawnTime -ne $expected) {
            $unexpectedChanges++
        }
    }

    $validation['XML valido apos aplicacao'] = $null -ne $validatedXml.DocumentElement
    $validation['Quantidade de monster preservada'] = ($afterRecords.Count -eq $localRecords.Count)
    $validation['Assinaturas de nome/coordenada apos aplicacao'] = $afterSignature
    $validation['Nome/coordenada inalterados'] = ($beforeSignature -eq $afterSignature)
    $validation['Alteracoes inesperadas de atributo'] = $unexpectedChanges
} else {
    $validation['XML alterado'] = $false
    if ($Mode -eq 'DryRun') {
        $validation['Modo dry run preservou o XML'] = $true
    } else {
        $validation['Modo apply sem novas alteracoes'] = $true
    }
}

Write-SummaryReport $summaryReport $Mode $Policy $XmlPath $tibiantis.WireHash $markers $localRecords $automaticMarkers $updates $categoryCounts $validation $ReportDirectory

if ($Mode -eq 'DryRun') {
    $dryRunReport = Join-Path $ReportDirectory 'spawn_tibiantis_dry_run.md'
    $dryRunBuilder = [System.Text.StringBuilder]::new()
    [void]$dryRunBuilder.AppendLine('# Resultado do dry run de importacao TibiAntis')
    [void]$dryRunBuilder.AppendLine()
    [void]$dryRunBuilder.AppendLine("- Politica: $Policy")
    [void]$dryRunBuilder.AppendLine("- Marcadores TibiAntis: $($markers.Count)")
    [void]$dryRunBuilder.AppendLine("- Pontos locais: $($localRecords.Count)")
    [void]$dryRunBuilder.AppendLine("- Regioes automaticas: $($automaticMarkers.Count)")
    [void]$dryRunBuilder.AppendLine("- Criaturas que receberiam novo spawntime: $($updates.Count)")
    if ($Policy -eq 'Rebuild') {
        [void]$dryRunBuilder.AppendLine("- Referencias diretas congeladas: $($rebuild.Seeds.Count)")
        [void]$dryRunBuilder.AppendLine("- Criaturas reconstruidas por propagacao: $($rebuild.Propagated.Count)")
        [void]$dryRunBuilder.AppendLine("- Overrides globais: $($rebuild.GlobalOverrides.Count)")
        [void]$dryRunBuilder.AppendLine("- Overrides manuais no relatorio: $($rebuild.ManualOverrides.Count)")
        [void]$dryRunBuilder.AppendLine("- Pontos encerrados manualmente: $($rebuild.ManualResolved.Count)")
        [void]$dryRunBuilder.AppendLine("- Divergencias acima de 2x: $($rebuild.Suspicious.Count)")
        [void]$dryRunBuilder.AppendLine("- Sem referencia em 17 tiles: $($rebuild.NoReference.Count)")
    }
    foreach ($category in 'DistanceOver10', 'MissingLocal', 'CountMismatch', 'Ambiguous', 'Unassigned') {
        [void]$dryRunBuilder.AppendLine("- $($category): $($categoryCounts[$category])")
    }
    [void]$dryRunBuilder.AppendLine('- XML: nao alterado')
    Write-MarkdownFile $dryRunReport $dryRunBuilder
}

[pscustomobject]@{
    Mode = $Mode
    XmlPath = $XmlPath
    ReportDirectory = $ReportDirectory
    TibiantisMarkers = $markers.Count
    LocalMonsterPoints = $localRecords.Count
    AutomaticRegions = $automaticMarkers.Count
    EligibleLocalCreatures = ($automaticMarkers | ForEach-Object { $_.AssignedLocalIds.Count } | Measure-Object -Sum).Sum
    PropagatedLocalCreatures = if ($Policy -eq 'Rebuild') { $rebuild.Propagated.Count } else { 0 }
    GlobalOverrideCreatures = if ($Policy -eq 'Rebuild') { $rebuild.GlobalOverrides.Count } else { 0 }
    ManualOverrideCreatures = if ($Policy -eq 'Rebuild') { $rebuild.ManualOverrides.Count } else { 0 }
    ManualResolvedCreatures = if ($Policy -eq 'Rebuild') { $rebuild.ManualResolved.Count } else { 0 }
    SuspiciousLocalCreatures = if ($Policy -eq 'Rebuild') { $rebuild.Suspicious.Count } else { 0 }
    NoReferenceLocalCreatures = if ($Policy -eq 'Rebuild') { $rebuild.NoReference.Count } else { 0 }
    SpawnTimesChanged = $updates.Count
    DistanceOver10 = $categoryCounts['DistanceOver10']
    MissingLocal = $categoryCounts['MissingLocal']
    CountMismatch = $categoryCounts['CountMismatch']
    Ambiguous = $categoryCounts['Ambiguous']
    Unassigned = $categoryCounts['Unassigned']
} | Format-List
