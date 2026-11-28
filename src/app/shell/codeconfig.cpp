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
#include <QUrl>

#include <optional>

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

/// t3.json's defaultThreadEnvMode: "local" or "worktree", anything else left out with a warning.
void readEnvMode(const QJsonObject &o, CodeProjects::Config &config)
{
    const QString mode = o.value(u"defaultThreadEnvMode"_s).toString();
    if (mode == u"local" || mode == u"worktree")
        config.defaultThreadEnvMode = mode;
    else if (o.contains(u"defaultThreadEnvMode"_s))
        config.warnings << CodeProjects::tr("defaultThreadEnvMode is neither “local” nor “worktree”: left out.");
}

/// t3.json's worktreeSubmodules: "recursive", "top-level" or "none", anything else left out with a warning.
void readSubmodules(const QJsonObject &o, CodeProjects::Config &config)
{
    const QString mode = o.value(u"worktreeSubmodules"_s).toString();
    if (mode == u"recursive" || mode == u"top-level" || mode == u"none")
        config.worktreeSubmodules = mode;
    else if (o.contains(u"worktreeSubmodules"_s))
        config.warnings << CodeProjects::tr("worktreeSubmodules is neither “recursive”, “top-level” nor “none”: left out.");
}

/// t3.json's worktreeSparsePatternsPath: the patterns of a file inside the project (at most 64 KB; its
/// non-empty lines that are no comment), else none with a warning.
void readSparsePatterns(const QJsonObject &o, const QString &folder, CodeProjects::Config &config)
{
    const QString relative = QDir::cleanPath(o.value(u"worktreeSparsePatternsPath"_s).toString().trimmed());
    if (!o.contains(u"worktreeSparsePatternsPath"_s))
        return;
    const QString root = QFileInfo(folder).canonicalFilePath();
    const QString real = QFileInfo(QDir(folder).filePath(relative)).canonicalFilePath();
    QFile file(real);
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.startsWith(u".."_s) || real.isEmpty()
        || !real.startsWith(root + u'/') || file.size() > 64 * 1024 || !file.open(QIODevice::ReadOnly)) {
        config.warnings << CodeProjects::tr("worktreeSparsePatternsPath names no file of the project: no sparse checkout.");
        return;
    }
    for (const QString &line : QString::fromUtf8(file.readAll()).split(u'\n')) {
        const QString pattern = line.trimmed();
        if (!pattern.isEmpty() && !pattern.startsWith(u'#'))
            config.sparsePatterns << pattern;
    }
}

/// t3.json's iconPath: a picture inside the project `folder` (no absolute path, nothing that leaves
/// it — a link pointing out of it neither).
void readIconPath(const QJsonObject &o, const QString &folder, CodeProjects::Config &config)
{
    const QJsonValue icon = o.value(u"iconPath"_s);
    if (!icon.isString() || icon.toString().trimmed().isEmpty()) {
        if (o.contains(u"iconPath"_s) && !icon.isString())
            config.warnings << CodeProjects::tr("iconPath is not text: left out.");
        return;
    }
    const QString relative = QDir::cleanPath(icon.toString().trimmed());
    const QString suffix = QFileInfo(relative).suffix().toLower();
    static const QStringList pictures{u"png"_s, u"svg"_s, u"jpg"_s, u"jpeg"_s, u"webp"_s, u"ico"_s, u"gif"_s};
    const QString root = QFileInfo(folder).canonicalFilePath();
    const QString real = QFileInfo(QDir(folder).filePath(relative)).canonicalFilePath();
    if (QDir::isAbsolutePath(relative) || relative.startsWith(u".."_s) || !pictures.contains(suffix))
        config.warnings << CodeProjects::tr("iconPath must be a picture inside the project: left out.");
    else if (root.isEmpty() || real.isEmpty() || !QFileInfo(real).isFile())
        config.warnings << CodeProjects::tr("iconPath names no file of the project: left out.");
    else if (real != root && !real.startsWith(root + u'/'))
        config.warnings << CodeProjects::tr("iconPath must be a picture inside the project: left out.");
    else
        config.iconPath = real;
}

