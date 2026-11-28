// AppSettings — device-local preferences of the app (QML singleton `AppSettings`, also
// `AppController.settings`). Stored with QSettings (INI format): on Linux
// ~/.config/Brulion/Brulion.ini, on Android in the app's private files, in the browser a file
// in the IndexedDB-backed app data folder. Vault settings shared between devices are NOT here —
// they live in the vault's .brulion/config.json (Workspace.config / Workspace.setConfig).
//
// Every property is validated (unknown theme -> "dark", sizes clamped) and written at once;
// QML binds to them directly: `Theme.mode: AppSettings.themeMode`.
//
// The Nextcloud app password is deliberately not kept in QSettings: it is stored in its own
// file `<app data>/secrets/nextcloud-password` with owner-only permissions (0600; the folder
// 0700). Limitation: that is plain text protected only by file permissions (and the app's
// private storage on Android, the browser's site storage on the web) — Brulion does not depend
// on an OS keychain (libsecret/KWallet) so it runs the same on every target. Use a Nextcloud
// *app password* (revocable per device), never the account password.
//
// Other app areas can keep simple preferences with value()/setValue() under their own key
// prefix ("views/tasks/showDone", "editor/sourceMode", ...).
#pragma once

#include "log/redact.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <optional>

class QJsonArray;
class QJsonObject;
class QSettings;

namespace brulion::shell {

struct AgentConfig;

class AppSettings : public QObject {
    Q_OBJECT

