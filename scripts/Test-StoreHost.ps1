[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [string]$HostPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitOperatingSystem) { throw 'Store host onboarding smoke requires x64 Windows.' }
if (-not $HostPath) { $HostPath = Join-Path $root "build/windows-store/$Configuration/Numpad9Store.exe" }
$HostPath = (Get-Item -LiteralPath $HostPath).FullName
$hostHash = (Get-FileHash -LiteralPath $HostPath -Algorithm SHA256).Hash
$reportDirectory = Join-Path $root 'artifacts/store-host-smoke'
$screenshots = Join-Path $root 'artifacts/screenshots'
New-Item -ItemType Directory -Force -Path $reportDirectory, $screenshots | Out-Null
. (Join-Path $PSScriptRoot 'Capture-Window.ps1')
Initialize-Numpad9WindowCapture
$hostProcess = $null
$captures = @()
$captions = @()
$passed = $false
$failure = $null
$cleanupFailure = $null
$closedGracefully = $false
try {
    # Launch only the onboarding host. Do not invoke its enable-input control.
    $hostProcess = Start-Process -FilePath $HostPath -WorkingDirectory (Split-Path $HostPath -Parent) -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline) {
        $hostProcess.Refresh()
        if ($hostProcess.HasExited) { throw "Store host exited before creating onboarding UI: $($hostProcess.ExitCode)" }
        [Numpad9WindowCapture]::AssertNoDialogs($hostProcess.Id)
        if ($hostProcess.MainWindowHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 200
    }
    $window = $hostProcess.MainWindowHandle
    if ($window -eq [IntPtr]::Zero) { throw 'Store host did not create a real onboarding window.' }
    [Numpad9WindowCapture]::AssertMainWindow($window, $hostProcess.Id, 'Numpad9.StoreHost')
    $caption = [Numpad9WindowCapture]::Caption($window)
    if ($caption -ne 'Numpad9 · 九键输入') { throw "Unexpected initial Chinese onboarding caption: $caption" }
    $captions += $caption
    $captures += Save-Numpad9WindowScreenshot -Window $window -Path (Join-Path $screenshots 'store-host-zh.png')
    [Numpad9WindowCapture]::AssertNoDialogs($hostProcess.Id)

    # The actual language button command changes text only; it never enables input.
    $captures += Save-Numpad9WindowScreenshot -Window $window -Path (Join-Path $screenshots 'store-host-en.png') -ToggleLanguage
    [Numpad9WindowCapture]::AssertNoDialogs($hostProcess.Id)
    $caption = [Numpad9WindowCapture]::Caption($window)
    if ($caption -ne 'Numpad9 · Nine-key input') { throw "Unexpected English onboarding caption: $caption" }
    $captions += $caption
    $hostProcess.Refresh()
    if ($hostProcess.HasExited) { throw 'Store host unexpectedly exited during onboarding capture.' }
    $passed = $true
} catch {
    $failure = $_
    throw
} finally {
    try {
        if ($hostProcess) {
            $hostProcess.Refresh()
            if (-not $hostProcess.HasExited) {
                $hostProcess.CloseMainWindow() | Out-Null
                if (-not $hostProcess.WaitForExit(5000)) {
                    # Cleanup only the exact process started by this script.
                    Stop-Process -Id $hostProcess.Id -Force
                    $hostProcess.WaitForExit()
                    throw 'Onboarding host did not close gracefully.'
                }
                $closedGracefully = $true
                if ($hostProcess.ExitCode -ne 0) { throw "Onboarding shutdown returned $($hostProcess.ExitCode)." }
            }
        }
    } catch {
        $cleanupFailure = $_.Exception.Message
        $passed = $false
        Write-Warning $cleanupFailure
    }
    [ordered]@{
        type = 'automated-unpackaged-store-host-onboarding-smoke'; passed = $passed;
        hostPath = $HostPath; hostSha256 = $hostHash; os = [Environment]::OSVersion.VersionString;
        captions = $captions; screenshots = $captures; closedGracefully = $closedGracefully;
        scope = 'unpackaged WinRT startup and real Chinese/English onboarding UI only';
        packageInstalled = $false; inputActivated = $false; microsoftInputGrantValidated = $false;
        cleanupFailure = $cleanupFailure; failure = $(if ($failure) { $failure.Exception.Message } else { $null })
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportDirectory 'result.json') -Encoding UTF8
    if ($cleanupFailure -and -not $failure) { throw $cleanupFailure }
}
Write-Host 'Store host onboarding startup and real bilingual window capture passed. No input service was activated.'
