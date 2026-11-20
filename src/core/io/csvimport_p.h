// Internals of the CSV mapping (csvimport.h) shared by the CSV, Airtable and Todoist importers:
// reading a file, cell parsing, the per-column plan and a row's page. Internal to the io module.
#pragma once

#include "io/csvimport.h"
#include "io/importutil_p.h"
#include "text/decode.h"

#include <QDate>
#include <QTime>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace brulion::io::detail {

/// A CSV file as read: its header (names made unique) and rows (each as wide as the header).
struct CsvData {
    QString path;
    QString name; ///< file name without its extension
    text::TextEncoding encoding = text::TextEncoding::Utf8;
    bool encodingDetected = true;
    QChar separator = u',';
    bool header = true;
    QStringList columns;
    QList<QStringList> rows;
    bool truncated = false;
};

/// Read (decode, detect the separator and the header, parse) one file; bounded.
CsvData readCsv(const InputFile &file, const CsvImportOptions &options);
/// Is the first record a header (as Python's csv.Sniffer: typed columns below text names)?
bool detectHeader(const QList<QStringList> &records);

bool isCsvNumber(const QString &cell, double *value);
std::optional<bool> csvBoolean(const QString &cell);

enum class DateOrder { Auto, DayFirst, MonthFirst };
struct CsvDate {
    QDate date;
    std::optional<QTime> time;
    bool utc = false;       ///< an instant ("Z", an offset): date and time are UTC
    bool ambiguous = false; ///< "05/06/2026": day and month could be swapped
};
/// "2026-11-20", "20.11.2026", "11/20/2026", "20 listopada 2026", "Nov 20, 2026", with an optional
/// time "14:30", "2:30pm", "14:30:00Z", "T14:30:00+01:00".
std::optional<CsvDate> parseCsvDate(const QString &cell, DateOrder order = DateOrder::Auto);
/// A cell's items by `separator` (", " also splits at "," without the space; "\n" by lines),
/// trimmed, quotes around an item removed, empty items and repeats dropped.
QStringList splitList(const QString &cell, const QString &separator);
/// A frontmatter key from a header: characters YAML keys would need quotes for removed.
QString propertyKey(const QString &header);

/// What one column becomes.
struct CsvColumnPlan {
    QString name;
    QString property;
    CsvColumnType inferred = CsvColumnType::Text;
    CsvColumnType type = CsvColumnType::Text;
    QString listSeparator = QStringLiteral(", ");
    bool dayFirst = true;
    bool attachments = false;
    bool primary = false; ///< planData(): the column naming the rows (an API's primary field)
    int filled = 0;
    QString sample;
};

/// One file's plan.
struct CsvFilePlan {
    CsvData data;
    QList<CsvColumnPlan> columns;
    int titleColumn = -1; ///< -1: a template (or nothing usable: the row number)
    int bodyColumn = -1;
    /// The title of each row (template or column; empty when the row gives none).
    QStringList titles;
    /// Folder of the pages under the import folder ("" = the file's name when several files are
    /// imported, else none).
    QString folder;
};

/// The plan of one table: columns of `fixed` (by name) keep its type and separator (an API's
/// field types; `primary` marks the title column), the others are inferred;
/// nullopt (and a warning) when it has no rows.
std::optional<CsvFilePlan> planData(CsvData data, const CsvImportOptions &options, CsvFlavor flavor,
                                    QStringList *warnings, const QHash<QString, CsvColumnPlan> &fixed);
/// Columns of `plans` whose items are titles of rows become Links (unless chosen otherwise).
void inferLinks(QList<CsvFilePlan> &plans, const CsvImportOptions &options);
/// "Książki-Grid view" -> "Książki".
QString airtableTableName(const QString &fileStem);

/// Plans of every CSV file of `files` (links between rows of all of them inferred together).
QList<CsvFilePlan> planCsv(const QList<InputFile> &files, const CsvImportOptions &options, CsvFlavor flavor,
                           QStringList *warnings);

/// `{{Column}}` / `{{row}}` of `templ` filled in for row `row` of `plan`.
QString renderRowTemplate(const QString &templ, const CsvFilePlan &plan, int row);

/// The page of one row.
struct CsvRowPage {
    QVariantMap frontmatter;
    QStringList order; ///< frontmatter keys in column order
    QString body;
    int mismatched = 0; ///< cells that did not fit their column's type (kept as text)
};

/// How the conversion finds what lies outside the row.
struct CsvRowContext {
    /// Wiki-link text for a title (the page of another row, "[[Page]]" or "[[Path|Title]]"); unset:
    /// "[[title]]".
    std::function<QString(const QString &title)> link;
    /// An attachment of the input added to the vault: its vault path, or "" when it is not in the
    /// input.
    std::function<QString(const CsvAttachmentRef &ref)> attachment;
    QTimeZone zone = QTimeZone::UTC;
};

CsvRowPage csvRowPage(const CsvFilePlan &plan, int row, const CsvImportOptions &options, const CsvRowContext &context);

/// The whole table as a Markdown table page (bounded rows; `truncated` when rows were left out).
QString csvTablePage(const CsvFilePlan &plan, bool *truncated);

/// An Obsidian base showing the pages of `folder` as a table with `properties` as its columns.
QString csvBaseFile(const QString &folder, const QString &viewName, const QStringList &properties);

/// The import of `plans` into `ctx` (pages, attachments, the base; Table mode: table pages),
/// as the CSV and Airtable importers do it. `sourceName` goes into the pages' `source` key
/// (nothing when empty).
void importCsvPlans(const QList<CsvFilePlan> &plans, const CsvImportOptions &options, ImportContext &ctx,
                    const QList<InputFile> &files, const QString &sourceName);

} // namespace brulion::io::detail
