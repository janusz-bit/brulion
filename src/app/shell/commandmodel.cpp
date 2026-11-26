#include "commandmodel.h"

#include "mediaengine.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "codeprojects.h"
#include "shortcutmap.h"
#include "workspace.h"
#include "workspacechats.h"

#include "notebook/tasktools.h"
#include "util/paths.h"

#include <QCoreApplication>
#include <QEvent>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

struct Command {
    QString id;
    QString title;
    QString subtitle;
    QString icon;
    QString shortcut;
    bool enabled = true;
    /// "" everywhere, "code" only in Code mode (its shortcut too: ShortcutMap's scopes).
    QString scope{};
};

/// `c` for the palette, with the shortcut it has now (ShortcutMap: custom or default).
QVariantMap toMap(const Command &c, const ShortcutMap *shortcuts)
{
    // (A command that is not a shortcut action — a project's script while another project is shown —
    // shows its own.)
    return {{u"id"_s, c.id},
            {u"title"_s, c.title},
            {u"subtitle"_s, c.subtitle},
            {u"icon"_s, c.icon},
            {u"shortcut"_s, shortcuts && !shortcuts->title(c.id).isEmpty() ? shortcuts->sequence(c.id) : c.shortcut},
            {u"enabled"_s, c.enabled}};
}

/// `commands` into `list` right after the command `id` (at the end when there is none).
void insertAfter(QList<Command> &list, const QString &id, const QList<Command> &commands)
{
    const auto it = std::ranges::find(std::as_const(list), id, &Command::id);
    qsizetype at = it == list.cend() ? list.size() : it - list.cbegin() + 1;
    for (const Command &c : commands)
        list.insert(at++, c);
}

/// The commands the shell carries out (AppController::uiActionRequested: it opens the dialogs, the
/// editor on screen copies the links) and their uiAction.
struct UiAction {
    QStringView id;
    QStringView action;
};
constexpr UiAction kUiActions[] = {
        {u"page.new", u"newPage"},
        {u"page.switch", u"switcher"},
        {u"page.find", u"findInNote"},
        {u"page.replace", u"replaceInNote"},
        {u"page.rename", u"renamePage"},
        {u"page.export", u"exportPage"},
        {u"page.print", u"printPage"},
        {u"page.source", u"toggleSourceMode"},
        {u"page.mode", u"toggleNoteMode"},
        {u"page.addProperty", u"addProperty"},
        {u"page.saveWebPictures", u"saveWebPictures"},
        {u"page.copyLink", u"copyLink:page"},
        {u"page.qrCode", u"qrCode"},
        {u"voice.record", u"recordVoiceNote"},
        {u"block.copyRef", u"copyLink:blockRef"},
        {u"block.copyLink", u"copyLink:blockLink"},
        {u"heading.copyLink", u"copyLink:heading"},
        {u"task.clock", u"taskClock"},
        {u"task.addDependency", u"taskDependency"},
        {u"ai.rewrite", u"writing:rewrite"},
        {u"ai.continue", u"writing:continue"},
        {u"ai.summarize", u"writing:summarize"},
        {u"folder.new", u"newFolder"},
        {u"page.move", u"movePage"},
        {u"page.reveal", u"revealInTree"},
        {u"page.trash", u"trashPage"},
        {u"encrypt.selection", u"encryptSelection"},
        {u"encrypt.page", u"encryptPage"},
        {u"encrypt.password", u"changePagePassword"},
        {u"encrypt.decrypt", u"decryptPage"},
        {u"encrypt.newPage", u"newEncryptedPage"},
        {u"theme.toggle", u"toggleTheme"},
        {u"about", u"about"},
        // Code mode's terminal panel (CodeView: TerminalPanel).
        {u"terminal.toggle", u"terminal:toggle"},
        {u"terminal.new", u"terminal:new"},
        {u"terminal.splitRight", u"terminal:splitRight"},
        {u"terminal.splitDown", u"terminal:splitDown"},
        {u"terminal.close", u"terminal:close"},
        {u"terminal.next", u"terminal:next"},
        {u"terminal.previous", u"terminal:previous"},
};

/// The uiAction of the command `id`, "" for a command that runs in C++.
QString uiActionOf(const QString &id)
{
    for (const UiAction &ui : kUiActions) {
        if (ui.id == id)
            return ui.action.toString();
    }
    return {};
}

/// The weekly or monthly note of a journal command: -1 the last one, 1 the next one, 0 this one.
int periodOffset(const QString &id)
{
    if (id.startsWith(u"journal.last"))
        return -1;
    return id.startsWith(u"journal.next") ? 1 : 0;
}

} // namespace

CommandModel::CommandModel(AppController *app) : QObject(nullptr), m_app(app)
{
    // Titles depend on the language; enabled states on the app's state.
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installEventFilter(this);
    connect(app, &AppController::statusChanged, this, &CommandModel::commandsChanged);
    connect(app, &AppController::currentRouteChanged, this, &CommandModel::commandsChanged);
    connect(app, &AppController::historyChanged, this, &CommandModel::commandsChanged);
    connect(app, &AppController::tabsChanged, this, &CommandModel::commandsChanged);
    connect(app, &AppController::syncStatusChanged, this, &CommandModel::commandsChanged);
    connect(app, &AppController::sidePanelAllowedChanged, this, &CommandModel::commandsChanged);
    connect(app, &AppController::sourceModeShownChanged, this, &CommandModel::commandsChanged);
    connect(app, &AppController::languageChanged, this, &CommandModel::commandsChanged);
    connect(app->workspace(), &Workspace::favoritesChanged, this, &CommandModel::commandsChanged);
    connect(app->workspace()->extensions(), &WorkspaceExtensions::commandsChanged, this, &CommandModel::commandsChanged);
    connect(app->settings(), &AppSettings::focusModeChanged, this, &CommandModel::commandsChanged);
    connect(app->settings(), &AppSettings::readingModeChanged, this, &CommandModel::commandsChanged);
    connect(app, &AppController::interfaceScaleChanged, this, &CommandModel::commandsChanged);
    connect(app->workspace()->encryption(), &WorkspaceEncryption::unlockedChanged, this,
            &CommandModel::commandsChanged);
    connect(app->appLock(), &AppLockController::changed, this, &CommandModel::commandsChanged);
    connect(app->appLock(), &AppLockController::lockedChanged, this, &CommandModel::commandsChanged);
    connect(app->settings(), &AppSettings::typewriterScrollingChanged, this, &CommandModel::commandsChanged);
    connect(app->settings(), &AppSettings::vimModeChanged, this, &CommandModel::commandsChanged);
    // The shortcuts' list: these commands with their default shortcuts (titles in the language:
    // again after a language switch, eventFilter()).
    if (ShortcutMap *shortcuts = app->shortcuts()) {
        shortcuts->setActions(shortcutActions());
        connect(shortcuts, &ShortcutMap::changed, this, &CommandModel::commandsChanged);
        // The AI's commands come and go with the assistant (their shortcuts too: "AI: Rewrite…").
        connect(app->settings(), &AppSettings::assistantEnabledChanged, this, [this, shortcuts] {
            shortcuts->setActions(shortcutActions());
            Q_EMIT commandsChanged();
        });
    }
    // Code mode's scripts: the shown project's (its scripts changed, another project shown).
    connect(app->workspace()->projects(), &CodeProjects::changed, this, [this] {
        refreshShortcutActions();
        Q_EMIT commandsChanged();
    });
    connect(app, &AppController::currentRouteChanged, this, &CommandModel::refreshShortcutActions);
    // The extensions' commands load after the app started (the notebook's pages): Settings →
    // Keyboard shortcuts lists them once they are there.
    connect(app->workspace()->extensions(), &WorkspaceExtensions::commandsChanged, this,
            &CommandModel::refreshShortcutActions);
}

