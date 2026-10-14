[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][string]$InstalledRoot,
    [Parameter(Mandatory)][string]$Publisher,
    [string]$CertificateSubjectName,
    [Parameter(Mandatory)][string]$Contact,
    [string]$PrivacyUrl,
    [string]$PolicyTextPath,
    [Parameter(Mandatory)][string]$InstallerUrl,
    [Parameter(Mandatory)][string]$SourceUrl,
    [string]$QaReport,
    [Parameter(Mandatory)][string[]]$TrustedProgramRootThumbprints,
    [string]$Version = '0.1.0',
    [string]$SignTool = $env:NUMPAD9_SIGNTOOL,
    [string]$ReportPath = 'artifacts/release-validation.json'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if (-not $CertificateSubjectName) { $CertificateSubjectName = $Publisher }
if ($env:OS -ne 'Windows_NT') { throw 'Release validation must run on Windows.' }
if (-not $Publisher -or -not $Contact -or $Publisher -match '(?i)placeholder|unsigned|development|example') { throw 'Real publisher and support contact are required.' }
if (-not $PrivacyUrl -and -not $PolicyTextPath) { throw 'Provide PrivacyUrl OR PolicyTextPath containing the actual privacy text submitted in Partner Center.' }
foreach ($url in @($InstallerUrl, $SourceUrl) + @($PrivacyUrl | Where-Object { $_ })) {
    if ($url -notmatch '^https://' -or $url -match '(?i)example\.(com|org)|localhost|REPLACE|YOUR_') { throw 'Installer/source URLs and any supplied privacy URL must be real HTTPS addresses.' }
}
$policyText = $null
if ($PolicyTextPath) {
    $policyFile = Get-Item -LiteralPath $PolicyTextPath
    if ($policyFile.PSIsContainer) { throw 'PolicyTextPath must identify a text file.' }
    $policyContent = Get-Content -Raw -Encoding UTF8 -LiteralPath $policyFile.FullName
    if ([string]::IsNullOrWhiteSpace($policyContent) -or $policyContent -match '(?i)REQUIRED BEFORE PUBLICATION|REPLACE[_ ]?ME|YOUR_[A-Z_]+|<[^>]*(真实|填写|placeholder)[^>]*>') { throw 'Submitted policy text must be nonempty and have no unresolved placeholders.' }
    $policyText = [ordered]@{ path = $policyFile.FullName; sha256 = (Get-FileHash -LiteralPath $policyFile.FullName -Algorithm SHA256).Hash; source = 'Partner Center privacy-policy text' }
}
if ($InstallerUrl -notmatch ([regex]::Escape($Version)) -or $InstallerUrl -notmatch '\.exe($|\?)') { throw 'Installer URL must be versioned and point to an EXE.' }
if (-not $SignTool -or -not (Test-Path $SignTool)) { throw 'Set NUMPAD9_SIGNTOOL to Windows SDK signtool.exe.' }
$payload = Join-Path $root 'release'
$pe = @()
foreach ($item in @(@('x64/Numpad9.exe', 0x8664), @('x64/Numpad9Ime.dll', 0x8664), @('x86/Numpad9Ime.dll', 0x014c))) {
    $path = Join-Path $payload $item[0]
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw "Invalid PE: $path" }
    $offset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($offset -lt 0 -or $offset + 6 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes, $offset) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes, $offset + 4) -ne $item[1]) { throw "Wrong PE architecture: $path" }
    $fileVersion = (Get-Item $path).VersionInfo.ProductVersion
    if ($fileVersion -ne $Version -and $fileVersion -ne "$Version.0") { throw "PE product version does not match release: $path ($fileVersion)" }
}
foreach ($arch in @('x64', 'x86')) {
    if (-not (Test-Path (Join-Path $payload "$arch/lexicon.tsv"))) { throw "Missing $arch dictionary." }
}
$uninstallers = @(Get-ChildItem -LiteralPath $InstalledRoot -Filter 'unins*.exe' -File)
if ($uninstallers.Count -ne 1) { throw 'Point InstalledRoot at the installed Numpad9 root containing exactly one generated uninstaller.' }
$x86Installed = Join-Path ${env:ProgramFiles(x86)} 'Numpad9'
if (-not (Test-Path "$InstalledRoot/versions/$Version/Numpad9Ime.dll") -or -not (Test-Path "$x86Installed/versions/$Version/Numpad9Ime.dll")) { throw 'Installed x86/x64 IME payload missing.' }
foreach ($relative in @('Numpad9.exe', 'Numpad9Ime.dll', 'lexicon.tsv')) {
    if ((Get-FileHash "$InstalledRoot/versions/$Version/$relative").Hash -ne (Get-FileHash "$payload/x64/$relative").Hash) { throw "Installed x64 $relative differs from staged release." }
}
foreach ($relative in @('Numpad9Ime.dll', 'lexicon.tsv')) {
    if ((Get-FileHash "$x86Installed/versions/$Version/$relative").Hash -ne (Get-FileHash "$payload/x86/$relative").Hash) { throw "Installed x86 $relative differs from staged release." }
}
if ((Get-Content -Raw "$InstalledRoot/privacy-policy.md") -match 'REQUIRED BEFORE PUBLICATION') { throw 'Installed privacy policy still has unresolved publisher/contact.' }
$pe = @((Get-ChildItem $payload -Recurse -File), (Get-ChildItem $InstalledRoot -Recurse -File), (Get-ChildItem $x86Installed -Recurse -File), (Get-Item $Installer)) |
    ForEach-Object { $_ } | Where-Object { $_.Extension -in '.exe', '.dll' } | Sort-Object FullName -Unique
