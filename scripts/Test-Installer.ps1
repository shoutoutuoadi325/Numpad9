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
$languageSnapshot = [IO.Path]::GetTempFileName()
$languageRestored = $false
$inputServiceStates = @()
$ownedCtfmon = $null
$inputSessionRestored = $false

function Initialize-DisposableInputSession {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Numpad9CiInputSession {
    [DllImport("kernel32.dll")] public static extern uint GetActiveConsoleSessionId();
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] private static extern IntPtr GetThreadDesktop(uint thread);
    [DllImport("user32.dll")] private static extern IntPtr GetProcessWindowStation();
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool GetUserObjectInformationW(IntPtr handle, int index,
        StringBuilder text, uint bytes, out uint needed);
    private static string PublicName(IntPtr handle) {
        var text = new StringBuilder(256);
        uint needed;
        if (handle == IntPtr.Zero || !GetUserObjectInformationW(handle, 2, text, 512, out needed))
            return "unavailable";
        var name = text.ToString();
        return name == "WinSta0" || name == "Default" || name == "Winlogon" || name == "Disconnect"
            ? name : "non-default";
    }
    public static string Desktop() { return PublicName(GetThreadDesktop(GetCurrentThreadId())); }
    public static string Station() { return PublicName(GetProcessWindowStation()); }
}
'@
    $session = [Diagnostics.Process]::GetCurrentProcess().SessionId
    Write-Host ("Input fixture session={0}; activeConsoleSession={1}; station={2}; desktop={3}" -f
        $session, [Numpad9CiInputSession]::GetActiveConsoleSessionId(),
        [Numpad9CiInputSession]::Station(), [Numpad9CiInputSession]::Desktop())
    $existing = @(Get-Process -Name ctfmon -ErrorAction SilentlyContinue)
    foreach ($process in $existing) { Write-Host "Existing ctfmon PID=$($process.Id); session=$($process.SessionId)" }

    # This is diagnostic CI setup, not a product installer action. Microsoft
    # documents that a disabled input service can prevent ctfmon/framework startup:
    # https://learn.microsoft.com/en-us/troubleshoot/windows-client/shell-experience/troubleshoot-ime-common-issues
    foreach ($name in @('TextInputManagementService', 'TabletInputService')) {
        $service = Get-Service -Name $name -ErrorAction SilentlyContinue
        if (-not $service) { Write-Host "Input service $name is absent."; continue }
        $configuration = Get-CimInstance -ClassName Win32_Service -Filter "Name='$name'"
        if (-not $configuration) { throw "Cannot obtain original input service configuration: $name" }
        Write-Host "Input service $name; state=$($service.Status); startMode=$($configuration.StartMode)"
        if ($env:GITHUB_ACTIONS -ne 'true') { continue }
        $record = [pscustomobject]@{
            name = $name; startMode = [string]$configuration.StartMode;
            status = [string]$service.Status; startModeChanged = $false; startRequested = $false
        }
        $script:inputServiceStates += $record
        if ($service.Status -ne 'Stopped') { continue }
        if ($record.startMode -eq 'Disabled') {
            $record.startModeChanged = $true
            Set-Service -Name $name -StartupType Manual
        }
        $record.startRequested = $true
        Start-Service -Name $name
        $service.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Running, [TimeSpan]::FromSeconds(10))
        Write-Host "Started disposable input service $name."
    }
    if ($env:GITHUB_ACTIONS -ne 'true') { Write-Host 'Input-session mutation skipped outside GitHub Actions.'; return }
    $sameSession = @(Get-Process -Name ctfmon -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session })
    if (-not $sameSession.Count) {
        $official = Join-Path $env:SystemRoot 'System32/ctfmon.exe'
        if (-not (Test-Path -LiteralPath $official -PathType Leaf)) { throw 'Official Windows ctfmon.exe is unavailable.' }
        $script:ownedCtfmon = Start-Process -FilePath $official -PassThru
        Write-Host "Launched own ctfmon PID=$($script:ownedCtfmon.Id) for session=$session."
    }
    Start-Sleep -Milliseconds 500
    $ready = @(Get-Process -Name ctfmon -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session })
    if (-not $ready.Count) { throw 'No ctfmon process is running in the disposable test session.' }
    foreach ($process in $ready) { Write-Host "Ready ctfmon PID=$($process.Id); session=$($process.SessionId)" }
}

