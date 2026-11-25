// QML registration of the shell's C++ types (library brulion_shell, src/app/shell/area.cmake)
// in the app's QML module "Brulion". The classes know nothing about QML; these wrappers make
// them QML types (the src/app/ui/qmlforeign.h pattern):
//
//   AppController    singleton — main()'s instance (AppController::instance())
//   AppSettings      singleton — AppController.settings
//   AppLock          singleton — AppController.appLock (the app lock: applockcontroller.h)
//   LocalAi          uncreatable — AppController.localAi (the model on the device)
//   McpServerSupervisor uncreatable — AppController.mcpServer (the remote MCP server while the app runs)
//   SemanticSearch   uncreatable — Workspace.semantic (search by meaning)
//   RecentNotebooks  uncreatable — AppController.recentNotebooks (the start screen's list)
//   ThemePages       singleton — AppController::themePages() (the notebook's theme pages)
//   Workspace        singleton — AppController.workspace (the EditorHost for the editor)
//   WorkspaceReminders, WorkspaceSync, WorkspaceGitHub, WorkspaceCalendars, WorkspaceBackups, WorkspaceExtensions,
//   WorkspaceChats   uncreatable —      Workspace.reminders / .sync / .github / .backups / .extensions
//                                       / .chats
//   ChatTurn         uncreatable — Workspace.chats.turn(path): one conversation's turn
//   AgentShare       uncreatable — Workspace.chats.share ("Share agents with the phone")
//   AgentLimits      uncreatable — Workspace.chats.limits (Claude Code's and Codex's subscription limits)
//   ChatLineage, ChatSchedules uncreatable — Workspace.chats.lineage / .schedules (sub-conversations,
//                    scheduled conversations)
//   WritingSession   creatable — WritingSession { workspace: Workspace } (the editor's AI assistant)
//   AiReportDraft    creatable — AiReportDraft {} (Report response: the report of an AI answer)
//   WorkspaceEncryption uncreatable — Workspace.encryption (encrypted notes)
//   EncryptedPageHost creatable — PageView's EditorHost (encryptedpagehost.h)
//   ExtensionFormModel, ExtensionControlsModel   uncreatable — a question's fields, a row's controls
//   ExtensionListModel, ExtensionConsoleModel    uncreatable — Workspace.extensions.list / .console
//   ExtensionView    creatable — ExtensionView { extensions: Workspace.extensions; extension; view }
//   ReplaceSession   uncreatable — Workspace.replacing (the notebook-wide replace)
//   CommandModel     singleton — the command palette's commands
//   ShortcutMap      singleton — the commands' keyboard shortcuts (custom ones too)
//   FolderTreeModel  creatable — FolderTreeModel { workspace: Workspace }
//   ViewTraits       creatable — ViewTraits { view: loader.item } (ShellRouteView)
//   ViewKeeper       creatable — ViewKeeper { limit: MemoryPolicy.keptViews } (ShellRouteView's hidden views)
//   CleanupSelection creatable — CleanupView's shown and ticked files
//   WebClipperSetup  singleton — Settings' "Browser extension (Web Clipper)" (the host manifests)
#pragma once

#include "aireportdraft.h"
#include "appcontroller.h"
#include "applockcontroller.h"
#include "appscripting.h"
#include "appsettings.h"
#include "appupdates.h"
#include "chatlineage.h"
#include "chatschedules.h"
#include "cleanupselection.h"
#include "commandmodel.h"
#include "encryptedpagehost.h"
#include "extensionviewmodel.h"
#include "foldertreemodel.h"
#include "recentnotebooks.h"
#include "replacesession.h"
#include "shortcutmap.h"
#include "themepages.h"
#include "viewkeeper.h"
#include "viewtraits.h"
#include "webclippersetup.h"
#include "workspace.h"
#include "workspacebackups.h"
#include "workspacecalendars.h"
#include "workspacechats.h"
#include "workspaceencryption.h"
#include "workspaceextensions.h"
#include "workspacegithub.h"
#include "workspacereminders.h"
#include "workspacesync.h"
#include "writingsession.h"

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

struct WebClipperSetupForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::WebClipperSetup)
    QML_NAMED_ELEMENT(WebClipperSetup)
    QML_SINGLETON
public:
    static WebClipperSetup *create(QQmlEngine *, QJSEngine *)
    {
        static WebClipperSetup *setup = new WebClipperSetup(detail::controllerForQml());
        return detail::cppOwned(setup);
    }
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

struct AppLockForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::AppLockController)
    QML_NAMED_ELEMENT(AppLock)
    QML_SINGLETON
public:
    static AppLockController *create(QQmlEngine *, QJSEngine *)
    {
        return detail::cppOwned(detail::controllerForQml()->appLock());
    }
};

struct AppScriptingForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::AppScripting)
    QML_NAMED_ELEMENT(AppScripting)
    QML_SINGLETON
