// FakeEditorHost — an in-memory EditorHost for the editor playground (`brulion --editor`) and
// the editor tests: files in a hash, suggestions/link resolution/block ids computed by parsing
// those files with the markdown module (simple, not the real index; link targets come from the
// real index, as the app's), assets as generated
// images, navigation recorded and announced by signals. It is also a QueryHost: ```query blocks
// run on the real index module (a VaultIndex rebuilt from the files after changes), so live
// results and their checkboxes work as in the app. Writes, external changes and task edits
// emit indexChanged() once per event-loop turn, like the shell's Workspace.
// Its vault configuration has the app's defaults except the note modes: `pageMode` is "outline"
// here (the editor's tests and the playground's sample pages were written for the outliner;
// the document mode is tried on pages that name it, see documentPath, or with pageMode set).
//
//   FakeEditorHost { id: host; Component.onCompleted: populateSample() }
//   PageController { host: host; path: host.samplePath }
#pragma once

#include "editorhost.h"
#include "queryhost.h"

#include <QHash>
#include <QStringList>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

namespace brulion {
class VaultIndex;
}

namespace brulion::editor {

class FakeEditorHost : public EditorHost, public QueryHost {
    Q_OBJECT
    Q_INTERFACES(brulion::QueryHost)
    /// Number of write() calls (tests).
    Q_PROPERTY(int writeCount READ writeCount NOTIFY written FINAL)
    /// The rich Polish sample page created by populateSample().
    Q_PROPERTY(QString samplePath READ samplePath CONSTANT FINAL)
    /// The sample recipe (page properties, then "# Title"), see recipeNote().
    Q_PROPERTY(QString recipePath READ recipePath CONSTANT FINAL)
    /// Last route passed to navigate().
    Q_PROPERTY(QVariantMap lastRoute READ lastRoute NOTIFY navigated FINAL)
    /// Fixed clock "YYYY-MM-DD HH:mm" for queries (`today`, `overdue`) and completed tasks;
    /// empty = the real clock.
    Q_PROPERTY(QString now READ now WRITE setNow NOTIFY nowChanged FINAL)
    /// Number of setTaskStatus() calls that changed a file (tests).
    Q_PROPERTY(int taskEdits READ taskEdits NOTIFY taskEdited FINAL)
    /// A sample document (`brulion: document`) for the document mode, see documentNote().
    Q_PROPERTY(QString documentPath READ documentPath CONSTANT FINAL)
    /// vaultConfig()'s note modes, "outline" | "document" (default: both "outline").
    Q_PROPERTY(QString pageMode READ pageMode WRITE setPageMode NOTIFY configChanged FINAL)
    Q_PROPERTY(QString journalMode READ journalMode WRITE setJournalMode NOTIFY configChanged FINAL)

public:
    explicit FakeEditorHost(QObject *parent = nullptr);
    ~FakeEditorHost() override;

    // EditorHost
    QString read(const QString &path, bool *ok = nullptr) override;
    bool write(const QString &path, const QString &content) override;
    /// Compare-and-swap against the in-memory file (setFile() changes it without a signal: another
    /// program's write the editor has not heard of yet); counts the refusals (casRefusals).
    WriteResult writeIfUnchanged(const QString &path, const QString &content, const QString &expected) override;
    /// Number of writeIfUnchanged() calls refused because the file held other text (tests).
    int casRefusals() const { return m_casRefusals; }
    QVariantList suggestPages(const QString &prefix, int limit = 20) const override;
    QVariantList suggestBlocks(const QString &text, int limit = 20) const override;
    QVariantList suggestTags(const QString &prefix, int limit = 20) const override;
    QString resolveLink(const QString &target, const QString &fromPath) const override;
    /// The real index's shortest target (VaultIndex::linkTarget(), as the shell's Workspace).
    QString linkTarget(const QString &path, const QString &fromPath) const override;
    QString blockText(const QString &blockId) const override;
    bool blockIdTaken(const QString &blockId) const override;
    QString linkedBlockText(const QString &path, const QString &blockId) const override;
    QString ensureBlockId(const QString &path, int line) override;
    QVariantList templates() const override;
    QUrl assetUrl(const QString &path) const override;
    bool canDecodePicture(const QString &path) const override;
    QString saveAttachment(const QString &fileName, const QByteArray &data) override;
    /// Pictures from the internet from an in-memory "internet" (setWebPicture()): fetched on the
    /// event loop, in the "always" setting (setWebPicturesShown()) by asking.
    QVariantMap webPicture(const QString &url) const override;
    void loadWebPicture(const QString &url, bool forDownload = false) override;
    QByteArray webPictureData(const QString &url, QString *format) const override;
    QVariantMap vaultConfig() const override;
    void navigate(const QVariantMap &route, bool newPane = false) override;
    void openUrl(const QUrl &url) override;
    /// The clipboard's HTML for tests: the Markdown escaped in a <pre> (the real app renders it).
    QString renderHtml(const QString &markdown, const QString &fromPath) const override;
    /// Pasted HTML as Markdown through QTextDocument (the real app has the io module's converter).
    QString htmlToMarkdown(const QString &html) const override;