void CommandModel::refreshShortcutActions()
{
    ShortcutMap *shortcuts = m_app->shortcuts();
    if (!shortcuts)
        return;
    const QList<ShortcutMap::Action> actions = shortcutActions();
    const QList<ShortcutMap::Action> &now = shortcuts->actions();
    const bool same =
            actions.size() == now.size() && std::ranges::equal(actions, now, [](const auto &a, const auto &b) {
                return a.id == b.id && a.title == b.title && a.defaultSequence == b.defaultSequence && a.scope == b.scope;
            });
    if (!same)
        shortcuts->setActions(actions);
}

CommandModel::~CommandModel()
{
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeEventFilter(this);
}

bool CommandModel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        if (ShortcutMap *shortcuts = m_app->shortcuts())
            shortcuts->setActions(shortcutActions());
        Q_EMIT commandsChanged();
    }
    return QObject::eventFilter(watched, event);
}

static QList<Command> buildCommands(const AppController *app)
{
    const bool ready = app->status() == u"ready";
    const QVariantMap route = app->currentRoute();
    const QString view = route.value(u"view"_s).toString();
    const bool onPage = ready && view == u"page";
    // Views showing one page in the editor: pages and daily notes.
    const bool inEditor = ready && (onPage || view == u"journal" || view == u"journals");
    const QString pagePath = route.value(u"path"_s).toString();
    const bool favorite = onPage && app->workspace()->isFavorite(pagePath);
    const bool english = app->settings()->language() == u"en";

    return {
            {u"journal.today"_s, CommandModel::tr("Today's journal"), {}, u"notebook-pen"_s, u"Ctrl+J"_s, ready},
            {u"journal.yesterday"_s, CommandModel::tr("Yesterday's journal"), {}, u"calendar-days"_s, {}, ready},
            {u"journal.tomorrow"_s, CommandModel::tr("Tomorrow's journal"), {}, u"calendar-plus"_s, {}, ready},
            // Weekly and monthly notes (docs/SYNTAX.md §1).
            {u"journal.week"_s, CommandModel::tr("This week's note"), CommandModel::tr("Weekly note"),
             u"calendar-range"_s, u"Ctrl+Alt+W"_s, ready},
            {u"journal.lastWeek"_s,
             CommandModel::tr("Last week's note"),
             CommandModel::tr("Weekly note"),
             u"calendar-range"_s,
             {},
             ready},
            {u"journal.nextWeek"_s,
             CommandModel::tr("Next week's note"),
             CommandModel::tr("Weekly note"),
             u"calendar-range"_s,
             {},
             ready},
            {u"journal.month"_s, CommandModel::tr("This month's note"), CommandModel::tr("Monthly note"),
             u"calendar-days"_s, u"Ctrl+Alt+M"_s, ready},
            {u"journal.lastMonth"_s,
             CommandModel::tr("Last month's note"),
             CommandModel::tr("Monthly note"),
             u"calendar-days"_s,
             {},
             ready},
            {u"journal.nextMonth"_s,
             CommandModel::tr("Next month's note"),
             CommandModel::tr("Monthly note"),
             u"calendar-days"_s,
             {},
             ready},
            {u"page.new"_s, CommandModel::tr("New page"), {}, u"square-pen"_s, u"Ctrl+N"_s, ready},
            {u"page.switch"_s, CommandModel::tr("Go to page…"), {}, u"file-text"_s, u"Ctrl+O"_s, ready},
            {u"capture"_s, CommandModel::tr("Quick capture"), CommandModel::tr("Add a note to today's journal"),
             u"plus"_s, u"Ctrl+Shift+N"_s, ready},
            // A voice note (docs/UI.md "Voice notes", uiAction "recordVoiceNote"): at the caret of the
            // page on screen, else at the end of today's journal; builds that record nothing: off.
            {u"voice.record"_s,
             CommandModel::tr("Record voice note"),
             CommandModel::tr("Into the note on screen, or today's journal; transcribed on this device"),
             u"mic"_s,
             {},
             ready && media::mediaSupported()},
            {u"go.journals"_s, CommandModel::tr("Journals"), {}, u"book-open"_s, {}, ready},
            {u"go.calendar"_s, CommandModel::tr("Calendar"), {}, u"calendar"_s, {}, ready},
            {u"go.tasks"_s, CommandModel::tr("Tasks"), {}, u"list-todo"_s, {}, ready},
            {u"go.habits"_s,
             CommandModel::tr("Habits"),
             CommandModel::tr("Tick today's habits, streaks, month view"),
             u"flame"_s,
             {},
             ready},
            {u"go.allPages"_s, CommandModel::tr("All pages"), {}, u"files"_s, {}, ready},
            {u"go.tags"_s, CommandModel::tr("Tags"), {}, u"tags"_s, {}, ready},
            {u"go.graph"_s, CommandModel::tr("Graph"), {}, u"waypoints"_s, {}, ready},
            {u"go.search"_s, CommandModel::tr("Search"), {}, u"search"_s, u"Ctrl+Shift+F"_s, ready},
            // (The open note's find bar; in the journal stream the day being written in.)
            {u"page.find"_s, CommandModel::tr("Find in note"), CommandModel::tr("And replace"), u"search"_s,
             u"Ctrl+F"_s, inEditor},
            {u"page.replace"_s, CommandModel::tr("Find and replace in note"), {}, u"search"_s, u"Ctrl+H"_s, inEditor},
            {u"go.replace"_s,
             CommandModel::tr("Replace in notebook"),
             CommandModel::tr("Find and replace in every note, with a preview"),
             u"search"_s,
             {},
             ready && !app->inDocsVault()},
            {u"page.favorite"_s,
             favorite ? CommandModel::tr("Remove from favorites") : CommandModel::tr("Add to favorites"),
             {},
             u"star"_s,
             {},
             onPage},
            {u"page.history"_s, CommandModel::tr("Page history"), {}, u"history"_s, {}, onPage},
            // One page as Markdown, HTML, PDF, Word… and printing it (ContentExportDialog of the
            // page on screen: uiActions "exportPage", "printPage").
            {u"page.export"_s,
             CommandModel::tr("Export page…"),
             CommandModel::tr("Markdown, HTML, PDF, Word"),
             u"share"_s,
             {},
             onPage},
            {u"page.print"_s, CommandModel::tr("Print page…"), CommandModel::tr("Or save it as PDF"), u"printer"_s,
             u"Ctrl+P"_s, onPage},
            // The page on screen added to a publishing target (docs/UI.md "Publishing").
            {u"page.publish"_s,
             CommandModel::tr("Publish page…"),
             CommandModel::tr("GitHub Pages, Nextcloud, your own server"),
             u"globe"_s,
             {},
             onPage && !app->inDocsVault()},
            // (The shell's rename dialog; a daily note is named by its date.)
            {u"page.rename"_s, CommandModel::tr("Rename page…"), {}, u"pencil"_s, u"F2"_s, onPage},
            // The editor's Ctrl+E — the only way on a phone to leave source mode once it is on.
            {u"page.source"_s, CommandModel::tr("Markdown source"),
             CommandModel::tr("Switch between blocks and the page's text"), u"code"_s, u"Ctrl+E"_s, inEditor},
            {u"nav.back"_s, CommandModel::tr("Back"), {}, u"arrow-left"_s, u"Alt+Left"_s, app->canGoBack()},
            {u"nav.forward"_s, CommandModel::tr("Forward"), {}, u"arrow-right"_s, u"Alt+Right"_s, app->canGoForward()},
            {u"panel.toggle"_s, CommandModel::tr("Toggle side panel"), {}, u"panel-right"_s, u"Ctrl+\\"_s, ready},
            // Focus mode and typewriter scrolling (docs/UI.md "Statistics and focus mode"): device
            // settings; the bars go only where an editor is shown.
            {u"view.focus"_s,
             app->settings()->focusMode() ? CommandModel::tr("Leave focus mode") : CommandModel::tr("Focus mode"),
             CommandModel::tr("Only the text, without side bars and toolbars"), u"maximize"_s, u"F8"_s, true},
            // Reading mode (docs/UI.md "Reading mode"): a device setting; pages and the journal
            // read-only.
            {u"view.reading"_s,
             app->settings()->readingMode() ? CommandModel::tr("Leave reading mode") : CommandModel::tr("Reading mode"),
             CommandModel::tr("Read notes without changing them by accident"),
             // (The book is the journal's; leaving the mode: the pencil, as in the page menu.)
             app->settings()->readingMode() ? u"pencil"_s : u"eye"_s, u"Ctrl+Shift+E"_s, true},
            {u"view.typewriter"_s,
             app->settings()->typewriterScrolling() ? CommandModel::tr("Turn off typewriter scrolling")
                                                    : CommandModel::tr("Typewriter scrolling"),
             CommandModel::tr("Keep the line being written in the middle of the screen"),
             u"text-select"_s,
             {},
             true},
            // Vim keys (docs/UI.md "Vim mode"): a device setting.
            {u"view.vim"_s,
             app->settings()->vimMode() ? CommandModel::tr("Turn off Vim keys") : CommandModel::tr("Vim keys"),
             CommandModel::tr("Normal, insert and visual mode in the blocks and the Markdown source"),
             u"keyboard"_s,
             {},
             true},
            // The interface's size (docs/UI.md "Look & feel"; desktop): like a browser's zoom, on
            // Ctrl+Alt (Ctrl+= / Ctrl+- zoom PDFs, drawings and canvases).
            {u"view.zoomIn"_s, CommandModel::tr("Larger interface"), CommandModel::tr("Text, buttons and icons"),
             u"zoom-in"_s, u"Ctrl+Alt+="_s, app->interfaceScaleSupported()},
            {u"view.zoomOut"_s, CommandModel::tr("Smaller interface"), CommandModel::tr("Text, buttons and icons"),
             u"zoom-out"_s, u"Ctrl+Alt+-"_s, app->interfaceScaleSupported()},
            {u"view.zoomReset"_s, CommandModel::tr("Interface size: automatic"),
             CommandModel::tr("As the screen suggests"), u"monitor"_s, u"Ctrl+Alt+0"_s, app->interfaceScaleSupported()},
            {u"sync.now"_s,
             CommandModel::tr("Sync now"),
             CommandModel::tr("Nextcloud"),
             u"refresh-cw"_s,
             {},
             ready && app->syncAvailable()},
            {u"go.sync"_s, CommandModel::tr("Sync center"), {}, u"folder-sync"_s, {}, ready},
            {u"calendars.open"_s,
             CommandModel::tr("Calendars"),
             CommandModel::tr("Subscriptions and CalDAV accounts"),
             u"calendar-range"_s,
             {},
             ready},
            {u"calendars.refresh"_s,
             CommandModel::tr("Refresh calendars"),
             CommandModel::tr("Subscriptions and CalDAV accounts"),
             u"refresh-cw"_s,
             {},
             ready && app->workspace()->calendars()->hasCalendars()},
            {u"calendars.newEvent"_s,
             CommandModel::tr("New event…"),
             CommandModel::tr("In a CalDAV calendar"),
             u"calendar-plus"_s,
             {},
             ready && !app->workspace()->calendars()->writableCalendars().isEmpty()},
            {u"go.importExport"_s, CommandModel::tr("Import and export"), {}, u"import"_s, {}, ready},
            {u"publish.open"_s,
             CommandModel::tr("Publish…"),
             CommandModel::tr("Pages on the web: the publishing targets"),
             u"globe"_s,
             {},
             ready && !app->inDocsVault()},
            {u"go.trash"_s, CommandModel::tr("Trash"), {}, u"trash-2"_s, {}, ready},
            {u"mode.code"_s,
             CommandModel::tr("Code mode"),
             CommandModel::tr("Projects and AI agents' threads, as in T3 Code"),
             u"code"_s,
             {},
             ready && app->settings()->appMode() != u"code"},
            {u"mode.notes"_s,
             CommandModel::tr("Notes mode"),
             CommandModel::tr("The notebook: journal, pages, tasks"),
             u"notebook-pen"_s,
             {},
             ready && app->settings()->appMode() == u"code"},
            {u"code.newThread"_s, CommandModel::tr("New thread"),
             CommandModel::tr("Code mode: an AI agent's new thread in a project"), u"plus"_s, u"Ctrl+Shift+O"_s,
             ready && app->workspace()->projects()->count() > 0, u"code"_s},
            {u"code.addProject"_s,
             CommandModel::tr("Add a project…"),
             CommandModel::tr("Code mode: a folder AI agents work in"),
             u"folder-plus"_s,
             {},
             ready},
            {u"code.cloneRepository"_s,
             CommandModel::tr("Clone a repository…"),
             CommandModel::tr("Code mode: a project from GitHub or any git address"),
             u"download"_s,
             {},
             ready && app->workspace()->projects()->gitAvailable()},
            // A project's files (docs/CODE-FILES.md): the file picker and the project search of
            // the thread on screen (CodeView's uiActions; in Code mode Print page's key and
            // Search's key run them, AppShell.runShortcut).
            {u"code.findFile"_s,
             CommandModel::tr("Go to file…"),
             CommandModel::tr("Code mode: a file of the project by its name"),
             u"file-text"_s,
             {},
             ready && view == u"code"},
            {u"code.search"_s,
             CommandModel::tr("Search in the project"),
             CommandModel::tr("Code mode: text in the project's files"),
             u"search"_s,
             {},
             ready && view == u"code"},
            {u"go.notifications"_s,
             CommandModel::tr("Notifications"),
             CommandModel::tr("Reminders, AI answers, agents and extensions that told you something"),
             u"bell"_s,
             {},
             ready},
            {u"go.cleanup"_s,
             CommandModel::tr("Clean up"),
             CommandModel::tr("Notes and attachments without links"),
             u"sparkles"_s,
             {},
             ready},
            {u"theme.toggle"_s, CommandModel::tr("Toggle dark mode"), {}, u"moon"_s, {}, true},
            // The other language's name in that language, so it can be found either way.
            {u"language.switch"_s,
             CommandModel::tr("Change language"),
             english ? u"Polski"_s : u"English"_s,
             u"keyboard"_s,
             {},
             true},
            {u"go.settings"_s, CommandModel::tr("Settings"), {}, u"settings"_s, u"Ctrl+,"_s, ready},
            {u"settings.shortcuts"_s,
             CommandModel::tr("Keyboard shortcuts"),
             CommandModel::tr("Change them in Settings"),
             u"keyboard"_s,
             {},
             ready},
            {u"vault.docs"_s,
             app->inDocsVault() ? CommandModel::tr("Back to my notebook") : CommandModel::tr("Brulion Documentation"),
             CommandModel::tr("Switch notebook"), u"circle-help"_s, u"Ctrl+Shift+B"_s, !app->docsVaultPath().isEmpty()},
            {u"vault.switch"_s, CommandModel::tr("Open another notebook…"), {}, u"folder-open"_s, {}, ready},
            {u"about"_s, CommandModel::tr("About Brulion"), {}, u"info"_s, {}, true},
    };
}

