// Logseq importer: a Logseq graph folder (pages/, journals/, assets/, logseq/config.edn) or a
// ZIP of it.
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/logseq_p.h"
#include "io/messages_p.h"
#include "markdown/properties.h"
#include "util/paths.h"

#include <QRegularExpression>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

/// Folder of the graph: the part before "logseq/config.edn", "pages/" or "journals/".
QString graphRoot(const QList<InputFile> &files)
{
    std::optional<QString> best;
    for (const InputFile &f : files) {
        const QStringList segments = f.path.split(u'/');
        for (qsizetype k = 0; k + 1 < segments.size(); ++k) {
            const QString &s = segments[k];
            const bool marker =
                    s == u"pages" || s == u"journals" || (s == u"logseq" && segments[k + 1] == u"config.edn");
            if (!marker)
                continue;
            const QString root = segments.mid(0, k).join(u'/');
            if (!best || root.size() < best->size())
                best = root;
            break;
        }
    }
    return best.value_or(QString());
}

/// config.edn without `;` comments (outside strings) — Logseq's template and users comment out
/// old values next to the active ones.
QString stripEdnComments(const QString &edn)
{
    QString out;
    out.reserve(edn.size());
    bool inString = false;
    bool inComment = false;
    for (qsizetype i = 0; i < edn.size(); ++i) {
        const QChar c = edn[i];
        if (inComment) {
            if (c == u'\n') {
                inComment = false;
                out += c;
            }
            continue;
        }
        if (inString) {
            out += c;
            if (c == u'\\' && i + 1 < edn.size())
                out += edn[++i];
            else if (c == u'"')
                inString = false;
            continue;
        }
        if (c == u';') {
            inComment = true;
            continue;
        }
        if (c == u'"')
            inString = true;
        out += c;
    }
    return out;
}

/// `:key "value"` from comment-free config.edn (good enough for the string settings we need).
QString ednString(const QString &edn, const QString &key)
{
    const QRegularExpression re(QRegularExpression::escape(key) + u"\\s+\"([^\"]*)\""_s);
    const auto m = re.match(edn);
    return m.hasMatch() ? m.captured(1) : QString();
}

/// Date of a journal file name in Logseq's file-name format ("yyyy_MM_dd" by default).
std::optional<QDate> journalFileDate(const QString &stem, const QString &format)
{
    for (const QString &f : {format, u"yyyy_MM_dd"_s, u"yyyy-MM-dd"_s, u"yyyyMMdd"_s}) {
        if (f.isEmpty())
            continue;
        const QDate d = JournalTitleFormat(f).parse(stem).value_or(QDate());
        if (d.isValid())
            return d;
    }
    return std::nullopt;
}

struct Source {
    const InputFile *file = nullptr;
    QString relative; ///< path inside the graph
    std::optional<QDate> journal;
    VaultPath path; ///< destination
    LogseqPageProperties props;
    QString originalTitle; ///< Logseq page name when it could not be kept as file name
};

