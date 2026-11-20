#include "encryptedpagehost.h"

#include "workspaceencryption.h"

#include "encryption/meld.h"
#include "markdown/encrypted.h"

#include <QTimer>

namespace brulion::shell {

using encryption::Failure;

EncryptedPageHost::EncryptedPageHost(QObject *parent) : EditorHost(parent) { }

EncryptedPageHost::~EncryptedPageHost() = default;

void EncryptedPageHost::setInner(EditorHost *inner)
{
    if (m_inner == inner)
        return;
    if (m_inner)
        disconnect(m_inner, nullptr, this, nullptr);
    m_inner = inner;
    if (m_inner) {
        // The inner host's news are this host's (the editor listens here).
        connect(m_inner, &EditorHost::fileChanged, this, &EditorHost::fileChanged);
        connect(m_inner, &EditorHost::saveOpenEditorsRequested, this, &EditorHost::saveOpenEditorsRequested);
        connect(m_inner, &EditorHost::editsDiscarded, this, &EditorHost::editsDiscarded);
        connect(m_inner, &EditorHost::indexChanged, this, &EditorHost::indexChanged);
        connect(m_inner, &EditorHost::webPictureChanged, this, &EditorHost::webPictureChanged);
        connect(m_inner, &EditorHost::pdfThumbnailChanged, this, &EditorHost::pdfThumbnailChanged);
        connect(m_inner, &EditorHost::macrosChanged, this, &EditorHost::macrosChanged);
    }
    Q_EMIT innerChanged();
}

void EncryptedPageHost::setEncryption(WorkspaceEncryption *encryption)
{
    if (m_encryption == encryption)
        return;
    if (m_encryption)
        disconnect(m_encryption, nullptr, this, nullptr);
    m_encryption = encryption;
    // A page about to lock: its editor writes what it holds now, while the key still works.
    if (m_encryption)
        connect(m_encryption, &WorkspaceEncryption::pageLocking, this, &EditorHost::saveOpenEditorsRequested);
    Q_EMIT encryptionChanged();
}

bool EncryptedPageHost::encrypted(const QString &path) const
{
    return markdown::isEncryptedNotePath(path);
}

QString EncryptedPageHost::sealed(const QString &path, const QString &content) const
{
    WorkspaceEncryption::PageSession *s = m_encryption ? m_encryption->pageSession(path) : nullptr;
    if (!s || s->foreign)
        return {};
    return encryption::writeNoteFile(content, s->key, s->hint);
}

QString EncryptedPageHost::read(const QString &path, bool *ok)
{
    if (ok)
        *ok = false;
    if (!m_inner)
        return {};
    if (!encrypted(path))
        return m_inner->read(path, ok);
    WorkspaceEncryption::PageSession *s = m_encryption ? m_encryption->pageSession(path) : nullptr;
    if (!s)
        return {}; // locked: nothing to show (PageView shows its lock instead)
    bool exists = false;
    const QString file = m_inner->read(path, &exists);
    if (!exists)
        return {};
    if (ok)
        *ok = true;
    if (file == s->lastFile)
        return s->lastText;
    // Changed elsewhere: with the key (this app's saves keep the salt), else with the password.
    encryption::OpenedNote note = encryption::openNoteWithKey(file, s->key);
    if (note.failure == Failure::WrongPassword)
        note = encryption::openNote(file, s->password);
    if (!note.ok()) {
        // No longer this password's (or damaged): never written over, and the page locks — after
        // this call, which still shows what the editor had.
        s->foreign = true;
        QTimer::singleShot(0, m_encryption.data(), [enc = m_encryption, path] {
            if (enc)
                enc->pageLockedOut(path);
        });
        return s->lastText;
    }
    s->key = note.key;
    s->hint = note.hint;
    s->lastFile = file;
    s->lastText = note.text;
    return s->lastText;
}

bool EncryptedPageHost::write(const QString &path, const QString &content)
{
    if (!m_inner)
        return false;
    if (!encrypted(path))
        return m_inner->write(path, content);
    const QString file = sealed(path, content);
    if (file.isEmpty() || !m_inner->write(path, file))
        return false;
    if (WorkspaceEncryption::PageSession *s = m_encryption ? m_encryption->pageSession(path) : nullptr) {
        s->lastFile = file;
        s->lastText = content;
    }
    return true;
}

EditorHost::WriteResult EncryptedPageHost::writeIfUnchanged(const QString &path, const QString &content,
                                                            const QString &expected)
{
    if (!m_inner)
        return WriteResult::Failed;
    if (!encrypted(path))
        return m_inner->writeIfUnchanged(path, content, expected);
    WorkspaceEncryption::PageSession *s = m_encryption ? m_encryption->pageSession(path) : nullptr;
    if (!s || s->foreign)
        return WriteResult::Failed;
    // The editor expects the Markdown last read or written here: the file then holds lastFile.
    if (expected != s->lastText)
        return WriteResult::Changed;
    const QString file = encryption::writeNoteFile(content, s->key, s->hint);
    const WriteResult result = m_inner->writeIfUnchanged(path, file, s->lastFile);
    if (result == WriteResult::Written) {
        if (WorkspaceEncryption::PageSession *now = m_encryption ? m_encryption->pageSession(path) : nullptr) {
            now->lastFile = file;
            now->lastText = content;
        }
    }
    return result;
}

void EncryptedPageHost::writeIfUnchangedAsync(const QString &path, const QString &content, const QString &expected,
                                              WriteDone done)
{
    if (!m_inner) {
        done(WriteResult::Failed);
        return;
    }
    if (!encrypted(path)) {
        m_inner->writeIfUnchangedAsync(path, content, expected, std::move(done));
        return;
    }
    WorkspaceEncryption::PageSession *s = m_encryption ? m_encryption->pageSession(path) : nullptr;
    if (!s || s->foreign) {
        done(WriteResult::Failed);
        return;
    }
    if (expected != s->lastText) {
        done(WriteResult::Changed);
        return;
    }
    // AES-GCM only (the key is the page's): cheap enough for every autosave on this thread.
    const QString file = encryption::writeNoteFile(content, s->key, s->hint);
    const QString expectedFile = s->lastFile;
    QPointer<WorkspaceEncryption> enc = m_encryption;
    m_inner->writeIfUnchangedAsync(path, file, expectedFile,
                                   [enc, path, file, content, done = std::move(done)](WriteResult result) {
                                       if (result == WriteResult::Written && enc) {
                                           if (WorkspaceEncryption::PageSession *now = enc->pageSession(path)) {
                                               now->lastFile = file;
                                               now->lastText = content;
                                           }
                                       }
                                       done(result);
                                   });
}

void EncryptedPageHost::waitForWrites()
{
    if (m_inner)
        m_inner->waitForWrites();
}

bool EncryptedPageHost::holdsText(const QString &path, const QString &text) const
{
    if (!m_inner)
        return false;
    if (!encrypted(path))
        return m_inner->holdsText(path, text);
    WorkspaceEncryption::PageSession *s = m_encryption ? m_encryption->pageSession(path) : nullptr;
    return s && !s->foreign && text == s->lastText && m_inner->holdsText(path, s->lastFile);
}

void EncryptedPageHost::editorOpened(const QString &path)
{
    if (m_inner)
        m_inner->editorOpened(path);
}

void EncryptedPageHost::editorClosed(const QString &path)
{
    if (m_inner)
        m_inner->editorClosed(path);
}

QString EncryptedPageHost::ensureBlockId(const QString &path, int line)
{
    // (Another page's block: the index never knows an encrypted page's blocks.)
    return m_inner && !encrypted(path) ? m_inner->ensureBlockId(path, line) : QString();
}

// ---- The rest: the inner host's ----------------------------------------------------------------

QVariantList EncryptedPageHost::suggestPages(const QString &prefix, int limit) const
{
    return m_inner ? m_inner->suggestPages(prefix, limit) : QVariantList();
}

QVariantList EncryptedPageHost::suggestBlocks(const QString &text, int limit) const
{
    return m_inner ? m_inner->suggestBlocks(text, limit) : QVariantList();
}

QVariantList EncryptedPageHost::suggestTags(const QString &prefix, int limit) const
{
    return m_inner ? m_inner->suggestTags(prefix, limit) : QVariantList();
}

QVariantList EncryptedPageHost::propertyKeys() const
{
    return m_inner ? m_inner->propertyKeys() : QVariantList();
}

QVariantList EncryptedPageHost::propertyValues(const QString &key, const QString &prefix, int limit) const
{
    return m_inner ? m_inner->propertyValues(key, prefix, limit) : QVariantList();
}

QString EncryptedPageHost::resolveLink(const QString &target, const QString &fromPath) const
{
    return m_inner ? m_inner->resolveLink(target, fromPath) : QString();
}

markdown::PageIcon EncryptedPageHost::pageIcon(const QString &path) const
{
    return m_inner ? m_inner->pageIcon(path) : markdown::PageIcon();
}

QString EncryptedPageHost::linkTarget(const QString &path, const QString &fromPath) const
{
    return m_inner ? m_inner->linkTarget(path, fromPath) : QString();
}

QVariantMap EncryptedPageHost::linkPart(const QString &target, int part, const QString &fromPath) const
{
    return m_inner ? m_inner->linkPart(target, part, fromPath) : QVariantMap();
}

QString EncryptedPageHost::blockText(const QString &blockId) const
{
    return m_inner ? m_inner->blockText(blockId) : QString();
}

bool EncryptedPageHost::blockIdTaken(const QString &blockId) const
{
    return m_inner && m_inner->blockIdTaken(blockId);
}

QString EncryptedPageHost::linkedBlockText(const QString &path, const QString &blockId) const
{
    return m_inner ? m_inner->linkedBlockText(path, blockId) : QString();
}

void EncryptedPageHost::finishIndexing()
{
    if (m_inner)
        m_inner->finishIndexing();
}

QVariantList EncryptedPageHost::templates() const
{
    return m_inner ? m_inner->templates() : QVariantList();
}

const VaultIndex *EncryptedPageHost::taskIndex() const
{
    return m_inner ? m_inner->taskIndex() : nullptr;
}

bool EncryptedPageHost::taskStatusHistory() const
{
    return m_inner && m_inner->taskStatusHistory();
}

QUrl EncryptedPageHost::assetUrl(const QString &path) const
{
    return m_inner ? m_inner->assetUrl(path) : QUrl();
}

QUrl EncryptedPageHost::drawingUrl(const QString &path, bool dark) const
{
    return m_inner ? m_inner->drawingUrl(path, dark) : QUrl();
}

QSize EncryptedPageHost::assetPixelSize(const QString &path) const
{
    return m_inner ? m_inner->assetPixelSize(path) : QSize();
}

bool EncryptedPageHost::canDecodePicture(const QString &path) const
{
    return m_inner && m_inner->canDecodePicture(path);
}

bool EncryptedPageHost::isDrawing(const QString &path) const
{
    return m_inner && m_inner->isDrawing(path);
}

QSize EncryptedPageHost::drawingSize(const QString &path) const
{
    return m_inner ? m_inner->drawingSize(path) : QSize();
}

QString EncryptedPageHost::saveAttachment(const QString &fileName, const QByteArray &data)
{
    return m_inner ? m_inner->saveAttachment(fileName, data) : QString();
}

QString EncryptedPageHost::importAttachment(const QString &fileName, const QByteArray &data)
{
    return m_inner ? m_inner->importAttachment(fileName, data) : QString();
}

QString EncryptedPageHost::importAttachment(const QUrl &file, qint64 maxBytes)
{
    return m_inner ? m_inner->importAttachment(file, maxBytes) : QString();
}

void EncryptedPageHost::discardImport(const QString &path)
{
    if (m_inner)
        m_inner->discardImport(path);
}

QVariantMap EncryptedPageHost::webPicture(const QString &url) const
{
    return m_inner ? m_inner->webPicture(url) : EditorHost::webPicture(url);
}

void EncryptedPageHost::loadWebPicture(const QString &url, bool forDownload)
{
    if (m_inner)
        m_inner->loadWebPicture(url, forDownload);
}

QByteArray EncryptedPageHost::webPictureData(const QString &url, QString *format) const
{
    return m_inner ? m_inner->webPictureData(url, format) : QByteArray();
}

QVariantMap EncryptedPageHost::pdfThumbnail(const QString &path, int page) const
{
    return m_inner ? m_inner->pdfThumbnail(path, page) : EditorHost::pdfThumbnail(path, page);
}

bool EncryptedPageHost::pdfViewable() const
{
    return m_inner && m_inner->pdfViewable();
}

bool EncryptedPageHost::openInOtherApp(const QString &path)
{
    return m_inner && m_inner->openInOtherApp(path);
}

QVariantMap EncryptedPageHost::vaultConfig() const
{
    return m_inner ? m_inner->vaultConfig() : QVariantMap();
}

QString EncryptedPageHost::renderHtml(const QString &markdown, const QString &fromPath) const
{
    return m_inner ? m_inner->renderHtml(markdown, fromPath) : QString();
}

QString EncryptedPageHost::htmlToMarkdown(const QString &html) const
{
    return m_inner ? m_inner->htmlToMarkdown(html) : QString();
}

bool EncryptedPageHost::rendersMacros() const
{
    return m_inner && m_inner->rendersMacros();
}

markdown::MacroView EncryptedPageHost::macroView(const markdown::MacroSpan &macro, const QString &path) const
{
    return m_inner ? m_inner->macroView(macro, path) : EditorHost::macroView(macro, path);
}

QVariantList EncryptedPageHost::suggestMacros(const QString &prefix, int limit) const
{
    return m_inner ? m_inner->suggestMacros(prefix, limit) : QVariantList();
}

QVariantList EncryptedPageHost::embeddableViews() const
{
    return m_inner ? m_inner->embeddableViews() : QVariantList();
}

std::optional<QString> EncryptedPageHost::templateVariable(const QString &extension, const QString &name,
                                                           const QString &arguments, const QVariantMap &context)
{
    return m_inner ? m_inner->templateVariable(extension, name, arguments, context) : std::nullopt;
}

void EncryptedPageHost::navigate(const QVariantMap &route, bool newPane)
{
    if (m_inner)
        m_inner->navigate(route, newPane);
}

void EncryptedPageHost::openUrl(const QUrl &url)
{
    if (m_inner)
        m_inner->openUrl(url);
}

QueryHost *EncryptedPageHost::innerQueries() const
{
    return qobject_cast<QueryHost *>(m_inner.data());
}

QVariantMap EncryptedPageHost::runQuery(const QString &source, const QString &currentPath)
{
    QueryHost *q = innerQueries();
    return q ? q->runQuery(source, currentPath) : QVariantMap();
}

QVariantMap EncryptedPageHost::runDataview(const QString &source, const QString &currentPath)
{
    QueryHost *q = innerQueries();
    return q ? q->runDataview(source, currentPath) : QVariantMap();
}

QVariantMap EncryptedPageHost::runView(const QString &language, const QString &source, const QString &currentPath,
                                       const QVariantMap &state)
{
    QueryHost *q = innerQueries();
    return q ? q->runView(language, source, currentPath, state) : QVariantMap();
}

bool EncryptedPageHost::setTaskStatus(const QString &path, int line, const QString &status)
{
    // (A task of a query's result: another page's, never an encrypted one's — the index holds none.)
    QueryHost *q = innerQueries();
    return q && !encrypted(path) && q->setTaskStatus(path, line, status);
}

} // namespace brulion::shell
