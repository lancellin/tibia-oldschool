param(
    [string]$BatchRoot = "C:\tibia-oldschool\tools\assets\tests\map-hd-standard-sample",
    [string]$SampleClientIds = "870,2109",
    [int]$SampleLimit = 0
)

$ErrorActionPreference = "Stop"

$root = "C:\tibia-oldschool"
$python = "C:\Users\guisu\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
$prepare = Join-Path $root "tools\assets\prepare_map_hd_assets.py"

$argsList = @(
    $prepare,
    "--map", (Join-Path $root "server\data\world\world.otbm"),
    "--otb", (Join-Path $root "server\data\items\items.otb"),
    "--items-xml", (Join-Path $root "server\data\items\items.xml"),
    "--dat", (Join-Path $root "sources\otclient-redemption\data\things\772\Tibia.dat"),
    "--spr", (Join-Path $root "sources\otclient-redemption\data\things\772\Tibia.spr"),
    "--out-root", $BatchRoot
)

if ($SampleClientIds.Trim()) {
    $argsList += @("--sample-client-ids", $SampleClientIds)
}

if ($SampleLimit -gt 0) {
    $argsList += @("--sample-limit", $SampleLimit)
}

Write-Host "Preparing map HD sample..."
Write-Host "BatchRoot      : $BatchRoot"
Write-Host "SampleClientIds: $SampleClientIds"
if ($SampleLimit -gt 0) {
    Write-Host "SampleLimit    : $SampleLimit"
}
Write-Host ""

& $python @argsList
if ($LASTEXITCODE -ne 0) {
    throw "prepare_map_hd_assets.py failed with exit code $LASTEXITCODE"
}

Write-Host ""
Write-Host "Sample inputs are ready at:"
Write-Host (Join-Path $BatchRoot "inputs-flat")