/// The automatic backups' commands (docs/UI.md "Backups"): their settings' section and a backup now.
static QList<Command> backupCommands(const AppController *app)
{
    const bool usable = app->status() == u"ready" && app->workspace()->backups()->available();
    return {
            {u"backup.open"_s,
             CommandModel::tr("Backups"),
             CommandModel::tr("Copies of this notebook; restore one"),
             u"hard-drive"_s,
             {},
             usable},
            {u"backup.now"_s, CommandModel::tr("Create a backup now"), {}, u"hard-drive"_s, {}, usable},
    };
}

// ---- Folders (Move to…, New folder…, Show in tree) -----------------------------------------

/// The folder commands, after "New page": a new folder at the root, moving the page shown, showing
/// it in the sidebar's tree (the shell opens the dialogs: AppController::uiActionRequested).
static QList<Command> folderCommands(const AppController *app)
{
    const bool ready = app->status() == u"ready";
    const QVariantMap route = app->currentRoute();
    const QString view = route.value(u"view"_s).toString();
    // Shown in the tree, also from a journal route, which may carry only its date. A daily note
    // opened as a page moves like any page (a misfiled day can be put right); the journal view
    // by date offers no move.
    const Workspace *ws = app->workspace();
    const QString path = route.value(u"path"_s).toString();
    const bool onPage = ready && view == u"page";
    const bool onFile =
            onPage
            || (ready && view == u"journal"
                && ws->exists(!path.isEmpty() ? path : ws->journalFileFor(route.value(u"date"_s).toString())));
    return {
            {u"folder.new"_s,
             CommandModel::tr("New folder…"),
             CommandModel::tr("At the notebook root"),
             u"folder-plus"_s,
             {},
             ready},
            {u"page.move"_s,
             CommandModel::tr("Move to…"),
             CommandModel::tr("Move the page to another folder"),
             u"folder-input"_s,
             {},
             onPage},
            {u"page.reveal"_s,
             CommandModel::tr("Show in tree"),
             CommandModel::tr("The page in the sidebar's folders"),
             u"folder-tree"_s,
             {},
             onFile},
            {u"page.trash"_s,
             CommandModel::tr("Move to trash"),
             CommandModel::tr("The open page or file; you can restore it from the trash"),
             u"trash-2"_s,
             {},
             onFile},
    };
}

