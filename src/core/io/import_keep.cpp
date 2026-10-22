// Google Keep importer: Google Takeout's Keep folder (one JSON file per note, attachments next
// to them).
#include "io/htmlmarkdown.h"
#include "io/htmlutil_p.h"
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/messages_p.h"
#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

constexpr int MaxDetectFiles = 40;

bool isKeepNote(const QJsonObject &o)
{
    return (o.contains(u"textContent"_s) || o.contains(u"listContent"_s))
           && (o.contains(u"userEditedTimestampUsec"_s) || o.contains(u"createdTimestampUsec"_s)
               || o.contains(u"isTrashed"_s));
}

QDateTime fromUsec(const QJsonValue &v)
{
    const qint64 usec = v.isString() ? v.toString().toLongLong() : qint64(v.toDouble());
    if (usec <= 0)
        return {};
    return QDateTime::fromMSecsSinceEpoch(usec / 1000, QTimeZone::UTC);
}

/// The input files by path, by file name and by stem, built once (an export has thousands of
/// notes and attachments — no linear search per attachment).
class AttachmentFinder {
public:
    explicit AttachmentFinder(const QList<InputFile> &files)
    {
        for (const InputFile &f : files) {
            const QString base = paths::basename(f.path);
            m_byPath.insert(pathKey(f.path), &f);
            addFirst(m_byName, base.toCaseFolded(), &f);
            const QString stem = base.section(u'.', 0, -2);
            if (!stem.isEmpty() && paths::extension(f.path) != u".json")
                addFirst(m_byStem, stem.toCaseFolded(), &f);
        }
    }

    /// Input file of an attachment: next to the JSON, else anywhere by name; Takeout sometimes
    /// names the file ".jpeg" while the JSON says ".jpg" (and the other way round).
    const InputFile *find(const QString &jsonPath, const QString &filePath) const
    {
        if (const InputFile *f = m_byPath.value(pathKey(paths::join(paths::dirname(jsonPath), filePath))))
            return f;
        const QString name = paths::basename(filePath);
        if (const InputFile *f = m_byName.value(name.toCaseFolded()))
            return f;
        const QString stem = name.section(u'.', 0, -2);
        return stem.isEmpty() ? nullptr : m_byStem.value(stem.toCaseFolded());
    }

private:
    static void addFirst(QHash<QString, const InputFile *> &map, const QString &key, const InputFile *file)
    {
        if (!map.contains(key))
            map.insert(key, file);
    }

    QHash<QString, const InputFile *> m_byPath;
    QHash<QString, const InputFile *> m_byName; ///< first file with that name
    QHash<QString, const InputFile *> m_byStem; ///< first non-JSON file with that stem
};

QString listItem(const QString &text, bool checked)
{
    const QStringList lines = normalizeNewlines(text).split(u'\n');
    QString out = (checked ? u"- [x] "_s : u"- [ ] "_s) + lines.first();
    for (qsizetype k = 1; k < lines.size(); ++k)
        out += u"\n  "_s + lines[k];
    return out;
}

