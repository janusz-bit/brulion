// Contract between the block editor (src/app/editor, qml/editor) and the rest of the app.
//
// The editor never touches the vault, the index or the platform directly: everything goes
// through an EditorHost. The shell's Workspace implements it for the real app; the editor
// playground and the tests use an in-memory fake. Keep this interface small and stable —
// the editor and the shell are developed in parallel against it.
#pragma once

#include "picturesize.h"

#include <QObject>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace brulion {

class EditorHost : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~EditorHost() override = default;

    // --- Files -------------------------------------------------------------------------------
    /// Current content of a vault file ("" + ok=false when missing).
    virtual QString read(const QString &path, bool *ok = nullptr) = 0;
    /// Save content (the host makes it atomic, records history, updates the index).
    virtual bool write(const QString &path, const QString &content) = 0;
    /// Result of writeIfUnchanged().
    enum class WriteResult { Written, Changed, Failed };
    /// Save `content` only when the file still holds `expected` — the text the editor last read
    /// or wrote — or does not exist: compare-and-swap against other programs (an agent's
    /// brulion-cli, Syncthing) whose change has not reached the editor yet. Changed: nothing was
    /// written; the editor takes the other version in (reload, three-way merge) and saves again.
    /// Default: read(), compare, write() (hosts with a vault compare on the disk itself).
    virtual WriteResult writeIfUnchanged(const QString &path, const QString &content, const QString &expected)
    {
        bool ok = false;
        const QString current = read(path, &ok);
        if (ok && current != expected)
            return WriteResult::Changed;
        return write(path, content) ? WriteResult::Written : WriteResult::Failed;
    }
    /// An editor shows `path` now / no longer shows it (PageController: loading, following a
    /// rename, another path or host, destruction). The host may watch the files editors show
    /// for programs that rewrite them in place; several editors can show one file. Default:
    /// nothing.
    virtual void editorOpened(const QString & /*path*/) { }
    virtual void editorClosed(const QString & /*path*/) { }

    // --- Index-backed helpers (all return plain QVariant data for QML) -------------------------
    /// Page completions for `[[`: list of { path, title, alias?, folder }.
    virtual QVariantList suggestPages(const QString &prefix, int limit = 20) const = 0;
    /// Block completions for `((`: list of { path, line, text, blockId? }.
    virtual QVariantList suggestBlocks(const QString &text, int limit = 20) const = 0;
    /// Completions for `#` (a tag names a page, docs/SYNTAX.md §3): list of { tag (the name to
    /// write, without '#'), count (tagged blocks), kind ("tag": in use | "page": a page no tag
    /// names yet), path?, title?, folder? (the page it names) } — tags in use first.
    virtual QVariantList suggestTags(const QString &prefix, int limit = 20) const = 0;
    /// Resolve a wiki-link target seen from `fromPath`; "" when unresolved.
    virtual QString resolveLink(const QString &target, const QString &fromPath) const = 0;
    /// The shortest wiki-link target that reaches `path` from `fromPath` ("Dom", or "Projekty/Dom"
    /// when the title alone names another page) — what `[[` completion writes; "" when no link
    /// can. Default: the title when resolveLink() takes it to `path`, else the path without .md.
    virtual QString linkTarget(const QString &path, const QString &fromPath) const
    {
        const qsizetype slash = path.lastIndexOf(u'/');
        QString stem = path;
        if (stem.endsWith(QLatin1String(".md"), Qt::CaseInsensitive))
            stem.chop(3);
        const QString title = stem.mid(slash + 1);
        return resolveLink(title, fromPath) == path ? title : stem;
    }
    /// Text of the block with this id (for ((id)) rendering); "" when unknown.
    virtual QString blockText(const QString &blockId) const = 0;
    /// A block somewhere in the vault has this id (also one whose text is empty): new ids avoid
    /// it, and a link to it is followed to wherever the block is. Default: blockText() is not "".
    /// Right after opening the index may be incomplete: finishIndexing() first.
    virtual bool blockIdTaken(const QString &blockId) const { return !blockText(blockId).isEmpty(); }
    /// Text of the block a `[[Page#^id]]` link to the page at vault path `path` leads to: the
    /// page's block with that id, else — moved to another page — the vault's block with it (where
    /// the shell's route resolution opens it); "" when unknown. Default: blockText(blockId).
    virtual QString linkedBlockText(const QString &path, const QString &blockId) const
    {
        Q_UNUSED(path)
        return blockText(blockId);
    }
    /// Complete the vault index now. Right after opening it is built in the background, and what
    /// is not indexed yet looks missing: called before deciding that a block id is free (a new
    /// one) or nowhere (a link to it). Default: nothing to do.
    virtual void finishIndexing() { }
    /// Id of the block at `line` of `path`, writing ` ^id` into that file when missing.
    virtual QString ensureBlockId(const QString &path, int line) = 0;
    /// Templates: list of { path, title }.
    virtual QVariantList templates() const = 0;

    // --- Assets ------------------------------------------------------------------------------
    /// URL the UI can load an attachment from (file:// on desktop/Android, blob/data on WASM).
    virtual QUrl assetUrl(const QString &path) const = 0;
    /// A picture attachment's pixel size when the host has it at hand (the browser: assetUrl()
    /// read the file for its data: URL); invalid otherwise — the editor then reads a local file's
    /// header itself (picturesize.h).
    virtual QSize assetPixelSize(const QString &path) const
    {
        Q_UNUSED(path);
        return {};
    }
    /// This device can decode the picture at `path` (its format, picturesize.h): the editor's
    /// embeds name a format it cannot show (WebP, AVIF without Qt's image plugins) instead of
    /// failing to load it. Hosts (tests) may say less.
    virtual bool canDecodePicture(const QString &path) const { return editor::canDecodePictureFormat(path); }
    /// Store pasted/dropped bytes in the attachments folder; returns the new vault path.
    virtual QString saveAttachment(const QString &fileName, const QByteArray &data) = 0;

    // --- Pictures from the internet (`![alt](https://…)`, docs/SYNTAX.md §2) -------------------
    // The UI never loads them itself: the host fetches them (when the user allows it) into the
    // device's cache and hands out the cached file. webPictureChanged() tells when one changed.
    /// The picture at the web URL `url` as written: { state: "ask" (not loaded — the user did not
    /// allow it yet), "loading", "ready" (url: a local URL of the cached picture, width, height
    /// when known) or "failed" (error: a sentence for the user) }. In the "always show" setting a
    /// picture not cached yet is fetched by asking ("loading"). Default: "ask" — nothing loads.
    virtual QVariantMap webPicture(const QString &url) const
    {
        Q_UNUSED(url);
        return {{QStringLiteral("state"), QStringLiteral("ask")}};
    }
    /// The user wants to see the picture (or store it: `forDownload`, also when pictures are not
    /// shown): fetch it unless cached; webPictureChanged(url) when it is ready or failed.
    virtual void loadWebPicture(const QString &url, bool forDownload = false)
    {
        Q_UNUSED(url);
        Q_UNUSED(forDownload);
    }
    /// The cached bytes of a ready picture and its format ("png", webpicture::sniffFormat());
    /// empty when not ready.
    virtual QByteArray webPictureData(const QString &url, QString *format) const
    {
        Q_UNUSED(url);
        Q_UNUSED(format);
        return {};
    }

    // --- Context -----------------------------------------------------------------------------
    /// Vault configuration as a map (journalsFolder, journalFileFormat, attachmentsFolder,
    /// templatesFolder, defaultReminderTime "HH:mm", weekStartsOn 1..7, journalMode / pageMode
    /// "outline" | "document" — the note modes, docs/SYNTAX.md §4).
    virtual QVariantMap vaultConfig() const = 0;

    // --- Clipboard (the block selection, docs/UI.md "Selecting and copying") -------------------
    /// The text/html flavour of copied blocks: `markdown` (blocks of the note at `fromPath`) as
    /// escaped, semantic HTML — lists, headings, checkboxes, tables, links — for mail and office
    /// apps; pictures as links, never data: URLs; links to notes as their text. "" when the host
    /// cannot render it (the clipboard then has the Markdown only). It should contain
    /// `<meta name="generator" content="Brulion">` (io::ClipboardHtmlMarker): a paste recognises
    /// its own copy by it and takes the Markdown text, not the HTML — also after a restart.
    virtual QString renderHtml(const QString &markdown, const QString &fromPath) const
    {
        Q_UNUSED(markdown);
        Q_UNUSED(fromPath);
        return {};
    }
    /// Pasted HTML (a browser, an office app) as Markdown; "" when the host cannot convert it (the
    /// clipboard's plain text is pasted then).
    virtual QString htmlToMarkdown(const QString &html) const
    {
        Q_UNUSED(html);
        return {};
    }

    // --- Navigation --------------------------------------------------------------------------
    /// Open a route ({view: "page", path, line?, blockId?} / {view: "tag", tag} — the page the
    /// tag names / {view: "journal", date: "YYYY-MM-DD"}, see docs/UI.md); newPane = side
    /// panel/new tab.
    virtual void navigate(const QVariantMap &route, bool newPane = false) = 0;
    /// Open an external http(s)/mailto link with the platform handler.
    virtual void openUrl(const QUrl &url) = 0;

Q_SIGNALS:
    /// A file changed outside this editor (sync, Syncthing, another editor/view, import).
    void fileChanged(const QString &path);
    /// The index changed (link targets, block texts, tags may render differently).
    void indexChanged();
    /// The picture from the internet at `url` (as written) changed state (webPicture()); "" —
    /// all of them (the setting changed).
    void webPictureChanged(const QString &url);
};

} // namespace brulion
