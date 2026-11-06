param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Executable).Path
$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if (-not $dumpbin) { throw 'dumpbin.exe is required to verify Windows executable exports.' }
$exports = (& $dumpbin.Source /exports $resolved) -join "`n"
foreach ($symbol in @('NvOptimusEnablement','AmdPowerXpressRequestHighPerformance')) {
    if ($exports -notmatch [regex]::Escape($symbol)) { throw "Missing hybrid-GPU export: $symbol" }
}
Write-Output 'PASS: executable requests the high-performance hybrid GPU'
