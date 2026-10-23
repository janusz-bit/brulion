#include "commandmodel.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "workspace.h"

#include <QCoreApplication>
#include <QEvent>

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
};

QVariantMap toMap(const Command &c)
{
    return { { u"id"_s, c.id },           { u"title"_s, c.title },
             { u"subtitle"_s, c.subtitle }, { u"icon"_s, c.icon },
             { u"shortcut"_s, c.shortcut }, { u"enabled"_s, c.enabled } };
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
    connect(app, &AppController::syncStatusChanged, this, &CommandModel::commandsChanged);
    connect(app, &AppController::sidePanelAllowedChanged, this, &CommandModel::commandsChanged);
    connect(app, &AppController::languageChanged, this, &CommandModel::commandsChanged);
    connect(app->workspace(), &Workspace::favoritesChanged, this, &CommandModel::commandsChanged);
}

CommandModel::~CommandModel()
{
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeEventFilter(this);
}

bool CommandModel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange)
        Q_EMIT commandsChanged();
    return QObject::eventFilter(watched, event);
}

static QList<Command> buildCommands(const AppController *app)
{
    const bool ready = app->status() == u"ready";
    const QVariantMap route = app->currentRoute();
    const bool onPage = ready && route.value(u"view"_s).toString() == u"page";
    // Views showing one page in the editor: pages and daily notes.
    const QString view = route.value(u"view"_s).toString();
    const bool inEditor = ready && (onPage || view == u"journal" || view == u"journals");
    const QString pagePath = route.value(u"path"_s).toString();
    const bool favorite = onPage && app->workspace()->isFavorite(pagePath);
    const bool english = app->settings()->language() == u"en";

    return {
        { u"journal.today"_s, CommandModel::tr("Today's journal"), {}, u"notebook-pen"_s, u"Ctrl+J"_s, ready },
        { u"journal.yesterday"_s, CommandModel::tr("Yesterday's journal"), {}, u"calendar-days"_s, {}, ready },
        { u"journal.tomorrow"_s, CommandModel::tr("Tomorrow's journal"), {}, u"calendar-plus"_s, {}, ready },
        { u"page.new"_s, CommandModel::tr("New page"), {}, u"square-pen"_s, u"Ctrl+N"_s, ready },
        { u"page.switch"_s, CommandModel::tr("Go to page…"), {}, u"file-text"_s, u"Ctrl+O"_s, ready },
        { u"capture"_s, CommandModel::tr("Quick capture"), CommandModel::tr("Add a note to today's journal"),
          u"plus"_s, u"Ctrl+Shift+N"_s, ready },
        { u"go.journals"_s, CommandModel::tr("Journals"), {}, u"book-open"_s, {}, ready },
        { u"go.calendar"_s, CommandModel::tr("Calendar"), {}, u"calendar"_s, {}, ready },
        { u"go.tasks"_s, CommandModel::tr("Tasks"), {}, u"list-todo"_s, {}, ready },
        { u"go.allPages"_s, CommandModel::tr("All pages"), {}, u"files"_s, {}, ready },
        { u"go.tags"_s, CommandModel::tr("Tags"), {}, u"tags"_s, {}, ready },
        { u"go.graph"_s, CommandModel::tr("Graph"), {}, u"waypoints"_s, {}, ready },
        { u"go.search"_s, CommandModel::tr("Search"), {}, u"search"_s, u"Ctrl+Shift+F"_s, ready },
        { u"page.favorite"_s, favorite ? CommandModel::tr("Remove from favorites") : CommandModel::tr("Add to favorites"),
          {}, u"star"_s, {}, onPage },
        { u"page.history"_s, CommandModel::tr("Page history"), {}, u"history"_s, {}, onPage },
        // The editor's Ctrl+E — the only way on a phone to leave source mode once it is on.
        { u"page.source"_s, CommandModel::tr("Markdown source"), CommandModel::tr("Switch between blocks and the page's text"),
          u"code"_s, u"Ctrl+E"_s, inEditor },
        { u"nav.back"_s, CommandModel::tr("Back"), {}, u"arrow-left"_s, u"Alt+Left"_s, app->canGoBack() },
        { u"nav.forward"_s, CommandModel::tr("Forward"), {}, u"arrow-right"_s, u"Alt+Right"_s, app->canGoForward() },
        { u"panel.toggle"_s, CommandModel::tr("Toggle side panel"), {}, u"panel-right"_s, u"Ctrl+\\"_s,
          ready && app->sidePanelAllowed() },
        { u"sync.now"_s, CommandModel::tr("Sync now"), CommandModel::tr("Nextcloud"), u"refresh-cw"_s, {},
          ready && app->syncAvailable() },
        { u"go.sync"_s, CommandModel::tr("Sync center"), {}, u"folder-sync"_s, {}, ready },
        { u"go.importExport"_s, CommandModel::tr("Import and export"), {}, u"import"_s, {}, ready },
        { u"go.trash"_s, CommandModel::tr("Trash"), {}, u"trash-2"_s, {}, ready },
        { u"theme.toggle"_s, CommandModel::tr("Toggle dark mode"), {}, u"moon"_s, {}, true },
        // The other language's name in that language, so it can be found either way.
        { u"language.switch"_s, CommandModel::tr("Change language"), english ? u"Polski"_s : u"English"_s,
          u"keyboard"_s, {}, true },
        { u"go.settings"_s, CommandModel::tr("Settings"), {}, u"settings"_s, u"Ctrl+,"_s, ready },
        { u"vault.docs"_s,
          app->inDocsVault() ? CommandModel::tr("Back to my notebook") : CommandModel::tr("Brulion documentation"),
          CommandModel::tr("Switch notebook"), u"book-open"_s, u"Ctrl+Shift+B"_s,
          !app->docsVaultPath().isEmpty() },
        { u"vault.switch"_s, CommandModel::tr("Open another notebook…"), {}, u"folder-open"_s, {}, ready },
        { u"about"_s, CommandModel::tr("About Brulion"), {}, u"info"_s, {}, true },
    };
}

