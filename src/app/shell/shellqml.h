// QML registration of the shell's C++ types (library brulion_shell, src/app/shell/area.cmake)
// in the app's QML module "Brulion". The classes know nothing about QML; these wrappers make
// them QML types (the src/app/ui/qmlforeign.h pattern):
//
//   AppController    singleton — main()'s instance (AppController::instance())
//   AppSettings      singleton — AppController.settings
//   Workspace        singleton — AppController.workspace (the EditorHost for the editor)
//   WorkspaceReminders, WorkspaceSync, WorkspaceBackups   uncreatable — Workspace.reminders /
//                                       Workspace.sync / Workspace.backups
//   ReplaceSession   uncreatable — Workspace.replacing (the notebook-wide replace)
//   CommandModel     singleton — the command palette's commands
//   ShortcutMap      singleton — the commands' keyboard shortcuts (custom ones too)
//   FolderTreeModel  creatable — FolderTreeModel { workspace: Workspace }
//   CleanupSelection creatable — CleanupView's shown and ticked files
#pragma once

#include "appcontroller.h"
#include "appsettings.h"
#include "appupdates.h"
#include "cleanupselection.h"
#include "commandmodel.h"
#include "foldertreemodel.h"
#include "replacesession.h"
#include "shortcutmap.h"
#include "workspace.h"
#include "workspacebackups.h"
#include "workspacereminders.h"
#include "workspacesync.h"

#include <QJSEngine>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;

namespace brulion::shell {

namespace detail {
/// The app's controller; QML tooling without main() (qmlscene-like runs) gets a private one.
inline AppController *controllerForQml()
{
    AppController *app = AppController::instance();
    if (!app) {
        static AppController fallback{ AppController::Options{} };
        app = &fallback;
    }
    QJSEngine::setObjectOwnership(app, QJSEngine::CppOwnership);
    return app;
}

template<typename T>
T *cppOwned(T *object)
{
    QJSEngine::setObjectOwnership(object, QJSEngine::CppOwnership);
    return object;
}
} // namespace detail

struct AppControllerForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::AppController)
    QML_NAMED_ELEMENT(AppController)
    QML_SINGLETON
public:
    static AppController *create(QQmlEngine *, QJSEngine *) { return detail::controllerForQml(); }
};

struct AppSettingsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::AppSettings)
    QML_NAMED_ELEMENT(AppSettings)
    QML_SINGLETON
public:
    static AppSettings *create(QQmlEngine *, QJSEngine *)
    {
        return detail::cppOwned(detail::controllerForQml()->settings());
    }
};

struct WorkspaceForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::Workspace)
    QML_NAMED_ELEMENT(Workspace)
    QML_SINGLETON
public:
    static Workspace *create(QQmlEngine *, QJSEngine *)
    {
        return detail::cppOwned(detail::controllerForQml()->workspace());
    }
};

struct AppUpdatesForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::AppUpdates)
    QML_NAMED_ELEMENT(AppUpdates)
    QML_UNCREATABLE("Use AppController.updates")
};

struct WorkspaceRemindersForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::WorkspaceReminders)
    QML_NAMED_ELEMENT(WorkspaceReminders)
    QML_UNCREATABLE("Use Workspace.reminders")
};

struct WorkspaceSyncForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::WorkspaceSync)
    QML_NAMED_ELEMENT(WorkspaceSync)
    QML_UNCREATABLE("Use Workspace.sync")
};

struct WorkspaceBackupsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::WorkspaceBackups)
    QML_NAMED_ELEMENT(WorkspaceBackups)
    QML_UNCREATABLE("Use Workspace.backups")
};

struct ReplaceSessionForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ReplaceSession)
    QML_NAMED_ELEMENT(ReplaceSession)
    QML_UNCREATABLE("Use Workspace.replacing")
};

struct CommandModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::CommandModel)
    QML_NAMED_ELEMENT(CommandModel)
    QML_SINGLETON
public:
    static CommandModel *create(QQmlEngine *, QJSEngine *)
    {
        return detail::cppOwned(detail::controllerForQml()->commands());
    }
};

struct ShortcutMapForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ShortcutMap)
    QML_NAMED_ELEMENT(ShortcutMap)
    QML_SINGLETON
public:
    static ShortcutMap *create(QQmlEngine *, QJSEngine *)
    {
        return detail::cppOwned(detail::controllerForQml()->shortcuts());
    }
};

struct FolderTreeModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::FolderTreeModel)
    QML_NAMED_ELEMENT(FolderTreeModel)
};

struct CleanupSelectionForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::CleanupSelection)
    QML_NAMED_ELEMENT(CleanupSelection)
};

} // namespace brulion::shell