$signatures = foreach ($file in $pe) {
    & $SignTool verify /pa /all /v $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "Signature check failed: $($file.FullName)" }
    $sig = Get-AuthenticodeSignature -LiteralPath $file.FullName
    if ($sig.Status -ne 'Valid' -or -not $sig.TimeStamperCertificate -or $sig.SignerCertificate.Subject -eq $sig.SignerCertificate.Issuer) { throw "Trusted timestamped signature required: $($file.FullName)" }
    $chain = [Security.Cryptography.X509Certificates.X509Chain]::new()
    $chain.ChainPolicy.RevocationMode = 'Online'
    $chain.ChainPolicy.VerificationFlags = 'NoFlag'
    if (-not $chain.Build($sig.SignerCertificate)) { throw "Certificate chain/revocation check failed: $($file.FullName)" }
    $rootThumbprint = $chain.ChainElements[$chain.ChainElements.Count - 1].Certificate.Thumbprint
    if ($TrustedProgramRootThumbprints -notcontains $rootThumbprint) { throw 'Signing root must match the independently verified Microsoft Trusted Root Program allowlist.' }
    if ($sig.SignerCertificate.GetNameInfo([Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false) -ne $CertificateSubjectName) { throw 'Signature certificate subject does not match the explicitly expected signer.' }
    [ordered]@{ path = $file.FullName; sha256 = (Get-FileHash $file.FullName -Algorithm SHA256).Hash; signer = $sig.SignerCertificate.Subject; root = $rootThumbprint }
    $chain.Dispose()
}
$qa = $null
$installerHash = (Get-FileHash $Installer -Algorithm SHA256).Hash
if ((Get-Item $Installer).VersionInfo.CompanyName -ne $Publisher) { throw 'Installer Publisher metadata differs from the application publisher.' }
if ((Get-Item $Installer).VersionInfo.ProductVersion -ne $Version) { throw 'Installer product version mismatch.' }
if ((Get-Item $uninstallers[0].FullName).VersionInfo.CompanyName -ne $Publisher) { throw 'Uninstaller Publisher metadata differs from the application publisher.' }
$downloaded = [IO.Path]::GetTempFileName()
try {
    Invoke-WebRequest -UseBasicParsing -Uri $InstallerUrl -OutFile $downloaded | Out-Null
    if ((Get-FileHash $downloaded -Algorithm SHA256).Hash -ne $installerHash) { throw 'Hosted installer is different from the tested signed package.' }
    if ($PrivacyUrl) {
        $policyPage = Invoke-WebRequest -UseBasicParsing -Uri $PrivacyUrl
        if ($policyPage.Content -match 'REQUIRED BEFORE PUBLICATION' -or $policyPage.Content -notmatch [regex]::Escape($Publisher) -or $policyPage.Content -notmatch [regex]::Escape($Contact)) { throw 'Hosted policy must contain real publisher/contact and no unresolved placeholders.' }
    }
    Invoke-WebRequest -UseBasicParsing -Method Head -Uri $SourceUrl | Out-Null
} finally { Remove-Item -LiteralPath $downloaded -Force }
if ($QaReport) {
    $qa = Get-Content -Raw -Encoding UTF8 -LiteralPath $QaReport | ConvertFrom-Json
    if ($qa.version -ne $Version -or $qa.installerSha256 -ne $installerHash) { throw 'Optional QA report must match the exact signed installer version and SHA256.' }
    foreach ($test in $qa.tests) {
        if ($test.status -eq 'passed' -and -not $test.evidence) { throw 'Optional passing QA claims require actual evidence.' }
    }
}
$report = [ordered]@{ status = 'submission-candidate'; version = $Version; publisher = $Publisher; expectedCertificateSubjectName = $CertificateSubjectName; contact = $Contact; privacyUrl = $PrivacyUrl; policyText = $policyText; installerUrl = $InstallerUrl; sourceUrl = $SourceUrl; checkedAtUtc = [DateTime]::UtcNow.ToString('o'); files = @($signatures); qa = $qa; certification = 'Microsoft certification not yet performed' }
$reportFull = Join-Path $root $ReportPath
New-Item -ItemType Directory -Force -Path (Split-Path $reportFull -Parent) | Out-Null
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $reportFull -Encoding UTF8
Write-Host "Submission candidate validated: $reportFull. This does not establish Microsoft certification."
