// EncryptedPageHost — the EditorHost of PageView (docs/UI.md "Encrypted notes"): every call goes to
// the `inner` host (the shell's Workspace) unchanged, except for an encrypted page (`.mdenc`) that
// WorkspaceEncryption has unlocked — its file is read decrypted with the page's key and every save
// is encrypted again (Meld Encrypt's v3 JSON, the same salt, a fresh IV), so the block editor shows
// and edits the Markdown while the disk, the history, the index and the sync only ever see the
// ciphertext. A page that is not unlocked reads as missing and refuses every write.
//
// The editor's compare-and-swap stays exact: the host keeps the last file text it read or wrote
// with the Markdown it holds (WorkspaceEncryption::PageSession), and compares the file, never the
// Markdown. A file changed elsewhere is read again with the key (another salt: with the password);
// one that no longer opens with them (a new password set on another device) locks the page and is
// never written over.
//
// The wrapper is permanent (PageView's controller never switches hosts): a page of any kind passes
// through it unchanged, so a route that goes from an encrypted page to another one never saves the
// one through the other's host.
#pragma once

#include "editorhost.h"
#include "queryhost.h"

#include <QPointer>

namespace brulion::shell {

class WorkspaceEncryption;

class EncryptedPageHost : public EditorHost, public QueryHost {
    Q_OBJECT
    Q_INTERFACES(brulion::QueryHost)
    /// The host everything goes to (the shell's Workspace).
    Q_PROPERTY(brulion::EditorHost *inner READ inner WRITE setInner NOTIFY innerChanged FINAL)
    /// The keys of the unlocked pages (Workspace.encryption).
    Q_PROPERTY(brulion::shell::WorkspaceEncryption *encryption READ encryption WRITE setEncryption NOTIFY
                       encryptionChanged FINAL)

public:
    explicit EncryptedPageHost(QObject *parent = nullptr);
    ~EncryptedPageHost() override;

    EditorHost *inner() const { return m_inner; }
    void setInner(EditorHost *inner);
    WorkspaceEncryption *encryption() const { return m_encryption; }
    void setEncryption(WorkspaceEncryption *encryption);

    // --- Files: decrypted / encrypted for unlocked encrypted pages ---------------------------------
    QString read(const QString &path, bool *ok = nullptr) override;
    bool write(const QString &path, const QString &content) override;
    WriteResult writeIfUnchanged(const QString &path, const QString &content, const QString &expected) override;
    void writeIfUnchangedAsync(const QString &path, const QString &content, const QString &expected,
                               WriteDone done) override;
    void waitForWrites() override;
    bool holdsText(const QString &path, const QString &text) const override;
    void editorOpened(const QString &path) override;
    void editorClosed(const QString &path) override;
    QString ensureBlockId(const QString &path, int line) override;

    // --- Everything else: the inner host's ---------------------------------------------------------
    QVariantList suggestPages(const QString &prefix, int limit = 20) const override;
    QVariantList suggestBlocks(const QString &text, int limit = 20) const override;
    QVariantList suggestTags(const QString &prefix, int limit = 20) const override;
    QVariantList propertyKeys() const override;
    QVariantList propertyValues(const QString &key, const QString &prefix, int limit = 20) const override;
    QString resolveLink(const QString &target, const QString &fromPath) const override;
    markdown::PageIcon pageIcon(const QString &path) const override;
    QString linkTarget(const QString &path, const QString &fromPath) const override;
    QVariantMap linkPart(const QString &target, int part, const QString &fromPath) const override;
    QString blockText(const QString &blockId) const override;
    bool blockIdTaken(const QString &blockId) const override;
    QString linkedBlockText(const QString &path, const QString &blockId) const override;
    void finishIndexing() override;
    QVariantList templates() const override;
    const VaultIndex *taskIndex() const override;
    bool taskStatusHistory() const override;
    QUrl assetUrl(const QString &path) const override;
    QUrl drawingUrl(const QString &path, bool dark) const override;
    QSize assetPixelSize(const QString &path) const override;
    bool canDecodePicture(const QString &path) const override;
    bool isDrawing(const QString &path) const override;
    QSize drawingSize(const QString &path) const override;
    QString saveAttachment(const QString &fileName, const QByteArray &data) override;
    QString importAttachment(const QString &fileName, const QByteArray &data) override;
    QString importAttachment(const QUrl &file, qint64 maxBytes) override;
    void discardImport(const QString &path) override;
    QVariantMap webPicture(const QString &url) const override;
    void loadWebPicture(const QString &url, bool forDownload = false) override;
    QByteArray webPictureData(const QString &url, QString *format) const override;
    QVariantMap pdfThumbnail(const QString &path, int page) const override;
    bool pdfViewable() const override;
    bool openInOtherApp(const QString &path) override;
    QVariantMap vaultConfig() const override;
    QString renderHtml(const QString &markdown, const QString &fromPath) const override;
    QString htmlToMarkdown(const QString &html) const override;
    bool rendersMacros() const override;
    markdown::MacroView macroView(const markdown::MacroSpan &macro, const QString &path) const override;
    QVariantList suggestMacros(const QString &prefix, int limit = 20) const override;
    QVariantList embeddableViews() const override;
    const QList<editor::InputRule> &inputRules() const override;
    const QHash<QString, editor::BlockTagStyle> &tagStyles() const override;
    std::optional<QString> templateVariable(const QString &extension, const QString &name, const QString &arguments,
                                            const QVariantMap &context) override;
    void navigate(const QVariantMap &route, bool newPane = false) override;
    void openUrl(const QUrl &url) override;

    // --- QueryHost (the inner host's, also for QML's `typeof host.runQuery`) -----------------------
    Q_INVOKABLE QVariantMap runQuery(const QString &source, const QString &currentPath) override;
    Q_INVOKABLE QVariantMap runDataview(const QString &source, const QString &currentPath) override;
    Q_INVOKABLE QVariantMap runView(const QString &language, const QString &source, const QString &currentPath,
                                    const QVariantMap &state) override;
    Q_INVOKABLE bool setTaskStatus(const QString &path, int line, const QString &status) override;

Q_SIGNALS:
    void innerChanged();
    void encryptionChanged();

private:
    /// Is `path` an encrypted page this host encrypts (unlocked or not)?
    bool encrypted(const QString &path) const;
    /// The encrypted file text of `content` for the unlocked page `path` ("" when locked).
    QString sealed(const QString &path, const QString &content) const;
    QueryHost *innerQueries() const;

    QPointer<EditorHost> m_inner;
    QPointer<WorkspaceEncryption> m_encryption;
};

} // namespace brulion::shell