// ---- Tabs, the split view, windows (docs/UI.md "Tabs, split view and windows") ---------------

/// The tab commands, after "Forward": a new tab, a copy of this one, closing it, the next and the
/// previous one; the split view (wide windows) and the page in a window of its own (desktop).
static QList<Command> tabCommands(const AppController *app)
{
    const bool ready = app->status() == u"ready";
    const bool several = ready && app->tabCount() > 1;
    const bool wide = ready && app->sidePanelAllowed();
    return {
            {u"tab.new"_s, CommandModel::tr("New tab"), {}, u"plus"_s, u"Ctrl+T"_s, ready},
            {u"tab.duplicate"_s,
             CommandModel::tr("Open in new tab"),
             CommandModel::tr("The view shown, in a tab of its own"),
             u"copy"_s,
             {},
             ready},
            {u"tab.close"_s, CommandModel::tr("Close tab"), {}, u"x"_s, u"Ctrl+W"_s, ready},
            {u"tab.next"_s, CommandModel::tr("Next tab"), {}, u"chevron-right"_s, u"Ctrl+Tab"_s, several},
            {u"tab.previous"_s, CommandModel::tr("Previous tab"), {}, u"chevron-left"_s, u"Ctrl+Shift+Tab"_s, several},
            {u"view.split"_s,
             CommandModel::tr("Split view"),
             CommandModel::tr("A second pane beside this one"),
             u"columns-2"_s,
             {},
             wide && app->splitTab() == 0},
            {u"view.closeSplit"_s,
             CommandModel::tr("Close split view"),
             {},
             u"square"_s,
             {},
             wide && app->splitTab() != 0},
            {u"window.open"_s,
             CommandModel::tr("Open in new window"),
             CommandModel::tr("The view shown, in a window of its own"),
             u"app-window"_s,
             {},
             ready && app->windowsAllowed()},
    };
}

/// Run a tab command; false when `id` is none.
static bool runTabCommand(AppController *app, const QString &id)
{
    if (id == u"tab.new")
        app->newTab();
    else if (id == u"tab.duplicate")
        app->openTab(app->currentRoute());
    else if (id == u"tab.close")
        app->closeTab(app->focusedTab());
    else if (id == u"tab.next")
        app->nextTab();
    else if (id == u"tab.previous")
        app->previousTab();
    else if (id == u"view.split")
        app->splitView();
    else if (id == u"view.closeSplit")
        app->closeSplit();
    else if (id == u"window.open")
        app->openWindow(app->currentRoute());
    else
        return false;
    return true;
}

