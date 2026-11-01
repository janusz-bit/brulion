// Workspace — PDFs of the notebook (docs/UI.md "PDF viewer"): the thumbnails of embedded PDFs
// (EditorHost::pdfThumbnail, cached as PNG files by pdfview::PdfThumbnails), "Otwórz w innej
// aplikacji" (platform::openFileExternally: the system's viewer, Android's ACTION_VIEW, the
// browser's new tab), and "Rysuj na tej stronie" (a drawing on a PDF page, embedded right after the
// PDF in the note the viewer was opened from: notebook::insertBlockAfterEmbed).
#include "workspace.h"

#include "openexternal.h"
#include "pdfpages.h"
#include "pdfrenderpool.h"
#include "pdfthumbnails.h"

#include "drawing/backdrop.h"
#include "drawing/drawing.h"
#include "markdown/inline.h"
#include "notebook/embeds.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QScreen>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// The thumbnails' cap: 64 MiB, 32 MiB on a phone.
qint64 thumbnailCacheBytes()
{
#ifdef Q_OS_ANDROID
    return 32LL * 1024 * 1024;
#else
    return pdfview::PdfThumbnails::DefaultCacheBytes;
#endif
}

/// The first bytes of a PDF: "%PDF-" within the first 1024 bytes (Acrobat accepts junk before it).
bool startsLikePdf(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.read(1024).contains("%PDF-");
}

} // namespace

bool Workspace::pdfViewable() const
{
    return platform::pdfAvailable();
}

bool Workspace::pdfSearchable() const
{
    return platform::pdfTextAvailable();
}

bool Workspace::isPdf(const QString &path) const
{
    return paths::isPdfPath(path);
}

QString Workspace::fileVersion(const QString &path) const
{
    if (!m_vault || path.isEmpty())
        return {};
    const std::optional<FileStat> st = m_vault->stat(paths::normalize(path));
    if (!st || st->isDir)
        return {};
    return QString::number(st->mtime) + u'-' + QString::number(st->size);
}

QString Workspace::fileSizeText(const QString &path) const
{
    if (!m_vault || path.isEmpty())
        return {};
    const std::optional<FileStat> st = m_vault->stat(paths::normalize(path));
    if (!st || st->isDir)
        return {};
    return QLocale().formattedDataSize(st->size, 1, QLocale::DataSizeTraditionalFormat);
}

pdfview::PdfThumbnails *Workspace::pdfThumbnails() const
{
    if (m_pdfThumbnails)
        return m_pdfThumbnails;
    auto *self = const_cast<Workspace *>(this);
    QString dir = m_pdfThumbnailDir;
    if (dir.isEmpty()) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        dir = base.isEmpty() ? QString() : QDir(base).filePath(u"pdf-thumbnails"_s);
    }
    m_pdfThumbnails = new pdfview::PdfThumbnails(dir, self);
    m_pdfThumbnails->setCacheBytes(thumbnailCacheBytes());
    const QScreen *screen = QGuiApplication::primaryScreen();
    m_pdfThumbnails->setLongSide(pdfview::PdfThumbnails::longSideForRatio(screen ? screen->devicePixelRatio() : 1));
    connect(m_pdfThumbnails, &pdfview::PdfThumbnails::changed, self, [self](const QString &file) {
        const QString path = self->m_pdfAsked.value(file);
        if (!path.isEmpty())
            Q_EMIT self->pdfThumbnailChanged(path);
    });
    return m_pdfThumbnails;
}

QVariantMap Workspace::pdfThumbnail(const QString &rawPath, int page) const
{
    if (!platform::pdfAvailable())
        return {{u"state"_s, u"unsupported"_s}};
    const QString path = paths::normalize(rawPath);
    const std::optional<FileStat> st = m_vault && !path.isEmpty() ? m_vault->stat(path) : std::nullopt;
    if (!st || st->isDir)
        return {{u"state"_s, u"failed"_s}, {u"error"_s, tr("This PDF cannot be read.")}, {u"locked"_s, false}};
    const QString file = m_vault->absolutePath(path);
    m_pdfAsked.insert(file, path);
    const pdfview::PdfThumbnails::Entry e = pdfThumbnails()->entry(file, st->size, st->mtime, std::max(1, page));
    using State = pdfview::PdfThumbnails::State;
    QVariantMap out{{u"pages"_s, e.pages},
                    {u"pageWidth"_s, e.pagePoints.width()},
                    {u"pageHeight"_s, e.pagePoints.height()},
                    {u"locked"_s, e.error == u"locked"}};
    switch (e.state) {
    case State::Loading: out.insert(u"state"_s, u"loading"_s); break;
    case State::Unsupported: out.insert(u"state"_s, u"unsupported"_s); break;
    case State::Ready:
        out.insert(u"state"_s, u"ready"_s);
        // (Kept in memory only — tests — there is no file to show.)
        out.insert(u"url"_s, e.file.isEmpty() ? QUrl() : QUrl::fromLocalFile(e.file));
        out.insert(u"width"_s, e.pixels.width());
        out.insert(u"height"_s, e.pixels.height());
        break;
    case State::Failed:
        // A cache that cannot be written: the file's chip (a click still opens the viewer).
        out.insert(u"state"_s, e.error == u"cache" ? u"unsupported"_s : u"failed"_s);
        out.insert(u"error"_s, e.error == u"locked"       ? tr("PDF protected with a password")
                               : e.error == u"unreadable" ? tr("This PDF cannot be read.")
                               : e.error == u"nopages"    ? tr("This PDF has no pages.")
                                                          : tr("No preview"));
        break;
    }
    return out;
}