    // ---- General ------------------------------------------------------------------------
    /// "dark" (default, with the graphite background: darkSurface) | "light" | "system" (Theme.mode).
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged FINAL)
    /// UI language "en" | "pl". Default: English (whatever the system's locale); the intro's
    /// first step and Settings → General choose Polish.
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)
    /// UI text size factor 0.85 … 1.4 (Theme.fontScale).
    Q_PROPERTY(qreal fontScale READ fontScale WRITE setFontScale NOTIFY fontScaleChanged FINAL)
    /// Reduce animations (Theme.reducedMotion).
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged FINAL)
    /// The interface's size on this device (desktop; ui/uiscale.h): 0 "Automatic" (default: by the
    /// screen, AppController.autoInterfaceScale), else a factor 0.8 … 2 on 5 % steps.
    Q_PROPERTY(qreal interfaceScale READ interfaceScale WRITE setInterfaceScale NOTIFY interfaceScaleChanged FINAL)

    // ---- Appearance (Settings → Appearance; Theme inputs, ThemePalette) -------------------
    /// "coral" (default) | "red" | "blue" | "green" | "teal" | "violet" | "graphite" | "custom"
    /// (Theme.accentName).
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY accentChanged FINAL)
    /// The custom accent "#rrggbb" (lower case; "" = none: "custom" then looks like "coral").
    Q_PROPERTY(QString customAccent READ customAccent WRITE setCustomAccent NOTIFY accentChanged FINAL)
    /// Light background "paper" (default) | "white".
    Q_PROPERTY(QString lightSurface READ lightSurface WRITE setLightSurface NOTIFY surfacesChanged FINAL)
    /// Dark background "graphite" (layered greys, default) | "ink" (warm layers) | "black" (OLED) | "slate".
    Q_PROPERTY(QString darkSurface READ darkSurface WRITE setDarkSurface NOTIFY surfacesChanged FINAL)
    /// Font family of the interface ("" = the system's).
    Q_PROPERTY(QString uiFontFamily READ uiFontFamily WRITE setUiFontFamily NOTIFY fontFamiliesChanged FINAL)
    /// "comfortable" (default) | "compact": smaller controls and rows (desktop only; touch UIs
    /// keep their 44 px targets).
    Q_PROPERTY(QString density READ density WRITE setDensity NOTIFY densityChanged FINAL)
    /// Navigation entries and sections hidden in the sidebar (ids of SidebarItems).
    Q_PROPERTY(QStringList sidebarHidden READ sidebarHidden WRITE setSidebarHidden NOTIFY sidebarHiddenChanged FINAL)
    /// The order of the sidebar's page sections: all of SidebarSections, each once.
    Q_PROPERTY(QStringList sidebarSections READ sidebarSections WRITE setSidebarSections NOTIFY
                       sidebarSectionsChanged FINAL)
    /// The phone's bottom navigation: exactly five views of bottomNavViews(), nothing else (the
    /// drawer opens from the top bar's ☰, quick capture is the top bar's +).
    Q_PROPERTY(QStringList bottomNavTabs READ bottomNavTabs WRITE setBottomNavTabs NOTIFY bottomNavTabsChanged FINAL)
    /// The icon bar's order with "|" between its groups (key `ui/rail/order`; the layout language's
    /// `#rail`, docs/NOTEBOOK-SETTINGS.md); empty: the app's own order and groups.
    Q_PROPERTY(QStringList railOrder READ railOrder WRITE setRailOrder NOTIFY railOrderChanged FINAL)
    /// The icon bar's views in order ("code" first, then railOrder's, then those it leaves out).
    Q_PROPERTY(QStringList railViews READ railViews NOTIFY railOrderChanged FINAL)
    /// "Use the notebook's layout" (key `ui/layout/followNotebook`, off by default): the settings
    /// note's layout block applied here whenever it changes (AppController).
    Q_PROPERTY(bool layoutFollowsNotebook READ layoutFollowsNotebook WRITE setLayoutFollowsNotebook NOTIFY
                       layoutFollowsNotebookChanged FINAL)
    /// Quick capture's + on the top bar of phones and narrow windows (wide ones always show it).
    Q_PROPERTY(bool captureButtonShown READ captureButtonShown WRITE setCaptureButtonShown NOTIFY
                       captureButtonShownChanged FINAL)
    /// The rail of views widens over the page panel with the views' names while the pointer rests
    /// on it (desktop; docs/UI.md "Sidebar"). On by default; off: the names in tooltips.
    Q_PROPERTY(bool railExpands READ railExpands WRITE setRailExpands NOTIFY railExpandsChanged FINAL)
    /// The built-in gym extension (Settings → Extensions; docs/UI.md "Gym"): the Gym view, its
    /// palette commands. Off by default.
    Q_PROPERTY(bool gymEnabled READ gymEnabled WRITE setGymEnabled NOTIFY gymEnabledChanged FINAL)
    /// The built-in habits extension (`nawyki`; Settings → Extensions; docs/UI.md "Habits"): the
    /// Habits view, its palette commands. On by default (habits were always there).
    Q_PROPERTY(bool habitsEnabled READ habitsEnabled WRITE setHabitsEnabled NOTIFY habitsEnabledChanged FINAL)
    /// The built-in flashcards extension (`fiszki`; Settings → Extensions; docs/UI.md
    /// "Flashcards"): the Flashcards view, its due count, its palette commands. On by default.
    Q_PROPERTY(bool flashcardsEnabled READ flashcardsEnabled WRITE setFlashcardsEnabled NOTIFY flashcardsEnabledChanged FINAL)
    /// Flashcards on this device (Settings → Extensions → Flashcards; passed to the extension with
    /// each view and count): new cards a day (0 … 9999, 20), the desired retention in percent (70 …
    /// 99, 90), the tags that make a note a deck ("flashcards, fiszki").
    Q_PROPERTY(int flashcardsNewPerDay READ flashcardsNewPerDay WRITE setFlashcardsNewPerDay NOTIFY flashcardsSettingsChanged FINAL)
    Q_PROPERTY(int flashcardsRetention READ flashcardsRetention WRITE setFlashcardsRetention NOTIFY flashcardsSettingsChanged FINAL)
    Q_PROPERTY(QString flashcardsTags READ flashcardsTags WRITE setFlashcardsTags NOTIFY flashcardsSettingsChanged FINAL)
    /// The built-in AI assistant extension (asks about a page, summarises it). Off by default.
    Q_PROPERTY(bool assistantEnabled READ assistantEnabled WRITE setAssistantEnabled NOTIFY assistantEnabledChanged FINAL)
    /// AI for extensions (b.ai, docs/EXTENSIONS.md "AI") on this device: "anthropic" | "openai"
    /// (any OpenAI-compatible API), the model ("" = the provider's default), the API's address
    /// ("" = the provider's own; a server of one's own: "http://192.168.1.10:11434/v1"). The key
    /// lives in `<app data>/secrets/ai-key` (owner-only, like the Nextcloud password).
    Q_PROPERTY(QString aiProvider READ aiProvider WRITE setAiProvider NOTIFY aiChanged FINAL)
    Q_PROPERTY(QString aiModel READ aiModel WRITE setAiModel NOTIFY aiChanged FINAL)
    /// The model on the device for the provider "local" (an ai::LocalModel's id; "": the default).
    Q_PROPERTY(QString aiLocalModel READ aiLocalModel WRITE setAiLocalModel NOTIFY aiChanged FINAL)
    Q_PROPERTY(QString aiBaseUrl READ aiBaseUrl WRITE setAiBaseUrl NOTIFY aiChanged FINAL)
    Q_PROPERTY(bool hasAiKey READ hasAiKey NOTIFY aiChanged FINAL)
    /// The address is plain http to another machine (ai::Settings::unencrypted()): the settings
    /// say that notes and the key can be read on the way.
    Q_PROPERTY(bool aiUnencrypted READ aiUnencrypted NOTIFY aiChanged FINAL)
    /// AI agents of the user's own that answer in the AI chat (the Agent Client Protocol:
    /// workspaceagents.h): rows {id, name, command, tools} — the programs this device may start,
    /// so they live here and never in the notebook. Empty in the browser and on Android.
    Q_PROPERTY(QVariantList acpAgents READ acpAgentRows NOTIFY acpAgentsChanged FINAL)
    /// Agents can run in this app (the desktop app).
    Q_PROPERTY(bool acpAvailable READ acpAvailable CONSTANT FINAL)
    /// Agents that are programs of this device can be added (the desktop app), not only ones on
    /// another computer.
    Q_PROPERTY(bool acpProgramsAvailable READ acpProgramsAvailable CONSTANT FINAL)
    /// The remote MCP server for claude.ai and ChatGPT run in the background while the app runs
    /// (mcpserversupervisor.h, desktop): on or off (off by default), its port (1024 … 65535, 8780),
    /// the public https address that reaches it ("" this computer's only), whether the apps may
    /// only read. Its sign-in password lives in `<app data>/secrets/mcp-password` (owner-only).
    Q_PROPERTY(bool mcpServerEnabled READ mcpServerEnabled WRITE setMcpServerEnabled NOTIFY mcpServerChanged FINAL)
    Q_PROPERTY(int mcpServerPort READ mcpServerPort WRITE setMcpServerPort NOTIFY mcpServerChanged FINAL)
    Q_PROPERTY(QString mcpServerPublicUrl READ mcpServerPublicUrl WRITE setMcpServerPublicUrl NOTIFY mcpServerChanged FINAL)
    Q_PROPERTY(bool mcpServerReadOnly READ mcpServerReadOnly WRITE setMcpServerReadOnly NOTIFY mcpServerChanged FINAL)
    Q_PROPERTY(bool hasMcpServerPassword READ hasMcpServerPassword NOTIFY mcpServerChanged FINAL)
    /// "Share agents with the phone" (agentshare.h; key `ai/share`): this computer runs `brulion-cli
    /// acp serve` for its agents while the app runs. Off by default. The address the devices are
    /// given ("" found by itself: Tailscale, else this computer's in its network; `ai/shareAddress`)
    /// and whether the user's own devices in the tailnet pair without a code (`ai/shareTrust`, on).
    Q_PROPERTY(bool acpShare READ acpShare WRITE setAcpShare NOTIFY acpShareChanged FINAL)
    Q_PROPERTY(QString acpShareAddress READ acpShareAddress WRITE setAcpShareAddress NOTIFY acpShareChanged FINAL)
    Q_PROPERTY(bool acpShareTrust READ acpShareTrust WRITE setAcpShareTrust NOTIFY acpShareChanged FINAL)
    /// The webhooks of scheduled conversations (hookserver.h; Settings → Extensions → Scheduled conversations →
    /// Webhooks): the switch (`ai/hooks`, off by default), where it listens ("" this computer only,
    /// "tailnet": its tailnet address; `ai/hooksAddress`), its port (`ai/hooksPort`, 8791) and the
    /// address callers reach it at — the user's own reverse proxy or Tailscale Funnel ("" none;
    /// `ai/hooksPublicUrl`). The hooks' tokens are kept with the schedules (`ai/schedules/…`).
    Q_PROPERTY(bool chatHooks READ chatHooks WRITE setChatHooks NOTIFY chatHooksChanged FINAL)
    Q_PROPERTY(QString chatHooksAddress READ chatHooksAddress WRITE setChatHooksAddress NOTIFY chatHooksChanged FINAL)
    Q_PROPERTY(int chatHooksPort READ chatHooksPort WRITE setChatHooksPort NOTIFY chatHooksChanged FINAL)
    Q_PROPERTY(QString chatHooksPublicUrl READ chatHooksPublicUrl WRITE setChatHooksPublicUrl NOTIFY chatHooksChanged
                       FINAL)
    /// "Notify when the AI answers" (key `ai/chatNotify`; WorkspaceChats::setNotifyMode): "always",
    /// "background" (only while the app is not in front: the default) or "never".
    Q_PROPERTY(QString aiChatNotify READ aiChatNotify WRITE setAiChatNotify NOTIFY aiChatNotifyChanged FINAL)
    /// "New conversations answered by" (key `ai/defaultPartner`; WorkspaceChats::setDefaultPartner):
    /// "" the built-in assistant (the default), `acp:<id>` an AI agent, an extension's chat command.
    Q_PROPERTY(QString aiDefaultPartner READ aiDefaultPartner WRITE setAiDefaultPartner NOTIFY aiDefaultPartnerChanged
                       FINAL)
    /// "Let AI agents send notifications" (key `ai/agentNotify`, on by default;
    /// WorkspaceChats::setAgentNotifications).
    Q_PROPERTY(bool aiAgentNotify READ aiAgentNotify WRITE setAiAgentNotify NOTIFY aiAgentNotifyChanged FINAL)
    /// The app's mode (key `ui/mode`; docs/CODE.md): "notes" — the notebook (the default) — or
    /// "code": projects and AI agents' threads, as in T3 Code. A device setting.
    Q_PROPERTY(QString appMode READ appMode WRITE setAppMode NOTIFY appModeChanged FINAL)
    /// "Let extensions send notifications" (key `extensions/notify`, on by default;
    /// NotificationCenter::setAllowed: b.notify).
    Q_PROPERTY(
            bool extensionsNotify READ extensionsNotify WRITE setExtensionsNotify NOTIFY extensionsNotifyChanged FINAL)
    /// "Archive conversations untouched for" (key `ai/chatAutoArchive`; WorkspaceChats::
    /// setAutoArchiveDays): "never" (the default), or the days as a number — "1", "3", "7", "30".
    Q_PROPERTY(QString aiChatAutoArchive READ aiChatAutoArchive WRITE setAiChatAutoArchive NOTIFY
                       aiChatAutoArchiveChanged FINAL)
    /// "Settle threads untouched for" (key `code/autoSettle`; CodeThreads::setAutoSettleDays, docs/CODE.md
    /// "Settling and snoozing"): the days as a number — "1", "3", "7" (the default, T3 Code's), "30" —
    /// or "never".
    Q_PROPERTY(QString codeAutoSettle READ codeAutoSettle WRITE setCodeAutoSettle NOTIFY codeAutoSettleChanged FINAL)
    /// "Remove the worktrees of settled threads" (key `code/worktreeCleanup`; CodeThreads::
    /// setWorktreeCleanup): "never" (the default), "merged" (once its pull request is merged), "1",
    /// "3", "7" or "30" (also that many days after it settled).
    Q_PROPERTY(QString codeWorktreeCleanup READ codeWorktreeCleanup WRITE setCodeWorktreeCleanup NOTIFY
                       codeWorktreeCleanupChanged FINAL)
    /// "Tool calls in chat" (key `ai/chatToolCalls`; docs/UI.md "AI agents (ACP)"): "all" (default:
    /// every call of an agent's turn), "hideFinished" (a call shows while it runs and stays only if
    /// it failed), "hideAll" (only failed ones; the status line says the agent works). Questions,
    /// failures and the plan always show.
    Q_PROPERTY(QString chatToolCalls READ chatToolCalls WRITE setChatToolCalls NOTIFY chatToolCallsChanged FINAL)

    // ---- Editor -------------------------------------------------------------------------
    /// "sans" (default, Inter) | "serif" (Literata) (Theme.editorFont).
    Q_PROPERTY(QString editorFont READ editorFont WRITE setEditorFont NOTIFY editorFontChanged FINAL)
    /// Editor text size in px at 100 % (12 … 28, default 16).
    Q_PROPERTY(int editorFontSize READ editorFontSize WRITE setEditorFontSize NOTIFY editorFontSizeChanged FINAL)
    /// Maximum width of the editor's text column in px (480 … 1600, default 680 — about 85
    /// characters of 16-px serif); 0 = full width. The old presets 760 and 640 read as 680 and 600.
    Q_PROPERTY(int editorWidth READ editorWidth WRITE setEditorWidth NOTIFY editorWidthChanged FINAL)
    /// A font family for notes instead of serif / sans ("" = editorFont decides).
    Q_PROPERTY(QString editorFontFamily READ editorFontFamily WRITE setEditorFontFamily NOTIFY fontFamiliesChanged FINAL)
    /// Font family of code ("" = the system's fixed-width font).
    Q_PROPERTY(QString editorMonoFamily READ editorMonoFamily WRITE setEditorMonoFamily NOTIFY fontFamiliesChanged FINAL)
    /// Line spacing of notes: "compact" | "normal" (default) | "relaxed" (Theme.lineSpacing).
    Q_PROPERTY(QString editorLineSpacing READ editorLineSpacing WRITE setEditorLineSpacing NOTIFY
                       editorLineSpacingChanged FINAL)
    /// Show the whole file as Markdown source by default (Ctrl+E switches per page).
    Q_PROPERTY(bool editorSourceMode READ editorSourceMode WRITE setEditorSourceMode NOTIFY editorSourceModeChanged FINAL)
    /// The page properties panel ("Properties") opens expanded (its header's tap switches it;
    /// folded by default: the journal stream stays dense).
    Q_PROPERTY(bool propertiesExpanded READ propertiesExpanded WRITE setPropertiesExpanded NOTIFY
                       propertiesExpandedChanged FINAL)
    /// Show pictures from the internet in notes without asking (`![x](https://…)`; off: each
    /// waits for "Show" — a synced or imported note cannot make the device fetch anything).
    Q_PROPERTY(
            bool webPicturesShown READ webPicturesShown WRITE setWebPicturesShown NOTIFY webPicturesShownChanged FINAL)
    /// A hairline frame around the pictures shown in notes (EmbedView; default off: a picture
    /// sits on the page's own colour, its transparent parts show the page). A picture not shown
    /// yet — loading, from the internet waiting for "Show", a format the device cannot decode —
    /// has its frame either way.
    Q_PROPERTY(bool pictureFrames READ pictureFrames WRITE setPictureFrames NOTIFY pictureFramesChanged FINAL)
    /// In a dark theme a picture is drawn on a light backing (Theme.pictureBacking; default on):
    /// a transparent PNG or SVG with dark lines (a diagram, a scan) stays readable instead of
    /// vanishing into the dark page. An opaque picture covers the backing, so photos look the same.
    /// Drawings have a dark version of their own and never get it. EmbedView and the picture viewers.
    Q_PROPERTY(bool lightPictureBacking READ lightPictureBacking WRITE setLightPictureBacking NOTIFY
                       lightPictureBackingChanged FINAL)
    /// Line numbers beside the code blocks of notes (CodeBlockView; default off): each line of
    /// the block's text numbered from 1, a wrapped line once. They are not part of the text (a
    /// copy, the file and the export have none).
    Q_PROPERTY(bool codeLineNumbers READ codeLineNumbers WRITE setCodeLineNumbers NOTIFY codeLineNumbersChanged
                       FINAL)
    /// Code mode's terminals drawn by Atrament (docs/CODE-TERMINAL.md "Drawn by Atrament"; default
    /// off; where Atrament is built: TerminalRegistry.atramentAvailable). This device's.
    Q_PROPERTY(
            bool terminalAtrament READ terminalAtrament WRITE setTerminalAtrament NOTIFY terminalAtramentChanged FINAL)
    /// Task status history (docs/SYNTAX.md §5 "Time tracking and status history"; default off):
    /// a status change adds a `State "DONE" from "TODO" [time]` line to the task's `:LOGBOOK:`, and
    /// "In progress" starts its clock (leaving it stops the clock), as Logseq does. Pushed into the
    /// Workspace (EditorHost::taskStatusHistory()).
    Q_PROPERTY(bool taskStatusHistory READ taskStatusHistory WRITE setTaskStatusHistory NOTIFY taskStatusHistoryChanged
                       FINAL)
    /// Pages drawn by Atrament (docs/ATRAMENT.md "The block editor in Atrament (M2)"; default off;
    /// where Atrament is built: DeviceInfo.atrament). This device's.
    Q_PROPERTY(bool editorAtrament READ editorAtrament WRITE setEditorAtrament NOTIFY editorAtramentChanged FINAL)
    /// Spell checking in the editor (SpellCheck, docs/UI.md "Spell checking"; default on).
    Q_PROPERTY(bool spellCheck READ spellCheck WRITE setSpellCheck NOTIFY spellCheckChanged FINAL)
    /// Its languages: "auto" (default: the UI language, the system's, English), "pl", "en",
    /// "pl,en" (spell/languages.h resolveLanguages).
    Q_PROPERTY(QString spellLanguages READ spellLanguages WRITE setSpellLanguages NOTIFY spellLanguagesChanged FINAL)
    /// Misspelled words are underlined in the blocks not being edited too (SpellCheck.wholePage).
    /// Default on, except on phones (Android: the system's checker over IPC for every word shown).
    Q_PROPERTY(bool spellWholePage READ spellWholePage WRITE setSpellWholePage NOTIFY spellWholePageChanged FINAL)
    /// The personal dictionary: words spell checking takes as right, sorted (a device setting:
    /// every notebook of this device; docs/UI.md "Spell checking" says why not in the notebook).
    Q_PROPERTY(QStringList spellingWords READ spellingWords WRITE setSpellingWords NOTIFY spellingWordsChanged FINAL)
    /// The whole-notebook search reads the text of PDFs and pictures (Workspace.attachments; docs/UI.md
    /// "Searching attachments"). Default on: a PDF's own text costs little.
    Q_PROPERTY(
            bool attachmentSearch READ attachmentSearch WRITE setAttachmentSearch NOTIFY attachmentSearchChanged FINAL)
    /// …and recognises text in pictures and scanned pages (OCR, where Tesseract is). Default off:
    /// a page takes a core about a second.
    Q_PROPERTY(bool attachmentOcr READ attachmentOcr WRITE setAttachmentOcr NOTIFY attachmentSearchChanged FINAL)
    /// OCR's languages: "auto" (default: the UI language and English) or Tesseract's codes
    /// separated by '+' ("pol+eng").
    Q_PROPERTY(QString attachmentOcrLanguages READ attachmentOcrLanguages WRITE setAttachmentOcrLanguages NOTIFY
                       attachmentSearchChanged FINAL)
    /// The word and character count in the corner of a page (wide windows; the page's More menu
    /// always has it). Default on.
    Q_PROPERTY(bool wordCountShown READ wordCountShown WRITE setWordCountShown NOTIFY wordCountShownChanged FINAL)
    /// Focus mode (docs/UI.md "Statistics and focus mode"): on a page or in the journal no sidebar,
    /// top bar, side panel or bottom navigation — the text column alone, centred. F8 (a custom
    /// shortcut), Esc, the Back key or its corner button leave it. Kept on this device.
    Q_PROPERTY(bool focusMode READ focusMode WRITE setFocusMode NOTIFY focusModeChanged FINAL)
    /// Reading mode (docs/UI.md "Reading mode"): pages and the journal are shown read-only — a tap
    /// or click follows links, opens embeds and folds, but never starts editing or changes a note
    /// (PageController::readOnly). Ctrl+Shift+E (a custom shortcut), the page header's book
    /// button, the page menu, the palette and Settings → Editor. Kept on this device.
    Q_PROPERTY(bool readingMode READ readingMode WRITE setReadingMode NOTIFY readingModeChanged FINAL)
    /// Typewriter scrolling: the editor keeps the caret's line in the middle of the view.
    Q_PROPERTY(bool typewriterScrolling READ typewriterScrolling WRITE setTypewriterScrolling NOTIFY
                       typewriterScrollingChanged FINAL)
    /// Vim keys in the block editor and source mode (docs/UI.md "Vim mode"); off by default.
    Q_PROPERTY(bool vimMode READ vimMode WRITE setVimMode NOTIFY vimModeChanged FINAL)
    /// The export's choice for pictures from the internet (ExportController::webPictures):
    /// "link" (default: the addresses stay) | "include" (copies in the HTML and Pandoc exports).
    Q_PROPERTY(QString exportWebPictures READ exportWebPictures WRITE setExportWebPictures NOTIFY
                       exportWebPicturesChanged FINAL)

    // ---- Intro (first run: IntroView) -----------------------------------------------------
    /// The intro (language, look, permissions, the first notebook's settings) was finished or
    /// skipped. Never stored: true when a notebook was opened before (an update from a version
    /// without the intro), else false.
    Q_PROPERTY(bool introDone READ introDone WRITE setIntroDone NOTIFY introDoneChanged FINAL)
    /// The settings the intro chose for the first notebook (keys of Workspace.setConfig():
    /// journalMode, pageMode, weekStartsOn, defaultReminderTime, journalFileFormat); applied
    /// once to the next new (empty, without .brulion/config.json) notebook, then cleared
    /// (AppController::finishOpening). {} when none are waiting.
    Q_PROPERTY(QVariantMap newNotebookConfig READ newNotebookConfig WRITE setNewNotebookConfig NOTIFY
                       newNotebookConfigChanged FINAL)

    // ---- Vaults -------------------------------------------------------------------------
    /// Absolute path of the vault opened last ("" before the first one).
    Q_PROPERTY(QString currentVault READ currentVault NOTIFY currentVaultChanged FINAL)
    /// [{ path, name, lastOpened (ISO date-time) }], most recent first, at most MaxRecentVaults
    /// (the start screen and the notebook switcher list them: AppController.recentNotebooks).
    Q_PROPERTY(QVariantList recentVaults READ recentVaults NOTIFY recentVaultsChanged FINAL)
    /// Follow symlinked folders that point outside the notebook, in every notebook opened on this
    /// device (Vault::setFollowsOutsideLinks through the Workspace; default off). Device-local,
    /// never in the synced .brulion/config.json: a sync peer must not be able to switch it on.
    Q_PROPERTY(bool followsOutsideLinks READ followsOutsideLinks WRITE setFollowsOutsideLinks NOTIFY
                       followsOutsideLinksChanged FINAL)

    // ---- Application --------------------------------------------------------------------
    /// Desktop: closing the window keeps Brulion running in the system tray (reminders fire).
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged FINAL)
    /// Desktop: what a single left click on the tray icon does — "window" (show or hide the
    /// window) or "calendar" (the tray calendar, ShellTrayCalendar). Default: defaultTrayClick().
    Q_PROPERTY(QString trayClick READ trayClick WRITE setTrayClick NOTIFY trayClickChanged FINAL)

    // ---- Reminders ----------------------------------------------------------------------
    Q_PROPERTY(bool remindersEnabled READ remindersEnabled WRITE setRemindersEnabled NOTIFY remindersEnabledChanged FINAL)
    /// Snooze length offered by notifications, minutes (1 … 120, default 10).
    Q_PROPERTY(int snoozeMinutes READ snoozeMinutes WRITE setSnoozeMinutes NOTIFY snoozeMinutesChanged FINAL)
    /// How long an alarm rings before it stops by itself, minutes (1 … 60, default 10).
    Q_PROPERTY(int alarmRingMinutes READ alarmRingMinutes WRITE setAlarmRingMinutes NOTIFY alarmRingMinutesChanged FINAL)
    /// Show reminders that were due while the app/device was off.
    Q_PROPERTY(bool notifyMissed READ notifyMissed WRITE setNotifyMissed NOTIFY notifyMissedChanged FINAL)

    // ---- Nextcloud (WebDAV) sync --------------------------------------------------------
    // Whether it syncs and the server folder are per notebook (the current one; with no notebook
    // open nothing syncs); the server, the user and the password are shared. A notebook's are
    // stored under the id of its folder ("sync/nextcloud/notebooks/<Vault::idForPath>/…", the id
    // its device state and sync bookkeeping have: a symlink or a relative path is the same
    // notebook), with the notebook's path.
    Q_PROPERTY(bool nextcloudEnabled READ nextcloudEnabled WRITE setNextcloudEnabled NOTIFY nextcloudChanged FINAL)
    /// Server address, e.g. "https://cloud.example.com".
    Q_PROPERTY(QString nextcloudUrl READ nextcloudUrl WRITE setNextcloudUrl NOTIFY nextcloudChanged FINAL)
    Q_PROPERTY(QString nextcloudUser READ nextcloudUser WRITE setNextcloudUser NOTIFY nextcloudChanged FINAL)
    /// Folder on the server, relative to the user's files ("Brulion" when the notebook has none).
    Q_PROPERTY(QString nextcloudFolder READ nextcloudFolder WRITE setNextcloudFolder NOTIFY nextcloudChanged FINAL)
    /// Automatic sync interval in minutes (0 = only manually / after edits; default 15).
    Q_PROPERTY(int syncIntervalMinutes READ syncIntervalMinutes WRITE setSyncIntervalMinutes NOTIFY nextcloudChanged FINAL)
    /// An app password is stored (its value is never exposed to QML).
    Q_PROPERTY(bool hasNextcloudPassword READ hasNextcloudPassword NOTIFY nextcloudPasswordChanged FINAL)
    /// The current notebook's server folder is end-to-end encrypted (docs/SYNC-ENCRYPTION.md): set
    /// by WorkspaceSync::setUpEncryption()'s answer, with the notebook key beside it.
    Q_PROPERTY(bool nextcloudEncrypted READ nextcloudEncrypted WRITE setNextcloudEncrypted NOTIFY nextcloudChanged FINAL)
    /// This device holds the current notebook's key for its encrypted server folder.
    Q_PROPERTY(bool hasNextcloudNotebookKey READ hasNextcloudNotebookKey NOTIFY nextcloudChanged FINAL)

    // ---- A folder of this device (docs/ANDROID.md "A folder picked in the system's dialog") ----
    // A notebook in the app's own storage kept in sync with a folder of the device — on Android
    // one picked in the system's dialog (its tree URI), the app for Google Play's way to a Syncthing
    // folder. Per notebook, under the id of its folder ("sync/folder/notebooks/<Vault::idForPath>/
    // address|name|where"). These three describe the current notebook's.
    /// The current notebook syncs with a folder of this device.
    Q_PROPERTY(bool deviceFolderLinked READ deviceFolderLinked NOTIFY deviceFolderChanged FINAL)
    /// That folder's name ("Notatki"; "" without one).
    Q_PROPERTY(QString deviceFolderName READ deviceFolderName NOTIFY deviceFolderChanged FINAL)
    /// Where it is ("Phone storage › Syncthing/Notatki", deviceFolderPlace(); a path elsewhere; ""
    /// unknown).
    Q_PROPERTY(QString deviceFolderWhere READ deviceFolderWhere NOTIFY deviceFolderChanged FINAL)

