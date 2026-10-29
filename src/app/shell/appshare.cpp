// AppController: text and files shared to Brulion from another app ("Udostępnij → Brulion",
// docs/ANDROID.md; the browser version's share target, docs/WEB.md; the --share developer option)
// and files opened with Brulion on a desktop ("Otwórz za pomocą": --open, a second launch's `share`
// message; docs/WINDOWS.md, docs/NIXOS.md). The pure parts — the platform's JSON, the share
// target's query, the opened files, the capture text, page titles and contents, a file's journal
// block, which opened file is a page of the notebook — are notebook/share.h.
#include "appcontroller.h"

#include "log/categories.h"

#include "notebook/pages.h"
#include "notebook/share.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QSet>
#include <QStringList>
#include <QVariantList>

#include <algorithm>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::shell {

using logging::lcApp;

namespace {

/// The first lines of a shared file for the dialog (plain text: shown with Text.PlainText).
QString previewOf(const QString &content)
{
    constexpr int MaxLines = 12;
    constexpr qsizetype MaxChars = 600;
    QStringList lines = QString(content).remove(u'\r').split(u'\n');
    const bool more = lines.size() > MaxLines;
    if (more)
        lines = lines.mid(0, MaxLines);
    QString preview = lines.join(u'\n').trimmed();
    if (preview.size() > MaxChars)
        return preview.left(MaxChars) + u'…';
    return more ? QString(preview + u"\n…"_s) : preview;
}

/// Remove the platform's copies of a share: only a folder inside `inbox` — the share's JSON names
/// it, and nothing else of the app's data (or of the notebook) may ever be removed because of it.
void removeShareCopies(const QString &rawDir, const QString &inbox)
{
    if (rawDir.isEmpty() || !QDir::isAbsolutePath(rawDir))
        return;
    const QString dir = QDir::cleanPath(rawDir);
    if (!dir.startsWith(inbox + u'/'))
        return;
    if (!QDir(dir).removeRecursively())
        qCWarning(lcApp) << "Could not remove the shared files' copies in" << dir;
}

/// How the notebook's folder is compared with an opened file's (Windows' file names ignore letter
/// case).
#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity FileNameCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity FileNameCase = Qt::CaseSensitive;
#endif

/// The page of the open notebook an opened file (absolute, clean) already is, "" when it is none.
/// Tried in turn: both paths with symbolic links resolved (a link to the notebook, a folder linked
/// to another place in it), then the path as given against the notebook's folder as given and
/// resolved — a page in a folder linked from outside (the vault follows such links) or a page that
/// is itself a link resolves to a path outside the notebook. A page the notebook knows wins, spelled
/// as it knows it; else the first one (a file it has not seen yet), only while the file exists.
QString notebookPageOf(const QString &file, const Vault &vault)
{
    const QFileInfo info(file);
    if (!info.isFile())
        return {};
    const QString root = vault.rootPath();
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    const QString canonical = info.canonicalFilePath();
    const QString given = QDir::cleanPath(info.absoluteFilePath());
    const std::pair<QString, QString> tries[] = {{canonical, canonicalRoot}, {given, root}, {given, canonicalRoot}};
    QStringList pages;
    for (const auto &[path, folder] : tries) {
        if (path.isEmpty() || folder.isEmpty())
            continue;
        const QString page = notebook::pageForOpenedFile(path, folder, FileNameCase);
        if (!page.isEmpty() && !pages.contains(page))
            pages.append(page);
    }
    for (const QString &page : std::as_const(pages)) {
        const std::optional<FileStat> known = vault.stat(vault.existingCase(page));
        if (known && !known->isDir)
            return known->path;
    }
    return pages.value(0);
}

} // namespace

QString AppController::shareInboxPath() const
{
    return QDir::cleanPath(m_dataDir + u"/share-in"_s);
}

void AppController::receiveShareJson(const QByteArray &json)
{
    receiveShare(QJsonDocument::fromJson(json).object().toVariantMap());
}

void AppController::receiveShareQuery(const QString &query)
{
    receiveShare(notebook::sharedContentToJson(notebook::sharedContentFromQuery(query)).toVariantMap());
}

