[CmdletBinding()]
param(
    [string]$Version = '0.1.0.0',
    [string]$IdentityName,
    [string]$PublisherDN,
    [string]$PublisherDisplayName,
    [switch]$DevelopmentIdentity,
    [switch]$SkipBuild,
    [string]$HostPath,
    [string]$RuntimeConfigurationPath,
    [string]$PolicyTextPath,
    [string]$Configuration = 'Release',
    [string]$MakeAppx
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if ($env:OS -ne 'Windows_NT') { throw 'MSIX packaging requires Windows SDK MakeAppx.' }
if ($Version -notmatch '^\d+\.\d+\.\d+\.0$') { throw 'Store package version must have four numeric components and a final zero.' }
foreach ($component in $Version.Split('.')) {
    if ([long]$component -gt 65535) { throw 'Package version components must be within 0..65535.' }
}
if ($DevelopmentIdentity) {
    if ($IdentityName -or $PublisherDN -or $PublisherDisplayName) { throw 'Do not combine development identity with real Store identity fields.' }
    $IdentityName = 'Numpad9.Development'
    $PublisherDN = 'CN=Numpad9 Development'
    $PublisherDisplayName = 'Numpad9 Development (CI only)'
    $label = 'DEVELOPMENT-IDENTITY-UNSIGNED'
} else {
    foreach ($value in @($IdentityName, $PublisherDN, $PublisherDisplayName)) {
        if ([string]::IsNullOrWhiteSpace($value) -or $value -match '(?i)@@|placeholder|REQUIRED BEFORE PUBLICATION|REPLACE|YOUR_|development|example') {
            throw 'Store upload builds require the exact real IdentityName, PublisherDN and PublisherDisplayName from Partner Center.'
        }
    }
    $label = 'STORE-UPLOAD'
}
if ($IdentityName -notmatch '^[A-Za-z0-9][A-Za-z0-9.\-]{2,49}$') { throw 'IdentityName must match the valid Partner Center package name.' }
if ($PublisherDN -notmatch '^CN=' -or $PublisherDN -match '[\r\n]') { throw 'PublisherDN must be the exact Partner Center distinguished name beginning CN=.' }
if ($PublisherDisplayName.Length -gt 256) { throw 'PublisherDisplayName is too long.' }
$runtimeConfigurationHash = $null
if ($RuntimeConfigurationPath) {
    $configurationFile = Get-Item -LiteralPath $RuntimeConfigurationPath
    if ($configurationFile.PSIsContainer) { throw 'RuntimeConfigurationPath must be a JSON file.' }
    try { $runtimeConfiguration = Get-Content -Raw -Encoding UTF8 -LiteralPath $configurationFile.FullName | ConvertFrom-Json }
    catch { throw 'Runtime configuration must be valid JSON. Its contents are not printed.' }
    $requiredFields = @('featureId', 'token', 'attestation')
    foreach ($field in $requiredFields) {
        $property = $runtimeConfiguration.PSObject.Properties[$field]
        if (-not $property -or $property.Value -isnot [string] -or [string]::IsNullOrWhiteSpace($property.Value) -or $property.Value -match '(?i)REPLACE[_ ]?ME|YOUR_[A-Z_]+|REQUIRED BEFORE PUBLICATION|@@|^<.*>$') {
            throw "Runtime configuration requires a non-placeholder string: $field"
        }
    }
    $binding = $runtimeConfiguration.PSObject.Properties['inputProfile']
    if ($binding -and $binding.Value -isnot [string]) { throw 'Optional inputProfile must be a string; empty selects the active Windows profile.' }
    if (@($runtimeConfiguration.PSObject.Properties.Name | Where-Object { $_ -notin ($requiredFields + 'inputProfile') }).Count) { throw 'Runtime configuration contains unexpected fields.' }
    $RuntimeConfigurationPath = $configurationFile.FullName
    $runtimeConfigurationHash = (Get-FileHash -LiteralPath $RuntimeConfigurationPath -Algorithm SHA256).Hash
} elseif (-not $DevelopmentIdentity) {
    throw 'Store upload builds require RuntimeConfigurationPath with Microsoft-granted featureId/token/attestation. These are not commercial signing credentials.'
}

if (-not $MakeAppx) {
    $sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
    $tools = @(Get-ChildItem -LiteralPath $sdkBin -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending | ForEach-Object { Join-Path $_.FullName 'x64/makeappx.exe' } | Where-Object { Test-Path -LiteralPath $_ })
    if (-not $tools.Count) { throw 'Install Windows SDK or provide -MakeAppx.' }
    $MakeAppx = $tools[0]
}
$MakeAppx = (Get-Item -LiteralPath $MakeAppx).FullName
$build = Join-Path $root 'build/windows-store'
if (-not $SkipBuild) {
    & cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_SYSTEM_VERSION=10.0.26100.0' '-DNUMPAD9_BUILD_STORE=ON' '-DNUMPAD9_BUILD_APP=OFF' '-DBUILD_TESTING=ON'
    if ($LASTEXITCODE -ne 0) { throw 'Store host CMake configure failed.' }
    & cmake --build $build --config $Configuration --target Numpad9Store --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Store host native build failed.' }
}
if (-not $HostPath) { $HostPath = Join-Path $build "$Configuration/Numpad9Store.exe" }
$HostPath = (Get-Item -LiteralPath $HostPath).FullName
$bytes = [IO.File]::ReadAllBytes($HostPath)
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw 'Store host is not a PE executable.' }
$peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
if ($peOffset -lt 0 -or $peOffset + 6 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes, $peOffset + 4) -ne 0x8664) { throw 'Store host must be an x64 PE.' }
$hostVersion = (Get-Item -LiteralPath $HostPath).VersionInfo.ProductVersion
if ($hostVersion -ne $Version -and "$hostVersion.0" -ne $Version) { throw "Store host product version $hostVersion does not match package version $Version." }

