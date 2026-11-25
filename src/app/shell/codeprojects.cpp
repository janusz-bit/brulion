#include "codeprojects.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::shell {

CodeProjects::CodeProjects(QObject *parent) : QObject(parent) { }

void CodeProjects::setStorage(std::function<QByteArray()> load, std::function<void(const QByteArray &)> save)
{
    m_load = std::move(load);
    m_save = std::move(save);
    this->load();
}

QString CodeProjects::slug(const QString &text)
{
    // (Letters without their marks: "żółw" → "zolw"; "ł" has no decomposition.)
    QString out;
    bool dash = false;
    for (const QChar c : text.normalized(QString::NormalizationForm_KD)) {
        QChar a = c;
        if (c == u'ł' || c == u'Ł')
            a = u'l';
        if (a.isLetterOrNumber() && a.unicode() < 128) {
            if (dash && !out.isEmpty())
                out += u'-';
            out += a.toLower();
            dash = false;
        } else if (a.category() != QChar::Mark_NonSpacing) {
            dash = true;
        }
        if (out.size() >= 40)
            break;
    }
    while (out.endsWith(u'-'))
        out.chop(1);
    return out.isEmpty() ? u"project"_s : out;
}

int CodeProjects::indexOf(const QString &id) const
{
    for (int i = 0; i < m_projects.size(); ++i)
        if (m_projects.at(i).id == id)
            return i;
    return -1;
}

QVariantList CodeProjects::list() const
{
    QVariantList out;
    for (const Project &p : m_projects)
        out << project(p.id);
    return out;
}

QVariantMap CodeProjects::project(const QString &id) const
{
    const int i = indexOf(id);
    if (i < 0)
        return {};
    const Project &p = m_projects.at(i);
    QVariantList scripts;
    for (const Script &s : p.scripts)
        scripts << QVariantMap{{u"name"_s, s.name}, {u"command"_s, s.command}, {u"icon"_s, s.icon}};
    return {{u"id"_s, p.id},
            {u"name"_s, p.name},
            {u"path"_s, p.path},
            {u"exists"_s, QFileInfo(p.path).isDir()},
            {u"scripts"_s, scripts}};
}

QString CodeProjects::folderOf(const QString &id) const
{
    const int i = indexOf(id);
    return i < 0 ? QString() : m_projects.at(i).path;
}

QString CodeProjects::nameOf(const QString &id) const
{
    const int i = indexOf(id);
    return i < 0 ? QString() : m_projects.at(i).name;
}

bool CodeProjects::contains(const QString &id) const
{
    return indexOf(id) >= 0;
}

QString CodeProjects::add(const QString &folder)
{
    QString path = folder.trimmed();
    if (path.startsWith(u"file:"_s))
        path = QUrl(path).toLocalFile();
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return {};
    path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    for (const Project &p : std::as_const(m_projects))
        if (QDir::cleanPath(p.path) == path)
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
    save();
    return p.id;
}

void CodeProjects::remove(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    m_projects.removeAt(i);
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
    const int i = indexOf(id);
    const int to = std::clamp(index, 0, int(m_projects.size()) - 1);
    if (i < 0 || i == to)
        return;
    m_projects.move(i, to);
    save();
}

void CodeProjects::setScripts(const QString &id, const QVariantList &scripts)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    QList<Script> out;
    for (const QVariant &v : scripts) {
        const QVariantMap m = v.toMap();
        Script s{m.value(u"name"_s).toString().simplified().left(40), m.value(u"command"_s).toString().trimmed(),
                 m.value(u"icon"_s).toString().trimmed().left(40)};
        if (s.command.isEmpty())
            continue;
        if (s.name.isEmpty())
            s.name = s.command.left(40);
        out << s;
        if (out.size() >= MaxScripts)
            break;
    }
    if (out == m_projects.at(i).scripts)
        return;
    m_projects[i].scripts = out;
    save();
}

QString CodeProjects::scriptsText(const QString &id) const
{
    const int i = indexOf(id);
    if (i < 0)
        return {};
    QStringList lines;
    for (const Script &s : m_projects.at(i).scripts)
        lines << (s.name == s.command.left(40) ? s.command : s.name + u": "_s + s.command);
    return lines.join(u'\n');
}

QVariantList CodeProjects::parseScripts(const QString &text)
{
    QVariantList out;
    for (const QString &raw : text.split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype colon = line.indexOf(u':');
        const QString head = colon > 0 ? line.left(colon).trimmed() : QString();
        // ("name: command": a short name without spaces; "http://…" or "a b: c" stay commands.)
        const bool named = colon > 0 && colon <= 40 && !head.contains(u' ') && !line.mid(colon + 1).startsWith(u"//"_s);
        out << QVariantMap{{u"name"_s, named ? head : QString()},
                           {u"command"_s, named ? line.mid(colon + 1).trimmed() : line},
                           {u"icon"_s, u"play"_s}};
    }
    return out;
}

void CodeProjects::setScriptsText(const QString &id, const QString &text)
{
    setScripts(id, parseScripts(text));
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
}

