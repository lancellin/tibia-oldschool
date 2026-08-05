param(
    [int]$FirstIndex = 2,
    [int]$LastIndex = 200,
    [string]$SourcePlayer = "Teste001",
    [string]$Database = "oldschool772db_backup_test_20260610",
    [string]$DatabaseHost = "127.0.0.1",
    [int]$DatabasePort = 3306,
    [string]$DatabaseUser = "oldschool772",
    [string]$DatabasePassword = "123456",
    [string]$AccountPasswordSha1 = "7c4a8d09ca3762af61e59520943dc26494f8941b",
    [switch]$AssignGridPositions,
    [int]$GridMinX = 33214,
    [int]$GridMaxX = 33278,
    [int]$GridMinY = 32276,
    [int]$GridMaxY = 32375,
    [int]$GridZ = 7,
    [ValidateRange(1, 1000)]
    [int]$GridColumns = 16,
    [ValidateRange(1, 100)]
    [int]$GridStepX = 4,
    [ValidateRange(1, 100)]
    [int]$GridStepY = 2
)

$ErrorActionPreference = "Stop"

if ($FirstIndex -lt 2 -or $LastIndex -gt 1000 -or $FirstIndex -gt $LastIndex) {
    throw "The requested character index range is invalid."
}

if ($AssignGridPositions) {
    $targetCount = $LastIndex - $FirstIndex + 1
    $gridRows = [int][Math]::Ceiling($targetCount / [double]$GridColumns)
    $requiredMaxX = $GridMinX + (($GridColumns - 1) * $GridStepX)
    $requiredMaxY = $GridMinY + (($gridRows - 1) * $GridStepY)
    if ($GridMinX -gt $GridMaxX -or $GridMinY -gt $GridMaxY -or
        $requiredMaxX -gt $GridMaxX -or $requiredMaxY -gt $GridMaxY) {
        throw (
            "The requested grid does not fit: needs through " +
            "$requiredMaxX,$requiredMaxY,$GridZ but bounds end at " +
            "$GridMaxX,$GridMaxY,$GridZ.")
    }
}

$mysql = "D:\tibia-dev-tools\mariadb-10.11.17\bin\mariadb.exe"
if (-not (Test-Path -LiteralPath $mysql)) {
    throw "MariaDB client was not found at $mysql."
}

$mysqlArgs = @(
    "--no-defaults",
    "--protocol=tcp",
    "-h", $DatabaseHost,
    "-P", $DatabasePort,
    "-u", $DatabaseUser,
    "--password=$DatabasePassword",
    $Database,
    "--batch",
    "--raw",
    "--skip-column-names"
)

function Invoke-ScalarQuery([string]$Query) {
    $value = & $mysql @mysqlArgs -e $Query
    if ($LASTEXITCODE -ne 0) {
        throw "MariaDB query failed."
    }
    return $value
}

function Convert-HexToBytes([string]$Hex) {
    return [System.Runtime.Remoting.Metadata.W3cXsd2001.SoapHexBinary]::Parse($Hex).Value
}

function Convert-BytesToHex([byte[]]$Bytes) {
    return ([BitConverter]::ToString($Bytes)).Replace("-", "")
}

function Get-ActorMarkerHex([uint32]$Guid) {
    $bytes = [BitConverter]::GetBytes($Guid)
    return "27" + (Convert-BytesToHex $bytes)
}

$escapedSourcePlayer = $SourcePlayer.Replace("'", "''")
$sourceRow = Invoke-ScalarQuery @"
SELECT p.id, p.account_id, a.premium_ends_at
FROM players p
JOIN accounts a ON a.id=p.account_id
WHERE p.name='$escapedSourcePlayer'
LIMIT 1;
"@

if (-not $sourceRow) {
    throw "Source player $SourcePlayer was not found."
}

$sourceParts = $sourceRow.Split("`t")
$sourcePlayerId = [uint32]$sourceParts[0]
$sourcePremiumEndsAt = [uint64]$sourceParts[2]
$sourceActorMarker = Get-ActorMarkerHex $sourcePlayerId

$columnCsv = Invoke-ScalarQuery @"
SELECT GROUP_CONCAT(COLUMN_NAME ORDER BY ORDINAL_POSITION SEPARATOR ',')
FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA='$Database'
  AND TABLE_NAME='players'
  AND COLUMN_NAME NOT IN (
    'id','name','account_id','health','healthmax','cap',
    'lastlogin','lastlogout','lastip','onlinetime'
  );
"@

if (-not $columnCsv) {
    throw "Could not resolve the players table columns."
}

$itemLines = & $mysql @mysqlArgs -e @"
SELECT pid,sid,itemtype,count,HEX(attributes)
FROM player_items
WHERE player_id=$sourcePlayerId
ORDER BY sid;
"@

if ($LASTEXITCODE -ne 0 -or -not $itemLines) {
    throw "Could not read the source inventory."
}

$sourceItems = New-Object System.Collections.Generic.List[object]
$latin1 = [Text.Encoding]::GetEncoding(28591)
$instancePattern = "(?<=floor_persistence_instance_id\x01\x20\x00)[0-9a-f]{32}"
$sourceInstanceCount = 0

foreach ($line in $itemLines) {
    $parts = $line.Split("`t")
    if ($parts.Count -lt 5) {
        throw "Malformed source player_items row."
    }

    $attributeText = ""
    if ($parts[4]) {
        $attributeText = $latin1.GetString((Convert-HexToBytes $parts[4]))
        $matches = [regex]::Matches($attributeText, $instancePattern)
        if ($matches.Count -gt 1) {
            throw "Source item sid $($parts[1]) contains more than one instance_id."
        }
        $sourceInstanceCount += $matches.Count
    }

    $sourceItems.Add([pscustomobject]@{
        Pid = [int]$parts[0]
        Sid = [int]$parts[1]
        ItemType = [int]$parts[2]
        Count = [int]$parts[3]
        AttributeText = $attributeText
    })
}