$stage = Join-Path $root "artifacts/msix-staging/$label"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'Assets') | Out-Null
# Explicit allowlist: never capture native TSF COM registration or its DLLs.
Copy-Item -LiteralPath $HostPath -Destination (Join-Path $stage 'Numpad9Store.exe')
foreach ($file in @('lexicon.tsv', 'NOTICE.txt', 'SOURCES.json')) {
    Copy-Item -LiteralPath (Join-Path $root "data/$file") -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'data/licenses') -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $root 'docs/dictionary.md') -Destination $stage
if ($RuntimeConfigurationPath) {
    Copy-Item -LiteralPath $RuntimeConfigurationPath -Destination (Join-Path $stage 'input-access.json')
}
$policyHash = $null
if ($PolicyTextPath) {
    $policy = Get-Content -Raw -Encoding UTF8 -LiteralPath $PolicyTextPath
    if ([string]::IsNullOrWhiteSpace($policy) -or $policy -match '(?i)REQUIRED BEFORE PUBLICATION|REPLACE[_ ]?ME|YOUR_[A-Z_]+') { throw 'Supplied policy must be actual nonempty text without unresolved placeholders.' }
    Copy-Item -LiteralPath $PolicyTextPath -Destination (Join-Path $stage 'privacy-policy.txt')
    $policyHash = (Get-FileHash -LiteralPath $PolicyTextPath -Algorithm SHA256).Hash
}

Add-Type -AssemblyName System.Drawing
$originalIcon = [Drawing.Image]::FromFile((Join-Path $root 'assets/Numpad9.png'))
try {
    foreach ($asset in @(@('StoreLogo.png', 50), @('Square44x44Logo.png', 44), @('Square150x150Logo.png', 150))) {
        # Deterministic size exports of the existing original app icon.
        $bitmap = [Drawing.Bitmap]::new([int]$asset[1], [int]$asset[1])
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.DrawImage($originalIcon, 0, 0, [int]$asset[1], [int]$asset[1])
            $bitmap.Save((Join-Path $stage "Assets/$($asset[0])"), [Drawing.Imaging.ImageFormat]::Png)
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
    }
} finally { $originalIcon.Dispose() }
$manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $root 'packaging/msix/AppxManifest.xml')
foreach ($pair in @(@('@@IDENTITY_NAME@@', $IdentityName), @('@@PUBLISHER_DN@@', $PublisherDN), @('@@PUBLISHER_DISPLAY_NAME@@', $PublisherDisplayName), @('@@VERSION@@', $Version))) {
    $manifest = $manifest.Replace($pair[0], [Security.SecurityElement]::Escape($pair[1]))
}
if ($manifest -match '@@') { throw 'Unresolved manifest template field.' }
[xml]$parsedManifest = $manifest
$manifest | Set-Content -LiteralPath (Join-Path $stage 'AppxManifest.xml') -Encoding UTF8
$package = Join-Path $root "artifacts/Numpad9-$Version-x64-$label.msix"
$log = Join-Path $root "artifacts/msix-$label.log"
# Keep full SDK semantic validation enabled: intentionally no /nv option.
& $MakeAppx pack /d $stage /p $package /h SHA256 /o /v 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "MakeAppx semantic validation/pack failed. See $log" }
Add-Type -AssemblyName System.IO.Compression
$archive = [IO.Compression.ZipFile]::OpenRead($package)
try {
    $entries = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
    if (@($entries | Where-Object { $_ -match '(?i)\.dll$|registry\.dat$|register\.exe$|AppxSignature\.p7x$|Numpad9Ime' }).Count) { throw 'Unexpected TSF registration, DLL, or signature in Store upload package.' }
    if (@($entries | Where-Object { $_ -match '(?i)\.exe$' }).Count -ne 1 -or 'Numpad9Store.exe' -notin $entries) { throw 'MSIX must contain only the new Store input host executable.' }
} finally { $archive.Dispose() }
[ordered]@{ version = $Version; identityName = $IdentityName; publisherDN = $PublisherDN; publisherDisplayName = $PublisherDisplayName;
    architecture = 'x64'; minimumWindowsBuild = 26100; developmentIdentity = [bool]$DevelopmentIdentity; runtimeConfigurationProvided = [bool]$RuntimeConfigurationPath;
    package = $package; sha256 = (Get-FileHash $package -Algorithm SHA256).Hash; policyTextSha256 = $policyHash; runtimeConfigurationSha256 = $runtimeConfigurationHash;
    signedLocally = $false; makeAppxSemanticValidation = 'passed'; customCapability = 'Microsoft.appCategory.textInputMethod_8wekyb3d8bbwe';
    certification = 'Microsoft-granted input configuration cannot be authenticated offline; Store custom capability approval and certification are separate requirements'; files = $entries } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root "artifacts/msix-$label.json") -Encoding UTF8
Write-Host "MSIX package created: $package"
if ($DevelopmentIdentity) { Write-Host 'CI identity only; do not submit this package to the Store.' }
else { Write-Host 'Unsigned Store-upload candidate; Microsoft Store signs the distributed package. No commercial certificate is required for this route.' }
