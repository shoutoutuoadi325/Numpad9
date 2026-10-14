[CmdletBinding()]
param([Parameter(Mandatory)][string]$Path, [string]$SignTool = $env:NUMPAD9_SIGNTOOL)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'Signing requires Windows.' }
if (-not $SignTool -or -not (Test-Path $SignTool)) { throw 'Set NUMPAD9_SIGNTOOL to Windows SDK signtool.exe.' }
$thumbprint = $env:NUMPAD9_CERT_THUMBPRINT
$timestamp = $env:NUMPAD9_TIMESTAMP_URL
if ($thumbprint -notmatch '^[a-fA-F0-9]{40}$') { throw 'Set NUMPAD9_CERT_THUMBPRINT to the exact code-signing certificate thumbprint.' }
if (-not $timestamp -or $timestamp -notmatch '^https?://') { throw 'Set NUMPAD9_TIMESTAMP_URL to your CA RFC3161 timestamp endpoint.' }
$cert = Get-Item "Cert:\CurrentUser\My\$thumbprint"
if (-not $cert.HasPrivateKey -or $cert.NotAfter -le (Get-Date)) { throw 'Code-signing certificate has no private key or is expired.' }
if ($cert.Subject -eq $cert.Issuer) { throw 'Self-signed development certificates cannot produce a Store release.' }
if (-not (@($cert.EnhancedKeyUsageList | ForEach-Object { $_.ObjectId.Value }) -contains '1.3.6.1.5.5.7.3.3')) { throw 'Code Signing EKU is required.' }
& $SignTool sign /sha1 $thumbprint /fd SHA256 /tr $timestamp /td SHA256 $Path
if ($LASTEXITCODE -ne 0) { throw "Signing failed: $Path" }
& $SignTool verify /pa /all /v $Path
if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $Path" }
$signature = Get-AuthenticodeSignature -LiteralPath $Path
if ($signature.Status -ne 'Valid' -or -not $signature.TimeStamperCertificate) { throw 'Valid timestamped signature is required.' }
