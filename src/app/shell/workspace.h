// Workspace — the open vault as the app sees it (QML singleton `Workspace`): owns the Vault (with
// its device-local History and LocalStore), the VaultIndex, the reminders (`reminders`:
// ReminderScheduler + the platform's notifier) and sync (`sync`: Nextcloud engine + scheduler,
// Syncthing status, conflict copies), and implements EditorHost — the contract the block editor
// talks to (src/app/editor/editorhost.h) — plus its optional QueryHost (editor/queryhost.h).
//
//   open(rootPath, deviceStatePath)   vault folder + its AppData folder (History, LocalStore);
//                                      starts watching for external changes, builds the index
//                                      in chunks on the event loop (`indexing`, `indexProgress`,
//                                      then `ready`: the reminders start), attaches sync
//   EditorHost                         read/write (atomic, History via Vault), suggestions,
//                                      link resolution, block ids, templates, assets,
//                                      attachments, vault config, navigation
//   QueryHost                          runQuery(source, currentPath) for ```query blocks and
//                                      views (tasks lists), setTaskStatus(path, line, status)
//   index queries                      search, backlinks, unlinkedReferences, tagList, journals,
//                                      journalDates, calendarItems (plain QVariant data; C++
//                                      models use index() directly); calendarItems and runQuery
//                                      keep caches of the last ranges and queries asked (after
//                                      an index change only the changed notes are computed)
//   pages                              createPage, renamePage (rewrites links in every file
//                                      linking to it), deletePage (-> .trash/), restore,
//                                      trashItems, deleteForever, emptyTrash; the cleanup view:
//                                      unreferencedFiles, deletePages, restoreAll
//   folders                            createFolder (names sanitised, hidden ones refused),
//                                      renameFolder, movePath (pages, attachments and folders;
//                                      every link and tag keeps its target: the index's
//                                      planRelocation(); special folders rebase the notebook's
//                                      settings), moveImpact / relocationImpact (what a move
//                                      would rewrite), folderPaths, folderInfo, subfolders
//   history                            history(path), historyText(id), restoreVersion(path, id)
//   journals                           journalPath(date), ensureJournal(date) (creates the daily
//                                      note from the journal template), setHabitCheck(name, date,
//                                      state) / addHabit (habits), quickCapture(text)
//                                      ("Buy milk tomorrow at 6pm" -> a task with ⏰ in today's
//                                      journal, notebook/capture.h), captureToJournal(text)
//   device-local state                 favorites, recentPages, foldState(path) (LocalStore)
//
// Changes: every Vault change (own writes, other views, Syncthing/Nextcloud, other programs)
// updates the index and bumps `revision`; fileChanged(path) is emitted for every changed file,
// also for EditorHost::write(): the same page may be open in a second pane that must reload,
// and the saving editor recognises its own text. writeFile() (QML views) is the same as write().
//
// Everything is synchronous on the GUI thread; dates are local "YYYY-MM-DD" strings.
#pragma once

#include "drawing/excalidrawmd.h"
#include "editorhost.h"
#include "notebook/indexcachekeeper.h"
#include "notebook/indexloader.h"
#include "notebook/pages.h"
#include "queryhost.h"
#include "replacesession.h"
#include "types.h"
#include "workspacebackups.h"
#include "workspacereminders.h"
#include "workspacesync.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QSet>
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
struct FileMove;
struct QueryItem;
} // namespace brulion

namespace brulion::pdfview {
class PdfThumbnails;
} // namespace brulion::pdfview

