#include "codeprojects.h"

#include "acp/codehost.h"
#include "remoteprojects.h"

#include <QDesktopServices>
#include "gitcli/client.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBoundaryFinder>
#include <QUrl>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// A script's fields as kept and as QML gives them.
CodeProjects::Script scriptFrom(const QVariantMap &m)
{
    CodeProjects::Script s;
    s.name = m.value(u"name"_s).toString().simplified().left(60);
    s.command = m.value(u"command"_s).toString().trimmed().left(4000);
    s.icon = m.value(u"icon"_s).toString().trimmed();
    s.shortcut = m.value(u"shortcut"_s).toString().trimmed().left(40);
    s.onWorktree = m.value(u"runOnWorktreeCreate"_s).toBool();
    s.wait = s.onWorktree && m.value(u"wait"_s).toBool();
    return s;
}

QJsonObject scriptJson(const CodeProjects::Script &s)
{
    QJsonObject o{{u"name"_s, s.name}, {u"command"_s, s.command}, {u"icon"_s, s.icon}};
    if (!s.shortcut.isEmpty())
        o.insert(u"shortcut"_s, s.shortcut);
    if (s.onWorktree)
        o.insert(u"runOnWorktreeCreate"_s, true);
    if (s.wait)
        o.insert(u"wait"_s, true);
    return o;
}

/// One grapheme of `icon` (an emoji with its modifiers and joiners, a letter), "" for none.
QString cleanIcon(const QString &icon)
{
    QString clean = icon.trimmed();
    if (clean.isEmpty())
        return {};
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, clean);
    const qsizetype end = graphemes.toNextBoundary();
    clean = clean.left(end > 0 ? end : clean.size()).left(16);
    return clean.at(0).category() == QChar::Other_Control || clean.at(0).isSpace() ? QString() : clean;
}

const QStringList &t3Icons()
{
    static const QStringList icons{u"play"_s, u"test"_s, u"lint"_s, u"configure"_s, u"build"_s, u"debug"_s};
    return icons;
}

} // namespace

CodeProjects::CodeProjects(QObject *parent) : QObject(parent)
{
    // (A t3.json saved by an editor fires several times: read once they are done.)
    m_configTimer.setSingleShot(true);
    m_configTimer.setInterval(250);
    connect(&m_configTimer, &QTimer::timeout, this, [this] {
        bool any = false;
        for (const QString &id : std::exchange(m_configDue, {})) {
            const Config before = m_configs.value(id);
            readConfig(id);
            any = any || !(m_configs.value(id) == before);
            // (A file saved by replacing it is another file: watched again below.)
            if (m_watcher)
                m_watcher->removePath(QDir(folderOf(id)).filePath(u"t3.json"_s));
        }
        watchConfigs();
        if (any)
            Q_EMIT changed();
    });
    // (git reports progress many times a second: the list follows a few times a second.)
    m_cloneTimer.setSingleShot(true);
    m_cloneTimer.setInterval(250);
    connect(&m_cloneTimer, &QTimer::timeout, this, &CodeProjects::clonesChanged);
}

CodeProjects::~CodeProjects()
{
    // A clone still running when the app quits: stopped (the client waits for git), and the
    // folder it made so far taken away — never one that was there before.
    bool running = false;
    for (const Clone &c : std::as_const(m_clones))
        running = running || c.running;
    if (!running)
        return;
    delete m_git;
    m_git = nullptr;
    for (const Clone &c : std::as_const(m_clones)) {
        if (c.running && !c.existed)
            QDir(c.folder).removeRecursively();
    }
}

void CodeProjects::setStorage(std::function<QByteArray()> load, std::function<void(const QByteArray &)> save)
{
    m_load = std::move(load);
    m_save = std::move(save);
    this->load();
}

QString CodeProjects::slug(const QString &text)
{
    return acp::projectSlug(text); // (the computer that shares its projects names them so too)
}