class KeepImporter final : public Importer {
public:
    QString id() const override { return u"keep"_s; }
    QString name() const override { return u"Google Keep"_s; }
    QStringList fileTypes() const override { return {u"*.zip"_s, u"*.json"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        int checked = 0;
        for (const InputFile &f : files) {
            if (paths::extension(f.path) != u".json")
                continue;
            if (isKeepNote(QJsonDocument::fromJson(f.data).object()))
                return 95;
            if (++checked >= MaxDetectFiles)
                break;
        }
        return 0;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        const AttachmentFinder finder(files);
        for (const InputFile &f : files) {
            if (paths::extension(f.path) != u".json")
                continue;
            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(f.data, &error);
            if (error.error != QJsonParseError::NoError) {
                ctx.warn(IoMessages::tr("“%1” is not valid JSON and was skipped.").arg(f.path));
                continue;
            }
            const QJsonObject o = doc.object();
            if (!isKeepNote(o) || o.value(u"isTrashed"_s).toBool())
                continue;
            importNote(o, f, finder, ctx);
        }
        return ctx.take();
    }

private:
    static void importNote(const QJsonObject &o, const InputFile &json, const AttachmentFinder &finder,
                           ImportContext &ctx)
    {
        const QTimeZone &zone = ctx.options().timeZone;
        const QString text = o.value(u"textContent"_s).toString();
        QStringList items;
        QString firstItem;
        for (const QJsonValue &v : o.value(u"listContent"_s).toArray()) {
            const QJsonObject item = v.toObject();
            const QString itemText = item.value(u"text"_s).toString();
            if (firstItem.isEmpty())
                firstItem = itemText;
            items.append(listItem(itemText, item.value(u"isChecked"_s).toBool()));
        }

        QString title = o.value(u"title"_s).toString().simplified();
        if (title.isEmpty())
            title = titleFromText(text.isEmpty() ? firstItem : text);
        if (title.isEmpty())
            title = paths::pageTitle(json.path.section(u'.', 0, -2) + u".md"_s);
        const VaultPath path = ctx.claimPage(pageSegment(title));

        QStringList parts;
        if (!text.trimmed().isEmpty())
            parts.append(normalizeNewlines(text));
        if (!items.isEmpty())
            parts.append(items.join(u'\n'));
        QStringList media;
        for (const QJsonValue &v : o.value(u"attachments"_s).toArray()) {
            const QString filePath = v.toObject().value(u"filePath"_s).toString();
            if (filePath.isEmpty())
                continue;
            const InputFile *file = finder.find(json.path, filePath);
            if (!file) {
                ctx.warn(IoMessages::tr("Some attachments referenced by notes are missing in the export."));
                continue;
            }
            media.append(attachmentLink(ctx.addAttachment(paths::basename(file->path), file->data)));
        }
        if (!media.isEmpty())
            parts.append(media.join(u'\n'));
        QStringList links;
        for (const QJsonValue &v : o.value(u"annotations"_s).toArray()) {
            const QJsonObject a = v.toObject();
            const QString url = a.value(u"url"_s).toString().trimmed();
            if (url.isEmpty())
                continue;
            const QString label = a.value(u"title"_s).toString().simplified();
            if (label.isEmpty())
                links.append(u"- <"_s + encodeLinkPath(url) + u'>');
            else
                links.append(u"- ["_s + escapeMarkdownText(label, false, true) + u"]("_s + encodeLinkPath(url) + u')');
        }
        if (!links.isEmpty())
            parts.append(links.join(u'\n'));

        QVariantMap fm;
        if (const QString c = wallClock(fromUsec(o.value(u"createdTimestampUsec"_s)), zone); !c.isEmpty())
            fm.insert(u"created"_s, c);
        if (const QString u = wallClock(fromUsec(o.value(u"userEditedTimestampUsec"_s)), zone); !u.isEmpty())
            fm.insert(u"updated"_s, u);
        QStringList tags;
        for (const QJsonValue &v : o.value(u"labels"_s).toArray()) {
            const QString name = tagName(v.toObject().value(u"name"_s).toString());
            if (!name.isEmpty())
                tags.append(name);
        }
        if (!tags.isEmpty())
            fm.insert(u"tags"_s, tags);
        if (o.value(u"isPinned"_s).toBool())
            fm.insert(u"pinned"_s, true);
        if (o.value(u"isArchived"_s).toBool())
            fm.insert(u"archived"_s, true);
        const QString color = o.value(u"color"_s).toString().toLower();
        if (!color.isEmpty() && color != u"default")
            fm.insert(u"color"_s, color);
        addTitleAlias(fm, o.value(u"title"_s).toString().simplified());
        // Keep's text is written as it is, but its dollars never make formulas ("Cena $10,
        // Netflix 15$"): Keep has none.
        ctx.addPage(path, noteWithFrontmatter(fm,
                                              {u"aliases"_s, u"created"_s, u"updated"_s, u"tags"_s, u"pinned"_s,
                                               u"archived"_s, u"color"_s},
                                              escapeTextFormulas(parts.join(u"\n\n"_s))));
    }
};

} // namespace

std::unique_ptr<Importer> makeKeepImporter()
{
    return std::make_unique<KeepImporter>();
}

} // namespace brulion::io::detail