/// The commands that copy a link (docs/SYNTAX.md §2 "Copied links") to the page, the current block
/// (the one being edited, else the selected one) and the heading of its section. The editor of
/// the page or day on screen copies them: uiAction "copyLink:<kind>" (page | blockRef | blockLink
/// | heading, BlockEditor.copyCurrentLink()); Ctrl+Shift+C copies the block reference there.
static QList<Command> linkCommands(bool inEditor)
{
    return {
            {u"page.copyLink"_s, CommandModel::tr("Copy link to page"), u"[[…]]"_s, u"link"_s, {}, inEditor},
            {u"block.copyRef"_s, CommandModel::tr("Copy block reference"), u"((…))"_s, u"link"_s, u"Ctrl+Shift+C"_s,
             inEditor},
            {u"block.copyLink"_s, CommandModel::tr("Copy link to block"), u"[[…#^…]]"_s, u"link"_s, {}, inEditor},
            {u"heading.copyLink"_s, CommandModel::tr("Copy link to heading"), u"[[…#…]]"_s, u"heading"_s, {}, inEditor},
    };
}

/// The uiAction of a link command ("block.copyRef" -> "copyLink:blockRef"), "" for other ids.
static QString linkAction(const QString &id)
{
    if (id == u"page.copyLink")
        return u"copyLink:page"_s;
    if (id == u"block.copyRef")
        return u"copyLink:blockRef"_s;
    if (id == u"block.copyLink")
        return u"copyLink:blockLink"_s;
    if (id == u"heading.copyLink")
        return u"copyLink:heading"_s;
    return {};
}