void AppController::receiveShare(const QVariantMap &map)
{
    notebook::SharedContent share = notebook::sharedContentFromJson(QJsonObject::fromVariantMap(map));
    if (share.isEmpty()) {
        qCInfo(lcApp) << "Ignoring an empty share";
        removeShareCopies(share.dir, shareInboxPath());
        toast(tr("Nothing to save."), u"info"_s);
        return;
    }
    // Files opened with Brulion join opened files still waiting (Windows starts Brulion once per
    // selected file, each start sends its own); any other newer share replaces the waiting one
    // (its copies go).
    QList<ShareFile> known;
    QSet<QString> reopened; // opened again: read again (changed since, readable now)
    if (m_share && m_share->source == u"open" && share.source == u"open") {
        for (const notebook::SharedFile &file : std::as_const(share.files)) {
            if (!file.path.isEmpty())
                reopened.insert(file.path);
        }
        share = notebook::mergeOpenedShares(*m_share, share);
        known = std::exchange(m_shareFiles, {});
        m_share.reset(); // opened files have no copies to remove
    } else if (m_share) {
        clearShare();
    }

    // Read the files now: on Android the copies were made for this moment, and the dialog shows
    // their titles and first lines.
    QList<ShareFile> files;
    for (const notebook::SharedFile &shared : std::as_const(share.files)) {
        // A file read for the waiting share is not read again, unless it was opened again: it may
        // have changed since (or be readable now), and that open asks for it as it is now.
        const auto read = std::find_if(known.cbegin(), known.cend(), [&](const ShareFile &file) {
            return !shared.path.isEmpty() && shared.error.isEmpty() && file.path == shared.path
                   && !reopened.contains(shared.path);
        });
        if (read != known.cend()) {
            files.append(*read);
            continue;
        }
        ShareFile file;
        file.path = shared.path;
        file.name = shared.name.isEmpty() ? QFileInfo(shared.path).fileName() : shared.name;
        file.size = shared.size;
        file.error = shared.error;
        if (file.error.isEmpty()) {
            const QFileInfo info(shared.path);
            QString mime = shared.mime;
            if (info.exists() && info.isFile()) {
                file.size = info.size();
                // A file opened with Brulion has no type: its name, else its content tells ("README"
                // or "notatka" without an extension is text/plain, a picture is not text).
                if (mime.isEmpty())
                    mime = QMimeDatabase().mimeTypeForFile(info).name();
            }
            notebook::SharedFileProblem problem = notebook::checkSharedFile(file.name, mime, file.size);
            if (problem == notebook::SharedFileProblem::None && !(info.exists() && info.isFile()))
                file.error = u"unreadable"_s;
            else
                file.error = notebook::sharedFileError(problem);
        }
        if (file.error.isEmpty()) {
            QFile f(shared.path);
            if (!f.open(QIODevice::ReadOnly)) {
                file.error = u"unreadable"_s;
            } else {
                const QByteArray bytes = f.read(notebook::SharedFileMaxBytes + 1);
                if (bytes.size() > notebook::SharedFileMaxBytes)
                    file.error = notebook::sharedFileError(notebook::SharedFileProblem::TooLarge);
                else if (notebook::looksBinary(bytes))
                    file.error = notebook::sharedFileError(notebook::SharedFileProblem::Binary);
                else
                    file.content = notebook::decodeSharedBytes(bytes);
            }
        }
        file.title = notebook::sharedPageTitle(file.name, file.content, tr("Shared note"));
        files.append(file);
    }
    m_share = std::move(share);
    m_shareFiles = files;
    Q_EMIT pendingShareChanged();
    showWindow();
    applyPendingShare();
}

