// HTML importer: .html / .htm files (also a saved web page or a ZIP of a site) converted to
// Markdown; images (data: URIs and local files) and links to other imported files become
// attachments and wiki links.
#include "io/htmlmarkdown.h"
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/messages_p.h"
#include "util/paths.h"

#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

bool isHtml(const QString &path)
{
    const QString ext = paths::extension(path);
    return ext == u".html" || ext == u".htm" || ext == u".xhtml";
}

/// Text of an HTML file: a BOM or valid UTF-8 wins; otherwise the charset the page declares
/// (`<meta charset>`, e.g. windows-1250 / ISO-8859-2 of old Polish pages) when Qt supports it,
/// else Latin-1 (decodeText()).
QString decodeHtml(const QByteArray &data)
{
    const bool bom = data.startsWith("\xEF\xBB\xBF") || data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF");
    if (!bom) {
        QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        QString text = utf8(data);
        if (!utf8.hasError())
            return text;
        QStringDecoder declared = QStringDecoder::decoderForHtml(data);
        if (declared.isValid() && qstricmp(declared.name(), "UTF-8") != 0) {
            text = declared(data);
            if (!declared.hasError())
                return text;
        }
    }
    return decodeText(data);
}

/// A relative reference ("img/a b.png?x#y" -> "img/a b.png"); empty for URLs with a scheme.
QString relativeReference(const QString &ref)
{
    QString r = ref.trimmed();
    if (r.isEmpty() || r.startsWith(u'#') || r.startsWith(u"//"_s) || !QUrl(r).scheme().isEmpty())
        return {};
    const qsizetype cut = r.indexOf(QRegularExpression(u"[?#]"_s));
    if (cut >= 0)
        r.truncate(cut);
    return percentDecode(r);
}

class HtmlImporter final : public Importer {
public:
    QString id() const override { return u"html"_s; }
    QString name() const override { return u"HTML"_s; }
    QStringList fileTypes() const override { return {u"*.html"_s, u"*.htm"_s, u"*.zip"_s}; }

    int detect(const QList<InputFile> &files) const override { return anyPath(files, isHtml) ? 40 : 0; }

    ImportResult run(const QList<InputFile> &rawFiles, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        const QList<InputFile> files = stripCommonRoot(rawFiles);
        SourceTree pages;
        QHash<QString, const InputFile *> bySource;
        QList<std::pair<const InputFile *, VaultPath>> htmlFiles;
        for (const InputFile &f : files) {
            bySource.insert(pathKey(f.path), &f);
            if (!isHtml(f.path) || paths::isIgnored(f.path))
                continue;
            const QString stem = f.path.left(f.path.lastIndexOf(u'.'));
            const VaultPath path = ctx.claimPage(stem);
            pages.add(f.path, path);
            htmlFiles.append({&f, path});
        }
        QHash<QString, VaultPath> attachments; // source key -> vault path
        bool missing = false;
        for (const auto &[file, path] : std::as_const(htmlFiles)) {
            const QString dir = paths::dirname(file->path);
            const QString pageTitle = paths::pageTitle(path);
            int imageNumber = 0;
            const auto localFile = [&](const QString &ref) -> std::optional<VaultPath> {
                const QString rel = relativeReference(ref);
                if (rel.isEmpty())
                    return std::nullopt;
                const QString source = rel.startsWith(u'/') ? paths::normalize(rel) : paths::join(dir, rel);
                const InputFile *target = bySource.value(pathKey(source));
                if (!target)
                    return std::nullopt;
                if (isHtml(target->path))
                    return pages.destination(target->path);
                const auto it = attachments.constFind(pathKey(source));
                if (it != attachments.constEnd())
                    return *it;
                const VaultPath stored = ctx.addAttachment(paths::basename(target->path), target->data);
                attachments.insert(pathKey(source), stored);
                return stored;
            };
            HtmlToMarkdownOptions o;
            o.image = [&](const HtmlImage &img) -> QString {
                if (img.src.startsWith(u"data:"_s, Qt::CaseInsensitive)) {
                    const auto decoded = decodeDataUri(img.src);
                    if (!decoded)
                        return {};
                    const QString ext = extensionForMime(decoded->first);
                    const QString name =
                            pageTitle + u'-' + QString::number(++imageNumber) + (ext.isEmpty() ? u".bin"_s : ext);
                    return attachmentLink(ctx.addAttachment(name, decoded->second));
                }
                const QString scheme = QUrl(img.src.trimmed()).scheme().toLower();
                if (scheme == u"http" || scheme == u"https")
                    return u"!["_s + escapeMarkdownText(img.alt, false, true) + u"]("_s
                           + encodeLinkPath(img.src.trimmed()) + u')';
                if (const auto local = localFile(img.src))
                    return attachmentLink(*local);
                missing = true;
                return img.alt.isEmpty() ? QString() : escapeMarkdownText(img.alt, false);
            };
            o.link = [&](const QString &href, const QString &label) -> std::optional<QString> {
                const auto local = localFile(href);
                if (!local)
                    return std::nullopt;
                if (paths::isMarkdown(*local)) {
                    const QString target = paths::stripMarkdownExtension(*local);
                    const QString plain = QString(label).remove(u'\\').trimmed();
                    const bool same = plain.isEmpty()
                                      || plain.compare(paths::pageTitle(*local), Qt::CaseInsensitive) == 0
                                      || plain.contains(u'|') || plain.contains(u'[');
                    return wikiLink(target, {}, {}, same ? QString() : plain);
                }
                // A link (unlike an image) never embeds the file.
                QString plain = QString(label).remove(u'\\').trimmed();
                if (plain.contains(u'|') || plain.contains(u'[')
                    || plain.compare(paths::basename(*local), Qt::CaseInsensitive) == 0)
                    plain.clear();
                return wikiLink(*local, {}, {}, plain);
            };
            const HtmlMarkdown md = htmlToMarkdown(decodeHtml(file->data), o);
            QString body = md.markdown;
            QString title = md.title;
            if (body.startsWith(u"# "_s)) {
                const qsizetype eol = body.indexOf(u'\n');
                const QString h1 = body.sliced(2, (eol < 0 ? body.size() : eol) - 2).trimmed();
                if (title.isEmpty() || h1.compare(title, Qt::CaseInsensitive) == 0) {
                    title = h1;
                    body = eol < 0 ? QString() : body.sliced(eol + 1);
                }
            }
            QVariantMap fm;
            if (!title.isEmpty() && pageSegment(title).compare(pageTitle, Qt::CaseInsensitive) != 0)
                fm.insert(u"aliases"_s, QStringList{title});
            ctx.addPage(path, noteWithFrontmatter(fm, {}, body));
        }
        if (missing)
            ctx.warn(IoMessages::tr("Some images referenced by the HTML files are missing and were left out."));
        return ctx.take();
    }
};

} // namespace

std::unique_ptr<Importer> makeHtmlImporter()
{
    return std::make_unique<HtmlImporter>();
}

} // namespace brulion::io::detail
