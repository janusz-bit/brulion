// CSV and Airtable importers (docs/IMPORT.md "CSV and Airtable"): spreadsheets as pages with
// properties — one page per row with the columns as typed frontmatter properties and an Obsidian
// base showing them as a table, or one page with the whole table (csvimport.h has the mapping).
// Airtable adds its export's specifics: attachments "photo.jpg (https://…)" (downloaded into the
// input beforehand, see csvAttachmentInputPath(), else linked), "checked" checkboxes, linked
// records as links to the rows' pages, and the bundle of Brulion's Airtable download (the REST API
// with a personal access token: {"brulion": "airtable-export", "version": 1, "bases": [{"id",
// "name", "tables": [{"id", "name", "primaryFieldId", "fields": [{"id", "name", "type",
// "options"}], "records": [{"id", "createdTime", "fields": {…}}]}]}]}).
#include "io/csvimport_p.h"
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/messages_p.h"
#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

/// A list cell of the bundle: its items joined by a character no value has.
constexpr char16_t BundleListSeparator = 0x1F;

bool isAirtableBundle(const QJsonObject &o)
{
    return o.value(u"brulion"_s).toString() == u"airtable-export" && o.value(u"bases"_s).isArray();
}

bool isNoteFile(const QString &path)
{
    const QString ext = paths::extension(path);
    return paths::isMarkdown(path) || ext == u".html" || ext == u".htm" || ext == u".enex";
}

/// The header of a Todoist template ("TYPE,CONTENT,…"): the Todoist importer's.
bool isTodoistTemplate(const QByteArray &data)
{
    const QByteArray head = data.left(200).trimmed().toUpper();
    return head.startsWith("TYPE,CONTENT") || head.startsWith("\xEF\xBB\xBFTYPE,CONTENT")
           || head.startsWith("TYPE;CONTENT") || head.startsWith("\"TYPE\",\"CONTENT\"");
}

/// Notion's database exports carry a 32-character id in the name ("Tasks 0123…cdef.csv").
bool hasNotionId(const QString &path)
{
    static const QRegularExpression id(u"[ _-][0-9a-f]{32}(?:_all)?\\.csv$"_s,
                                       QRegularExpression::CaseInsensitiveOption);
    return id.match(path).hasMatch();
}

/// How sure is it an Airtable CSV export: its attachment addresses or its file name.
int airtableCsvScore(const InputFile &file)
{
    if (paths::basename(file.path).contains(u"-Grid view"_s, Qt::CaseInsensitive))
        return 85;
    const QString head = decodeText(file.data.left(256 * 1024));
    static const QRegularExpression url(
            u"\\((https://(?:dl\\.airtable\\.com|[a-z0-9-]+\\.airtableusercontent\\.com)/[^\\s)]+)\\)"_s,
            QRegularExpression::CaseInsensitiveOption);
    return url.match(head).hasMatch() ? 90 : 0;
}

class CsvImporter final : public Importer {
public:
    QString id() const override { return u"csv"_s; }
    QString name() const override { return u"CSV"_s; }
    QStringList fileTypes() const override { return {u"*.csv"_s, u"*.tsv"_s, u"*.zip"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        int csv = 0, notes = 0;
        for (const InputFile &f : files) {
            if (f.path.startsWith(u"_attachments/"_s))
                continue;
            if (looksLikeCsv(f)) {
                if (hasNotionId(f.path) || isTodoistTemplate(f.data))
                    return 0;
                ++csv;
            } else if (isNoteFile(f.path)) {
                ++notes;
            }
        }
        if (csv == 0)
            return 0;
        return notes > 0 ? 5 : 55;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        QStringList warnings;
        const QList<CsvFilePlan> plans = planCsv(files, options.csv, CsvFlavor::Generic, &warnings);
        for (const QString &w : std::as_const(warnings))
            ctx.warn(w);
        if (plans.isEmpty()) {
            ctx.warn(IoMessages::tr("No CSV file with rows was found."));
            return ctx.take();
        }
        importCsvPlans(plans, options.csv, ctx, files, QString());
        return ctx.take();
    }
};

/// A value of the Airtable API as cell text: lists joined by BundleListSeparator, attachments as
/// "name (url)", collaborators by name, linked records by the titles of their rows.
QString apiCellText(const QJsonValue &v, const QString &type, const QHash<QString, QString> &recordTitles)
{
    const auto joinList = [&](const QJsonArray &a) {
        QStringList parts;
        for (const auto &item : a) {
            const QString t = apiCellText(item, QString(), recordTitles).trimmed();
            if (!t.isEmpty())
                parts.append(t);
        }
        return parts.join(QChar(BundleListSeparator));
    };
    if (type == u"multipleRecordLinks") {
        QStringList titles;
        for (const auto &item : v.toArray()) {
            const QString id = item.isObject() ? item.toObject().value(u"id"_s).toString() : item.toString();
            const QString title =
                    recordTitles.value(id, item.isObject() ? item.toObject().value(u"name"_s).toString() : QString());
            if (!title.trimmed().isEmpty())
                titles.append(title.simplified());
        }
        return titles.join(QChar(BundleListSeparator));
    }
    if (type == u"multipleAttachments") {
        QStringList refs;
        for (const auto &item : v.toArray()) {
            const QJsonObject a = item.toObject();
            const QString url = a.value(u"url"_s).toString();
            if (url.isEmpty())
                continue;
            QString name = a.value(u"filename"_s).toString().simplified();
            name.remove(u'(').remove(u')');
            refs.append(u"%1 (%2)"_s.arg(name.isEmpty() ? u"attachment"_s : name, url));
        }
        return refs.join(u", "_s);
    }
    switch (v.type()) {
    case QJsonValue::String: return v.toString();
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (d == std::floor(d) && std::abs(d) < 1e15)
            return QString::number(qlonglong(d));
        return QString::number(d, 'g', 15);
    }
    case QJsonValue::Bool: return v.toBool() ? u"true"_s : u"false"_s;
    case QJsonValue::Array: return joinList(v.toArray());
    case QJsonValue::Object: {
        const QJsonObject o = v.toObject();
        // Collaborators {id, email, name}; AI text {state, value}; buttons {label, url}; errors.
        if (o.contains(u"name"_s))
            return o.value(u"name"_s).toString();
        if (o.contains(u"value"_s))
            return apiCellText(o.value(u"value"_s), QString(), recordTitles);
        if (o.contains(u"label"_s))
            return o.value(u"label"_s).toString();
        if (o.contains(u"email"_s))
            return o.value(u"email"_s).toString();
        return {};
    }
    default: return {};
    }
}

