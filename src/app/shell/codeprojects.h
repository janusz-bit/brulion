#pragma once
// CodeProjects — the projects of Code mode (docs/CODE.md; QML: Workspace.projects): folders on this
// device — a git repository usually — that AI agents work in, as in T3 Code. Kept in the device's
// settings (AppSettings "code/projects", JSON), never in the notebook: a path is this device's, and
// a synced file must never point an agent at a folder. A thread of a project is a conversation of
// the AI chat whose page names it (`project: <id>`, chat::Conversation::project) — the agent's
// session runs in the project's folder, or in the git worktree the thread has of its own.
//
//   Workspace.projects.list / add(folder) / remove(id) / rename(id, name) / move(id, index)
//   Workspace.projects.setScripts(id, [{name, command, icon}])   // the project's script buttons
//   Workspace.projects.folderOf(id) / nameOf(id) / project(id)
//   Workspace.projects.worktreeFor(id, title) / worktreesRoot / editorCommand
//
// Single-threaded (the GUI thread).

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace brulion::shell {

class CodeProjects : public QObject {
    Q_OBJECT
    /// The projects, in the user's order: [{id, name, path, exists, scripts: [{name, command,
    /// icon}]}].
    Q_PROPERTY(QVariantList list READ list NOTIFY changed FINAL)
    Q_PROPERTY(int count READ count NOTIFY changed FINAL)
    /// Where new worktrees go: <worktreesRoot>/<project id>/<thread> ("" the default:
    /// ~/.brulion/worktrees). A device setting.
    Q_PROPERTY(QString worktreesRoot READ worktreesRoot WRITE setWorktreesRoot NOTIFY changed FINAL)
    /// The command "Open in editor" runs with the folder as its last argument ("" the default:
    /// the system's handler of the folder). A device setting: `code`, `zed`, `cursor`, `idea` …
    Q_PROPERTY(QString editorCommand READ editorCommand WRITE setEditorCommand NOTIFY changed FINAL)

public:
    explicit CodeProjects(QObject *parent = nullptr);

    struct Script {
        QString name;
        QString command;
        QString icon;
        bool operator==(const Script &) const = default;
    };
    struct Project {
        QString id;
        QString name;
        QString path;
        QList<Script> scripts;
        bool operator==(const Project &) const = default;
    };

    /// How many projects a device keeps at most, and scripts a project.
    static constexpr int MaxProjects = 100;
    static constexpr int MaxScripts = 12;

    /// Where the list is kept: `load` gives the JSON, `save` keeps it (AppController: the device's
    /// settings). Unset: nothing is kept.
    void setStorage(std::function<QByteArray()> load, std::function<void(const QByteArray &)> save);

    QVariantList list() const;
    int count() const { return int(m_projects.size()); }
    QList<Project> projects() const { return m_projects; }

    /// Adds the folder `folder` (a path or a file: URL) as a project: its id (that of the project
    /// it is already, "" for a folder that is not there).
    Q_INVOKABLE QString add(const QString &folder);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    /// Moves the project `id` to `index` in the list.
    Q_INVOKABLE void move(const QString &id, int index);
    /// Its script buttons: [{name, command, icon}] (empty ones left out, at most MaxScripts).
    Q_INVOKABLE void setScripts(const QString &id, const QVariantList &scripts);
    /// Its scripts as the project dialog edits them: a line each, "name: command" …
    Q_INVOKABLE QString scriptsText(const QString &id) const;
    /// … and back (a line without a short name before ":" is a command named after itself).
    Q_INVOKABLE void setScriptsText(const QString &id, const QString &text);
    /// The scripts of `text` (setScriptsText()) as setScripts() takes them.
    static QVariantList parseScripts(const QString &text);

    /// {id, name, path, exists, scripts} of `id` ({} none).
    Q_INVOKABLE QVariantMap project(const QString &id) const;
    /// Its folder ("" none) / its name ("" none).
    Q_INVOKABLE QString folderOf(const QString &id) const;
    Q_INVOKABLE QString nameOf(const QString &id) const;
    Q_INVOKABLE bool contains(const QString &id) const;

    QString worktreesRoot() const;
    void setWorktreesRoot(const QString &root);
    QString editorCommand() const { return m_editor; }
    void setEditorCommand(const QString &command);
    /// The folder and branch of a new worktree for a thread `title` of project `id`:
    /// {folder: <worktreesRoot>/<id>/<slug>, branch: "brulion/<slug>"} — a free folder name.
    Q_INVOKABLE QVariantMap worktreeFor(const QString &id, const QString &title) const;
    /// Makes a git worktree of project `id` for a new thread `title` (worktreeFor(): a new branch
    /// from the project's HEAD) without waiting: worktreeCreated() says how it went. False when it
    /// could not start (no such project, no git).
    Q_INVOKABLE bool createWorktree(const QString &id, const QString &title);
    /// Opens `folder` in the editor (editorCommand, else the system's handler of folders).
    Q_INVOKABLE bool openInEditor(const QString &folder) const;
    /// Shows `folder` in the system's file manager.
    Q_INVOKABLE bool openFolder(const QString &folder) const;
    /// `command` split as a shell would (quotes, backslashes), no shell run (tests).
    static QStringList splitCommand(const QString &command);

    /// "Mój Projekt (2)!" → "moj-projekt-2": lower-case ASCII letters, digits and dashes, at most
    /// 40 characters ("project" when nothing is left).
    static QString slug(const QString &text);

Q_SIGNALS:
    void changed();
    /// createWorktree() ended: the worktree's folder and branch, or `error` ("" fine).
    void worktreeCreated(const QString &project, const QString &folder, const QString &branch, const QString &error);

private:
    QList<Project> m_projects;
    QString m_worktreesRoot;
    QString m_editor;
    std::function<QByteArray()> m_load;
    std::function<void(const QByteArray &)> m_save;
    int indexOf(const QString &id) const;
    void load();
    void save();
};

} // namespace brulion::shell
