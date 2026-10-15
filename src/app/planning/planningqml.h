// QML registration of the planning classes (library brulion_planning, planning/CMakeLists.txt)
// in the app's QML module "Brulion" — the classes know nothing about QML (the src/app/ui and
// editor pattern): CalendarModel, TaskListModel, GraphModel, GraphItem and PlanningActions are
// creatable types.
//
// PlanningContext (singleton, app-only) is the seam between the views and the shell: it hands
// the open vault's VaultIndex (shell::Workspace::index()) and the EditorHost (the Workspace) to
// the models and follows Workspace::openChanged (a new index per vault, none while closed):
//
//   CalendarModel { vaultIndex: PlanningContext.vaultIndex }
//   PlanningActions { host: PlanningContext.host }
//
// Without an AppController (component gallery, tools) both are null and the views show their
// empty states.
#pragma once

#include "calendarmodel.h"
#include "graphitem.h"
#include "graphmodel.h"
#include "planningactions.h"
#include "tasklistmodel.h"

#include "appcontroller.h"
#include "index/vaultindex.h"
#include "workspace.h"

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace brulion::planning {

struct CalendarModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::planning::CalendarModel)
    QML_NAMED_ELEMENT(CalendarModel)
};

struct TaskListModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::planning::TaskListModel)
    QML_NAMED_ELEMENT(TaskListModel)
};

struct GraphModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::planning::GraphModel)
    QML_NAMED_ELEMENT(GraphModel)
};

struct GraphItemForeign {
    Q_GADGET
    QML_FOREIGN(brulion::planning::GraphItem)
    QML_NAMED_ELEMENT(GraphItem)
};

struct PlanningActionsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::planning::PlanningActions)
    QML_NAMED_ELEMENT(PlanningActions)
};

/// The open vault's index and host for the planning views (see the file comment).
class PlanningContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The VaultIndex of the open vault (null while no vault is open).
    Q_PROPERTY(QObject *vaultIndex READ vaultIndex NOTIFY vaultIndexChanged FINAL)
    /// The Workspace as EditorHost (writes, navigation).
    Q_PROPERTY(brulion::EditorHost *host READ host CONSTANT FINAL)

public:
    explicit PlanningContext(QObject *parent = nullptr) : QObject(parent)
    {
        if (auto *app = shell::AppController::instance())
            m_workspace = app->workspace();
        if (m_workspace)
            connect(m_workspace, &shell::Workspace::openChanged, this, &PlanningContext::vaultIndexChanged);
    }

    QObject *vaultIndex() const { return m_workspace ? static_cast<QObject *>(m_workspace->index()) : nullptr; }
    EditorHost *host() const { return m_workspace.data(); }

Q_SIGNALS:
    void vaultIndexChanged();

private:
    QPointer<shell::Workspace> m_workspace;
};

} // namespace brulion::planning
