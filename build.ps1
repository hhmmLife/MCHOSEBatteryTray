param(
    [string]$ToolchainRoot,
    [string]$OutputRoot = (Join-Path $PSScriptRoot 'build/portable/G7BatteryTray'),
    [switch]$Tests
)
$ErrorActionPreference = 'Stop'
if ($ToolchainRoot) { $taskCompiler = Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe' }
else { $taskCompiler = (Get-Command x86_64-w64-mingw32-clang++.exe -ErrorAction Stop).Source; $ToolchainRoot = Split-Path (Split-Path $taskCompiler) }
if (!(Test-Path -LiteralPath $taskCompiler)) { throw 'Provide -ToolchainRoot for LLVM-MinGW (x64 UCRT).' }
$taskBuild = Join-Path $PSScriptRoot 'build/intermediate'
New-Item -ItemType Directory -Force -Path $taskBuild,$OutputRoot | Out-Null
$taskWindres = Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-windres.exe'
$taskResource = Join-Path $taskBuild 'app.res'
Push-Location $PSScriptRoot
try {
    & $taskWindres '-I' 'assets' '-i' 'assets/app.rc' '-o' $taskResource '-O' 'coff'
    if ($LASTEXITCODE) { throw 'Resource compilation failed.' }
    $taskSources = @('src/app.cpp','src/storage.cpp','src/device.cpp','src/device_protocol.cpp','src/notifications.cpp','src/notification_client.cpp','src/tray_icon.cpp')
    & $taskCompiler '-std=c++20' '-Os' '-DNDEBUG' '-Wall' '-Wextra' '-Wpedantic' '-Wno-unknown-pragmas' '-municode' '-mwindows' '-static' '-Wl,--strip-all' @taskSources $taskResource '-o' (Join-Path $OutputRoot 'G7BatteryTray.exe') '-lcfgmgr32' '-lhid' '-lshell32' '-lshlwapi' '-lpropsys' '-lole32' '-loleaut32' '-luuid' '-lruntimeobject' '-lcomctl32' '-ladvapi32' '-limm32'
    if ($LASTEXITCODE) { throw 'Application compilation failed.' }
    Copy-Item -LiteralPath 'README.txt' -Destination $OutputRoot -Force
    Copy-Item -LiteralPath 'THIRD-PARTY-NOTICES.txt' -Destination $OutputRoot -Force
    Copy-Item -LiteralPath 'README.md','COMPATIBILITY.md','PROTOCOLS.md' -Destination $OutputRoot -Force
    if (Test-Path -LiteralPath 'VERIFICATION.txt') { Copy-Item -LiteralPath 'VERIFICATION.txt' -Destination $OutputRoot -Force }
    if ($Tests) {
        & $taskCompiler '-std=c++20' '-O2' '-Wall' '-Wextra' '-static' 'tests/device_tests.cpp' 'src/device_protocol.cpp' '-o' (Join-Path $taskBuild 'device_tests.exe')
        if ($LASTEXITCODE) { throw 'Device test compilation failed.' }
        & (Join-Path $taskBuild 'device_tests.exe')
        if ($LASTEXITCODE) { throw 'Device protocol tests failed.' }
        & $taskCompiler '-std=c++20' '-O2' '-Wall' '-Wextra' '-static' 'tests/model_tests.cpp' '-o' (Join-Path $taskBuild 'model_tests.exe')
        if ($LASTEXITCODE) { throw 'Test compilation failed.' }
        & (Join-Path $taskBuild 'model_tests.exe')
        if ($LASTEXITCODE) { throw 'Model tests failed.' }
        & $taskCompiler '-std=c++20' '-O2' '-Wall' '-Wextra' '-municode' '-static' 'tests/storage_tests.cpp' 'src/storage.cpp' '-ladvapi32' '-o' (Join-Path $taskBuild 'storage_tests.exe')
        if ($LASTEXITCODE) { throw 'Storage test compilation failed.' }
        & (Join-Path $taskBuild 'storage_tests.exe') (Join-Path $taskBuild 'test-data')
        if ($LASTEXITCODE) { throw 'Storage tests failed.' }
        & $taskCompiler '-std=c++20' '-O2' '-Wall' '-Wextra' '-municode' '-static' 'tests/icon_tests.cpp' 'src/tray_icon.cpp' '-lgdi32' '-luser32' '-o' (Join-Path $taskBuild 'icon_tests.exe')
        if ($LASTEXITCODE) { throw 'Icon test compilation failed.' }
        & (Join-Path $taskBuild 'icon_tests.exe') (Join-Path $taskBuild 'icon-preview.bmp')
        if ($LASTEXITCODE) { throw 'Icon tests failed.' }
    }
} finally { Pop-Location }
Write-Output (Join-Path $OutputRoot 'G7BatteryTray.exe')
