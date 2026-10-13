; Inno Setup 6.3+; all payloads are offline, built with the static C++ runtime.
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Publisher
  #error Publisher is required. Use scripts/Build-Installer.ps1.
#endif
#ifndef Contact
  #error Contact is required. Use scripts/Build-Installer.ps1.
#endif
#ifndef SourceUrl
  #error SourceUrl is required. Use scripts/Build-Installer.ps1.
#endif
#ifdef UnsignedDevelopment
  #define ArtifactSuffix "-UNSIGNED-TESTING"
#else
  #ifdef ExternalSigning
    #define ArtifactSuffix "-EXTERNAL-SIGNING-PENDING"
  #else
    #define ArtifactSuffix "-SIGNED-CANDIDATE"
  #endif
#endif

[Setup]
AppId={{FF2A0BD5-637C-4CC6-874D-8E91E7B0849F}
AppName=Numpad9
AppVersion={#AppVersion}
AppPublisher={#Publisher}
AppContact={#Contact}
AppSupportURL={#SourceUrl}
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany={#Publisher}
VersionInfoDescription=Numpad9 phone-style numeric keypad input method
VersionInfoProductName=Numpad9
VersionInfoProductVersion={#AppVersion}
DefaultDirName={commonpf64}\Numpad9
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
DefaultGroupName=Numpad9
PrivilegesRequired=admin
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.17763
OutputDir=..\artifacts
OutputBaseFilename=Numpad9-{#AppVersion}-x64{#ArtifactSuffix}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\assets\Numpad9.ico
LicenseFile=..\LICENSE
UninstallDisplayIcon={app}\versions\{#AppVersion}\Numpad9.exe
UninstallDisplayName=Numpad9
CloseApplications=no
RestartApplications=no
RestartIfNeededByRun=no
SetupLogging=yes
#ifdef UnsignedDevelopment
SignedUninstaller=no
#else
#ifndef ExternalSigning
SignTool=numpad9
#endif
SignedUninstaller=yes
#ifdef ExternalUninstallerDir
SignedUninstallerDir={#ExternalUninstallerDir}
#else
SignedUninstallerDir=..\artifacts\signed-uninstaller
#endif
#endif

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
#if FileExists(CompilerPath + "Languages\ChineseSimplified.isl")
Name: "zhcn"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
#endif

[Files]
Source: "..\release\x64\Numpad9.exe"; DestDir: "{app}\versions\{#AppVersion}"; Flags: ignoreversion uninsrestartdelete
Source: "..\release\x64\Numpad9Ime.dll"; DestDir: "{app}\versions\{#AppVersion}"; Flags: ignoreversion uninsrestartdelete
Source: "..\release\x64\lexicon.tsv"; DestDir: "{app}\versions\{#AppVersion}"; Flags: ignoreversion uninsrestartdelete
Source: "..\release\x86\Numpad9Ime.dll"; DestDir: "{commonpf32}\Numpad9\versions\{#AppVersion}"; Flags: ignoreversion uninsrestartdelete
Source: "..\release\x86\lexicon.tsv"; DestDir: "{commonpf32}\Numpad9\versions\{#AppVersion}"; Flags: ignoreversion uninsrestartdelete
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\artifacts\metadata\privacy-policy.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\artifacts\metadata\SOURCE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\data\NOTICE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\data\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\data\SOURCES.json"; DestDir: "{app}\licenses"; Flags: ignoreversion; AfterInstall: RegisterTextServices

[Icons]
Name: "{commonprograms}\Numpad9"; Filename: "{app}\versions\{#AppVersion}\Numpad9.exe"; WorkingDir: "{app}\versions\{#AppVersion}"
Name: "{commonprograms}\Numpad9 license"; Filename: "{app}\LICENSE"

[Code]
const
  ComKey = 'Software\Classes\CLSID\{E03FE2C4-24B9-4C4E-B605-88717E6A4D49}\InprocServer32';
var
  Old32, Old64: String;
  RegistrationStarted, RegistrationFailed, InstallComplete: Boolean;

function Dll32: String;
begin
  Result := ExpandConstant('{commonpf32}\Numpad9\versions\{#AppVersion}\Numpad9Ime.dll');
end;

function Dll64: String;
begin
  Result := ExpandConstant('{app}\versions\{#AppVersion}\Numpad9Ime.dll');
end;

function RegisteredPath(Root: Integer): String;
begin
  Result := '';
  RegQueryStringValue(Root, ComKey, '', Result);
end;

function RunRegistration(Path: String; Is32, Unregister: Boolean): Boolean;
var
  ExePath, Args: String;
  ExitCode: Integer;
begin
  Result := False;
  if not FileExists(Path) then Exit;
  if Is32 then ExePath := ExpandConstant('{syswow64}\regsvr32.exe')
  else ExePath := ExpandConstant('{sys}\regsvr32.exe');
  Args := '/s ';
  if Unregister then Args := Args + '/u ';
  Args := Args + '"' + Path + '"';
  Result := Exec(ExePath, Args, '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
  if Result then Result := (ExitCode = 0);
  if not Result then Log('Text service registration failed: ' + Path);
end;

procedure RestoreOldRegistration;
begin
  { Unregister before re-registering old paths: both versions share one profile. }
  RunRegistration(Dll32, True, True);
  RunRegistration(Dll64, False, True);
  if Old32 <> '' then
    if not RunRegistration(Old32, True, False) then Log('ROLLBACK FAILED: restore x86 registration manually.');
  if Old64 <> '' then
    if not RunRegistration(Old64, False, False) then Log('ROLLBACK FAILED: restore x64 registration manually.');
  RegistrationStarted := False;
end;

function NextVersionPart(var Value: String): Integer;
var
  Dot: Integer;
  Part: String;
begin
  Dot := Pos('.', Value);
  if Dot = 0 then begin
    Part := Value;
    Value := '';
  end else begin
    Part := Copy(Value, 1, Dot - 1);
    Delete(Value, 1, Dot);
  end;
  Result := StrToIntDef(Part, -1);
end;

function IsDowngrade(NewVersion, OldPath: String): Boolean;
var
  OldVersion: String;
  NewPart, OldPart, Index: Integer;
begin
  Result := False;
  if OldPath = '' then Exit;
  OldVersion := ExtractFileName(RemoveBackslash(ExtractFileDir(OldPath)));
  for Index := 1 to 3 do begin
    NewPart := NextVersionPart(NewVersion);
    OldPart := NextVersionPart(OldVersion);
    if (NewPart < 0) or (OldPart < 0) then Exit;
    if NewPart < OldPart then begin Result := True; Exit; end;
    if NewPart > OldPart then Exit;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if CompareText(RemoveBackslash(ExpandConstant('{app}')), ExpandConstant('{commonpf64}\Numpad9')) <> 0 then begin
    Result := 'Numpad9 must be installed in its standard Program Files directory for Windows app-container compatibility.';
    Exit;
  end;
  Old32 := RegisteredPath(HKLM32);
  Old64 := RegisteredPath(HKLM64);
  if IsDowngrade('{#AppVersion}', Old32) or IsDowngrade('{#AppVersion}', Old64) then begin
    Result := 'A newer Numpad9 version is registered. Uninstall it before installing an older version.';
    Exit;
  end;
  { Never overwrite a version folder while a process may have its DLL loaded.
    Fix/rebuild releases must receive a NEW version. }
  if DirExists(ExpandConstant('{app}\versions\{#AppVersion}')) or
     DirExists(ExpandConstant('{commonpf32}\Numpad9\versions\{#AppVersion}')) then
    Result := 'This version is already installed. Uninstall it, close applications using Numpad9, and restart Windows if requested before reinstalling. Updates must use a new version.';
end;

procedure RegisterTextServices;
begin
  { Execute inside [Files] installation, while Inno can still roll back files.
    ssPostInstall would run after the install transaction has already committed. }
  RegistrationStarted := True;
  if not RunRegistration(Dll32, True, False) then begin
    RegistrationFailed := True;
    RestoreOldRegistration;
    Log('INSTALL FAILED: x86 text service registration; previous registration restored where possible.');
    Abort;
  end;
  if not RunRegistration(Dll64, False, False) then begin
    RegistrationFailed := True;
    RestoreOldRegistration;
    Log('INSTALL FAILED: x64 text service registration; previous registration restored where possible.');
    Abort;
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  Result := 0;
  if RegistrationFailed then Result := 1603;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssDone then InstallComplete := True;
end;

procedure DeinitializeSetup;
begin
  if RegistrationStarted and not InstallComplete then RestoreOldRegistration;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path32, Path64: String;
  Unregistered: Boolean;
begin
  { InitializeUninstall precedes user confirmation; never unregister there. }
  if CurUninstallStep <> usUninstall then Exit;
  Unregistered := True;
  Path32 := RegisteredPath(HKLM32);
  Path64 := RegisteredPath(HKLM64);
  { An older uninstaller must never remove a newer version's shared TSF profile. }
  if (Path32 <> '') and (CompareText(Path32, Dll32) <> 0) then Exit;
  if (Path64 <> '') and (CompareText(Path64, Dll64) <> 0) then Exit;
  if (Path32 <> '') and not RunRegistration(Path32, True, True) then Unregistered := False;
  if (Path64 <> '') and not RunRegistration(Path64, False, True) then Unregistered := False;
  if not Unregistered then begin
    { Preserve a usable install if any unregister operation fails. }
    if Path32 <> '' then RunRegistration(Path32, True, False);
    if Path64 <> '' then RunRegistration(Path64, False, False);
    Log('Uninstall aborted because TSF unregister failed; registration restored where possible.');
    Abort;
  end;
end;
