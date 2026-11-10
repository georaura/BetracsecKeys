; Instalador de BeTracks Keys para Windows (Inno Setup 6). Lo compila .github/workflows/windows.yml:
;   ISCC /DAppVersion=0.1.0 /DBuildDir=<...\BetracsecKeys_artefacts\Release> BetracsecKeys.iss
; Instala la app en Archivos de programa y el VST3 en la carpeta común de VST3, que es donde lo buscan los DAW.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\win\BetracsecKeys_artefacts\Release"
#endif

[Setup]
AppId={{6C1E8D52-4B7A-4E0F-9C55-2F8B1E3A7D10}
AppName=BeTracks Keys
AppVersion={#AppVersion}
AppPublisher=BeTracks
AppPublisherURL=https://betracksec.com/keys
DefaultDirName={autopf}\BeTracks Keys
DefaultGroupName=BeTracks Keys
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputBaseFilename=BeTracksKeys-{#AppVersion}-Windows
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Components]
Name: "app"; Description: "BeTracks Keys (aplicación)"; Types: full custom; Flags: fixed
Name: "vst3"; Description: "Plugin VST3 (para tu DAW)"; Types: full custom

[Files]
Source: "{#BuildDir}\Standalone\BeTracks Keys.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#BuildDir}\VST3\BeTracks Keys.vst3\*"; DestDir: "{commoncf64}\VST3\BeTracks Keys.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\BeTracks Keys"; Filename: "{app}\BeTracks Keys.exe"
Name: "{autodesktop}\BeTracks Keys"; Filename: "{app}\BeTracks Keys.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Run]
Filename: "{app}\BeTracks Keys.exe"; Description: "{cm:LaunchProgram,BeTracks Keys}"; Flags: nowait postinstall skipifsilent
