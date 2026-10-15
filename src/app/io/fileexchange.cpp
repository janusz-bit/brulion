#include "fileexchange.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QSet>

#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

namespace {

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace

QString fileSystemPathOf(const QUrl &url)
{
    if (url.isLocalFile())
        return QDir::cleanPath(url.toLocalFile());
    if (url.scheme().isEmpty())
        return QDir::cleanPath(url.toString());
    // content://… (Android documents): Qt's file engine opens the URL itself.
    return url.toString();
}

QString displayNameOf(const QUrl &url)
{
    const QString fsPath = fileSystemPathOf(url);
    QString name = QFileInfo(fsPath).fileName();
    if (name.isEmpty() || name.contains(u'%') || name.contains(u':')) {
        // A content URI without a display name: its last decoded segment ("primary:Notatki").
        const QString last = QUrl::fromPercentEncoding(url.path(QUrl::FullyEncoded).section(u'/', -1).toUtf8());
        name = last.section(u':', -1).section(u'/', -1);
    }
    return name.isEmpty() ? url.toString(QUrl::RemoveUserInfo) : name;
}

bool isSkippedFolder(const QString &name)
{
    static const QSet<QString> skipped = {
        u".git"_s,         u".hg"_s,         u".svn"_s,        u"node_modules"_s, u".trash"_s, u".Trash"_s,
        u".stversions"_s,  u".stfolder"_s,   u"__MACOSX"_s,    u".brulion"_s,     u".cache"_s,
    };
    return skipped.contains(name) || name.startsWith(u".Trash-"_s);
}

bool writeToUrl(const QUrl &url, const QByteArray &data, QString *error)
{
    const QString path = fileSystemPathOf(url);
    if (path.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("brulion::ioviews::FileGatherer", "No file was chosen.");
        return false;
    }
    if (url.isLocalFile() || url.scheme().isEmpty()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            if (error)
                *error = file.errorString();
            return false;
        }
        return true;
    }
    // Android documents cannot be replaced by rename: write in place.
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.close();
    return true;
}

// ---- FileGatherer ---------------------------------------------------------------------------------

FileGatherer::FileGatherer(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &FileGatherer::step);
}

void FileGatherer::start(const QList<QUrl> &urls)
{
    cancel();
    m_running = true;
    m_stopped = false;
    m_pending.clear();
    m_next = 0;
    m_files.clear();
    m_warnings.clear();
    m_bytes = 0;
    for (const QUrl &url : urls)
        collect(url);
    if (urls.size() == 1)
        m_sourceName = displayNameOf(urls.first());
    else
        m_sourceName = tr("%n file(s)", "", int(urls.size()));
    Q_EMIT progress(0, int(m_pending.size()));
    m_timer.start();
}

void FileGatherer::cancel()
{
    if (!m_running)
        return;
    m_timer.stop();
    m_running = false;
    m_pending.clear();
    m_files.clear();
    Q_EMIT cancelled();
}

void FileGatherer::collect(const QUrl &url)
{
    const QString root = fileSystemPathOf(url);
    const QFileInfo rootInfo(root);
    if (!rootInfo.exists()) {
        m_warnings.append(tr("“%1” could not be found.").arg(displayNameOf(url)));
        return;
    }
    if (!rootInfo.isDir()) {
        m_pending.append(Pending{ root, displayNameOf(url), rootInfo.size() });
        return;
    }
    // A folder: walk it ourselves so skipped folders are never entered and symlinked folders
    // (loops, duplicates) are not followed.
    struct Dir {
        QString fsPath;
        QString relative;
    };
    // A symlinked file is read only when it stays inside the picked folder: a downloaded "vault"
    // with "Notatka.md -> ~/.ssh/id_rsa" must not copy the key into the notebook (and on to the
    // sync server).
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    const auto insideRoot = [&canonicalRoot](const QFileInfo &entry) {
        const QString target = entry.canonicalFilePath();
        return !canonicalRoot.isEmpty() && !target.isEmpty() && target.startsWith(canonicalRoot + u'/');
    };
    QList<Dir> stack{ Dir{ root, displayNameOf(url) } };
    bool tooMany = false;
    while (!stack.isEmpty() && !tooMany) {
        const Dir dir = stack.takeLast();
        const QFileInfoList entries = QDir(dir.fsPath).entryInfoList(
                QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
        for (const QFileInfo &entry : entries) {
            const QString relative = dir.relative + u'/' + entry.fileName();
            if (entry.isDir()) {
                if (!entry.isSymLink() && !isSkippedFolder(entry.fileName()))
                    stack.append(Dir{ entry.filePath(), relative });
                continue;
            }
            // Pipes, sockets and devices hold no notes (reading a pipe would block forever).
            if (!entry.isFile())
                continue;
            if (entry.isSymLink() && !insideRoot(entry)) {
                m_warnings.append(tr("“%1” is a link to a file outside the chosen folder and was skipped.").arg(relative));
                continue;
            }
            if (m_pending.size() >= m_limits.maxFiles) {
                m_warnings.append(tr("The folder has more than %n file(s); only the first ones were read.", "",
                                     m_limits.maxFiles));
                tooMany = true;
                break;
            }
            m_pending.append(Pending{ entry.filePath(), relative, entry.size() });
        }
    }
}

void FileGatherer::step()
{
    if (!m_running)
        return;
    constexpr qint64 BytesPerTurn = 32LL * 1024 * 1024;
    qint64 turnBytes = 0;
    int read = 0;
    while (m_next < m_pending.size() && read < m_chunk && turnBytes < BytesPerTurn && !m_stopped) {
        const Pending &p = m_pending.at(m_next++);
        ++read;
        if (p.size > m_limits.maxFileBytes) {
            m_warnings.append(tr("“%1” (%2) is too large and was skipped.").arg(p.relative, sizeText(p.size)));
            continue;
        }
        if (m_bytes + p.size > m_limits.maxTotalBytes) {
            m_warnings.append(tr("Only the first %1 were read: the selection is too large to import at once.")
                                      .arg(sizeText(m_limits.maxTotalBytes)));
            m_stopped = true;
            break;
        }
        QFile file(p.fsPath);
        if (!file.open(QIODevice::ReadOnly)) {
            m_warnings.append(tr("“%1” could not be read: %2").arg(p.relative, file.errorString()));
            continue;
        }
        QByteArray data = file.readAll();
        m_bytes += data.size();
        turnBytes += data.size();
        m_files.append(io::InputFile{ p.relative, std::move(data) });
    }
    Q_EMIT progress(int(m_next), int(m_pending.size()));
    if (!m_running)
        return; // cancelled from a progress handler
    if (m_next < m_pending.size() && !m_stopped) {
        m_timer.start();
        return;
    }
    m_running = false;
    const QList<io::InputFile> files = std::exchange(m_files, {});
    m_pending.clear();
    Q_EMIT finished(files, m_warnings, m_sourceName, m_bytes);
}

} // namespace brulion::ioviews