/// The commands for the note on screen, after "Markdown source": available where that is (`editor`:
/// an editor shows a page or a day) except on a picture attachment, which is no note; those that need
/// blocks or the properties panel not in source mode (no blocks to point at, no id can be added, the
/// panel is hidden there).
static QList<Command> editorCommands(const AppController *app, bool editor)
{
    const QVariantMap route = app->currentRoute();
    const bool picture = route.value(u"view"_s).toString() == u"page"
                         && app->workspace()->isPicture(route.value(u"path"_s).toString());
    const bool inEditor = editor && !picture;
    const bool blocks = inEditor && !app->sourceModeShown();
    const VaultIndex *index = app->workspace()->taskIndex();
    const QList<notebook::TaskPlace> running =
            index ? notebook::runningClockPlaces(*index) : QList<notebook::TaskPlace>();
    QList<Command> out{
            // The note mode: document (Obsidian-like) / outline (Logseq-like), written into the note
            // (docs/UI.md "Note modes").
            {u"page.mode"_s,
             CommandModel::tr("Toggle note mode"),
             CommandModel::tr("Document (like Obsidian) or outline (like Logseq)"),
             u"file-text"_s,
             {},
             inEditor},
            // A page property (the properties panel's add row, docs/UI.md "Page properties").
            {u"page.addProperty"_s, CommandModel::tr("Add page property"),
             CommandModel::tr("Status, date, tags, aliases… in the page's properties"), u"plus"_s, u"Ctrl+;"_s, blocks},
            // Copying a link (docs/SYNTAX.md §2 "Copied links") to the page, the current block (the one
            // being edited, else the selected one) and the heading of its section: the editor of the
            // page or day on screen copies it (uiAction "copyLink:<kind>", BlockEditor.copyCurrentLink());
            // Ctrl+Shift+C copies the block reference there.
            {u"page.copyLink"_s, CommandModel::tr("Copy link to page"), u"[[…]]"_s, u"link"_s, {}, inEditor},
            // The note on screen — in the journal stream the day being written in — as QR codes
            // another phone's camera reads as text (docs/UI.md "QR code"; uiAction "qrCode": the
            // editor's QrCodeDialog). Source mode too: its text as it is being edited.
            {u"page.qrCode"_s,
             CommandModel::tr("Share as QR code…"),
             CommandModel::tr("The note's text for another phone's camera, no internet needed"),
             u"qr-code"_s,
             {},
             inEditor},
            {u"block.copyRef"_s, CommandModel::tr("Copy block reference"), u"((…))"_s, u"link"_s, u"Ctrl+Shift+C"_s,
             blocks},
            {u"block.copyLink"_s, CommandModel::tr("Copy link to block"), u"[[…#^…]]"_s, u"link"_s, {}, blocks},
            {u"heading.copyLink"_s, CommandModel::tr("Copy link to heading"), u"[[…#…]]"_s, u"heading"_s, {}, blocks},
            // Tasks (docs/SYNTAX.md §5): the timer and a dependency of the task at the cursor (uiActions
            // "taskClock" / "taskDependency": BlockEditor.toggleCurrentTaskClock() /
            // openCurrentDependencyPicker()), and stopping the timer that runs, wherever it is.
            {u"task.clock"_s,
             CommandModel::tr("Start or stop task timer"),
             CommandModel::tr("The task at the cursor; the time goes to its LOGBOOK"),
             u"clock"_s,
             {},
             blocks},
            {u"task.addDependency"_s,
             CommandModel::tr("Add task dependency…"),
             CommandModel::tr("The task at the cursor waits for another one (⛔)"),
             u"link"_s,
             {},
             blocks},
            {u"task.stopClocks"_s,
             CommandModel::tr("Stop task timer"),
             running.isEmpty() ? CommandModel::tr("No timer runs") : running.first().task.text,
             u"square"_s,
             {},
             !running.isEmpty()},
            // Pictures from the internet on the page into the notebook (uiAction "saveWebPictures": the
            // editor on screen, BlockEditor.downloadWebPictures("")).
            {u"page.saveWebPictures"_s,
             CommandModel::tr("Save pictures from the internet to notebook"),
             CommandModel::tr("Copies in the attachments folder instead of web addresses"),
             u"download"_s,
             {},
             blocks},
    };
    // The AI in the editor (docs/UI.md "AI in the editor") while the assistant is on: the selection,
    // or the block or paragraph at the caret, rewritten, continued or summed up (uiAction
    // "writing:<action>": BlockEditor.startWriting()); outline and source mode alike.
    if (app->settings()->assistantEnabled()) {
        out.append({u"ai.rewrite"_s, CommandModel::tr("AI: Rewrite…"),
                    CommandModel::tr("The selection or the block: shorter, clearer, corrected, translated…"),
                    u"sparkles"_s, u"Ctrl+Shift+A"_s, inEditor});
        out.append({u"ai.continue"_s,
                    CommandModel::tr("AI: Continue writing"),
                    CommandModel::tr("The next sentences after the caret"),
                    u"pencil"_s,
                    {},
                    inEditor});
        out.append({u"ai.summarize"_s,
                    CommandModel::tr("AI: Summarize"),
                    CommandModel::tr("A short summary under the selection or the block"),
                    u"scroll-text"_s,
                    {},
                    inEditor});
    }
    return out;
}

/// All commands in the palette's order: buildCommands() with the note's commands (editorCommands())
/// after "Markdown source", the event log after the sync center, the folder commands after "New
/// page", the tab commands after "Forward" and the backup commands at the end.
/// The extensions' commands (docs/EXTENSIONS.md): "Gym" (go.gym) while the gym is on (go.habits
/// before them only while habits are, allCommands()), every
/// command of a loaded extension as "ext.<extension>.<name>" with the extension's own label, every
/// view with a label as "extview.<extension>.<name>" (the route "extension"), then the
/// extensions' own tools: the Luau console, "Run Luau block", "New extension", "Reload
/// extensions", Settings → Extensions. A command on the block under the caret (meta.block, "Run
/// Luau block") only in an editor, run there (uiAction "extension:<extension>/<name>",
/// BlockEditor.runExtensionCommand()).
static QList<Command> extensionCommands(const AppController *app, bool blocks)
{
    QList<Command> out;
    const WorkspaceExtensions *extensions = app->workspace()->extensions();
    const bool ready = app->status() == u"ready";
    if (extensions->gymEnabled())
        out.append({u"go.gym"_s, CommandModel::tr("Gym"), CommandModel::tr("Today's workout, plans, progress"), u"dumbbell"_s,
                    {}, ready});
    // Flashcards (the built-in `fiszki`, docs/UI.md "Flashcards") while they are on: the view with
    // what is to study now, the cards of the page on screen.
    if (extensions->flashcardsEnabled()) {
        const int due = extensions->flashcardsDue();
        out.append({u"go.flashcards"_s, CommandModel::tr("Flashcards"),
                    due > 0 ? CommandModel::tr("%n card(s) to study now", nullptr, due)
                            : CommandModel::tr("Decks, a review session, statistics"),
                    u"layers"_s, {}, ready});
        const QVariantMap route = app->currentRoute();
        out.append({u"flashcards.reviewPage"_s, CommandModel::tr("Review this page's cards"),
                    CommandModel::tr("Flashcards: a session of the cards on this page"), u"layers"_s, {},
                    ready && route.value(u"view"_s).toString() == u"page" && !route.value(u"path"_s).toString().isEmpty()});
    }
    const auto iconOf = [](const QString &extension) {
        return extension == u"silownia" ? u"dumbbell"_s
               : extension == u"nawyki" ? u"flame"_s
               : extension == u"fiszki" ? u"layers"_s
                                        : u"puzzle"_s;
    };
    for (const extensions::CommandInfo &c : extensions->commands()) {
        if (c.palette) // (else only the app or a view's button runs it: the assistant's chat turn)
            out.append({u"ext."_s + c.extension + u'.' + c.name, c.label, extensions->title(c.extension),
                        iconOf(c.extension), c.key, ready && (!c.needsBlock || blocks)});
    }
    for (const extensions::ViewInfo &v : extensions->views()) {
        if (!v.label.isEmpty() && v.extension != u"silownia" && v.extension != u"nawyki"
            && v.extension != u"fiszki") // (their own views)
            out.append({u"extview."_s + v.extension + u'.' + v.name, v.label, extensions->title(v.extension),
                        v.icon.isEmpty() ? iconOf(v.extension) : v.icon, {}, ready});
    }
    // The AI chat (docs/UI.md "AI chat") while the assistant is on: its conversations, a new one
    // about the page on screen.
    if (app->settings()->assistantEnabled()) {
        out.append({u"ai.newChat"_s, CommandModel::tr("AI: new conversation"),
                    CommandModel::tr("Ask about your notes, or ask to do something in them"), u"sparkles"_s, {}, ready});
        out.append({u"go.chat"_s, CommandModel::tr("AI chat"), CommandModel::tr("Conversations with the AI assistant"),
                    u"message-square"_s, {}, ready});
    }
    // While the AI chat shows: a prompt link of the draft (brulion://chat, docs/UI.md "Automation").
    if (app->currentRoute().value(u"view"_s).toString() == u"chat")
        out.append({u"ai.copyPromptLink"_s, CommandModel::tr("AI: Copy prompt link"),
                    CommandModel::tr("A link that opens a new conversation with this prompt, never sending it"),
                    u"link"_s, {}, ready});
    out.append({u"go.console"_s, CommandModel::tr("Luau console"),
                CommandModel::tr("Evaluate Luau with the notebook's API; what extensions print"), u"terminal"_s, {},
                ready});
    out.append({u"extensions.runBlock"_s, CommandModel::tr("Run Luau block"),
                CommandModel::tr("The ```luau block under the cursor, in the console"), u"play"_s, {}, ready && blocks});
    out.append({u"extensions.new"_s, CommandModel::tr("New extension"),
                CommandModel::tr("A page of Luau code that adds commands and views"), u"puzzle"_s, {}, ready});
    out.append({u"extensions.reload"_s, CommandModel::tr("Reload extensions"),
                CommandModel::tr("Start the extensions again from their pages"), u"refresh-cw"_s, {}, ready});
    out.append({u"extensions.settings"_s, CommandModel::tr("Extensions"),
                CommandModel::tr("Settings: built-in extensions and the notebook's own"), u"puzzle"_s, {}, true});
    return out;
}