/// The column plan of an Airtable field type ({} for types left to inference).
std::optional<CsvColumnPlan> fieldPlan(const QJsonObject &field)
{
    const QString type = field.value(u"type"_s).toString();
    CsvColumnPlan p;
    p.listSeparator = QString(QChar(BundleListSeparator));
    const auto as = [&p](CsvColumnType t) {
        p.inferred = t;
        p.type = t;
        return p;
    };
    if (type == u"number" || type == u"currency" || type == u"percent" || type == u"rating" || type == u"count"
        || type == u"autoNumber" || type == u"duration")
        return as(CsvColumnType::Number);
    if (type == u"checkbox")
        return as(CsvColumnType::Checkbox);
    if (type == u"date")
        return as(CsvColumnType::Date);
    if (type == u"dateTime" || type == u"createdTime" || type == u"lastModifiedTime")
        return as(CsvColumnType::DateTime);
    if (type == u"multipleSelects")
        return as(CsvColumnType::List);
    if (type == u"multipleRecordLinks")
        return as(CsvColumnType::Links);
    if (type == u"multipleAttachments") {
        p.attachments = true;
        p.listSeparator = u", "_s;
        return as(CsvColumnType::Attachments);
    }
    if (type == u"multipleCollaborators" || type == u"multipleLookupValues")
        return as(CsvColumnType::List);
    if (type == u"button")
        return as(CsvColumnType::Skip);
    if (type == u"singleLineText" || type == u"email" || type == u"url" || type == u"phoneNumber"
        || type == u"singleSelect" || type == u"multilineText" || type == u"richText" || type == u"singleCollaborator"
        || type == u"createdBy" || type == u"lastModifiedBy" || type == u"barcode" || type == u"aiText")
        return as(CsvColumnType::Text);
    return std::nullopt; // formula, rollup, lookup …: inferred from the values
}

