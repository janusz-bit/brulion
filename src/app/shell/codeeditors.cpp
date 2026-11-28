// CodeProjects: "Open in…" (docs/CODE.md "Open in an editor") — the editors of this computer found
// on PATH (VS Code and its kin, Zed, Sublime Text, the JetBrains IDEs, Kate, GNOME's …), a custom
// command of the user's, the file manager; the preferred one remembered (a device setting). A file
// opens at its line where the editor takes one: `--goto file:line` (VS Code, VSCodium, Cursor,
// Windsurf), `file:line` (Zed, Sublime Text), `--line N file` (JetBrains, Kate), `+N file` (gedit).
// Programs start detached with argument lists (no shell).

#include "codeprojects.h"

#include "codefiles/viewer.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::shell {

QList<CodeProjects::KnownEditor> CodeProjects::knownEditors()
{
    // (Linux names first; the Windows and macOS launchers where they differ: findExecutable adds
    // .exe / .cmd on Windows by itself.)
    return {
            {u"code"_s, u"Visual Studio Code"_s, {u"code"_s}},
            {u"code-insiders"_s, u"VS Code Insiders"_s, {u"code-insiders"_s}},
            {u"codium"_s, u"VSCodium"_s, {u"codium"_s, u"vscodium"_s}},
            {u"cursor"_s, u"Cursor"_s, {u"cursor"_s}},
            {u"windsurf"_s, u"Windsurf"_s, {u"windsurf"_s}},
            {u"zed"_s, u"Zed"_s, {u"zed"_s, u"zeditor"_s, u"zed-editor"_s}},
            {u"subl"_s, u"Sublime Text"_s, {u"subl"_s, u"sublime_text"_s}},
            {u"idea"_s, u"IntelliJ IDEA"_s, {u"idea"_s, u"idea-ultimate"_s, u"idea-community"_s, u"idea64"_s}},
            {u"pycharm"_s, u"PyCharm"_s, {u"pycharm"_s, u"pycharm-professional"_s, u"pycharm-community"_s, u"pycharm64"_s}},
            {u"webstorm"_s, u"WebStorm"_s, {u"webstorm"_s, u"webstorm64"_s}},
            {u"clion"_s, u"CLion"_s, {u"clion"_s, u"clion64"_s}},
            {u"goland"_s, u"GoLand"_s, {u"goland"_s, u"goland64"_s}},
            {u"rustrover"_s, u"RustRover"_s, {u"rustrover"_s, u"rustrover64"_s}},
            {u"phpstorm"_s, u"PhpStorm"_s, {u"phpstorm"_s, u"phpstorm64"_s}},
            {u"rider"_s, u"Rider"_s, {u"rider"_s, u"rider64"_s}},
            {u"kate"_s, u"Kate"_s, {u"kate"_s}},
            {u"gnome-text-editor"_s, u"GNOME Text Editor"_s, {u"gnome-text-editor"_s}},
            {u"gedit"_s, u"gedit"_s, {u"gedit"_s}},
            {u"notepad++"_s, u"Notepad++"_s, {u"notepad++"_s}},
    };
}

QStringList CodeProjects::editorArguments(const QString &editor, const QString &path, int line)
{
    const bool atLine = line > 0 && QFileInfo(path).isFile();
    const QString n = QString::number(line);
    if (!atLine)
        return {path};
    static const QStringList gotoStyle{u"code"_s, u"code-insiders"_s, u"codium"_s, u"cursor"_s, u"windsurf"_s};
    static const QStringList colonStyle{u"zed"_s, u"subl"_s};
    static const QStringList lineStyle{u"idea"_s, u"pycharm"_s, u"webstorm"_s, u"clion"_s, u"goland"_s,
                                       u"rustrover"_s, u"phpstorm"_s, u"rider"_s, u"kate"_s};
    if (gotoStyle.contains(editor))
        return {u"--goto"_s, path + u':' + n};
    if (colonStyle.contains(editor))
        return {path + u':' + n};
    if (lineStyle.contains(editor))
        return {u"--line"_s, n, path};
    if (editor == u"gedit")
        return {u'+' + n, path};
    if (editor == u"notepad++")
        return {u"-n"_s + n, path};
    return {path};
}

void CodeProjects::setEditorSearchPaths(const QStringList &paths)
{
    m_editorPaths = paths;
    m_scanned = false;
    Q_EMIT editorsChanged();
}