QVariantMap CodeProjects::worktreeFor(const QString &id, const QString &title) const
{
    if (indexOf(id) < 0)
        return {};
    const QString base = slug(title);
    const QDir dir(QDir(worktreesRoot()).filePath(id));
    QString name = base;
    for (int n = 2; QFileInfo::exists(dir.filePath(name)); ++n)
        name = base + u'-' + QString::number(n);
    return {{u"folder"_s, dir.filePath(name)}, {u"branch"_s, QString(u"brulion/"_s + name)}};
}

bool CodeProjects::createWorktree(const QString &id, const QString &title)
{
#if QT_CONFIG(process)
    const QString repo = folderOf(id);
    const QString git = QStandardPaths::findExecutable(u"git"_s);
    if (repo.isEmpty() || git.isEmpty())
        return false;
    const QVariantMap place = worktreeFor(id, title);
    const QString folder = place.value(u"folder"_s).toString();
    const QString branch = place.value(u"branch"_s).toString();
    QDir().mkpath(QFileInfo(folder).absolutePath());
    auto *proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"GIT_TERMINAL_PROMPT"_s, u"0"_s);
    env.insert(u"LC_ALL"_s, u"C"_s);
    proc->setProcessEnvironment(env);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    // (No shell: the folder and the branch are arguments of their own.)
    proc->setProgram(git);
    proc->setArguments({u"-C"_s, repo, u"worktree"_s, u"add"_s, u"-b"_s, branch, folder, u"HEAD"_s});
    connect(proc, &QProcess::finished, this, [this, proc, id, folder, branch](int code, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(proc->readAll()).trimmed();
        proc->deleteLater();
        if (status != QProcess::NormalExit || code != 0)
            Q_EMIT worktreeCreated(id, QString(), QString(),
                                   output.isEmpty() ? tr("git could not make the worktree.") : output.right(500));
        else
            Q_EMIT worktreeCreated(id, folder, branch, QString());
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, id](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        proc->deleteLater();
        Q_EMIT worktreeCreated(id, QString(), QString(), tr("git could not be started."));
    });
    proc->start();
    return true;
#else
    Q_UNUSED(id)
    Q_UNUSED(title)
    return false;
#endif
}

QStringList CodeProjects::splitCommand(const QString &command)
{
    QStringList out;
    QString part;
    bool inPart = false;
    QChar quote;
    for (qsizetype i = 0; i < command.size(); ++i) {
        const QChar c = command.at(i);
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            else if (c == u'\\' && quote == u'"' && i + 1 < command.size())
                part += command.at(++i);
            else
                part += c;
        } else if (c == u'"' || c == u'\'') {
            quote = c;
            inPart = true;
        } else if (c == u'\\' && i + 1 < command.size()) {
            part += command.at(++i);
            inPart = true;
        } else if (c.isSpace()) {
            if (inPart)
                out << std::exchange(part, {});
            inPart = false;
        } else {
            part += c;
            inPart = true;
        }
    }
    if (inPart)
        out << part;
    return out;
}

bool CodeProjects::openInEditor(const QString &folder) const
{
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return false;
    const QStringList parts = splitCommand(m_editor);
#if QT_CONFIG(process)
    if (!parts.isEmpty()) {
        const QString program = QStandardPaths::findExecutable(parts.first());
        return !program.isEmpty() && QProcess::startDetached(program, parts.mid(1) << folder, folder);
    }
#endif
    return openFolder(folder);
}

bool CodeProjects::openFolder(const QString &folder) const
{
    return !folder.isEmpty() && QFileInfo(folder).isDir() && QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
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
        for (const QJsonValue &sv : o.value(u"scripts"_s).toArray()) {
            const QJsonObject so = sv.toObject();
            const Script s{so.value(u"name"_s).toString(), so.value(u"command"_s).toString(),
                           so.value(u"icon"_s).toString()};
            if (!s.command.isEmpty() && p.scripts.size() < MaxScripts)
                p.scripts << s;
        }
        m_projects << p;
        if (m_projects.size() >= MaxProjects)
            break;
    }
    m_worktreesRoot = all.value(u"worktreesRoot"_s).toString();
    m_editor = all.value(u"editor"_s).toString();
    Q_EMIT changed();
}

void CodeProjects::save()
{
    QJsonArray projects;
    for (const Project &p : std::as_const(m_projects)) {
        QJsonArray scripts;
        for (const Script &s : p.scripts)
            scripts << QJsonObject{{u"name"_s, s.name}, {u"command"_s, s.command}, {u"icon"_s, s.icon}};
        QJsonObject o{{u"id"_s, p.id}, {u"name"_s, p.name}, {u"path"_s, p.path}};
        if (!scripts.isEmpty())
            o.insert(u"scripts"_s, scripts);
        projects << o;
    }
    QJsonObject all{{u"projects"_s, projects}};
    if (!m_worktreesRoot.isEmpty())
        all.insert(u"worktreesRoot"_s, m_worktreesRoot);
    if (!m_editor.isEmpty())
        all.insert(u"editor"_s, m_editor);
    if (m_save)
        m_save(QJsonDocument(all).toJson(QJsonDocument::Compact));
    Q_EMIT changed();
}

} // namespace brulion::shell
