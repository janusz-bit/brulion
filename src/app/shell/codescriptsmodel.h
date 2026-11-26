#pragma once
// CodeScriptsModel — the rows of the project dialog's scripts editor (docs/UI.md "Code mode";
// QML type CodeScriptsModel): this device's scripts of a project in their order, then those of its
// t3.json no script of the device's overrides (CodeProjects::scripts()). A row of the file is read
// only until it is changed: then it is the device's own, of the same name — it overrides the file's
// (`overrides`); deleting that one brings the file's back. save() keeps the device's rows
// (CodeProjects::setScripts); t3.json is never written.
//
//   CodeScriptsModel { projects: Workspace.projects; project: id }
//   model.add() / remove(row) / moveRow(row, delta) / setText(row, "command", "npm test") /
//   setFlag(row, "wait", true) / save()

#include <QAbstractListModel>
#include <QPointer>

#include "codeprojects.h"

namespace brulion::shell {

class CodeScriptsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(brulion::shell::CodeProjects *projects READ projects WRITE setProjects NOTIFY projectsChanged FINAL)
    /// The project whose scripts are edited (setting it reads them again).
    Q_PROPERTY(QString project READ project WRITE setProject NOTIFY projectChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// The rows differ from what is kept.
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged FINAL)
    /// Changes with every change of the rows (a binding over several rows asks again).
    Q_PROPERTY(int revision READ revision NOTIFY dirtyChanged FINAL)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        CommandRole,
        IconRole,
        IconNameRole,
        ShortcutRole,
        OnWorktreeRole,
        WaitRole,
        /// The row is the file's, unchanged ("t3json"), or the device's ("device").
        SourceRole,
        /// A device's row that replaces one of the file of its name.
        OverridesRole,
        /// The script's id (as CodeProjects::scripts() gives it).
        ScriptIdRole,
    };

    explicit CodeScriptsModel(QObject *parent = nullptr);

    CodeProjects *projects() const { return m_projects; }
    void setProjects(CodeProjects *projects);
    QString project() const { return m_project; }
    void setProject(const QString &project);
    int count() const { return int(m_rows.size()); }
    bool dirty() const;
    int revision() const { return m_revision; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Reads the project's scripts again (what was changed and not saved is gone).
    Q_INVOKABLE void reload();
    /// A new empty row of the device's at the end of its rows: its row.
    Q_INVOKABLE int add();
    /// Deletes a row of the device's (a row of the file cannot be: it is in t3.json); an override
    /// deleted brings the file's row of its name back.
    Q_INVOKABLE bool remove(int row);
    /// Moves a row of the device's by `delta` among the device's rows (the file's stay after them).
    Q_INVOKABLE bool moveRow(int row, int delta);
    /// Sets "name", "command", "icon" or "shortcut" of `row` / "runOnWorktreeCreate" or "wait".
    Q_INVOKABLE void setText(int row, const QString &field, const QString &value);
    Q_INVOKABLE void setFlag(int row, const QString &field, bool value);
    /// A row can be moved up / down (a device's row with another of the device's there).
    Q_INVOKABLE bool canMove(int row, int delta) const;
    /// Another row has its name (letter case aside): two cannot.
    Q_INVOKABLE bool nameTaken(int row) const;
    /// Keeps the device's rows (CodeProjects::setScripts): empty commands left out.
    Q_INVOKABLE void save();

Q_SIGNALS:
    void projectsChanged();
    void projectChanged();
    void countChanged();
    void dirtyChanged();

private:
    struct Row {
        CodeProjects::Script script;
        bool fromFile = false;
        bool overrides = false;
        QString id;
        bool operator==(const Row &) const = default;
    };
    QPointer<CodeProjects> m_projects;
    QString m_project;
    QList<Row> m_rows;
    QList<Row> m_kept;
    int m_revision = 0;
    /// The device's rows end here (the file's come after).
    int deviceCount() const;
    void changed(int row);
    /// `row` as a row of the device's (a file's one becomes its override): its row now.
    int own(int row);
    /// Whether the file has a script named `name`.
    bool inFile(const QString &name) const;
    CodeProjects::Script fileScript(const QString &name) const;
};

} // namespace brulion::shell