void AppController::applyPendingShare()
{
    if (!m_share || m_status == u"boot" || m_status == u"loading")
        return; // finishOpening() comes back here once the notebook is ready
    if (!m_workspace->isOpen() || m_status != u"ready") {
        toast(tr("Open or create a notebook first to save what you shared."), u"warning"_s);
        return; // kept: the dialog opens once a notebook is ready
    }
    if (inDocsVault()) {
        // Shared notes belong in the user's notebook, never in the documentation.
        const QString back = lastUserVault();
        if (!back.isEmpty() && openRecentVault(back))
            return; // shown once that notebook is ready
        toast(tr("Open or create a notebook first to save what you shared."), u"warning"_s);
        return;
    }
    if (m_share->source == u"open") {
        // A file opened with Brulion that already is a page of this notebook opens as that page
        // (also through a link to the notebook, a folder linked into it, a page that is a link:
        // notebookPageOf); only the others are offered as copies.
        const Vault &vault = *m_workspace->vault();
        QString first;
        QList<notebook::SharedFile> keptShared;
        QList<ShareFile> keptFiles;
        for (qsizetype i = 0; i < m_shareFiles.size(); ++i) {
            const ShareFile &file = m_shareFiles.at(i);
            const QString page = file.path.isEmpty() ? QString() : notebookPageOf(file.path, vault);
            if (!page.isEmpty()) {
                if (first.isEmpty())
                    first = page;
                continue;
            }
            keptFiles.append(file);
            if (i < m_share->files.size()) // one entry per file (receiveShare)
                keptShared.append(m_share->files.at(i));
        }
        if (!first.isEmpty()) {
            qCInfo(lcApp) << "An opened file is a page of the notebook: opening it";
            m_shareFiles = keptFiles;
            m_share->files = keptShared;
            if (m_shareFiles.isEmpty())
                m_share.reset(); // nothing else to offer (opened files have no copies)
            Q_EMIT pendingShareChanged();
            openPage(first);
            if (!m_share)
                return;
        }
    }
    requestUiAction(u"share"_s);
}

QVariantMap AppController::pendingShare() const
{
    if (!m_share)
        return {};
    QVariantList files;
    int readable = 0;
    QString title;
    for (const ShareFile &file : m_shareFiles) {
        QString problem;
        if (file.error == u"tooLarge")
            problem = tr("Too large to save (over %1 MB)").arg(notebook::SharedFileMaxBytes / (1024 * 1024));
        else if (file.error == u"notText" || file.error == u"binary")
            problem = tr("Not a text file");
        else if (file.error == u"tooMany")
            problem = tr("Too many files at once (at most %1)").arg(notebook::SharedFilesMax);
        else if (!file.error.isEmpty())
            problem = tr("Could not be read");
        if (problem.isEmpty()) {
            ++readable;
            if (title.isEmpty())
                title = file.title;
        }
        files.append(QVariantMap{{u"name"_s, file.name.isEmpty() ? tr("(no name)") : file.name},
                                 {u"title"_s, file.title},
                                 {u"preview"_s, problem.isEmpty() ? previewOf(file.content) : QString()},
                                 {u"size"_s, file.size},
                                 {u"error"_s, problem}});
    }
    const bool hasFiles = shareSavesFiles();
    const QString text = notebook::composeSharedText(m_share->subject, m_share->text, m_share->url);
    if (!hasFiles) {
        // The subject (a web page's title), else the text's first line; links are no title.
        const QString source = m_share->subject.trimmed().isEmpty() ? m_share->text : m_share->subject;
        title = notebook::sharedPageTitle(QString(), source, tr("Shared note"));
    }
    return {{u"kind"_s, hasFiles ? u"files"_s : u"text"_s},
            {u"text"_s, hasFiles ? QString() : text},
            {u"files"_s, files},
            {u"readable"_s, readable},
            {u"target"_s, hasFiles && readable == 1 ? u"page"_s : u"journal"_s},
            {u"title"_s, title},
            {u"source"_s, m_share->source}};
}

