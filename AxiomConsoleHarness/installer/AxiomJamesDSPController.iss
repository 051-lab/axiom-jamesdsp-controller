#define MyAppName "JamesDSP Controller"
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0-dev"
#endif
#define MyAppPublisher "JamesDSP"
#define MyAppExeName "JamesDSPController.exe"
#define PackageDir "..\dist\JamesDSPController-win-x64"
#define AppIconFile "..\Assets\axiom-controller.ico"

[Setup]
AppId={{8D92EB73-FEE5-4E5B-B0AD-12439B16090A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\JamesDSP Controller
DefaultGroupName=JamesDSP
DisableProgramGroupPage=yes
OutputDir=..\dist\installer
OutputBaseFilename=JamesDSPController-{#MyAppVersion}-win-x64-setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} Installer
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
WizardStyle=modern
SetupIconFile={#AppIconFile}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
Name: "autostart"; Description: "Start JamesDSP Controller when I sign in"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: files; Name: "{app}\AxiomJamesDSPController.exe"
Type: files; Name: "{app}\AxiomJamesDSPController.dll"
Type: files; Name: "{app}\AxiomJamesDSPController.deps.json"
Type: files; Name: "{app}\AxiomJamesDSPController.runtimeconfig.json"
Type: files; Name: "{app}\AxiomJamesDSPController.Core.dll"
Type: files; Name: "{app}\AxiomJamesDSPConsole.exe"
Type: files; Name: "{app}\Launch Axiom JamesDSP Controller.cmd"
Type: files; Name: "{app}\axiom-liveprog-test.ini"
Type: files; Name: "{autodesktop}\Axiom JamesDSP Controller.lnk"
Type: files; Name: "{commonstartup}\Axiom JamesDSP Controller.lnk"
Type: files; Name: "{group}\Axiom JamesDSP Controller.lnk"

[Icons]
Name: "{group}\JamesDSP Controller"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\JamesDSP Controller"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{commonstartup}\JamesDSP Controller"; Filename: "{app}\{#MyAppExeName}"; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch JamesDSP Controller"; Flags: nowait postinstall skipifsilent

[Code]
function InitializeUninstall(): Boolean;
begin
  Result := True;
  MsgBox(
    'JamesDSP Controller application files will be removed. Profiles, settings, diagnostics, and runtime data under Local AppData will be preserved.',
    mbInformation,
    MB_OK);
end;
