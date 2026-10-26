param(
    [string]$RuntimeRoot = (Join-Path $PSScriptRoot 'build/portable/G7BatteryTray'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot 'dist'),
    [string]$Version = '1.2.0'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be numeric major.minor.patch.' }
$RuntimeRoot = [IO.Path]::GetFullPath($RuntimeRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$executable = Join-Path $RuntimeRoot 'G7BatteryTray.exe'
if ((Get-Item -LiteralPath $executable).VersionInfo.FileVersion -ne $Version) { throw 'Executable version does not match the package version.' }
$staging = Join-Path $PSScriptRoot ('build/package/' + [guid]::NewGuid().ToString('N'))
$portable = Join-Path $staging 'MCHOSEBatteryTray'
$source = Join-Path $staging 'MCHOSEBatteryTray-source'
New-Item -ItemType Directory -Path $portable,$source -Force | Out-Null
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
Copy-Item -LiteralPath $executable -Destination $portable
$documentation = @('README.md','README.txt','COMPATIBILITY.md','PROTOCOLS.md','VERIFICATION.txt','THIRD-PARTY-NOTICES.txt')
foreach ($file in $documentation) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $portable }
$sourceItems = $documentation + @('.gitignore','.gitattributes','build.ps1','package.ps1','src','assets','tests','third_party')
foreach ($item in $sourceItems) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $item) -Destination $source -Recurse }
$runtimeZip = Join-Path $OutputRoot "MCHOSEBatteryTray-$Version-win-x64.zip"
$sourceZip = Join-Path $OutputRoot "MCHOSEBatteryTray-$Version-source.zip"
Compress-Archive -LiteralPath $portable -DestinationPath $runtimeZip -CompressionLevel Optimal -Force
Compress-Archive -LiteralPath $source -DestinationPath $sourceZip -CompressionLevel Optimal -Force
$hashes = foreach ($archive in @($runtimeZip,$sourceZip)) {
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($archive))"
}
$checksumPath = Join-Path $OutputRoot "SHA256SUMS-$Version.txt"
[IO.File]::WriteAllText($checksumPath, ($hashes -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
Get-Item -LiteralPath $runtimeZip,$sourceZip,$checksumPath | Select-Object Name,Length,FullName
