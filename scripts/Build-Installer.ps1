[CmdletBinding()]
param(
    [string]$Version = '0.1.0',
    [string]$Publisher,
    [string]$Contact,
    [string]$SupportUrl,
    [string]$SourceUrl,
    [string]$Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    [switch]$UnsignedDevelopment,
    [switch]$GitHubUnsignedRelease,
    [switch]$ExternalSigning,
    [string]$SignedUninstallerDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if (@(@($UnsignedDevelopment, $GitHubUnsignedRelease, $ExternalSigning) | Where-Object { $_ }).Count -gt 1) { throw 'Choose exactly one unsigned development, unsigned GitHub release, or external signing mode.' }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be numeric major.minor.patch.' }
if (-not (Test-Path $Iscc)) { throw 'Install Inno Setup 6.3+ or specify -Iscc.' }
foreach ($value in @($Publisher, $Contact, $SupportUrl, $SourceUrl)) {
    if ($value -and ($value.Contains('"') -or $value.Contains('{') -or $value.Contains('}') -or $value.Contains("`n") -or $value.Contains("`r"))) { throw 'Metadata cannot contain quotes, braces, or newlines.' }
}
if ($UnsignedDevelopment) {
    if (-not $Publisher) { $Publisher = 'Unsigned development build' }
    if (-not $Contact) { $Contact = 'Development build - not for Store submission' }
    if (-not $SourceUrl) { $SourceUrl = 'https://www.gnu.org/licenses/gpl-3.0.html' }
} elseif (-not $Publisher -or -not $Contact -or $SourceUrl -notmatch '^https://') {
    throw 'Public builds require your real Publisher, Contact, and HTTPS corresponding-source URL.'
}
if (-not $SupportUrl) { $SupportUrl = if ($Contact -match '^https://') { $Contact } else { $SourceUrl } }
if ($SupportUrl -notmatch '^https://') { throw 'SupportUrl must be an HTTPS address.' }
if ($GitHubUnsignedRelease -and (@($Publisher, $Contact, $SourceUrl) -match '(?i)unsigned development|REQUIRED BEFORE PUBLICATION|REPLACE|example\.(com|org)|YOUR_')) { throw 'Unsigned GitHub releases require completed real publisher/support/source metadata.' }
$payload = Join-Path $root 'release'
foreach ($relative in @('x64/Numpad9.exe', 'x64/Numpad9Ime.dll', 'x64/lexicon.tsv', 'x86/Numpad9Ime.dll', 'x86/lexicon.tsv')) {
    if (-not (Test-Path (Join-Path $payload $relative))) { throw "Missing payload: $relative. Run Build-Windows.ps1 first." }
}
foreach ($relative in @('x64/Numpad9.exe', 'x64/Numpad9Ime.dll', 'x86/Numpad9Ime.dll')) {
    $peVersion = (Get-Item (Join-Path $payload $relative)).VersionInfo.ProductVersion
    if ($peVersion -ne $Version -and $peVersion -ne "$Version.0") { throw "Payload $relative is version $peVersion, not requested installer version $Version." }
}
$metadata = Join-Path $root 'artifacts/metadata'
New-Item -ItemType Directory -Force -Path $metadata | Out-Null
$policy = Get-Content -Raw -LiteralPath (Join-Path $root 'docs/privacy-exe.template.md')
$policy = $policy.Replace('- 真实发行人 / Legal publisher: **REQUIRED BEFORE PUBLICATION**', "- 真实发行人 / Legal publisher: $Publisher")
$policy = $policy.Replace('- 隐私及支持联系方式 / Privacy and support contact: **REQUIRED BEFORE PUBLICATION**', "- 隐私及支持联系方式 / Privacy and support contact: $Contact")
$policy = $policy.Replace('This installed-policy template is a publication draft until those two fields are completed. Partner Center EXE properties accept privacy-policy text directly; a hosted HTTPS policy is an alternative. Publishing this unresolved draft is not acceptable.', 'Publisher and support details above apply to this installer build.')
$policy = $policy.Replace('这是安装包内政策的草稿，发布前必须在本文末尾填写真实发行人和联系方式。Partner Center 的 EXE 产品属性可直接填写完整隐私政策文本；也可提供公开 HTTPS 政策网页。以下描述针对本仓库的离线版本；功能改变时需要重新审核和更新。', '以下描述针对本版本的离线功能；功能改变时需要重新审核和更新。')
$policy | Set-Content -LiteralPath (Join-Path $metadata 'privacy-policy.md') -Encoding UTF8
@("Numpad9 $Version", 'Application license: GNU GPL version 3.', "Full corresponding source for this version: $SourceUrl", 'Dictionary attribution: see NOTICE.txt and licenses directory.', 'No additional EULA restricts the freedoms granted by these licenses.') | Set-Content -LiteralPath (Join-Path $metadata 'SOURCE.txt') -Encoding UTF8
$argsList = @('/Qp', "/DAppVersion=$Version", "/DPublisher=$Publisher", "/DContact=$Contact", "/DSupportUrl=$SupportUrl", "/DSourceUrl=$SourceUrl")
if ($UnsignedDevelopment) {
    $argsList += '/DUnsignedDevelopment=1'
} elseif ($GitHubUnsignedRelease) {
    foreach ($file in Get-ChildItem $payload -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' }) {
        if ((Get-AuthenticodeSignature -LiteralPath $file.FullName).Status -ne 'NotSigned') { throw 'The explicitly unsigned GitHub route requires genuinely unsigned payloads.' }
    }
    $argsList += '/DGitHubUnsignedRelease=1'
} elseif ($ExternalSigning) {
    foreach ($file in Get-ChildItem $payload -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' }) {
        if ((Get-AuthenticodeSignature -LiteralPath $file.FullName).Status -ne 'Valid') { throw 'External signing requires already-signed, trusted payload PE files.' }
    }
    if (-not $SignedUninstallerDirectory -or -not (Test-Path $SignedUninstallerDirectory)) { throw 'Provide the existing exact pre-signed Inno uninstaller cache directory. See docs/signing.md for bootstrap.' }
    $SignedUninstallerDirectory = (Get-Item -LiteralPath $SignedUninstallerDirectory).FullName
    $cache = @(Get-ChildItem -LiteralPath $SignedUninstallerDirectory -File)
    if (-not $cache.Count) { throw 'Pre-signed uninstaller cache is empty.' }
    foreach ($file in $cache) {
        if ((Get-AuthenticodeSignature -LiteralPath $file.FullName).Status -ne 'Valid') { throw "Unsigned/invalid uninstaller cache: $($file.Name)" }
    }
    if ($SignedUninstallerDirectory -match '["{}\r\n]') { throw 'Unsupported uninstaller cache path.' }
    $argsList += @('/DExternalSigning=1', "/DExternalUninstallerDir=$SignedUninstallerDirectory")
} else {
    foreach ($file in Get-ChildItem $payload -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' }) {
        & (Join-Path $PSScriptRoot 'Sign-File.ps1') -Path $file.FullName
    }
    # Inno signs BOTH the generated uninstaller and outer installer with this tool.
    # Metadata is never concatenated into shell commands; only repository file paths are quoted.
    $signScript = Join-Path $PSScriptRoot 'Sign-File.ps1'
    $command = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File $q' + $signScript + '$q -Path $f'
    $argsList += "/Snumpad9=$command"
}
$argsList += (Join-Path $root 'packaging/Numpad9.iss')
& $Iscc @argsList
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
$suffix = if ($UnsignedDevelopment) { '-UNSIGNED-TESTING' } elseif ($GitHubUnsignedRelease) { '-UNSIGNED' } elseif ($ExternalSigning) { '-EXTERNAL-SIGNING-PENDING' } else { '-SIGNED-CANDIDATE' }
$installer = Join-Path $root "artifacts/Numpad9-$Version-x64$suffix.exe"
if (-not (Test-Path $installer)) { throw 'Expected installer was not generated.' }
if (-not $UnsignedDevelopment -and -not $GitHubUnsignedRelease -and -not $ExternalSigning) {
    if ((Get-AuthenticodeSignature $installer).Status -ne 'Valid') { throw 'Installer signature is invalid.' }
}
if ($GitHubUnsignedRelease -and (Get-AuthenticodeSignature -LiteralPath $installer).Status -ne 'NotSigned') { throw 'Unsigned release installer has an unexpected signature state.' }
Get-FileHash $installer -Algorithm SHA256
if ($GitHubUnsignedRelease) { Write-Host 'Unsigned GitHub installer built. Run the exact installer smoke and Prepare-GitHubRelease.ps1 before GitHub publication; this is not a Store installer.' }
else { Write-Host 'Installer is a candidate. Run Validate-Release.ps1 before submission.' }
if ($ExternalSigning) { Write-Host 'The OUTER installer is not signed yet. Sign it on Mac, then run the Windows release validator.' }
