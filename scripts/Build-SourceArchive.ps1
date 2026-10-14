[CmdletBinding()]
param([string]$Version = '0.1.0')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be numeric major.minor.patch.' }
$root = Split-Path $PSScriptRoot -Parent
$folders = @('src', 'data', 'tools', 'tests', 'scripts', 'packaging', 'docs', '.github', 'assets')
$files = @('CMakeLists.txt', 'README.md', 'LICENSE', '.gitignore') | ForEach-Object { Get-Item -LiteralPath (Join-Path $root $_) -Force }
foreach ($folder in $folders) {
    $path = Join-Path $root $folder
    if (Test-Path $path) { $files += Get-ChildItem -LiteralPath $path -Recurse -File -Force | Where-Object { $_.FullName -notmatch '[\\/](__pycache__|node_modules|\.venv)[\\/]' -and $_.Extension -ne '.pyc' -and $_.Name -ne 'input-access.json' } }
}
New-Item -ItemType Directory -Force -Path (Join-Path $root 'artifacts') | Out-Null
$out = Join-Path $root "artifacts/Numpad9-$Version-source.zip"
Add-Type -AssemblyName System.IO.Compression
$stream = [IO.File]::Open($out, [IO.FileMode]::Create)
$zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $files | Sort-Object FullName -Unique) {
        $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
        $entry = $zip.CreateEntry("Numpad9-$Version/$relative", [IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = [DateTimeOffset]::new(2026, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
        $input = [IO.File]::OpenRead($file.FullName)
        $output = $entry.Open()
        try { $input.CopyTo($output) } finally { $output.Dispose(); $input.Dispose() }
    }
} finally { $zip.Dispose(); $stream.Dispose() }
Get-FileHash $out -Algorithm SHA256
Write-Host 'Publish this version-matched complete source archive alongside the installer and retain original dictionary notices.'
