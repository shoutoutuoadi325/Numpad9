[CmdletBinding()]
param(
    [string]$Version = '0.1.0',
    [string]$Installer,
    [string]$Configuration = 'Release',
    [switch]$SkipScreenshot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitOperatingSystem) { throw 'Installer automation requires x64 Windows.' }
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run on an elevated disposable Windows CI machine.' }
if (-not $Installer) { $Installer = Join-Path $root "artifacts/Numpad9-$Version-x64-UNSIGNED-TESTING.exe" }
$Installer = (Get-Item -LiteralPath $Installer).FullName
$dir64 = Join-Path $env:ProgramFiles 'Numpad9'
$dir32 = Join-Path ${env:ProgramFiles(x86)} 'Numpad9'
if ((Test-Path $dir64) -or (Test-Path $dir32)) { throw 'Installer smoke requires a clean machine without an existing Numpad9 install.' }
$artifacts = Join-Path $root 'artifacts/installer-smoke'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$clsid = '{E03FE2C4-24B9-4C4E-B605-88717E6A4D49}'
$profile = '{859113AF-D94C-4FD7-8870-9B1F607E6896}'
$comPath = "SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
$tipPath = "SOFTWARE\Microsoft\CTF\TIP\$clsid"
$hostProcess = $null
$installed = $false
$uninstalled = $false
$passed = $false
$screenshot = $null
$failure = $null

function Read-Inproc([Microsoft.Win32.RegistryView]$View) {
    $machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', $View)
    try {
        $key = $machine.OpenSubKey($comPath)
        if (-not $key) { return $null }
        try { return $key.GetValue('') } finally { $key.Dispose() }
    } finally { $machine.Dispose() }
}

function Find-Profile([Microsoft.Win32.RegistryKey]$Key, [int]$Depth = 0) {
    if ($Depth -gt 4) { return $false }
    foreach ($name in $Key.GetSubKeyNames()) {
        if ($name -eq $profile) { return $true }
        $child = $Key.OpenSubKey($name)
        try { if ($child -and (Find-Profile $child ($Depth + 1))) { return $true } }
        finally { if ($child) { $child.Dispose() } }
    }
    return $false
}

function Check-Profile([Microsoft.Win32.RegistryView]$View) {
    $machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', $View)
    try {
        $key = $machine.OpenSubKey($tipPath)
        if (-not $key) { return $false }
        try { return Find-Profile $key } finally { $key.Dispose() }
    } finally { $machine.Dispose() }
}

function Stop-OwnHost {
    if ($script:hostProcess) {
        $script:hostProcess.Refresh()
        if (-not $script:hostProcess.HasExited) {
            $script:hostProcess.CloseMainWindow() | Out-Null
            if (-not $script:hostProcess.WaitForExit(5000)) {
                # Only terminate the process this test itself launched.
                Stop-Process -Id $script:hostProcess.Id -Force
                $script:hostProcess.WaitForExit()
            }
        }
    }
}

function Uninstall-OwnPackage {
    $uninstaller = Join-Path $dir64 'unins000.exe'
    if (Test-Path $uninstaller) {
        $log = Join-Path $artifacts 'uninstall.log'
        $process = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', ('/LOG="' + $log + '"')) -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "Silent uninstall failed: $($process.ExitCode)" }
    }
}

