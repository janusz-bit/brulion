// The terminals' registry tied to the notebook (terminalregistry.h): compiled into the app, where
// the shell's Workspace is — the library brulion_terminal knows nothing of it.
#include "terminalqml.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "codeprojects.h"
#include "codethreads.h"
#include "workspace.h"
#include "workspacechats.h"

#include <QPointer>

#if defined(BRULION_HAS_ATRAMENT) && defined(BRULION_TERMINAL_ATRAMENT)
#include "atramentview.h"
#include "terminalatrament.h"
#endif

using namespace Qt::StringLiterals;

namespace brulion::terminal {

#if defined(BRULION_HAS_ATRAMENT) && defined(BRULION_TERMINAL_ATRAMENT)
namespace {
// Terminals drawn by Atrament (TerminalView.atrament) use the process's App: AtramentView's, with
// the app's bundled fonts. Set before any QML runs.
[[maybe_unused]] const bool atramentProvided = [] {
    TerminalAtrament::setAppProvider([]() -> atrament::App * { return &AtramentView::atramentApp(); });
    return true;
}();
} // namespace
#endif

static void settleThread(shell::Workspace *workspace, TerminalRegistry *registry, const QString &path)
{
    const QVariantMap place = workspace->chats()->threadPlace(path);
    QStringList stop;
    QList<TerminalRegistry::SettleScript> run;
    for (const QVariant &v : workspace->projects()->scripts(place.value(u"project"_s).toString())) {
        const QVariantMap script = v.toMap();
        if (script.value(u"stopOnSettle"_s).toBool())
            stop << script.value(u"command"_s).toString();
        if (script.value(u"runOnSettle"_s).toBool())
            run << TerminalRegistry::SettleScript{script.value(u"command"_s).toString(), script.value(u"name"_s).toString()};
    }
    const QString worktree = place.value(u"worktree"_s).toString();
    registry->settle(path, stop, place.value(u"exists"_s).toBool() ? worktree : QString(), run);
}

TerminalRegistry *registryForQml()
{
    TerminalRegistry *registry = TerminalRegistry::instance();
    static bool wired = false;
    shell::AppController *app = shell::AppController::instance();
    if (wired || !app || !app->workspace())
        return registry;
    wired = true;
    shell::Workspace *workspace = app->workspace();
    // A thread renamed or moved: its terminals follow; deleted (to the trash, or by another
    // program): they end; archived: those in which no program runs end (T3 Code's settling).
    QObject::connect(workspace, &shell::Workspace::pathRenamed, registry, &TerminalRegistry::pathRenamed);
    QObject::connect(workspace, &shell::Workspace::pathRemoved, registry, &TerminalRegistry::pathRemoved);
    if (shell::WorkspaceChats *chats = workspace->chats())
        QObject::connect(chats, &shell::WorkspaceChats::archivedChanged, registry, &TerminalRegistry::archived);
    // Settled (docs/CODE.md "Settling and snoozing"): its scripts with stopOnSettle end, its idle
    // terminals too, and its scripts with runOnSettle run in its worktree (never in a project's own
    // folder, as T3 Code skips main checkouts).
    QObject::connect(workspace->codeThreads(), &shell::CodeThreads::settledChanged, registry,
                     [workspace, registry](const QString &path, bool on) {
                         if (on)
                             settleThread(workspace, registry, path);
                     });
    // Each thread's layout (tabs, splits, folders, commands) over a restart: this device's.
    const QPointer<shell::AppSettings> settings = app->settings();
    registry->setLayoutStorage(
            [settings] { return settings ? settings->value(u"code/terminalLayouts"_s).toString().toUtf8() : QByteArray(); },
            [settings](const QByteArray &json) {
                if (settings)
                    settings->setValue(u"code/terminalLayouts"_s, QString::fromUtf8(json));
            });
    return registry;
}

} // namespace brulion::terminal