namespace brulion::shell {

/// Write `content` to `path` so that the text it replaces is kept in the vault's version history
/// even when an ordinary snapshot of the path was taken minutes ago (History's 5-minute throttle
/// would skip it): restoring a version, resolving a sync conflict — changes after which the user
/// is told "your previous version is in the history". False on I/O errors
/// (vault.errorString()).
bool writeTextKeepingHistory(Vault &vault, const QString &path, const QString &content,
                             ChangeSource source = ChangeSource::User);
/// The same for bytes (Vault::writeBinary): a drawing written over its previous version (the sync
/// center's merge of a conflict copy) — the version replaced stays in History, which keeps
/// snapshots of text files (an SVG, `*.excalidraw.md`, `.excalidraw`), whatever its size (the
/// vault snapshots only what it holds in memory; a drawing with pasted photos may be larger).
/// Nothing is written when that version cannot be recorded. False with `error` (History's or the
/// vault's errorString()).
bool writeBytesKeepingHistory(Vault &vault, const QString &path, const QByteArray &bytes,
                              ChangeSource source = ChangeSource::User, QString *error = nullptr);

class Workspace : public EditorHost, public QueryHost {
    Q_OBJECT
    Q_INTERFACES(brulion::QueryHost)

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
    /// Share of the files indexed since open(), 0…1 (1 when done or closed).
    Q_PROPERTY(qreal indexProgress READ indexProgress NOTIFY indexProgressChanged FINAL)
    /// A vault is open and its index holds every file (lists, queries and reminders are complete).
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged FINAL)
    /// Reminders of the open vault (missed reminders, upcoming, snooze): workspacereminders.h.
    Q_PROPERTY(brulion::shell::WorkspaceReminders *reminders READ reminders CONSTANT FINAL)
    /// Nextcloud sync, Syncthing status and conflict copies: workspacesync.h.
    Q_PROPERTY(brulion::shell::WorkspaceSync *sync READ sync CONSTANT FINAL)
    /// Automatic backups of the open notebook: workspacebackups.h.
    Q_PROPERTY(brulion::shell::WorkspaceBackups *backups READ backups CONSTANT FINAL)
    /// Find and replace in the whole notebook (replacesession.h).
    Q_PROPERTY(brulion::shell::ReplaceSession *replacing READ replacing CONSTANT FINAL)
    /// Number of daily notes (grows while the index is built).
    Q_PROPERTY(int journalCount READ journalCount NOTIFY indexChanged FINAL)
    /// Number of items in the trash (a deleted folder counts once).
    Q_PROPERTY(int trashCount READ trashCount NOTIFY trashChanged FINAL)
    /// Grows with every change of the vault's files (bind lists to it to refresh them).
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)
    /// Number of Markdown pages.
    Q_PROPERTY(int pageCount READ pageCount NOTIFY revisionChanged FINAL)
    /// Pages without the daily notes and conflict copies (the sidebar's "Pages" count).
    Q_PROPERTY(int pagesWithoutJournals READ pagesWithoutJournals NOTIFY indexChanged FINAL)
    /// Vault configuration (see vaultConfig()).
    Q_PROPERTY(QVariantMap config READ vaultConfig NOTIFY configChanged FINAL)
    /// [{ path, title }] pinned pages, in the user's order.
    Q_PROPERTY(QVariantList favorites READ favorites NOTIFY favoritesChanged FINAL)
    /// [{ path, title }] recently opened pages, newest first (at most 20).
    Q_PROPERTY(QVariantList recentPages READ recentPages NOTIFY recentPagesChanged FINAL)
    /// Pictures from the internet in notes are shown without asking (AppSettings'
    /// webPicturesShown; workspacepictures.cpp). Off: each waits for "Show".
    Q_PROPERTY(
            bool webPicturesShown READ webPicturesShown WRITE setWebPicturesShown NOTIFY webPicturesShownChanged FINAL)
    /// Task status changes are logged in the task's `:LOGBOOK:` and "In progress" starts / stops its
    /// clock (AppSettings' taskStatusHistory; docs/SYNTAX.md §5). Off by default.
    Q_PROPERTY(bool taskStatusHistory READ taskStatusHistory WRITE setTaskStatusHistory NOTIFY taskStatusHistoryChanged
                       FINAL)
    /// PDFs open in the app's own viewer (a PDF renderer is built in: platform::pdfAvailable();
    /// workspacepdf.cpp). Off (the browser): they go to another app / a new tab.
    Q_PROPERTY(bool pdfViewable READ pdfViewable CONSTANT FINAL)
    /// The PDF viewer can search (platform::pdfTextAvailable(): Qt PDF, poppler, Android 15+).
    Q_PROPERTY(bool pdfSearchable READ pdfSearchable CONSTANT FINAL)

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
    bool indexing() const { return m_loader && !m_loader->isDone(); }
    qreal indexProgress() const;
    bool ready() const { return isOpen() && !indexing(); }
    WorkspaceReminders *reminders() const { return m_reminders; }
    /// Vaults opened from now on arm no reminders (the documentation notebook, which has none):
    /// their scheduler never starts, so nothing replaces what the platform armed for the
    /// notebook before (Android keeps its alarms) — see AppController::finishOpening().
    void setRemindersPaused(bool paused) { m_remindersPaused = paused; }
    bool remindersPaused() const { return m_remindersPaused; }
    WorkspaceSync *sync() const { return m_sync; }
    WorkspaceBackups *backups() const { return m_backups; }
    ReplaceSession *replacing() const { return m_replacing; }
    /// The threads for looking through notes (indexworkers.h); none on WebAssembly.
    notebook::WorkerPool workerPool();
    int journalCount() const;
    int trashCount() const;
    int revision() const { return m_revision; }
    int pageCount() const;
    int pagesWithoutJournals() const;

    Vault *vault() const { return m_vault.get(); }
    VaultIndex *index() const { return m_index.get(); }
    /// How the index was loaded after open() (tests, the event log).
    notebook::IndexLoader::Stats indexStats() const;
    /// The index cache's saving policy for notebooks opened from now on (tests; the default keeps
    /// no cache under 200 notes and saves 5 s after a load, 60 s after the last change).
    void setIndexCachePolicy(const notebook::IndexCacheKeeper::Policy &policy) { m_cachePolicy = policy; }
    /// Keeps the open notebook's index cache (nullptr: none — no device state folder,
    /// BRULION_NO_INDEX_CACHE, or the index is still loading).
    notebook::IndexCacheKeeper *indexCacheKeeper() const { return m_cacheKeeper.get(); }
    /// The index cache file cannot be kept (`why`; the browser's storage refused it, see
    /// WasmStorage::cacheNotSaved()): the open notebook writes none any more.
    void stopIndexCache(const QString &why);

    /// Build the rest of the index now (normally done in chunks in the background): before what
    /// needs every note known — a rename, a route to a block, a new block id — and in tests.
    void finishIndexing() override;
    /// Clock for journals, templates and attachment names (default: current local time).
    void setClock(std::function<QDateTime()> clock);
    QDateTime now() const;
    /// UI language for templates ({{weekday}}, {{date:dddd}}), quick capture dates, reminder
    /// texts and sync messages: "pl" (default) or "en".
    void setUiLanguage(const QString &language);
    /// Serve assets as data: URLs (WebAssembly, where file:// URLs do not reach the vault).
    /// Default: true on WebAssembly, else false.
    void setDataUrlAssets(bool on);

    // ---- EditorHost -----------------------------------------------------------------------
    QString read(const QString &path, bool *ok = nullptr) override;
    bool write(const QString &path, const QString &content) override;
    /// Vault::writeTextIfUnchanged(): a change of another program is taken in (fileChanged) instead
    /// of overwritten.
    WriteResult writeIfUnchanged(const QString &path, const QString &content, const QString &expected) override;
    /// Pages shown by editors are watched themselves (Vault::watchFile()), counted per page.
    void editorOpened(const QString &path) override;
    void editorClosed(const QString &path) override;
    Q_INVOKABLE QVariantList suggestPages(const QString &prefix, int limit = 20) const override;
    Q_INVOKABLE QVariantList suggestBlocks(const QString &text, int limit = 20) const override;
    Q_INVOKABLE QVariantList suggestTags(const QString &prefix, int limit = 20) const override;
    /// Page-property keys and values in use (VaultIndex::propertyKeys() / propertyValues()).
    QVariantList propertyKeys() const override;
    QVariantList propertyValues(const QString &key, const QString &prefix, int limit = 20) const override;
    Q_INVOKABLE QString resolveLink(const QString &target, const QString &fromPath) const override;
    markdown::PageIcon pageIcon(const QString &path) const override;
    /// Pictures of the notebook this device can show, for a page's cover (docs/UI.md "Page icon
    /// and cover"): [{ path, title (the file name), folder, url (assetUrl) }] whose path matches
    /// every word of `query` (case and diacritics aside), in path order; at most `limit`.
    Q_INVOKABLE QVariantList pictureAttachments(const QString &query, int limit = 60) const;
    /// The icon of the page at `path` for QML lists (editor::pageIconMap: { kind, name }).
    Q_INVOKABLE QVariantMap pageIconOf(const QString &path) const;
    /// The shortest [[target]] reaching `path` from `fromPath` (VaultIndex::linkTarget(): "Dom",
    /// "Projekty/Dom" when the title alone names another page); "" when unknown.
    Q_INVOKABLE QString linkTarget(const QString &path, const QString &fromPath = QString()) const override;
    /// What a path part of a link opens (VaultIndex::resolveLinkPart()); never waits for the
    /// index (rendering asks: colours may lag while it is built, a click finishes it first —
    /// resolveRoute()).
    Q_INVOKABLE QVariantMap linkPart(const QString &target, int part, const QString &fromPath) const override;
    Q_INVOKABLE QString blockText(const QString &blockId) const override;
    /// The index knows a block with this id (blockById: `^id` or Logseq `id::`, any text).
    bool blockIdTaken(const QString &blockId) const override;
    QString linkedBlockText(const QString &path, const QString &blockId) const override;
    Q_INVOKABLE QString ensureBlockId(const QString &path, int line) override;
    Q_INVOKABLE QVariantList templates() const override;
    Q_INVOKABLE QUrl assetUrl(const QString &path) const override;
    /// image://drawing/<the file's absolute path, percent-encoded>?v=<mtime>-<size>&dark=0|1: the
    /// preview of a drawing in the light or the dark theme (DrawingImageProvider); empty without
    /// a vault or a path.
    Q_INVOKABLE QUrl drawingUrl(const QString &path, bool dark) const override;
    /// The browser: the picture's size, read with its data: URL (else invalid: the editor reads
    /// the local file's header itself).
    QSize assetPixelSize(const QString &path) const override;
    Q_INVOKABLE QString saveAttachment(const QString &fileName, const QByteArray &data) override;
    /// A picked file used from the notebook (a drawing's backdrop): the attachment of the same name
    /// and bytes in the attachments folder, else stored as saveAttachment() stores it.
    Q_INVOKABLE QString importAttachment(const QString &fileName, const QByteArray &data) override;
    /// A picked local file or Android document: one inside the notebook's folder is used where it
    /// is (its vault path; an Android document by the file its URI names, platform/documentpaths.h),
    /// others imported as above (at most `maxBytes`, MaxAttachmentBytes by default).
    Q_INVOKABLE QString importAttachment(const QUrl &file, qint64 maxBytes = MaxAttachmentBytes) override;
    /// The pick that imported `path` was abandoned: the copy importAttachment() stored for it is
    /// deleted again (not to the trash: the picked file is still where it was) — only one stored in
    /// this session that no later pick reused.
    Q_INVOKABLE void discardImport(const QString &path) override;
    /// The file system path of the vault path `path` ("" without a notebook or a path): the
    /// drawing editor's PDF pages (DrawingBackdrops' path resolver).
    Q_INVOKABLE QString absoluteFilePath(const QString &path) const;
    /// Pictures from the internet (workspacepictures.cpp): fetched by webpicture::PictureFetcher
    /// into the device's cache (<app cache>/web-pictures, webpicture::PictureCache), shown from
    /// there — file: URLs, data: URLs in the browser. Not tied to the notebook: the cache is the
    /// device's, and it survives switching notebooks.
    QVariantMap webPicture(const QString &url) const override;
    void loadWebPicture(const QString &url, bool forDownload = false) override;
    QByteArray webPictureData(const QString &url, QString *format) const override;
    bool webPicturesShown() const;
    void setWebPicturesShown(bool shown);
    /// EditorHost: the index for task dependencies and clocks; the status history setting.
    const VaultIndex *taskIndex() const override;
    bool taskStatusHistory() const override { return m_taskStatusHistory; }
    void setTaskStatusHistory(bool on);
    /// Stop every running task clock (the palette's "Stop task timer"); how many stopped.
    Q_INVOKABLE int stopTaskClocks();
    /// The cache's folder (tests: a temporary one; "" — the default).
    void setWebPictureCacheDir(const QString &dir);
    /// The size the cache is pruned to (tests; 0 — the default: 200 MB, 50 MB in the browser,
    /// whose cache is in the tab's memory).
    void setWebPictureCacheBytes(qint64 bytes) { m_webPictureCacheBytes = bytes; }
    /// Which hosts are this device's or its private network's (tests: 127.0.0.1 as a host of
    /// the internet; webpicture::isPrivateHost by default). Such a picture is not fetched without
    /// asking, and no redirect leads there from a host of the internet.
    void setWebPictureHostCheck(std::function<bool(const QString &)> isPrivate);
    /// `host` is this device's or its private network's (setWebPictureHostCheck()): a bulk
    /// fetch (the export's copies) leaves it out — such a picture is fetched only when asked for.
    bool isPrivateWebHost(const QString &host) const;
    /// The browser build: the scheme of the page Brulion runs on ("https", "http"; "" — not in a
    /// browser, the default elsewhere). Pictures are fetched with requests a page may send to
    /// another site (PictureFetcher::setSimpleRequests), and a failed connection names what a
    /// browser forbids: a site that does not allow other pages its pictures (CORS), http:// on a
    /// https:// page (mixed content).
    void setWebPageScheme(const QString &scheme);
    QString webPageScheme() const { return m_webPageScheme; }
    QVariantMap vaultConfig() const override;

    // ---- PDFs (workspacepdf.cpp; docs/UI.md "PDF viewer") ------------------------------------
    bool pdfViewable() const override;
    bool pdfSearchable() const;
    /// The file is a PDF by its name (paths::isPdfPath): PageView opens it in the PDF viewer.
    Q_INVOKABLE bool isPdf(const QString &path) const;
    /// "<mtime ms>-<size>" of the vault's file ("" when there is none): the PDF viewer reloads
    /// when it changes.
    Q_INVOKABLE QString fileVersion(const QString &path) const;
    /// The vault file's size as people read it ("1.4 MB" in the locale; "" when there is none):
    /// the PDF viewer's panel when it cannot show the pages.
    Q_INVOKABLE QString fileSizeText(const QString &path) const;
    /// The thumbnail of page `page` of the PDF at `path` (EditorHost::pdfThumbnail): drawn and cached
    /// by pdfview::PdfThumbnails (<app cache>/pdf-thumbnails, 64 MiB — 32 MiB on Android —, the long
    /// side for the primary screen's ratio); pdfThumbnailChanged(path) when one is ready or failed.
    QVariantMap pdfThumbnail(const QString &path, int page) const override;
    /// The thumbnails' folder (tests: a temporary one; "" — the default) and its size cap (0 — the
    /// default).
    void setPdfThumbnailCacheDir(const QString &dir) { m_pdfThumbnailDir = dir; }
    /// An attachment of the notebook handed to the system's app / a new browser tab
    /// (platform::openFileExternally): the path normalised, inside the vault (no "..", a file), and
    /// on the allow-list of platform/externalfiles.h by its name AND its first bytes (a PDF starts
    /// with "%PDF-", a DOCX is a zip with Word's parts…: a renamed program is never handed to
    /// ShellExecute / xdg-open), with that list's MIME type. Messages: errorOccurred (not a PDF / not
    /// a kind that may open elsewhere, no app, could not be opened), noticeOccurred (the browser
    /// blocked the tab: downloaded instead). The browser reads the bytes (at most
    /// MaxAttachmentBytes) for the tab's blob. False when it was not opened.
    Q_INVOKABLE bool openInOtherApp(const QString &path) override;
    /// openInOtherApp() would hand this file over (the read-only view's and the top bar's button).
    Q_INVOKABLE bool canOpenInOtherApp(const QString &path) const;
    /// "Draw on this page": the drawing DrawingBackdrops prepared (`prepared`: {svg, path, page,
    /// name, error}) stored as "<name> p. <page>.svg" (EditorHost::drawingFileNameOn; a free name)
    /// and embedded (`![[<link target>]]`) right after the block of `fromNote` that embeds the PDF
    /// (around line `fromLine`; notebook::insertBlockAfterEmbed, written with writeIfUnchanged, three
    /// tries), or — no `fromNote`, or it is gone — as a block of today's journal (captureToJournal).
    /// {path (the drawing), note (where it was embedded), journal (bool)}; {} and errorOccurred on
    /// failure.
    Q_INVOKABLE QVariantMap addDrawingOnPdfPage(const QString &pdfPath, const QVariantMap &prepared,
                                                const QString &fromNote, int fromLine);
    /// The drawings `note` embeds whose backdrop is page `page` of `pdfPath` (core
    /// drawing::backdropSource of each embedded drawing file, at most 50 read): [{path, name}] —
    /// "Draw on this page" offers to open one instead of making a second.
    Q_INVOKABLE QVariantList drawingsOnPdfPage(const QString &pdfPath, int page, const QString &note) const;
    /// The note a drawing belongs to (the drawing editor's "Previous / Next PDF page",
    /// docs/UI.md "Drawing editor" → Backdrop): `fromNote` (the note it was opened from) when that
    /// links to `drawingPath`, else the first note that does (backlinks(): journals newest first,
    /// then by path); "" none.
    Q_INVOKABLE QString noteOfDrawing(const QString &drawingPath, const QString &fromNote) const;
    /// The drawing of a neighbouring PDF page DrawingBackdrops prepared (`prepared`: {svg, path,
    /// page, name, error}) stored as "<name> s. <page>.svg" (EditorHost::drawingFileNameOn; a free
    /// name) and embedded in `note` right before (`before`: the previous page) or after the block
    /// that embeds `drawingPath` (notebook::insertBlockBeforeEmbed / insertBlockAfterEmbed, compare
    /// and swap, three tries), or — no note, or it is gone — as a block of today's journal.
    /// {path, note, journal}; {} and errorOccurred on failure.
    Q_INVOKABLE QVariantMap addDrawingBeside(const QString &drawingPath, const QVariantMap &prepared,
                                             const QString &note, bool before);

    /// Copied blocks as HTML (io::clipboardHtml: attachments as file links — none in the browser,
    /// whose assets are data: URLs —, block references as their text); workspaceclipboard.cpp.
    QString renderHtml(const QString &markdown, const QString &fromPath) const override;
    /// Pasted HTML as Markdown (io::htmlToMarkdown); workspaceclipboard.cpp.
    QString htmlToMarkdown(const QString &html) const override;
    /// Forwards the route to the shell (navigationRequested) after resolveRoute().
    Q_INVOKABLE void navigate(const QVariantMap &route, bool newPane = false) override;
    Q_INVOKABLE void openUrl(const QUrl &url) override;
    /// Page routes the editor could not resolve get their path: {blockId} (a ((block)) of another
    /// page) the page holding the block; {target} (a link to a missing page) the path the page
    /// will have — newPagesFolder/<target>.md, or the journal page for a date — without creating
    /// it (the editor creates it with its first content). A tag route {view: "tag", tag} — a
    /// click on #tag — becomes the page the tag names (tagPage()); with `overview: true` it stays
    /// the tag overview. A path part of a link {view: "page", target: prefix, linkTarget, linkPart,
    /// from} (docs/SYNTAX.md §2 "Path parts") becomes what it opens: {view: "page", path} of the
    /// page, {view: "allPages", path} of the folder, or {view: "page", path, target} of the new
    /// page (newPagePath()) — never with the part keys; {} for a part the target does not have.
    /// Other routes are returned unchanged.
    QVariantMap resolveRoute(const QVariantMap &route);
    /// The page a tag names (docs/SYNTAX.md §3, VaultIndex::resolveTag(): like [[tag]] written at
    /// the vault root), else the path the page gets when written (newPagePath(): nested tags are
    /// folders, a date the journal page; named as the tag was first written). "" for no tag.
    Q_INVOKABLE QString tagPage(const QString &tag);
    /// Path a new page named `target` gets ("Projekty/Dom" -> "<newPagesFolder>/Projekty/Dom.md",
    /// "2026-10-20" -> that day's journal page); "" for an empty name.
    Q_INVOKABLE QString newPagePath(const QString &target) const;

    // ---- Files for QML views ----------------------------------------------------------------
    /// Content of a file ("" when missing).
    Q_INVOKABLE QString readFile(const QString &path);
    /// The start of a file as text, reading at most `maxBytes` of it (a picture's text view: a
    /// photo is never read whole for what it shows): { text, truncated, bytes (read and shown),
    /// size (the file's), bytesText, sizeText ("1,5 MB" in the UI language) }. UTF-8 with a
    /// leading BOM dropped, like readFile(); a character cut by the limit is left out whole (never
    /// half of it as "�"). An empty text (ok false) for a missing or unreadable file.
    Q_INVOKABLE QVariantMap readTextStart(const QString &path, int maxBytes) const;
    /// Save a file from a view other than the editor (emits fileChanged so editors reload).
    Q_INVOKABLE bool writeFile(const QString &path, const QString &content);
    Q_INVOKABLE bool exists(const QString &path) const;
    /// "Projekty/Dom.md" -> "Dom".
    Q_INVOKABLE QString pageTitle(const QString &path) const;
    /// "Projekty/Dom.md" -> "Projekty".
    Q_INVOKABLE QString folderOf(const QString &path) const;
    /// An attachment picture (png, jpg, ...): PageView shows it as a picture, not as text.
    Q_INVOKABLE bool isPicture(const QString &path) const;
    /// A picture whose format this device can decode (QImageReader's formats: WebP and AVIF need
    /// Qt's image plugins, which Android, the browser and some desktops lack); false for files
    /// that are no picture. The picture view and the editor's embeds name a format it cannot show.
    Q_INVOKABLE bool canDecodePicture(const QString &path) const override;
    /// The file is a drawing (docs/SYNTAX.md §2 "Drawings"): an SVG whose start names an embedded
    /// Excalidraw scene, a `.excalidraw` scene, an Obsidian Excalidraw plugin file (a note whose
    /// frontmatter has `excalidraw-plugin` — from the index, without reading the file): its embed
    /// and its picture view open the drawing editor; such a note opens as its drawing (PageView).
    Q_INVOKABLE bool isDrawing(const QString &path) const override;
    /// The size of a `.excalidraw` or plugin drawing's preview (editor::localDrawingSize).
    QSize drawingSize(const QString &path) const override;
    /// An attachment's bytes (the drawing editor); empty when missing or unreadable.
    Q_INVOKABLE QByteArray readAttachment(const QString &path) const;
    /// Write an existing attachment again (the drawing editor's saves): atomic, what it replaces
    /// kept in History as for notes, fileChanged follows. False (and errorOccurred) when it could
    /// not be written; false without a message when the file is gone from the disk (deleted
    /// elsewhere, not reported yet): it is never written again, pathRemoved follows.
    Q_INVOKABLE bool writeAttachment(const QString &path, const QByteArray &data);
    /// Write a new attachment at `path`, or at "Name 2.ext", … when that is taken (the drawing
    /// editor keeps what was drawn here beside a file changed elsewhere into one it cannot edit).
    /// The path used; "" (and errorOccurred) when it could not be written.
    Q_INVOKABLE QString createAttachment(const QString &path, const QByteArray &data);
    /// The picture's pixels from its header ("assets/a.png" -> 4000 × 3000; -1 × -1 when the
    /// header has none): the picture view's own-pixels size, without decoding the picture; a
    /// `.excalidraw` or plugin drawing's preview size (drawingSize()).
    Q_INVOKABLE QSize pictureSize(const QString &path) const;
    /// Is `path` a daily note? Returns its date "YYYY-MM-DD" or "".
    Q_INVOKABLE QString journalDate(const QString &path) const;

    // ---- Pages ------------------------------------------------------------------------------
    /// Create an empty page `title` ("Projekty/Dom" creates folders) in `folder` (default: the
    /// vault's newPagesFolder). Returns the new path ("Dom 2.md" when taken), "" on failure. A
    /// page differing only in letter case ("dom" next to "Dom.md") is that page: its path is
    /// returned (noticeOccurred() says so) — case-insensitive storage holds one file for both.
    /// Folders are used as spelled in the vault ("projekty/Dom" -> "Projekty/Dom.md").
    /// `mode` ("document" | "outline", "" = the notebook's default): the note mode chosen in the
    /// new-page dialog — written as the frontmatter key `brulion:` only when the page would not
    /// get that mode anyway (markdown::withNoteModeChoice; the existing page of a letter-case twin
    /// is never changed).
    Q_INVOKABLE QString createPage(const QString &title, const QString &folder = QString(),
                                   const QString &mode = QString());
    /// Create a folder ("Projekty/Dom" creates both levels; folders used as spelled in the vault).
    /// Every new name is sanitised like a page title ("a:b?" -> "a b"); a name that would be hidden
    /// (a leading '.', or one of the ignored folders: logseq, node_modules, …) is refused with an
    /// error. Returns the folder's path ("" on failure); an existing folder is fine.
    Q_INVOKABLE QString createFolder(const QString &path);
    /// Rename a page in its folder and rewrite what names it in every file (links, tags:
    /// docs/SYNTAX.md §2, §3; every link keeps its target, an ambiguous one gets the shortest
    /// path). `newTitle` is a title: a "/" in it is sanitised like any character a file name cannot
    /// have ("TCP/IP" -> "TCP IP.md", the page header's title edit) — unless `slashMoves` (the
    /// Rename dialog): then it moves the page, the path from the notebook root ("Archiwum/Dom").
    /// Returns the new path or "" (target exists, invalid name, I/O error).
    Q_INVOKABLE QString renamePage(const QString &path, const QString &newTitle, bool slashMoves = false);
    /// Rename a folder the same way (with `slashMoves` a "/" in `newName` moves it, from the
    /// notebook root).
    Q_INVOKABLE QString renameFolder(const QString &path, const QString &newName, bool slashMoves = false);
    /// Move a page, an attachment or a folder into `targetFolder` ("" = the notebook root):
    /// every link and tag that named a moved file still names it (VaultIndex::planRelocation(),
    /// one history step per rewritten note); favourites, recent pages, fold state, history and
    /// open routes follow; moving a special folder (journals, templates, attachments, new pages)
    /// or one holding it updates .brulion/config.json (a notice says so). Refused with an error:
    /// into itself, into a hidden or ignored folder, onto an existing name or a letter-case twin.
    /// Returns the new path ("" on failure; the path itself when it is in that folder already).
    Q_INVOKABLE QString movePath(const QString &path, const QString &targetFolder);
    /// What movePath(path, targetFolder) would do: { ok, error (translated, when not ok), path (the
    /// new path), links, tags (rewritten; the ```query filters that name a moved page or folder
    /// counted as links, their tag: filters as tags), queries (those filters), notes (rewritten
    /// files), specials (the settings of
    /// .brulion/config.json that follow: "journals" | "templates" | "attachments" | "newPages" |
    /// "journalTemplate") }.
    Q_INVOKABLE QVariantMap moveImpact(const QString &path, const QString &targetFolder);
    /// moveImpact() without blocking, for the dragged row's ghost: the plan is made a few
    /// milliseconds per event-loop turn (RelocationPlanner) and moveImpactReady(path,
    /// targetFolder, impact) delivers it — on the next turn when it is known already (impacts are
    /// kept per target until the notebook changes). A new request or cancelMoveImpact() drops
    /// the one in progress; a change of the notebook meanwhile starts it again.
    Q_INVOKABLE void requestMoveImpact(const QString &path, const QString &targetFolder);
    Q_INVOKABLE void cancelMoveImpact();
    /// The same for a rename (renamePage() / renameFolder() with `newName` and `slashMoves`):
    /// { path } is "" when the name is not usable.
    Q_INVOKABLE QVariantMap renameImpact(const QString &path, const QString &newName, bool isDir,
                                         bool slashMoves = false);
    /// Tracked folders (not hidden or ignored ones) in Polish alphabetical order of their paths.
    Q_INVOKABLE QStringList folderPaths() const;
    /// { exists, pages (Markdown files inside, at any depth, without conflict copies),
    ///   attachments, folders (subfolders at any depth), specials: [{ special, path }] — the
    ///   notebook's special folders the folder is or holds (special: "journals" | "templates" |
    ///   "attachments" | "newPages") and the journal template ("journalTemplate") }.
    Q_INVOKABLE QVariantMap folderInfo(const QString &path) const;
    /// The direct subfolders of `folder` ("" = the root) for All pages' folder rows, in Polish
    /// order: [{ path, name, pages (at any depth, without daily notes outside the journals folder) }];
    /// attachment-only folders left out.
    Q_INVOKABLE QVariantList subfolders(const QString &folder) const;
    /// Move a file or folder to the trash. Returns its path inside .trash/ ("" on failure).
    Q_INVOKABLE QString deletePage(const QString &path);
    /// Move several files to the trash. Returns their paths inside .trash/, without those that
    /// failed (errorOccurred says which).
    Q_INVOKABLE QStringList deletePages(const QStringList &paths);
    /// Restore a trashed file; returns the path it was restored to ("" on failure).
    Q_INVOKABLE QString restore(const QString &trashPath);
    /// Restore several trashed files; returns how many came back.
    Q_INVOKABLE int restoreAll(const QStringList &trashPaths);
    /// The trash, most recently deleted first: [{ trashPath, original, title, folder (it was a
    /// folder), files, size, deletedAt ("YYYY-MM-DDTHH:mm:ss" or ""), when (human) }].
    Q_INVOKABLE QVariantList trashItems() const;
    /// Delete one trashed item (a trashPath from trashItems()) permanently.
    Q_INVOKABLE bool deleteForever(const QString &trashPath);
    /// Delete everything in the trash permanently.
    Q_INVOKABLE bool emptyTrash();
    /// What the cleanup view offers (notebook::unreferencedFiles() without favourites; empty while
    /// the notebook is being indexed, when links are not known yet): [{ path, title, folder,
    /// attachment, picture, pdf, size, sizeText, modified (human), openTasks, reminders }].
    Q_INVOKABLE QVariantList unreferencedFiles() const;

    // ---- Version history (device-local, vault/history.h) ------------------------------------
    /// Versions of `path`, newest first: [{ id, time ("YYYY-MM-DDTHH:mm:ss"), when (human), size,
    /// reason: what replaced that text — "user" | "external" | "sync" | "import" | "system" }].
    Q_INVOKABLE QVariantList history(const QString &path) const;
    /// Text of a version ("" when unknown).
    Q_INVOKABLE QString historyText(const QString &id) const;
    /// Write version `id` back to `path` (also a deleted page); the current text is kept in the
    /// history first. False when the version is unknown or the file cannot be written.
    Q_INVOKABLE bool restoreVersion(const QString &path, const QString &id);
    /// Paths with a history (also deleted ones), sorted.
    Q_INVOKABLE QStringList historyPaths() const;

    // ---- Index queries for views (plain data; C++ models use index()) ------------------------
    /// Full-text search: [{ path, title, folder, line (-1: title only), excerpt (plain text),
    /// score, journal ("YYYY-MM-DD" for daily notes) }], best first.
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 50) const;
    /// Links to `path` from other notes: [{ path, title, line, text (plain text of the linking
    /// block), blockId?, journal }], journals newest first, then by path.
    Q_INVOKABLE QVariantList backlinks(const QString &path) const;
    /// Plain-text mentions of the page in other notes: [{ path, title, line, excerpt }].
    Q_INVOKABLE QVariantList unlinkedReferences(const QString &path) const;
    /// Every tag, most used first: [{ tag, display, count }].
    Q_INVOKABLE QVariantList tagList() const;
    /// Calendar entries within [from, to] ("YYYY-MM-DD", at most 400 days): [{ date, time ("HH:mm"
    /// or ""), kind ("journal" | "due" | "scheduled" | "start" | "reminder" | "done"), title,
    /// path, line, status ("" or a task status), recurring, alarm }].
    Q_INVOKABLE QVariantList calendarItems(const QString &from, const QString &to) const;
    /// The plan of the day `date` (the side panel's "Day plan"; reminders/dayplan.h): one row
    /// per block with a due date, a scheduled date or reminders that day — [{ time ("HH:mm" or
    /// ""), title, path, line, status ("" or a task status), due, scheduled, reminder, alarm,
    /// recurring }], timed rows first by time.
    Q_INVOKABLE QVariantList dayPlan(const QString &date) const;
    /// Calendar ranges and queries whose caches are kept (the most recently asked first; tests).
    static constexpr int MaxCachedRanges = 4;
    static constexpr int MaxCachedQueries = 8;
    /// A query matching more is not kept (a cache holds every match, ~0.2 KB each, whatever its
    /// `limit:`): it is computed afresh each time, as before the caches.
    static constexpr int MaxCachedQueryMatches = 5000;
    /// The ranges ("from|to") and queries ("source\x1fcurrentPath") kept, most recent first (tests).
    QStringList cachedLists() const;

    // ---- QueryHost ----------------------------------------------------------------------------
    Q_INVOKABLE QVariantMap runQuery(const QString &source, const QString &currentPath = QString()) override;
    Q_INVOKABLE bool setTaskStatus(const QString &path, int line, const QString &status) override;

    // ---- Journals ---------------------------------------------------------------------------
    /// Path of the daily note for "YYYY-MM-DD" (whether it exists or not).
    Q_INVOKABLE QString journalPath(const QString &date) const;
    /// The daily note for `date`, created from the journal template when missing — for edits
    /// that go into it (quick capture, a task added from the calendar); opening a day uses
    /// journalFileFor() and never creates the note. "" for an invalid date or on I/O errors.
    Q_INVOKABLE QString ensureJournal(const QString &date);
    /// The note of the day `date` ("YYYY-MM-DD"): its existing file (also under another name the
    /// journal folder accepts), else where it will be created — never writes ("" for a bad date).
    QString journalFileFor(const QString &date) const;
    /// Append `text` as a new block ("- text", further lines indented) to today's journal.
    /// Returns the journal's path ("" when nothing was written).
    Q_INVOKABLE QString captureToJournal(const QString &text);
    /// Append ready-made blocks (lines joined with "\n", written as given: a shared file's
    /// indentation is kept; notebook::sharedFileBlock) to today's journal. Returns its path ("" when
    /// nothing was written).
    QString appendJournalBlocks(const QString &blocks);
    /// Quick capture: like captureToJournal(), but a date in the first line makes it a task with
    /// a reminder (with a time: "Buy milk tomorrow at 6pm" -> "- [ ] Buy milk ⏰ 2026-10-15
    /// 18:00") or a due date (without one: "📅 2026-10-16"); see notebook/capture.h.
    /// Returns the journal's path ("" when nothing was written).
    Q_INVOKABLE QString quickCapture(const QString &text);
    /// What quickCapture(text) would write: { kind: "note" | "reminder" | "due" | "", text (without
    /// the date), date ("YYYY-MM-DD" or ""), time ("HH:mm" or ""), when (human: "jutro 18:00"),
    /// line (the first line written) } — for a live preview in the capture dialog.
    Q_INVOKABLE QVariantMap quickCapturePreview(const QString &text) const;
    /// A habit's day (docs/SYNTAX.md §5 "Habits", notebook::setHabitCheck()): habit
    /// `name` in the daily note of `date` ("YYYY-MM-DD") set to `state` ("done" | "skipped" |
    /// "open"): the day's check ticked, or a new line "- [x] Name #nawyk ✅ today" next to the
    /// day's other checks; the note is created from the journal template only when something is
    /// written. `pageLink`: the habit is a page (the new line links to it). Returns { ok, changed,
    /// path, line, before ("open" | "done" | "skipped"; "" when the day had no check) }; ok false
    /// (and errorOccurred) when it could not be written.
    Q_INVOKABLE QVariantMap setHabitCheck(const QString &name, const QString &date, const QString &state,
                                          bool pageLink = false);
    /// A new habit: "- Name\n  habit:: schedule" appended to `page` (a path; "" = the page
    /// "Habits" (Polish notes: "Nawyki") where new pages go, created when missing). Returns the
    /// page's path ("" on failure: an empty name, a write error).
    Q_INVOKABLE QString addHabit(const QString &name, const QString &schedule, const QString &page = QString());
    /// Daily notes, newest first: [{ date, path, title }] from `offset`, at most `limit`.
    Q_INVOKABLE QVariantList journals(int offset = 0, int limit = 50) const;
    /// Days "YYYY-MM-DD" within [from, to] that have a daily note (calendar dots).
    Q_INVOKABLE QStringList journalDates(const QString &from, const QString &to) const;
    /// BDatePicker marks of the days with a daily note among `cells` (a month grid's cells
    /// [{ date, … }], first to last): { "YYYY-MM-DD": ["journal"] } (JournalsView's "Go to day").
    Q_INVOKABLE QVariantMap journalMarks(const QVariantList &cells) const;
    /// A weekly or monthly note (docs/SYNTAX.md §1 "Weekly and monthly notes"; `period` "week" |
    /// "month", `date` any day of it, "YYYY-MM-DD"): { period, start, end ("YYYY-MM-DD"), week (the
    /// ISO week; 0 for a month), path (its existing file, else where it will be created — never
    /// written here: the editor creates it with its first change, from `template`), exists, label
    /// ("Week 45 (Nov 2–8, 2026)", "November 2026"), template (the period's template when
    /// the file is there, else "") }; {} for an unknown period or a bad date.
    Q_INVOKABLE QVariantMap periodicNote(const QString &period, const QString &date) const;
    /// periodicNote() of the note at `path` when it is a weekly or monthly note, else {}.
    Q_INVOKABLE QVariantMap periodicNoteOf(const QString &path) const;
    /// The start "YYYY-MM-DD" of the week / month a typed phrase names ("this week", "2026-W45",
    /// "November", "next month", or Polish "ten tydzień", "listopad", "w przyszłym miesiącu":
    /// periodic::parsePeriodPhrase), "" when it names none.
    Q_INVOKABLE QString parsePeriod(const QString &period, const QString &text) const;
    /// The starts "YYYY-MM-DD" of the weeks / months overlapping [from, to] that have a note
    /// (the calendar's marks on week numbers and the month's title).
    Q_INVOKABLE QStringList periodicNoteStarts(const QString &period, const QString &from, const QString &to) const;

    // ---- Configuration ----------------------------------------------------------------------
    /// Save .brulion/config.json from a map with the keys of vaultConfig() (missing keys keep
    /// their value; journalMode / pageMode "outline" | "document"). False for an unusable journal
    /// file format, an unknown mode or on I/O errors.
    Q_INVOKABLE bool setConfig(const QVariantMap &config);
    /// Problems found in .brulion/config.json or in an Obsidian vault's settings (English, for
    /// the settings page).
    Q_INVOKABLE QStringList configWarnings() const;
    /// The config.json keys taken from the Obsidian vault's `.obsidian/` folder while the
    /// notebook has no .brulion/config.json (Vault::configAdoptedFromObsidian()).
    Q_INVOKABLE QStringList configFromObsidian() const;
    /// Write the settings in use (also those taken from `.obsidian/`) to .brulion/config.json,
    /// for devices that do not see `.obsidian/` (the browser with Nextcloud).
    Q_INVOKABLE bool saveAdoptedConfig();

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
    /// The app left the foreground (another window has the focus, the phone shows another app)
    /// or came back. Coming back rescans the vault: other programs may have rewritten notes in
    /// place meanwhile, which the vault's folder watches do not report (it watches only the
    /// files open in editors themselves), and hands the reminders to Android again
    /// (WorkspaceReminders::resendSoon()). Connected to QGuiApplication::applicationStateChanged.
    void applicationStateChanged(Qt::ApplicationState state);
    /// Folded blocks of a page on this device (the editor's PageController.foldState: lines of
    /// folded blocks); follows renames, forgotten when the page is deleted.
    Q_INVOKABLE QVariantList foldState(const QString &path) const;
    /// Remember PageController.foldState for `path` (an empty list forgets it).
    Q_INVOKABLE void setFoldState(const QString &path, const QVariantList &lines);