void CodeProjects::scanEditors() const
{
    if (m_scanned)
        return;
    m_scanned = true;
    m_found.clear();
#if QT_CONFIG(process)
    for (const KnownEditor &e : knownEditors()) {
        for (const QString &program : e.programs) {
            const QString found = QStandardPaths::findExecutable(program, m_editorPaths);
            if (!found.isEmpty()) {
                m_found.append({e.id, found});
                break;
            }
        }
    }
#endif
}

void CodeProjects::rescanEditors()
{
    m_scanned = false;
    Q_EMIT editorsChanged();
}

QVariantList CodeProjects::editors() const
{
    scanEditors();
    QVariantList out;
    const QList<KnownEditor> known = knownEditors();
    for (const auto &[id, program] : m_found) {
        const auto it = std::ranges::find(known, id, &KnownEditor::id);
        out << QVariantMap{{u"id"_s, id}, {u"name"_s, it->name}, {u"icon"_s, u"code"_s}};
    }
    if (!m_editor.isEmpty())
        out << QVariantMap{{u"id"_s, u"custom"_s}, {u"name"_s, m_editor}, {u"icon"_s, u"terminal"_s}};
    out << QVariantMap{{u"id"_s, u"files"_s}, {u"name"_s, tr("File manager")}, {u"icon"_s, u"folder-open"_s}};
    return out;
}

void CodeProjects::setPreferredEditor(const QString &id)
{
    const QString clean = id.trimmed();
    if (clean == m_preferredEditor)
        return;
    m_preferredEditor = clean;
    save();
    Q_EMIT editorsChanged();
}

QString CodeProjects::effectiveEditor() const
{
    scanEditors();
    const auto found = [this](const QString &id) {
        return std::ranges::any_of(m_found, [&id](const auto &e) { return e.first == id; });
    };
    if (m_preferredEditor == u"files" || (m_preferredEditor == u"custom" && !m_editor.isEmpty())
        || (!m_preferredEditor.isEmpty() && found(m_preferredEditor)))
        return m_preferredEditor;
    // (None chosen, or one no longer here: the custom command — the user's own choice before —,
    // else the first editor found, else the file manager.)
    if (!m_editor.isEmpty())
        return u"custom"_s;
    return m_found.isEmpty() ? u"files"_s : m_found.first().first;
}

bool CodeProjects::openInEditor(const QString &path, int line) const
{
    return openWith(effectiveEditor(), path, line);
}

bool CodeProjects::openWith(const QString &editor, const QString &path, int line) const
{
    QString target = path.trimmed();
    if (target.startsWith(u"file:"_s))
        target = QUrl(target).toLocalFile();
    if (target.isEmpty() || !QFileInfo::exists(target))
        return false;
    target = QDir::cleanPath(QFileInfo(target).absoluteFilePath());
    const QString workDir = QFileInfo(target).isDir() ? target : QFileInfo(target).absolutePath();
    if (editor == u"files")
        return openFolder(target);
#if QT_CONFIG(process)
#ifdef Q_OS_WIN
    // (Windows: the path in its native form; a command split by Windows' rules.)
    const QString native = QDir::toNativeSeparators(target);
    const QStringList parts = splitWindowsCommand(m_editor);
#else
    const QString &native = target;
    const QStringList parts = splitCommand(m_editor);
#endif
    if (editor == u"custom") {
        if (parts.isEmpty())
            return false;
        const QString program = QStandardPaths::findExecutable(parts.first(), m_editorPaths);
        return !program.isEmpty() && startEditor(program, parts.mid(1) << native, workDir);
    }
    scanEditors();
    const auto it = std::ranges::find_if(m_found, [&editor](const auto &e) { return e.first == editor; });
    if (it == m_found.cend())
        return false;
    return startEditor(it->second, editorArguments(editor, native, line), workDir);
#else
    Q_UNUSED(line)
    return false;
#endif
}

