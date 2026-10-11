; Inno Setup script for Merge (built by CI: iscc /DAppVersion=x.y.z /DBuildDir=... /DSamplesDir=... merge.iss)

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\Merge_artefacts\Release"
#endif
#ifndef SamplesDir
  #define SamplesDir "..\build\Samples"
#endif

[Setup]
AppId={{8F6C2E54-3B1A-4C8E-9C2F-6D5E4B3A2F10}
AppName=Merge
AppVersion={#AppVersion}
AppVerName=Merge {#AppVersion}
AppPublisher=Merge contributors
AppPublisherURL=https://github.com/crescentfreshhh/merge
DefaultDirName={autopf}\Merge
DefaultGroupName=Merge
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=Merge-Setup-{#AppVersion}
SetupIconFile=merge.ico
UninstallDisplayIcon={app}\Merge.exe
LicenseFile=..\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
PrivilegesRequiredOverridesAllowed=dialog

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#BuildDir}\Merge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SamplesDir}\*"; DestDir: "{app}\Samples"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"
Source: "..\resources\fonts\OFL.txt"; DestDir: "{app}"; DestName: "Inter-OFL.txt"

[Icons]
Name: "{autoprograms}\Merge"; Filename: "{app}\Merge.exe"
Name: "{autodesktop}\Merge"; Filename: "{app}\Merge.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.merge"; ValueType: string; ValueName: ""; ValueData: "Merge.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Merge.Project"; ValueType: string; ValueName: ""; ValueData: "Merge Project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Merge.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Merge.exe,0"
Root: HKA; Subkey: "Software\Classes\Merge.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Merge.exe"" ""%1"""

[Run]
Filename: "{app}\Merge.exe"; Description: "Launch Merge"; Flags: nowait postinstall skipifsilent