function Restore-DisposableInputSession {
    $errors = @()
    if ($script:ownedCtfmon) {
        try {
            $script:ownedCtfmon.Refresh()
            if (-not $script:ownedCtfmon.HasExited) {
                # Keep the original process object/handle; never kill by name,
                # select another session, or terminate an existing user process.
                $script:ownedCtfmon.Kill()
                if (-not $script:ownedCtfmon.WaitForExit(5000)) { throw 'Own ctfmon did not exit.' }
            }
        } catch { $errors += "Own ctfmon cleanup failed: $($_.Exception.Message)" }
    }
    foreach ($record in $script:inputServiceStates) {
        try {
            if ($record.startRequested) {
                $service = Get-Service -Name $record.name
                if ($service.Status -ne 'Stopped') {
                    Stop-Service -Name $record.name
                    $service.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped, [TimeSpan]::FromSeconds(10))
                }
            }
        } catch { $errors += "Input service state restoration failed ($($record.name)): $($_.Exception.Message)" }
        try {
            if ($record.startModeChanged) {
                $mode = switch ($record.startMode) {
                    'Disabled' { 'Disabled' }
                    'Manual' { 'Manual' }
                    'Auto' { 'Automatic' }
                    default { throw 'Unsupported original service startup mode.' }
                }
                Set-Service -Name $record.name -StartupType $mode
            }
            $actual = Get-CimInstance -ClassName Win32_Service -Filter "Name='$($record.name)'"
            $service = Get-Service -Name $record.name
            if ($actual.StartMode -ne $record.startMode -or [string]$service.Status -ne $record.status) {
                throw 'Original startup mode and service state were not restored.'
            }
        } catch { $errors += "Input service configuration restoration failed ($($record.name)): $($_.Exception.Message)" }
    }
    if ($errors.Count) { throw ($errors -join '; ') }
    $script:inputSessionRestored = $true
}