$sourceStorageCount = [int](Invoke-ScalarQuery "SELECT COUNT(*) FROM player_storage WHERE player_id=$sourcePlayerId;")
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
$creation = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()

try {
    for ($index = $FirstIndex; $index -le $LastIndex; ++$index) {
        $accountName = (100000 + $index).ToString()
        $characterName = "Teste{0:D3}" -f $index
        $targetPositionSql = ""
        if ($AssignGridPositions) {
            $gridSlot = $index - $FirstIndex
            $gridRow = [int][Math]::Floor($gridSlot / [double]$GridColumns)
            $gridColumn = $gridSlot % $GridColumns
            if (($gridRow % 2) -eq 1) {
                $gridColumn = ($GridColumns - 1) - $gridColumn
            }
            $targetX = $GridMinX + ($gridColumn * $GridStepX)
            $targetY = $GridMinY + ($gridRow * $GridStepY)
            $targetPositionSql = (
                "UPDATE players SET posx=$targetX,posy=$targetY,posz=$GridZ " +
                "WHERE id=@new_player;")
        }

        $existing = [int](Invoke-ScalarQuery @"
SELECT
    (SELECT COUNT(*) FROM accounts WHERE name='$accountName') +
    (SELECT COUNT(*) FROM players WHERE name='$characterName');
"@)
        if ($existing -ne 0) {
            throw "Target $accountName / $characterName already exists."
        }

        $itemValues = New-Object System.Collections.Generic.List[string]
        $generatedIdentities = 0

        foreach ($sourceItem in $sourceItems) {
            $attributeText = $sourceItem.AttributeText
            if ($attributeText) {
                $matches = [regex]::Matches($attributeText, $instancePattern)
                if ($matches.Count -eq 1) {
                    $random = New-Object byte[] 16
                    $rng.GetBytes($random)
                    $newInstanceId = (Convert-BytesToHex $random).ToLowerInvariant()
                    $attributeText = [regex]::Replace(
                        $attributeText, $instancePattern, $newInstanceId, 1)
                    ++$generatedIdentities
                }
            }

            $attributeHex = ""
            if ($attributeText) {
                $attributeHex = Convert-BytesToHex $latin1.GetBytes($attributeText)
            }

            $itemValues.Add(
                "(@new_player,$($sourceItem.Pid),$($sourceItem.Sid)," +
                "$($sourceItem.ItemType),$($sourceItem.Count),UNHEX('$attributeHex'))")
        }

        if ($itemValues.Count -ne $sourceItems.Count -or
            $generatedIdentities -ne $sourceInstanceCount) {
            throw "Generated inventory counters differ for $characterName."
        }

        $sql = New-Object System.Collections.Generic.List[string]
        $sql.Add("START TRANSACTION;")
        $sql.Add(
            "INSERT INTO accounts " +
            "(name,password,secret,type,premium_ends_at,email,creation) VALUES " +
            "('$accountName','$AccountPasswordSha1',NULL,1,$sourcePremiumEndsAt,'',$creation);")
        $sql.Add("SET @new_account=LAST_INSERT_ID();")
        $sql.Add(
            "INSERT INTO players " +
            "(name,account_id,health,healthmax,cap,lastlogin,lastlogout,lastip,onlinetime,$columnCsv) " +
            "SELECT '$characterName',@new_account,80000,80000,5000,0,0,0,0,$columnCsv " +
            "FROM players WHERE id=$sourcePlayerId;")
        $sql.Add("SET @new_player=LAST_INSERT_ID();")
        if ($targetPositionSql) {
            $sql.Add($targetPositionSql)
        }
        $sql.Add(
            "INSERT INTO player_items " +
            "(player_id,pid,sid,itemtype,count,attributes) VALUES`n" +
            (($itemValues -join ",`n") + ";"))
        $sql.Add(
            'SET @target_actor_marker=UNHEX(CONCAT(' +
            "'27'," +
            "LPAD(HEX(MOD(@new_player,256)),2,'0')," +
            "LPAD(HEX(MOD(FLOOR(@new_player/256),256)),2,'0')," +
            "LPAD(HEX(MOD(FLOOR(@new_player/65536),256)),2,'0')," +
            "LPAD(HEX(MOD(FLOOR(@new_player/16777216),256)),2,'0')));")
        $sql.Add(
            "UPDATE player_items SET attributes=REPLACE(" +
            "attributes,UNHEX('$sourceActorMarker'),@target_actor_marker) " +
            "WHERE player_id=@new_player " +
            "AND LOCATE(UNHEX('$sourceActorMarker'),attributes)>0;")
        $sql.Add(
            'INSERT INTO player_storage (player_id,`key`,`value`) ' +
            ('SELECT @new_player,`key`,`value` FROM player_storage ' +
            'WHERE player_id={0};' -f $sourcePlayerId))
        $sql.Add("COMMIT;")

        ($sql -join "`n") | & $mysql @mysqlArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Creation transaction failed for $accountName / $characterName."
        }

        if (($index - $FirstIndex + 1) % 10 -eq 0 -or $index -eq $LastIndex) {
            Write-Output "Created through $characterName."
        }
    }
}
finally {
    $rng.Dispose()
}

Write-Output ((
    "Completed accounts {0} through {1}; source_items={2}; " +
    "new_instance_ids_per_character={3}; source_storages={4}.") -f
    (100000 + $FirstIndex), (100000 + $LastIndex),
    $sourceItems.Count, $sourceInstanceCount, $sourceStorageCount)