/// Encrypted notes (docs/UI.md "Encrypted notes") and the app lock (docs/UI.md "App lock"): the
/// page on screen's (`editor`) — a selection or the caret's new fragment, the Markdown page
/// encrypted, an encrypted page under a new password or decrypted (uiActions: PageView) —, then
/// locking what is unlocked and the app itself.
static QList<Command> encryptionCommands(const AppController *app, bool editor)
{
    const QVariantMap route = app->currentRoute();
    const QString path = route.value(u"view"_s).toString() == u"page" ? route.value(u"path"_s).toString() : QString();
    const WorkspaceEncryption *encryption = app->workspace()->encryption();
    const bool encryptedPage = encryption->isEncryptedPath(path);
    const bool unlockedPage = encryptedPage && encryption->isPageUnlocked(path);
    const bool blocks = editor && !app->sourceModeShown();
    return {
            {u"encrypt.selection"_s,
             CommandModel::tr("Encrypt selection…"),
             CommandModel::tr("The selected text under a password, readable in Obsidian with Meld Encrypt"),
             u"lock"_s,
             {},
             blocks},
            {u"encrypt.page"_s,
             CommandModel::tr("Encrypt page…"),
             CommandModel::tr("The whole page under a password (a .mdenc file, as Meld Encrypt writes it)"),
             u"lock"_s,
             {},
             editor && paths::isMarkdown(path) && app->workspace()->journalDate(path).isEmpty()},
            {u"encrypt.password"_s,
             CommandModel::tr("Change page password…"),
             CommandModel::tr("The encrypted page under a new password"),
             u"key-round"_s,
             {},
             unlockedPage},
            {u"encrypt.decrypt"_s,
             CommandModel::tr("Decrypt page permanently…"),
             CommandModel::tr("The encrypted page becomes an ordinary Markdown page"),
             u"lock-open"_s,
             {},
             unlockedPage},
            {u"encrypt.lockAll"_s,
             CommandModel::tr("Lock encrypted notes"),
             CommandModel::tr("Forget the passwords typed, lock the open encrypted pages"),
             u"lock"_s,
             {},
             encryption->unlocked()},
    };
}

/// The project Code mode shows ("" none): a new thread's (the route's), a thread's, else the last one
/// used.
static QString codeProject(const AppController *app)
{
    const QVariantMap route = app->currentRoute();
    if (route.value(u"view"_s).toString() != u"code")
        return {};
    const CodeProjects *projects = app->workspace()->projects();
    const QString path = route.value(u"path"_s).toString();
    QString id = path.isEmpty() ? route.value(u"project"_s).toString()
                                : app->workspace()->chats()->threadPlace(path).value(u"project"_s).toString();
    if (id.isEmpty() && path.isEmpty())
        id = app->settings()->value(u"code/lastProject"_s).toString();
    if (!projects->contains(id) && path.isEmpty() && projects->count() > 0)
        id = projects->list().first().toMap().value(u"id"_s).toString();
    return projects->contains(id) ? id : QString();
}

/// Code mode's commands (docs/CODE.md "Shortcuts"): the thread's, the header's, and the shown
/// project's scripts ("Run: Testy — kalkulator", id `code.script.<project>.<script>`) with the
/// shortcuts they were given. Their shortcuts work in Code mode only (ShortcutMap's scope "code").
/// Those that need the view on screen come back to it as uiActions "code:<what>" (CodeView,
/// CodeSidebar).
static QList<Command> codeCommands(const AppController *app)
{
    const bool ready = app->status() == u"ready";
    const bool inCode = ready && app->currentRoute().value(u"view"_s).toString() == u"code"
                        && app->workspace()->projects()->count() > 0;
    const bool thread = inCode && !app->currentRoute().value(u"path"_s).toString().isEmpty();
    QList<Command> out{
            {u"code.newLocalThread"_s, CommandModel::tr("New local thread"),
             CommandModel::tr("Code mode: a new thread in the project's own folder"), u"hard-drive"_s,
             u"Ctrl+Shift+N"_s, ready && app->workspace()->projects()->count() > 0, u"code"_s},
            {u"code.previousThread"_s, CommandModel::tr("Previous thread"), CommandModel::tr("Code mode"),
             u"arrow-up"_s, u"Ctrl+Shift+["_s, inCode, u"code"_s},
            {u"code.nextThread"_s, CommandModel::tr("Next thread"), CommandModel::tr("Code mode"), u"arrow-down"_s,
             u"Ctrl+Shift+]"_s, inCode, u"code"_s},
            {u"code.renameThread"_s, CommandModel::tr("Rename thread"), CommandModel::tr("Code mode"), u"pencil"_s,
             u"F2"_s, thread, u"code"_s},
            {u"code.branches"_s, CommandModel::tr("Branches…"),
             CommandModel::tr("Code mode: switch or create a branch, check out a pull request"), u"git-branch"_s,
             u"Ctrl+Shift+G"_s, inCode, u"code"_s},
            {u"code.environment"_s, CommandModel::tr("Where the new thread works…"),
             CommandModel::tr("Code mode: local, a new worktree or this worktree"), u"folder-tree"_s,
             u"Ctrl+Shift+X"_s, inCode && !thread, u"code"_s},
            {u"code.openInEditor"_s, CommandModel::tr("Open in editor"),
             CommandModel::tr("Code mode: the thread's folder in your editor"), u"code"_s, u"Ctrl+O"_s, inCode,
             u"code"_s},
            {u"code.toggleChanges"_s, CommandModel::tr("Show or hide the changes"),
             CommandModel::tr("Code mode: the Changes panel"), u"git-compare"_s, u"Ctrl+D"_s, inCode, u"code"_s},
            // The thread's terminals (docs/CODE-TERMINAL.md "Keys"): T3 Code's mod+j (in Code mode Ctrl+J
            // is the terminal's, elsewhere today's journal), the rest Ctrl+Shift chords the terminal
            // leaves to the app (TerminalView: Ctrl+letters are the program's) — T3 Code's mod+d,
            // mod+shift+d, mod+w with Shift, its Ctrl+Alt+] / [ as they are.
            {u"terminal.toggle"_s, CommandModel::tr("Show or hide the terminal"), CommandModel::tr("Code mode"),
             u"terminal"_s, u"Ctrl+J"_s, inCode, u"code"_s},
            {u"terminal.new"_s, CommandModel::tr("New terminal"), CommandModel::tr("Code mode: a shell in the thread's folder"),
             u"plus"_s, u"Ctrl+Shift+T"_s, inCode, u"code"_s},
            {u"terminal.splitRight"_s, CommandModel::tr("Split terminal right"), CommandModel::tr("Code mode"),
             u"columns-2"_s, u"Ctrl+Shift+D"_s, inCode, u"code"_s},
            {u"terminal.splitDown"_s, CommandModel::tr("Split terminal down"), CommandModel::tr("Code mode"),
             u"rows-2"_s, u"Ctrl+Alt+Shift+D"_s, inCode, u"code"_s},
            {u"terminal.close"_s, CommandModel::tr("Close terminal"), CommandModel::tr("Code mode"), u"x"_s,
             u"Ctrl+Shift+W"_s, inCode, u"code"_s},
            {u"terminal.next"_s, CommandModel::tr("Next terminal"), CommandModel::tr("Code mode"), u"chevron-right"_s,
             u"Ctrl+Alt+]"_s, inCode, u"code"_s},
            {u"terminal.previous"_s, CommandModel::tr("Previous terminal"), CommandModel::tr("Code mode"),
             u"chevron-left"_s, u"Ctrl+Alt+["_s, inCode, u"code"_s},
    };
    if (const QString project = inCode ? codeProject(app) : QString(); !project.isEmpty()) {
        const CodeProjects *projects = app->workspace()->projects();
        const QString name = projects->nameOf(project);
        for (const QVariant &v : projects->scripts(project)) {
            const QVariantMap script = v.toMap();
            out.append({u"code.script."_s + project + u'.' + script.value(u"id"_s).toString(),
                        CommandModel::tr("Run: %1 — %2").arg(script.value(u"name"_s).toString(), name),
                        script.value(u"command"_s).toString().simplified().left(120),
                        script.value(u"iconName"_s).toString(), script.value(u"shortcut"_s).toString(), true,
                        u"code"_s});
        }
    }
    return out;
}

