// CSV as pages with properties (docs/IMPORT.md "CSV and Airtable"): the mapping of a spreadsheet's
// columns to frontmatter properties that the "csv" and "airtable" importers use, and the analysis
// the import screen and `brulion-cli import` show before anything is written. Part of the io
// module — see io/io.h for the overview.
//
// Reading: the encoding is detected (UTF-8 with or without a BOM, UTF-16 with or without one,
// windows-1250 / ISO-8859-2 for Polish exports, windows-1252) or chosen; the separator (`,` `;` tab
// `|`, Excel's `sep=;` first line) and the header row are detected or chosen; RFC 4180 quoting with
// line breaks inside cells. Bounded: at most MaxCsvImportRows rows, MaxCsvImportColumns columns and
// MaxCsvImportCellLength characters a cell — more is left out with a warning.
//
// Mapping: one page per row (the title from a column or a template "{{Name}} – {{Date}}", the
// page's folder from an optional template "{{Category}}"), the other columns as properties of
// inferred types — numbers (also "1 234,5"), dates and date-times (ISO, "20.11.2026",
// "11/20/2026 2:30pm", "20 listopada 2026"), checkboxes ("tak"/"nie", "checked", "✓"), lists by a
// separator, tags, links to the pages of other rows (Airtable's linked records), web addresses —,
// an optional body column, empty cells left out; or one page with the whole table.
#pragma once

#include "io/files.h"

#include <QChar>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimeZone>
#include <QVariant>

#include <optional>

namespace brulion::io {

inline constexpr int MaxCsvImportRows = 100000;
inline constexpr int MaxCsvImportColumns = 200;
inline constexpr int MaxCsvImportCellLength = 100000;
/// Rows of the analysis' preview.
inline constexpr int CsvPreviewRows = 8;

/// What a column becomes.
enum class CsvColumnType {
    Auto,     ///< inferred from the values (the analysis says what)
    Text,     ///< a text property
    Number,   ///< a number property ("1 234,50" -> 1234.5)
    Date,     ///< a date property (yyyy-MM-dd)
    DateTime, ///< a date-time property (yyyy-MM-ddTHH:mm, wall-clock time)
    Checkbox, ///< true / false
    List,     ///< a list, split by the list separator
    Tags,     ///< added to `tags` (each value a tag, split by the list separator)
    Links,    ///< a list of wiki links "[[Title]]" (split by the list separator)
    Skip,     ///< left out
    /// Attachments "photo.jpg (https://…)" (Airtable): links to the files (embedded in the page
    /// when they are pictures) when the files are in the input, else their web addresses.
    Attachments,
};

/// "auto", "text", "number", "date", "datetime", "checkbox", "list", "tags", "links", "skip",
/// "attachments".
QString csvColumnTypeName(CsvColumnType type);
std::optional<CsvColumnType> csvColumnTypeFromName(QStringView name);

/// The user's choice for one column (by its header text).
struct CsvColumnOptions {
    QString property;                         ///< frontmatter key; empty: from the header
    CsvColumnType type = CsvColumnType::Auto; ///< Auto: inferred

    friend bool operator==(const CsvColumnOptions &, const CsvColumnOptions &) = default;
};

struct CsvImportOptions {
    enum class Mode {
        Pages, ///< one page per row, the columns as properties
        Table, ///< one page per file with the whole table
    };
    Mode mode = Mode::Pages;
    /// "" = detected; else "utf-8", "utf-16le", "utf-16be", "windows-1250", "windows-1252" or
    /// "iso-8859-2" (text/decode.h encodingFromName(): also "cp1250", "latin2" …).
    QString encoding;
    /// Null: detected.
    QChar separator;
    /// Unset: detected.
    std::optional<bool> header;
    /// The column naming the page (its header text); empty: Name, Title, Nazwa, Tytuł … or the
    /// first column with distinct values. Ignored when titleTemplate is set.
    QString titleColumn;
    /// "{{Name}} – {{Date}}": `{{Column}}` is a cell (header text, any letter case), `{{row}}` the
    /// row's number; overrides titleColumn.
    QString titleTemplate;
    /// Folder of the page under the import folder, e.g. "{{Category}}/{{Subcategory}}" (empty: none).
    QString folderTemplate;
    /// The column that becomes the page's text; "" = detected (a long text column named Notes,
    /// Description, Notatki, Opis …), "-" = none.
    QString bodyColumn;
    /// Separator of list, tag and link cells; empty: detected per column (", " ";" "|" newline).
    QString listSeparator;
    /// By header text.
    QHash<QString, CsvColumnOptions> columns;
    /// Pages mode: an Obsidian base (`<name>.base`) showing the imported pages as a table.
    bool base = true;
    /// Pages mode: cells with web addresses of attachments ("photo.jpg (https://…)", Airtable)
    /// become embedded attachments when the file is in the input (downloaded beforehand, see
    /// csvAttachmentInputPath()); else links.
    bool attachments = true;

