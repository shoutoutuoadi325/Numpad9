[CmdletBinding()]
param([string]$Configuration = 'Release', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if ($env:OS -ne 'Windows_NT') { throw 'Windows with Visual Studio 2022 C++ and Windows SDK is required.' }
foreach ($arch in @('x64', 'x86')) {
    $platform = if ($arch -eq 'x64') { 'x64' } else { 'Win32' }
    $app = if ($arch -eq 'x64') { 'ON' } else { 'OFF' }
    $build = Join-Path $root "build/windows-$arch"
    & cmake -S $root -B $build -G 'Visual Studio 17 2022' -A $platform "-DNUMPAD9_BUILD_APP=$app" '-DBUILD_TESTING=ON'
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $arch" }
    & cmake --build $build --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $arch" }
    if (-not $SkipTests) {
        & ctest --test-dir $build -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Tests failed: $arch" }
    }
    $stage = Join-Path $root "release/$arch"
    & cmake --install $build --config $Configuration --prefix $stage
    if ($LASTEXITCODE -ne 0) { throw "Staging failed: $arch" }
    foreach ($file in @('Numpad9Ime.dll', 'lexicon.tsv')) {
        if (-not (Test-Path (Join-Path $stage $file))) { throw "Missing staged $arch/$file" }
    }
    if ($arch -eq 'x64' -and -not (Test-Path (Join-Path $stage 'Numpad9.exe'))) { throw 'Missing staged host.' }
}
Write-Host 'Windows candidate built. Run signing and structural release validation before submission.'