QString CodeProjects::slugOrEmpty(const QString &text)
{
    // (projectSlug()'s "project" is also what it gives when nothing of `text` is left: then `text`
    // and a word after it give that word alone.)
    const QString out = acp::projectSlug(text);
    return out == u"project" && acp::projectSlug(QString(text + u" x"_s)) == u"x" ? QString() : out;
}

int CodeProjects::indexOf(const QString &id) const
{
    for (int i = 0; i < m_projects.size(); ++i)
        if (m_projects.at(i).id == id)
            return i;
    return -1;
}

QList<CodeProjects::Project> CodeProjects::displayed() const
{
    QList<Project> out = m_projects;
    std::ranges::stable_partition(out, [](const Project &p) { return p.pinned; });
    return out;
}

QVariantList CodeProjects::list() const
{
    QVariantList out;
    for (const Project &p : displayed())
        out << project(p.id);
    // (Then those of the user's other computers this device has no project of the id of.)
    if (m_remote) {
        for (const RemoteProjects::Project &r : m_remote->projects()) {
            if (indexOf(r.id) < 0)
                out << project(r.id);
        }
    }
    return out;
}

int CodeProjects::count() const
{
    int n = int(m_projects.size());
    if (m_remote) {
        for (const RemoteProjects::Project &r : m_remote->projects())
            n += indexOf(r.id) < 0 ? 1 : 0;
    }
    return n;
}

void CodeProjects::setRemote(RemoteProjects *remote)
{
    if (m_remote == remote)
        return;
    if (m_remote)
        m_remote->disconnect(this);
    m_remote = remote;
    if (m_remote) {
        connect(m_remote, &RemoteProjects::changed, this, &CodeProjects::changed);
        connect(m_remote, &RemoteProjects::worktreeCreated, this, &CodeProjects::worktreeCreated);
    }
    Q_EMIT changed();
}

bool CodeProjects::isRemote(const QString &id) const
{
    return indexOf(id) < 0 && m_remote && m_remote->find(id).has_value();
}

QVariantMap CodeProjects::project(const QString &id) const
{
    const int i = indexOf(id);
    if (i < 0 && m_remote) {
        if (const std::optional<RemoteProjects::Project> r = m_remote->find(id))
            return {{u"id"_s, r->id},       {u"name"_s, r->name},          {u"path"_s, QString()},
                    {u"exists"_s, false},   {u"scripts"_s, QVariantList()}, {u"remote"_s, true},
                    {u"host"_s, r->host},   {u"remotePath"_s, r->path}};
    }
    if (i < 0)
        return {};
    const Project &p = m_projects.at(i);
    const Config c = m_configs.value(id);
    return {{u"id"_s, p.id},
            {u"name"_s, p.name},
            {u"path"_s, p.path},
            {u"exists"_s, QFileInfo(p.path).isDir()},
            {u"pinned"_s, p.pinned},
            {u"icon"_s, p.icon},
            {u"iconUrl"_s, c.iconPath.isEmpty() ? QString() : QUrl::fromLocalFile(c.iconPath).toString()},
            {u"agent"_s, p.agent},
            {u"env"_s, p.env},
            {u"newThreadEnv"_s, newThreadEnv(id)},
            {u"scripts"_s, scripts(id)},
            {u"config"_s, c.found && c.error.isEmpty()},
            {u"configError"_s, c.error}};
}

QString CodeProjects::folderOf(const QString &id) const
{
    const int i = indexOf(id);
    return i < 0 ? QString() : m_projects.at(i).path;
}

QString CodeProjects::nameOf(const QString &id) const
{
    const int i = indexOf(id);
    if (i < 0 && m_remote) {
        if (const std::optional<RemoteProjects::Project> r = m_remote->find(id))
            return r->name;
    }
    return i < 0 ? QString() : m_projects.at(i).name;
}

bool CodeProjects::contains(const QString &id) const
{
    return indexOf(id) >= 0 || isRemote(id);
}