public:
    /// `settingsFile`: INI file (empty = the platform's default location for the app).
    /// `dataDir`: app data folder for the secrets file (empty = QStandardPaths::AppDataLocation).
    explicit AppSettings(const QString &settingsFile = {}, const QString &dataDir = {}, QObject *parent = nullptr);
    ~AppSettings() override;

    static constexpr int MaxRecentVaults = 12;
    /// The server folder proposed for the first notebook set up with an account on this device.
    static constexpr auto NewAccountFolder = "Notatki";

    QString themeMode() const;
    void setThemeMode(const QString &mode);
    QString language() const;
    void setLanguage(const QString &language);
    qreal fontScale() const;
    void setFontScale(qreal scale);
    bool reducedMotion() const;
    void setReducedMotion(bool on);
    qreal interfaceScale() const;
    void setInterfaceScale(qreal scale);

    /// Ids for sidebarHidden: the navigation entries, then the sections.
    static QStringList sidebarItems();
    /// The sidebar's page sections in their default order: Favorites, Recent, All pages.
    static QStringList sidebarSectionIds();
    /// Views the bottom navigation can offer, and its default five (Journal, Pages, Search, Tasks,
    /// Calendar).
    static QStringList bottomNavViews();
    static QStringList defaultBottomNavTabs();
    QStringList railOrder() const;
    void setRailOrder(const QStringList &order);
    QStringList railViews() const;
    bool layoutFollowsNotebook() const;
    void setLayoutFollowsNotebook(bool on);
    /// The group of the icon bar `view` is in (0 the first; the app's own when railOrder is empty;
    /// a view railOrder leaves out: after its groups).
    Q_INVOKABLE int railGroup(const QString &view) const;
    /// The layout set here as the layout language's text (#rail, #panel, #bottom).
    Q_INVOKABLE QString layoutText() const;
    /// `text` (the layout language) applied to these settings — the places it writes; what does not
    /// read is left out: the warnings (uilayout::parse()).
    Q_INVOKABLE QStringList applyLayoutText(const QString &text);
    /// A font family name as stored: trimmed; "" for one longer than 128 characters or with
    /// control characters (settings files come from anywhere).
    static QString cleanFontFamily(const QString &family);

    QString accentColor() const;
    void setAccentColor(const QString &name);
    QString customAccent() const;
    void setCustomAccent(const QString &color);
    QString lightSurface() const;
    void setLightSurface(const QString &name);
    QString darkSurface() const;
    void setDarkSurface(const QString &name);
    QString uiFontFamily() const;
    void setUiFontFamily(const QString &family);
    QString density() const;
    void setDensity(const QString &density);
    QStringList sidebarHidden() const;
    void setSidebarHidden(const QStringList &ids);
    /// Show or hide one sidebar entry or section.
    Q_INVOKABLE void setSidebarItemShown(const QString &id, bool shown);
    QStringList sidebarSections() const;
    void setSidebarSections(const QStringList &ids);
    /// Move one page section up (`delta` -1) or down (+1) in the sidebar.
    Q_INVOKABLE void moveSidebarSection(const QString &id, int delta);
    QStringList bottomNavTabs() const;
    void setBottomNavTabs(const QStringList &views);
    /// Put `view` into slot `index` (0 … 4) of the bottom navigation; a view already in another
    /// slot swaps places with the one it replaces.
    Q_INVOKABLE void setBottomNavTab(int index, const QString &view);
    bool captureButtonShown() const;
    bool railExpands() const;
    void setRailExpands(bool on);
    bool gymEnabled() const;
    void setGymEnabled(bool on);
    bool habitsEnabled() const;
    void setHabitsEnabled(bool on);
    bool assistantEnabled() const;
    void setAssistantEnabled(bool on);
    bool flashcardsEnabled() const;
    void setFlashcardsEnabled(bool on);
    int flashcardsNewPerDay() const;
    void setFlashcardsNewPerDay(int n);
    int flashcardsRetention() const;
    void setFlashcardsRetention(int percent);
    QString flashcardsTags() const;
    void setFlashcardsTags(const QString &tags);
    /// The flashcards' settings as the extension takes them ({tags, newPerDay, retention}).
    QVariantMap flashcardsSettings() const;
    /// The ids of the built-in extensions switched on ("silownia", "asystent").
    QStringList enabledExtensions() const;
    QString aiProvider() const;
    void setAiProvider(const QString &provider);
    QString aiModel() const;
    void setAiModel(const QString &model);
    QString aiLocalModel() const;
    void setAiLocalModel(const QString &id);
    QString aiBaseUrl() const;
    bool aiUnencrypted() const;
    void setAiBaseUrl(const QString &url);
    bool hasAiKey() const;
    /// Stores the AI key ("" forgets it); false when it could not be written.
    Q_INVOKABLE bool setAiKey(const QString &key);
    QString aiKey() const;
    QList<AgentConfig> acpAgents() const;
    QVariantList acpAgentRows() const;
    bool acpShare() const;
    void setAcpShare(bool on);
    QString acpShareAddress() const;
    void setAcpShareAddress(const QString &address);
    bool acpShareTrust() const;
    void setAcpShareTrust(bool on);
    bool chatHooks() const;
    void setChatHooks(bool on);
    QString chatHooksAddress() const;
    void setChatHooksAddress(const QString &address);
    int chatHooksPort() const;
    void setChatHooksPort(int port);
    QString chatHooksPublicUrl() const;
    void setChatHooksPublicUrl(const QString &url);
    QString aiChatNotify() const;
    void setAiChatNotify(const QString &mode);
    QString aiDefaultPartner() const;
    void setAiDefaultPartner(const QString &partner);
    bool aiAgentNotify() const;
    void setAiAgentNotify(bool on);
    bool extensionsNotify() const;
    void setExtensionsNotify(bool on);
    QString appMode() const;
    void setAppMode(const QString &mode);
    QString aiChatAutoArchive() const;
    void setAiChatAutoArchive(const QString &days);
    /// aiChatAutoArchive() in days (0: never).
    int aiChatAutoArchiveDays() const;
    QString codeAutoSettle() const;
    void setCodeAutoSettle(const QString &days);
    /// codeAutoSettle() in days (0: never).
    int codeAutoSettleDays() const;
    QString codeWorktreeCleanup() const;
    void setCodeWorktreeCleanup(const QString &when);
    QString chatToolCalls() const;
    void setChatToolCalls(const QString &shown);
    bool acpAvailable() const;
    bool acpProgramsAvailable() const;
    bool mcpServerEnabled() const;
    void setMcpServerEnabled(bool on);
    int mcpServerPort() const;
    void setMcpServerPort(int port);
    QString mcpServerPublicUrl() const;
    void setMcpServerPublicUrl(const QString &url);
    bool mcpServerReadOnly() const;
    void setMcpServerReadOnly(bool on);
    bool hasMcpServerPassword() const;
    /// The sign-in password of the MCP server: at least 8 characters, at most 1024 ("" forgets it);
    /// false when it is too short or long or could not be written.
    Q_INVOKABLE bool setMcpServerPassword(const QString &password);
    /// The file that keeps it (passed to the server as --password-file).
    QString mcpServerPasswordFile() const;
    /// Agents of another computer this device was paired with (WorkspaceAgents::paired): each
    /// added — or, the same agent of the same computer, given its new token —, its token kept in
    /// the app's secrets (`<app data>/secrets/acp-<id>`, owner-only). Their ids.
    QStringList addRemoteAcpAgents(const QList<AgentConfig> &agents);
    /// Adds an agent: one of WorkspaceAgents::presets() by its id ("claude", "codex", "gemini"),
    /// or "" for one of the user's own (no command yet). Its id ("claude-2" when one is there).
    Q_INVOKABLE QString addAcpAgent(const QString &preset);
    /// Changes the agent `id`'s name or command (empty: left as it is) / Brulion's tools.
    Q_INVOKABLE void setAcpAgentName(const QString &id, const QString &name);
    Q_INVOKABLE void setAcpAgentCommand(const QString &id, const QString &command);
    Q_INVOKABLE void setAcpAgentTools(const QString &id, bool on);
    /// "Let it change the app while it runs" (AgentConfig::appAccess; an agent on another computer
    /// reaches this app through that computer's tools).
    Q_INVOKABLE void setAcpAgentApp(const QString &id, bool on);
    Q_INVOKABLE void removeAcpAgent(const QString &id);
    /// What the chat learnt of agent `id`: the options chosen for its sessions and the options its
    /// last session offered (AgentConfig::options, ::known).
    void setAcpAgentMemory(const QString &id, const QJsonObject &options, const QJsonArray &known);
    /// The agents a preset offers ({id, name, command}), for the settings' menu (none where no
    /// programs start).
    Q_INVOKABLE QVariantList acpPresets() const;
    void setCaptureButtonShown(bool on);
    /// Back to the default look: accent, backgrounds, fonts, density, line spacing, sizes (the
    /// visibility of elements stays).
    Q_INVOKABLE void resetAppearance();

    QString editorFont() const;
    void setEditorFont(const QString &font);
    QString editorFontFamily() const;
    void setEditorFontFamily(const QString &family);
    QString editorMonoFamily() const;
    void setEditorMonoFamily(const QString &family);
    QString editorLineSpacing() const;
    void setEditorLineSpacing(const QString &spacing);
    int editorFontSize() const;
    void setEditorFontSize(int px);
    int editorWidth() const;
    void setEditorWidth(int px);
    bool editorSourceMode() const;
    void setEditorSourceMode(bool on);
    bool propertiesExpanded() const;
    void setPropertiesExpanded(bool on);
    bool webPicturesShown() const;
    void setWebPicturesShown(bool on);
    bool pictureFrames() const;
    void setPictureFrames(bool on);
    bool lightPictureBacking() const;
    void setLightPictureBacking(bool on);
    bool codeLineNumbers() const;
    void setCodeLineNumbers(bool on);
    bool terminalAtrament() const;
    void setTerminalAtrament(bool on);
    QString exportWebPictures() const;
    void setExportWebPictures(const QString &mode);
    bool editorAtrament() const;
    void setEditorAtrament(bool on);
    bool spellCheck() const;
    void setSpellCheck(bool on);
    bool taskStatusHistory() const;
    void setTaskStatusHistory(bool on);
    QString spellLanguages() const;
    void setSpellLanguages(const QString &languages);
    bool spellWholePage() const;
    void setSpellWholePage(bool on);
    QStringList spellingWords() const;
    void setSpellingWords(const QStringList &words);
    bool attachmentSearch() const;
    void setAttachmentSearch(bool on);
    bool attachmentOcr() const;
    void setAttachmentOcr(bool on);
    QString attachmentOcrLanguages() const;
    void setAttachmentOcrLanguages(const QString &languages);
    bool wordCountShown() const;
    void setWordCountShown(bool on);
    bool focusMode() const;
    void setFocusMode(bool on);
    bool readingMode() const;
    void setReadingMode(bool on);
    bool typewriterScrolling() const;
    bool vimMode() const;
    void setVimMode(bool on);
    void setTypewriterScrolling(bool on);

    /// Custom keyboard shortcuts (ShortcutMap): the stored values of group "shortcuts", command id
    /// -> what was stored (a string as written, "" for none; anything else is malformed and left to
    /// ShortcutMap to ignore).
    QVariantMap shortcutOverrides() const;
    /// Store `id`'s custom shortcut (portable text, "" for none); nullopt removes it (the default).
    void setShortcutOverride(const QString &id, const std::optional<QString> &sequence);
    /// Remove every custom shortcut.
    void clearShortcutOverrides();

    bool introDone() const;
    void setIntroDone(bool done);
    QVariantMap newNotebookConfig() const;
    /// Keeps only the known keys with usable values; {} clears it.
    void setNewNotebookConfig(const QVariantMap &config);

    QString currentVault() const;
    void setCurrentVault(const QString &path);
    QVariantList recentVaults() const;
    bool followsOutsideLinks() const;
    void setFollowsOutsideLinks(bool on);
    /// The one-time check of devices that followed folder links to outside before that became the
    /// setting above: while it was never chosen here (no key), each notebook's first open asks
    /// once. Switched on (and stored) only when this device knew the notebook before — `knownBefore`
    /// (its device state folder had content: nothing a sync peer can create) or one of the recent
    /// notebooks — and its folder has such a link (`outsideLinks`, Vault::skippedOutsideLinks()).
    /// Checked for good either way: a link planted later, or a notebook new here, never turns it on.
    void checkOutsideLinksOnce(const QString &notebook, bool knownBefore, bool outsideLinks);
    /// Put `path` first in the recent list (and make it current).
    /// `when`: when it was opened (the app's clock, `--now`; invalid: now).
    void addRecentVault(const QString &path, const QDateTime &when = {});
    Q_INVOKABLE void forgetRecentVault(const QString &path);

    bool closeToTray() const;
    void setCloseToTray(bool on);
    QString trayClick() const;
    /// Anything but "window" / "calendar" stores the default.
    void setTrayClick(const QString &action);
    /// "calendar" (the tray calendar, as a click on the clock of Windows or of a Linux panel).
    static QString defaultTrayClick();

    bool remindersEnabled() const;
    void setRemindersEnabled(bool on);
    int snoozeMinutes() const;
    void setSnoozeMinutes(int minutes);
    int alarmRingMinutes() const;
    void setAlarmRingMinutes(int minutes);
    bool notifyMissed() const;
    void setNotifyMissed(bool on);

    bool nextcloudEnabled() const;
    void setNextcloudEnabled(bool on);
    QString nextcloudUrl() const;
    void setNextcloudUrl(const QString &url);
    QString nextcloudUser() const;
    void setNextcloudUser(const QString &user);
    QString nextcloudFolder() const;
    void setNextcloudFolder(const QString &folder);
    /// The current notebook has a server folder of its own (chosen, or kept from its last sync).
    bool hasOwnNextcloudFolder() const;
    /// The server folder the settings form shows for the current notebook: its own; for one that
    /// has none, "Notatki" (NewAccountFolder) when no account is set up yet, else the notebook
    /// folder's name (without characters a server folder cannot have) — and never a folder another notebook on this
    /// device has, holds or is inside ("Praca 2" then), nor the one of 0.3.0 waiting for its
    /// notebook.
    QString proposedNextcloudFolder() const;
    /// Another notebook on this device that syncs with `folder`, a folder inside it or around it
    /// (its switch is on and its folder exists; not `except`, default: the current notebook): its
    /// path, "" when none. Their notes would mix on the server.
    QString nextcloudFolderUser(const QString &folder) const;
    QString nextcloudFolderUser(const QString &folder, const QString &except) const;
    /// Switch Nextcloud sync off in every notebook (`exceptCurrent`: but the current one), also
    /// the 0.3.0 switch still waiting for its notebook. The account is shared: without it (signed
    /// out) or with another one, the others must not sync by themselves.
    void disableNextcloudEverywhere(bool exceptCurrent = false);

    // Up to 0.3.0 one switch and one server folder applied to every notebook ("sync/nextcloud/
    // enabled", "…/folder"). They wait, read by nothing but notebookOpened(), for the notebook
    // they belong to.
    /// The settings of 0.3.0 or older wait for their notebook.
    bool hasSharedNextcloud() const;
    /// Remember `notebook` as theirs: the notebook that was current when they were first seen
    /// (AppController: not the documentation, then the last other notebook). Once; "" records
    /// that none is.
    void recordSharedNextcloudOwner(const QString &notebook);
    /// `notebook` was opened (never called for the documentation). `syncedRemote`: what its sync
    /// bookkeeping names ("remote" of "sync/meta" in its LocalStore, nextcloudRemote()). It takes
    /// over the settings of 0.3.0 when they are its own: its bookkeeping names exactly that
    /// account and folder, or it is the recorded notebook — any other notebook never does (its
    /// notes would flow into that folder). The switch stays off when another notebook syncs with
    /// that folder meanwhile. A notebook with sync off and no server folder of its own keeps the
    /// folder its bookkeeping names on this account, so turning sync on continues there.
    void notebookOpened(const QString &notebook, const QString &syncedRemote);
    int syncIntervalMinutes() const;
    void setSyncIntervalMinutes(int minutes);

    bool hasNextcloudPassword() const;
    /// Store the app password (owner-only file); "" deletes it. Returns false on I/O errors.
    Q_INVOKABLE bool setNextcloudPassword(const QString &password);
    /// The stored password (C++ only: the sync integration reads it; never exposed to QML).
    QString nextcloudPassword() const;
    /// File that holds the password.
    QString passwordFile() const;
    /// The password stored for the app data folder `dataDir`, read without an AppSettings (and so
    /// without touching the settings file): Android's background check, in a process of its own.
    static QString storedNextcloudPassword(const QString &dataDir);

    bool nextcloudEncrypted() const;
    void setNextcloudEncrypted(bool on);
    bool hasNextcloudNotebookKey() const;
    /// The current notebook's key for its encrypted server folder (C++ only; never in QML): 32
    /// bytes, or empty. Kept like the app password, in its own owner-only file per notebook
    /// (`<app data>/secrets/sync-key-<notebook id>`).
    QByteArray nextcloudNotebookKey() const;
    /// Store it ("" deletes it). Returns false on I/O errors.
    bool setNextcloudNotebookKey(const QByteArray &key);

    /// A notebook's folder of this device.
    struct DeviceFolderLink {
        QString address; ///< Android: the picked folder's tree URI; elsewhere its path
        QString name;
        QString where;
        bool isEmpty() const { return address.isEmpty(); }
        friend bool operator==(const DeviceFolderLink &, const DeviceFolderLink &) = default;
    };
    /// `notebook`'s folder of this device (empty: none).
    DeviceFolderLink deviceFolderLink(const QString &notebook) const;
    /// Link `notebook` to a folder of this device; an empty link removes it.
    void setDeviceFolderLink(const QString &notebook, const DeviceFolderLink &link);
    /// The notebook linked to the folder `address` ("" when none is): one folder, one notebook.
    QString deviceFolderNotebook(const QString &address) const;
    bool deviceFolderLinked() const { return !deviceFolderLink(currentVault()).isEmpty(); }
    QString deviceFolderName() const { return deviceFolderLink(currentVault()).name; }
    QString deviceFolderWhere() const { return deviceFolderPlace(deviceFolderLink(currentVault()).where); }
    /// Where a folder is, as people say it: Android's "primary:Documents/Notatki" is "Phone
    /// storage › Documents/Notatki", "1A2B-3C4D:Notatki" "Memory card › Notatki"; anything else as
    /// it is (a path).
    static QString deviceFolderPlace(const QString &where);

    /// Generic preference for other app areas (key with an area prefix, e.g. "views/tasks/x").
    /// The value comes back with the type of `defaultValue` (bool, int, double, …) — the INI
    /// file stores only text; one that does not convert gives `defaultValue`.
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &defaultValue = {}) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);

    // ---- SearchView's recent searches ("search/recent") ------------------------------------
    /// The recent searches, newest first (a single stored text is a list of one).
    Q_INVOKABLE QStringList recentSearches() const;
    /// Put `query` (trimmed; "" changes nothing) first, without an older one that is the same
    /// after folding case and diacritics, keep at most 8 and store them; returns the new list.
    Q_INVOKABLE QStringList rememberSearch(const QString &query);
    /// Forget the recent searches.
    Q_INVOKABLE void clearRecentSearches();

    /// Write pending changes to disk now (QSettings also does it by itself).
    void sync();
    /// The settings file in use.
    QString fileName() const;