public:
    static AppScripting *create(QQmlEngine *, QJSEngine *)
    {
        return detail::cppOwned(detail::controllerForQml()->scripting());
    }
};

struct ThemePagesForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ThemePages)
    QML_NAMED_ELEMENT(ThemePages)
    QML_SINGLETON
public:
    static ThemePages *create(QQmlEngine *, QJSEngine *)
    {
        return detail::cppOwned(detail::controllerForQml()->themePages());
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

struct LocalAiForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::LocalAi)
    QML_NAMED_ELEMENT(LocalAi)
    QML_UNCREATABLE("Use AppController.localAi")
};

struct McpServerSupervisorForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::McpServerSupervisor)
    QML_NAMED_ELEMENT(McpServerSupervisor)
    QML_UNCREATABLE("Use AppController.mcpServer")
};

struct SemanticSearchForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::SemanticSearch)
    QML_NAMED_ELEMENT(SemanticSearch)
    QML_UNCREATABLE("Use Workspace.semantic")
};

struct RecentNotebooksForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::RecentNotebooks)
    QML_NAMED_ELEMENT(RecentNotebooks)
    QML_UNCREATABLE("Use AppController.recentNotebooks")
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

struct WorkspaceGitHubForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::WorkspaceGitHub)
    QML_NAMED_ELEMENT(WorkspaceGitHub)
    QML_UNCREATABLE("Use Workspace.github")
};

struct WorkspaceCalendarsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::WorkspaceCalendars)
    QML_NAMED_ELEMENT(WorkspaceCalendars)
    QML_UNCREATABLE("Use Workspace.calendars")
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

struct WorkspaceExtensionsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::WorkspaceExtensions)
    QML_NAMED_ELEMENT(WorkspaceExtensions)
    QML_UNCREATABLE("Use Workspace.extensions")
};

struct WorkspaceChatsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::WorkspaceChats)
    QML_NAMED_ELEMENT(WorkspaceChats)
    QML_UNCREATABLE("Use Workspace.chats")
};

struct AgentShareForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::AgentShare)
    QML_NAMED_ELEMENT(AgentShare)
    QML_UNCREATABLE("Use Workspace.chats.share")
};

struct AgentLimitsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::AgentLimits)
    QML_NAMED_ELEMENT(AgentLimits)
    QML_UNCREATABLE("Use Workspace.chats.limits")
};

struct ChatLineageForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ChatLineage)
    QML_NAMED_ELEMENT(ChatLineage)
    QML_UNCREATABLE("Use Workspace.chats.lineage")
};

struct ChatSchedulesForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ChatSchedules)
    QML_NAMED_ELEMENT(ChatSchedules)
    QML_UNCREATABLE("Use Workspace.chats.schedules")
};

struct WritingSessionForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::WritingSession)
    QML_NAMED_ELEMENT(WritingSession)
};

struct AiReportDraftForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::AiReportDraft)
    QML_NAMED_ELEMENT(AiReportDraft)
};

struct ChatTurnForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ChatTurn)
    QML_NAMED_ELEMENT(ChatTurn)
    QML_UNCREATABLE("Use Workspace.chats.turn(path)")
};

struct WorkspaceEncryptionForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::WorkspaceEncryption)
    QML_NAMED_ELEMENT(WorkspaceEncryption)
    QML_UNCREATABLE("Use Workspace.encryption")
};

struct EncryptedPageHostForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::EncryptedPageHost)
    QML_NAMED_ELEMENT(EncryptedPageHost)
};

struct ExtensionFormModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ExtensionFormModel)
    QML_NAMED_ELEMENT(ExtensionFormModel)
    QML_UNCREATABLE("Use Workspace.extensions.questionFields")
};

struct ExtensionListModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ExtensionListModel)
    QML_NAMED_ELEMENT(ExtensionListModel)
    QML_UNCREATABLE("Use Workspace.extensions.list")
};

struct ExtensionConsoleModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ExtensionConsoleModel)
    QML_NAMED_ELEMENT(ExtensionConsoleModel)
    QML_UNCREATABLE("Use Workspace.extensions.console")
};

struct ExtensionControlsModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ExtensionControlsModel)
    QML_NAMED_ELEMENT(ExtensionControlsModel)
    QML_UNCREATABLE("A row's controls of an ExtensionView")
};

struct ExtensionViewModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ExtensionViewModel)
    QML_NAMED_ELEMENT(ExtensionView)
};

struct FolderTreeModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::FolderTreeModel)
    QML_NAMED_ELEMENT(FolderTreeModel)
};

struct ViewKeeperForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ViewKeeper)
    QML_NAMED_ELEMENT(ViewKeeper)
};

struct ViewTraitsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::ViewTraits)
    QML_NAMED_ELEMENT(ViewTraits)
};

struct CleanupSelectionForeign {
    Q_GADGET
    QML_FOREIGN(brulion::shell::CleanupSelection)
    QML_NAMED_ELEMENT(CleanupSelection)
};

} // namespace brulion::shell
