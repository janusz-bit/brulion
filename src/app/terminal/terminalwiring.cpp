// The terminals' registry tied to the notebook (terminalregistry.h): compiled into the app, where
// the shell's Workspace is — the library brulion_terminal knows nothing of it.
#include "terminalqml.h"

#include "appcontroller.h"
#include "workspace.h"
#include "workspacechats.h"

namespace brulion::terminal {

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
    return registry;
}

} // namespace brulion::terminal