function Invoke-DisposableLanguageSetup([string]$Mode) {
    # Run the International module in Windows PowerShell so its native language
    # list type never crosses PowerShell 7 compatibility/remoting serialization.
    $languageScript = @'
param([string]$Mode, [string]$SnapshotPath, [string]$OwnTip)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Import-Module International
function Restore-List([object[]]$Records) {
    if (-not $Records.Count) { throw 'Original language list was empty.' }
    $result = New-WinUserLanguageList -Language $Records[0].LanguageTag
    foreach ($record in $Records | Select-Object -Skip 1) { $result.Add([string]$record.LanguageTag) }
    for ($i = 0; $i -lt $Records.Count; ++$i) {
        $result[$i].InputMethodTips.Clear()
        foreach ($tip in $Records[$i].InputMethodTips) { $result[$i].InputMethodTips.Add([string]$tip) }
        foreach ($property in $Records[$i].WritableProperties.PSObject.Properties) {
            $result[$i].($property.Name) = $property.Value
        }
    }
    return ,$result
}
if ($Mode -eq 'Enroll') {
    $original = Get-WinUserLanguageList
    $records = @(foreach ($language in $original) {
        $editable = [ordered]@{}
        foreach ($property in $language.GetType().GetProperties() | Where-Object { $_.GetSetMethod($false) -and $_.Name -notin @('LanguageTag', 'InputMethodTips') }) {
            $editable[$property.Name] = $property.GetValue($language, $null)
        }
        [pscustomobject]@{ LanguageTag = $language.LanguageTag; InputMethodTips = @($language.InputMethodTips); WritableProperties = [pscustomobject]$editable }
    })
    if (-not $records.Count) { throw 'Current user language list was empty.' }
    $records | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $SnapshotPath -Encoding UTF8
    # Deserialize the saved snapshot to avoid mutating any captured objects.
    $records = @(Get-Content -Raw -LiteralPath $SnapshotPath | ConvertFrom-Json)
    $english = @($records | Where-Object { $_.LanguageTag -like 'en-*' } | Select-Object -First 1)
    if ($english.Count) {
        $ordered = @($english[0]) + @($records | Where-Object { $_.LanguageTag -ne $english[0].LanguageTag })
        $test = Restore-List $ordered
    } else {
        $test = New-WinUserLanguageList -Language 'en-US'
        $rest = Restore-List $records
        foreach ($language in $rest) { $test.Add($language) }
    }
    # Windows normalizes zh-CN to the canonical zh-Hans-CN on readback.
    if (-not @($test | Where-Object { $_.LanguageTag -in @('zh-CN', 'zh-Hans-CN') }).Count) { $test.Add('zh-Hans-CN') }
    $chinese = @($test | Where-Object { $_.LanguageTag -in @('zh-CN', 'zh-Hans-CN') })[0]
    if ($OwnTip -notin @($chinese.InputMethodTips)) { $chinese.InputMethodTips.Add($OwnTip) }
    # Preferences only: no Install-Language, DISM, or language-pack download.
    Set-WinUserLanguageList -LanguageList $test -Force -WarningAction SilentlyContinue
    $actual = Get-WinUserLanguageList
    foreach ($language in $actual) {
        Write-Host ('CI language: ' + $language.LanguageTag + '; input profiles: ' + (@($language.InputMethodTips) -join ', '))
    }
    if ($actual[0].LanguageTag -notlike 'en-*') { throw 'CI language setup did not keep English first.' }
    $actualChinese = @($actual | Where-Object { $_.LanguageTag -in @('zh-CN', 'zh-Hans-CN') })
    if (-not $actualChinese.Count -or $OwnTip -notin @($actualChinese[0].InputMethodTips)) { throw 'Chinese preference or this exact Numpad9 TIP was not enrolled.' }
    Write-Host ('CI language tags: ' + (($actual | ForEach-Object { $_.LanguageTag }) -join ', '))
} elseif ($Mode -eq 'Restore') {
    $records = @(Get-Content -Raw -LiteralPath $SnapshotPath | ConvertFrom-Json)
    $restored = Restore-List $records
    Set-WinUserLanguageList -LanguageList $restored -Force -WarningAction SilentlyContinue
    $actual = Get-WinUserLanguageList
    if ($actual.Count -ne $records.Count) { throw 'Original language count was not restored.' }
    for ($i = 0; $i -lt $records.Count; ++$i) {
        if ($actual[$i].LanguageTag -ne $records[$i].LanguageTag -or
            (@($actual[$i].InputMethodTips) -join '|') -ne (@($records[$i].InputMethodTips) -join '|')) { throw 'Original language order/input preferences were not restored.' }
        foreach ($property in $records[$i].WritableProperties.PSObject.Properties) {
            if ($actual[$i].($property.Name) -ne $property.Value) { throw 'An original writable language preference was not restored.' }
        }
    }
    Write-Host ('Restored language tags: ' + (($actual | ForEach-Object { $_.LanguageTag }) -join ', '))
} else { throw 'Unknown disposable language setup mode.' }
'@
    $escapedSnapshot = $languageSnapshot.Replace("'", "''")
    $ownTip = "0804:$clsid$profile"
    $command = "& {`n$languageScript`n} -Mode '$Mode' -SnapshotPath '$escapedSnapshot' -OwnTip '$ownTip'"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    & (Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe') -NoLogo -NoProfile -NonInteractive -OutputFormat Text -EncodedCommand $encoded
    if ($LASTEXITCODE -ne 0) { throw "Disposable user-language $Mode failed: $LASTEXITCODE" }
}

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
        . (Join-Path $PSScriptRoot 'Capture-Window.ps1')
        $screenshots = Join-Path $root 'artifacts/screenshots'
        New-Item -ItemType Directory -Force -Path $screenshots | Out-Null
        $screenshot = Join-Path $screenshots 'main-window.png'
        $capture = Save-Numpad9WindowScreenshot -Window $hostProcess.MainWindowHandle -Path $screenshot -ToggleLanguage
        Write-Host "Captured the actual native window at $($capture.width) x $($capture.height) physical pixels."
    }
    Invoke-DisposableLanguageSetup 'Enroll'
    Initialize-DisposableInputSession
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
    try {
        if ((Get-Item -LiteralPath $languageSnapshot).Length -gt 0) { Invoke-DisposableLanguageSetup 'Restore'; $languageRestored = $true }
    } catch {
        $passed = $false
        $message = $_.Exception.Message
        $cleanupFailure = if ($cleanupFailure) { "$cleanupFailure; $message" } else { $message }
        Write-Warning "Disposable language restore failed: $message"
    } finally { Remove-Item -LiteralPath $languageSnapshot -Force }
    try { Restore-DisposableInputSession }
    catch {
        $passed = $false
        $message = $_.Exception.Message
        $cleanupFailure = if ($cleanupFailure) { "$cleanupFailure; $message" } else { $message }
        Write-Warning "Disposable input-session restore failed: $message"
    }
    [ordered]@{ version = $Version; installerSha256 = (Get-FileHash $Installer -Algorithm SHA256).Hash; os = [Environment]::OSVersion.VersionString; type = 'automated-cloud-installer-smoke'; passed = $passed; screenshot = $screenshot; scope = 'silent install, x64/x86 machine registration and TSF profile activation, native host window, uninstall'; physicalDeviceCompatibilityTest = $false; originalUserLanguagePreferencesRestored = $languageRestored; originalInputSessionRestored = $inputSessionRestored; cleanupFailure = $cleanupFailure } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts 'result.json') -Encoding UTF8
    if ($cleanupFailure -and -not $failure) { throw $cleanupFailure }
}
Write-Host 'Automated cloud installer/host/uninstall smoke passed. This is not a physical-device compatibility claim.'