Q_SIGNALS:
    void introDoneChanged();
    void newNotebookConfigChanged();
    void themeModeChanged();
    void languageChanged();
    void fontScaleChanged();
    void reducedMotionChanged();
    void interfaceScaleChanged();
    void accentChanged();
    void surfacesChanged();
    void fontFamiliesChanged();
    void densityChanged();
    void sidebarHiddenChanged();
    void sidebarSectionsChanged();
    void bottomNavTabsChanged();
    void railOrderChanged();
    void layoutFollowsNotebookChanged();
    void captureButtonShownChanged();
    void railExpandsChanged();
    void gymEnabledChanged();
    void habitsEnabledChanged();
    void assistantEnabledChanged();
    void flashcardsEnabledChanged();
    void flashcardsSettingsChanged();
    void aiChanged();
    void acpAgentsChanged();
    void mcpServerChanged();
    void acpShareChanged();
    void chatHooksChanged();
    void aiChatNotifyChanged();
    void aiDefaultPartnerChanged();
    void aiAgentNotifyChanged();
    void extensionsNotifyChanged();
    void appModeChanged();
    void aiChatAutoArchiveChanged();
    void codeAutoSettleChanged();
    void codeWorktreeCleanupChanged();
    void chatToolCallsChanged();
    void editorLineSpacingChanged();
    void editorFontChanged();
    void editorFontSizeChanged();
    void editorWidthChanged();
    void editorSourceModeChanged();
    void propertiesExpandedChanged();
    void webPicturesShownChanged();
    void pictureFramesChanged();
    void lightPictureBackingChanged();
    void codeLineNumbersChanged();
    void terminalAtramentChanged();
    void exportWebPicturesChanged();
    void editorAtramentChanged();
    void spellCheckChanged();
    void taskStatusHistoryChanged();
    void spellLanguagesChanged();
    void spellWholePageChanged();
    void spellingWordsChanged();
    void attachmentSearchChanged();
    void wordCountShownChanged();
    void focusModeChanged();
    void readingModeChanged();
    void typewriterScrollingChanged();
    void vimModeChanged();
    void currentVaultChanged();
    void recentVaultsChanged();
    void followsOutsideLinksChanged();
    void closeToTrayChanged();
    void trayClickChanged();
    void remindersEnabledChanged();
    void snoozeMinutesChanged();
    void alarmRingMinutesChanged();
    void notifyMissedChanged();
    void nextcloudChanged();
    void nextcloudPasswordChanged();
    /// A notebook's folder of this device was linked or unlinked (also: another notebook is current).
    void deviceFolderChanged();
    void valueChanged(const QString &key);