QString CodeProjects::projectOfFolder(const QString &folder) const
{
    const QString clean = QDir::cleanPath(folder);
    if (clean.isEmpty() || clean == u".")
        return {};
    for (const Project &p : m_projects)
        if (QDir::cleanPath(p.path) == clean)
            return p.id;
    const QString root = QDir::cleanPath(worktreesRoot()) + u'/';
    if (clean.startsWith(root)) {
        const QString id = clean.mid(root.size()).section(u'/', 0, 0);
        if (contains(id))
            return id;
    }
    return {};
}

QString CodeProjects::add(const QString &folder)
{
    QString path = folder.trimmed();
    if (path.startsWith(u"file:"_s))
        path = QUrl(path).toLocalFile();
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return {};
    path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity paths = Qt::CaseInsensitive; // (C:/Projekty and c:/projekty)
#else
    constexpr Qt::CaseSensitivity paths = Qt::CaseSensitive;
#endif
    for (const Project &p : std::as_const(m_projects))
        if (QDir::cleanPath(p.path).compare(path, paths) == 0)
            return p.id;
    if (m_projects.size() >= MaxProjects)
        return {};
    Project p;
    p.name = QFileInfo(path).fileName();
    if (p.name.isEmpty())
        p.name = path;
    const QString base = slug(p.name);
    p.id = base;
    for (int n = 2; indexOf(p.id) >= 0; ++n)
        p.id = base + u'-' + QString::number(n);
    p.path = path;
    m_projects.append(p);
    readConfig(p.id);
    watchConfigs();
    save();
    return p.id;
}

void CodeProjects::remove(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    m_projects.removeAt(i);
    m_configs.remove(id);
    watchConfigs();
    save();
}

void CodeProjects::rename(const QString &id, const QString &name)
{
    const int i = indexOf(id);
    const QString clean = name.simplified().left(80);
    if (i < 0 || clean.isEmpty() || m_projects.at(i).name == clean)
        return;
    m_projects[i].name = clean;
    save();
}

void CodeProjects::move(const QString &id, int index)
{
    // (Within its part of list(): the pinned ones, or the others.)
    QList<Project> shown = displayed();
    const auto it = std::ranges::find(shown, id, &Project::id);
    if (it == shown.end())
        return;
    const int from = int(it - shown.begin());
    const int pinned = int(std::ranges::count_if(shown, &Project::pinned));
    const int first = it->pinned ? 0 : pinned;
    const int last = it->pinned ? pinned - 1 : int(shown.size()) - 1;
    const int to = std::clamp(index, first, last);
    if (from == to)
        return;
    shown.move(from, to);
    m_projects = shown;
    save();
}

void CodeProjects::setPinned(const QString &id, bool pinned)
{
    const int i = indexOf(id);
    if (i < 0 || m_projects.at(i).pinned == pinned)
        return;
    m_projects[i].pinned = pinned;
    save();
}

void CodeProjects::setIcon(const QString &id, const QString &icon)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    const QString clean = cleanIcon(icon);
    if (m_projects.at(i).icon == clean)
        return;
    m_projects[i].icon = clean;
    save();
}

void CodeProjects::setDefaults(const QString &id, const QString &agent, const QString &env)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    const QString cleanEnv = env == u"local" || env == u"worktree" ? env : QString();
    const QString cleanAgent = agent.trimmed().left(200);
    if (m_projects.at(i).agent == cleanAgent && m_projects.at(i).env == cleanEnv)
        return;
    m_projects[i].agent = cleanAgent;
    m_projects[i].env = cleanEnv;
    save();
}

QString CodeProjects::newThreadEnv(const QString &id) const
{
    const int i = indexOf(id);
    if (i >= 0 && !m_projects.at(i).env.isEmpty())
        return m_projects.at(i).env;
    if (!m_newThreadEnv.isEmpty())
        return m_newThreadEnv;
    const QString fromFile = m_configs.value(id).defaultThreadEnvMode;
    return fromFile.isEmpty() ? u"local"_s : fromFile;
}

