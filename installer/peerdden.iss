; PeerDen Inno Setup script
; Build: iscc installer\peerdden.iss
; Requires: Inno Setup 6+ from https://jrsoftware.org/isinfo.php

#define MyAppName "PeerDen"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "PeerDen"
#define MyAppURL "https://forums.peerden.io"
#define MyAppExeName "peerdden.exe"

[Setup]
AppId={{8B7D3A2E-4F1C-5E9A-B2D6-8C4E1F3A7B90}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=output
OutputBaseFilename=PeerDen-Setup-{#MyAppVersion}
SetupIconFile=..\build\peerdden.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1; Check: not IsAdminInstallMode

[Files]
; Assumes build output is in build\Release\ (run cmake --build first)
Source: "..\build\Release\peerdden.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\wintun.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\logo.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\icon.png"; DestDir: "{app}"; Flags: ignoreversion
; OpenSSL DLLs (copied by CMake when built with OpenSSL; skip if TLS not used)
Source: "..\build\Release\libssl-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\build\Release\libcrypto-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\build\Release\libssl-3.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\build\Release\libcrypto-3.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\build\Release\libssl-1_1-x64.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\build\Release\libcrypto-1_1-x64.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: quicklaunchicon

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    { Add firewall rule for ICMP so peers can ping each other on the virtual network }
    Exec(ExpandConstant('{sys}\cmd.exe'), '/c netsh advfirewall firewall add rule name="PeerDen TUN ICMP" dir=in protocol=icmpv4:8,0 action=allow', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"; ValueType: String; ValueName: "{app}\{#MyAppExeName}"; ValueData: "RUNASADMIN"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent runascurrentuser

[UninstallRun]
; Remove firewall rule added during install
Filename: "{sys}\cmd.exe"; Parameters: "/c netsh advfirewall firewall delete rule name=""PeerDen TUN ICMP"""; Flags: runhidden waituntilterminated

[UninstallDelete]
Type: dirifempty; Name: "{app}"
