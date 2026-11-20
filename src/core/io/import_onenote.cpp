// OneNote importer (docs/IMPORT.md "OneNote"): the notebooks Brulion's Microsoft Graph download
// wrote (the cloudimport module): `onenote.json` {"brulion": "onenote-export", "version": 1,
// "notebooks": [{"id", "name", "groups": [{"id", "name", "groups": […], "sections": […]}],
// "sections": [{"id", "name", "pages": [{"id", "guid", "title", "level", "order", "created",
// "modified", "file": "pages/…html"}]}]}], "resources": {"<Graph resource URL>": {"file":
// "resources/…", "name"?, "type"?}}} next to the pages' HTML and the downloaded resources.
//
// Notebooks, section groups and sections become folders; pages become pages, a page's subpages
// (OneNote's page levels) a folder with its name next to it ("Plan.md" and "Plan/Szczegóły.md").
// The page HTML (Graph's output HTML) is converted with htmlToMarkdown(): to-do tags become tasks
// (`- [ ]` / `- [x]`), other note tags tags (#important), pictures (the full-resolution ones) and
// attached files attachments, links to other pages of the import wiki links, embedded videos
// links; OneNote's OCR text of pictures is dropped.
#include "io/htmlmarkdown.h"
#include "io/htmlutil_p.h"
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/links_p.h"
#include "io/messages_p.h"
#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

/// Pages larger than this are not converted (a link to the HTML file instead).
constexpr qsizetype MaxPageHtml = 8 * 1024 * 1024;
/// Deepest nesting of section groups followed.
constexpr int MaxGroupDepth = 16;

bool isManifest(const QJsonObject &o)
{
    return o.value(u"brulion"_s).toString() == u"onenote-export" && o.value(u"notebooks"_s).isArray();
}