private:
    void storeAcpAgents(const QList<AgentConfig> &agents);
    template<typename T>
    bool store(QAnyStringView key, const T &value, const T &current);

    /// The sync settings of one notebook's group in "sync/nextcloud/notebooks".
    struct NotebookSync {
        QString id;
        QString path; ///< "" when unknown (written by 0.3.1–0.3.5 for a notebook no longer recent)
        bool enabled = false;
        bool hasFolder = false;
        QString folder;
    };
    QList<NotebookSync> notebookSyncs() const;
    /// The key of setting `name` of `notebook` ("" without a notebook): in its group, or where
    /// 0.3.1–0.3.5 wrote it (the hash of the path as written) until it is moved.
    QString notebookKey(const QString &notebook, QStringView name) const;
    bool hasNotebookValue(const QString &notebook, QStringView name) const;
    void setNotebookValue(const QString &notebook, QStringView name, const QVariant &value);
    /// The id of `notebook`'s group ("" without a notebook), made ready for a value: its old keys
    /// adopted and its path recorded.
    QString notebookGroup(const QString &notebook);
    template<typename T>
    bool storeNotebookValue(const QString &notebook, QStringView name, const T &value, const T &current);
    /// Move the settings 0.3.1–0.3.5 wrote for `notebook` under the hash of its path as written
    /// into its group (when its folder exists: the id is the canonical path), and record its path.
    void adoptNotebookKeys(const QString &notebook);
    bool claimSharedNextcloud(const QString &notebook, const QString &syncedRemote);

    std::unique_ptr<QSettings> m_settings;
    QString m_dataDir;
    /// The app password is masked in every log line (logging::redact) while it is stored.
    mutable logging::ScopedSecret m_passwordSecret;
    mutable logging::ScopedSecret m_aiKeySecret;
    mutable logging::ScopedSecret m_mcpPasswordSecret;
};

} // namespace brulion::shell
