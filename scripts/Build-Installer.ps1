[CmdletBinding()]
param(
    [string]$Version = '0.1.0',
    [string]$Publisher,
    [string]$Contact,
    [string]$SourceUrl,
    [string]$Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    [switch]$UnsignedDevelopment,
    [switch]$ExternalSigning,
    [string]$SignedUninstallerDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if ($UnsignedDevelopment -and $ExternalSigning) { throw 'Choose unsigned development OR external signing.' }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be numeric major.minor.patch.' }
if (-not (Test-Path $Iscc)) { throw 'Install Inno Setup 6.3+ or specify -Iscc.' }
foreach ($value in @($Publisher, $Contact, $SourceUrl)) {
    if ($value -and ($value.Contains('"') -or $value.Contains('{') -or $value.Contains('}') -or $value.Contains("`n") -or $value.Contains("`r"))) { throw 'Metadata cannot contain quotes, braces, or newlines.' }
}
if ($UnsignedDevelopment) {
    if (-not $Publisher) { $Publisher = 'Unsigned development build' }
    if (-not $Contact) { $Contact = 'Development build - not for Store submission' }
    if (-not $SourceUrl) { $SourceUrl = 'https://www.gnu.org/licenses/gpl-3.0.html' }
} elseif (-not $Publisher -or -not $Contact -or $SourceUrl -notmatch '^https://') {
    throw 'Signed builds require your real Publisher, Contact, and HTTPS corresponding-source URL.'
}
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
$policy = Get-Content -Raw -LiteralPath (Join-Path $root 'docs/store/privacy-policy.md')
$policy = $policy.Replace('- 真实发行人 / Legal publisher: **REQUIRED BEFORE PUBLICATION**', "- 真实发行人 / Legal publisher: $Publisher")
$policy = $policy.Replace('- 隐私及支持联系方式 / Privacy and support contact: **REQUIRED BEFORE PUBLICATION**', "- 隐私及支持联系方式 / Privacy and support contact: $Contact")
$policy = $policy.Replace('This file is a publication draft until those two fields are completed. The release validator requires a real hosted policy URL and contact; publishing this unresolved draft is not acceptable.', 'Publisher and support details above apply to this installer build.')
$policy = $policy.Replace('发布前必须在本文末尾填写真实发行人和联系方式，并将完整内容发布到可公开访问的 HTTPS 网页。以下描述针对本仓库的离线版本；功能改变时需要重新审核和更新。', '以下描述针对本版本的离线功能；功能改变时需要重新审核和更新。')
$policy | Set-Content -LiteralPath (Join-Path $metadata 'privacy-policy.md') -Encoding UTF8
@("Numpad9 $Version", 'Application license: GNU GPL version 3.', "Full corresponding source for this version: $SourceUrl", 'Dictionary attribution: see NOTICE.txt and licenses directory.', 'No additional EULA restricts the freedoms granted by these licenses.') | Set-Content -LiteralPath (Join-Path $metadata 'SOURCE.txt') -Encoding UTF8
$argsList = @('/Qp', "/DAppVersion=$Version", "/DPublisher=$Publisher", "/DContact=$Contact", "/DSourceUrl=$SourceUrl")
if ($UnsignedDevelopment) {
    $argsList += '/DUnsignedDevelopment=1'
} elseif ($ExternalSigning) {
    foreach ($file in Get-ChildItem $payload -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' }) {
        if ((Get-AuthenticodeSignature -LiteralPath $file.FullName).Status -ne 'Valid') { throw 'External signing requires already-signed, trusted payload PE files.' }
    }
    if (-not $SignedUninstallerDirectory -or -not (Test-Path $SignedUninstallerDirectory)) { throw 'Provide the existing exact pre-signed Inno uninstaller cache directory. See mac-signing.md for bootstrap.' }
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
$suffix = if ($UnsignedDevelopment) { '-UNSIGNED-TESTING' } elseif ($ExternalSigning) { '-EXTERNAL-SIGNING-PENDING' } else { '-SIGNED-CANDIDATE' }
$installer = Join-Path $root "artifacts/Numpad9-$Version-x64$suffix.exe"
if (-not (Test-Path $installer)) { throw 'Expected installer was not generated.' }
if (-not $UnsignedDevelopment -and -not $ExternalSigning) {
    if ((Get-AuthenticodeSignature $installer).Status -ne 'Valid') { throw 'Installer signature is invalid.' }
}
Get-FileHash $installer -Algorithm SHA256
Write-Host 'Installer is a candidate. Run Validate-Release.ps1 before submission.'
if ($ExternalSigning) { Write-Host 'The OUTER installer is not signed yet. Sign it on Mac, then run the Windows release validator.' }
