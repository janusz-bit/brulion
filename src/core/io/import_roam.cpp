// Roam Research importer: the JSON export (pages with nested blocks). Daily pages become journal
// pages, blocks become an outline, referenced blocks get their uid as block id.
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/logseq_p.h"
#include "io/messages_p.h"
#include "markdown/inline.h"
#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

constexpr int MaxDepth = 64;

bool isRoamPage(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    return o.contains(u"title"_s)
           && (o.contains(u"children"_s) || o.contains(u"create-time"_s) || o.contains(u"edit-time"_s)
               || o.contains(u"uid"_s));
}

bool isRoamExport(const QJsonDocument &doc)
{
    if (!doc.isArray() || doc.array().isEmpty())
        return false;
    return isRoamPage(doc.array().first());
}

/// Roam uids may contain '_' (not allowed in block ids): mapped to '-'.
QString blockId(const QString &uid)
{
    QString out = uid;
    for (QChar &c : out) {
        const bool ok = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-';
        if (!ok)
            c = u'-';
    }
    return out;
}

/// Replace matches of `re` (found in `masked`, the line with code blanked) by `fn(match, line)`.
QString replaceInLine(const QString &line, const QRegularExpression &re,
                      const std::function<QString(const QRegularExpressionMatch &, const QString &)> &fn)
{
    const QString masked = markdown::maskInlineCode(line);
    QString out;
    qsizetype last = 0;
    for (auto it = re.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(line).sliced(last, m.capturedStart() - last);
        out += fn(m, line);
        last = m.capturedEnd();
    }
    if (last == 0)
        return line;
    return out + QStringView(line).sliced(last);
}

QString group(const QRegularExpressionMatch &m, const QString &line, int n)
{
    return line.sliced(m.capturedStart(n), m.capturedLength(n));
}

/// Roam inline syntax -> Brulion (outside code).
QString convertRoamText(const QString &text)
{
    static const QRegularExpression highlight(u"\\^\\^(.+?)\\^\\^"_s);
    static const QRegularExpression italic(u"(?<![_\\w])__(?=\\S)(.+?)(?<=\\S)__(?![_\\w])"_s);
    static const QRegularExpression blockEmbed(
            u"\\{\\{\\s*(?:\\[\\[)?embed(?:\\]\\])?\\s*:\\s*\\(\\(\\s*([^()\\s]+)\\s*\\)\\)\\s*\\}\\}"_s);
    static const QRegularExpression pageEmbed(
            u"\\{\\{\\s*(?:\\[\\[)?embed(?:\\]\\])?\\s*:\\s*\\[\\[(.+?)\\]\\]\\s*\\}\\}"_s);
    static const QRegularExpression blockRef(u"(?<![!(])\\(\\(([A-Za-z0-9_-]{4,})\\)\\)"_s);
    static const QRegularExpression aliasLink(u"\\[([^\\]\\[]+)\\]\\(\\[\\[([^\\]]+)\\]\\]\\)"_s);
    static const QRegularExpression todo(u"\\{\\{\\s*(?:\\[\\[)?(TODO|DONE)(?:\\]\\])?\\s*\\}\\}\\s?"_s);
    return mapLinesOutsideCode(text, [](const QString &input, const QString &) -> QString {
        QString line = input;
        line = replaceInLine(line, blockEmbed, [](const auto &m, const QString &l) -> QString {
            return u"!(("_s + blockId(group(m, l, 1)) + u"))"_s;
        });
        line = replaceInLine(line, pageEmbed, [](const auto &m, const QString &l) -> QString {
            return u"![["_s + group(m, l, 1) + u"]]"_s;
        });
        line = replaceInLine(line, blockRef, [](const auto &m, const QString &l) -> QString {
            return u"(("_s + blockId(group(m, l, 1)) + u"))"_s;
        });
        line = replaceInLine(line, aliasLink, [](const auto &m, const QString &l) -> QString {
            return u"[["_s + group(m, l, 2) + u'|' + group(m, l, 1) + u"]]"_s;
        });
        line = replaceInLine(line, todo, [](const auto &m, const QString &l) -> QString {
            return group(m, l, 1) == u"DONE" ? u"[x] "_s : u"[ ] "_s;
        });
        line = replaceInLine(line, highlight, [](const auto &m, const QString &l) -> QString {
            return u"=="_s + group(m, l, 1) + u"=="_s;
        });
        line = replaceInLine(line, italic,
                             [](const auto &m, const QString &l) -> QString { return u'*' + group(m, l, 1) + u'*'; });
        return line;
    });
}

