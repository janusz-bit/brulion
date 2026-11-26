// CodeProjects: a project's t3.json (T3 Code's project file, docs/CODE.md "t3.json") — its scripts,
// defaultThreadEnvMode and iconPath, read when the project is added or loaded and again when the
// file changes; never written. What it says is data: its commands run only when the user asks.

#include "codeprojects.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// `text` without control characters (a line break, a tab, an escape a name could hide), its runs
/// of spaces one.
QString cleanLine(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text)
        out += c.category() == QChar::Other_Control || c.category() == QChar::Other_Format ? u' ' : c;
    return out.simplified();
}

} // namespace

CodeProjects::Config CodeProjects::parseConfig(const QByteArray &data, const QString &folder)
{
    Config config;
    config.found = true;
    if (data.size() > MaxConfigBytes) {
        config.error = tr("t3.json is larger than %1 KB: it was not read.").arg(MaxConfigBytes / 1024);
        return config;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        config.error = tr("t3.json is not valid JSON: %1 (at character %2).")
                               .arg(parseError.errorString())
                               .arg(parseError.offset);
        return config;
    }
    if (!doc.isObject()) {
        config.error = tr("t3.json is not a JSON object.");
        return config;
    }
    const QJsonObject o = doc.object();

    const QString mode = o.value(u"defaultThreadEnvMode"_s).toString();
    if (mode == u"local" || mode == u"worktree")
        config.defaultThreadEnvMode = mode;
    else if (o.contains(u"defaultThreadEnvMode"_s))
        config.warnings << tr("defaultThreadEnvMode is neither “local” nor “worktree”: left out.");

    // The icon: a picture inside the project (no absolute path, nothing that leaves it — a link
    // pointing out of it neither).
    if (const QJsonValue icon = o.value(u"iconPath"_s); icon.isString() && !icon.toString().trimmed().isEmpty()) {
        const QString relative = QDir::cleanPath(icon.toString().trimmed());
        const QString suffix = QFileInfo(relative).suffix().toLower();
        const QStringList pictures{u"png"_s, u"svg"_s, u"jpg"_s, u"jpeg"_s, u"webp"_s, u"ico"_s, u"gif"_s};
        const QString root = QFileInfo(folder).canonicalFilePath();
        const QString full = QDir(folder).filePath(relative);
        const QString real = QFileInfo(full).canonicalFilePath();
        if (QDir::isAbsolutePath(relative) || relative.startsWith(u".."_s) || !pictures.contains(suffix))
            config.warnings << tr("iconPath must be a picture inside the project: left out.");
        else if (root.isEmpty() || real.isEmpty() || !QFileInfo(real).isFile())
            config.warnings << tr("iconPath names no file of the project: left out.");
        else if (real != root && !real.startsWith(root + u'/'))
            config.warnings << tr("iconPath must be a picture inside the project: left out.");
        else
            config.iconPath = real;
    } else if (o.contains(u"iconPath"_s) && !icon.isString()) {
        config.warnings << tr("iconPath is not text: left out.");
    }

    const QJsonValue scripts = o.value(u"scripts"_s);
    if (!scripts.isUndefined() && !scripts.isArray())
        config.warnings << tr("scripts is not a list: left out.");
    const QStringList icons{u"play"_s, u"test"_s, u"lint"_s, u"configure"_s, u"build"_s, u"debug"_s};
    QStringList names;
    int n = 0;
    for (const QJsonValue &v : scripts.toArray()) {
        ++n;
        if (!v.isObject()) {
            config.warnings << tr("Script %1 is not an object: left out.").arg(n);
            continue;
        }
        const QJsonObject so = v.toObject();
        const QJsonValue name = so.value(u"name"_s), command = so.value(u"command"_s);
        if (!name.isString() || !command.isString()) {
            config.warnings << tr("Script %1 has no name or command as text: left out.").arg(n);
            continue;
        }
        Script s;
        s.name = cleanLine(name.toString()).left(60);
        // (The command as written: it runs through the user's shell when asked, line breaks and all.)
        s.command = command.toString().trimmed();
        if (s.name.isEmpty() || s.command.isEmpty()) {
            config.warnings << tr("Script %1 has an empty name or command: left out.").arg(n);
            continue;
        }
        if (s.command.size() > 4000) {
            config.warnings << tr("The command of “%1” is longer than 4000 characters: left out.").arg(s.name);
            continue;
        }
        if (names.contains(s.name.toLower())) {
            config.warnings << tr("A second script named “%1”: left out.").arg(s.name);
            continue;
        }
        if (config.scripts.size() >= MaxScripts) {
            config.warnings << tr("More than %1 scripts: the others are left out.").arg(MaxScripts);
            break;
        }
        const QJsonValue icon = so.value(u"icon"_s);
        s.icon = icon.isString() && icons.contains(icon.toString()) ? icon.toString() : u"play"_s;
        // (A value that is not a boolean is its default: false, and async true — no waiting.)
        s.onWorktree = so.value(u"runOnWorktreeCreate"_s).toBool(false);
        s.wait = s.onWorktree && so.value(u"async"_s).isBool() && !so.value(u"async"_s).toBool();
        names << s.name.toLower();
        config.scripts << s;
    }
    return config;
}