    friend bool operator==(const CsvImportOptions &, const CsvImportOptions &) = default;
};

/// One column as analysed.
struct CsvColumnInfo {
    QString name;     ///< header text (or "Column 3" without a header row)
    QString property; ///< the frontmatter key used
    CsvColumnType inferred = CsvColumnType::Text;
    CsvColumnType type = CsvColumnType::Text; ///< what is used (the choice or the inferred type)
    QString listSeparator;                    ///< for List / Tags / Links
    bool attachments = false;                 ///< holds attachment cells ("name (https://…)")
    int filled = 0;                           ///< non-empty cells
    QString sample;                           ///< the first non-empty cell (shortened)
};

/// One CSV file as analysed.
struct CsvFileAnalysis {
    QString path;                               ///< as in the input
    QString name;                               ///< the file name without ".csv" ("Książki")
    QString encoding = QStringLiteral("utf-8"); ///< the one used (text/decode.h encodingName())
    bool encodingDetected = true;
    QChar separator = u',';
    bool header = true;
    QList<CsvColumnInfo> columns;
    /// The first CsvPreviewRows rows (cells as read).
    QList<QStringList> rows;
    int rowCount = 0;
    QString titleColumn; ///< the column naming the pages ("" with a template)
    QString bodyColumn;  ///< "" when none
    /// Titles of the preview rows as the pages will be named.
    QStringList titles;
    /// The first row's page as it will be written (Pages mode), or the table page (Table mode)
    /// — shortened.
    QString samplePage;
    /// Web addresses of attachment cells (Pages mode with `attachments`), each once.
    QStringList attachmentUrls;
    bool truncated = false; ///< rows, columns or cells over the bounds were left out
};

/// The analysis of the CSV files of an input.
struct CsvAnalysis {
    QList<CsvFileAnalysis> files;
    QStringList warnings;
};

/// The kind of spreadsheet export (the importer).
enum class CsvFlavor {
    Generic,  ///< "csv"
    Airtable, ///< "airtable": attachments "name (url)", "checked", linked records
};

/// Analyse the CSV files (".csv", ".tsv", ".txt" with a separator) of `files` with `options`.
CsvAnalysis analyzeCsv(const QList<InputFile> &files, const CsvImportOptions &options,
                       CsvFlavor flavor = CsvFlavor::Generic, const QTimeZone &zone = QTimeZone::UTC);

/// Does this look like a CSV file (by name and content)?
bool looksLikeCsv(const InputFile &file);

/// Where a downloaded attachment of `url` goes in the input (its file name kept), so the import
/// finds it: "_attachments/<hash of url>/<name>".
QString csvAttachmentInputPath(const QString &url, const QString &name);

/// One attachment cell entry: Airtable writes "photo.jpg (https://dl.airtable.com/…)", several
/// separated by ", ".
struct CsvAttachmentRef {
    QString name;
    QString url;
};
/// The attachments of a cell (empty when it holds none).
QList<CsvAttachmentRef> parseAttachmentCell(const QString &cell);

/// The value of a cell as `type` ({} when it is empty or does not fit the type: Number of "abc").
/// Dates and date-times are wall-clock (an instant with "Z" or an offset is converted to `zone`).
QVariant csvCellValue(const QString &cell, CsvColumnType type,
                      const QString &listSeparator = QStringLiteral(", "), const QTimeZone &zone = QTimeZone::UTC,
                                                                    bool dayFirst = true);

} // namespace brulion::io
