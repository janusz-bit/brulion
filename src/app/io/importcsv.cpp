// The spreadsheet mapping of ImportController (CSV and Airtable): csvOptions as io::CsvImportOptions,
// the analysis as rows for the mapping card (ImportCsvMapping.qml), the choices of its menus, and
// the attachment cells' downloaded files joining the input.
#include "importcontroller.h"

#include "io/csvimport.h"
#include "util/paths.h"

#include <QSet>

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

namespace {

constexpr qsizetype MaxPreviewCell = 60;

QString shortCell(const QString &cell)
{
    QString s = cell.simplified();
    if (s.size() > MaxPreviewCell)
        s = s.left(MaxPreviewCell - 1) + u'…';
    return s;
}

QString separatorName(QChar c)
{
    if (c == u'\t')
        return u"tab"_s;
    return QString(c);
}

} // namespace

io::CsvImportOptions ImportController::csvImportOptions(const QVariantMap &o)
{
    io::CsvImportOptions out;
    out.mode = o.value(u"mode"_s).toString() == u"table" ? io::CsvImportOptions::Mode::Table
                                                         : io::CsvImportOptions::Mode::Pages;
    out.encoding = o.value(u"encoding"_s).toString();
    const QString separator = o.value(u"separator"_s).toString();
    if (separator == u"tab" || separator == u"\t")
        out.separator = u'\t';
    else if (separator.size() == 1)
        out.separator = separator.front();
    const QString header = o.value(u"header"_s).toString();
    if (header == u"yes")
        out.header = true;
    else if (header == u"no")
        out.header = false;
    out.titleColumn = o.value(u"titleColumn"_s).toString();
    out.titleTemplate = o.value(u"titleTemplate"_s).toString();
    out.folderTemplate = o.value(u"folderTemplate"_s).toString();
    out.bodyColumn = o.value(u"bodyColumn"_s).toString();
    const QString list = o.value(u"listSeparator"_s).toString();
    out.listSeparator = list == u"newline" ? u"\n"_s : list;
    out.base = o.value(u"base"_s, true).toBool();
    out.attachments = o.value(u"attachments"_s, true).toBool();
    const QVariantMap columns = o.value(u"columns"_s).toMap();
    for (auto it = columns.constBegin(); it != columns.constEnd(); ++it) {
        const QVariantMap c = it.value().toMap();
        io::CsvColumnOptions co;
        co.property = c.value(u"property"_s).toString();
        co.type = io::csvColumnTypeFromName(c.value(u"type"_s).toString()).value_or(io::CsvColumnType::Auto);
        out.columns.insert(it.key(), co);
    }
    return out;
}

void ImportController::setCsvOptions(const QVariantMap &options)
{
    if (options == m_csvOptions)
        return;
    m_csvOptions = options;
    Q_EMIT csvOptionsChanged();
    updateCsvMapping();
    if (!m_input.isEmpty() && (m_state == u"ready" || m_state == u"analyzing" || m_state == u"error"))
        scheduleAnalyze();
}

void ImportController::setCsvOption(const QString &key, const QVariant &value)
{
    QVariantMap o = m_csvOptions;
    if (!value.isValid()
        || (value.typeId() == QMetaType::QString && value.toString().isEmpty() && key != u"bodyColumn"))
        o.remove(key);
    else
        o.insert(key, value);
    setCsvOptions(o);
}

void ImportController::setCsvColumn(const QString &column, const QString &property, const QString &type)
{
    QVariantMap o = m_csvOptions;
    QVariantMap columns = o.value(u"columns"_s).toMap();
    QVariantMap c;
    if (!property.trimmed().isEmpty())
        c.insert(u"property"_s, property.trimmed());
    if (!type.isEmpty() && type != u"auto")
        c.insert(u"type"_s, type);
    if (c.isEmpty())
        columns.remove(column);
    else
        columns.insert(column, c);
    if (columns.isEmpty())
        o.remove(u"columns"_s);
    else
        o.insert(u"columns"_s, columns);
    setCsvOptions(o);
}

void ImportController::resetCsvMapping()
{
    setCsvOptions({});
}

void ImportController::setCsvFileIndex(int index)
{
    if (index == m_csvFileIndex || index < 0 || index >= m_csvAnalysis.files.size())
        return;
    m_csvFileIndex = index;
    updateCsvMapping();
}

