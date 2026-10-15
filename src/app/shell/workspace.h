// Workspace — the open vault as the app sees it (QML singleton `Workspace`): owns the Vault (with
// its device-local History and LocalStore) and the VaultIndex, and implements EditorHost — the
// contract the block editor talks to (src/app/editor/editorhost.h).
//
//   open(rootPath, deviceStatePath)   vault folder + its AppData folder (History, LocalStore);
//                                      starts watching for external changes and builds the
//                                      index in the background (`indexing`)
//   EditorHost                         read/write (atomic, History via Vault), suggestions,
//                                      link resolution, block ids, templates, assets,
//                                      attachments, vault config, navigation
//   pages                              createPage, renamePage (rewrites links in every file
//                                      linking to it), deletePage (-> .trash/), restore
//   journals                           journalPath(date), ensureJournal(date) (creates the daily
//                                      note from the journal template), captureToJournal(text)
//   sidebar state                      favorites, recentPages (device-local LocalStore)
//
// Changes: every Vault change (own writes, other views, Syncthing/Nextcloud, other programs)
// updates the index and bumps `revision`; fileChanged(path) is emitted for every changed file,
// also for EditorHost::write(): the same page may be open in a second pane that must reload,
// and the saving editor recognises its own text. writeFile() (QML views) is the same as write().
//
// Everything is synchronous on the GUI thread; dates are local "YYYY-MM-DD" strings.
#pragma once

#include "editorhost.h"
#include "types.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace brulion {
class Vault;
class VaultIndex;
} // namespace brulion

namespace brulion::shell {

class Workspace : public EditorHost {
    Q_OBJECT

    /// A vault is open.
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY openChanged FINAL)
    /// Absolute path of the vault folder ("" when closed).
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY openChanged FINAL)
    /// Display name of the vault (its folder name).
    Q_PROPERTY(QString name READ name NOTIFY openChanged FINAL)
    /// The vault folder is shared by Syncthing (it has a .stfolder marker).
    Q_PROPERTY(bool syncthingFolder READ isSyncthingFolder NOTIFY openChanged FINAL)
    /// Index-backed helpers (suggestions, link resolution, rename rewriting) are compiled in.
    Q_PROPERTY(bool indexAvailable READ indexAvailable CONSTANT FINAL)
    /// The index is still being built after open().
    Q_PROPERTY(bool indexing READ indexing NOTIFY indexingChanged FINAL)
    /// Grows with every change of the vault's files (bind lists to it to refresh them).
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)
    /// Number of Markdown pages.
    Q_PROPERTY(int pageCount READ pageCount NOTIFY revisionChanged FINAL)
    /// Vault configuration (see vaultConfig()).
    Q_PROPERTY(QVariantMap config READ vaultConfig NOTIFY configChanged FINAL)
    /// [{ path, title }] pinned pages, in the user's order.
    Q_PROPERTY(QVariantList favorites READ favorites NOTIFY favoritesChanged FINAL)
    /// [{ path, title }] recently opened pages, newest first (at most 20).
    Q_PROPERTY(QVariantList recentPages READ recentPages NOTIFY recentPagesChanged FINAL)