void CodeProjects::readConfig(const QString &id)
{
    const QString folder = folderOf(id);
    if (folder.isEmpty()) {
        m_configs.remove(id);
        return;
    }
    QFile file(QDir(folder).filePath(u"t3.json"_s));
    if (!QFileInfo(file.fileName()).isFile() || !file.open(QIODevice::ReadOnly)) {
        m_configs.insert(id, Config{});
        return;
    }
    // (One byte more than allowed tells it is too large without reading all of a huge file.)
    m_configs.insert(id, parseConfig(file.read(MaxConfigBytes + 1), folder));
}

void CodeProjects::reloadConfig(const QString &id)
{
    const Config before = m_configs.value(id);
    readConfig(id);
    if (!(m_configs.value(id) == before))
        Q_EMIT changed();
}

void CodeProjects::watchConfigs()
{
    // The projects' folders (a t3.json made or deleted) and their t3.json files (one changed).
    QStringList wanted;
    for (const Project &p : std::as_const(m_projects)) {
        if (!QFileInfo(p.path).isDir())
            continue;
        wanted << QDir::cleanPath(p.path);
        const QString file = QDir(p.path).filePath(u"t3.json"_s);
        if (QFileInfo(file).isFile())
            wanted << file;
    }
    if (!m_watcher) {
        if (wanted.isEmpty())
            return;
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &CodeProjects::configChanged);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &CodeProjects::configChanged);
    }
    const QStringList now = m_watcher->files() + m_watcher->directories();
    QStringList gone, added;
    for (const QString &path : now)
        if (!wanted.contains(path))
            gone << path;
    for (const QString &path : std::as_const(wanted))
        if (!now.contains(path))
            added << path;
    if (!gone.isEmpty())
        m_watcher->removePaths(gone);
    if (!added.isEmpty())
        m_watcher->addPaths(added);
}

void CodeProjects::configChanged(const QString &path)
{
    // A t3.json changed, or a project's folder did (any file in it: only one that made or deleted
    // its t3.json matters).
    const bool isFile = path.endsWith(u"/t3.json"_s);
    const QString folder = QDir::cleanPath(isFile ? QFileInfo(path).absolutePath() : path);
    for (const Project &p : std::as_const(m_projects)) {
        if (QDir::cleanPath(p.path) != folder)
            continue;
        if (!isFile && QFileInfo(QDir(folder).filePath(u"t3.json"_s)).isFile() == m_configs.value(p.id).found)
            continue;
        if (!m_configDue.contains(p.id))
            m_configDue << p.id;
        m_configTimer.start();
    }
}

} // namespace brulion::shell