void Workspace::pdfFileChanged(const QString &path, const QString &oldPath)
{
    if (!m_vault)
        return;
    for (const QString &p : {path, oldPath}) {
        if (p.isEmpty() || !paths::isPdfPath(p))
            continue;
        // Its open readers closed (Windows: Syncthing can replace the file); a new mtime is a new
        // thumbnail key, so the next ask draws it again.
        pdfview::PdfReaderPool::instance().forget(m_vault->absolutePath(p));
        if (m_pdfAsked.contains(m_vault->absolutePath(p)))
            QMetaObject::invokeMethod(this, [this, p] { Q_EMIT pdfThumbnailChanged(p); }, Qt::QueuedConnection);
    }
}

void Workspace::releasePdfReaders(const QString &path)
{
    if (!m_vault || path.isEmpty())
        return;
    const QString file = m_vault->absolutePath(path);
    if (paths::isPdfPath(path))
        pdfview::PdfReaderPool::instance().forget(file);
    else if (QFileInfo(file).isDir())
        // A folder may hold open PDFs: every idle reader closed (at most PdfReaderPool::MaxIdle;
        // the next page or thumbnail opens its file again). A reader drawing a page at this moment
        // still holds its file until it is done.
        pdfview::PdfReaderPool::instance().closeIdle();
}

bool Workspace::openInOtherApp(const QString &rawPath)
{
    if (!m_vault || rawPath.isEmpty())
        return false;
    const QString path = paths::normalize(rawPath);
    const QString name = paths::basename(path.isEmpty() ? rawPath : path);
    const QString file = path.isEmpty() ? QString() : m_vault->absolutePath(path);
    // Inside the notebook (normalize() keeps a leading ".." of a path that leaves it), a file, a PDF
    // by its name and by its first bytes.
    const bool inside = !path.isEmpty() && !path.startsWith(u"../"_s) && path != u".."_s && !QDir::isAbsolutePath(path);
    if (!inside || !paths::isPdfPath(path) || !QFileInfo(file).isFile() || !startsLikePdf(file)) {
        Q_EMIT errorOccurred(tr("“%1” is not a PDF file.").arg(name));
        return false;
    }
    QByteArray bytes;
#ifdef Q_OS_WASM
    // The browser's tab gets the bytes (IDBFS is no file its viewer could open).
    if (QFileInfo(file).size() > MaxAttachmentBytes) {
        Q_EMIT errorOccurred(tr("“%1” is too large to open here.").arg(name));
        return false;
    }
    bytes = readAttachment(path);
    if (bytes.isEmpty()) {
        Q_EMIT errorOccurred(tr("Could not open “%1” in another app.").arg(name));
        return false;
    }
#endif
    switch (platform::openFileExternally(file, u"application/pdf"_s, name, bytes)) {
    case platform::OpenResult::Opened: return true;
    case platform::OpenResult::Blocked:
        Q_EMIT noticeOccurred(tr("The browser blocked the new tab: the PDF was downloaded instead."));
        return true;
    case platform::OpenResult::NoApp: Q_EMIT errorOccurred(tr("No app on this device opens PDF files.")); return false;
    case platform::OpenResult::Failed: break;
    }
    Q_EMIT errorOccurred(tr("Could not open “%1” in another app.").arg(name));
    return false;
}

QString Workspace::attachmentPathOf(const QString &target, const QString &fromNote) const
{
    QString path = resolveLink(target, fromNote);
    if (!path.isEmpty())
        return path;
    // As the editor's embeds: a path with folders as written, a bare name in the attachments folder.
    const QString decoded = QUrl::fromPercentEncoding(target.toUtf8());
    if (decoded.contains(u'/'))
        return paths::normalize(decoded);
    QString folder = m_vault ? paths::normalize(m_vault->config().attachmentsFolder) : QString();
    if (folder.isEmpty())
        folder = u"assets"_s;
    return paths::join(folder, decoded);
}

