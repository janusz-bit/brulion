// Simplenote importer: the export ZIP's notes.json ("activeNotes"; trashed notes are skipped).
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/messages_p.h"
#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

bool isNotesJson(const InputFile &f)
{
    return paths::basename(f.path).compare(u"notes.json"_s, Qt::CaseInsensitive) == 0;
}

/// The note without its first non-empty line when that line is just `title` ("Title" or
/// "# Title"); otherwise — a longer line shortened for the title, a task, a list item — the whole
/// note, so nothing is lost.
QString bodyWithoutTitle(const QString &content, const QString &title)
{
    static const QRegularExpression headingMarks(u"^#{1,6}\\s+"_s);
    const QStringList lines = normalizeNewlines(content).split(u'\n');
    qsizetype k = 0;
    while (k < lines.size() && lines[k].trimmed().isEmpty())
        ++k;
    if (k == lines.size())
        return {};
    QString first = lines[k].trimmed();
    first.remove(headingMarks);
    if (first.trimmed() != title)
        return content;
    return lines.mid(k + 1).join(u'\n');
}

class SimplenoteImporter final : public Importer {
public:
    QString id() const override { return u"simplenote"_s; }
    QString name() const override { return u"Simplenote"_s; }
    QStringList fileTypes() const override { return {u"*.zip"_s, u"notes.json"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        for (const InputFile &f : files) {
            if (isNotesJson(f) && QJsonDocument::fromJson(f.data).object().contains(u"activeNotes"_s))
                return 95;
        }
        return 0;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        bool found = false;
        for (const InputFile &f : files) {
            if (!isNotesJson(f))
                continue;
            QJsonParseError error{};
            const QJsonObject root = QJsonDocument::fromJson(f.data, &error).object();
            if (error.error != QJsonParseError::NoError || !root.contains(u"activeNotes"_s)) {
                ctx.warn(IoMessages::tr("“%1” is not a Simplenote export and was skipped.").arg(f.path));
                continue;
            }
            found = true;
            for (const QJsonValue &v : root.value(u"activeNotes"_s).toArray())
                importNote(v.toObject(), ctx);
        }
        if (!found)
            ctx.warn(IoMessages::tr("No Simplenote notes (notes.json) were found."));
        return ctx.take();
    }

private:
    static void importNote(const QJsonObject &n, ImportContext &ctx)
    {
        const QString content = n.value(u"content"_s).toString();
        const QDateTime created = QDateTime::fromString(n.value(u"creationDate"_s).toString(), Qt::ISODateWithMs);
        const QDateTime modified = QDateTime::fromString(n.value(u"lastModified"_s).toString(), Qt::ISODateWithMs);
        QString title = titleFromText(content);
        QString body = bodyWithoutTitle(content, title);
        if (title.isEmpty()) {
            title = u"Simplenote "_s
                    + (created.isValid() ? created.toTimeZone(ctx.options().timeZone).toString(u"yyyy-MM-dd HH-mm"_s)
                                         : n.value(u"id"_s).toString().left(8));
            body = content;
        }
        QVariantMap fm;
        if (const QString c = wallClock(created, ctx.options().timeZone); !c.isEmpty())
            fm.insert(u"created"_s, c);
        if (const QString m = wallClock(modified, ctx.options().timeZone); !m.isEmpty())
            fm.insert(u"updated"_s, m);
        QStringList tags;
        for (const QJsonValue &t : n.value(u"tags"_s).toArray()) {
            const QString name = tagName(t.toString());
            if (!name.isEmpty())
                tags.append(name);
        }
        if (!tags.isEmpty())
            fm.insert(u"tags"_s, tags);
        if (n.value(u"pinned"_s).toBool())
            fm.insert(u"pinned"_s, true);
        ctx.addPage(ctx.claimPage(pageSegment(title)),
                    noteWithFrontmatter(fm, {u"created"_s, u"updated"_s, u"tags"_s, u"pinned"_s}, body));
    }
};

} // namespace

std::unique_ptr<Importer> makeSimplenoteImporter()
{
    return std::make_unique<SimplenoteImporter>();
}

} // namespace brulion::io::detail
