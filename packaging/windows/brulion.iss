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
;
; Notifications: the Start menu shortcut carries the AppUserModelID JanuszBit.Brulion, which the app
; registers under HKCU\Software\Classes\AppUserModelId (src/platform/appidentity.h); the
; uninstaller removes that key. Start at sign-in: the task "autostart" (offered, checked, on the
; first install only) writes the value "Brulion" of HKCU\...\CurrentVersion\Run, the same the
; app's switch (Settings -> Application) writes (src/platform/autostart.h); an update leaves it as
; the user set it; the uninstaller removes it when it starts this installation's brulion.exe.
;
; "Otwórz za pomocą → Brulion" (docs/WINDOWS.md): always registered for .md, .markdown and .txt —
; the ProgID JanuszBit.Brulion.Note (its command `"…\brulion.exe" --open "%1"`), its name in each
; type's OpenWithProgids and Applications\brulion.exe with its SupportedTypes, under
; HKCU\Software\Classes (HKLM with /ALLUSERS). Offered, never the default: no type's default value
; or UserChoice is written. The uninstaller removes exactly these (a type's key only when nothing
; else is left in it). The portable ZIP registers nothing. UTF-8 with a BOM: the Polish texts below.

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
; Explorer learns of the "Open with" registration (and of its removal) at once.
ChangesAssociations=yes
RestartApplications=no

[Languages]
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
polish.AutostartTask=Uruchamiaj Brulion po zalogowaniu (ukryty w zasobniku, żeby przypomnienia dzwoniły)
english.AutostartTask=Start Brulion when I sign in (hidden in the tray, so reminders ring)
polish.NoteTypeName=Notatka (Brulion)
english.NoteTypeName=Note (Brulion)

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
; Reminders ring only while Brulion runs. Offered on the first install only: an update must not
; switch it on again after the user turned it off in the app or in Task Manager (UsePreviousTasks
; would select it again, whatever checkedonce says); from then on the app's switch decides.
Name: "autostart"; Description: "{cm:AutostartTask}"; Check: not IsUpdate

[InstallDelete]
; An update brings its own Qt Quick modules: no files of the previous version's are left among them.
Type: filesandordirs; Name: "{app}\qml"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Brulion"; ValueData: """{app}\brulion.exe"" --hidden"; Tasks: autostart
; Turned on here: Task Manager's "off" of an earlier entry goes too.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run"; ValueType: none; ValueName: "Brulion"; Flags: deletevalue dontcreatekey; Tasks: autostart
; The app writes it at every start (the toasts' name and icon); removed with the app.
Root: HKCU; Subkey: "Software\Classes\AppUserModelId\JanuszBit.Brulion"; Flags: uninsdeletekey dontcreatekey
; "Otwórz za pomocą → Brulion" for .md, .markdown and .txt (docs/WINDOWS.md): offered, never the
; default — no type's default value or UserChoice is written. Explorer starts one brulion.exe per
; selected file: the running one collects them in one share dialog (--open, main.cpp).
Root: HKA; Subkey: "Software\Classes\JanuszBit.Brulion.Note"; ValueType: string; ValueName: ""; ValueData: "{cm:NoteTypeName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\JanuszBit.Brulion.Note\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\brulion.exe"",0"
Root: HKA; Subkey: "Software\Classes\JanuszBit.Brulion.Note\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\brulion.exe"" --open ""%1"""
Root: HKA; Subkey: "Software\Classes\.md"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\.md\OpenWithProgids"; ValueType: string; ValueName: "JanuszBit.Brulion.Note"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\.markdown"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\.markdown\OpenWithProgids"; ValueType: string; ValueName: "JanuszBit.Brulion.Note"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\.txt"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\.txt\OpenWithProgids"; ValueType: string; ValueName: "JanuszBit.Brulion.Note"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\Applications\brulion.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Brulion"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\brulion.exe\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\brulion.exe"",0"
Root: HKA; Subkey: "Software\Classes\Applications\brulion.exe\SupportedTypes"; ValueType: string; ValueName: ".md"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\brulion.exe\SupportedTypes"; ValueType: string; ValueName: ".markdown"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\brulion.exe\SupportedTypes"; ValueType: string; ValueName: ".txt"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\brulion.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\brulion.exe"" --open ""%1"""

[Icons]
Name: "{autoprograms}\Brulion"; Filename: "{app}\brulion.exe"; WorkingDir: "{app}"; AppUserModelID: "JanuszBit.Brulion"
Name: "{autodesktop}\Brulion"; Filename: "{app}\brulion.exe"; WorkingDir: "{app}"; AppUserModelID: "JanuszBit.Brulion"; Tasks: desktopicon

[Run]
Filename: "{app}\brulion.exe"; Description: "{cm:LaunchProgram,Brulion}"; Flags: nowait postinstall skipifsilent

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  ApprovedKey = 'Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run';
  // The uninstall entry Setup writes for the AppId above (per user or for all users).
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{5E3B8C1A-7F42-4D6B-9A0E-2C8D4B6F1E37}_is1';

// Brulion is installed already: this setup updates it.
function IsUpdate: Boolean;
begin
  Result := RegKeyExists(HKCU, UninstallKey) or RegKeyExists(HKLM, UninstallKey);
end;

// The start at sign-in goes with this installation (also when the app's switch wrote it); an
// entry for another copy (a portable one) stays.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command: String;
begin
  if CurUninstallStep = usUninstall then
  begin
    if RegQueryStringValue(HKCU, RunKey, 'Brulion', Command) and
       (Pos(Lowercase(ExpandConstant('{app}\brulion.exe')), Lowercase(Command)) > 0) then
    begin
      RegDeleteValue(HKCU, RunKey, 'Brulion');
      RegDeleteValue(HKCU, ApprovedKey, 'Brulion');
    end;
  end;
end;
