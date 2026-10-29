// Trilium Notes importer (zadam/trilium, TriliumNext, Trilium 0.10x): the ZIP export of a note
// tree, format "HTML" or "Markdown". `!!!meta.json` describes the tree — titles, types, labels and
// relations, attachments, clones, the file of every note — so the import walks the meta, never the
// (lossy) file names. Content conversion: trilium.cpp.
//
// Mapping (see docs/IMPORT.md for the user's view):
// * A note with content is a page `<parent folders>/<title>.md`; a note with children gets a
//   folder of the same name next to its page (or only the folder when it has no content). The
//   branch prefix is part of the name ("Część I - Rozdział 1"), the bare title an alias. Sibling
//   names are unique case-insensitively ("Zadania", "Zadania 2"), never a name the vault ignores
//   or a daily note's path; the dialect has no order property, so Trilium's order shows only in
//   the child lists.
// * Below its content every page lists its children in Trilium's order (as Trilium shows its note
//   list), after a `---` rule: links, images embedded; `#hideChildrenOverview`: only clone
//   placements, image and file notes. Books (collections) are such lists; so are notes without
//   content that carry labels, are linked to or hold clones.
// * Day notes (#dateNote) are daily notes (a custom title suffix a heading); the calendar, year,
//   quarter, month and week notes are folders only (pages when they have content or are linked
//   to). Children of a day note are pages in the mirrored folder, listed in the day.
// * Clones: the note is imported once, at its first place in the tree; every other place is a
//   link to it in the child list of that place's parent.
// * Code notes are pages with one fenced block (Markdown code notes: their Markdown); mermaid a
//   `mermaid` block; a canvas one drawing (docs/SYNTAX.md §2 "Drawings"): Trilium's SVG picture
//   with the Excalidraw scene embedded (the pictures pasted into the canvas inside the scene) —
//   Brulion's own picture of the scene when Trilium's is missing or empty —, which Brulion's
//   drawing editor and excalidraw.com open; mind map and spreadsheet (and a canvas whose content
//   is no scene) their rendered image with the source kept as an attachment (mind maps also as a
//   nested list) — an SVG Qt cannot draw (HTML labels, CSS) is linked instead of embedded;
//   relation maps a list of their notes; web views a link. Image and file notes are attachments.
//   Saved searches, note maps, rendered notes and launchers are skipped and listed in a warning;
//   protected notes exported without a protected session have no content and are counted in a
//   warning.
// * Labels: without a value -> tags, with a value -> properties; relations -> properties linking
//   to the target page. Trilium's own labels and relations (iconClass, archived, sorted, calendar
//   labels, scripts, sharing, …) are dropped; a template's attribute definitions become empty
//   properties. Inheritable attributes stay on their note.
// * Links name pages by their vault path, the title or the link's text as the alias
//   (`[[Import/Trilium/Osoby/Jan Kowalski|Jan Kowalski]]`): Trilium's references are exact, a
//   bare title would resolve to any page of that title (an earlier import, the user's own).
//   References are resolved against the meta: relative links, links to other clone paths,
//   attachment names, include-note boxes (by note id); links to notes outside the export keep
//   only their text.
// * Bounded work for hostile exports: notes over MaxTextBytes or MaxListItems are kept as files,
//   folder paths stop at MaxFolderDepth / MaxFolderBytes, sibling names and frontmatter are
//   built in linear time.
#include "drawing/drawing.h"
#include "drawing/excalidraw.h"
#include "drawing/svgwrite.h"
#include "io/htmlmarkdown.h"
#include "io/htmlutil_p.h"
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/messages_p.h"
#include "io/trilium_p.h"
#include "io/zip.h"
#include "markdown/frontmatter.h"
#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <limits>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype MaxMetaBytes = qsizetype(256) << 20;
constexpr int MaxNotes = 500'000;
/// Deeper notes share the folder of their ancestor at this level (file systems limit paths)…
constexpr int MaxFolderDepth = 40;
/// … and so do notes whose folder path (relative to the target folder) would get longer than
/// this many bytes of UTF-8 (Linux and Android allow 4096 for a whole path).
constexpr qsizetype MaxFolderBytes = 1024;
/// Larger text notes are kept as files (converting them would take very long).
constexpr qsizetype MaxTextBytes = qsizetype(4) << 20;
/// Text notes with more list items are kept as files too: QTextDocument needs time quadratic in
/// the length of a list.
constexpr int MaxListItems = 5'000;
/// Larger (or binary) code notes are kept as files.
constexpr qsizetype MaxCodeBytes = qsizetype(4) << 20;
constexpr int MaxListedTitles = 10;
constexpr int MaxMindMapDepth = 32;

const QString MetaName = u"!!!meta.json"_s;

/// `!!!meta.json` of the export: the shallowest one (at the top of a ZIP, or in the folder of an
/// unpacked and re-packed export).
const InputFile *findMeta(const QList<InputFile> &files)
{
    const InputFile *best = nullptr;
    qsizetype bestDepth = std::numeric_limits<qsizetype>::max();
    for (const InputFile &f : files) {
        if (paths::basename(f.path) != MetaName)
            continue;
        const qsizetype depth = f.path.count(u'/');
        if (depth < bestDepth) {
            best = &f;
            bestDepth = depth;
        }
    }
    return best;
}

bool looksLikeMeta(const QByteArray &data)
{
    return data.trimmed().startsWith('{') && data.contains("\"files\"") && data.contains("\"formatVersion\"");
}

bool hasScheme(QStringView text)
{
    static const QRegularExpression re(u"^[A-Za-z][A-Za-z0-9+.-]{1,31}:"_s);
    return re.matchView(text).hasMatch();
}