/// buildCommands() with the note mode command right after "Markdown source", available where that
/// is (an editor shows a page or a day) except on a picture attachment, which is no note: document
/// (Obsidian-like) / outline (Logseq-like), written into the note (docs/UI.md "Note modes"); and
/// the event log after the sync center.
static QList<Command> allCommands(const AppController *app)
{
    QList<Command> out = buildCommands(app);
    const auto source = std::find_if(out.cbegin(), out.cend(), [](const Command &c) { return c.id == u"page.source"; });
    const QVariantMap route = app->currentRoute();
    const bool picture = route.value(u"view"_s).toString() == u"page"
                         && app->workspace()->isPicture(route.value(u"path"_s).toString());
    const bool inEditor = source != out.cend() && source->enabled && !picture;
    out.insert(source == out.cend() ? out.size() : std::distance(out.cbegin(), source) + 1,
               {u"page.mode"_s,
                CommandModel::tr("Toggle note mode"),
                CommandModel::tr("Document (like Obsidian) or outline (like Logseq)"),
                u"file-text"_s,
                {},
                inEditor});
    // Copying links, after the note mode: where it is available.
    const auto mode = std::find_if(out.cbegin(), out.cend(), [](const Command &c) { return c.id == u"page.mode"; });
    qsizetype at = mode == out.cend() ? out.size() : std::distance(out.cbegin(), mode) + 1;
    for (const Command &c : linkCommands(inEditor))
        out.insert(at++, c);
    // The event log ("Rejestr zdarzeń") right after the sync center; also found as logs, diagnostics.
    const auto sync = std::find_if(out.cbegin(), out.cend(), [](const Command &c) { return c.id == u"go.sync"; });
    out.insert(sync == out.cend() ? out.size() : std::distance(out.cbegin(), sync) + 1,
               {u"go.logs"_s,
                CommandModel::tr("Event log"),
                CommandModel::tr("Logs, diagnostics, sync problems"),
                u"scroll-text"_s,
                {},
                app->status() == u"ready"});
    return out;
}

QVariantList CommandModel::commands() const
{
    QVariantList out;
    for (const Command &c : allCommands(m_app))
        out.append(toMap(c));
    return out;
}

QStringList CommandModel::ids() const
{
    QStringList out;
    for (const Command &c : allCommands(m_app))
        out.append(c.id);
    return out;
}

bool CommandModel::run(const QString &id)
{
    const QList<Command> all = allCommands(m_app);
    const auto it = std::find_if(all.cbegin(), all.cend(), [&](const Command &c) { return c.id == id; });
    if (it == all.cend() || !it->enabled)
        return false;

    const auto go = [this](const QString &view) { m_app->navigate({ { u"view"_s, view } }); };
    if (id == u"journal.today")
        m_app->openToday();
    else if (id == u"journal.yesterday")
        m_app->openJournal(m_app->today(-1));
    else if (id == u"journal.tomorrow")
        m_app->openJournal(m_app->today(1));
    else if (id == u"page.new")
        m_app->requestUiAction(u"newPage"_s);
    else if (id == u"page.switch")
        m_app->requestUiAction(u"switcher"_s);
    else if (id == u"capture")
        m_app->quickCapture();
    else if (id.startsWith(u"go."))
        go(id.mid(3));
    else if (id == u"page.favorite")
        m_app->workspace()->toggleFavorite(m_app->currentRoute().value(u"path"_s).toString());
    else if (id == u"page.history")
        m_app->navigate({ { u"view"_s, u"history"_s }, { u"path"_s, m_app->currentRoute().value(u"path"_s) } });
    else if (id == u"page.source")
        m_app->requestUiAction(u"toggleSourceMode"_s);
    else if (id == u"page.mode")
        m_app->requestUiAction(u"toggleNoteMode"_s);
    else if (const QString action = linkAction(id); !action.isEmpty())
        m_app->requestUiAction(action);
    else if (id == u"nav.back")
        m_app->back();
    else if (id == u"nav.forward")
        m_app->forward();
    else if (id == u"panel.toggle")
        m_app->setSidePanelVisible(!m_app->sidePanelVisible());
    else if (id == u"sync.now")
        m_app->requestSync();
    else if (id == u"theme.toggle")
        m_app->requestUiAction(u"toggleTheme"_s);
    else if (id == u"language.switch")
        m_app->settings()->setLanguage(m_app->settings()->language() == u"en" ? u"pl"_s : u"en"_s);
    else if (id == u"vault.docs")
        m_app->toggleDocsVault();
    else if (id == u"vault.switch")
        m_app->closeVault();
    else if (id == u"about")
        m_app->requestUiAction(u"about"_s);
    else
        return false;
    return true;
}

} // namespace brulion::shell