static QList<Command> allCommands(const AppController *app)
{
    QList<Command> out = buildCommands(app);
    insertAfter(out, u"code.newThread"_s, codeCommands(app));
    const auto source = std::ranges::find(std::as_const(out), u"page.source"_s, &Command::id);
    const bool editor = source != out.cend() && source->enabled;
    insertAfter(out, u"page.source"_s, editorCommands(app, editor));
    insertAfter(out, u"go.habits"_s, extensionCommands(app, editor && !app->sourceModeShown()));
    // Habits are the built-in extension `nawyki`: offered while it is on (as the gym).
    if (!app->workspace()->extensions()->habitsEnabled())
        out.removeIf([](const Command &c) { return c.id == u"go.habits"; });
    // The event log ("Event log"); also found as logs, diagnostics.
    insertAfter(out, u"go.sync"_s,
                {{u"go.logs"_s,
                  CommandModel::tr("Event log"),
                  CommandModel::tr("Logs, diagnostics, sync problems"),
                  u"scroll-text"_s,
                  {},
                  app->status() == u"ready"}});
    insertAfter(out, u"page.new"_s, folderCommands(app));
    // A whiteboard (docs/UI.md "Canvas view"): "Untitled canvas.canvas" in the new pages' folder.
    insertAfter(out, u"page.new"_s,
                {{u"canvas.new"_s,
                  CommandModel::tr("New canvas"),
                  CommandModel::tr("A whiteboard of cards, notes, files and connections (JSON Canvas)"),
                  u"layout-dashboard"_s,
                  {},
                  app->status() == u"ready"}});
    insertAfter(out, u"page.qrCode"_s, encryptionCommands(app, editor));
    // After the page and folder commands ("New page", "New canvas", folders, move, show, trash).
    insertAfter(out, u"page.trash"_s,
                {{u"encrypt.newPage"_s,
                  CommandModel::tr("New encrypted page…"),
                  CommandModel::tr("A page readable only with its password (Meld Encrypt's format)"),
                  u"lock"_s,
                  {},
                  app->status() == u"ready"},
                 {u"app.lock"_s,
                  CommandModel::tr("Lock Brulion"),
                  CommandModel::tr("The lock screen until the PIN or the phone's lock"),
                  u"lock-keyhole"_s,
                  {},
                  app->appLock()->enabled() && !app->appLock()->locked()}});
    insertAfter(out, u"nav.forward"_s, tabCommands(app));
    out += backupCommands(app);
    return out;
}

QVariantList CommandModel::commands() const
{
    const QList<Command> all = allCommands(m_app);
    const ShortcutMap *shortcuts = m_app->shortcuts();
    QVariantList out;
    out.reserve(all.size());
    for (const Command &c : all)
        out.append(toMap(c, shortcuts));
    return out;
}

QList<ShortcutMap::Action> CommandModel::shortcutActions() const
{
    // The palette itself first; titles that follow the state ("Add to favorites" / "Remove…")
    // by what the command does.
    QList<ShortcutMap::Action> out{{u"palette"_s, tr("Command palette"), u"Ctrl+K"_s}};
    const QList<Command> commands = allCommands(m_app);
    // What an extension suggests is its default only while the app has no use for it: no command
    // of the app's own has it by default, no key that types text or that the editor takes, not
    // one an earlier extension's command took.
    QSet<QString> taken{u"Ctrl+K"_s};
    for (const Command &c : commands)
        if (!c.id.startsWith(u"ext.") && !c.shortcut.isEmpty() && c.scope.isEmpty())
            taken.insert(ShortcutMap::normalize(c.shortcut));
    for (const Command &c : commands) {
        QString title = c.title;
        if (c.id == u"page.favorite")
            title = tr("Add to or remove from favorites");
        else if (c.id == u"vault.docs")
            title = tr("Brulion Documentation or back to my notebook");
        else if (c.id == u"view.focus")
            title = tr("Focus mode");
        else if (c.id == u"view.typewriter")
            title = tr("Typewriter scrolling");
        else if (c.id == u"view.vim")
            title = tr("Vim keys");
        QString shortcut = c.shortcut;
        if (c.scope == u"code" && !c.id.startsWith(u"code.script."))
            title = tr("Code mode: %1").arg(title);
        if (c.id.startsWith(u"code.script.")) {
            // (The script's own shortcut: its default; one Code mode may not use, none.)
            const QString n = ShortcutMap::normalize(shortcut);
            shortcut = n.isEmpty() || ShortcutMap::typesText(n) || !ShortcutMap::reservedIn(n, c.scope).isEmpty()
                               ? QString()
                               : n;
        }
        if (c.id.startsWith(u"ext.")) {
            title = c.subtitle + u": "_s + c.title; // ("Licznik słów: Policz słowa")
            const QString n = ShortcutMap::normalize(shortcut);
            shortcut = n.isEmpty() || ShortcutMap::typesText(n) || !ShortcutMap::reservedUse(n).isEmpty()
                                       || taken.contains(n)
                               ? QString()
                               : n;
            if (!shortcut.isEmpty())
                taken.insert(shortcut);
        }
        out.append({c.id, title, shortcut, c.scope});
    }
    return out;
}

QStringList CommandModel::ids() const
{
    QStringList out;
    for (const QList<Command> all = allCommands(m_app); const Command &c : all)
        out.append(c.id);
    return out;
}

