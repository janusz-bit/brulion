; Brulion for Windows - the Inno Setup 6 installer (docs/WINDOWS.md). Built by
; scripts/windows-build.ps1 (step "package"), which passes:
;   /DAppVersion=<x.y.z>     the project version (CMakeLists.txt)
;   /DSourceDir=<folder>     the deployed app (brulion.exe, brulion-cli.exe, Qt, the MSVC runtime)
;   /DOutputDir=<folder>     where Brulion-<version>-windows-x86_64-setup.exe goes
;
; Per user, without administrator rights: into %LOCALAPPDATA%\Programs\Brulion ({autopf} in the
; non-administrative install mode), a Start menu shortcut, a desktop icon on request, Polish and
; English. The uninstaller removes only what the installer put there: the notes (a folder the user
; chose, the sample notebook and the documentation in %APPDATA%\Brulion\Brulion), the settings
; (%APPDATA%\Brulion\Brulion.ini) and the event log (%LOCALAPPDATA%\Brulion) stay.

#ifndef AppVersion
  #error Pass /DAppVersion=<x.y.z> (scripts/windows-build.ps1 does)
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<the deployed app folder> (scripts/windows-build.ps1 does)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Fixed for good: Windows recognises the installed Brulion by it (updates, the uninstaller).
AppId={{5E3B8C1A-7F42-4D6B-9A0E-2C8D4B6F1E37}
AppName=Brulion
AppVersion={#AppVersion}
AppVerName=Brulion {#AppVersion}
AppPublisher=janusz-bit
AppPublisherURL=https://github.com/janusz-bit/brulion
AppSupportURL=https://github.com/janusz-bit/brulion/issues
AppUpdatesURL=https://github.com/janusz-bit/brulion/releases
AppCopyright=AGPL-3.0-or-later
VersionInfoVersion={#AppVersion}
VersionInfoProductName=Brulion
VersionInfoDescription=Brulion {#AppVersion} Setup
; Per user, no administrator rights; an administrator may still ask for all users (/ALLUSERS).
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
DefaultDirName={autopf}\Brulion
DefaultGroupName=Brulion
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Brulion-{#AppVersion}-windows-x86_64-setup
SetupIconFile={#SourcePath}\brulion.ico
UninstallDisplayIcon={app}\brulion.exe
UninstallDisplayName=Brulion
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=auto
; A running Brulion is asked to close before its files are replaced or removed.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; An update brings its own Qt Quick modules: no files of the previous version's are left among them.
Type: filesandordirs; Name: "{app}\qml"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Brulion"; Filename: "{app}\brulion.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Brulion"; Filename: "{app}\brulion.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\brulion.exe"; Description: "{cm:LaunchProgram,Brulion}"; Flags: nowait postinstall skipifsilent
