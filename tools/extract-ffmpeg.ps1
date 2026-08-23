$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$ffmpeg = Join-Path $root 'third_party\ffmpeg'
$sevenZip = 'C:\Program Files\7-Zip\7z.exe'
if (-not (Test-Path $sevenZip)) { throw '7-Zip is required at C:\Program Files\7-Zip\7z.exe' }
Get-ChildItem $ffmpeg -Filter *.7z -File | ForEach-Object {
    & $sevenZip x $_.FullName "-o$ffmpeg" -y
    if ($LASTEXITCODE -ne 0) { throw "7-Zip failed on $($_.Name)" }
}
Write-Host 'FFmpeg runtime DLLs:'
Get-ChildItem $ffmpeg -Filter *.dll -File | Select-Object Name,Length