QVariantMap Workspace::addDrawingOnPdfPage(const QString &rawPdfPath, const QVariantMap &prepared,
                                           const QString &rawFromNote, int fromLine)
{
    if (!m_vault)
        return {};
    const QString pdfPath = paths::normalize(rawPdfPath);
    const QByteArray svg = prepared.value(u"svg"_s).toByteArray();
    if (!prepared.value(u"error"_s).toString().isEmpty() || svg.isEmpty() || pdfPath.isEmpty()) {
        Q_EMIT errorOccurred(tr("The drawing could not be made."));
        return {};
    }
    QString name = prepared.value(u"name"_s).toString().trimmed();
    if (name.isEmpty())
        name = QFileInfo(paths::basename(pdfPath)).completeBaseName().trimmed();
    if (name.isEmpty())
        name = tr("Drawing");
    const int page = std::max(0, prepared.value(u"page"_s).toInt());
    const QString drawing = saveAttachment(EditorHost::drawingFileNameOn(name, page) + u".svg"_s, svg);
    if (drawing.isEmpty())
        return {}; // (saveAttachment() said why)
    const QString fromNote = paths::normalize(rawFromNote);
    if (!fromNote.isEmpty() && paths::isMarkdown(fromNote) && m_vault->exists(fromNote)) {
        QString target = linkTarget(drawing, fromNote);
        if (target.isEmpty())
            target = paths::basename(drawing);
        const QString body = u"![["_s + target + u"]]"_s;
        const auto embedsIt = [this, &pdfPath, &fromNote](QStringView t) {
            return attachmentPathOf(t.toString(), fromNote) == pdfPath;
        };
        // The note may change meanwhile (sync, an editor in the side panel): compare and swap.
        for (int attempt = 0; attempt < 3; ++attempt) {
            bool ok = false;
            const QString content = m_vault->readText(fromNote, &ok);
            if (!ok)
                break;
            const notebook::EmbedInsertion insertion =
                    notebook::insertBlockAfterEmbed(content, fromLine, embedsIt, body);
            const WriteResult written = writeIfUnchanged(fromNote, insertion.content, content);
            if (written == WriteResult::Written)
                return {{u"path"_s, drawing},
                        {u"note"_s, fromNote},
                        {u"journal"_s, false},
                        {u"line"_s, insertion.line}};
            if (written == WriteResult::Failed)
                return {{u"path"_s, drawing}, {u"note"_s, QString()}, {u"journal"_s, false}};
        }
    }
    // Opened from the tree, the quick switcher, or the note went: today's journal.
    const QString journal = journalPath(now().date().toString(Qt::ISODate));
    QString target = linkTarget(drawing, journal);
    if (target.isEmpty())
        target = paths::basename(drawing);
    const QString written = captureToJournal(u"![["_s + target + u"]]"_s);
    return {{u"path"_s, drawing}, {u"note"_s, written}, {u"journal"_s, !written.isEmpty()}};
}

QVariantList Workspace::drawingsOnPdfPage(const QString &rawPdfPath, int page, const QString &rawNote) const
{
    QVariantList out;
    if (!m_vault || page < 1)
        return out;
    const QString pdfPath = paths::normalize(rawPdfPath);
    const QString note = paths::normalize(rawNote);
    if (pdfPath.isEmpty() || note.isEmpty() || !m_vault->exists(note))
        return out;
    bool ok = false;
    const QString content = m_vault->readText(note, &ok);
    if (!ok || !content.contains(u"!["))
        return out;
    QStringList seen;
    int read = 0;
    for (const Link &link : markdown::extractInline(content).links) {
        const bool embed = link.kind == LinkKind::Embed
                           || (link.kind == LinkKind::Markdown && link.from >= 0 && link.from < content.size()
                               && content.at(link.from) == u'!');
        if (!embed || !link.target.endsWith(u".svg"_s, Qt::CaseInsensitive))
            continue;
        const QString path = attachmentPathOf(link.target, note);
        if (path.isEmpty() || seen.contains(path) || ++read > 50)
            continue;
        seen.append(path);
        const std::optional<drawing::Drawing> d = drawing::readDrawing(readAttachment(path));
        const std::optional<drawing::BackdropSource> source = d ? drawing::backdropSource(*d) : std::nullopt;
        if (source && source->page == page && paths::normalize(source->path) == pdfPath)
            out.append(
                    QVariantMap{{u"path"_s, path}, {u"name"_s, QFileInfo(paths::basename(path)).completeBaseName()}});
    }
    return out;
}

} // namespace brulion::shell