public:
    static constexpr int MaxRecentPages = 20;

    explicit Workspace(QObject *parent = nullptr);
    ~Workspace() override;

    /// Open the vault folder (created when missing) with its device-local state folder
    /// (History + LocalStore; empty = none). Closes the previous vault first.
    bool open(const QString &rootPath, const QString &deviceStatePath, QString *error = nullptr);
    void close();

    bool isOpen() const;
    QString rootPath() const;
    QString name() const;
    bool isSyncthingFolder() const;
    bool indexAvailable() const { return true; }
    bool indexing() const { return !m_indexQueue.isEmpty(); }
    int revision() const { return m_revision; }
    int pageCount() const;

    Vault *vault() const { return m_vault.get(); }
    VaultIndex *index() const { return m_index.get(); }

    /// Build the rest of the index now (tests; normally done in chunks in the background).
    void finishIndexing();
    /// Clock for journals, templates and attachment names (default: current local time).
    void setClock(std::function<QDateTime()> clock);
    QDateTime now() const;
    /// UI language for templates ({{weekday}}, {{date:dddd}}): "pl" (default) or "en".
    void setUiLanguage(const QString &language);
    /// Serve assets as data: URLs (WebAssembly, where file:// URLs do not reach the vault).
    /// Default: true on WebAssembly, else false.
    void setDataUrlAssets(bool on);

    // ---- EditorHost -----------------------------------------------------------------------
    QString read(const QString &path, bool *ok = nullptr) override;
    bool write(const QString &path, const QString &content) override;
    Q_INVOKABLE QVariantList suggestPages(const QString &prefix, int limit = 20) const override;
    Q_INVOKABLE QVariantList suggestBlocks(const QString &text, int limit = 20) const override;
    Q_INVOKABLE QVariantList suggestTags(const QString &prefix, int limit = 20) const override;
    Q_INVOKABLE QString resolveLink(const QString &target, const QString &fromPath) const override;
    Q_INVOKABLE QString blockText(const QString &blockId) const override;
    Q_INVOKABLE QString ensureBlockId(const QString &path, int line) override;
    Q_INVOKABLE QVariantList templates() const override;
    Q_INVOKABLE QUrl assetUrl(const QString &path) const override;
    Q_INVOKABLE QString saveAttachment(const QString &fileName, const QByteArray &data) override;
    QVariantMap vaultConfig() const override;
    /// Forwards the route to the shell (navigationRequested) after resolveRoute().
    Q_INVOKABLE void navigate(const QVariantMap &route, bool newPane = false) override;
    Q_INVOKABLE void openUrl(const QUrl &url) override;
    /// Page routes the editor could not resolve get their path: {blockId} (a ((block)) of another
    /// page) the page holding the block; {target} (a link to a missing page) the path the page
    /// will have — newPagesFolder/<target>.md, or the journal page for a date — without creating
    /// it (the editor creates it with its first content). Other routes are returned unchanged.
    QVariantMap resolveRoute(const QVariantMap &route);
    /// Path a new page named `target` gets ("Projekty/Dom" -> "<newPagesFolder>/Projekty/Dom.md",
    /// "2026-10-20" -> that day's journal page); "" for an empty name.
    QString newPagePath(const QString &target) const;

    // ---- Files for QML views ----------------------------------------------------------------
    /// Content of a file ("" when missing).
    Q_INVOKABLE QString readFile(const QString &path);
    /// Save a file from a view other than the editor (emits fileChanged so editors reload).
    Q_INVOKABLE bool writeFile(const QString &path, const QString &content);
    Q_INVOKABLE bool exists(const QString &path) const;
    /// "Projekty/Dom.md" -> "Dom".
    Q_INVOKABLE QString pageTitle(const QString &path) const;
    /// "Projekty/Dom.md" -> "Projekty".
    Q_INVOKABLE QString folderOf(const QString &path) const;
    /// Is `path` a daily note? Returns its date "YYYY-MM-DD" or "".
    Q_INVOKABLE QString journalDate(const QString &path) const;

    // ---- Pages ------------------------------------------------------------------------------
    /// Create an empty page `title` ("Projekty/Dom" creates folders) in `folder` (default: the
    /// vault's newPagesFolder). Returns the new path ("Dom 2.md" when taken), "" on failure.
    Q_INVOKABLE QString createPage(const QString &title, const QString &folder = QString());
    Q_INVOKABLE bool createFolder(const QString &path);
    /// Rename a page in place and rewrite links to it in every file. Returns the new path or
    /// "" (target exists, invalid name, I/O error).
    Q_INVOKABLE QString renamePage(const QString &path, const QString &newTitle);
    /// Rename a folder (pages keep their links: links by page name do not change).
    Q_INVOKABLE QString renameFolder(const QString &path, const QString &newName);
    /// Move a file or folder to the trash. Returns its path inside .trash/ ("" on failure).
    Q_INVOKABLE QString deletePage(const QString &path);
    /// Restore a trashed file; returns the path it was restored to ("" on failure).
    Q_INVOKABLE QString restore(const QString &trashPath);

    // ---- Journals ---------------------------------------------------------------------------
    /// Path of the daily note for "YYYY-MM-DD" (whether it exists or not).
    Q_INVOKABLE QString journalPath(const QString &date) const;
    /// The daily note for `date`, created from the journal template when missing. "" for an
    /// invalid date or on I/O errors.
    Q_INVOKABLE QString ensureJournal(const QString &date);
    /// Append `text` as a new block ("- text", further lines indented) to today's journal.
    /// Returns the journal's path ("" when nothing was written).
    Q_INVOKABLE QString captureToJournal(const QString &text);

    // ---- Configuration ----------------------------------------------------------------------
    /// Save .brulion/config.json from a map with the keys of vaultConfig() (missing keys keep
    /// their value). False for an unusable journal file format or on I/O errors.
    Q_INVOKABLE bool setConfig(const QVariantMap &config);
    /// Problems found in .brulion/config.json (English, for the settings page).
    Q_INVOKABLE QStringList configWarnings() const;

    // ---- Lists for the shell ---------------------------------------------------------------
    /// Items for the quick switcher: [{ title, subtitle (folder / alias), icon, path }].
    /// Empty query: recent pages, then recently changed ones; else index suggestions.
    Q_INVOKABLE QVariantList switcherPages(const QString &query, int limit = 50) const;
    QVariantList favorites() const;
    QVariantList recentPages() const;
    Q_INVOKABLE bool isFavorite(const QString &path) const;
    Q_INVOKABLE void setFavorite(const QString &path, bool favorite);
    Q_INVOKABLE void toggleFavorite(const QString &path) { setFavorite(path, !isFavorite(path)); }
    /// Remember that a page was opened (recentPages).
    Q_INVOKABLE void notePageOpened(const QString &path);

Q_SIGNALS:
    void openChanged();
    void indexingChanged();
    void revisionChanged();
    void configChanged();
    void favoritesChanged();
    void recentPagesChanged();
    /// Files changed (paths, in change order) — after the index was updated.
    void filesChanged(const QStringList &paths);
    /// A path was renamed or moved (pages: links were rewritten already).
    void pathRenamed(const QString &oldPath, const QString &newPath);
    /// A path was deleted (moved to the trash or removed by another program).
    void pathRemoved(const QString &path);
    /// navigate() from the editor or a view; AppController handles it.
    void navigationRequested(const QVariantMap &route, bool newPane);
    /// Something failed that the user should know about (translated message).
    void errorOccurred(const QString &message);

private:
    void onVaultChanged(const QList<brulion::VaultChange> &changes);
    void indexChunk();
    void indexFile(const QString &path);
    QStringList storedList(const QString &key) const;
    void storeList(const QString &key, const QStringList &list);
    void forgetPath(const QString &path);
    void renamePathInLists(const QString &oldPath, const QString &newPath);
    QString templateText(const QString &templatePath, const QDateTime &when, const QString &title) const;
    /// A daily note of `date` under any name journalDateFromPath() accepts ("" when none).
    QString existingJournal(QDate date) const;

    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    QStringList m_indexQueue;
    QTimer m_indexTimer;
    int m_revision = 0;
    std::function<QDateTime()> m_clock;
    QString m_language = QStringLiteral("pl");
    bool m_dataUrlAssets = false;
    mutable QHash<QString, QUrl> m_dataUrls;
};

} // namespace brulion::shell