void CodeProjects::setScripts(const QString &id, const QVariantList &scripts)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    QList<Script> out;
    QStringList names;
    for (const QVariant &v : scripts) {
        Script s = scriptFrom(v.toMap());
        if (s.command.isEmpty())
            continue;
        if (s.name.isEmpty())
            s.name = s.command.simplified().left(60);
        if (!t3Icons().contains(s.icon))
            s.icon = u"play"_s;
        // (A second one of a name: left out — a name is what a t3.json's script is overridden by.)
        if (names.contains(s.name.toLower()))
            continue;
        names << s.name.toLower();
        out << s;
        if (out.size() >= MaxScripts)
            break;
    }
    if (out == m_projects.at(i).scripts)
        return;
    m_projects[i].scripts = out;
    save();
}

QList<std::pair<CodeProjects::Script, QString>> CodeProjects::mergeScripts(const QList<Script> &device,
                                                                           const QList<Script> &file)
{
    QList<std::pair<Script, QString>> out;
    QStringList names;
    for (const Script &s : device) {
        const bool overrides = std::ranges::any_of(
                file, [&s](const Script &f) { return f.name.compare(s.name, Qt::CaseInsensitive) == 0; });
        out.append({s, overrides ? u"override"_s : u"device"_s});
        names << s.name.toLower();
    }
    for (const Script &s : file) {
        if (names.contains(s.name.toLower()) || out.size() >= MaxScripts)
            continue;
        names << s.name.toLower();
        out.append({s, u"t3json"_s});
    }
    return out;
}

QString CodeProjects::iconOf(const QString &t3Icon)
{
    if (t3Icon == u"test")
        return u"circle-check"_s;
    if (t3Icon == u"lint")
        return u"list-checks"_s;
    if (t3Icon == u"configure")
        return u"sliders-horizontal"_s;
    if (t3Icon == u"build")
        return u"layers"_s;
    if (t3Icon == u"debug")
        return u"eye"_s;
    return u"play"_s;
}

QVariantList CodeProjects::scripts(const QString &id) const
{
    const int i = indexOf(id);
    if (i < 0)
        return {};
    QVariantList out;
    QStringList ids;
    for (const auto &[s, source] : mergeScripts(m_projects.at(i).scripts, m_configs.value(id).scripts)) {
        QString sid = slug(s.name);
        const QString base = sid;
        for (int n = 2; ids.contains(sid); ++n)
            sid = base + u'-' + QString::number(n);
        ids << sid;
        out << QVariantMap{{u"id"_s, sid},
                           {u"name"_s, s.name},
                           {u"command"_s, s.command},
                           {u"icon"_s, s.icon},
                           {u"iconName"_s, iconOf(s.icon)},
                           {u"shortcut"_s, s.shortcut},
                           {u"runOnWorktreeCreate"_s, s.onWorktree},
                           {u"wait"_s, s.wait},
                           {u"source"_s, source == u"t3json" ? u"t3json"_s : u"device"_s},
                           {u"overrides"_s, source == u"override"}};
    }
    return out;
}

QVariantList CodeProjects::setupScripts(const QString &id) const
{
    QVariantList out;
    for (const QVariant &v : scripts(id))
        if (v.toMap().value(u"runOnWorktreeCreate"_s).toBool())
            out << v;
    return out;
}

QString CodeProjects::scriptField(const QString &id, const QString &scriptId, const QString &field) const
{
    for (const QVariant &v : scripts(id))
        if (v.toMap().value(u"id"_s).toString() == scriptId)
            return v.toMap().value(field).toString();
    return {};
}