try {
    $log = Join-Path $artifacts 'install.log'
    $process = Start-Process -FilePath $Installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/RESTARTEXITCODE=3010', ('/LOG="' + $log + '"')) -Wait -PassThru
    $installed = Test-Path (Join-Path $dir64 'unins000.exe')
    if ($process.ExitCode -ne 0) { throw "Silent install failed or unexpectedly needs reboot: $($process.ExitCode)" }
    foreach ($pair in @(@('Registry64', $dir64), @('Registry32', $dir32))) {
        $expected = [IO.Path]::GetFullPath((Join-Path $pair[1] "versions/$Version/Numpad9Ime.dll"))
        if ((Read-Inproc $pair[0]) -ne $expected) { throw "Wrong machine COM registration for $($pair[0])." }
        if (-not (Check-Profile $pair[0])) { throw "Same TSF language profile missing for $($pair[0])." }
        if (-not (Test-Path (Join-Path $pair[1] "versions/$Version/lexicon.tsv"))) { throw 'Installed dictionary missing.' }
    }
    $uninstallKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{FF2A0BD5-637C-4CC6-874D-8E91E7B0849F}_is1'
    $metadata = Get-ItemProperty -LiteralPath $uninstallKey
    if ($metadata.DisplayName -ne 'Numpad9' -or $metadata.DisplayVersion -ne $Version -or -not $metadata.Publisher) { throw 'Programs-list metadata is missing or wrong.' }
    $hostProcess = Start-Process -FilePath (Join-Path $dir64 "versions/$Version/Numpad9.exe") -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline) {
        $hostProcess.Refresh()
        if ($hostProcess.HasExited) { throw "Host exited before creating a window: $($hostProcess.ExitCode)" }
        if ($hostProcess.MainWindowHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 200
    }
    if ($hostProcess.MainWindowHandle -eq [IntPtr]::Zero) { throw 'Host did not create a real native window.' }
    if (-not $SkipScreenshot) {
        Add-Type -AssemblyName System.Drawing
        $drawingReferences = @([Drawing.Bitmap].Assembly.Location, [Drawing.Size].Assembly.Location)
        # PowerShell/.NET 9+ forwards drawing interfaces to these assemblies.
        $drawingReferences += @(Get-ChildItem (Join-Path (Split-Path ([Drawing.Bitmap].Assembly.Location) -Parent) 'System.Private.Windows.*.dll') | ForEach-Object { $_.FullName })
        Add-Type -ReferencedAssemblies $drawingReferences @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class Numpad9WindowCapture {
  [DllImport("kernel32.dll")] private static extern void Sleep(uint milliseconds);
  [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll", SetLastError=true)] private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
  [DllImport("user32.dll", SetLastError=true)] private static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int width, int height, uint flags);
  [DllImport("user32.dll", SetLastError=true)] private static extern bool GetWindowRect(IntPtr h, out Rect rect);
  [DllImport("user32.dll", SetLastError=true)] private static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] private static extern bool RedrawWindow(IntPtr h, IntPtr rect, IntPtr region, uint flags);
  [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr h, uint message, IntPtr wp, IntPtr lp);
  public static int[] Capture(IntPtr window, string path) {
    // Keep resize, measurement and rendering on this one DPI-aware thread.
    IntPtr previous = SetThreadDpiAwarenessContext(new IntPtr(-4)); // PER_MONITOR_AWARE_V2
    if (previous == IntPtr.Zero) throw new InvalidOperationException("Window API failed with Win32 error " + Marshal.GetLastWin32Error());
    try {
      if (!SetWindowPos(window, IntPtr.Zero, 0, 0, 1366, 900, 0x0040))
        throw new InvalidOperationException("Window API failed with Win32 error " + Marshal.GetLastWin32Error());
      SendMessage(window, 0x0111, new IntPtr(106), IntPtr.Zero); // Actual app English control.
      RedrawWindow(window, IntPtr.Zero, IntPtr.Zero, 0x0581);
      Sleep(500);
      Rect rect;
      if (!GetWindowRect(window, out rect)) throw new InvalidOperationException("Window API failed with Win32 error " + Marshal.GetLastWin32Error());
      int width = rect.Right - rect.Left, height = rect.Bottom - rect.Top;
      if (width != 1366 || height != 900)
        throw new InvalidOperationException("Requested physical window 1366x900, observed " + width + "x" + height +
          " at rectangle (" + rect.Left + "," + rect.Top + ")-(" + rect.Right + "," + rect.Bottom + ").");
      using (Bitmap bitmap = new Bitmap(width, height)) {
        using (Graphics graphics = Graphics.FromImage(bitmap)) {
          IntPtr dc = graphics.GetHdc();
          try {
            if (!PrintWindow(window, dc, 2)) throw new InvalidOperationException("Window API failed with Win32 error " + Marshal.GetLastWin32Error());
          } finally { graphics.ReleaseHdc(dc); }
        }
        int first = bitmap.GetPixel(width / 2, height / 2).ToArgb();
        bool different = false;
        for (int x = 25; x < width; x += 35)
          for (int y = 25; y < height; y += 25)
            if (bitmap.GetPixel(x, y).ToArgb() != first) different = true;
        if (!different) throw new InvalidOperationException("Captured window appears blank.");
        bitmap.Save(path, ImageFormat.Png);
      }
      return new int[] { width, height };
    } finally { SetThreadDpiAwarenessContext(previous); }
  }
}
'@
        $screenshots = Join-Path $root 'artifacts/screenshots'
        New-Item -ItemType Directory -Force -Path $screenshots | Out-Null
        $screenshot = Join-Path $screenshots 'main-window.png'
        $dimensions = [Numpad9WindowCapture]::Capture($hostProcess.MainWindowHandle, $screenshot)
        Write-Host "Captured the actual native window at $($dimensions[0]) x $($dimensions[1]) physical pixels."
    }
    foreach ($pair in @(@('x64', $dir64), @('x86', $dir32))) {
        $arch = $pair[0]
        $smoke = Join-Path $root "build/windows-$arch/$Configuration/ime_smoke.exe"
        if (-not (Test-Path -LiteralPath $smoke)) { throw "Missing $arch registered-profile smoke executable: $smoke" }
        $ime = [IO.Path]::GetFullPath((Join-Path $pair[1] "versions/$Version/Numpad9Ime.dll"))
        $smokeLog = Join-Path $artifacts "registered-$arch.log"
        & $smoke $ime '--registered' 2>&1 | Tee-Object -FilePath $smokeLog
        if ($LASTEXITCODE -ne 0) { throw "Actual TSF registered-profile smoke failed for $arch (exit $LASTEXITCODE). See $smokeLog" }
    }
    Stop-OwnHost
    Uninstall-OwnPackage
    $uninstalled = $true
    foreach ($view in @('Registry64', 'Registry32')) {
        if (Read-Inproc $view) { throw "COM registration remained after uninstall: $view" }
        if (Check-Profile $view) { throw "TSF profile remained after uninstall: $view" }
    }
    if ((Test-Path $dir64) -or (Test-Path $dir32) -or (Test-Path $uninstallKey)) { throw 'Installed files or Programs-list entry remained after clean uninstall.' }
    $shortcut = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'Numpad9.lnk'
    if (Test-Path $shortcut) { throw 'Start-menu shortcut remained after uninstall.' }
    $passed = $true
} catch {
    $failure = $_
    throw
} finally {
    $cleanupFailure = $null
    try {
        Stop-OwnHost
        if ($installed -and -not $uninstalled) { Uninstall-OwnPackage }
    } catch {
        $cleanupFailure = $_.Exception.Message
        $passed = $false
        Write-Warning "Own-package cleanup failed: $cleanupFailure"
    }
    [ordered]@{ version = $Version; installerSha256 = (Get-FileHash $Installer -Algorithm SHA256).Hash; os = [Environment]::OSVersion.VersionString; type = 'automated-cloud-installer-smoke'; passed = $passed; screenshot = $screenshot; scope = 'silent install, x64/x86 machine registration and TSF profile activation, native host window, uninstall'; physicalDeviceCompatibilityTest = $false; cleanupFailure = $cleanupFailure } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts 'result.json') -Encoding UTF8
    if ($cleanupFailure -and -not $failure) { throw $cleanupFailure }
}
Write-Host 'Automated cloud installer/host/uninstall smoke passed. This is not a physical-device compatibility claim.'