Q_SIGNALS:
    void openChanged();
    void indexingChanged();
    void indexProgressChanged();
    void readyChanged();
    void trashChanged();
    void revisionChanged();
    void configChanged();
    void favoritesChanged();
    void recentPagesChanged();
    void webPicturesShownChanged();
    void taskStatusHistoryChanged();
    /// Files changed (paths, in change order) — after the index was updated.
    void filesChanged(const QStringList &paths);
    /// A path was renamed or moved (pages: links were rewritten already).
    void pathRenamed(const QString &oldPath, const QString &newPath);
    /// A folder was renamed or moved (after the pathRenamed() of the files in it): routes, the
    /// tree's expanded folders and views showing it follow.
    void folderRenamed(const QString &oldPath, const QString &newPath);
    /// A path was deleted (moved to the trash or removed by another program).
    void pathRemoved(const QString &path);
    /// navigate() from the editor or a view; AppController handles it.
    void navigationRequested(const QVariantMap &route, bool newPane);
    /// Something failed that the user should know about (translated message).
    void errorOccurred(const QString &message);
    /// Something the user should know that is not an error (translated; an info toast).
    void noticeOccurred(const QString &message);
    /// requestMoveImpact(path, targetFolder) is known: moveImpact()'s map.
    void moveImpactReady(const QString &path, const QString &targetFolder, const QVariantMap &impact);