QStringList CodeProjects::setupList(const QString &id, const QString &field, bool waited) const
{
    QStringList out;
    for (const QVariant &v : setupScripts(id)) {
        const QVariantMap m = v.toMap();
        if (m.value(u"wait"_s).toBool() == waited)
            out << m.value(field).toString();
    }
    return out;
}

QString CodeProjects::worktreesRoot() const
{
    return m_worktreesRoot.isEmpty() ? QDir::home().filePath(u".brulion/worktrees"_s) : m_worktreesRoot;
}

void CodeProjects::setWorktreesRoot(const QString &root)
{
    QString clean = root.trimmed();
    if (clean.startsWith(u"file:"_s))
        clean = QUrl(clean).toLocalFile();
    if (!clean.isEmpty())
        clean = QDir::cleanPath(clean);
    if (clean == m_worktreesRoot)
        return;
    m_worktreesRoot = clean;
    save();
}

void CodeProjects::setEditorCommand(const QString &command)
{
    const QString clean = command.trimmed();
    if (clean == m_editor)
        return;
    m_editor = clean;
    save();
    Q_EMIT editorsChanged();
}

void CodeProjects::setBranchPrefix(const QString &prefix)
{
    // (Only what a branch name may have: "feature/", "ala-"; anything else keeps the one before.)
    const QString clean = prefix.trimmed();
    if (clean == m_branchPrefix || (!clean.isEmpty() && !validBranchName(clean + u"x"_s)))
        return;
    m_branchPrefix = clean;
    save();
}

void CodeProjects::setDeletedWorktrees(const QString &policy)
{
    const QString clean = policy == u"delete" || policy == u"keep" ? policy : u"ask"_s;
    if (clean == m_deletedWorktrees)
        return;
    m_deletedWorktrees = clean;
    save();
}

void CodeProjects::setNewThreadEnvDefault(const QString &env)
{
    const QString clean = env == u"local" || env == u"worktree" ? env : QString();
    if (clean == m_newThreadEnv)
        return;
    m_newThreadEnv = clean;
    save();
}

void CodeProjects::setThreadSort(const QString &sort)
{
    const QString clean = sort == u"created" ? sort : u"activity"_s;
    if (clean == m_threadSort)
        return;
    m_threadSort = clean;
    save();
}

void CodeProjects::keepUndo(int toast, const QStringList &trashPaths, const QString &id, const QString &folder,
                            const QString &branch, bool removed)
{
    if (toast <= 0)
        return;
    m_undos.insert(toast, Undo{trashPaths, id, folder, branch, removed});
}

void CodeProjects::undo(int toast)
{
    if (!m_undos.contains(toast))
        return;
    const Undo u = m_undos.take(toast);
    if (m_restore && !u.trash.isEmpty())
        m_restore(u.trash);
    if (u.removed && !u.folder.isEmpty() && !u.branch.isEmpty())
        restoreWorktree(u.project, u.folder, u.branch);
}

void CodeProjects::forgetUndo(int toast)
{
    m_undos.remove(toast);
}

