# Brulion on Windows (test version)

Since version 0.4.5 Brulion has a **test version** for Windows 10 and 11 (x86_64): the same program as on
Linux (C++20 + Qt 6.11.2, Qt Quick), built with the MSVC 2022 compiler. It is in every release
in two forms:

| File | What it is |
|---|---|
| `Brulion-<version>-windows-x86_64-setup.exe` | installer (Inno Setup) — installs for the current user, without administrator rights |
| `brulion-<version>-windows-x86_64.zip` | portable version — a `Brulion` folder with `brulion.exe`, `brulion-cli.exe`, the Qt libraries and the MSVC runtime |

"Test version" means: it builds and passes the tests in CI, but it has not yet been used day to day for long,
the files are not signed, and it has the limitations described below. Since version 0.5.0 reminders and alarms are
native Windows notifications with buttons, and Brulion can start when you sign in. Report bugs in the
[issues](https://github.com/janusz-bit/brulion/issues) with the event log (*Settings → Application →
Event log*; how to send it: the README's Polish section
["Gdy coś nie działa"](../README.md#gdy-coś-nie-działa--rejestr-zdarzeń), "Troubleshooting").

## Installation

* **Installer:** run `Brulion-<version>-windows-x86_64-setup.exe`. The program goes to
  `%LOCALAPPDATA%\Programs\Brulion`, a shortcut to the Start menu, a desktop icon on request (the
  "Create a desktop shortcut" task) and — only on the first install, checked — "Start Brulion
  when I sign in" (below, "Autostart"). Brulion also appears in **Open with** for
  `.md`, `.markdown` and `.txt` files, without changing the default program (below, "Open with"). The installer's
  language: Polish or English (according to the system).
  A new version installs over the previous one (the same `AppId`); a running Brulion is asked
  to close.
* **SmartScreen:** the files are not signed with a certificate, so Windows warns on the first
  run: "Windows protected your PC" → **More info** → **Run anyway**.
  The SHA-256 sums are in the release's `SHA256SUMS.txt`.
* **Portable version:** unpack the ZIP anywhere and run `Brulion\brulion.exe`. Nothing
  is installed; notes and settings are in the same places as with the installer (below), not
  in the program folder.
* **Uninstalling:** Settings → Apps → Brulion → Uninstall (or `unins000.exe` in the program
  folder). It removes only the program files, the shortcuts, this installation's autostart entry, the notification
  registration and "Open with" — **notes, settings and the event log stay**.
* Installing without windows (e.g. a script): `Brulion-<version>-windows-x86_64-setup.exe /VERYSILENT
  /SUPPRESSMSGBOXES /CURRENTUSER` (with autostart: `/TASKS=autostart`, without: `/TASKS=""`),
  uninstalling: `unins000.exe /VERYSILENT /SUPPRESSMSGBOXES`. An administrator can install for
  everyone (`/ALLUSERS`, to `C:\Program Files\Brulion`; "Open with" then for everyone);
  autostart and the notification registration are still entries of the user who installs (every
  other user turns autostart on in the settings).

## Where the data lives

Paths from `QStandardPaths` and `QSettings` (organization and application "Brulion"):

| What | Where |
|---|---|
| Notes (the default new notebook) | `Documents\Brulion` — or any chosen folder (e.g. a Syncthing folder) |
| Settings (list of notebooks, language, theme…) | `%APPDATA%\Brulion\Brulion.ini` |
| App data: the sample notebook ("Sample notebook" / "Przykładowy brulion" — the folder name follows the language), the documentation ("Brulion Documentation" / "Dokumentacja Brulionu"), version history and the notebooks' device state, Nextcloud passwords | `%APPDATA%\Brulion\Brulion\` |
| Event log (`brulion.log`) | `%LOCALAPPDATA%\Brulion\Brulion\State\logs\` |
| Cache (pictures from the internet) | `%LOCALAPPDATA%\Brulion\Brulion\cache\` |
| Program (installer) | `%LOCALAPPDATA%\Programs\Brulion\` |
| Single instance | named pipe `\\.\pipe\<%TEMP%>/brulion-<hash>` and lock file `%TEMP%\brulion-<hash>.lock` |
| Notification registration (toast name and icon) | registry: `HKCU\Software\Classes\AppUserModelId\JanuszBit.Brulion` (`DisplayName`, `IconUri` → `brulion.ico` next to the program) |
| Autostart | registry: value `Brulion` in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`; disabled in Task Manager: `…\Explorer\StartupApproved\Run\Brulion` |
| "Open with" (installer) | registry: `HKCU\Software\Classes\JanuszBit.Brulion.Note`, `JanuszBit.Brulion.Note` values in `…\Classes\.md`, `.markdown`, `.txt` `\OpenWithProgids`, `…\Classes\Applications\brulion.exe` (below, "Open with") |

`%APPDATA%` is usually `C:\Users\<user>\AppData\Roaming`, `%LOCALAPPDATA%` —
`…\AppData\Local`, `%TEMP%` — `…\AppData\Local\Temp`.

**Separate profile:** the environment variable `BRULION_PROFILE=<name>` (letters, digits, `-`, `_`, up to 32
characters) replaces the organization "Brulion" with "Brulion-<name>" — settings, data, the log and the
single-instance socket are then separate (`%APPDATA%\Brulion-<name>\…`). The same for `brulion.exe`
and `brulion-cli.exe`. It is meant for tests (on Windows the `XDG_*` variables from the Linux tests change
nothing) and for trying something out without touching your own profile; it also works on Linux.

## Open with

The installer adds Brulion to **Open with** for `.md`, `.markdown` and `.txt` files: right-click
a file in Explorer → **Open with** → **Brulion** (or **Choose another
app** if it is not there yet). The **Save to Brulion** dialog opens — the same as
"Share → Brulion" on Android: the file goes into **today's journal** (as a block) or
as a **new page** (title from the file name, content unchanged). Brulion saves a **copy** in the open
notebook, and the file itself stays where it is, untouched; **Cancel** saves nothing. A file that
already is a page of the open notebook simply opens. Several selected files go into
one dialog (Windows starts Brulion separately for each file, and the running instance collects them).

* **The default program does not change.** The installer only offers Brulion: it does not write
  the type's default program or the user's choice (`UserChoice`). After installation Windows may ask once
  how to open `.txt` files — pick the program you used so far (e.g. **Notepad**), and everything
  stays as it was. You make Brulion the default yourself: **Open with → Choose another
  app → Brulion → Always**.
* **The portable version (ZIP)** registers nothing: there you have **Choose another app →
  Choose an app on your PC** and `brulion.exe` (Windows then runs it without `--open`, so
  Brulion only opens) — or the installer.
* Without an open notebook (first start) Brulion first asks to open or
  create a notebook; the file waits.

What the installer writes (for the current user in `HKCU\Software\Classes`, with `/ALLUSERS`
in `HKLM\Software\Classes`; uninstalling removes exactly this):

| Key | Contents |
|---|---|
| `JanuszBit.Brulion.Note` | type "Note (Brulion)": icon and the command `"…\brulion.exe" --open "%1"` |
| `.md\OpenWithProgids`, `.markdown\OpenWithProgids`, `.txt\OpenWithProgids` | value `JanuszBit.Brulion.Note` (empty) — Brulion in the "Open with" list |
| `Applications\brulion.exe` | `FriendlyAppName` "Brulion", `SupportedTypes` (`.md`, `.markdown`, `.txt`), the same command |

`--open` on the command line: `brulion.exe --open "C:\ścieżka\notatka.md" …` — everything after `--open`
is files (also a name starting with `-`); relative paths are resolved from the current folder.

## Notifications

Reminders ⏰ and alarms ⏰! are Windows notifications (toasts; `src/app/notify/windowstoastbus.h`,
C++/WinRT, Windows 10 1809 or newer):

* **A reminder** shows the reminder text, below it the page and the time, with the buttons
  **"Snooze 10 min"** (length from Settings → Reminders), for a task **"Done"** (the task
  marked as done, `[x]` with `✅`, without opening the note) and **"OK"**. Clicking the notification
  opens the note at that line and brings the Brulion window to the front. It stays on screen for about 25 s,
  then waits in the Action Center (where it also works as long as Brulion is running).
* **An alarm** (the Windows "alarm" scenario) stays on screen until you respond, and its sound
  repeats for the alarm's ringing time (10 min by default); then the notification stays, now
  silent — as on Linux. A looping sound (Alarm 1–10, Call 1–10, the default alarm sound)
  is played by the notification itself; a short sound (e.g. Mail) Brulion plays again every 30 s by showing
  the notification once more.
* **Sounds** (Settings → Reminders): "Default" is the Windows notification sound (alarm:
  the looping Windows alarm sound), "Ringtone" — one of the Windows notification sounds (Notification,
  Instant message, Mail, Reminder, Text message, Alarm 1–10, Call 1–10), "Silent" — no
  sound. Notifications of a program without an MSIX package cannot play their own sound files
  (the "Sound file…" item is disabled; a file chosen on Linux plays as the default sound).
  "Play sample" shows a short notification with the chosen sound.
* The notifications' name and icon come from the `JanuszBit.Brulion` registration (AppUserModelID), which Brulion
  writes on every start (`src/platform/appidentity.h`) — the portable version too, without a shortcut
  in the Start menu; the installer's shortcut has the same identifier (grouping on the taskbar).
* On "Quit" Brulion removes its notifications from the Action Center: clicking them would have
  nowhere to go any more. Reminders that passed while the program was closed wait in the journal
  (and come back on the next start).
* **Notifications turned off:** when Brulion (or all notifications) is turned off in Windows Settings → System → Notifications,
  reminders are shown by a balloon of the tray icon, as
  before — without buttons. The same when the identifier `JanuszBit.Brulion` is not
  registered (then Windows would accept the notification and not show it). Before Brulion shows
  its first notification, Windows does not know its setting yet — this counts as on. **"Do not disturb" / Focus assist** sends notifications
  straight to the Action Center without sound; alarms break through only when Brulion is on the priority
  list.
* Reminders are counted down by the running Brulion — closing the window leaves it in the tray (the
  "Keep running in the system tray" setting, on by default; "Quit" in the icon's menu closes it).

## Autostart

**Settings → Application → "Start when you sign in"** (or the installer task): Windows
starts Brulion after sign-in hidden in the tray (`brulion.exe --hidden`), so reminders
ring from the start of the session. It is the `Brulion` value of the key
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (`src/platform/autostart.h`):

* It is visible in **Task Manager → Startup apps**; disabled there, it shows in Brulion
  as off, and turned on again in Brulion it lifts the Task Manager's disabling.
* When Brulion is already running, starting it with `--hidden` does not show a window (the second instance just exits).
* **Portable version:** the entry points to the `brulion.exe` from which it was turned on. If you move the folder,
  on the next manual start the entry points to the new place by itself (provided there is no program at the old
  path any more — Brulion does not touch the entry of another, existing copy).
* With a separate profile (`BRULION_PROFILE`) the switch is disabled: the autostart entry cannot set
  the environment variable, so it would start the default profile.
* The installer task exists only on the first install: an update leaves autostart as
  it was set in Brulion or in Task Manager.
* Uninstalling removes the entry when it points to this installation.

## Tray calendar

Clicking Brulion's icon in the system tray (next to the clock) opens a **calendar**, similar to
the one Windows 7 and 10 show when you click the clock (`ShellTrayCalendar.qml`):

* a small window right above the icon (with the taskbar at the top or at a side — at that edge), always on
  top, without a taskbar button: today's date, the month with dots on days with entries
  in the journal, due dates 📅, scheduled dates ⏳ and reminders ⏰, and the list of tasks and reminders of
  the selected day;
* clicking a task or "Open journal" hides the calendar and shows that day in the Brulion window;
  at the bottom **"Open Brulion"** and **"Quick capture…"**;
* the calendar disappears after a click anywhere else, after Escape, after clicking the icon again
  and after Alt+F4 — Brulion keeps running in the tray;
* without an open notebook it shows only the month and a request to open a notebook.

**Settings → Application → "Click on the tray icon"**: **Calendar** (the default)
or **Window** (shows or hides the Brulion window, as before the calendar version). A double click
always shows the window, and the calendar is also in the icon's menu (right-click → **Calendar**).

**Brulion does not replace the Windows clock calendar — and it cannot be done properly.** The clock
flyout belongs to the system shell (Explorer, in Windows 10 `ShellExperienceHost`, in Windows 11 the
notification center): there is no public API to replace it, extend it or
add entries to it. Windows 10 shows in it only events from the Calendar app's accounts
(Outlook, Google…) — a program without an MSIX package cannot add itself there — and Windows 11 no longer
has that list. The registry switch that brought back the Windows 7 calendar
(`UseWin32TrayClockExperience`) stopped working in early versions of Windows 10. Programs that
change the clock (ExplorerPatcher, Windhawk mods, ElevenClock, T-Clock) inject code into
Explorer or cover it with their own window: they break after Windows updates, antivirus software
flags them, and an unsigned program all the more should not do that.

You can, however, use Brulion's calendar instead of the system one:

1. Hide the system clock — Windows 10: Settings → Personalization → Taskbar → "Turn system
   icons on or off" → Clock; Windows 11: Settings → Time & language → Date & time →
   "Show time and date in the System tray" (newer releases).
2. Pin Brulion's icon permanently in the tray — Taskbar settings → "Other system tray
   icons" (Windows 11) or "Select which icons appear on the taskbar" (Windows 10) → Brulion
   on.
3. Turn on "Start when you sign in" (above), so that the icon is there from the start of the session.

On Linux the calendar works the same way (icon menu → **Calendar**; clicking the icon by default
shows the window, setting as above). Under X11 the window appears at the cursor; under Wayland
a window cannot position itself — the compositor places it where it wants (usually in the centre), and may not
give it focus: then it does not disappear after a click beside it and gets no keyboard input (Escape does not
reach it) — you close it with the **×** button in its corner, by choosing an entry or **Open Brulion**,
or by clicking the icon again.

## New version

Below the top bar a message about a newer release appears with a **View on GitHub** button,
just as on Android ([docs/ANDROID.md](ANDROID.md#new-version)). **Settings →
Application → Check for new versions** turns it off.

## Limitations of the test version

* **Unsigned files** (SmartScreen, above) and **no automatic updates** — a new version
  is installed with the installer over the previous one.
* **Notifications work as long as Brulion is running** (no COM activator): a click
  on a notification after the program has closed would do nothing, which is why "Quit" removes them (above).
* **Command line:** `brulion-cli.exe` (next to `brulion.exe`) works as on Linux
  (docs/CLI.md). `brulion.exe cli …` works too, but `brulion.exe` is a GUI program —
  its output is not visible in the console; in scripts and for AI agents use `brulion-cli.exe`.
  Remote control of the app (`--remote-control`, `brulion-cli app …`) works through a named
  pipe, but its end-to-end test is Linux-only (below).
* **Notebook folders cannot be renamed in Explorer while Brulion is running.**
  Brulion watches the notebook's folders (`QFileSystemWatcher` keeps an open handle to each of them),
  and Windows does not move a folder in which something is open — Explorer reports that the folder is
  in use. The same for Syncthing when it moves a folder renamed on another device: it reports an error
  and tries again. In Brulion (sidebar, "Rename", "Move to…") it works normally;
  deleting folders and everything other programs do with files (Syncthing, editors) work too.
  A folder renamed on another device or in Nextcloud is renamed here by Nextcloud sync
  itself; when another program keeps a file from that folder open, Windows does not
  allow it and the notes are moved one by one, while the open one is downloaded under the new name (the old one
  goes to the trash once the program releases it) — nothing is lost.
* **Names Windows does not allow:** a note from another device whose name has a
  `< > : " \ | ? *` character (or `CON`, `NUL`, `AUX`… or a trailing dot) cannot be saved on Windows —
  sync reports it as an error on every run until the name is changed on the other
  device.
* **A note open in another program without sharing** (e.g. locked by Word) cannot be saved by Brulion
  as long as that program holds it: Windows does not replace a file opened without
  `FILE_SHARE_DELETE`, and Brulion saves atomically (a new file in place of the old one). Close it there
  before you change it in Brulion; sync retries such a file on the next run.
* **Spell checking** (docs/UI.md "Spell checking"): Hunspell is compiled into the program
  (`scripts/windows-build.ps1` downloads its 1.7.2 release and the Polish and English dictionaries
  from the LibreOffice dictionaries repository, with verified SHA-256 sums), the dictionaries live in the
  `dictionaries\` folder next to `brulion.exe` (with their licences). Not yet checked on real
  Windows: a program folder with Polish characters in its path.
* Symbolic links and junctions in a notebook are not tested on Windows.
* x86_64 only (no Windows on ARM) and only Windows 10/11.
* The tray calendar does not replace the Windows clock calendar (above, "Tray calendar").
  Its position next to the icon (taskbar on any side, several monitors) and disappearing after a click
  beside it still need checking on real Windows.

## Building

### In CI

`.github/workflows/windows.yml` (runner `windows-2022`): Qt 6.11.2 `win64_msvc2022_64` from aqt
(`jurplel/install-qt-action`, cached; aqtinstall from a pinned commit of the master branch, because release
3.3.0 does not know the Qt 6.11 repository layout for Windows) together with the **Qt PDF** module (`modules:
qtpdf`, a Qt repository extension: PDF thumbnails, the PDF viewer with search, and PDF pages
as a drawing backdrop, `src/platform/pdfpages_qtpdf.cpp`; `windeployqt` adds `Qt6Pdf.dll`, about
5 MB — the `QtQuick.Pdf` QML module is not used), MSVC from `ilammy/msvc-dev-cmd`, ccache
from Chocolatey (compilation cache in the Actions cache, saved right after the build), CMake +
Ninja, Release, then `scripts/windows-build.ps1` step by step: build, tests, packages, package
check → artifact `brulion-windows` (ZIP + installer). It runs like `appimage.yml`: manually
(`gh workflow run windows.yml --ref main`), on a push to `main` with the `[ci]` marker (when it changes
`src/`, `tests/`, `packaging/windows/`…) and from `release.yml`, which attaches both files to the release
(and to `SHA256SUMS.txt`) — the release is published only when Windows passes too.

### Locally

Needed: Visual Studio 2022 with the "Desktop development with C++" workload (it has CMake
and Ninja), Qt 6.11.2 for MSVC (the Qt installer or aqt newer than 3.3.0:
`aqt install-qt windows desktop 6.11.2 win64_msvc2022_64 -O C:\Qt -m qtpdf`; without `qtpdf` Brulion
builds without PDFs: no thumbnails, PDF viewer or PDF backdrop — a PDF then opens
in the system's default app), Inno Setup 6 (`winget install JRSoftware.InnoSetup`). A clone with Git for Windows
keeps LF line endings thanks to `.gitattributes` (`* -text`) — the tests compare bytes. In PowerShell
in the repository directory:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows-build.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64
# only selected steps:
scripts\windows-build.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64 -Steps build,test
```

Without an MSVC environment (no `cl.exe` in `PATH`) the script loads it by itself (`vswhere` + `vcvars64.bat`);
`ccache` in `PATH` (4.6 or newer) is used automatically.
Steps:

1. **build** — `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release` in `build-windows`. MSVC gets
   `/utf-8` (Polish strings in the code), `/permissive-`, `/Zc:__cplusplus`, `/bigobj` and
   `/W4` warnings without `/WX` (CMakeLists.txt, `cmake/BrulionWarnings.cmake`); `brulion.exe`
   and `brulion-cli.exe` have the `packaging/windows/brulion.ico` icon and a version in their resources
   (`packaging/windows/brulion.rc.in`, `cmake/BrulionWindows.cmake`). Notifications use
   the C++/WinRT headers from the Windows SDK (`<winrt/Windows.UI.Notifications.h>`, the `cppwinrt` folder, which
   the MSVC environment has in `INCLUDE`) and `WindowsApp.lib`; nothing extra is installed.
2. **test** — `ctest` offscreen (`QT_QPA_PLATFORM=offscreen`) with Qt in `PATH`.
3. **package** — into `build-windows\package\Brulion`: `brulion.exe`, `brulion-cli.exe`,
   `windeployqt --release --qmldir src\app\qml --no-translations --no-opengl-sw` (Qt libraries,
   plugins — platforms, image formats with SVG, TLS through Schannel without OpenSSL — and QML modules), plus
   `platforms\qoffscreen.dll` (runs without a screen: package check, screenshots) and
   the MSVC libraries (`Microsoft.VC143.CRT`, next to the program — without installing the Visual C++
   Redistributable). From this folder: `dist-windows\brulion-<version>-windows-x86_64.zip` and
   the installer from `packaging/windows/brulion.iss` →
   `dist-windows\Brulion-<version>-windows-x86_64-setup.exe`.
4. **check** — without Qt in `PATH` and without Qt variables: the ZIP unpacked into a new folder, then
   the installer installed silently for the current user with the `autostart` task (the value
   `Run\Brulion` must start the installed `brulion.exe --hidden`); from both, `brulion-cli.exe
   --version` and the shell self-test of the real app (`brulion.exe --demo --route journals
   --selftest-shell --no-single-instance …`, offscreen, profile `BRULION_PROFILE=package-check`) —
   a missing library or QML module fails it; every run must register the profile's notification
   identifier (`AppUserModelId\JanuszBit.Brulion.package-check`, icon next to the program),
   and the portable version in a separate profile must not add autostart; then the same installer
   once more, silently, as an update, after autostart was turned off — it must not turn it back on;
   finally a silent uninstall, after which the profile's settings must remain and the autostart entry must
   be gone.

The installer's `AppId` (`{5E3B8C1A-7F42-4D6B-9A0E-2C8D4B6F1E37}`) is fixed — Windows recognizes
the installed Brulion by it; do not change it.

### Tests on Windows

The same tests as on Linux (`QTEST`, QML tests of the real app, `brulion-cli` as a process),
with exceptions:

* not built/run: the bash script tests (`tests/scripts`, `qml-smoke.sh` in
  `tst_content_qml`, `tst_planning_qml`, `tst_appio_qml`, `scroll-jumps.sh` in
  `tst_scroll_jumps_qml`) and `tst_remote_control_qml` (its private home directory is
  `XDG_*` folders, sockets in the runtime directory and Unix permissions);
* skipped (`QSKIP` with a reason): in `tst_ipc_singleinstance` and `tst_ipc_control` the cases of
  folder permissions, symbolic links and an abandoned socket file — on Windows the socket is
  a named pipe with the user's access list, without a file;
* skipped depending on the file system's capabilities (`tests/common/filesystemtraits.h`): cases
  with **symbolic links** (`QFile::link()` makes a `.lnk` shortcut on Windows) — in `tst_vault`,
  `tst_vault_edge`, `tst_vault_safety`, `tst_shell_settings`, `tst_shell_nextcloud`; with
  **Unix permissions** (a folder without read permission, a file for the owner only; Windows has
  only the "read-only" attribute, and the rest is handled by the profile's access lists) — in
  `tst_vault_safety`, `tst_sync_engine`, `tst_sync_folders` (`renameHereRefusedFallsBack`: a
  read-only folder that sync cannot rename), `tst_log_logger`,
  `tst_shell_settings`; with **two
  files differing only in letter case** (NTFS does not tell them apart) — in `tst_vault`,
  `tst_notebook_journals`, `tst_shell_workspace`, `tst_sync_folders` (there, on such a file system,
  the test instead checks that a folder whose letter case was changed by another device gets
  the same name here without a transfer and without the trash);
* in `tst_vault_watch`: `watcherDetectsFolderRenames` (another program cannot rename a watched
  folder on Windows) and the end of `inPlaceRewriteOfAnOpenFileIsReportedAtOnce` (saving a note
  that another program keeps open) — both described above in "Limitations";
* Windows only: `tst_windowstoastbus` — real notifications under a test identifier
  (shown, replaced, closed, cleared); when notifications are turned off on the given computer
  (e.g. a CI runner), these cases are skipped;
* the QML self-tests (`tst_shell_qml*`, `tst_shell_journey*`, `tst_notify_sounds_qml*`,
  `tst_appio_import_qml*`, `tst_content_journals_qml`) get a separate profile
  `BRULION_PROFILE=test-<name>` (`brulion_test_profile()` in `tests/CMakeLists.txt`), which their
  fixture removes before running — instead of `XDG_*` folders. In such a profile the autostart
  switch is disabled, so no test writes to the `Run` key; each profile registers
  its own notification identifier (`JanuszBit.Brulion.test-<name>`). In-process C++ tests
  (e.g. `tst_shell_navigation`) do not set Brulion's names and get the identifier
  `JanuszBit.Brulion.unnamed`, which nobody registers: their notifications go to the tray,
  and the cleanup does not remove the real Brulion's notifications.

## Single instance and remote control

`QLocalServer` on Windows is a named pipe. The single-instance key lives in the user's temporary
folder (`%TEMP%`, in the user profile; Qt's `RuntimeLocation` would be just the home
directory) — the lock file is there too, and the pipe name contains that path. `UserAccessOption` gives
the pipe an access list for this user only (the equivalent of the socket's `0600` on Linux). Pipe
names are shared by the whole computer, so, like `SO_PEERCRED` on Linux, both sides check
the process on the other side (`GetNamedPipeClientProcessId`/`GetNamedPipeServerProcessId`) and its user's
SID (`src/ipc/controlprotocol.cpp`, `peerUid()`): Brulion does not use a pipe that another user created
first under that name.

A second start with files (`brulion.exe --open "%1"` from "Open with") turns their paths
into absolute ones (from its current folder) and sends the running instance a `share` message
(`src/ipc/appmessage.h`), and before sending allows it to come to the foreground
(`AllowSetForegroundWindow`, `platform::allowForegroundActivation()`): the Brulion window comes
to the front instead of only flashing on the taskbar. Several files selected at once means several starts —
the first one, when Brulion is not running, becomes the main instance, the others wait until it starts listening,
and the files go into one dialog. A Brulion older than 0.6.0, while still running, does not know the
`share` message and ignores it — then closing it and using "Open with" again helps.