QVariantList ImportController::csvTypeChoices() const
{
    return {
            QVariantMap{{u"value"_s, u"auto"_s}, {u"text"_s, tr("Detect")}},
            QVariantMap{{u"value"_s, u"text"_s}, {u"text"_s, tr("Text")}},
            QVariantMap{{u"value"_s, u"number"_s}, {u"text"_s, tr("Number")}},
            QVariantMap{{u"value"_s, u"date"_s}, {u"text"_s, tr("Date")}},
            QVariantMap{{u"value"_s, u"datetime"_s}, {u"text"_s, tr("Date and time")}},
            QVariantMap{{u"value"_s, u"checkbox"_s}, {u"text"_s, tr("Checkbox")}},
            QVariantMap{{u"value"_s, u"list"_s}, {u"text"_s, tr("List")}},
            QVariantMap{{u"value"_s, u"tags"_s}, {u"text"_s, tr("Tags")}},
            QVariantMap{{u"value"_s, u"links"_s}, {u"text"_s, tr("Links to pages")}},
            QVariantMap{{u"value"_s, u"attachments"_s}, {u"text"_s, tr("Attachments")}},
            QVariantMap{{u"value"_s, u"skip"_s}, {u"text"_s, tr("Leave out")}},
    };
}

QVariantList ImportController::csvEncodingChoices() const
{
    QVariantList out{QVariantMap{{u"value"_s, QString()}, {u"text"_s, tr("Detect")}}};
    for (const QString &e :
         {u"utf-8"_s, u"utf-16le"_s, u"utf-16be"_s, u"windows-1250"_s, u"iso-8859-2"_s, u"windows-1252"_s}) {
        QString text = e.toUpper();
        if (e == u"windows-1250")
            text = tr("Windows-1250 (Polish Windows)");
        else if (e == u"iso-8859-2")
            text = tr("ISO-8859-2 (Latin-2)");
        else if (e == u"windows-1252")
            text = tr("Windows-1252 (Western)");
        out.append(QVariantMap{{u"value"_s, e}, {u"text"_s, text}});
    }
    return out;
}

QVariantList ImportController::csvSeparatorChoices() const
{
    return {
            QVariantMap{{u"value"_s, QString()}, {u"text"_s, tr("Detect")}},
            QVariantMap{{u"value"_s, u","_s}, {u"text"_s, tr("Comma ,")}},
            QVariantMap{{u"value"_s, u";"_s}, {u"text"_s, tr("Semicolon ;")}},
            QVariantMap{{u"value"_s, u"tab"_s}, {u"text"_s, tr("Tab")}},
            QVariantMap{{u"value"_s, u"|"_s}, {u"text"_s, tr("Vertical bar |")}},
    };
}

QVariantList ImportController::csvTitleChoices() const
{
    QVariantList out;
    const QVariantMap file = m_csvMapping.value(u"file"_s).toMap();
    const QString detected = file.value(u"detectedTitle"_s).toString();
    out.append(QVariantMap{{u"value"_s, QString()},
                           {u"text"_s, detected.isEmpty() ? tr("Detect") : tr("Detect (%1)").arg(detected)}});
    for (const QVariant &c : file.value(u"columns"_s).toList()) {
        const QString name = c.toMap().value(u"name"_s).toString();
        out.append(QVariantMap{{u"value"_s, name}, {u"text"_s, name}});
    }
    return out;
}

QVariantList ImportController::csvBodyChoices() const
{
    QVariantList out;
    const QVariantMap file = m_csvMapping.value(u"file"_s).toMap();
    const QString detected = file.value(u"detectedBody"_s).toString();
    out.append(QVariantMap{{u"value"_s, QString()},
                           {u"text"_s, detected.isEmpty() ? tr("Detect (none)") : tr("Detect (%1)").arg(detected)}});
    out.append(QVariantMap{{u"value"_s, u"-"_s}, {u"text"_s, tr("None: every column is a property")}});
    for (const QVariant &c : file.value(u"columns"_s).toList()) {
        const QString name = c.toMap().value(u"name"_s).toString();
        out.append(QVariantMap{{u"value"_s, name}, {u"text"_s, name}});
    }
    return out;
}

QList<QPair<QString, QString>> ImportController::pendingAttachments() const
{
    QSet<QString> have;
    for (const io::InputFile &f : m_input)
        have.insert(f.path);
    QList<QPair<QString, QString>> out;
    QSet<QString> seen;
    for (const io::CsvFileAnalysis &f : m_csvAnalysis.files) {
        for (const io::CsvAttachmentRef &ref : f.attachments) {
            if (seen.contains(ref.url) || have.contains(io::csvAttachmentInputPath(ref.url, ref.name)))
                continue;
            seen.insert(ref.url);
            out.append({ref.url, ref.name});
        }
    }
    return out;
}

void ImportController::addInputFiles(const QList<io::InputFile> &files, const QStringList &warnings)
{
    if (m_state == u"importing" || m_input.isEmpty())
        return;
    QSet<QString> have;
    for (const io::InputFile &f : std::as_const(m_input))
        have.insert(f.path);
    // An input prepared already takes the new files prepared too (downloads: in memory).
    QStringList prepareWarnings;
    const QList<io::InputFile> added = m_inputPrepared ? io::prepareImportFiles(files, &prepareWarnings) : files;
    for (const io::InputFile &f : added) {
        if (have.contains(f.path))
            continue;
        m_input.append(f);
        m_inputBytes += f.size();
    }
    m_readWarnings += warnings;
    m_readWarnings += prepareWarnings;
    m_readWarnings.removeDuplicates();
    Q_EMIT inputChanged();
    analyze();
}