    // QueryHost (also invokable, like the shell's Workspace)
    Q_INVOKABLE QVariantMap runQuery(const QString &source, const QString &currentPath) override;
    Q_INVOKABLE bool setTaskStatus(const QString &path, int line, const QString &status) override;
    /// Make runQuery() fail like a host without an index (tests).
    Q_INVOKABLE void setQueriesFail(bool fail) { m_queriesFail = fail; }
    /// Picture formats ("WEBP") canDecodePicture() says no to whatever Qt can decode, like a
    /// device without Qt's image plugins (tests).
    Q_INVOKABLE void setUndecodablePictureFormats(const QStringList &formats) { m_undecodable = formats; }

    /// What the "internet" answers for `url`: the bytes of a picture, or — empty — a failure.
    Q_INVOKABLE void setWebPicture(const QString &url, const QByteArray &data);
    /// The same with a picture given as text (an SVG, from QML).
    Q_INVOKABLE void setWebPictureText(const QString &url, const QString &text) { setWebPicture(url, text.toUtf8()); }
    /// Pictures from the internet are shown without asking (the app's setting); emits
    /// webPictureChanged("").
    Q_INVOKABLE void setWebPicturesShown(bool shown);
    /// Fetches made so far (tests: nothing loads without being allowed).
    Q_INVOKABLE int webFetchCount() const { return m_webFetches; }
    int webFetches() const { return m_webFetches; }

    QString now() const { return m_now; }
    void setNow(const QString &now);
    QString pageMode() const { return m_pageMode; }
    void setPageMode(const QString &mode);
    QString journalMode() const { return m_journalMode; }
    void setJournalMode(const QString &mode);
    int taskEdits() const { return m_taskEdits; }
    /// Number of runQuery() calls (tests).
    int queryRuns() const { return m_queryRuns; }

    /// Put a file without any signal (setting up a test or the playground).
    Q_INVOKABLE void setFile(const QString &path, const QString &content);
    /// Put an attachment given as text (an SVG picture in the playground's self-test).
    Q_INVOKABLE void setTextAsset(const QString &path, const QString &content);
    /// Replace an attachment given as text in its file (the same URL, like a picture replaced in
    /// the vault: Qt's pixmap cache may still have the old one); emits indexChanged.
    Q_INVOKABLE void overwriteTextAsset(const QString &path, const QString &content);
    /// Change a file "outside the editor": stores it and emits fileChanged(path).
    Q_INVOKABLE void externalWrite(const QString &path, const QString &content);
    Q_INVOKABLE void removeFile(const QString &path);
    Q_INVOKABLE QString file(const QString &path) const;
    Q_INVOKABLE QStringList files() const;
    /// Fill the vault with sample pages (Polish), see samplePath.
    Q_INVOKABLE void populateSample();
    /// Make write() fail (tests).
    Q_INVOKABLE void setFailWrites(bool fail) { m_failWrites = fail; }

    int writeCount() const { return m_writeCount; }
    QString samplePath() const;
    QVariantMap lastRoute() const { return m_lastRoute; }
    /// The text of the sample page.
    static QString sampleNote();
    /// A sample recipe with page properties and a "# Title" heading repeating its name.
    QString recipePath() const;
    static QString recipeNote();
    /// A trip plan written as a document (headings, paragraphs, nested and numbered lists, tasks,
    /// a callout, inline calculation) with `brulion: document` in its frontmatter.
    QString documentPath() const;
    static QString documentNote();

Q_SIGNALS:
    void nowChanged();
    void configChanged();
    void taskEdited(const QString &path, int line);
    void written(const QString &path);
    void navigated(const QVariantMap &route, bool newPane);
    void urlOpened(const QUrl &url);

private:
    QHash<QString, QString> m_files;
    QHash<QString, QByteArray> m_assets;
    mutable QHash<QString, QUrl> m_assetUrls;
    mutable std::unique_ptr<QTemporaryDir> m_assetDir;
    QHash<QString, QByteArray> m_webSources;
    /// url → "loading" | "ready" | "failed"; missing: not fetched.
    mutable QHash<QString, QString> m_webStates;
    QHash<QString, QUrl> m_webFiles;
    QSet<QString> m_webInFlight;
    bool m_webShown = false;
    int m_webFetches = 0;
    int m_writeCount = 0;
    int m_casRefusals = 0;
    bool m_failWrites = false;
    QVariantMap m_lastRoute;
    QString m_now;
    QString m_pageMode = QStringLiteral("outline");
    QString m_journalMode = QStringLiteral("outline");
    int m_taskEdits = 0;
    int m_queryRuns = 0;
    bool m_queriesFail = false;
    QStringList m_undecodable;
    /// The index for queries and link targets, rebuilt from the files after a change.
    mutable std::unique_ptr<VaultIndex> m_index;
    mutable bool m_indexDirty = true;
    const VaultIndex &currentIndex() const;
    bool m_indexSignalPending = false;
    /// Files changed: the index is out of date; indexChanged() follows on the next turn.
    void filesChanged(bool announce = true);
};

} // namespace brulion::editor