/// Collect the uids of all `((uid))` references.
void collectRefs(const QJsonArray &blocks, QSet<QString> &refs, int depth)
{
    static const QRegularExpression ref(u"\\(\\(([A-Za-z0-9_-]{4,})\\)\\)"_s);
    if (depth > MaxDepth)
        return;
    for (const QJsonValue &v : blocks) {
        const QJsonObject b = v.toObject();
        for (auto it = ref.globalMatch(b.value(u"string"_s).toString()); it.hasNext();)
            refs.insert(it.next().captured(1));
        collectRefs(b.value(u"children"_s).toArray(), refs, depth + 1);
    }
}

void writeBlocks(const QJsonArray &blocks, int depth, const QSet<QString> &refs, QStringList &out)
{
    if (depth > MaxDepth)
        return;
    const QString indent(depth, u'\t');
    for (const QJsonValue &v : blocks) {
        const QJsonObject b = v.toObject();
        QString text = convertRoamText(b.value(u"string"_s).toString());
        text.replace(u"\r\n"_s, u"\n"_s);
        QStringList lines = text.split(u'\n');
        QString &first = lines.first();
        const int heading = b.value(u"heading"_s).toInt();
        if (heading >= 1 && heading <= 6 && !first.startsWith(u"[ ] "_s) && !first.startsWith(u"[x] "_s))
            first.prepend(QString(heading, u'#') + u' ');
        const QString uid = b.value(u"uid"_s).toString();
        QString idLine;
        if (!uid.isEmpty() && refs.contains(uid)) {
            if (first.trimmed().startsWith(u"```"_s))
                idLine = u"id:: "_s + blockId(uid);
            else
                first += u" ^"_s + blockId(uid);
        }
        out.append(indent + u"- "_s + first);
        for (qsizetype k = 1; k < lines.size(); ++k)
            out.append(lines[k].isEmpty() ? QString() : indent + u"  "_s + lines[k]);
        if (!idLine.isEmpty())
            out.append(indent + u"  "_s + idLine);
        writeBlocks(b.value(u"children"_s).toArray(), depth + 1, refs, out);
    }
}

class RoamImporter final : public Importer {
public:
    QString id() const override { return u"roam"_s; }
    QString name() const override { return u"Roam Research"_s; }
    QStringList fileTypes() const override { return {u"*.json"_s, u"*.zip"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        for (const InputFile &f : files) {
            if (paths::extension(f.path) == u".json" && isRoamExport(QJsonDocument::fromJson(f.data)))
                return 90;
        }
        return 0;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        QList<QJsonObject> pages;
        for (const InputFile &f : files) {
            if (paths::extension(f.path) != u".json")
                continue;
            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(f.data, &error);
            if (error.error != QJsonParseError::NoError) {
                ctx.warn(IoMessages::tr("“%1” is not valid JSON and was skipped.").arg(f.path));
                continue;
            }
            if (!isRoamExport(doc))
                continue;
            for (const QJsonValue &v : doc.array()) {
                if (v.toObject().contains(u"title"_s))
                    pages.append(v.toObject());
            }
        }
        QSet<QString> refs;
        for (const QJsonObject &p : std::as_const(pages))
            collectRefs(p.value(u"children"_s).toArray(), refs, 0);

        const QList<JournalTitleFormat> daily = {JournalTitleFormat(u"MMMM do, yyyy"_s)};
        static const QRegularExpression dailyUid(u"^(\\d{2})-(\\d{2})-(\\d{4})$"_s);
        for (const QJsonObject &p : std::as_const(pages)) {
            const QString title = p.value(u"title"_s).toString().trimmed();
            std::optional<QDate> date;
            if (const auto m = dailyUid.match(p.value(u"uid"_s).toString()); m.hasMatch()) {
                const QDate d(m.captured(3).toInt(), m.captured(1).toInt(), m.captured(2).toInt());
                if (d.isValid())
                    date = d;
            }
            if (!date)
                date = daily.first().parse(title);
            QStringList lines;
            writeBlocks(p.value(u"children"_s).toArray(), 0, refs, lines);
            QString body = convertJournalLinks(lines.join(u'\n'), daily, ctx);
            if (date) {
                if (!lines.isEmpty())
                    ctx.addJournal(*date, body + u'\n');
                continue;
            }
            const QString name = title.isEmpty() ? IoMessages::tr("Untitled") : title;
            const VaultPath path = ctx.claimPage(name);
            QVariantMap fm;
            if (paths::pageTitle(path).compare(paths::basename(name), Qt::CaseInsensitive) != 0)
                fm.insert(u"aliases"_s, QStringList{name});
            ctx.addPage(path, noteWithFrontmatter(fm, {}, body));
        }
        return ctx.take();
    }

private:
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
};

} // namespace

std::unique_ptr<Importer> makeRoamImporter()
{
    return std::make_unique<RoamImporter>();
}

} // namespace brulion::io::detail