/// Text of a note file: UTF-8 as Trilium writes it (a BOM skipped). A damaged byte becomes U+FFFD
/// instead of the whole note being read in a single-byte code page (decodeText()).
QString utf8Text(const QByteArray &data)
{
    if (data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF"))
        return decodeText(data); // UTF-16 with a BOM: not Trilium's, but readable
    return QString::fromUtf8(data.startsWith("\xEF\xBB\xBF") ? data.sliced(3) : data);
}

/// A title as one line: control characters and line breaks become spaces.
QString cleanTitle(const QString &title)
{
    QString out = title;
    for (QChar &c : out) {
        const char16_t u = c.unicode();
        if (u < 0x20 || u == 0x7f || u == 0x2028 || u == 0x2029)
            c = u' ';
    }
    return out.simplified();
}

/// Labels with a meaning for Trilium itself (packages/commons/src/lib/builtin_attributes.ts),
/// not the user's data.
bool isSystemLabel(const QString &name)
{
    static const QSet<QString> names = {
            u"inbox"_s,
            u"disableVersioning"_s,
            u"versioningLimit"_s,
            u"calendarRoot"_s,
            u"archived"_s,
            u"excludeFromExport"_s,
            u"disableInclusion"_s,
            u"appCss"_s,
            u"appTheme"_s,
            u"appThemeBase"_s,
            u"customFont"_s,
            u"hidePromotedAttributes"_s,
            u"readOnly"_s,
            u"autoReadOnlyDisabled"_s,
            u"cssClass"_s,
            u"iconClass"_s,
            u"keyboardShortcut"_s,
            u"run"_s,
            u"runOnInstance"_s,
            u"runAtHour"_s,
            u"customRequestHandler"_s,
            u"customResourceProvider"_s,
            u"widget"_s,
            u"similarNotesWidgetDisabled"_s,
            u"workspace"_s,
            u"searchHome"_s,
            u"sqlConsoleHome"_s,
            u"llmChatHome"_s,
            u"datePattern"_s,
            u"weekPattern"_s,
            u"enableWeekNote"_s,
            u"monthPattern"_s,
            u"quarterPattern"_s,
            u"yearPattern"_s,
            u"enableQuarterNote"_s,
            u"dateNote"_s,
            u"weekNote"_s,
            u"monthNote"_s,
            u"quarterNote"_s,
            u"yearNote"_s,
            u"pageSize"_s,
            u"viewType"_s,
            u"mapRootNoteId"_s,
            u"mapExcludeRelation"_s,
            u"mapIncludeRelation"_s,
            u"excludeFromNoteMap"_s,
            u"bookmarkFolder"_s,
            u"bookmarked"_s,
            u"sorted"_s,
            u"sortDirection"_s,
            u"sortFoldersFirst"_s,
            u"sortNatural"_s,
            u"sortLocale"_s,
            u"top"_s,
            u"bottom"_s,
            u"fullContentWidth"_s,
            u"subtreeHidden"_s,
            u"expanded"_s,
            u"displayRelations"_s,
            u"hideRelations"_s,
            u"titleTemplate"_s,
            u"template"_s,
            u"snippet"_s,
            u"snippetDescription"_s,
            u"textSnippet"_s,
            u"textSnippetDescription"_s,
            u"aiQuickAction"_s,
            u"toc"_s,
            u"color"_s,
            u"keepCurrentHoisting"_s,
            u"executeButton"_s,
            u"executeDescription"_s,
            u"executeTitle"_s,
            u"newNotesOnTop"_s,
            u"clipperInbox"_s,
            u"clipType"_s,
            u"sentFromSender"_s,
            u"hideChildrenOverview"_s,
            u"mediaNotesPlayMode"_s,
            u"collection"_s,
            u"webViewSrc"_s,
            u"hideHighlightWidget"_s,
            u"iconPack"_s,
            u"docName"_s,
            u"docUrl"_s,
            u"language"_s,
            u"originalFileName"_s,
            u"command"_s,
            u"displayMode"_s,
            u"searchString"_s,
            u"ancestorDepth"_s,
            u"orderBy"_s,
            u"orderDirection"_s,
            u"limit"_s,
            u"fastSearch"_s,
            u"includeArchivedNotes"_s,
            u"debug"_s,
            u"launcherType"_s,
            u"builtinWidget"_s,
            u"scriptInLauncherContent"_s,
            u"baseSize"_s,
            u"growthFactor"_s,
            u"desktopOnly"_s,
            u"electronOnly"_s,
            u"serverOnly"_s,
            u"beta"_s,
            u"oneNotePageId"_s,
            u"oneNoteSectionId"_s,
            u"oneNoteImportFailed"_s,
            u"stateId"_s,
            u"markdownSymbol"_s,
            u"isCompleted"_s,
            u"isHidden"_s,
            u"maxNestingDepth"_s,
            u"includeArchived"_s,
            u"boardCardRedirectTo"_s,
            u"tabWidth"_s,
            u"indentWithTabs"_s,
            u"wrapLines"_s,
            u"printLandscape"_s,
            u"printPageSize"_s,
            u"printScale"_s,
            u"printMargins"_s,
            u"mapType"_s,
            // zadam/trilium 0.6x
            u"bookZoomLevel"_s,
            u"hideIncludedImages"_s,
            u"manualTransactionHandling"_s,
            u"cssClassPrefix"_s,
    };
    // "label:x" / "relation:x" (attribute definitions), "board:…", "calendar:…", "disabled:…";
    // "noteInfoWidgetDisabled" and the like.
    static const QRegularExpression family(u"^(?:share|workspace|runOn)[A-Z]|WidgetDisabled$"_s);
    return names.contains(name) || name.contains(u':') || family.match(name).hasMatch();
}

/// Relations with a meaning for Trilium itself (links it records automatically, scripts,
/// templates, sharing).
bool isSystemRelation(const QString &name)
{
    static const QSet<QString> names = {
            u"internalLink"_s,     u"imageLink"_s,
            u"relationMapLink"_s,  u"includeNoteLink"_s,
            u"internalBookmark"_s, u"template"_s,
            u"inherit"_s,          u"dateTemplate"_s,
            u"weekTemplate"_s,     u"monthTemplate"_s,
            u"quarterTemplate"_s,  u"yearTemplate"_s,
            u"widget"_s,           u"renderNote"_s,
            u"searchScript"_s,     u"ancestor"_s,
            u"target"_s,           u"script"_s,
            u"hoistedNote"_s,      u"shareCss"_s,
            u"shareLogo"_s,        u"shareOpenGraphImage"_s,
            u"shareJs"_s,          u"shareHtml"_s,
            u"shareTemplate"_s,    u"shareFavicon"_s,
            u"includeMapLink"_s,
    };
    static const QRegularExpression family(u"^runOn[A-Z]"_s);
    return names.contains(name) || name.contains(u':') || family.match(name).hasMatch();
}

/// Note types without a counterpart in Brulion (skipped, listed in a warning).
bool isSkippedType(const QString &type)
{
    static const QSet<QString> types = {u"search"_s, u"noteMap"_s,       u"note-map"_s, u"render"_s, u"launcher"_s,
                                        u"doc"_s,    u"contentWidget"_s, u"llmChat"_s,  u"aiChat"_s};
    return types.contains(type);
}

/// Types imported as pages with their own conversion (everything else unknown counts as text, as
/// in Trilium).
bool isSpecialType(const QString &type)
{
    static const QSet<QString> types = {u"code"_s,        u"mermaid"_s,     u"canvas"_s,       u"mindMap"_s,
                                        u"spreadsheet"_s, u"relationMap"_s, u"relation-map"_s, u"webView"_s,
                                        u"web-view"_s,    u"book"_s,        u"image"_s,        u"file"_s};
    return types.contains(type);
}

bool isContainerLabel(const QString &name)
{
    return name == u"calendarRoot" || name == u"workspaceCalendarRoot" || name == u"yearNote" || name == u"quarterNote"
           || name == u"monthNote" || name == u"weekNote";
}

/// The rendered image Trilium stores with a diagram note ("mermaid-export.svg", …).
bool isExportImage(const QString &noteType, const QString &attachmentTitle)
{
    static const QRegularExpression re(u"^(?:mermaid|canvas|mindmap|spreadsheet)-export\\.(?:svg|png)$"_s,
                                       QRegularExpression::CaseInsensitiveOption);
    return (noteType == u"mermaid" || noteType == u"canvas" || noteType == u"mindMap" || noteType == u"spreadsheet")
           && re.match(attachmentTitle).hasMatch();
}

/// Can Brulion show the rendered image of a diagram? Mermaid's and mind maps' SVGs draw their
/// labels in `<foreignObject>` (HTML) and style everything with CSS: Qt's SVG renderer shows
/// them as black shapes without text. Such an image is linked, not embedded.
bool isRenderableExport(const QString &noteType, const QString &fileName, const QByteArray &data)
{
    if (!fileName.endsWith(u".svg"_s, Qt::CaseInsensitive))
        return true;
    if (data.contains("<foreignObject") || data.contains("<foreignobject"))
        return false;
    return (noteType != u"mermaid" && noteType != u"mindMap") || !data.contains("<style");
}

/// Image files Brulion shows in a list of child notes (others are linked).
bool isImageFile(const QString &path)
{
    static const QSet<QString> ext = {u".png"_s, u".jpg"_s,  u".jpeg"_s, u".gif"_s, u".webp"_s, u".svg"_s,
                                      u".bmp"_s, u".avif"_s, u".heic"_s, u".tif"_s, u".tiff"_s, u".ico"_s};
    return ext.contains(paths::extension(path).toLower());
}

/// A URL as a Markdown link destination.
QString urlDestination(const QString &url)
{
    QString out;
    for (const QChar c : url) {
        switch (c.unicode()) {
        case u' ': out += u"%20"; break;
        case u'(': out += u"%28"; break;
        case u')': out += u"%29"; break;
        case u'<': out += u"%3C"; break;
        case u'>': out += u"%3E"; break;
        case u'\n':
        case u'\r':
        case u'\t': break;
        default: out += c;
        }
    }
    return out;
}

/// A file name (one path segment) with an extension (from the MIME type when it has none):
/// "/" and "\" of a title are no folders, ".." goes nowhere.
QString fileNameOf(const QString &name, const QString &mime, const QString &fallback)
{
    QString f = cleanTitle(QString(name).replace(u'/', u' ').replace(u'\\', u' ')).simplified();
    if (f.isEmpty())
        f = fallback;
    if (paths::extension(f).isEmpty())
        f += extensionForMime(mime);
    return attachmentFileName(f);
}

/// The page content: frontmatter and the non-empty parts separated by blank lines.
QString compose(const QVariantMap &frontmatter, const QStringList &order, const QStringList &parts)
{
    QStringList kept;
    for (const QString &p : parts) {
        if (!p.trimmed().isEmpty())
            kept.append(p);
    }
    QString out = frontmatter.isEmpty() ? QString() : markdown::stringifyFrontmatter(frontmatter, order);
    if (!kept.isEmpty())
        out += kept.join(u"\n\n"_s) + u'\n';
    return out;
}

struct Attribute {
    bool relation = false;
    QString name;
    QString value;
};

struct Attachment {
    QString id;
    QString title;
    QString mime;
    QString source;          ///< archive path of its file
    bool referenced = false; ///< linked or embedded by a note
    bool renderable = true;  ///< an image Brulion can show (isRenderableExport())
    VaultPath path;          ///< where it was stored (empty: its file is missing)
    /// Kept inside its canvas's drawing (Trilium's picture of the canvas, a picture pasted into
    /// it): `path` is the drawing, nothing stored of its own.
    bool inDrawing = false;
};

enum class Kind {
    Skip,    ///< not imported (a clone placement, a saved search, a protected note …)
    Page,    ///< a page
    Journal, ///< a daily note
    Folder,  ///< only a folder for its children
    Asset,   ///< an attachment (image and file notes)
};

struct Note {
    // From the meta.
    QString id;
    QString title;
    QString prefix;
    QString type;
    QString mime;
    QString format;
    bool clone = false;
    bool protectedNote = false; ///< "[protected]": exported without a protected session
    QString dataSource;         ///< archive path of the content file ("" none)
    QString dirSource;          ///< archive path of the folder of the children
    bool hasDir = false;        ///< the meta names a folder (dirFileName)
    QList<Attribute> attributes;
    QList<Attachment> attachments;
    int parent = -1;
    QList<int> children;

    // Decided by the import.
    Kind kind = Kind::Skip;
    int primary = -1;          ///< a clone placement: the imported note
    bool transparent = false;  ///< the root of a whole-tree export: its children are at the top
    bool listChildren = false; ///< its page lists its children
    QString folder;            ///< folder (relative to the target folder) the note is in
    QString childFolder;       ///< folder of its children
    QString name;              ///< page / folder name in `folder`
    VaultPath page;
    QDate date;
    QString dayTitle; ///< a day note: its title beyond "16 - Friday" ("Christmas Eve!")
    VaultPath asset;
    VaultPath drawing;             ///< a canvas: its drawing, one SVG with the scene ("" none)
    bool drawingRenderable = true; ///< Brulion can show the drawing (else it is linked)

    QString label(QStringView key) const
    {
        for (const Attribute &a : attributes) {
            if (!a.relation && a.name == key)
                return a.value;
        }
        return {};
    }
    bool hasLabel(QStringView key) const
    {
        return std::any_of(attributes.begin(), attributes.end(),
                           [&](const Attribute &a) { return !a.relation && a.name == key; });
    }
};

/// A reference in a note: a note, an attachment of a note, or something not in the export.
struct Ref {
    int note = -1;
    int attachment = -1;
    bool missing = false;
};

class TriliumImport {
public:
    TriliumImport(const QList<InputFile> &files, const InputFile &meta, ImportContext &ctx)
        : m_ctx(ctx), m_root(paths::dirname(meta.path))
    {
        for (const InputFile &f : files)
            m_files.insert(pathKey(f.path), &f);
        m_metaData = meta.data;
        m_known.insert(pathKey(meta.path));
    }

    void run()
    {
        if (!parseMeta())
            return;
        resolveClones();
        classify();
        markReferences();
        place();
        for (int i = 0; i < m_notes.size(); ++i) {
            const Kind kind = m_notes[i].kind;
            if (kind == Kind::Page || kind == Kind::Journal)
                render(i);
        }
        report();
    }

private:
    // --- the meta ---------------------------------------------------------------------------------

    QString archivePath(const QString &dir, const QString &name) const
    {
        return sanitizeArchivePath(dir.isEmpty() ? name : dir + u'/' + name);
    }

    bool parseMeta()
    {
        if (m_metaData.size() > MaxMetaBytes) {
            m_ctx.warn(IoMessages::tr("The Trilium export is too large to be read."));
            return false;
        }
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(m_metaData, &error);
        m_metaData.clear();
        if (error.error != QJsonParseError::NoError || !doc.isObject() || !doc.object().value(u"files"_s).isArray()) {
            const QString reason = error.error != QJsonParseError::NoError
                                           ? error.errorString()
                                           : IoMessages::tr("the list of notes is missing");
            m_ctx.warn(IoMessages::tr("The Trilium export cannot be read: “!!!meta.json” is damaged (%1). Nothing "
                                      "was imported.")
                               .arg(reason));
            return false;
        }
        const QJsonObject meta = doc.object();
        const QJsonArray top = meta.value(u"files"_s).toArray();
        // "HTML for web publishing" (share theme): a static site, not notes to import.
        bool share = m_files.contains(pathKey(paths::join(m_root, u"search-index.json"_s)));
        for (const QJsonValue &v : top) {
            const QJsonObject o = v.toObject();
            if (!o.value(u"noImport"_s).toBool())
                continue;
            const QString data = o.value(u"dataFileName"_s).toString();
            if (data.startsWith(u"assets/"_s))
                share = true;
            m_known.insert(pathKey(archivePath(m_root, data))); // navigation.html, index.html, style.css
        }
        if (share) {
            m_ctx.warn(IoMessages::tr("This is Trilium's export for web publishing. Export the notes again with the "
                                      "format “HTML” or “Markdown” and import that ZIP file."));
            return false;
        }
        const QJsonValue version = meta.value(u"formatVersion"_s);
        if (!version.isDouble() || (version.toInt() != 1 && version.toInt() != 2)) {
            m_ctx.warn(IoMessages::tr("The Trilium export has a format version Brulion does not know (%1); the notes "
                                      "were imported as well as possible.")
                               .arg(version.isDouble() ? QString::number(version.toDouble()) : version.toString()));
        }

        struct Pending {
            QJsonObject object;
            int parent = -1;
            QString dir;
        };
        QList<Pending> stack;
        for (qsizetype k = top.size() - 1; k >= 0; --k) {
            const QJsonObject o = top[k].toObject();
            if (!o.isEmpty() && !o.value(u"noImport"_s).toBool())
                stack.append(Pending{o, -1, m_root});
        }
        while (!stack.isEmpty()) {
            if (m_notes.size() >= MaxNotes) {
                m_ctx.warn(IoMessages::tr("The Trilium export is too large; only a part of it was imported."));
                break;
            }
            const Pending p = stack.takeLast();
            const QJsonObject &o = p.object;
            Note n;
            n.id = o.value(u"noteId"_s).toString().trimmed();
            n.title = cleanTitle(o.value(u"title"_s).toString());
            n.prefix = cleanTitle(o.value(u"prefix"_s).toString());
            n.type = o.value(u"type"_s).toString().trimmed();
            if (n.type.isEmpty())
                n.type = u"text"_s;
            n.mime = o.value(u"mime"_s).toString().trimmed();
            n.format = o.value(u"format"_s).toString().trimmed().toLower();
            n.clone = o.value(u"isClone"_s).toBool();
            if (const QString data = o.value(u"dataFileName"_s).toString(); !data.isEmpty())
                n.dataSource = archivePath(p.dir, data);
            const QString dir = o.value(u"dirFileName"_s).toString();
            n.hasDir = !dir.isEmpty() && !archivePath(p.dir, dir).isEmpty();
            n.dirSource = n.hasDir ? archivePath(p.dir, dir) : p.dir;
            n.protectedNote = n.dataSource.isEmpty() && n.title == u"[protected]";
            n.parent = p.parent;
            for (const QJsonValue &v : o.value(u"attributes"_s).toArray()) {
                const QJsonObject a = v.toObject();
                QString type = a.value(u"type"_s).toString();
                QString name = a.value(u"name"_s).toString().trimmed();
                // Very old exports: attribute definitions as their own types.
                if (type == u"label-definition" || type == u"relation-definition") {
                    name = type.section(u'-', 0, 0) + u':' + name;
                    type = u"label"_s;
                }
                if (name.isEmpty() || (type != u"label" && type != u"relation"))
                    continue;
                const QJsonValue value = a.value(u"value"_s);
                n.attributes.append(Attribute{type == u"relation", name,
                                              value.isString() ? value.toString() : value.toVariant().toString()});
            }
            for (const QJsonValue &v : o.value(u"attachments"_s).toArray()) {
                const QJsonObject a = v.toObject();
                Attachment att;
                att.id = a.value(u"attachmentId"_s).toString().trimmed();
                att.title = cleanTitle(a.value(u"title"_s).toString());
                att.mime = a.value(u"mime"_s).toString().trimmed();
                if (const QString data = a.value(u"dataFileName"_s).toString(); !data.isEmpty())
                    att.source = archivePath(p.dir, data);
                n.attachments.append(att);
            }
            const int index = int(m_notes.size());
            if (p.parent >= 0)
                m_notes[p.parent].children.append(index);
            const QJsonArray children = o.value(u"children"_s).toArray();
            for (qsizetype k = children.size() - 1; k >= 0; --k) {
                const QJsonObject c = children[k].toObject();
                if (!c.isEmpty() && !c.value(u"noImport"_s).toBool())
                    stack.append(Pending{c, index, n.dirSource});
            }
            m_notes.append(std::move(n));
        }
        // Lookups for references.
        for (int i = 0; i < m_notes.size(); ++i) {
            const Note &n = m_notes[i];
            m_known.insert(pathKey(n.dataSource));
            for (const Attachment &a : n.attachments)
                m_known.insert(pathKey(a.source));
            if (n.clone)
                continue;
            if (!n.dataSource.isEmpty())
                m_byData.insert(pathKey(n.dataSource), i);
            if (n.hasDir)
                m_byDir.insert(pathKey(n.dirSource), i);
            for (int k = 0; k < n.attachments.size(); ++k) {
                const Attachment &a = n.attachments[k];
                if (!a.source.isEmpty())
                    m_byAttachmentPath.insert(pathKey(a.source), {i, k});
                if (!a.id.isEmpty())
                    m_byAttachmentId.insert(a.id, {i, k});
            }
        }
        return true;
    }

    /// The first placement of a note is imported, later ones (Trilium's clone stubs, or a note id
    /// seen again) are links to it.
    void resolveClones()
    {
        for (int i = 0; i < m_notes.size(); ++i) {
            Note &n = m_notes[i];
            if (n.id.isEmpty()) {
                n.clone = false;
                continue;
            }
            if (!n.clone && !m_byId.contains(n.id))
                m_byId.insert(n.id, i);
            else
                n.clone = true;
        }
        for (Note &n : m_notes) {
            if (n.clone)
                n.primary = m_byId.value(n.id, -1);
        }
    }

    void classify()
    {
        for (int i = 0; i < m_notes.size(); ++i) {
            Note &n = m_notes[i];
            if (n.clone)
                continue;
            const bool hasChildren = !n.children.isEmpty();
            if (n.parent < 0 && n.id == u"root" && n.dataSource.isEmpty()) {
                n.transparent = true;
                n.kind = Kind::Folder;
                continue;
            }
            if (n.protectedNote) {
                ++m_protected;
                n.kind = hasChildren ? Kind::Folder : Kind::Skip;
                continue;
            }
            const QString &type = n.type;
            if (type == u"image" || type == u"file") {
                n.kind = n.dataSource.isEmpty() ? (hasChildren ? Kind::Folder : Kind::Skip) : Kind::Asset;
                if (n.dataSource.isEmpty())
                    m_missingFiles = true;
                continue;
            }
            if (isSkippedType(type)) {
                m_skipped.append(n.title);
                n.kind = hasChildren ? Kind::Folder : Kind::Skip;
                continue;
            }
            if (std::any_of(n.attributes.begin(), n.attributes.end(),
                            [](const Attribute &a) { return !a.relation && isContainerLabel(a.name); })) {
                // Calendar, year, month … notes: folders unless they have content.
                n.kind = hasContent(n) ? Kind::Page : Kind::Folder;
                continue;
            }
            if (const QDate date = QDate::fromString(n.label(u"dateNote").trimmed(), u"yyyy-MM-dd"_s);
                date.isValid() && !isSpecialType(type)) {
                n.kind = Kind::Journal;
                n.date = date;
                n.dayTitle = customDayTitle(n.title, date);
                continue;
            }
            if (type == u"book" || type == u"geoMap") { // collections ("geoMap": an old map collection)
                // An empty collection without children (a calendar root of another export) is
                // nothing; one that is linked to still gets a page (needPage()).
                n.kind = hasChildren || hasContent(n) ? Kind::Page : Kind::Folder;
                n.listChildren = true;
                continue;
            }
            if (isSpecialType(type) || !n.dataSource.isEmpty() || !hasChildren) {
                n.kind = Kind::Page; // a leaf always has a file; without one it is an empty page
                if (n.dataSource.isEmpty() && !hasChildren && type != u"webView" && type != u"web-view")
                    m_missingFiles = true;
                continue;
            }
            n.kind = Kind::Folder;
        }
    }

    /// Has note `n` a file with content (an HTML page: something inside its content wrapper)?
    bool hasContent(const Note &n) const
    {
        const InputFile *f = file(n.dataSource);
        if (!f || f->data.trimmed().isEmpty())
            return false;
        const bool html = n.format != u"markdown" && !paths::isMarkdown(n.dataSource)
                          && paths::extension(n.dataSource).compare(u".html"_s, Qt::CaseInsensitive) == 0;
        return !html || f->data.size() > MaxTextBytes || !trilium::noteBody(utf8Text(f->data)).trimmed().isEmpty();
    }

    /// The part of a day note's title after Trilium's "16 - Friday" ("16 - Piątek - Urodziny"
    /// -> "Urodziny"); empty for the plain pattern.
    static QString customDayTitle(const QString &title, QDate date)
    {
        static const QRegularExpression re(u"^0?(\\d{1,2})\\s*-\\s*[^-]+?\\s+-\\s+(.+)$"_s);
        const auto m = re.match(title.trimmed());
        if (!m.hasMatch() || m.captured(1).toInt() != date.day())
            return {};
        return m.captured(2).trimmed();
    }

    // --- references -------------------------------------------------------------------------------

    const InputFile *file(const QString &source) const
    {
        return source.isEmpty() ? nullptr : m_files.value(pathKey(source));
    }

    /// The imported note a reference to note `i` means (clones: the first placement).
    int effective(int i) const
    {
        if (i < 0 || i >= m_notes.size())
            return -1;
        return m_notes[i].clone ? m_notes[i].primary : i;
    }

    Ref byNoteId(const QString &id) const
    {
        const int i = m_byId.value(id, -1);
        return i < 0 ? Ref{-1, -1, true} : Ref{i, -1, false};
    }

    Ref byAttachmentId(const QString &id) const
    {
        const auto it = m_byAttachmentId.constFind(id);
        return it == m_byAttachmentId.constEnd() ? Ref{-1, -1, true} : Ref{it->first, it->second, false};
    }

    /// Trilium's note path "root/…/<noteId>?viewMode=attachments&attachmentId=…".
    Ref byNotePath(const QString &path) const
    {
        const qsizetype q = path.indexOf(u'?');
        if (q >= 0) {
            static const QRegularExpression attachment(u"(?:^|&)attachmentId=([A-Za-z0-9_]+)"_s);
            if (const auto m = attachment.match(path.sliced(q + 1)); m.hasMatch())
                return byAttachmentId(m.captured(1));
        }
        QString notePath = q >= 0 ? path.first(q) : path;
        if (const qsizetype hash = notePath.indexOf(u'#'); hash >= 0)
            notePath.truncate(hash);
        const QStringList ids = notePath.split(u'/', Qt::SkipEmptyParts);
        return ids.isEmpty() ? Ref{-1, -1, true} : byNoteId(ids.last());
    }

    /// What `href` (an `href`/`src` as written in a note whose file is in `sourceDir`) refers
    /// to; nullopt when it is no reference into the export (a URL, a fragment).
    std::optional<Ref> resolve(const QString &href, const QString &sourceDir) const
    {
        const QString h = href.trimmed();
        if (h.isEmpty())
            return std::nullopt;
        if (h.startsWith(u"#root"_s) || h.startsWith(u"#/root"_s))
            return byNotePath(h.sliced(h.startsWith(u"#/"_s) ? 2 : 1));
        if (h.startsWith(u'#') || hasScheme(h) || h.startsWith(u"//"_s))
            return std::nullopt;
        static const QRegularExpression queryOrFragment(u"[?#]"_s);
        const QString decoded = percentDecode(h);
        QStringList candidates = {h, decoded};
        for (const QString &c : {h, decoded}) {
            const qsizetype cut = c.indexOf(queryOrFragment);
            if (cut > 0)
                candidates.append(c.first(cut));
        }
        for (const QString &c : std::as_const(candidates)) {
            // A link to a note without a file and folder (a protected note): "Folder/" or, in
            // old exports, "Folder/undefined".
            if (c.endsWith(u'/') || c == u"undefined" || c.endsWith(u"/undefined"_s))
                return Ref{-1, -1, true};
            const QString key = pathKey(archivePath(sourceDir, c));
            if (const auto it = m_byData.constFind(key); it != m_byData.constEnd())
                return Ref{*it, -1, false};
            if (const auto it = m_byAttachmentPath.constFind(key); it != m_byAttachmentPath.constEnd())
                return Ref{it->first, it->second, false};
            if (const auto it = m_byDir.constFind(key); it != m_byDir.constEnd())
                return Ref{*it, -1, false};
        }
        // References the export did not rewrite (the server's API paths).
        static const QRegularExpression api(u"(?:^|/)api/(images|attachments|notes)/([A-Za-z0-9_]+)"_s);
        if (const auto m = api.match(decoded); m.hasMatch())
            return m.captured(1) == u"attachments" ? byAttachmentId(m.captured(2)) : byNoteId(m.captured(2));
        return Ref{-1, -1, true};
    }

    /// Mark what the notes refer to before names are given: notes without content that are
    /// linked to get a page; attachments shown in some note are not listed again.
    void markReferences()
    {
        static const QRegularExpression htmlRef(u"\\b(href|src|data-note-id)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')"_s,
                                                QRegularExpression::CaseInsensitiveOption);
        // `](dest)`; old exports wrote attachment names with spaces raw, so up to the ")".
        static const QRegularExpression markdownRef(u"\\]\\(\\s*(?:<([^>\\n]*)>|([^)\\n]+?))\\s*\\)"_s);
        const auto mark = [&](const Ref &ref) {
            if (ref.missing || ref.note < 0)
                return;
            if (ref.attachment >= 0) {
                m_notes[ref.note].attachments[ref.attachment].referenced = true;
                return;
            }
            needPage(effective(ref.note));
        };
        for (int i = 0; i < m_notes.size(); ++i) {
            const Note &n = m_notes[i];
            if (n.clone)
                continue;
            for (const Attribute &a : n.attributes) {
                if (a.relation && !isSystemRelation(a.name))
                    mark(byNoteId(a.value.trimmed()));
            }
            if (n.kind == Kind::Folder && !n.transparent && hasUserAttributes(n))
                needPage(i);
            // Text notes (and books, unknown types read as text) and Markdown code notes.
            const bool markdownCode = n.type == u"code" && trilium::languageForMime(n.mime) == u"markdown";
            const bool text = n.type == u"book" || markdownCode || !isSpecialType(n.type);
            if ((n.kind != Kind::Page && n.kind != Kind::Journal) || !text)
                continue;
            const InputFile *f = file(n.dataSource);
            if (!f || f->data.size() > MaxTextBytes)
                continue;
            const QString content = utf8Text(f->data);
            const QString dir = paths::dirname(n.dataSource);
            for (auto it = htmlRef.globalMatch(content); it.hasNext();) {
                const auto m = it.next();
                const QString value = decodeHtmlEntities(m.captured(2) + m.captured(3));
                if (m.captured(1).compare(u"data-note-id"_s, Qt::CaseInsensitive) == 0)
                    mark(byNoteId(value.trimmed()));
                else if (const auto ref = resolve(value, dir))
                    mark(*ref);
            }
            if (n.format == u"markdown" || markdownCode || paths::isMarkdown(n.dataSource)) {
                for (auto it = markdownRef.globalMatch(content); it.hasNext();) {
                    const auto m = it.next();
                    QString dest = m.captured(1) + m.captured(2);
                    if (const qsizetype title = dest.indexOf(u" \""_s); title > 0)
                        dest.truncate(title); // `](dest "title")`
                    if (const auto ref = resolve(dest, dir))
                        mark(*ref);
                }
            }
        }
        // Clone placements are listed on the page of their parent.
        for (const Note &n : std::as_const(m_notes)) {
            if (n.clone && n.parent >= 0)
                needPage(n.parent);
        }
    }

    void needPage(int i)
    {
        if (i < 0)
            return;
        Note &n = m_notes[i];
        if (n.kind == Kind::Folder && !n.transparent) {
            n.kind = Kind::Page;
            n.listChildren = true;
        }
    }

    bool hasUserAttributes(const Note &n) const
    {
        return std::any_of(n.attributes.begin(), n.attributes.end(), [](const Attribute &a) {
            return a.relation ? !isSystemRelation(a.name) : !isSystemLabel(a.name);
        });
    }

    // --- names and paths --------------------------------------------------------------------------

    QString displayName(const Note &n) const
    {
        const QString title = n.title.isEmpty() ? IoMessages::tr("Untitled") : n.title;
        return n.prefix.isEmpty() ? title : n.prefix + u" - "_s + title;
    }

    /// The first number to try for the next sibling named `baseKey` in the folder `folderKey`
    /// ("Untitled 2", "Untitled 3" …): thousands of same-titled siblings stay linear.
    int &nextSuffix(const QString &folderKey, const QString &baseKey)
    {
        return m_nextSuffix[folderKey + u'\n' + baseKey];
    }

    /// `base`, or "base 2", "base 3" … — unique in `folder`, case-insensitively, and no name the
    /// vault ignores ("logseq", "node_modules": everything in them would be skipped).
    QString claimName(const QString &folder, const QString &base)
    {
        const QString folderKey = pathKey(folder);
        const QString baseKey = pathKey(base);
        QSet<QString> &used = m_usedNames[folderKey];
        int &next = nextSuffix(folderKey, baseKey);
        for (int k = std::max(next, 1);; ++k) {
            const QString name = k == 1 ? base : base + u' ' + QString::number(k);
            const QString key = k == 1 ? baseKey : baseKey + u' ' + QString::number(k);
            if (used.contains(key) || paths::isIgnored(name))
                continue;
            next = k + 1;
            used.insert(key);
            return name;
        }
    }

    /// A page `base` (or "base 2" …) in `folder`: free among its siblings (pages and folders,
    /// case-insensitively) and in the vault, never a name the vault ignores (its folder would be).
    VaultPath claimPageName(const QString &folder, const QString &base)
    {
        const QString folderKey = pathKey(folder);
        const QString baseKey = pathKey(base);
        QSet<QString> &used = m_usedNames[folderKey];
        int &next = nextSuffix(folderKey, baseKey);
        for (int k = std::max(next, 1);; ++k) {
            const QString candidate = k == 1 ? base : base + u' ' + QString::number(k);
            const QString key = k == 1 ? baseKey : baseKey + u' ' + QString::number(k);
            if (used.contains(key) || paths::isIgnored(candidate))
                continue;
            next = k + 1;
            const VaultPath page = m_ctx.claimPage(paths::join(folder, candidate));
            used.insert(key);
            used.insert(pathKey(paths::pageTitle(page)));
            return page;
        }
    }

    /// The folder for the children of a note named `name` in `parentFolder`, or `parentFolder`
    /// itself when the path would get too deep or too long (m_flattened).
    QString childFolderOf(const Note &n, const QString &parentFolder)
    {
        const qsizetype bytes = parentFolder.toUtf8().size() + 1 + n.name.toUtf8().size();
        if (parentFolder.isEmpty() || (parentFolder.count(u'/') + 1 < MaxFolderDepth && bytes <= MaxFolderBytes))
            return paths::join(parentFolder, n.name);
        if (!n.children.isEmpty())
            m_flattened = true;
        return parentFolder;
    }

    void place()
    {
        // Daily notes are written to the journal's paths: no page may take them.
        for (const Note &n : std::as_const(m_notes)) {
            if (n.kind == Kind::Journal && !n.clone)
                m_ctx.reservePath(m_ctx.journalPath(n.date));
        }
        for (int i = 0; i < m_notes.size(); ++i) {
            Note &n = m_notes[i];
            const QString parentFolder = n.parent < 0 ? QString() : m_notes[n.parent].childFolder;
            n.folder = parentFolder;
            n.childFolder = parentFolder;
            if (n.transparent || n.clone)
                continue;
            if (n.kind == Kind::Page) {
                // The page decides the name (free in the vault too); its folder follows it.
                n.page = claimPageName(parentFolder, pageSegment(displayName(n)));
                n.name = paths::pageTitle(n.page);
                n.childFolder = childFolderOf(n, parentFolder);
            } else if (n.kind != Kind::Skip || !n.children.isEmpty()) {
                n.name = claimName(parentFolder, pageSegment(displayName(n)));
                n.childFolder = childFolderOf(n, parentFolder);
            } else {
                n.name = pageSegment(displayName(n)); // only names the folder of its attachments
            }
            if (n.kind == Kind::Asset) {
                const QString fileName =
                        fileNameOf(n.label(u"originalFileName").isEmpty() ? n.title : n.label(u"originalFileName"),
                                   n.mime, u"file"_s);
                if (const InputFile *f = file(n.dataSource))
                    n.asset = m_ctx.addAttachment(paths::join(parentFolder, fileName), f->data);
                else
                    m_missingFiles = true;
            }
            if (n.type == u"canvas" && n.kind == Kind::Page)
                placeCanvas(i, parentFolder);
            for (Attachment &a : n.attachments) {
                if (a.inDrawing)
                    continue;
                const InputFile *f = file(a.source);
                if (!f) {
                    m_missingImages = true;
                    continue;
                }
                const bool exportImage = isExportImage(n.type, a.title);
                const QString relative = exportImage ? paths::join(parentFolder, n.name + paths::extension(a.title))
                                                     : paths::join(paths::join(parentFolder, n.name),
                                                                   fileNameOf(a.title, a.mime, u"attachment"_s));
                a.path = m_ctx.addAttachment(relative, f->data);
                a.renderable = !exportImage || isRenderableExport(n.type, a.title, f->data);
            }
        }
        for (const Note &n : std::as_const(m_notes)) {
            if (n.kind == Kind::Page && !n.clone)
                m_pageTitles.insert(pathKey(paths::pageTitle(n.page)));
        }
    }

    /// The scene of canvas note `i`: its content (an Excalidraw scene's JSON) as Brulion reads a
    /// scene — `type` "excalidraw" and `version` 2 added when missing (zadam/trilium before 0.61),
    /// Trilium's own keys `_meta` and `svg` left out, `files` given as an array (some versions)
    /// made an object by `id` — and, with `foldPictures`, the pictures of the scene's image
    /// elements that Trilium (0.61 and later) keeps as image attachments of the note, titled with
    /// their `fileId`, added to `files` as excalidraw.com keeps them (`{mimeType, id, dataURL,
    /// created: 0}`; a file the scene has already wins — the attachment is the same picture).
    /// `folded` receives those attachments' indices, `legacyPicture` the picture old Trilium kept
    /// in the content (`svg`). nullopt: no JSON object with an `elements` array (a damaged or
    /// protected note), more than a scene may hold (drawing::MaxSceneBytes, MaxSceneContainers)
    /// or no Excalidraw scene.
    std::optional<drawing::Drawing> canvasScene(int i, bool foldPictures, QList<int> *folded, QByteArray *legacyPicture)
    {
        const Note &n = m_notes[i];
        const InputFile *f = file(n.dataSource);
        if (!f || f->data.size() > drawing::MaxSceneBytes)
            return std::nullopt;
        // (Counted before parsing, as drawing::sceneRoot() does.)
        qsizetype containers = 0;
        for (const char c : f->data) {
            if ((c == '[' || c == '{') && ++containers > drawing::MaxSceneContainers)
                return std::nullopt;
        }
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(f->data, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            return std::nullopt;
        QJsonObject scene = doc.object();
        if (!scene.value(u"elements"_s).isArray())
            return std::nullopt;
        if (!scene.contains(u"type"_s))
            scene.insert(u"type"_s, u"excalidraw"_s);
        if (!scene.contains(u"version"_s))
            scene.insert(u"version"_s, 2);
        if (legacyPicture)
            *legacyPicture = scene.value(u"svg"_s).toString().toUtf8();
        scene.remove(u"_meta"_s);
        scene.remove(u"svg"_s);
        QJsonObject files;
        if (const QJsonValue given = scene.value(u"files"_s); given.isObject()) {
            files = given.toObject();
        } else {
            for (const QJsonValue &v : given.toArray()) {
                const QJsonObject entry = v.toObject();
                const QString id = entry.value(u"id"_s).toString();
                if (!id.isEmpty() && !files.contains(id))
                    files.insert(id, entry);
            }
        }
        if (foldPictures) {
            QSet<QString> used;
            for (const QJsonValue &v : scene.value(u"elements"_s).toArray()) {
                const QJsonObject e = v.toObject();
                if (e.value(u"type"_s).toString() == u"image" && !e.value(u"isDeleted"_s).toBool())
                    used.insert(e.value(u"fileId"_s).toString());
            }
            for (int k = 0; k < n.attachments.size(); ++k) {
                const Attachment &a = n.attachments[k];
                const QString mime = a.mime.toLower();
                const InputFile *picture = file(a.source);
                if (!picture || !mime.startsWith(u"image/"_s) || isExportImage(n.type, a.title) || a.title.isEmpty()
                    || !used.contains(a.title))
                    continue;
                if (!files.contains(a.title)) {
                    files.insert(a.title,
                                 QJsonObject{{u"mimeType"_s, mime},
                                             {u"id"_s, a.title},
                                             {u"dataURL"_s, QString(u"data:"_s + mime + u";base64,"_s
                                                                    + QString::fromLatin1(picture->data.toBase64()))},
                                             {u"created"_s, 0}});
                }
                if (folded)
                    folded->append(k);
            }
        }
        scene.insert(u"files"_s, files);
        return drawing::sceneFromObject(scene);
    }

    /// Store canvas note `i`'s drawing (Note::drawing): Trilium's SVG picture of it
    /// (`canvas-export.svg`, else old Trilium's picture in the content) with the scene embedded
    /// (drawing::svgWithScene); when there is none or it is no usable picture (a PNG, an empty
    /// drawing's `width="0"`), the drawing as Brulion draws it (drawing::toSvg) — only when Brulion
    /// draws every element (editable()), else its picture would miss some. Nothing (the note is
    /// imported as before: its picture and the source as `.excalidraw`) when the content is no
    /// scene. The export images and the pictures folded into the scene then point at the drawing.
    void placeCanvas(int i, const QString &parentFolder)
    {
        Note &n = m_notes[i];
        QByteArray exportSvg;
        for (const Attachment &a : std::as_const(n.attachments)) {
            if (isExportImage(n.type, a.title) && a.title.endsWith(u".svg"_s, Qt::CaseInsensitive)) {
                if (const InputFile *f = file(a.source)) {
                    exportSvg = f->data;
                    break;
                }
            }
        }
        struct Made {
            QByteArray file;
            bool renderable = true;
        };
        const auto make = [&](bool foldPictures, QList<int> *folded) -> std::optional<Made> {
            QByteArray legacy;
            const std::optional<drawing::Drawing> scene = canvasScene(i, foldPictures, folded, &legacy);
            if (!scene)
                return std::nullopt;
            const bool editable = drawing::editable(*scene);
            const QByteArray &picture = exportSvg.isEmpty() ? legacy : exportSvg;
            if (!picture.isEmpty()) {
                if (std::optional<QByteArray> svg = drawing::svgWithScene(picture, *scene)) {
                    // Brulion draws an editable drawing itself; another one is Trilium's picture
                    // through Qt's SVG renderer (HTML labels it cannot draw: linked).
                    return Made{std::move(*svg),
                                editable || isRenderableExport(n.type, u"canvas-export.svg"_s, picture)};
                }
            }
            if (!editable)
                return std::nullopt;
            return Made{drawing::toSvg(*scene), true};
        };
        // Brulion must read its drawing back: a scene beyond drawing::MaxSceneBytes (large
        // pictures pasted into the canvas) keeps its pictures as files next to it.
        QList<int> folded;
        std::optional<Made> made = make(true, &folded);
        if (made && !drawing::readDrawing(made->file)) {
            made.reset();
            if (!folded.isEmpty()) {
                folded.clear();
                made = make(false, nullptr);
                if (made && !drawing::readDrawing(made->file))
                    made.reset();
                if (made)
                    m_ctx.warn(IoMessages::tr("The pictures of the drawing “%1” are too large to keep inside it; "
                                              "they were saved next to it.")
                                       .arg(displayName(n)));
            }
        }
        if (!made)
            return;
        // Where its picture was stored before: links into the import keep their shape.
        n.drawing = m_ctx.addAttachment(paths::join(parentFolder, n.name + u".svg"_s), made->file);
        n.drawingRenderable = made->renderable;
        for (int k = 0; k < n.attachments.size(); ++k) {
            Attachment &a = n.attachments[k];
            if (isExportImage(n.type, a.title)) {
                // References to Trilium's picture (an image of the note, an include box) show
                // the drawing.
                a.inDrawing = true;
                a.path = n.drawing;
                a.renderable = n.drawingRenderable;
            } else if (folded.contains(k)) {
                a.inDrawing = true;
                a.path = n.drawing;
                a.referenced = true; // not listed under the drawing
            }
        }
    }

    // --- links ------------------------------------------------------------------------------------

    /// Wiki-link target of a page: its vault path. Trilium's references are exact; a bare title
    /// would resolve to any page of the vault with that title (an earlier import, the user's own
    /// "Zadania").
    static QString pageTarget(const Note &n) { return paths::stripMarkdownExtension(n.page); }

    /// Wiki-link target of the daily note of `date`: its title, or its path when an imported
    /// page has that title too ("2026-10-16.md" in a folder).
    QString journalTarget(QDate date) const
    {
        const QString target = m_ctx.journalLinkTarget(date);
        if (!target.contains(u'/') && m_pageTitles.contains(pathKey(target)))
            return paths::stripMarkdownExtension(m_ctx.journalPath(date));
        return target;
    }

    /// A link to the daily note of `date` (shown as a date; by path with the date as its text).
    QString journalLink(QDate date) const
    {
        const QString target = journalTarget(date);
        const QString title = m_ctx.journalLinkTarget(date);
        return wikiLink(target, {}, {}, target == title ? QString() : paths::basename(title));
    }

    /// Can `label` be the text of a wiki link (`[[target|label]]`)?
    static bool usableAlias(const QString &label)
    {
        return !label.trimmed().isEmpty() && !label.contains(u'|') && !label.contains(u'[') && !label.contains(u']')
               && !label.contains(u'\n');
    }

    /// A link to page `n` showing `display` — else the note's title, else the page's name.
    static QString pageLink(const Note &n, const QString &display)
    {
        QString alias = paths::pageTitle(n.page);
        for (const QString &candidate : {display, n.title}) {
            if (usableAlias(candidate)) {
                alias = candidate.trimmed();
                break;
            }
        }
        return wikiLink(pageTarget(n), {}, {}, alias);
    }

    /// A link to an attachment (never an embed), labelled like attachmentLink() does.
    static QString fileLink(const VaultPath &path, const QString &label)
    {
        return wikiLink(path, {}, {}, usableAlias(label) ? label.trimmed() : paths::basename(path));
    }

    static QString plainLabel(const QString &markdown)
    {
        static const QRegularExpression escape(u"\\\\(?=[!-/:-@\\[-`{-~])"_s);
        return QString(markdown).remove(escape).trimmed();
    }

    /// A link (not an embed) to note `t` shown as `display` (empty: its title).
    QString linkToNote(int t, const QString &display) const
    {
        if (t < 0)
            return {};
        const Note &n = m_notes[t];
        switch (n.kind) {
        case Kind::Page: return pageLink(n, display);
        case Kind::Journal: return journalLink(n.date);
        case Kind::Asset: return n.asset.isEmpty() ? QString() : fileLink(n.asset, display);
        default: return {};
        }
    }

    std::optional<QString> linkMarkdown(const Ref &ref, const QString &labelMarkdown, bool reference)
    {
        if (ref.missing || ref.note < 0) {
            m_missingLinks = true;
            return labelMarkdown;
        }
        const QString plain = plainLabel(labelMarkdown);
        if (ref.attachment >= 0) {
            const Attachment &a = m_notes[ref.note].attachments[ref.attachment];
            if (a.path.isEmpty())
                return labelMarkdown;
            return fileLink(a.path, plain);
        }
        const int t = effective(ref.note);
        if (t < 0) {
            m_missingLinks = true;
            return labelMarkdown;
        }
        const Note &n = m_notes[t];
        // A reference link shows the target's title (its text is only a snapshot).
        const QString link = linkToNote(t, reference ? n.title : plain);
        return link.isEmpty() ? labelMarkdown : link;
    }

    /// The rendered image of a diagram note (mermaid, canvas …) Brulion can show, or nullptr.
    static const Attachment *shownExportImage(const Note &n)
    {
        for (const Attachment &a : n.attachments) {
            if (isExportImage(n.type, a.title) && !a.path.isEmpty() && a.renderable)
                return &a;
        }
        return nullptr;
    }

    /// Markdown of note `t` as an embed (an image, an include-note box).
    QString embedNote(int t) const
    {
        if (t < 0)
            return {};
        const Note &n = m_notes[t];
        if (!n.drawing.isEmpty() && n.drawingRenderable)
            return attachmentLink(n.drawing);
        if (const Attachment *image = shownExportImage(n))
            return attachmentLink(image->path);
        switch (n.kind) {
        case Kind::Page: return wikiLink(pageTarget(n), {}, {}, {}, true);
        case Kind::Journal: return wikiLink(journalTarget(n.date), {}, {}, {}, true);
        case Kind::Asset: return n.asset.isEmpty() ? QString() : attachmentLink(n.asset);
        default: return {};
        }
    }

    QString imageMarkdown(int owner, const QString &sourceDir, const QString &src, const QString &alt)
    {
        const QString s = src.trimmed();
        const QString altText = alt.isEmpty() ? QString() : escapeMarkdownText(alt, false);
        if (s.startsWith(u"data:"_s, Qt::CaseInsensitive)) {
            const auto decoded = decodeDataUri(s);
            if (!decoded)
                return altText;
            const QString ext = extensionForMime(decoded->first);
            const Note &n = m_notes[owner];
            const QString name =
                    u"image-"_s + QString::number(++m_dataImages[owner]) + (ext.isEmpty() ? u".bin"_s : ext);
            return attachmentLink(
                    m_ctx.addAttachment(paths::join(paths::join(n.folder, n.name), name), decoded->second));
        }
        if (hasScheme(s) || s.startsWith(u"//"_s)) {
            const QString scheme = QUrl(s).scheme().toLower();
            if (scheme != u"http" && scheme != u"https")
                return altText;
            return u"!["_s + escapeMarkdownText(alt, false, true) + u"]("_s + urlDestination(s) + u')';
        }
        const auto ref = resolve(s, sourceDir);
        if (!ref || ref->missing || ref->note < 0) {
            m_missingImages = true;
            return altText;
        }
        if (ref->attachment >= 0) {
            const Attachment &a = m_notes[ref->note].attachments[ref->attachment];
            if (a.path.isEmpty())
                return altText;
            if (!a.renderable) // a diagram Brulion cannot draw: its page (with the source)
                return embedNote(effective(ref->note));
            return attachmentLink(a.path);
        }
        const QString embed = embedNote(effective(ref->note));
        if (embed.isEmpty())
            m_missingImages = true;
        return embed.isEmpty() ? altText : embed;
    }

    trilium::Resolver resolver(int owner, const QString &sourceDir)
    {
        trilium::Resolver r;
        r.link = [this, sourceDir](const QString &href, const QString &label,
                                   bool reference) -> std::optional<QString> {
            const auto ref = resolve(href, sourceDir);
            if (!ref)
                return std::nullopt;
            return linkMarkdown(*ref, label, reference);
        };
        r.image = [this, owner, sourceDir](const QString &src, const QString &alt) {
            return imageMarkdown(owner, sourceDir, src, alt);
        };
        r.include = [this](const QString &id) {
            const Ref ref = byNoteId(id);
            const QString embed = ref.missing ? QString() : embedNote(effective(ref.note));
            if (embed.isEmpty())
                m_missingLinks = true;
            return embed;
        };
        return r;
    }

    // --- content ----------------------------------------------------------------------------------

    /// The file of note `i`, or nullptr (reported as missing when the meta names one).
    const InputFile *dataFile(int i)
    {
        const Note &n = m_notes[i];
        const InputFile *f = file(n.dataSource);
        if (!f && !n.dataSource.isEmpty())
            m_missingFiles = true;
        return f;
    }

    /// A note too large to convert, kept as a file of its own and linked.
    QString keptAsFile(int i, const InputFile &f, const QString &extension)
    {
        const Note &n = m_notes[i];
        m_ctx.warn(IoMessages::tr("“%1” is too large to convert; it was saved as an attachment.").arg(displayName(n)));
        const VaultPath path = m_ctx.addAttachment(paths::join(n.folder, n.name + extension), f.data);
        return attachmentLink(path, paths::basename(path));
    }

    QString textContent(int i)
    {
        const Note &n = m_notes[i];
        const InputFile *f = dataFile(i);
        if (!f)
            return {};
        const bool markdown = n.format == u"markdown" || (n.format.isEmpty() && paths::isMarkdown(n.dataSource))
                              || paths::extension(n.dataSource) == u".mkd";
        if (f->data.size() > MaxTextBytes || (!markdown && f->data.count("<li") + f->data.count("<LI") > MaxListItems))
            return keptAsFile(i, *f, markdown ? u".md"_s : u".html"_s);
        const QString raw = utf8Text(f->data);
        const trilium::Resolver r = resolver(i, paths::dirname(n.dataSource));
        return markdown ? trilium::markdownNoteToMarkdown(raw, n.title, r)
                        : trilium::htmlNoteToMarkdown(trilium::noteBody(raw), r);
    }

    QString codeContent(int i)
    {
        const Note &n = m_notes[i];
        const InputFile *f = dataFile(i);
        if (!f)
            return {};
        const QString language = trilium::languageForMime(n.mime);
        if (f->data.size() > MaxCodeBytes || f->data.contains('\0')) {
            const QString ext = paths::extension(n.dataSource);
            return keptAsFile(i, *f, ext.isEmpty() || ext == u".dat" ? u".txt"_s : ext);
        }
        QString code = utf8Text(f->data);
        code.replace(u"\r\n"_s, u"\n"_s);
        if (code.endsWith(u'\n'))
            code.chop(1);
        if (language == u"markdown")
            return trilium::markdownNoteToMarkdown(code, {}, resolver(i, paths::dirname(n.dataSource)));
        if (code.trimmed().isEmpty())
            return {};
        const QString fence = codeFenceFor(code);
        return fence + language + u'\n' + code + u'\n' + fence;
    }

    /// The rendered image of a diagram note (mermaid, canvas …), as an embed.
    QString exportImage(const Note &n) const
    {
        const Attachment *image = shownExportImage(n);
        return image ? attachmentLink(image->path) : QString();
    }

    /// The rendered image of a diagram note that Brulion cannot show (isRenderableExport()), as a
    /// link to the file.
    QString exportImageLink(const Note &n) const
    {
        for (const Attachment &a : n.attachments) {
            if (isExportImage(n.type, a.title) && !a.path.isEmpty() && !a.renderable)
                return fileLink(a.path, paths::basename(a.path));
        }
        return {};
    }

    /// The source of a drawing kept as an attachment ("Szkic.excalidraw"), as a link.
    QString sourceFile(int i, const QString &extension)
    {
        const Note &n = m_notes[i];
        const InputFile *f = dataFile(i);
        if (!f || f->data.trimmed().isEmpty())
            return {};
        const VaultPath path = m_ctx.addAttachment(paths::join(n.folder, n.name + extension), f->data);
        return attachmentLink(path, paths::basename(path));
    }

    QString mindMapList(int i)
    {
        const InputFile *f = dataFile(i);
        if (!f)
            return {};
        const QJsonObject root = QJsonDocument::fromJson(f->data).object().value(u"nodeData"_s).toObject();
        QStringList lines;
        std::function<void(const QJsonObject &, int)> walk = [&](const QJsonObject &node, int depth) {
            if (depth > MaxMindMapDepth)
                return;
            const QString topic = cleanTitle(node.value(u"topic"_s).toString());
            if (!topic.isEmpty())
                lines.append(QString(depth, u'\t') + u"- "_s + escapeMarkdownText(topic, false));
            for (const QJsonValue &c : node.value(u"children"_s).toArray())
                walk(c.toObject(), topic.isEmpty() ? depth : depth + 1);
        };
        walk(root, 0);
        return lines.join(u'\n');
    }

    QString relationMapList(int i)
    {
        const InputFile *f = dataFile(i);
        if (!f)
            return {};
        QStringList lines;
        QSet<int> seen;
        for (const QJsonValue &v : QJsonDocument::fromJson(f->data).object().value(u"notes"_s).toArray()) {
            const Ref ref = byNoteId(v.toObject().value(u"noteId"_s).toString());
            const int t = effective(ref.note);
            if (ref.missing || t < 0 || seen.contains(t))
                continue;
            seen.insert(t);
            if (const QString link = linkToNote(t, QString()); !link.isEmpty())
                lines.append(u"- "_s + link);
        }
        return lines.join(u'\n');
    }

    QString webViewLink(const Note &n) const
    {
        const QString url = n.label(u"webViewSrc").trimmed();
        if (url.isEmpty())
            return {};
        const QString scheme = QUrl(url).scheme().toLower();
        if (scheme == u"http" || scheme == u"https")
            return u'<' + urlDestination(url) + u'>';
        return escapeMarkdownText(url, false);
    }

    /// The list of a note's children in Trilium's order, as Trilium shows it below a note:
    /// links, images embedded, folders by name. Not `all`: only clone placements and image and
    /// file notes (they have no other place).
    QString childList(int i, bool all) const
    {
        QStringList lines;
        for (const int c : m_notes[i].children) {
            const Note &child = m_notes[c];
            const int t = child.clone ? child.primary : c;
            if (t < 0)
                continue;
            const Note &target = m_notes[t];
            if (target.protectedNote || (!all && !child.clone && target.kind != Kind::Asset)
                || (target.kind == Kind::Journal && !writesJournal(target)))
                continue;
            QString link = target.kind == Kind::Asset && !target.asset.isEmpty() && isImageFile(target.asset)
                                   ? attachmentLink(target.asset)
                                   : linkToNote(t, displayName(child));
            if (link.isEmpty() && target.kind == Kind::Folder)
                link = escapeMarkdownText(displayName(child), false);
            if (!link.isEmpty())
                lines.append(u"- "_s + link);
        }
        return lines.join(u'\n');
    }

    /// Does day note `n` make a daily note (render() skips an empty one)?
    bool writesJournal(const Note &n) const
    {
        return hasContent(n) || !n.children.isEmpty() || !n.dayTitle.isEmpty() || hasUserAttributes(n)
               || std::any_of(n.attachments.cbegin(), n.attachments.cend(),
                              [](const Attachment &a) { return !a.path.isEmpty(); });
    }

    std::pair<QVariantMap, QStringList> frontmatter(int i) const
    {
        const Note &n = m_notes[i];
        // Values are collected per key and the map built once: thousands of labels stay linear.
        QStringList order;
        QHash<QString, QStringList> values;
        QStringList aliases;
        QStringList tags;
        QSet<QString> tagKeys;
        if (n.kind == Kind::Page && !n.title.isEmpty() && pageSegment(displayName(n)) != n.title)
            aliases.append(n.title);
        const auto addTag = [&](const QString &text) {
            const QString tag = tagName(text);
            if (!tag.isEmpty() && !tagKeys.contains(tag.toCaseFolded())) {
                tagKeys.insert(tag.toCaseFolded());
                tags.append(tag);
            }
        };
        const auto add = [&](const QString &key, const QString &value) {
            if (key.compare(u"tags"_s, Qt::CaseInsensitive) == 0) {
                for (const QString &t : value.split(u',', Qt::SkipEmptyParts))
                    addTag(t);
                return;
            }
            if (key.compare(u"aliases"_s, Qt::CaseInsensitive) == 0) {
                if (!aliases.contains(value))
                    aliases.append(value);
                return;
            }
            auto it = values.find(key);
            if (it == values.end()) {
                it = values.insert(key, {});
                order.append(key);
            }
            it->append(value);
        };
        const bool isTemplate = n.hasLabel(u"template");
        QStringList definitions;
        for (const Attribute &a : n.attributes) {
            if (a.relation) {
                if (isSystemRelation(a.name))
                    continue;
                const QString link = relationLink(byNoteId(a.value.trimmed()));
                if (!link.isEmpty())
                    add(a.name, link);
                continue;
            }
            if (a.name == u"pageUrl") {
                if (!a.value.trimmed().isEmpty())
                    add(u"source"_s, a.value.trimmed());
                continue;
            }
            if (isTemplate && (a.name.startsWith(u"label:"_s) || a.name.startsWith(u"relation:"_s))) {
                definitions.append(a.name.section(u':', 1));
                continue;
            }
            if (isSystemLabel(a.name))
                continue;
            if (a.value.isEmpty())
                addTag(a.name);
            else
                add(a.name, a.value);
        }
        QVariantMap fm;
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            fm.insert(it.key(), it->size() == 1 ? QVariant(it->first()) : QVariant(*it));
        // A template's attribute definitions: the properties a note made from it gets (empty).
        for (const QString &key : std::as_const(definitions)) {
            if (!key.isEmpty() && !key.contains(u':') && !fm.contains(key)) {
                fm.insert(key, QString());
                order.append(key);
            }
        }
        if (!aliases.isEmpty())
            fm.insert(u"aliases"_s, aliases);
        if (!tags.isEmpty())
            fm.insert(u"tags"_s, tags);
        order.prepend(u"tags"_s);
        order.prepend(u"aliases"_s);
        return {fm, order};
    }

    /// A relation's value: a link to the target note, empty when it was not imported.
    QString relationLink(const Ref &ref) const
    {
        const int t = ref.missing ? -1 : effective(ref.note);
        if (t < 0)
            return {};
        const Note &n = m_notes[t];
        switch (n.kind) {
        case Kind::Page: return pageLink(n, n.title);
        case Kind::Journal: return journalLink(n.date);
        case Kind::Asset: return n.asset.isEmpty() ? QString() : fileLink(n.asset, n.title);
        default: return {};
        }
    }

    void render(int i)
    {
        const Note &n = m_notes[i];
        const QString &type = n.type;
        QStringList parts;
        if (n.kind == Kind::Journal && !n.dayTitle.isEmpty())
            parts.append(u"# "_s + escapeMarkdownText(n.dayTitle, false));
        if (type == u"code") {
            parts.append(codeContent(i));
        } else if (type == u"mermaid") {
            parts.append(exportImage(n));
            if (const InputFile *f = dataFile(i)) {
                QString code = utf8Text(f->data).replace(u"\r\n"_s, u"\n"_s);
                while (code.endsWith(u'\n'))
                    code.chop(1);
                if (!code.trimmed().isEmpty()) {
                    const QString fence = codeFenceFor(code);
                    parts.append(fence + u"mermaid\n"_s + code + u'\n' + fence);
                }
            }
            parts.append(exportImageLink(n));
        } else if (type == u"canvas") {
            if (!n.drawing.isEmpty()) {
                parts.append(n.drawingRenderable ? attachmentLink(n.drawing)
                                                 : fileLink(n.drawing, paths::basename(n.drawing)));
            } else {
                parts.append(exportImage(n));
                parts.append(exportImageLink(n));
                parts.append(sourceFile(i, u".excalidraw"_s));
            }
        } else if (type == u"mindMap") {
            parts.append(exportImage(n));
            parts.append(mindMapList(i));
            parts.append(exportImageLink(n));
            parts.append(sourceFile(i, u".json"_s));
        } else if (type == u"spreadsheet") {
            parts.append(exportImage(n));
            parts.append(exportImageLink(n));
            parts.append(sourceFile(i, u".json"_s));
        } else if (type == u"relationMap" || type == u"relation-map") {
            parts.append(relationMapList(i));
        } else if (type == u"webView" || type == u"web-view") {
            parts.append(webViewLink(n));
        } else if (type == u"text" || !n.mime.startsWith(u"application/json"_s)) {
            parts.append(textContent(i)); // text, books, unknown types (Trilium reads them as text)
        }
        // Attachments the content does not show.
        QStringList attachments;
        for (const Attachment &a : n.attachments) {
            if (!a.referenced && !a.path.isEmpty() && !isExportImage(type, a.title))
                attachments.append(attachmentLink(a.path, a.title));
        }
        parts.append(attachments.join(u'\n'));
        // The children below the content, after a rule (a list ending the content would merge
        // with them).
        const bool all = n.listChildren || n.kind == Kind::Journal || !n.hasLabel(u"hideChildrenOverview");
        if (const QString children = childList(i, all); !children.isEmpty()) {
            if (std::any_of(parts.cbegin(), parts.cend(), [](const QString &p) { return !p.trimmed().isEmpty(); }))
                parts.append(u"---"_s);
            parts.append(children);
        }
        const auto [fm, order] = frontmatter(i);
        const QString content = compose(fm, order, parts);
        if (n.kind == Kind::Journal) {
            if (!content.trimmed().isEmpty())
                m_ctx.addJournal(n.date, content);
            return;
        }
        m_ctx.addPage(n.page, content);
    }

    // --- report -----------------------------------------------------------------------------------

    static QString titleList(const QStringList &titles)
    {
        QStringList quoted;
        for (qsizetype k = 0; k < titles.size() && k < MaxListedTitles; ++k)
            quoted.append(IoMessages::tr("“%1”", "a quoted note title in a list").arg(titles[k]));
        QString out = quoted.join(u", "_s);
        if (titles.size() > MaxListedTitles)
            out += u" …"_s;
        return out;
    }

    void report()
    {
        // Files the meta does not know: another export picked together with this one (they
        // all unpack next to each other), or files added by hand.
        int unknown = 0;
        for (auto it = m_files.cbegin(); it != m_files.cend(); ++it) {
            if (!m_known.contains(it.key()))
                ++unknown;
        }
        if (unknown > 0)
            m_ctx.warn(IoMessages::tr("%n file(s) of the selection are not part of the Trilium export and were not "
                                      "imported. Import each Trilium export on its own.",
                                      nullptr, unknown));
        if (m_protected > 0)
            m_ctx.warn(IoMessages::tr("%n protected note(s) could not be imported: Trilium exports their content "
                                      "only while a protected session is open.",
                                      nullptr, m_protected));
        if (!m_skipped.isEmpty())
            m_ctx.warn(IoMessages::tr("%n Trilium note(s) of a kind Brulion does not have (saved searches, note maps, "
                                      "rendered notes, launchers) were skipped: %1",
                                      nullptr, int(m_skipped.size()))
                               .arg(titleList(m_skipped)));
        if (m_missingLinks)
            m_ctx.warn(IoMessages::tr("Some links point to notes that are not in the export (another part of the "
                                      "tree, an excluded or a protected note); only their text was kept."));
        if (m_missingImages)
            m_ctx.warn(IoMessages::tr("Some attachments referenced by notes are missing in the export."));
        if (m_missingFiles)
            m_ctx.warn(IoMessages::tr("Some notes of the Trilium export have no file in it; they were imported "
                                      "without content."));
        if (m_flattened)
            m_ctx.warn(IoMessages::tr("The note tree is nested too deeply for file paths; the deepest notes were put "
                                      "into the folder of an ancestor."));
    }

    ImportContext &m_ctx;
    QString m_root; ///< archive folder of the export (the folder of !!!meta.json)
    QByteArray m_metaData;
    QHash<QString, const InputFile *> m_files;
    QSet<QString> m_known; ///< files the meta accounts for
    QList<Note> m_notes;
    QHash<QString, int> m_byId;
    QHash<QString, int> m_byData;
    QHash<QString, int> m_byDir;
    QHash<QString, std::pair<int, int>> m_byAttachmentPath;
    QHash<QString, std::pair<int, int>> m_byAttachmentId;
    QHash<QString, QSet<QString>> m_usedNames;
    QHash<QString, int> m_nextSuffix; ///< folder key + "\n" + name key -> next suffix to try
    QSet<QString> m_pageTitles;       ///< title keys of the imported pages
    QHash<int, int> m_dataImages;
    QStringList m_skipped;
    int m_protected = 0;
    bool m_missingLinks = false;
    bool m_missingImages = false;
    bool m_missingFiles = false;
    bool m_flattened = false;
};

class TriliumImporter final : public Importer {
public:
    QString id() const override { return u"trilium"_s; }
    QString name() const override { return u"Trilium"_s; }
    QStringList fileTypes() const override { return {u"*.zip"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        const InputFile *meta = findMeta(files);
        if (!meta)
            return 0;
        // A damaged meta still means Trilium: the import explains what is wrong.
        return looksLikeMeta(meta->data) ? 100 : 70;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        if (const InputFile *meta = findMeta(files)) {
            TriliumImport(files, *meta, ctx).run();
        } else {
            ctx.warn(IoMessages::tr("This is no Trilium export: “!!!meta.json” is missing. Export the notes in "
                                    "Trilium with the format “HTML” or “Markdown” and choose the ZIP file."));
        }
        return ctx.take();
    }
};

} // namespace

std::unique_ptr<Importer> makeTriliumImporter()
{
    return std::make_unique<TriliumImporter>();
}

} // namespace brulion::io::detail