private:
    void onVaultChanged(const QList<brulion::VaultChange> &changes);
    void indexChunk();
    void indexFile(const QString &path);
    /// The loader's results are ready: merge them (indexChunk()).
    void onIndexResultsReady();
    QStringList storedList(const QString &key) const;
    void storeList(const QString &key, const QStringList &list);
    void forgetPath(const QString &path);
    void renamePathInLists(const QString &oldPath, const QString &newPath);
    /// Append a block (lines joined with "\n") to today's journal; returns its path.
    QString appendToTodaysJournal(const QString &block);
    void indexingFinished();
    void setReady(bool wasReady);
    QVariantMap queryItem(const brulion::QueryItem &item) const;
    /// The translated message of a refused page / folder operation (notebook/pages.h).
    QString pathProblemMessage(const notebook::PathProblem &problem) const;
    /// What relocate(from, to) rewrites: { ok, error, path, links, tags, notes, specials }.
    QVariantMap relocationImpact(const QString &from, const QString &to);
    /// notebook::RelocationImpact as moveImpact() returns it.
    QVariantMap impactMap(const notebook::RelocationImpact &impact) const;
    /// The impact of moving `from` to `to` known for this state of the notebook (the index's
    /// version, the vault's revision); an empty map when there is none.
    QVariantMap cachedImpact(const QString &from, const QString &to);
    void storeImpact(const QString &from, const QString &to, const QVariantMap &impact);
    /// One event-loop turn of requestMoveImpact().
    void moveImpactStep();
    /// Move or rename `from` to `to` keeping every link and tag (renamePage, renameFolder,
    /// movePath: notebook::relocate()); returns `to` or "" (an error was emitted). Settings naming
    /// `from` or a path in it follow it (a notice says so).
    QString relocate(const QString &from, const QString &to);
    /// Folders produce no Vault change of their own (only files do): tell the lists and the tree,
    /// and for the user's own changes (`byUser`) the sync (an empty folder travels too).
    void notifyFolderChange(const QStringList &paths, bool byUser = true);

    std::unique_ptr<Vault> m_vault;
    /// Copies importAttachment() stored in this session that discardImport() may still remove.
    QSet<QString> m_importedCopies;
    std::unique_ptr<VaultIndex> m_index;
    /// CalendarCaches of the last ranges calendarItems() was asked for, QueryCaches of the last
    /// queries runQuery() ran (workspacequery.cpp; dropped by close()).
    struct ListCaches;
    mutable std::shared_ptr<ListCaches> m_listCaches;
    ListCaches &listCaches() const;
    /// Relocation impacts (ok ones) for one state of the notebook: key "from\nto".
    struct ImpactCache {
        int version = -1;
        int revision = -1;
        QHash<QString, QVariantMap> maps;
    };
    ImpactCache m_impacts;
    struct PendingImpact;
    std::shared_ptr<PendingImpact> m_pendingImpact; ///< requestMoveImpact() in progress
    QTimer m_impactTimer;
    /// The index loader's QThreadPool (indexworkers.h; none on WebAssembly), a child: deleted
    /// after the loader.
    QObject *m_indexPool = nullptr;
    /// Loads the index after open(): parses on worker threads, merges in slices (m_indexTimer).
    std::unique_ptr<notebook::IndexLoader> m_loader;
    /// Keeps the index cache on the device once the index is loaded (docs/PERFORMANCE.md, L2).
    std::unique_ptr<notebook::IndexCacheKeeper> m_cacheKeeper;
    notebook::IndexCacheKeeper::Policy m_cachePolicy;
    QString m_indexCachePath; ///< "" when the notebook keeps none
    QTimer m_indexTimer;
    QElapsedTimer m_indexClock; ///< since open() (the event log's "Index ready" line)
    int m_revision = 0;
    mutable int m_trashCount = -1;
    // Children of this object (parented: QML never takes them over); detached by close().
    WorkspaceReminders *m_reminders = nullptr;
    WorkspaceSync *m_sync = nullptr;
    WorkspaceBackups *m_backups = nullptr;
    ReplaceSession *m_replacing = nullptr;
    std::function<QDateTime()> m_clock;
    QString m_language = QStringLiteral("pl");
    bool m_dataUrlAssets = false;
    /// Pictures from the internet: the cache, the fetcher and their state (workspacepictures.cpp;
    /// a child object, created on first use).
    class WebPictures;
    WebPictures *webPictures() const;
    mutable WebPictures *m_webPictures = nullptr;
    bool m_webPicturesShown = false;
    bool m_taskStatusHistory = false;
    QString m_webPictureCacheDir;
    qint64 m_webPictureCacheBytes = 0;
    std::function<bool(const QString &)> m_webPictureHostCheck;
    QString m_webPageScheme;
    bool m_remindersPaused = false;
    bool m_wasInactive = false; ///< the app left the foreground since the last rescan
    /// The thumbnails of embedded PDFs (workspacepdf.cpp; a child, created on first use) and the
    /// vault paths asked for, by their absolute paths (PdfThumbnails::changed names those).
    pdfview::PdfThumbnails *pdfThumbnails() const;
    mutable pdfview::PdfThumbnails *m_pdfThumbnails = nullptr;
    mutable QHash<QString, QString> m_pdfAsked;
    QString m_pdfThumbnailDir;
    /// A PDF changed or went: its open readers are closed, its thumbnails drawn again on the next ask.
    void pdfFileChanged(const QString &path, const QString &oldPath = QString());
    /// Before Brulion itself writes, moves or deletes `path` (a file or a folder): the idle readers
    /// of PDFs there closed (pdfview::PdfReaderPool) — Windows cannot replace, move or delete a file
    /// that is open.
    void releasePdfReaders(const QString &path);
    /// The vault path an embed's target names from `fromNote` (as the editor resolves it: the
    /// index's link resolution, else the attachments folder).
    QString attachmentPathOf(const QString &target, const QString &fromNote) const;
    /// The drawing file `drawing` just stored, embedded as a block of its own in `note` beside the
    /// block `embedsIt` accepts nearest line `line` (after it, or before), else in today's journal
    /// (addDrawingOnPdfPage(), addDrawingBeside()).
    QVariantMap embedNewDrawing(const QString &drawing, const QString &note, int line,
                                const std::function<bool(QStringView)> &embedsIt, bool before);
    /// drawingUrl(): the embedded pictures of Excalidraw plugin drawings by the drawing's path, with
    /// the version of the drawing they were read from.
    mutable QHash<QString, std::pair<QString, QList<drawing::EmbeddedFile>>> m_drawingPictures;
    mutable QHash<QString, QUrl> m_dataUrls;
    mutable QHash<QString, QSize> m_dataUrlSizes; // pictures among them (picturesize.h)
    /// Pages shown by editors (editorOpened()), and by how many.
    QHash<QString, int> m_editorFiles;
};

} // namespace brulion::shell