/// A t3.json script's options beyond its name and command (anything that is not a boolean: its
/// default — false, async true).
void readScriptOptions(const QJsonObject &so, CodeProjects::Script &s, QStringList &warnings)
{
    static const QStringList icons{u"play"_s, u"test"_s, u"lint"_s, u"configure"_s, u"build"_s, u"debug"_s};
    const QJsonValue icon = so.value(u"icon"_s);
    s.icon = icon.isString() && icons.contains(icon.toString()) ? icon.toString() : u"play"_s;
    s.onWorktree = so.value(u"runOnWorktreeCreate"_s).toBool(false);
    s.wait = s.onWorktree && so.value(u"async"_s).isBool() && !so.value(u"async"_s).toBool();
    s.stopOnSettle = so.value(u"stopOnSettle"_s).toBool(false);
    s.runOnSettle = so.value(u"runOnSettle"_s).toBool(false);
    const QJsonValue preview = so.value(u"previewUrl"_s);
    s.previewUrl = CodeProjects::previewUrlOf(preview.toString());
    if (!preview.isUndefined() && s.previewUrl.isEmpty())
        warnings << CodeProjects::tr("The previewUrl of “%1” is no http or https address: left out.").arg(s.name);
    s.autoOpenPreview = !s.previewUrl.isEmpty() && so.value(u"autoOpenPreview"_s).toBool(false);
}

/// Script `n` of t3.json's `scripts` (none, with a warning, when it is left out: not an object, no
/// name or command as text, an empty one, a command too long, a name of `names` already).
std::optional<CodeProjects::Script> parseScript(const QJsonValue &v, int n, const QStringList &names,
                                                QStringList &warnings)
{
    if (!v.isObject()) {
        warnings << CodeProjects::tr("Script %1 is not an object: left out.").arg(n);
        return std::nullopt;
    }
    const QJsonObject so = v.toObject();
    const QJsonValue name = so.value(u"name"_s), command = so.value(u"command"_s);
    if (!name.isString() || !command.isString()) {
        warnings << CodeProjects::tr("Script %1 has no name or command as text: left out.").arg(n);
        return std::nullopt;
    }
    CodeProjects::Script s;
    s.name = cleanLine(name.toString()).left(60);
    // (The command as written: it runs through the user's shell when asked, line breaks and all.)
    s.command = command.toString().trimmed();
    if (s.name.isEmpty() || s.command.isEmpty()) {
        warnings << CodeProjects::tr("Script %1 has an empty name or command: left out.").arg(n);
        return std::nullopt;
    }
    if (s.command.size() > 4000) {
        warnings << CodeProjects::tr("The command of “%1” is longer than 4000 characters: left out.").arg(s.name);
        return std::nullopt;
    }
    if (names.contains(s.name.toLower())) {
        warnings << CodeProjects::tr("A second script named “%1”: left out.").arg(s.name);
        return std::nullopt;
    }
    readScriptOptions(so, s, warnings);
    return s;
}

} // namespace

QString CodeProjects::previewUrlOf(const QString &text)
{
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty() || (scheme != u"http" && scheme != u"https"))
        return {};
    return url.toString();
}

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

    readEnvMode(o, config);
    readSubmodules(o, config);
    readSparsePatterns(o, folder, config);
    readIconPath(o, folder, config);

    const QJsonValue scripts = o.value(u"scripts"_s);
    if (!scripts.isUndefined() && !scripts.isArray())
        config.warnings << tr("scripts is not a list: left out.");
    QStringList names;
    int n = 0;
    for (const QJsonValue &v : scripts.toArray()) {
        const std::optional<Script> s = parseScript(v, ++n, names, config.warnings);
        if (!s)
            continue;
        if (config.scripts.size() >= MaxScripts) {
            config.warnings << tr("More than %1 scripts: the others are left out.").arg(MaxScripts);
            break;
        }
        names << s->name.toLower();
        config.scripts << *s;
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