class AirtableImporter final : public Importer {
public:
    QString id() const override { return u"airtable"_s; }
    QString name() const override { return u"Airtable"_s; }
    QStringList fileTypes() const override { return {u"*.csv"_s, u"*.json"_s, u"*.zip"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        int best = 0;
        int checked = 0;
        for (const InputFile &f : files) {
            if (f.path.startsWith(u"_attachments/"_s))
                continue;
            if (paths::extension(f.path) == u".json" && f.data.contains("airtable-export")) {
                if (isAirtableBundle(QJsonDocument::fromJson(f.data).object()))
                    return 95;
            }
            if (looksLikeCsv(f) && !hasNotionId(f.path) && ++checked <= 20)
                best = std::max(best, airtableCsvScore(f));
        }
        if (best > 0 && anyPath(files, isNoteFile))
            return 10;
        return best;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        QStringList warnings;
        QList<CsvFilePlan> plans = planCsv(files, options.csv, CsvFlavor::Airtable, &warnings);
        for (const InputFile &f : files) {
            if (paths::extension(f.path) != u".json" || f.path.startsWith(u"_attachments/"_s))
                continue;
            QJsonParseError error{};
            const QJsonObject o = QJsonDocument::fromJson(f.data, &error).object();
            if (error.error != QJsonParseError::NoError || !isAirtableBundle(o)) {
                warnings.append(IoMessages::tr("“%1” is not an Airtable download and was skipped.").arg(f.path));
                continue;
            }
            plans += bundlePlans(o, options.csv, &warnings);
        }
        for (const QString &w : std::as_const(warnings))
            ctx.warn(w);
        if (plans.isEmpty()) {
            ctx.warn(IoMessages::tr("No Airtable table with records was found."));
            return ctx.take();
        }
        importCsvPlans(plans, options.csv, ctx, files, name());
        return ctx.take();
    }

private:
    /// The tables of a download as plans: field types decide the columns' types, linked records
    /// name the rows they link to.
    static QList<CsvFilePlan> bundlePlans(const QJsonObject &bundle, const CsvImportOptions &options,
                                          QStringList *warnings)
    {
        QList<CsvFilePlan> out;
        const QJsonArray bases = bundle.value(u"bases"_s).toArray();
        // Every record's title (its primary field), for the links of other tables.
        QHash<QString, QString> recordTitles;
        for (const auto &b : bases) {
            for (const auto &t : b.toObject().value(u"tables"_s).toArray()) {
                const QJsonObject table = t.toObject();
                const QString primary = primaryFieldName(table);
                for (const auto &r : table.value(u"records"_s).toArray()) {
                    const QJsonObject record = r.toObject();
                    const QString title =
                            apiCellText(record.value(u"fields"_s).toObject().value(primary), QString(), {})
                                    .simplified();
                    recordTitles.insert(record.value(u"id"_s).toString(), title);
                }
            }
        }
        int tableCount = 0;
        for (const auto &b : bases)
            tableCount += int(b.toObject().value(u"tables"_s).toArray().size());
        for (const auto &b : bases) {
            const QJsonObject base = b.toObject();
            const QString baseName = base.value(u"name"_s).toString().simplified();
            for (const auto &t : base.value(u"tables"_s).toArray()) {
                const QJsonObject table = t.toObject();
                CsvData data;
                data.name = table.value(u"name"_s).toString().simplified();
                if (data.name.isEmpty())
                    data.name = u"Table"_s;
                data.path = baseName.isEmpty() ? data.name : baseName + u'/' + data.name;
                const QString primary = primaryFieldName(table);
                QHash<QString, CsvColumnPlan> fixed;
                QHash<QString, QString> types;
                for (const auto &fv : table.value(u"fields"_s).toArray()) {
                    const QJsonObject field = fv.toObject();
                    const QString name = field.value(u"name"_s).toString();
                    if (name.isEmpty() || data.columns.contains(name) || data.columns.size() >= MaxCsvImportColumns)
                        continue;
                    data.columns.append(name);
                    types.insert(name, field.value(u"type"_s).toString());
                    if (auto plan = fieldPlan(field)) {
                        plan->primary = name == primary;
                        fixed.insert(name, *plan);
                    } else if (name == primary) {
                        CsvColumnPlan p;
                        p.primary = true;
                        fixed.insert(name, p);
                    }
                }
                for (const auto &r : table.value(u"records"_s).toArray()) {
                    if (data.rows.size() >= MaxCsvImportRows) {
                        data.truncated = true;
                        break;
                    }
                    const QJsonObject fields = r.toObject().value(u"fields"_s).toObject();
                    QStringList row;
                    for (const QString &c : std::as_const(data.columns)) {
                        QString cell = apiCellText(fields.value(c), types.value(c), recordTitles);
                        if (cell.size() > MaxCsvImportCellLength) {
                            cell.truncate(MaxCsvImportCellLength);
                            data.truncated = true;
                        }
                        row.append(cell);
                    }
                    data.rows.append(row);
                }
                auto plan = planData(std::move(data), options, CsvFlavor::Airtable, warnings, fixed);
                if (!plan)
                    continue;
                // Folders: <base>/<table> when there are several tables.
                if (tableCount > 1) {
                    plan->folder = bases.size() > 1 && !baseName.isEmpty()
                                           ? pageSegment(baseName) + u'/' + pageSegment(plan->data.name)
                                           : pageSegment(plan->data.name);
                }
                out.append(std::move(*plan));
            }
        }
        return out;
    }

    static QString primaryFieldName(const QJsonObject &table)
    {
        const QString primaryId = table.value(u"primaryFieldId"_s).toString();
        const QJsonArray fields = table.value(u"fields"_s).toArray();
        for (const auto &f : fields) {
            if (f.toObject().value(u"id"_s).toString() == primaryId)
                return f.toObject().value(u"name"_s).toString();
        }
        return fields.isEmpty() ? QString() : fields.first().toObject().value(u"name"_s).toString();
    }
};

} // namespace

std::unique_ptr<Importer> makeCsvImporter()
{
    return std::make_unique<CsvImporter>();
}

std::unique_ptr<Importer> makeAirtableImporter()
{
    return std::make_unique<AirtableImporter>();
}

} // namespace brulion::io::detail