QVariantMap AppController::saveShare(const QVariantMap &choice)
{
    const auto failed = [](const QString &error) { return QVariantMap{{u"ok"_s, false}, {u"error"_s, error}}; };
    if (!m_share)
        return failed(tr("Nothing to save."));
    // Never while a notebook opens (the documentation may still be the open one) or in the
    // documentation.
    if (m_status != u"ready" || !m_workspace->isOpen() || inDocsVault())
        return failed(tr("Open or create a notebook first to save what you shared."));
    QList<const ShareFile *> files;
    for (const ShareFile &file : std::as_const(m_shareFiles)) {
        if (file.error.isEmpty())
            files.append(&file);
    }
    const bool hasFiles = shareSavesFiles();
    if (hasFiles && files.isEmpty())
        return failed(tr("None of the shared files can be saved."));
    const QString target = choice.value(u"target"_s).toString();
    const QString chosenTitle = choice.value(u"title"_s).toString().trimmed();
    const QString text = choice.value(u"text"_s).toString();

    QVariantMap result{{u"ok"_s, true}};
    if (target == u"journal") {
        QString path;
        if (hasFiles) {
            QStringList blocks;
            for (const ShareFile *file : std::as_const(files)) {
                const QString title = files.size() == 1 && !chosenTitle.isEmpty() ? chosenTitle : file->title;
                blocks.append(notebook::sharedFileBlock(title, file->content));
            }
            path = m_workspace->appendJournalBlocks(blocks.join(u'\n'));
            result[u"count"_s] = blocks.size();
        } else {
            if (text.trimmed().isEmpty())
                return failed(tr("Nothing to save."));
            // Never through quick capture's date parsing: "jutro" in a shared title is no reminder.
            path = m_workspace->captureToJournal(text);
            result[u"count"_s] = 1;
        }
        if (path.isEmpty())
            return failed(tr("Could not save to today's journal."));
        result[u"path"_s] = path;
        clearShare();
        openToday();
        toast(tr("Saved in today's journal"), u"success"_s);
        return result;
    }
    if (target != u"page")
        return failed(tr("Nothing to save."));

    // One page per file (or one from the text), in the notebook's folder for new pages.
    struct Wanted {
        QString title;
        QString content;
    };
    QList<Wanted> pages;
    if (hasFiles) {
        for (const ShareFile *file : std::as_const(files))
            pages.append({files.size() == 1 && !chosenTitle.isEmpty() ? chosenTitle : file->title,
                          notebook::sharedPageContent(file->content)});
    } else {
        if (text.trimmed().isEmpty())
            return failed(tr("Nothing to save."));
        pages.append({chosenTitle.isEmpty() ? pendingShare().value(u"title"_s).toString() : chosenTitle,
                      notebook::sharedPageContent(text)});
    }
    Vault &vault = *m_workspace->vault();
    if (pages.size() == 1) {
        // A page whose name differs only in letter case is that page: nothing is written over it.
        const notebook::CreatedPage planned = notebook::plannedPage(vault, pages.first().title);
        if (planned.existing)
            return failed(tr("A page with this name already exists: %1").arg(paths::pageTitle(planned.path)));
    }
    QStringList created;
    QStringList skipped;
    for (const Wanted &page : std::as_const(pages)) {
        const notebook::CreatedPage made =
                notebook::createPage(vault, page.title, QString(), std::nullopt, page.content, ChangeSource::User);
        if (made.existing) {
            skipped.append(page.title);
        } else if (made.path.isEmpty()) {
            if (created.isEmpty())
                return failed(made.problem.kind == notebook::PathProblem::Failed
                                      ? tr("Could not create the page: %1").arg(made.problem.detail)
                                      : tr("Nothing to save."));
            skipped.append(page.title);
        } else {
            created.append(made.path);
        }
    }
    if (created.isEmpty())
        return failed(tr("A page with this name already exists: %1").arg(skipped.join(u", "_s)));
    result[u"path"_s] = created.first();
    result[u"count"_s] = int(created.size());
    result[u"skipped"_s] = skipped;
    clearShare();
    openPage(created.first());
    if (!skipped.isEmpty())
        toast(tr("Saved %n page(s); not saved, a page with this name exists: %1", "", int(created.size()))
                      .arg(skipped.join(u", "_s)),
              u"warning"_s);
    else
        toast(tr("Saved %n page(s)", "", int(created.size())), u"success"_s);
    return result;
}

bool AppController::shareSavesFiles() const
{
    if (!m_share || m_shareFiles.isEmpty())
        return false;
    for (const ShareFile &file : m_shareFiles) {
        if (file.error.isEmpty())
            return true;
    }
    return notebook::composeSharedText(m_share->subject, m_share->text, m_share->url).trimmed().isEmpty();
}

void AppController::dismissShare()
{
    clearShare();
}

void AppController::clearShare()
{
    if (!m_share)
        return;
    removeShareCopies(m_share->dir, shareInboxPath());
    m_share.reset();
    m_shareFiles.clear();
    Q_EMIT pendingShareChanged();
}

} // namespace brulion::shell