void CodeProjects::load()
{
    m_projects.clear();
    const QJsonObject all = QJsonDocument::fromJson(m_load ? m_load() : QByteArray()).object();
    for (const QJsonValue &v : all.value(u"projects"_s).toArray()) {
        const QJsonObject o = v.toObject();
        Project p;
        p.id = slug(o.value(u"id"_s).toString());
        p.name = o.value(u"name"_s).toString().simplified().left(80);
        p.path = o.value(u"path"_s).toString();
        if (o.value(u"id"_s).toString().isEmpty() || p.path.isEmpty() || indexOf(p.id) >= 0)
            continue; // (broken, or a second one of an id: left out)
        if (p.name.isEmpty())
            p.name = p.id;
        QStringList names;
        for (const QJsonValue &sv : o.value(u"scripts"_s).toArray()) {
            Script s = scriptFrom(sv.toObject().toVariantMap());
            if (s.command.isEmpty() || p.scripts.size() >= MaxScripts)
                continue;
            if (s.name.isEmpty())
                s.name = s.command.simplified().left(60);
            if (!t3Icons().contains(s.icon))
                s.icon = u"play"_s;
            if (names.contains(s.name.toLower()))
                continue;
            names << s.name.toLower();
            p.scripts << s;
        }
        p.pinned = o.value(u"pinned"_s).toBool();
        p.agent = o.value(u"agent"_s).toString().left(200);
        const QString env = o.value(u"env"_s).toString();
        p.env = env == u"local" || env == u"worktree" ? env : QString();
        p.icon = cleanIcon(o.value(u"icon"_s).toString());
        m_projects << p;
        if (m_projects.size() >= MaxProjects)
            break;
    }
    m_worktreesRoot = all.value(u"worktreesRoot"_s).toString();
    m_clonesRoot = all.value(u"clonesRoot"_s).toString();
    m_editor = all.value(u"editor"_s).toString();
    m_preferredEditor = all.value(u"preferredEditor"_s).toString();
    m_branchPrefix = all.contains(u"branchPrefix"_s) ? all.value(u"branchPrefix"_s).toString() : u"brulion/"_s;
    if (!m_branchPrefix.isEmpty() && !validBranchName(m_branchPrefix + u"x"_s))
        m_branchPrefix = u"brulion/"_s;
    const QString deleted = all.value(u"deletedWorktrees"_s).toString();
    m_deletedWorktrees = deleted == u"delete" || deleted == u"keep" ? deleted : u"ask"_s;
    const QString env = all.value(u"newThreadEnv"_s).toString();
    m_newThreadEnv = env == u"local" || env == u"worktree" ? env : QString();
    m_threadSort = all.value(u"threadSort"_s).toString() == u"created" ? u"created"_s : u"activity"_s;
    m_configs.clear();
    for (const Project &p : std::as_const(m_projects))
        readConfig(p.id);
    watchConfigs();
    Q_EMIT changed();
    Q_EMIT editorsChanged();
}

void CodeProjects::save()
{
    QJsonArray projects;
    for (const Project &p : std::as_const(m_projects)) {
        QJsonArray scripts;
        for (const Script &s : p.scripts)
            scripts << scriptJson(s);
        QJsonObject o{{u"id"_s, p.id}, {u"name"_s, p.name}, {u"path"_s, p.path}};
        if (!scripts.isEmpty())
            o.insert(u"scripts"_s, scripts);
        if (p.pinned)
            o.insert(u"pinned"_s, true);
        if (!p.icon.isEmpty())
            o.insert(u"icon"_s, p.icon);
        if (!p.agent.isEmpty())
            o.insert(u"agent"_s, p.agent);
        if (!p.env.isEmpty())
            o.insert(u"env"_s, p.env);
        projects << o;
    }
    QJsonObject all{{u"projects"_s, projects}};
    if (!m_worktreesRoot.isEmpty())
        all.insert(u"worktreesRoot"_s, m_worktreesRoot);
    if (!m_clonesRoot.isEmpty())
        all.insert(u"clonesRoot"_s, m_clonesRoot);
    if (!m_editor.isEmpty())
        all.insert(u"editor"_s, m_editor);
    if (!m_preferredEditor.isEmpty())
        all.insert(u"preferredEditor"_s, m_preferredEditor);
    if (m_branchPrefix != u"brulion/")
        all.insert(u"branchPrefix"_s, m_branchPrefix);
    if (m_deletedWorktrees != u"ask")
        all.insert(u"deletedWorktrees"_s, m_deletedWorktrees);
    if (!m_newThreadEnv.isEmpty())
        all.insert(u"newThreadEnv"_s, m_newThreadEnv);
    if (m_threadSort != u"activity")
        all.insert(u"threadSort"_s, m_threadSort);
    if (m_save)
        m_save(QJsonDocument(all).toJson(QJsonDocument::Compact));
    Q_EMIT changed();
}

} // namespace brulion::shell