/// A page-id GUID from a OneNote link ("…&page-id={3A…}&end", "page-id=3a…").
QString pageGuid(const QString &href)
{
    static const QRegularExpression re(u"page-id=\\{?([0-9a-fA-F-]{36})\\}?"_s);
    const auto m = re.match(href);
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

/// The page title of a onenote: link ("…Sekcja.one#Strona&section-id=…").
QString linkTitle(const QString &href)
{
    const qsizetype hash = href.indexOf(u'#');
    if (hash < 0)
        return {};
    qsizetype end = href.indexOf(u'&', hash + 1);
    if (end < 0)
        end = href.size();
    return percentDecode(QStringView(href).sliced(hash + 1, end - hash - 1)).simplified();
}

struct PageRef {
    QString id;
    QString guid;
    QString title;
    int level = 0;
    QString file;
    QString created;
    QString modified;
    VaultPath path;
};

struct SectionRef {
    QString name;
    QString folder; ///< relative to the target folder
    QList<PageRef> pages;
};

class OneNoteImporter final : public Importer {
public:
    QString id() const override { return u"onenote"_s; }
    QString name() const override { return u"OneNote"_s; }
    QStringList fileTypes() const override { return {u"*.json"_s, u"*.zip"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        for (const InputFile &f : files) {
            if (paths::basename(f.path) == u"onenote.json" && f.data.contains("onenote-export")
                && isManifest(QJsonDocument::fromJson(f.data).object()))
                return 95;
        }
        return 0;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        QHash<QString, const InputFile *> byPath;
        for (const InputFile &f : files)
            byPath.insert(f.path, &f);
        int manifests = 0;
        for (const InputFile &f : files) {
            if (paths::basename(f.path) != u"onenote.json")
                continue;
            QJsonParseError error{};
            const QJsonObject o = QJsonDocument::fromJson(f.data, &error).object();
            if (error.error != QJsonParseError::NoError || !isManifest(o)) {
                ctx.warn(IoMessages::tr("“%1” is not a OneNote download and was skipped.").arg(f.path));
                continue;
            }
            ++manifests;
            importManifest(o, paths::dirname(f.path), byPath, ctx);
        }
        if (manifests == 0)
            ctx.warn(IoMessages::tr("No OneNote download was found: sign in to Microsoft to download the notebooks."));
        return ctx.take();
    }

private:
    struct Run {
        ImportContext &ctx;
        QString root; ///< folder of onenote.json in the input
        const QHash<QString, const InputFile *> &files;
        QJsonObject resources;
        QList<SectionRef> sections;
        QHash<QString, VaultPath> pageByGuid;
        QHash<QString, VaultPath> pageByTitle; ///< folded title -> the first page
        QHash<QString, VaultPath> attachments; ///< resource URL -> vault path
        int missingResources = 0;
        int unresolvedLinks = 0;
        int largePages = 0;
        int missingPages = 0;
    };

    void importManifest(const QJsonObject &manifest, const QString &root,
                        const QHash<QString, const InputFile *> &files, ImportContext &ctx) const
    {
        Run run{ctx, root, files, manifest.value(u"resources"_s).toObject(), {}, {}, {}, {}, 0, 0, 0, 0};
        const QJsonArray notebooks = manifest.value(u"notebooks"_s).toArray();
        for (const auto &nv : notebooks) {
            const QJsonObject nb = nv.toObject();
            const QString name = nb.value(u"name"_s).toString().simplified();
            collect(nb, pageSegment(name.isEmpty() ? IoMessages::tr("Notebook") : name), 0, run);
        }
        // Paths first (links between pages need them), then the pages.
        for (SectionRef &s : run.sections)
            placePages(s, run);
        for (const SectionRef &s : std::as_const(run.sections)) {
            for (const PageRef &p : s.pages)
                writePage(p, run);
        }
        if (run.missingResources > 0)
            ctx.warn(IoMessages::tr("%n picture(s) or file(s) were not downloaded and were left out.", nullptr,
                                    run.missingResources));
        if (run.unresolvedLinks > 0)
            ctx.warn(IoMessages::tr("%n link(s) lead to OneNote pages outside the import; their text was kept.",
                                    nullptr, run.unresolvedLinks));
        if (run.largePages > 0)
            ctx.warn(IoMessages::tr("%n page(s) are too large to convert; they were saved as HTML files.", nullptr,
                                    run.largePages));
        if (run.missingPages > 0)
            ctx.warn(IoMessages::tr("%n page(s) were not downloaded and were left out.", nullptr, run.missingPages));
    }

    /// Sections of a notebook or section group (and of its groups) in order.
    static void collect(const QJsonObject &container, const QString &folder, int depth, Run &run)
    {
        for (const auto &sv : container.value(u"sections"_s).toArray()) {
            const QJsonObject s = sv.toObject();
            SectionRef section;
            section.name = s.value(u"name"_s).toString().simplified();
            section.folder =
                    folder + u'/' + pageSegment(section.name.isEmpty() ? IoMessages::tr("Section") : section.name);
            QList<std::pair<int, PageRef>> ordered;
            const QJsonArray pages = s.value(u"pages"_s).toArray();
            for (qsizetype i = 0; i < pages.size(); ++i) {
                const QJsonObject p = pages[i].toObject();
                PageRef ref;
                ref.id = p.value(u"id"_s).toString();
                ref.guid = p.value(u"guid"_s).toString().toLower().remove(u'{').remove(u'}');
                ref.title = p.value(u"title"_s).toString().simplified();
                ref.level = std::clamp(p.value(u"level"_s).toInt(), 0, 16);
                ref.file = p.value(u"file"_s).toString();
                ref.created = p.value(u"created"_s).toString();
                ref.modified = p.value(u"modified"_s).toString();
                ordered.append({p.contains(u"order"_s) ? p.value(u"order"_s).toInt() : int(i), ref});
            }
            std::ranges::stable_sort(ordered, [](const auto &a, const auto &b) { return a.first < b.first; });
            for (auto &o : ordered)
                section.pages.append(std::move(o.second));
            run.sections.append(std::move(section));
        }
        if (depth >= MaxGroupDepth)
            return;
        for (const auto &gv : container.value(u"groups"_s).toArray()) {
            const QJsonObject g = gv.toObject();
            const QString name = g.value(u"name"_s).toString().simplified();
            collect(g, folder + u'/' + pageSegment(name.isEmpty() ? IoMessages::tr("Section group") : name), depth + 1,
                    run);
        }
    }

    /// Paths of a section's pages: a page's subpages (the next pages one level deeper) go into a
    /// folder named after it.
    static void placePages(SectionRef &section, Run &run)
    {
        QStringList parents; // segments of the pages above, by level
        for (PageRef &p : section.pages) {
            QString title = p.title.isEmpty() ? IoMessages::tr("Untitled page") : p.title;
            const int level = std::min<int>(p.level, int(parents.size()));
            parents.resize(level);
            QStringList segments{section.folder};
            segments += parents;
            segments.append(pageSegment(title));
            p.path = run.ctx.claimPage(segments.join(u'/'));
            parents.append(paths::pageTitle(p.path));
            if (!p.guid.isEmpty())
                run.pageByGuid.insert(p.guid, p.path);
            if (!run.pageByTitle.contains(title.toCaseFolded()))
                run.pageByTitle.insert(title.toCaseFolded(), p.path);
        }
    }

    static const InputFile *inputFile(const Run &run, const QString &relative)
    {
        if (relative.isEmpty())
            return nullptr;
        const QString path = run.root.isEmpty() ? relative : run.root + u'/' + relative;
        return run.files.value(path, nullptr);
    }

    /// The vault path of a resource (added once), "" when it was not downloaded.
    static QString resource(const QString &url, const QString &fallbackName, Run &run)
    {
        if (const auto it = run.attachments.constFind(url); it != run.attachments.constEnd())
            return *it;
        const QJsonObject r = run.resources.value(url).toObject();
        const InputFile *f = inputFile(run, r.value(u"file"_s).toString());
        if (!f) {
            ++run.missingResources;
            return {};
        }
        QString name = r.value(u"name"_s).toString().trimmed();
        if (name.isEmpty())
            name = fallbackName;
        if (name.isEmpty())
            name = paths::basename(f->path);
        else if (paths::extension(name).isEmpty())
            name += extensionForMime(r.value(u"type"_s).toString());
        const VaultPath path = run.ctx.addAttachment(name, f->data);
        run.attachments.insert(url, path);
        return path;
    }

    /// Graph's page HTML made ready for htmlToMarkdown(): to-do tags as checkboxes, other tags as
    /// markers, attached files as links, the full-resolution pictures, no OCR alt texts.
    static QString prepare(QString html, QHash<QString, QString> *objectNames)
    {
        html.remove(QChar(FormulaOpen)).remove(QChar(FormulaClose));
        // Tags: data-tag="to-do", "to-do:completed", "important,question".
        static const QRegularExpression tagged(
                u"<([a-zA-Z][a-zA-Z0-9]*)\\b([^>]*?\\bdata-tag\\s*=\\s*\"([^\"]*)\"[^>]*)>"_s);
        QString out;
        qsizetype last = 0;
        for (auto it = tagged.globalMatch(html); it.hasNext();) {
            const auto m = it.next();
            out += QStringView(html).sliced(last, m.capturedEnd() - last);
            last = m.capturedEnd();
            QString insert;
            for (const QString &raw : m.captured(3).split(u',')) {
                const QString tag = raw.trimmed().toLower();
                if (tag == u"to-do")
                    insert.prepend(u"<input type=\"checkbox\">"_s);
                else if (tag == u"to-do:completed")
                    insert.prepend(u"<input type=\"checkbox\" checked>"_s);
                else if (!tag.isEmpty())
                    insert += u"BRULIONTAG"_s + QString::fromLatin1(tag.toUtf8().toHex()) + u"END "_s;
            }
            out += insert;
        }
        out += QStringView(html).sliced(last);
        html = out;
        // <object data-attachment="Plan.pdf" type="application/pdf" data="…/resources/…/$value"/>
        static const QRegularExpression object(u"<object\\b([^>]*?)/?>(?:\\s*</object>)?"_s,
                                               QRegularExpression::CaseInsensitiveOption);
        out.clear();
        last = 0;
        for (auto it = object.globalMatch(html); it.hasNext();) {
            const auto m = it.next();
            out += QStringView(html).sliced(last, m.capturedStart() - last);
            last = m.capturedEnd();
            const QString url = decodeHtmlEntities(htmlAttribute(m.captured(1), u"data"_s).value_or(QString()));
            const QString name =
                    decodeHtmlEntities(htmlAttribute(m.captured(1), u"data-attachment"_s).value_or(QString()));
            if (url.isEmpty())
                continue;
            objectNames->insert(url, name);
            out += u"<p><a href=\""_s + url.toHtmlEscaped() + u"\">"_s + (name.isEmpty() ? url : name).toHtmlEscaped()
                   + u"</a></p>"_s;
        }
        out += QStringView(html).sliced(last);
        html = out;
        // <img src="…" data-fullres-src="…" alt="OCR text">: the full resolution, no alt.
        static const QRegularExpression img(u"<img\\b([^>]*)>"_s, QRegularExpression::CaseInsensitiveOption);
        out.clear();
        last = 0;
        for (auto it = img.globalMatch(html); it.hasNext();) {
            const auto m = it.next();
            out += QStringView(html).sliced(last, m.capturedStart() - last);
            last = m.capturedEnd();
            const QString attrs = m.captured(1);
            QString src = htmlAttribute(attrs, u"data-fullres-src"_s).value_or(QString());
            if (src.isEmpty())
                src = htmlAttribute(attrs, u"src"_s).value_or(QString());
            // A paragraph of its own: after a list it would join the list's last item.
            out += u"<p><img src=\""_s + src + u"\"></p>"_s;
        }
        out += QStringView(html).sliced(last);
        return out;
    }

    static QString tagsBack(QString markdown)
    {
        static const QRegularExpression marker(u"BRULIONTAG([0-9a-f]+)END ?"_s);
        QString out;
        qsizetype last = 0;
        for (auto it = marker.globalMatch(markdown); it.hasNext();) {
            const auto m = it.next();
            out += QStringView(markdown).sliced(last, m.capturedStart() - last);
            last = m.capturedEnd();
            const QString tag =
                    tagName(QString::fromUtf8(QByteArray::fromHex(m.captured(1).toLatin1())).replace(u':', u'-'));
            if (!tag.isEmpty())
                out += u'#' + tag + u' ';
        }
        out += QStringView(markdown).sliced(last);
        return out;
    }

    void writePage(const PageRef &p, Run &run) const
    {
        const InputFile *f = inputFile(run, p.file);
        if (!f) {
            ++run.missingPages;
            return;
        }
        QVariantMap fm;
        const QTimeZone &zone = run.ctx.options().timeZone;
        if (const QDateTime c = QDateTime::fromString(p.created, Qt::ISODateWithMs); c.isValid())
            fm.insert(u"created"_s, wallClock(c, zone));
        if (const QDateTime u = QDateTime::fromString(p.modified, Qt::ISODateWithMs); u.isValid())
            fm.insert(u"updated"_s, wallClock(u, zone));
        fm.insert(u"source"_s, name());
        addTitleAlias(fm, p.title);
        if (f->data.size() > MaxPageHtml) {
            ++run.largePages;
            const VaultPath html = run.ctx.addAttachment(paths::pageTitle(p.path) + u".html"_s, f->data);
            run.ctx.addPage(p.path, noteWithFrontmatter(fm, {u"aliases"_s, u"created"_s, u"updated"_s, u"source"_s},
                                                        attachmentLink(html)));
            return;
        }
        QHash<QString, QString> objectNames;
        const QString html = prepare(decodeText(f->data), &objectNames);
        HtmlToMarkdownOptions o;
        o.embedsAsLinks = true;
        const QString pictureName = paths::pageTitle(p.path);
        o.image = [&run, &pictureName](const HtmlImage &image) -> QString {
            if (!run.resources.contains(image.src)) {
                const QString scheme = QUrl(image.src).scheme().toLower();
                if (scheme == u"https" || scheme == u"http")
                    return u"![]("_s + QString(image.src).replace(u' ', u"%20"_s) + u')';
                return {};
            }
            const QString path = resource(image.src, pictureName, run);
            return path.isEmpty() ? QString() : u"![["_s + path + u"]]"_s;
        };
        o.link = [&run, &objectNames](const QString &href, const QString &label) -> std::optional<QString> {
            if (run.resources.contains(href)) {
                const QString path = resource(href, objectNames.value(href), run);
                if (path.isEmpty())
                    return label;
                return attachmentLink(path, objectNames.value(href));
            }
            if (href.startsWith(u"onenote:"_s, Qt::CaseInsensitive) || href.contains(u"page-id="_s)) {
                VaultPath target = run.pageByGuid.value(pageGuid(href));
                if (target.isEmpty())
                    target = run.pageByTitle.value(linkTitle(href).toCaseFolded());
                if (target.isEmpty()) {
                    ++run.unresolvedLinks;
                    return label;
                }
                return wikiLink(paths::stripMarkdownExtension(target), {}, {},
                                label == paths::pageTitle(target) ? QString() : label);
            }
            return std::nullopt;
        };
        QString body = htmlToMarkdown(html, o).markdown;
        body = tagsBack(body);
        run.ctx.addPage(p.path, noteWithFrontmatter(fm, {u"aliases"_s, u"created"_s, u"updated"_s, u"source"_s}, body));
    }
};

} // namespace

std::unique_ptr<Importer> makeOneNoteImporter()
{
    return std::make_unique<OneNoteImporter>();
}

} // namespace brulion::io::detail