void ImportController::updateCsvMapping()
{
    QVariantMap m;
    const bool csv = effectiveId() == u"csv" || effectiveId() == u"airtable";
    m.insert(u"available"_s, csv && !m_csvAnalysis.files.isEmpty());
    if (m_csvFileIndex >= m_csvAnalysis.files.size())
        m_csvFileIndex = 0;
    QVariantList files;
    int attachments = 0, downloaded = 0;
    QSet<QString> have;
    for (const io::InputFile &f : std::as_const(m_input))
        have.insert(f.path);
    for (qsizetype i = 0; i < m_csvAnalysis.files.size(); ++i) {
        const io::CsvFileAnalysis &f = m_csvAnalysis.files[i];
        files.append(QVariantMap{{u"value"_s, int(i)}, {u"text"_s, f.name}});
        for (const io::CsvAttachmentRef &ref : f.attachments) {
            ++attachments;
            downloaded += have.contains(io::csvAttachmentInputPath(ref.url, ref.name));
        }
    }
    m.insert(u"files"_s, files);
    m.insert(u"fileIndex"_s, m_csvFileIndex);
    m.insert(u"attachments"_s, attachments);
    m.insert(u"downloadedAttachments"_s, downloaded);
    m.insert(u"warnings"_s, m_csvAnalysis.warnings);
    m.insert(u"customized"_s, !m_csvOptions.isEmpty());
    if (!m_csvAnalysis.files.isEmpty()) {
        const io::CsvFileAnalysis &f = m_csvAnalysis.files[m_csvFileIndex];
        const QVariantList typeChoices = csvTypeChoices();
        const auto typeText = [&typeChoices](io::CsvColumnType t) {
            const QString name = io::csvColumnTypeName(t);
            for (const QVariant &c : typeChoices) {
                if (c.toMap().value(u"value"_s).toString() == name)
                    return c.toMap().value(u"text"_s).toString();
            }
            return name;
        };
        const QVariantMap chosen = m_csvOptions.value(u"columns"_s).toMap();
        QVariantList columns;
        QStringList header;
        for (const io::CsvColumnInfo &c : f.columns) {
            const QVariantMap own = chosen.value(c.name).toMap();
            columns.append(QVariantMap{
                    {u"name"_s, c.name},
                    {u"property"_s, c.property},
                    {u"chosenProperty"_s, own.value(u"property"_s).toString()},
                    {u"type"_s, own.value(u"type"_s, u"auto"_s).toString()},
                    {u"typeText"_s, typeText(c.type)},
                    {u"inferredText"_s, typeText(c.inferred)},
                    {u"isTitle"_s, c.name == f.titleColumn},
                    {u"isBody"_s, c.name == f.bodyColumn},
                    {u"filled"_s, c.filled},
                    {u"sample"_s, c.sample},
            });
            header.append(shortCell(c.name));
        }
        QVariantList rows;
        for (qsizetype r = 0; r < f.rows.size(); ++r) {
            QStringList cells;
            for (const QString &cell : f.rows[r])
                cells.append(shortCell(cell));
            rows.append(QVariantMap{{u"title"_s, f.titles.value(r)}, {u"cells"_s, cells}});
        }
        const QString explicitTitle = m_csvOptions.value(u"titleColumn"_s).toString();
        const QString explicitBody = m_csvOptions.value(u"bodyColumn"_s).toString();
        m.insert(u"file"_s, QVariantMap{
                                    {u"name"_s, f.name},
                                    {u"path"_s, f.path},
                                    {u"encoding"_s, f.encoding},
                                    {u"encodingDetected"_s, f.encodingDetected},
                                    {u"separator"_s, separatorName(f.separator)},
                                    {u"header"_s, f.header},
                                    {u"rowCount"_s, f.rowCount},
                                    {u"truncated"_s, f.truncated},
                                    {u"titleColumn"_s, f.titleColumn},
                                    {u"bodyColumn"_s, f.bodyColumn},
                                    {u"detectedTitle"_s, explicitTitle.isEmpty() ? f.titleColumn : QString()},
                                    {u"detectedBody"_s, explicitBody.isEmpty() ? f.bodyColumn : QString()},
                                    {u"columns"_s, columns},
                                    {u"headerCells"_s, header},
                                    {u"rows"_s, rows},
                                    {u"titles"_s, f.titles},
                                    {u"samplePage"_s, f.samplePage},
                            });
    }
    m_csvMapping = m;
    Q_EMIT csvMappingChanged();
}

} // namespace brulion::ioviews