bool CommandModel::run(const QString &id)
{
    const QList<Command> all = allCommands(m_app);
    const auto it = std::ranges::find(all, id, &Command::id);
    if (it == all.cend() || !it->enabled)
        return false;

    if (const QString action = uiActionOf(id); !action.isEmpty())
        m_app->requestUiAction(action);
    else if (id == u"journal.today")
        m_app->openToday();
    else if (id == u"journal.yesterday")
        m_app->openJournal(m_app->today(-1));
    else if (id == u"journal.tomorrow")
        m_app->openJournal(m_app->today(1));
    else if (id == u"journal.week" || id == u"journal.lastWeek" || id == u"journal.nextWeek")
        m_app->openPeriodicNote(u"week"_s, QString(), periodOffset(id));
    else if (id == u"journal.month" || id == u"journal.lastMonth" || id == u"journal.nextMonth")
        m_app->openPeriodicNote(u"month"_s, QString(), periodOffset(id));
    else if (id == u"capture")
        m_app->quickCapture();
    else if (id == u"encrypt.lockAll")
        m_app->workspace()->encryption()->lockAll();
    else if (id == u"app.lock")
        m_app->appLock()->lockNow();
    else if (id == u"canvas.new") {
        const QString path = m_app->workspace()->createCanvas(QString());
        if (path.isEmpty())
            return false;
        m_app->openPage(path);
    } else if (id == u"extensions.runBlock")
        m_app->requestUiAction(u"extension:*/run"_s);
    else if (id == u"extensions.new") {
        const QString path = m_app->workspace()->extensions()->createExtension();
        if (path.isEmpty())
            return false;
        m_app->openPage(path);
    } else if (id == u"mode.code" || id == u"mode.notes") {
        m_app->settings()->setAppMode(id.mid(5));
    } else if (id == u"code.newThread" || id == u"code.newLocalThread") {
        // (The project shown — a thread's too —, else the last one used.)
        QString project = codeProject(m_app);
        if (project.isEmpty())
            project = m_app->currentRoute().value(u"project"_s).toString();
        m_app->settings()->setAppMode(u"code"_s);
        QVariantMap route{{u"view"_s, u"code"_s}, {u"new"_s, true}, {u"project"_s, project}};
        if (id == u"code.newLocalThread")
            route.insert(u"env"_s, u"local"_s);
        m_app->navigate(route);
    } else if (id == u"code.addProject" || id == u"code.cloneRepository") {
        // (Before the "code." actions below, which would take them.)
        m_app->settings()->setAppMode(u"code"_s);
        m_app->navigate({{u"view"_s, u"code"_s}, {id.mid(5), true}});
    } else if (id.startsWith(u"code.script.")) {
        m_app->requestUiAction(u"code:script:"_s + id.section(u'.', 3));
    } else if (id.startsWith(u"code.")) {
        m_app->requestUiAction(u"code:"_s + id.mid(5));
    } else if (id == u"code.findFile") {
        m_app->requestUiAction(u"code:findFile"_s);
    } else if (id == u"code.search") {
        m_app->requestUiAction(u"code:search"_s);
    } else if (id == u"ai.newChat") {
        const QString path = m_app->currentRoute().value(u"path"_s).toString();
        m_app->navigate({{u"view"_s, u"chat"_s},
                         {u"new"_s, true},
                         {u"page"_s, path.isEmpty() ? QString() : m_app->workspace()->pageTitle(path)}});
    } else if (id == u"ai.copyPromptLink") {
        m_app->requestUiAction(u"chat:copyPromptLink"_s);
    } else if (id == u"flashcards.reviewPage") {
        const QString path = m_app->currentRoute().value(u"path"_s).toString();
        const QString deck = u"page:"_s + m_app->workspace()->pageTitle(path);
        m_app->navigate({{u"view"_s, u"flashcards"_s}, {u"tab"_s, u"review"_s}, {u"deck"_s, deck}});
    } else if (id == u"extensions.reload")
        m_app->workspace()->extensions()->reloadAll();
    else if (id == u"extensions.settings")
        m_app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"extensions"_s}});
    else if (id.startsWith(u"extview.")) {
        const QString extension = id.section(u'.', 1, 1), name = id.section(u'.', 2);
        // The gym's views are tabs of its own view (GymView: its header, the rest timer).
        if (extension == u"silownia") {
            m_app->navigate({{u"view"_s, u"gym"_s}, {u"tab"_s, name}});
            return true;
        }
        m_app->navigate({{u"view"_s, u"extension"_s},
                         {u"extension"_s, extension},
                         {u"name"_s, name},
                         {u"title"_s, m_app->workspace()->extensions()->viewTitle(extension, name)}});
    } else if (id.startsWith(u"ext.")) {
        const QString extension = id.section(u'.', 1, 1), name = id.section(u'.', 2);
        const auto commands = m_app->workspace()->extensions()->commands();
        const auto c = std::ranges::find_if(commands, [&](const extensions::CommandInfo &info) {
            return info.extension == extension && info.name == name;
        });
        if (c == commands.cend())
            return false;
        if (c->needsBlock) {
            m_app->requestUiAction(u"extension:"_s + extension + u'/' + name);
        } else {
            // ctx.page: the page on screen (a command about "this page"); ctx.selection: what the
            // editor on screen has selected — it offers it now, before the command runs.
            WorkspaceExtensions *ext = m_app->workspace()->extensions();
            ext->clearSelection();
            m_app->requestUiAction(u"offerSelection"_s);
            ext->runCommand(extension, name, m_app->currentRoute().value(u"path"_s).toString());
        }
    } else if (id.startsWith(u"go."))
        m_app->navigate({{u"view"_s, id.mid(3)}});
    else if (id == u"page.favorite")
        m_app->workspace()->toggleFavorite(m_app->currentRoute().value(u"path"_s).toString());
    else if (id == u"page.publish")
        m_app->navigate({{u"view"_s, u"importExport"_s},
                         {u"tab"_s, u"publish"_s},
                         {u"add"_s, u"page"_s},
                         {u"path"_s, m_app->currentRoute().value(u"path"_s)}});
    else if (id == u"publish.open")
        m_app->navigate({{u"view"_s, u"importExport"_s}, {u"tab"_s, u"publish"_s}});
    else if (id == u"page.history")
        m_app->navigate({{u"view"_s, u"history"_s}, {u"path"_s, m_app->currentRoute().value(u"path"_s)}});
    else if (id == u"view.focus")
        m_app->settings()->setFocusMode(!m_app->settings()->focusMode());
    else if (id == u"view.reading")
        m_app->settings()->setReadingMode(!m_app->settings()->readingMode());
    else if (id == u"view.typewriter")
        m_app->settings()->setTypewriterScrolling(!m_app->settings()->typewriterScrolling());
    else if (id == u"view.vim")
        m_app->settings()->setVimMode(!m_app->settings()->vimMode());
    else if (id == u"view.zoomIn")
        m_app->zoomInterface(1);
    else if (id == u"view.zoomOut")
        m_app->zoomInterface(-1);
    else if (id == u"view.zoomReset")
        m_app->zoomInterface(0);
    else if (id == u"settings.shortcuts")
        m_app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"shortcuts"_s}});
    else if (id == u"task.stopClocks")
        m_app->workspace()->stopTaskClocks();
    else if (id == u"nav.back")
        m_app->back();
    else if (id == u"nav.forward")
        m_app->forward();
    else if (id == u"panel.toggle" && !m_app->sidePanelAllowed())
        m_app->requestUiAction(u"toggleContextDrawer"_s); // narrow windows: the drawer from the right
    else if (id == u"panel.toggle")
        m_app->setSidePanelVisible(!m_app->sidePanelVisible());
    else if (id == u"sync.now")
        m_app->requestSync();
    else if (id == u"calendars.open")
        m_app->navigate({{u"view"_s, u"sync"_s}, {u"tab"_s, u"calendars"_s}});
    else if (id == u"calendars.refresh")
        m_app->workspace()->calendars()->refresh({});
    else if (id == u"calendars.newEvent")
        m_app->navigate({{u"view"_s, u"calendar"_s}, {u"open"_s, u"event"_s}});
    else if (id == u"backup.open")
        m_app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"backups"_s}});
    else if (id == u"backup.now")
        m_app->workspace()->backups()->createNow();
    else if (id == u"language.switch")
        m_app->settings()->setLanguage(m_app->settings()->language() == u"en" ? u"pl"_s : u"en"_s);
    else if (id == u"vault.docs")
        m_app->toggleDocsVault();
    else if (id == u"vault.switch")
        m_app->closeVault();
    else if (!runTabCommand(m_app, id))
        return false;
    return true;
}

} // namespace brulion::shell