bool CodeProjects::startEditor(const QString &program, const QStringList &arguments, const QString &workDir)
{
#if QT_CONFIG(process)
#ifdef Q_OS_WIN
    // A batch file (findExecutable() finds `code` as code.cmd): CreateProcess would hand it to
    // cmd.exe with arguments cmd.exe reads by rules of its own; quoted for them here instead.
    const QString suffix = QFileInfo(program).suffix().toLower();
    if (suffix == u"cmd" || suffix == u"bat") {
        const QString line = batchCommandLine(QDir::toNativeSeparators(program), arguments);
        QString cmd = qEnvironmentVariable("ComSpec");
        if (cmd.isEmpty() || !QFileInfo(cmd).isFile())
            cmd = QStandardPaths::findExecutable(u"cmd.exe"_s);
        if (line.isEmpty() || cmd.isEmpty())
            return false;
        QProcess process;
        process.setProgram(cmd);
        process.setNativeArguments(line);
        process.setWorkingDirectory(workDir);
        return process.startDetached();
    }
#endif
    return QProcess::startDetached(program, arguments, workDir);
#else
    Q_UNUSED(program)
    Q_UNUSED(arguments)
    Q_UNUSED(workDir)
    return false;
#endif
}

bool CodeProjects::openFolder(const QString &folder) const
{
    if (folder.isEmpty() || !QFileInfo::exists(folder))
        return false;
    const QString dir = QFileInfo(folder).isDir() ? folder : QFileInfo(folder).absolutePath();
    return QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
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

QStringList CodeProjects::splitWindowsCommand(const QString &command)
{
    QStringList out;
    QString part;
    bool inPart = false;
    bool quoted = false;
    for (qsizetype i = 0; i < command.size();) {
        const QChar c = command.at(i);
        if (c == u'\\') {
            qsizetype backslashes = 0;
            while (i < command.size() && command.at(i) == u'\\') {
                ++backslashes;
                ++i;
            }
            if (i < command.size() && command.at(i) == u'"') {
                // 2n backslashes and a quote: n and the quote's own meaning; 2n + 1: n and a quote.
                part += QString(backslashes / 2, u'\\');
                if (backslashes % 2) {
                    part += u'"';
                    ++i;
                }
            } else {
                part += QString(backslashes, u'\\');
            }
            inPart = true;
        } else if (c == u'"') {
            if (quoted && i + 1 < command.size() && command.at(i + 1) == u'"') {
                part += u'"'; // ("" inside quotes: a quote, as the C runtime reads it)
                i += 2;
            } else {
                quoted = !quoted;
                ++i;
            }
            inPart = true;
        } else if (!quoted && (c == u' ' || c == u'\t')) {
            if (inPart)
                out << std::exchange(part, {});
            inPart = false;
            ++i;
        } else {
            part += c;
            inPart = true;
            ++i;
        }
    }
    if (inPart)
        out << part;
    return out;
}

QString CodeProjects::batchCommandLine(const QString &batch, const QStringList &arguments)
{
    QString line = u"/d /v:off /s /c \""_s;
    const QStringList all = QStringList{batch} + arguments;
    for (qsizetype i = 0; i < all.size(); ++i) {
        QString argument = all.at(i);
        if (argument.contains(u'"') || argument.contains(u'%') || argument.contains(u'\n') || argument.contains(u'\r')
            || argument.contains(QChar(0)))
            return {};
        // (The program the batch file runs reads "C:\" as C: and a quote.)
        qsizetype trailing = 0;
        while (trailing < argument.size() && argument.at(argument.size() - 1 - trailing) == u'\\')
            ++trailing;
        argument += QString(trailing, u'\\');
        if (i > 0)
            line += u' ';
        line += u'"' + argument + u'"';
    }
    return line + u'"';
}

void CodeProjects::dropOnThread(const QString &path, const QString &folder, const QList<QUrl> &files)
{
    QStringList local;
    for (const QUrl &url : files)
        if (url.isLocalFile())
            local << QDir::cleanPath(url.toLocalFile());
    const codefiles::Dropped dropped = codefiles::droppedFiles(folder, local);
    if (path.isEmpty() || (dropped.pictures.isEmpty() && dropped.text.isEmpty()))
        return;
    QList<QUrl> pictures;
    for (const QString &picture : dropped.pictures)
        pictures << QUrl::fromLocalFile(picture);
    m_drops.insert(path, {pictures, dropped.text});
    Q_EMIT threadDropped(path);
}

QList<QUrl> CodeProjects::takeDropPictures(const QString &path)
{
    return m_drops.contains(path) ? std::exchange(m_drops[path].first, {}) : QList<QUrl>();
}

QString CodeProjects::takeDropText(const QString &path) { return m_drops.take(path).second; }

} // namespace brulion::shell
