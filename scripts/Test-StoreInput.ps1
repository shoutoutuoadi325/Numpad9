[CmdletBinding()]
param([switch]$HarnessOnly, [string]$PackagePath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT' -or [Environment]::OSVersion.Version.Build -lt 26100) { throw 'This check requires Windows build 26100 or later.' }
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'This disposable-package probe runs only on an isolated GitHub Actions runner.' }
$root = Split-Path $PSScriptRoot -Parent
$privateDirectory = Join-Path $root 'private/store-probe'
$reportDirectory = Join-Path $root 'artifacts/store-input-probe'
New-Item -ItemType Directory -Force -Path $privateDirectory, $reportDirectory | Out-Null
# A real STA Win32 text host in this process, separate from the production app.
# SendInput is used only by this test driver, never by the application.
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
public static class Numpad9StoreInputProbe {
    public static IntPtr Window, Edit;
    static Thread thread;
    static Exception failure;
    static readonly ManualResetEvent ready = new ManualResetEvent(false);
    delegate IntPtr WndProc(IntPtr w,uint m,IntPtr p,IntPtr l);
    static readonly WndProc proc = Procedure;
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct WndClass {
        public uint style; public WndProc procedure; public int classExtra,windowExtra;
        public IntPtr instance,icon,cursor,brush; public string menu,name;
    }
    [StructLayout(LayoutKind.Sequential)] struct Point { public int x,y; }
    [StructLayout(LayoutKind.Sequential)] struct Message {
        public IntPtr window; public uint message; public UIntPtr wparam; public IntPtr lparam;
        public uint time; public Point point; public uint privateValue;
    }
    [StructLayout(LayoutKind.Sequential)] struct Keyboard {
        public ushort vk,scan; public uint flags,time; public UIntPtr extra;
    }
    [StructLayout(LayoutKind.Sequential)] struct Mouse {
        public int dx,dy; public uint data,flags,time; public UIntPtr extra;
    }
    [StructLayout(LayoutKind.Explicit)] struct InputData {
        [FieldOffset(0)] public Keyboard key; [FieldOffset(0)] public Mouse mouse;
    }
    [StructLayout(LayoutKind.Sequential)] struct Input { public uint type; public InputData data; }
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern ushort RegisterClassW(ref WndClass value);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateWindowExW(uint ex,string name,string text,uint style,int x,int y,int w,int h,IntPtr parent,IntPtr id,IntPtr instance,IntPtr parameter);
    [DllImport("user32.dll")] static extern IntPtr DefWindowProcW(IntPtr w,uint m,IntPtr p,IntPtr l);
    [DllImport("user32.dll")] static extern int GetMessageW(out Message message,IntPtr window,uint min,uint max);
    [DllImport("user32.dll")] static extern bool TranslateMessage(ref Message message);
    [DllImport("user32.dll")] static extern IntPtr DispatchMessageW(ref Message message);
    [DllImport("user32.dll")] static extern void PostQuitMessage(int code);
    [DllImport("user32.dll")] static extern bool DestroyWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window,int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern IntPtr SetFocus(IntPtr window);
    [DllImport("user32.dll")] static extern IntPtr GetFocus();
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr window,uint message,IntPtr wparam,IntPtr lparam);
    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr window,uint message,IntPtr wparam,IntPtr lparam);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr window,StringBuilder text,int length);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool SetWindowTextW(IntPtr window,string text);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window,int id);
    [DllImport("user32.dll",SetLastError=true)] static extern uint SendInput(uint count,Input[] inputs,int size);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr LoadLibraryW(string name);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);
    [DllImport("kernel32.dll")] static extern uint WTSGetActiveConsoleSessionId();
    [DllImport("ole32.dll")] static extern int CoInitializeEx(IntPtr reserved,uint mode);
    [DllImport("ole32.dll")] static extern void CoUninitialize();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
    public static string Read(IntPtr window) { var text=new StringBuilder(1024); GetWindowTextW(window,text,text.Capacity); return text.ToString(); }
    static IntPtr Procedure(IntPtr w,uint m,IntPtr p,IntPtr l) {
        if(m==0x8001) { SetForegroundWindow(w); SetFocus(Edit); return IntPtr.Zero; }
        if(m==0x10) { DestroyWindow(w); return IntPtr.Zero; }
        if(m==2) { PostQuitMessage(0); return IntPtr.Zero; }
        return DefWindowProcW(w,m,p,l);
    }
    public static void Start() {
        thread=new Thread(()=>{
            bool com=false;
            try {
                int hr=CoInitializeEx(IntPtr.Zero,2); if(hr<0) Marshal.ThrowExceptionForHR(hr); com=true;
                if(LoadLibraryW("Msftedit.dll")==IntPtr.Zero) throw new Win32Exception();
                var cls=new WndClass { procedure=proc,instance=GetModuleHandleW(null),name="Numpad9.PrivateInputProbe" };
                if(RegisterClassW(ref cls)==0) throw new Win32Exception();
                Window=CreateWindowExW(0,cls.name,"Numpad9 input probe",0x10CF0000,60,60,800,400,IntPtr.Zero,IntPtr.Zero,cls.instance,IntPtr.Zero);
                Edit=CreateWindowExW(0,"RICHEDIT50W","",0x50A11004,20,20,720,280,Window,new IntPtr(501),cls.instance,IntPtr.Zero);
                if(Window==IntPtr.Zero || Edit==IntPtr.Zero) throw new Win32Exception();
                SendMessageW(Edit,0x04CC,new IntPtr(0x00010000),new IntPtr(0x00010000)); // EM_SETEDITSTYLE, SES_USECTF
                ready.Set();
                Message message; int result;
                while((result=GetMessageW(out message,IntPtr.Zero,0,0))>0) { TranslateMessage(ref message); DispatchMessageW(ref message); }
                if(result<0) throw new Win32Exception();
            } catch(Exception error) { failure=error; ready.Set(); }
            finally { if(com) CoUninitialize(); }
        });
        thread.SetApartmentState(ApartmentState.STA); thread.Start();
        if(!ready.WaitOne(10000)) throw new Exception("Probe window did not become ready.");
        if(failure!=null) throw failure;
    }
    public static void Focus() {
        ShowWindow(Window,5);
        var deadline=DateTime.UtcNow.AddSeconds(3);
        do {
            SendMessageW(Window,0x8001,IntPtr.Zero,IntPtr.Zero);
            if(GetForegroundWindow()==Window) return;
            Thread.Sleep(50);
        } while(DateTime.UtcNow<deadline);
        throw new Exception("The test text window is not foreground; no keys sent. Runner session="+
            System.Diagnostics.Process.GetCurrentProcess().SessionId+", console session="+
            WTSGetActiveConsoleSessionId()+", foreground present="+(GetForegroundWindow()!=IntPtr.Zero));
    }
    public static void Key(ushort scan) {
        if(GetForegroundWindow()!=Window) throw new Exception("Focus changed; no keys sent to another application.");
        var inputs=new Input[2];
        inputs[0].type=1; inputs[0].data.key=new Keyboard{scan=scan,flags=8};
        inputs[1].type=1; inputs[1].data.key=new Keyboard{scan=scan,flags=10};
        if(SendInput(2,inputs,Marshal.SizeOf(typeof(Input)))!=2) throw new Win32Exception();
    }
    public static void Clear() { SetWindowTextW(Edit,""); }
    public static void Stop() {
        if(Window!=IntPtr.Zero) PostMessageW(Window,0x10,IntPtr.Zero,IntPtr.Zero);
        if(thread!=null && !thread.Join(5000)) throw new Exception("Probe thread did not close.");
        Window=Edit=IntPtr.Zero;
        if(failure!=null) throw failure;
    }
}
'@
$hostProcess = $null
$certificate = $null
$installed = $false
$passed = $false
$cleanupPassed = $true
$phase = 'harness'
$failureMessage = $null
$identity = Get-Content -Raw -LiteralPath (Join-Path $root 'packaging/msix/store-identity.json') | ConvertFrom-Json
try {
    [Numpad9StoreInputProbe]::Start()
    [Numpad9StoreInputProbe]::Focus()
    [Numpad9StoreInputProbe]::Key(0x1E) # Known harmless 'a', only in our own field.
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([Numpad9StoreInputProbe]::Read([Numpad9StoreInputProbe]::Edit).ToLowerInvariant() -ne 'a' -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    if ([Numpad9StoreInputProbe]::Read([Numpad9StoreInputProbe]::Edit).ToLowerInvariant() -ne 'a') { throw 'The real foreground keyboard/text harness did not receive ordinary input.' }
    [Numpad9StoreInputProbe]::Clear()
    if (-not $HarnessOnly) {
        $phase = 'install'
        if (-not $PackagePath) { throw 'A private real-identity package is required.' }
        if (Get-AppxPackage -Name $identity.identityName) { throw 'Refuse to replace a preexisting package.' }
        $PackagePath = (Get-Item -LiteralPath $PackagePath).FullName
        $certificate = New-SelfSignedCertificate -Type Custom -Subject $identity.publisherDN -CertStoreLocation Cert:\CurrentUser\My -KeyUsage DigitalSignature -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3','2.5.29.19={text}')
        $publicCertificate = Join-Path $privateDirectory 'runner.cer'
        Export-Certificate -Cert $certificate -FilePath $publicCertificate | Out-Null
        Import-Certificate -FilePath $publicCertificate -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
        $signTool = @(Get-ChildItem "${env:ProgramFiles(x86)}/Windows Kits/10/bin/*/x64/signtool.exe" | Sort-Object FullName -Descending)[0].FullName
        & $signTool sign /sha1 $certificate.Thumbprint /fd SHA256 $PackagePath
        if ($LASTEXITCODE -ne 0) { throw 'The temporary runner package signing failed.' }
        Add-AppxPackage -Path $PackagePath
        $installed = $true
        $package = Get-AppxPackage -Name $identity.identityName
        if (-not $package -or $package.PackageFamilyName -ne $identity.packageFamilyName) { throw 'Installed package family does not match the granted identity.' }
        $phase = 'activate'
        $launchTime = Get-Date
        Start-Process explorer.exe -ArgumentList "shell:AppsFolder\$($identity.packageFamilyName)!App" | Out-Null
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        do {
            $hostProcess = @(Get-Process Numpad9Store -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $package.InstallLocation 'Numpad9Store.exe') -and $_.StartTime -ge $launchTime.AddSeconds(-1) -and $_.MainWindowHandle -ne [IntPtr]::Zero }) | Select-Object -First 1
            if (-not $hostProcess) { Start-Sleep -Milliseconds 200 }
        } while (-not $hostProcess -and [DateTime]::UtcNow -lt $deadline)
        if (-not $hostProcess) { throw 'The production packaged host did not open.' }
        $enable = [Numpad9StoreInputProbe]::GetDlgItem($hostProcess.MainWindowHandle,101)
        if ($enable -eq [IntPtr]::Zero) { throw 'Production enable-input control was not found.' }
        [Numpad9StoreInputProbe]::SendMessageW($enable,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([Numpad9StoreInputProbe]::Read($enable) -ne '停用九键输入') { throw 'The production input service did not start with the grant and derived Windows profile.' }
        $phase = 'cross-process-input'
        [Numpad9StoreInputProbe]::Focus()
        foreach ($scan in @(0x4D,0x4B,0x4B,0x48,0x4D,0x39)) { [Numpad9StoreInputProbe]::Key([ushort]$scan); Start-Sleep -Milliseconds 120 }
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while ([Numpad9StoreInputProbe]::Read([Numpad9StoreInputProbe]::Edit) -ne '你好' -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
        if ([Numpad9StoreInputProbe]::Read([Numpad9StoreInputProbe]::Edit) -ne '你好') { throw 'The production app did not commit the expected nine-key candidate in the separate text application.' }
        $phase = 'stop'
        [Numpad9StoreInputProbe]::SendMessageW($enable,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
        if ([Numpad9StoreInputProbe]::Read($enable) -ne '启用九键输入') { throw 'The input service did not stop.' }
        [Numpad9StoreInputProbe]::Clear(); [Numpad9StoreInputProbe]::Focus(); [Numpad9StoreInputProbe]::Key(0x1E)
        Start-Sleep -Milliseconds 200
        if ([Numpad9StoreInputProbe]::Read([Numpad9StoreInputProbe]::Edit).ToLowerInvariant() -ne 'a') { throw 'Ordinary text input did not resume after stopping.' }
    }
    $passed = $true
} catch { $failureMessage = $_.Exception.Message; throw }
finally {
    $cleanupActions = @(
        { if ($hostProcess -and -not $hostProcess.HasExited) { Stop-Process -Id $hostProcess.Id; $hostProcess.WaitForExit(5000) | Out-Null } },
        { [Numpad9StoreInputProbe]::Stop() },
        { if ($installed) { Get-AppxPackage -Name $identity.identityName | Remove-AppxPackage } },
        { if ($certificate) { $path = "Cert:/LocalMachine/TrustedPeople/$($certificate.Thumbprint)"; if (Test-Path $path) { Remove-Item $path } } },
        { if ($certificate) { $path = "Cert:/CurrentUser/My/$($certificate.Thumbprint)"; if (Test-Path $path) { Remove-Item $path } } }
    )
    foreach ($cleanupAction in $cleanupActions) {
        try { & $cleanupAction }
        catch { $cleanupPassed = $false; $passed = $false; Write-Warning 'Isolated runner cleanup failed.' }
    }
    if (Test-Path $privateDirectory) { Remove-Item -LiteralPath $privateDirectory -Recurse -Force }
    # Only this allowlisted report is uploaded. No grant/config/package/text.
    [ordered]@{ passed=$passed; phase=$phase; harnessOnly=[bool]$HarnessOnly; build=[Environment]::OSVersion.Version.Build;
        productionPackage=[bool](-not $HarnessOnly); crossProcessCommitVerified=($passed -and -not $HarnessOnly);
        cleanupPassed=$cleanupPassed; failure=$failureMessage } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reportDirectory 'result.json') -Encoding UTF8
    if (-not $cleanupPassed -and -not $failureMessage) { throw 'Isolated runner cleanup did not pass.' }
}
Write-Host 'Store input probe passed within its reported scope.'