class LogseqImporter final : public Importer {
public:
    QString id() const override { return u"logseq"_s; }
    QString name() const override { return u"Logseq"_s; }
    QStringList fileTypes() const override { return {u"*.zip"_s, u"*.md"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        if (anyPath(files,
                    [](const QString &p) { return p == u"logseq/config.edn" || p.endsWith(u"/logseq/config.edn"); }))
            return 95;
        static const QRegularExpression journal(u"(^|/)journals/\\d{4}_\\d{2}_\\d{2}\\.md$"_s);
        const bool journals = anyPath(files, [](const QString &p) { return journal.match(p).hasMatch(); });
        const bool pages =
                anyPath(files, [](const QString &p) { return p.startsWith(u"pages/") || p.contains(u"/pages/"); });
        return journals && pages ? 75 : journals ? 50 : 0;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        const QString root = graphRoot(files);
        const QString prefix = root.isEmpty() ? QString() : root + u'/';

        QString edn;
        for (const InputFile &f : files) {
            if (f.path == prefix + u"logseq/config.edn")
                edn = stripEdnComments(decodeText(f.data));
        }
        const QString fileFormat = ednString(edn, u":journal/file-name-format"_s);
        QList<JournalTitleFormat> titleFormats;
        QString titleFormat = ednString(edn, u":journal/page-title-format"_s);
        if (titleFormat.isEmpty())
            titleFormat = ednString(edn, u":date-formatter"_s); // the older name of the setting
        if (!titleFormat.isEmpty())
            titleFormats.append(JournalTitleFormat(titleFormat));
        titleFormats.append(JournalTitleFormat(u"MMM do, yyyy"_s));

        QList<Source> pages;
        QList<Source> attachments;
        bool orgWarned = false;
        for (const InputFile &f : files) {
            if (!prefix.isEmpty() && !f.path.startsWith(prefix))
                continue;
            Source s;
            s.file = &f;
            s.relative = f.path.sliced(prefix.size());
            if (paths::isIgnored(s.relative))
                continue; // logseq/ (config, backups), .git/, …
            const QString ext = paths::extension(s.relative);
            if (ext == u".org" && !orgWarned) {
                ctx.warn(IoMessages::tr("Org-mode pages are not converted; they were imported as attachments."));
                orgWarned = true;
            }
            if (ext != u".md" && ext != u".markdown") {
                attachments.append(s);
                continue;
            }
            const QString folder = paths::dirname(s.relative);
            const QString stem = paths::pageTitle(s.relative);
            if (folder == u"journals")
                s.journal = journalFileDate(stem, fileFormat);
            s.props = takeLogseqPageProperties(decodeText(f.data));
            pages.append(std::move(s));
        }

        // Destinations.
        SourceTree tree;
        for (Source &s : pages) {
            if (s.journal) {
                s.path = ctx.journalPath(*s.journal);
            } else {
                const QString folder = paths::dirname(s.relative);
                QString pageName = s.props.values.value(u"title"_s).toString().trimmed();
                if (pageName.isEmpty()) {
                    pageName = folder == u"pages" || folder == u"journals"
                                       ? logseqPageNameFromFile(paths::basename(s.relative))
                                       : paths::stripMarkdownExtension(s.relative);
                }
                s.path = ctx.claimPage(pageName);
                if (paths::pageTitle(s.path).compare(paths::basename(pageName), Qt::CaseInsensitive) != 0)
                    s.originalTitle = pageName;
            }
            tree.add(s.relative, s.path);
        }
        for (Source &a : attachments) {
            QString rel = a.relative;
            if (rel.startsWith(u"assets/"))
                rel = rel.sliced(7);
            a.path = ctx.claimAttachment(rel);
            tree.add(a.relative, a.path);
        }

        for (const Source &s : std::as_const(pages)) {
            QString body = logseqBlocksToBrulion(s.props.body);
            body = convertJournalLinks(body, titleFormats, ctx);
            body = tree.rewrite(body, s.relative);
            const QString content = noteWithFrontmatter(frontmatter(s), {u"aliases"_s, u"tags"_s}, body);
            if (s.journal)
                ctx.addJournal(*s.journal, content);
            else
                ctx.addPage(s.path, content);
        }
        for (const Source &a : std::as_const(attachments))
            ctx.addFile(a.path, a.file->data);
        return ctx.take();
    }

private:
    /// `[[Oct 14th, 2026]]` -> `[[2026-10-14]]` (the vault's journal page name).
    static QString convertJournalLinks(const QString &body, const QList<JournalTitleFormat> &formats,
                                       const ImportContext &ctx)
    {
        return replaceLinks(body, [&](const FoundLink &l) -> std::optional<QString> {
            if (l.kind != LinkKind::Wiki && l.kind != LinkKind::Embed)
                return std::nullopt;
            const auto date = parseJournalTitle(l.target, formats);
            if (!date)
                return std::nullopt;
            return wikiLink(ctx.journalLinkTarget(*date), l.heading, l.blockId, l.display, l.kind == LinkKind::Embed);
        });
    }

    /// Page properties as frontmatter: alias:: -> aliases, tags:: -> tags, title:: dropped (the
    /// file name), other keys as written.
    static QVariantMap frontmatter(const Source &s)
    {
        QVariantMap fm;
        QStringList aliases;
        for (const QString &key : s.props.keys) {
            const QString raw = s.props.values.value(key).toString();
            if (key == u"title")
                continue;
            if (key == u"alias" || key == u"aliases") {
                aliases.append(markdown::parsePropertyValue(u"alias"_s, raw).toStringList());
            } else if (key == u"tags" || key == u"tag") {
                QStringList tags = fm.value(u"tags"_s).toStringList();
                for (const QString &t : markdown::parsePropertyValue(u"tags"_s, raw).toStringList()) {
                    const QString name = tagName(t);
                    if (!name.isEmpty())
                        tags.append(name);
                }
                if (!tags.isEmpty())
                    fm.insert(u"tags"_s, tags);
            } else if (key != u"id" && key != u"collapsed") {
                fm.insert(key, raw);
            }
        }
        if (!s.originalTitle.isEmpty())
            aliases.prepend(s.originalTitle);
        aliases = markdown::uniqueCaseInsensitive(aliases);
        if (!aliases.isEmpty())
            fm.insert(u"aliases"_s, aliases);
        return fm;
    }
};

} // namespace

std::unique_ptr<Importer> makeLogseqImporter()
{
    return std::make_unique<LogseqImporter>();
}

} // namespace brulion::io::detail
