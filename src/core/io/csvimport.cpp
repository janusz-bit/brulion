// CSV reading, detection and type inference for the CSV and Airtable importers (csvimport.h).
#include "io/csvimport.h"

#include "io/csvimport_p.h"

#include "calc/table.h"
#include "io/htmlmarkdown.h"
#include "io/htmlutil_p.h"
#include "io/importutil_p.h"
#include "io/links_p.h"
#include "io/messages_p.h"
#include "markdown/frontmatter.h"
#include "util/paths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace brulion::io {

using namespace Qt::StringLiterals;
using namespace detail;

namespace {

constexpr qsizetype SampleChars = 120;
/// Records read to detect the separator and the header.
constexpr int DetectRecords = 100;
/// Most rows a "one table page" holds (more: a warning, the rest left out).
constexpr int MaxTableRows = 5000;
/// Characters of the analysis' sample page.
constexpr qsizetype MaxSamplePage = 4000;

QString shortened(const QString &text, qsizetype max = SampleChars)
{
    QString s = text.simplified();
    if (s.size() <= max)
        return s;
    qsizetype cut = max;
    if (s.at(cut - 1).isHighSurrogate())
        --cut;
    return s.left(cut) + u'…';
}

const QStringList &columnTypeNames()
{
    static const QStringList names{u"auto"_s, u"text"_s, u"number"_s, u"date"_s, u"datetime"_s,   u"checkbox"_s,
                                   u"list"_s, u"tags"_s, u"links"_s,  u"skip"_s, u"attachments"_s};
    return names;
}

/// Header words (folded: lower case, no diacritics) of the columns that name a row.
bool isTitleHeader(const QString &folded)
{
    static const QSet<QString> words{u"name"_s,      u"title"_s,   u"nazwa"_s,  u"tytul"_s,   u"subject"_s,
                                     u"temat"_s,     u"content"_s, u"task"_s,   u"zadanie"_s, u"imie i nazwisko"_s,
                                     u"full name"_s, u"page"_s,    u"strona"_s, u"nazwisko"_s};
    return words.contains(folded);
}

bool isBodyHeader(const QString &folded)
{
    static const QSet<QString> words{u"notes"_s, u"note"_s,     u"notatki"_s, u"notatka"_s, u"description"_s,
                                     u"opis"_s,  u"body"_s,     u"content"_s, u"tresc"_s,   u"text"_s,
                                     u"tekst"_s, u"comments"_s, u"uwagi"_s,   u"details"_s, u"szczegoly"_s};
    return words.contains(folded);
}

bool isTagsHeader(const QString &folded)
{
    static const QSet<QString> words{u"tags"_s,       u"tag"_s,       u"tagi"_s,     u"labels"_s,        u"etykiety"_s,
                                     u"categories"_s, u"kategorie"_s, u"keywords"_s, u"slowa kluczowe"_s};
    return words.contains(folded);
}

/// Lower case without Polish diacritics and surrounding spaces ("Tytuł" -> "tytul").
QString foldHeader(const QString &header)
{
    QString s = header.trimmed().toLower();
    static const QString from = u"ąćęłńóśźż"_s;
    static const QString to = u"acelnoszz"_s;
    for (QChar &c : s) {
        if (const qsizetype i = from.indexOf(c); i >= 0)
            c = to.at(i);
    }
    return s.simplified();
}

// ---- Reading ---------------------------------------------------------------------------------

/// Bounded RFC 4180 parsing: CRLF / LF / CR end records outside quotes, `""` is a quote, a stray
/// quote inside an unquoted field is text.
struct Parsed {
    QList<QStringList> records;
    bool truncated = false;
};

Parsed parseBounded(QStringView text, QChar separator, int maxRecords, bool forDetection = false)
{
    Parsed out;
    QStringList row;
    QString field;
    bool quoted = false, wasQuoted = false, rowHasData = false;
    const auto endField = [&] {
        if (row.size() < MaxCsvImportColumns) {
            if (field.size() > MaxCsvImportCellLength) {
                field.truncate(MaxCsvImportCellLength);
                if (field.back().isHighSurrogate())
                    field.chop(1);
                field += u'…';
                out.truncated = true;
            }
            row.append(field);
        } else {
            out.truncated = true;
        }
        field.clear();
        wasQuoted = false;
    };
    const auto endRow = [&]() -> bool {
        endField();
        if (rowHasData || row.size() > 1 || !row.first().isEmpty())
            out.records.append(row);
        row.clear();
        rowHasData = false;
        return out.records.size() < maxRecords;
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == u'"') {
                if (i + 1 < text.size() && text[i + 1] == u'"') {
                    field += u'"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else if (field.size() <= MaxCsvImportCellLength) {
                field += c;
            } else {
                out.truncated = true;
            }
            continue;
        }
        if (c == separator) {
            endField();
            rowHasData = true;
        } else if (c == u'\r' || c == u'\n') {
            if (c == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n')
                ++i;
            if (!endRow()) {
                // More records follow (anything but blank lines).
                for (qsizetype k = i + 1; k < text.size(); ++k) {
                    if (!text[k].isSpace()) {
                        out.truncated = !forDetection;
                        break;
                    }
                }
                return out;
            }
        } else if (c == u'"' && field.isEmpty() && !wasQuoted) {
            quoted = true;
            wasQuoted = true;
            rowHasData = true;
        } else if (field.size() <= MaxCsvImportCellLength) {
            field += c;
            rowHasData = true;
        } else {
            out.truncated = true;
        }
    }
    if (rowHasData || !field.isEmpty() || !row.isEmpty())
        endRow();
    return out;
}

/// Excel's first line "sep=;" names the separator.
QChar excelSeparatorLine(QStringView text, qsizetype *skip)
{
    static const QRegularExpression re(u"^\"?sep=(.)\"?[ \\t]*(?:\\r\\n|\\n|\\r|$)"_s,
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.matchView(text.first(std::min<qsizetype>(text.size(), 16)));
    if (!m.hasMatch())
        return {};
    *skip = m.capturedEnd();
    return m.capturedView(1).front();
}

QChar detectSeparator(QStringView text, const QString &path)
{
    if (paths::extension(path) == u".tsv")
        return u'\t';
    QChar best = u',';
    int bestScore = 0;
    for (const QChar sep : {u',', u';', u'\t', u'|'}) {
        const Parsed p = parseBounded(text, sep, DetectRecords, true);
        if (p.records.isEmpty())
            continue;
        const qsizetype width = p.records.first().size();
        if (width < 2)
            continue;
        const int score =
                int(std::ranges::count_if(p.records, [width](const QStringList &r) { return r.size() == width; }));
        if (score > bestScore) {
            best = sep;
            bestScore = score;
        }
    }
    return best;
}

} // namespace

namespace detail {

bool isCsvNumber(const QString &cell, double *value)
{
    QString s = cell.trimmed();
    if (s.isEmpty() || s.size() > 40)
        return false;
    static const QRegularExpression plain(u"^-?(?:0|[1-9]\\d*)(?:[.,]\\d+)?(?:[eE][-+]?\\d+)?$"_s);
    static const QRegularExpression spaced(
            u"^-?[1-9]\\d{0,2}(?:[ \\x{00A0}\\x{202F}\\x{2009}]\\d{3})+(?:[.,]\\d+)?$"_s);
    static const QRegularExpression commaThousands(u"^-?[1-9]\\d{0,2}(?:,\\d{3})+(?:\\.\\d+)?$"_s);
    static const QRegularExpression dotThousands(u"^-?[1-9]\\d{0,2}(?:\\.\\d{3})+(?:,\\d+)?$"_s);
    // Leading zeros ("007", "00-950") and a plus sign ("+48 …") are codes, not numbers.
    if (s.startsWith(u"0") && s.size() > 1 && s.at(1).isDigit())
        return false;
    if (plain.match(s).hasMatch()) {
        s.replace(u',', u'.');
    } else if (spaced.match(s).hasMatch()) {
        s.remove(u' ').remove(QChar(0x00A0)).remove(QChar(0x202F)).remove(QChar(0x2009));
        s.replace(u',', u'.');
    } else if (commaThousands.match(s).hasMatch() && s.count(u',') > 1) {
        s.remove(u',');
    } else if (commaThousands.match(s).hasMatch() && s.contains(u'.')) {
        s.remove(u',');
    } else if (dotThousands.match(s).hasMatch() && (s.count(u'.') > 1 || s.contains(u','))) {
        s.remove(u'.');
        s.replace(u',', u'.');
    } else {
        return false;
    }
    bool ok = false;
    const double d = s.toDouble(&ok);
    if (!ok || !std::isfinite(d))
        return false;
    // Integers beyond 2^53 are identifiers (they would lose digits).
    if (std::abs(d) > 9007199254740992.0)
        return false;
    if (value)
        *value = d;
    return true;
}

std::optional<bool> csvBoolean(const QString &cell)
{
    const QString s = cell.trimmed().toLower();
    static const QSet<QString> yes{u"true"_s, u"yes"_s, u"tak"_s, u"checked"_s, u"✓"_s, u"✔"_s, u"☑"_s, u"prawda"_s};
    static const QSet<QString> no{u"false"_s, u"no"_s, u"nie"_s,   u"unchecked"_s, u"☐"_s,
                                  u"✗"_s,     u"✘"_s,  u"fałsz"_s, u"falsz"_s};
    if (yes.contains(s))
        return true;
    if (no.contains(s))
        return false;
    return std::nullopt;
}

namespace {

int monthNumber(QStringView word)
{
    QString w = word.toString().toLower();
    if (w.endsWith(u'.'))
        w.chop(1);
    static const QStringList english{u"january"_s,   u"february"_s, u"march"_s,    u"april"_s,
                                     u"may"_s,       u"june"_s,     u"july"_s,     u"august"_s,
                                     u"september"_s, u"october"_s,  u"november"_s, u"december"_s};
    static const QStringList polishGenitive{u"stycznia"_s, u"lutego"_s,       u"marca"_s,     u"kwietnia"_s,
                                            u"maja"_s,     u"czerwca"_s,      u"lipca"_s,     u"sierpnia"_s,
                                            u"września"_s, u"października"_s, u"listopada"_s, u"grudnia"_s};
    static const QStringList polishNominative{u"styczeń"_s,  u"luty"_s,        u"marzec"_s,   u"kwiecień"_s,
                                              u"maj"_s,      u"czerwiec"_s,    u"lipiec"_s,   u"sierpień"_s,
                                              u"wrzesień"_s, u"październik"_s, u"listopad"_s, u"grudzień"_s};
    static const QStringList polishShort{u"sty"_s, u"lut"_s, u"mar"_s, u"kwi"_s, u"maj"_s, u"cze"_s,
                                         u"lip"_s, u"sie"_s, u"wrz"_s, u"paź"_s, u"lis"_s, u"gru"_s};
    for (int i = 0; i < 12; ++i) {
        if (w == english[i] || w == polishGenitive[i] || w == polishNominative[i] || w == polishShort[i]
            || (w.size() >= 3 && english[i].startsWith(w) && w.size() <= 4))
            return i + 1;
    }
    return 0;
}

} // namespace

std::optional<CsvDate> parseCsvDate(const QString &cell, DateOrder order)
{
    const QString s = cell.trimmed();
    if (s.size() < 6 || s.size() > 48)
        return std::nullopt;
    static const QRegularExpression timeRe(
            u"(?:[ T]+|,\\s*)(\\d{1,2})[:.](\\d{2})(?::(\\d{2})(?:[.,]\\d+)?)?\\s*([aApP]\\.?[mM]\\.?)?\\s*(Z|[+-]\\d{2}:?\\d{2}|UTC|GMT)?$"_s);
    QString datePart = s;
    std::optional<QTime> time;
    QString zone;
    if (const auto m = timeRe.match(s); m.hasMatch() && m.capturedStart() > 0) {
        int hour = m.captured(1).toInt();
        const int minute = m.captured(2).toInt();
        const int second = m.captured(3).toInt();
        if (const QString ampm = m.captured(4).toLower(); !ampm.isEmpty()) {
            if (hour < 1 || hour > 12)
                return std::nullopt;
            const bool pm = ampm.startsWith(u'p');
            hour = (hour % 12) + (pm ? 12 : 0);
        }
        const QTime t(hour, minute, second);
        if (!t.isValid())
            return std::nullopt;
        time = t;
        zone = m.captured(5);
        datePart = s.first(m.capturedStart());
    }
    datePart = datePart.trimmed();
    if (datePart.endsWith(u','))
        datePart.chop(1);
    QDate date;
    bool ambiguous = false;
    static const QRegularExpression iso(u"^(\\d{4})[-/.](\\d{1,2})[-/.](\\d{1,2})$"_s);
    static const QRegularExpression numeric(u"^(\\d{1,2})([./-])(\\d{1,2})\\2(\\d{4}|\\d{2})(?:\\s*r\\.?)?$"_s);
    static const QRegularExpression dayMonthName(u"^(\\d{1,2})\\.?\\s+(\\p{L}+\\.?)\\s+(\\d{4})(?:\\s*r\\.?)?$"_s);
    static const QRegularExpression monthNameDay(u"^(\\p{L}+\\.?)\\s+(\\d{1,2})(?:st|nd|rd|th)?,?\\s+(\\d{4})$"_s);
    if (const auto m = iso.match(datePart); m.hasMatch()) {
        date = QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    } else if (const auto m2 = numeric.match(datePart); m2.hasMatch()) {
        const int a = m2.captured(1).toInt();
        const int b = m2.captured(3).toInt();
        int year = m2.captured(4).toInt();
        if (m2.capturedLength(4) == 2)
            year += year < 70 ? 2000 : 1900;
        bool dayFirst = m2.captured(2) != u"/"_s;
        if (order == DateOrder::DayFirst)
            dayFirst = true;
        else if (order == DateOrder::MonthFirst)
            dayFirst = false;
        if (a > 12 && b <= 12)
            dayFirst = true;
        else if (b > 12 && a <= 12)
            dayFirst = false;
        ambiguous = a <= 12 && b <= 12 && a != b;
        date = dayFirst ? QDate(year, b, a) : QDate(year, a, b);
    } else if (const auto m3 = dayMonthName.match(datePart); m3.hasMatch()) {
        if (const int month = monthNumber(m3.capturedView(2)))
            date = QDate(m3.captured(3).toInt(), month, m3.captured(1).toInt());
    } else if (const auto m4 = monthNameDay.match(datePart); m4.hasMatch()) {
        if (const int month = monthNumber(m4.capturedView(1)))
            date = QDate(m4.captured(3).toInt(), month, m4.captured(2).toInt());
    }
    if (!date.isValid() || date.year() < 1000 || date.year() > 9999)
        return std::nullopt;
    CsvDate out;
    out.date = date;
    out.time = time;
    out.ambiguous = ambiguous;
    if (!zone.isEmpty() && time) {
        out.utc = true;
        if (zone != u"Z" && zone != u"UTC" && zone != u"GMT") {
            const QStringView hhmm = QStringView(zone).sliced(1);
            const int minutes = hhmm.first(2).toInt() * 60 + hhmm.last(2).toInt();
            const QDateTime at(date, *time,
                               QTimeZone::fromSecondsAheadOfUtc((zone.startsWith(u'-') ? -1 : 1) * minutes * 60));
            if (!at.isValid())
                return std::nullopt;
            const QDateTime utc = at.toUTC();
            out.date = utc.date();
            out.time = utc.time();
        }
    }
    return out;
}

QStringList splitList(const QString &cell, const QString &separator)
{
    QStringList items;
    if (separator == u"\n") {
        items = QString(cell).replace(u"\r\n"_s, u"\n"_s).split(u'\n');
    } else {
        const QString sep = separator.trimmed().isEmpty() ? separator : separator.trimmed();
        items = cell.split(sep);
    }
    QStringList out;
    for (const QString &item : std::as_const(items)) {
        QString t = item.trimmed();
        if (t.size() >= 2 && t.startsWith(u'"') && t.endsWith(u'"'))
            t = t.sliced(1, t.size() - 2).trimmed();
        if (!t.isEmpty() && !out.contains(t))
            out.append(t);
    }
    return out;
}

QString propertyKey(const QString &header)
{
    QString s = header.simplified();
    s.removeIf([](QChar c) {
        return c == u':' || c == u'#' || c == u'{' || c == u'}' || c == u'[' || c == u']' || c == u',' || c == u'&'
               || c == u'*' || c == u'!' || c == u'|' || c == u'>' || c == u'\'' || c == u'"' || c == u'%' || c == u'@'
               || c == u'`' || c.category() == QChar::Other_Control || c.category() == QChar::Other_Format;
    });
    s = s.simplified();
    while (s.startsWith(u'-') || s.startsWith(u'?') || s.startsWith(u'.'))
        s = s.sliced(1).trimmed();
    if (s.size() > 80)
        s.truncate(80);
    return s;
}

} // namespace detail

// ---- Public helpers ---------------------------------------------------------------------------

QString csvColumnTypeName(CsvColumnType type)
{
    return columnTypeNames().value(int(type), u"text"_s);
}

std::optional<CsvColumnType> csvColumnTypeFromName(QStringView name)
{
    const qsizetype i = columnTypeNames().indexOf(name.trimmed().toString().toLower());
    if (i < 0)
        return std::nullopt;
    return CsvColumnType(int(i));
}

bool looksLikeCsv(const InputFile &file)
{
    const QString ext = paths::extension(file.path);
    if (ext != u".csv" && ext != u".tsv")
        return false;
    return !file.data.isEmpty();
}

QString csvAttachmentInputPath(const QString &url, const QString &name)
{
    const QByteArray hash = QCryptographicHash::hash(url.trimmed().toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    QString file = paths::basename(name.trimmed());
    if (file.isEmpty())
        file = u"attachment"_s;
    return u"_attachments/"_s + QString::fromLatin1(hash) + u'/' + file;
}

QList<CsvAttachmentRef> parseAttachmentCell(const QString &cell)
{
    QList<CsvAttachmentRef> out;
    const QString s = cell.trimmed();
    if (!s.endsWith(u')') || !s.contains(u"(http"_s))
        return out;
    // "name (https://…)" items joined by "," (Airtable) — a name may hold commas and parentheses,
    // an address no spaces.
    static const QRegularExpression item(u"\\s*(.*?)\\s*\\((https?://[^\\s()]+)\\)\\s*(?:,|$)"_s);
    qsizetype pos = 0;
    while (pos < s.size()) {
        const auto m =
                item.match(s, pos, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        if (!m.hasMatch() || m.capturedLength() == 0)
            return {};
        out.append(CsvAttachmentRef{m.captured(1).trimmed(), m.captured(2)});
        pos = m.capturedEnd();
        if (out.size() > 500)
            break;
    }
    return out;
}

QVariant csvCellValue(const QString &cell, CsvColumnType type, const QString &listSeparator, const QTimeZone &zone,
                      bool dayFirst)
{
    const QString s = cell.trimmed();
    switch (type) {
    case CsvColumnType::Skip: return {};
    case CsvColumnType::Checkbox: {
        if (s.isEmpty())
            return false;
        const auto b = csvBoolean(s);
        if (b)
            return *b;
        if (s == u"1")
            return true;
        if (s == u"0")
            return false;
        return {};
    }
    case CsvColumnType::Auto:
    case CsvColumnType::Text:
        return s.isEmpty() ? QVariant() : QVariant(QString(cell).replace(u"\r\n"_s, u"\n"_s).trimmed());
    case CsvColumnType::Number: {
        double d = 0;
        if (!isCsvNumber(s, &d))
            return {};
        if (d == std::floor(d) && std::abs(d) < 1e15)
            return QVariant(qlonglong(d));
        return d;
    }
    case CsvColumnType::Date:
    case CsvColumnType::DateTime: {
        const auto d = parseCsvDate(s, dayFirst ? DateOrder::DayFirst : DateOrder::MonthFirst);
        if (!d)
            return {};
        if (type == CsvColumnType::Date || !d->time) {
            QDate day = d->date;
            if (d->utc && d->time)
                day = QDateTime(d->date, *d->time, QTimeZone::UTC).toTimeZone(zone).date();
            return day;
        }
        QDateTime at(d->date, *d->time);
        if (d->utc)
            at = QDateTime(d->date, *d->time, QTimeZone::UTC).toTimeZone(zone);
        // Wall-clock: written without a zone (yyyy-MM-ddTHH:mm).
        const QTime t = at.time();
        return u"%1T%2"_s.arg(at.date().toString(u"yyyy-MM-dd"_s), t.toString(t.second() ? u"HH:mm:ss"_s : u"HH:mm"_s));
    }
    case CsvColumnType::List:
    case CsvColumnType::Tags:
    case CsvColumnType::Links:
    case CsvColumnType::Attachments: {
        const QStringList items = splitList(cell, listSeparator.isEmpty() ? u", "_s : listSeparator);
        if (items.isEmpty())
            return {};
        return items;
    }
    }
    return {};
}

// ---- Reading a file -----------------------------------------------------------------------------

namespace detail {

CsvData readCsv(const InputFile &file, const CsvImportOptions &options)
{
    CsvData out;
    out.path = file.path;
    QString stem = paths::basename(file.path);
    if (const qsizetype dot = stem.lastIndexOf(u'.'); dot > 0)
        stem.truncate(dot);
    out.name = stem.trimmed().isEmpty() ? u"CSV"_s : stem.trimmed();
    const text::DetectedEncoding detected = text::detectEncoding(file.data);
    const auto chosen = text::encodingFromName(options.encoding);
    out.encodingDetected = !chosen.has_value();
    out.encoding = chosen.value_or(detected.encoding);
    QString content = text::decodeTextAs(file.data, out.encoding);
    if (content.startsWith(QChar(0xFEFF)))
        content.remove(0, 1);
    QStringView view = content;
    qsizetype skip = 0;
    QChar separator = excelSeparatorLine(view, &skip);
    view = view.sliced(skip);
    if (!options.separator.isNull())
        separator = options.separator;
    if (separator.isNull())
        separator = detectSeparator(view, file.path);
    out.separator = separator;
    Parsed parsed = parseBounded(view, separator, MaxCsvImportRows + 1);
    out.truncated = parsed.truncated;
    QList<QStringList> &records = parsed.records;
    // Trailing all-empty records (spreadsheets write rows of separators).
    while (!records.isEmpty()
           && std::ranges::all_of(records.last(), [](const QString &c) { return c.trimmed().isEmpty(); }))
        records.removeLast();
    if (records.isEmpty())
        return out;
    out.header = options.header.value_or(detectHeader(records));
    qsizetype width = 0;
    for (const QStringList &r : std::as_const(records))
        width = std::max(width, r.size());
    QStringList header;
    if (out.header) {
        header = records.takeFirst();
        for (QString &h : header)
            h = h.simplified();
    }
    while (header.size() < width)
        header.append(QString());
    // Names for unnamed and repeated columns.
    QSet<QString> used;
    for (qsizetype i = 0; i < header.size(); ++i) {
        QString h = header[i];
        if (h.isEmpty())
            h = IoMessages::tr("Column %1").arg(i + 1);
        QString unique = h;
        for (int n = 2; used.contains(unique.toCaseFolded()); ++n)
            unique = u"%1 %2"_s.arg(h).arg(n);
        used.insert(unique.toCaseFolded());
        header[i] = unique;
    }
    if (records.size() > MaxCsvImportRows) {
        records.resize(MaxCsvImportRows);
        out.truncated = true;
    }
    for (QStringList &r : records) {
        while (r.size() < header.size())
            r.append(QString());
    }
    out.columns = header;
    out.rows = std::move(records);
    return out;
}

bool detectHeader(const QList<QStringList> &records)
{
    if (records.size() < 2)
        return true;
    const QStringList &first = records.first();
    int empty = 0;
    QSet<QString> seen;
    for (const QString &c : first) {
        const QString t = c.trimmed();
        if (t.isEmpty()) {
            ++empty;
            continue;
        }
        if (seen.contains(t.toCaseFolded()))
            return false; // repeated names: data
        seen.insert(t.toCaseFolded());
    }
    if (empty * 2 > first.size())
        return false;
    // Votes by column (as Python's csv.Sniffer): the header cell differs from a column of numbers,
    // dates or booleans below it.
    int votes = 0;
    const qsizetype rows = std::min<qsizetype>(records.size(), DetectRecords);
    for (qsizetype col = 0; col < first.size(); ++col) {
        int numbers = 0, dates = 0, bools = 0, filled = 0;
        QSet<qsizetype> lengths;
        for (qsizetype r = 1; r < rows; ++r) {
            const QString cell = records[r].value(col).trimmed();
            if (cell.isEmpty())
                continue;
            ++filled;
            numbers += isCsvNumber(cell, nullptr);
            dates += parseCsvDate(cell).has_value();
            bools += csvBoolean(cell).has_value();
            lengths.insert(cell.size());
        }
        if (filled == 0)
            continue;
        const QString head = first.value(col).trimmed();
        const auto typedVote = [&](int count, bool headIsType) {
            if (count * 10 >= filled * 8)
                votes += headIsType ? -1 : 1;
        };
        typedVote(numbers, isCsvNumber(head, nullptr));
        typedVote(dates, parseCsvDate(head).has_value());
        typedVote(bools, csvBoolean(head).has_value());
        if (lengths.size() == 1 && filled >= 2 && head.size() != *lengths.begin())
            ++votes;
    }
    return votes >= 0;
}

} // namespace detail

} // namespace brulion::io

// ---- Planning ------------------------------------------------------------------------------------

namespace brulion::io::detail {

namespace {

/// Rows looked at to infer a column's type.
constexpr int InferRows = 2000;

QString detectListSeparator(const QList<QStringList> &rows, int col, int sample)
{
    int comma = 0, semicolon = 0, pipe = 0, newline = 0;
    for (int r = 0; r < sample; ++r) {
        const QString &cell = rows[r][col];
        comma += cell.contains(u',');
        semicolon += cell.contains(u';');
        pipe += cell.contains(u'|');
        newline += cell.contains(u'\n');
    }
    const int best = std::max({comma, semicolon, pipe, newline});
    if (best == 0 || best == comma)
        return u", "_s;
    if (best == semicolon)
        return u";"_s;
    if (best == pipe)
        return u"|"_s;
    return u"\n"_s;
}

/// The inferred type of a column (without links: they need every file's titles).
void inferColumn(CsvColumnPlan &col, const CsvData &data, int c, CsvFlavor flavor)
{
    const int sample = int(std::min<qsizetype>(data.rows.size(), InferRows));
    int filled = 0, numbers = 0, dates = 0, withTime = 0, bools = 0, attachments = 0, multiline = 0;
    int dayFirstVotes = 0, monthFirstVotes = 0, slashDates = 0;
    qsizetype longest = 0, totalLength = 0;
    for (int r = 0; r < sample; ++r) {
        const QString cell = data.rows[r][c].trimmed();
        if (cell.isEmpty())
            continue;
        if (col.sample.isEmpty())
            col.sample = shortened(cell);
        ++filled;
        longest = std::max(longest, cell.size());
        totalLength += cell.size();
        multiline += cell.contains(u'\n');
        numbers += isCsvNumber(cell, nullptr);
        bools += csvBoolean(cell).has_value();
        if (!parseAttachmentCell(cell).isEmpty())
            ++attachments;
        if (const auto d = parseCsvDate(cell)) {
            ++dates;
            withTime += d->time.has_value();
            // "a/b/yyyy": which one is the day?
            static const QRegularExpression slash(u"^(\\d{1,2})/(\\d{1,2})/\\d{2,4}"_s);
            if (const auto m = slash.match(cell); m.hasMatch()) {
                ++slashDates;
                if (m.captured(1).toInt() > 12)
                    ++dayFirstVotes;
                else if (m.captured(2).toInt() > 12)
                    ++monthFirstVotes;
            }
        }
    }
    for (int r = sample; r < data.rows.size(); ++r)
        filled += !data.rows[r][c].trimmed().isEmpty();
    col.filled = filled;
    col.dayFirst = slashDates == 0 || dayFirstVotes > monthFirstVotes;
    const int checked = sample > 0 ? int(std::ranges::count_if(data.rows.first(sample),
                                                               [c](const QStringList &row) {
                                                                   return row[c].trimmed().compare(u"checked"_s,
                                                                                                   Qt::CaseInsensitive)
                                                                          == 0;
                                                               }))
                                   : 0;
    const QString folded = foldHeader(col.name);
    const int seen = int(std::min<qsizetype>(filled, sample));
    if (seen == 0) {
        col.inferred = CsvColumnType::Text;
    } else if (bools == seen && (flavor == CsvFlavor::Airtable ? checked == seen : true)) {
        col.inferred = CsvColumnType::Checkbox;
    } else if (flavor == CsvFlavor::Airtable && checked == seen) {
        col.inferred = CsvColumnType::Checkbox;
    } else if (numbers == seen) {
        col.inferred = CsvColumnType::Number;
    } else if (dates == seen) {
        col.inferred = withTime > 0 ? CsvColumnType::DateTime : CsvColumnType::Date;
    } else if (attachments * 10 >= seen * 8) {
        col.inferred = CsvColumnType::Attachments;
        col.attachments = true;
    } else if (isTagsHeader(folded)) {
        col.inferred = CsvColumnType::Tags;
        col.listSeparator = detectListSeparator(data.rows, c, sample);
    } else {
        col.inferred = CsvColumnType::Text;
        // A list (multiple select): short items repeating across rows, many cells with several.
        const QString sep = detectListSeparator(data.rows, c, sample);
        if (seen >= 3 && longest <= 300 && multiline * 2 < seen) {
            int withSeparator = 0, items = 0;
            QSet<QString> distinct;
            qsizetype itemLength = 0;
            for (int r = 0; r < sample; ++r) {
                const QString &cell = data.rows[r][c];
                if (cell.trimmed().isEmpty())
                    continue;
                const QStringList parts = splitList(cell, sep);
                withSeparator += parts.size() > 1;
                items += int(parts.size());
                for (const QString &p : parts) {
                    distinct.insert(p.toCaseFolded());
                    itemLength += p.size();
                }
            }
            if (items > 0 && withSeparator * 10 >= seen * 3 && itemLength / items <= 30
                && distinct.size() * 10 <= items * 7) {
                col.inferred = CsvColumnType::List;
                col.listSeparator = sep;
            }
        }
    }
    (void)totalLength;
}

/// The column naming the rows when none is chosen: by its header, else the first one with
/// (mostly) distinct, short, filled values.
int guessTitleColumn(const CsvData &data, const QList<CsvColumnPlan> &columns)
{
    for (int c = 0; c < columns.size(); ++c) {
        if (isTitleHeader(foldHeader(columns[c].name)) && columns[c].filled > 0)
            return c;
    }
    const int rows = int(std::min<qsizetype>(data.rows.size(), InferRows));
    for (int c = 0; c < columns.size(); ++c) {
        const CsvColumnType t = columns[c].inferred;
        if (t == CsvColumnType::Checkbox || t == CsvColumnType::Number || t == CsvColumnType::Attachments)
            continue;
        QSet<QString> distinct;
        int filled = 0;
        qsizetype longest = 0;
        for (int r = 0; r < rows; ++r) {
            const QString cell = data.rows[r][c].trimmed();
            if (cell.isEmpty())
                continue;
            ++filled;
            longest = std::max(longest, cell.size());
            distinct.insert(cell.toCaseFolded());
        }
        if (filled * 10 >= rows * 8 && distinct.size() * 10 >= filled * 8 && longest <= 200)
            return c;
    }
    return columns.isEmpty() ? -1 : 0;
}

int guessBodyColumn(const CsvData &data, const QList<CsvColumnPlan> &columns, int title)
{
    for (int c = 0; c < columns.size(); ++c) {
        if (c == title || columns[c].inferred != CsvColumnType::Text || !isBodyHeader(foldHeader(columns[c].name)))
            continue;
        qsizetype total = 0;
        int filled = 0;
        bool multiline = false;
        for (const QStringList &row : data.rows) {
            const QString &cell = row[c];
            if (cell.trimmed().isEmpty())
                continue;
            ++filled;
            total += cell.size();
            multiline = multiline || cell.contains(u'\n');
        }
        if (filled > 0 && (multiline || total / filled > 40))
            return c;
    }
    return -1;
}

int columnByName(const QList<CsvColumnPlan> &columns, const QString &name)
{
    const QString key = name.trimmed().toCaseFolded();
    for (int c = 0; c < columns.size(); ++c) {
        if (columns[c].name.toCaseFolded() == key)
            return c;
    }
    return -1;
}

QString rowTitle(const CsvFilePlan &plan, int row, const CsvImportOptions &options)
{
    if (!options.titleTemplate.trimmed().isEmpty())
        return renderRowTemplate(options.titleTemplate, plan, row).simplified();
    if (plan.titleColumn < 0)
        return {};
    return plan.data.rows[row][plan.titleColumn].simplified();
}

} // namespace

QString renderRowTemplate(const QString &templ, const CsvFilePlan &plan, int row)
{
    static const QRegularExpression placeholder(u"\\{\\{\\s*([^{}]+?)\\s*\\}\\}"_s);
    QString out;
    qsizetype pos = 0;
    for (auto it = placeholder.globalMatch(templ); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(templ).sliced(pos, m.capturedStart() - pos);
        pos = m.capturedEnd();
        QString name = m.captured(1);
        // Obsidian Importer's {{source["Column"]}}.
        static const QRegularExpression source(u"^source\\[\\s*[\"'](.*)[\"']\\s*\\]$"_s);
        if (const auto s = source.match(name); s.hasMatch())
            name = s.captured(1);
        if (name.compare(u"row"_s, Qt::CaseInsensitive) == 0) {
            out += QString::number(row + 1);
            continue;
        }
        if (const int c = columnByName(plan.columns, name); c >= 0)
            out += plan.data.rows[row][c].simplified();
    }
    out += QStringView(templ).sliced(pos);
    return out;
}

std::optional<CsvFilePlan> planData(CsvData data, const CsvImportOptions &options, CsvFlavor flavor,
                                    QStringList *warnings, const QHash<QString, CsvColumnPlan> &fixed)
{
    CsvFilePlan plan;
    plan.data = std::move(data);
    const QString &path = plan.data.path;
    if (plan.data.columns.isEmpty() || plan.data.rows.isEmpty()) {
        if (warnings)
            warnings->append(IoMessages::tr("“%1” holds no rows and was skipped.").arg(path));
        return std::nullopt;
    }
    if (plan.data.truncated && warnings)
        warnings->append(IoMessages::tr("“%1” is larger than Brulion imports (%2 rows, %3 columns, %4 characters a "
                                        "cell); the rest was left out.")
                                 .arg(path)
                                 .arg(MaxCsvImportRows)
                                 .arg(MaxCsvImportColumns)
                                 .arg(MaxCsvImportCellLength));
    QSet<QString> keys;
    for (int c = 0; c < plan.data.columns.size(); ++c) {
        CsvColumnPlan col;
        col.name = plan.data.columns[c];
        if (const auto it = fixed.constFind(col.name); it != fixed.constEnd()) {
            col = *it;
            col.name = plan.data.columns[c];
            for (const QStringList &row : std::as_const(plan.data.rows)) {
                if (row[c].trimmed().isEmpty())
                    continue;
                ++col.filled;
                if (col.sample.isEmpty())
                    col.sample = shortened(row[c]);
            }
        } else {
            inferColumn(col, plan.data, c, flavor);
        }
        const CsvColumnOptions chosen = options.columns.value(col.name);
        col.type = chosen.type == CsvColumnType::Auto ? col.inferred : chosen.type;
        QString property = propertyKey(chosen.property.trimmed().isEmpty() ? col.name : chosen.property);
        if (property.isEmpty())
            property = IoMessages::tr("Column %1").arg(c + 1);
        if (col.type == CsvColumnType::Tags) {
            property = u"tags"_s;
        } else if (property.compare(u"tags"_s, Qt::CaseInsensitive) == 0 && col.type == CsvColumnType::List) {
            col.type = CsvColumnType::Tags;
            property = u"tags"_s;
        }
        if (property.compare(u"brulion"_s, Qt::CaseInsensitive) == 0
            || property.compare(u"aliases"_s, Qt::CaseInsensitive) == 0
            || property.compare(u"source"_s, Qt::CaseInsensitive) == 0
            || (property.compare(u"tags"_s, Qt::CaseInsensitive) == 0 && col.type != CsvColumnType::Tags))
            property += u" (CSV)"_s;
        if (col.type != CsvColumnType::Tags) {
            QString unique = property;
            for (int n = 2; keys.contains(unique.toCaseFolded()); ++n)
                unique = u"%1 %2"_s.arg(property).arg(n);
            keys.insert(unique.toCaseFolded());
            property = unique;
        }
        col.property = property;
        if (!options.listSeparator.isEmpty() && !fixed.contains(col.name))
            col.listSeparator = options.listSeparator;
        plan.columns.append(col);
    }
    if (options.titleTemplate.trimmed().isEmpty()) {
        int title = -1;
        if (!options.titleColumn.trimmed().isEmpty()) {
            title = columnByName(plan.columns, options.titleColumn);
            if (title < 0 && warnings)
                warnings->append(
                        IoMessages::tr("“%1” has no column “%2”; Brulion chose the column that names the pages.")
                                .arg(path, options.titleColumn));
        }
        if (title < 0) {
            for (int c = 0; c < plan.columns.size() && title < 0; ++c) {
                if (fixed.value(plan.columns[c].name).primary)
                    title = c;
            }
        }
        plan.titleColumn = title >= 0 ? title : guessTitleColumn(plan.data, plan.columns);
    }
    if (options.bodyColumn == u"-")
        plan.bodyColumn = -1;
    else if (options.bodyColumn.trimmed().isEmpty())
        plan.bodyColumn = guessBodyColumn(plan.data, plan.columns, plan.titleColumn);
    else
        plan.bodyColumn = columnByName(plan.columns, options.bodyColumn);
    if (plan.bodyColumn == plan.titleColumn)
        plan.bodyColumn = -1;
    for (int r = 0; r < plan.data.rows.size(); ++r)
        plan.titles.append(rowTitle(plan, r, options));
    return plan;
}

void inferLinks(QList<CsvFilePlan> &plans, const CsvImportOptions &options)
{
    // Links: a text or list column whose items are (mostly) titles of rows — Airtable's linked
    // records, a "Related" column of a spreadsheet.
    QSet<QString> titles;
    for (const CsvFilePlan &p : std::as_const(plans)) {
        for (const QString &t : p.titles) {
            if (!t.isEmpty())
                titles.insert(t.toCaseFolded());
        }
    }
    if (titles.size() < 2)
        return;
    for (CsvFilePlan &p : plans) {
        for (int c = 0; c < p.columns.size(); ++c) {
            CsvColumnPlan &col = p.columns[c];
            if (c == p.titleColumn || c == p.bodyColumn
                || (col.inferred != CsvColumnType::Text && col.inferred != CsvColumnType::List))
                continue;
            const QString sep = col.inferred == CsvColumnType::List ? col.listSeparator : u", "_s;
            int items = 0, matched = 0;
            const int rows = int(std::min<qsizetype>(p.data.rows.size(), InferRows));
            for (int r = 0; r < rows; ++r) {
                if (p.data.rows[r][c].size() > 2000)
                    continue;
                const QStringList parts = splitList(p.data.rows[r][c], sep);
                items += int(parts.size());
                matched += int(std::ranges::count_if(
                        parts, [&titles](const QString &s) { return titles.contains(s.toCaseFolded()); }));
            }
            if (items > 0 && matched > 0 && matched * 10 >= items * 8) {
                col.inferred = CsvColumnType::Links;
                col.listSeparator = options.listSeparator.isEmpty() ? sep : options.listSeparator;
                if (options.columns.value(col.name).type == CsvColumnType::Auto)
                    col.type = CsvColumnType::Links;
            }
        }
    }
}

QList<CsvFilePlan> planCsv(const QList<InputFile> &files, const CsvImportOptions &options, CsvFlavor flavor,
                           QStringList *warnings)
{
    QList<CsvFilePlan> plans;
    for (const InputFile &f : files) {
        if (!looksLikeCsv(f) || f.path.startsWith(u"_attachments/"_s))
            continue;
        CsvData data = readCsv(f, options);
        if (flavor == CsvFlavor::Airtable)
            data.name = airtableTableName(data.name);
        if (auto plan = planData(std::move(data), options, flavor, warnings, {}))
            plans.append(std::move(*plan));
    }
    inferLinks(plans, options);
    return plans;
}

QString airtableTableName(const QString &fileStem)
{
    // Airtable names the download "<Table>-<View>.csv" ("Książki-Grid view.csv").
    static const QRegularExpression view(u"-(?:Grid view|Grid|Widok siatki)(?: \\d+)?$"_s,
                                         QRegularExpression::CaseInsensitiveOption);
    QString name = fileStem;
    name.remove(view);
    return name.trimmed().isEmpty() ? fileStem : name.trimmed();
}

// ---- A row's page ---------------------------------------------------------------------------------

namespace {

bool looksLikeHtml(const QString &text)
{
    static const QRegularExpression tags(u"<(?:p|br|div|ul|ol|li|b|i|strong|em|a|h[1-6]|span|table)\\b[^>]*>"_s,
                                         QRegularExpression::CaseInsensitiveOption);
    return tags.match(text).hasMatch();
}

QString wikiTitleLink(const QString &title, const CsvRowContext &context)
{
    if (title.startsWith(u"[[") && title.endsWith(u"]]"))
        return title;
    if (context.link)
        return context.link(title);
    return u"[["_s + QString(title).remove(u'[').remove(u']').remove(u'|') + u"]]"_s;
}

} // namespace

CsvRowPage csvRowPage(const CsvFilePlan &plan, int row, const CsvImportOptions &options, const CsvRowContext &context)
{
    CsvRowPage page;
    const QStringList &cells = plan.data.rows[row];
    QStringList tags;
    QStringList embeds;
    for (int c = 0; c < plan.columns.size(); ++c) {
        const CsvColumnPlan &col = plan.columns[c];
        if (c == plan.titleColumn && options.titleTemplate.trimmed().isEmpty())
            continue;
        if (c == plan.bodyColumn || col.type == CsvColumnType::Skip)
            continue;
        const QString &cell = cells[c];
        if (cell.trimmed().isEmpty() && col.type != CsvColumnType::Checkbox)
            continue;
        QVariant value;
        switch (col.type) {
        case CsvColumnType::Tags:
            for (const QString &item : splitList(cell, col.listSeparator)) {
                const QString tag = tagName(item);
                if (!tag.isEmpty() && !tags.contains(tag))
                    tags.append(tag);
            }
            continue;
        case CsvColumnType::Links: {
            QStringList links;
            for (const QString &item : splitList(cell, col.listSeparator))
                links.append(wikiTitleLink(item, context));
            value = links;
            break;
        }
        case CsvColumnType::Attachments: {
            QStringList values;
            const QList<CsvAttachmentRef> refs = parseAttachmentCell(cell);
            if (refs.isEmpty()) {
                for (const QString &item : splitList(cell, col.listSeparator))
                    values.append(item);
            }
            for (const CsvAttachmentRef &ref : refs) {
                const QString path = options.attachments && context.attachment ? context.attachment(ref) : QString();
                if (path.isEmpty()) {
                    values.append(ref.url);
                    continue;
                }
                values.append(wikiLink(path));
                embeds.append(attachmentLink(path));
            }
            value = values;
            break;
        }
        default:
            value = csvCellValue(cell, col.type, col.listSeparator, context.zone, col.dayFirst);
            if (!value.isValid()) {
                ++page.mismatched;
                value = QString(cell).trimmed();
            }
            break;
        }
        if (!value.isValid())
            continue;
        page.frontmatter.insert(col.property, value);
        page.order.append(col.property);
    }
    if (!tags.isEmpty()) {
        page.frontmatter.insert(u"tags"_s, tags);
        page.order.append(u"tags"_s);
    }
    QString body;
    if (plan.bodyColumn >= 0) {
        const QString text = cells[plan.bodyColumn];
        body = looksLikeHtml(text) ? htmlToMarkdown(text).markdown : text;
        body = normalizeNewlines(body);
    }
    if (!embeds.isEmpty()) {
        // Pictures and PDFs shown on the page (other files are linked from the property).
        QStringList shown;
        for (const QString &e : std::as_const(embeds)) {
            if (e.startsWith(u'!'))
                shown.append(e);
        }
        if (!shown.isEmpty())
            body = body.isEmpty() ? shown.join(u"\n\n"_s) : body + u"\n\n"_s + shown.join(u"\n\n"_s);
    }
    page.body = body;
    return page;
}

QString csvTablePage(const CsvFilePlan &plan, bool *truncated)
{
    calc::TableModel model;
    QList<int> shown;
    for (int c = 0; c < plan.columns.size(); ++c) {
        if (plan.columns[c].type == CsvColumnType::Skip)
            continue;
        shown.append(c);
        model.header.append(escapeMarkdownText(plan.columns[c].name, false));
        model.aligns.append(plan.columns[c].type == CsvColumnType::Number ? calc::TableAlign::Right
                                                                          : calc::TableAlign::None);
    }
    const qsizetype rows = std::min<qsizetype>(plan.data.rows.size(), MaxTableRows);
    if (truncated)
        *truncated = plan.data.rows.size() > MaxTableRows;
    for (qsizetype r = 0; r < rows; ++r) {
        QStringList out;
        for (const int c : std::as_const(shown)) {
            QString v = plan.data.rows[r][c].trimmed();
            v.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
            QStringList lines = v.split(u'\n');
            for (QString &l : lines)
                l = escapeMarkdownText(l.trimmed(), false);
            v = lines.join(u"<br>"_s);
            if (v.startsWith(u'='))
                v.prepend(u'\\'); // text, not a formula
            out.append(v);
        }
        model.rows.append(out);
    }
    return escapeTextFormulas(calc::serializeTable(model));
}

QString csvBaseFile(const QString &folder, const QString &viewName, const QStringList &properties)
{
    QVariantMap filters;
    filters.insert(u"and"_s, QVariantList{u"file.inFolder(\"%1\")"_s.arg(QString(folder).replace(u'"', u"\\\""_s)),
                                          u"file.ext == \"md\""_s});
    QVariantList order{u"file.name"_s};
    for (const QString &p : properties)
        order.append(p);
    QVariantMap view;
    view.insert(u"type"_s, u"table"_s);
    view.insert(u"name"_s, viewName);
    view.insert(u"order"_s, order);
    QVariantMap base;
    base.insert(u"filters"_s, filters);
    base.insert(u"views"_s, QVariantList{view});
    return markdown::stringifyYaml(base, {u"filters"_s, u"views"_s});
}

void importCsvPlans(const QList<CsvFilePlan> &plans, const CsvImportOptions &options, ImportContext &ctx,
                    const QList<InputFile> &files, const QString &sourceName)
{
    const bool several = plans.size() > 1;
    // Downloaded attachments by input path.
    QHash<QString, const InputFile *> inputs;
    for (const InputFile &f : files) {
        if (f.path.startsWith(u"_attachments/"_s))
            inputs.insert(f.path, &f);
    }
    QHash<QString, QString> attachmentByUrl; // url -> vault path
    const auto attachment = [&](const CsvAttachmentRef &ref) -> QString {
        if (const auto it = attachmentByUrl.constFind(ref.url); it != attachmentByUrl.constEnd())
            return *it;
        const InputFile *file = inputs.value(csvAttachmentInputPath(ref.url, ref.name));
        if (!file) {
            // The file name may have been sanitised by the archive reader: match by the folder.
            const QString prefix = paths::dirname(csvAttachmentInputPath(ref.url, ref.name)) + u'/';
            for (auto it = inputs.constBegin(); it != inputs.constEnd(); ++it) {
                if (it.key().startsWith(prefix)) {
                    file = it.value();
                    break;
                }
            }
        }
        if (!file)
            return {};
        const QString path = ctx.addAttachment(paths::basename(file->path), file->data);
        attachmentByUrl.insert(ref.url, path);
        return path;
    };
    if (options.mode == CsvImportOptions::Mode::Table) {
        for (const CsvFilePlan &plan : plans) {
            bool truncated = false;
            const QString table = csvTablePage(plan, &truncated);
            QVariantMap fm;
            if (!sourceName.isEmpty())
                fm.insert(u"source"_s, sourceName);
            addTitleAlias(fm, plan.data.name);
            ctx.addPage(ctx.claimPage(pageSegment(plan.data.name)),
                        noteWithFrontmatter(fm, {u"aliases"_s, u"source"_s}, table));
            if (truncated)
                ctx.warn(IoMessages::tr(
                                 "“%1” has more than %2 rows; the table page holds the first %2. Import it as one "
                                 "page per row to keep all of them.")
                                 .arg(plan.data.path)
                                 .arg(MaxTableRows));
        }
        return;
    }
    // Titles -> the page of their first row (links of other rows lead there).
    struct Planned {
        QString title;
        VaultPath path;
    };
    QList<QList<Planned>> planned;
    QHash<QString, VaultPath> pageByTitle;
    int untitled = 0, repeated = 0;
    for (const CsvFilePlan &plan : plans) {
        QList<Planned> rows;
        const QString folder = !plan.folder.isEmpty() ? plan.folder : several ? pageSegment(plan.data.name) : QString();
        QSet<QString> seen;
        for (int r = 0; r < plan.data.rows.size(); ++r) {
            QString title = plan.titles.value(r);
            if (title.isEmpty()) {
                title = u"%1 %2"_s.arg(plan.data.name).arg(r + 1);
                ++untitled;
            }
            QStringList segments;
            if (!folder.isEmpty())
                segments.append(folder);
            if (!options.folderTemplate.trimmed().isEmpty()) {
                for (const QString &s : renderRowTemplate(options.folderTemplate, plan, r).split(u'/')) {
                    if (!s.trimmed().isEmpty())
                        segments.append(pageSegment(s.simplified()));
                }
            }
            segments.append(pageSegment(title));
            const VaultPath path = ctx.claimPage(segments.join(u'/'));
            if (seen.contains(title.toCaseFolded()))
                ++repeated;
            seen.insert(title.toCaseFolded());
            if (!pageByTitle.contains(title.toCaseFolded()))
                pageByTitle.insert(title.toCaseFolded(), path);
            rows.append(Planned{title, path});
        }
        planned.append(rows);
    }
    CsvRowContext context;
    context.zone = ctx.options().timeZone;
    context.attachment = attachment;
    context.link = [&pageByTitle](const QString &title) -> QString {
        const QString clean = QString(title).remove(u'[').remove(u']').remove(u'|');
        const auto it = pageByTitle.constFind(title.toCaseFolded());
        if (it == pageByTitle.constEnd())
            return u"[["_s + clean + u"]]"_s;
        const QString stem = paths::pageTitle(*it);
        if (stem == clean)
            return u"[["_s + stem + u"]]"_s;
        return u"[["_s + stem + u'|' + clean + u"]]"_s;
    };
    int mismatched = 0;
    for (qsizetype p = 0; p < plans.size(); ++p) {
        const CsvFilePlan &plan = plans[p];
        QStringList properties;
        for (int r = 0; r < plan.data.rows.size(); ++r) {
            CsvRowPage page = csvRowPage(plan, r, options, context);
            mismatched += page.mismatched;
            for (const QString &k : std::as_const(page.order)) {
                if (!properties.contains(k))
                    properties.append(k);
            }
            const Planned &pl = planned[p][r];
            addTitleAlias(page.frontmatter, pl.title);
            if (!sourceName.isEmpty())
                page.frontmatter.insert(u"source"_s, sourceName);
            QStringList order = page.order;
            order.prepend(u"aliases"_s);
            if (!sourceName.isEmpty() && !order.contains(u"source"_s))
                order.append(u"source"_s);
            ctx.addPage(pl.path, noteWithFrontmatter(page.frontmatter, order, page.body));
        }
        // A base next to the pages (not for pages at the notebook's top: it would show every note).
        const QString relativeFolder = !plan.folder.isEmpty() ? plan.folder
                                       : several              ? pageSegment(plan.data.name)
                                                              : QString();
        const QString pagesFolder = paths::join(ctx.options().targetFolder, relativeFolder);
        if (options.base && !plan.data.rows.isEmpty() && !pagesFolder.isEmpty()) {
            const VaultPath basePath =
                    ctx.claimFile(paths::join(relativeFolder, pageSegment(plan.data.name) + u".base"_s));
            ctx.addFile(basePath, csvBaseFile(pagesFolder, plan.data.name, properties).toUtf8());
        }
    }
    if (untitled > 0)
        ctx.warn(IoMessages::tr("%n row(s) had no title; their pages are named after the file and the row number.",
                                nullptr, untitled));
    if (repeated > 0)
        ctx.warn(IoMessages::tr("%n row(s) have the same title as an earlier row; their pages got a number (“… 2”).",
                                nullptr, repeated));
    if (mismatched > 0)
        ctx.warn(IoMessages::tr("%n value(s) did not fit their column's type and were kept as text.", nullptr,
                                mismatched));
}

} // namespace brulion::io::detail

// ---- The analysis ------------------------------------------------------------------------------

namespace brulion::io {

CsvAnalysis analyzeCsv(const QList<InputFile> &files, const CsvImportOptions &options, CsvFlavor flavor,
                       const QTimeZone &zone)
{
    using namespace detail;
    CsvAnalysis out;
    const QList<CsvFilePlan> plans = planCsv(files, options, flavor, &out.warnings);
    // The sample page of each file, with the links of the whole input.
    QHash<QString, QString> titles;
    for (const CsvFilePlan &p : plans) {
        for (const QString &t : p.titles) {
            if (!t.isEmpty() && !titles.contains(t.toCaseFolded()))
                titles.insert(t.toCaseFolded(), t);
        }
    }
    for (const CsvFilePlan &plan : plans) {
        CsvFileAnalysis a;
        a.path = plan.data.path;
        a.name = plan.data.name;
        a.encoding = text::encodingName(plan.data.encoding);
        a.encodingDetected = plan.data.encodingDetected;
        a.separator = plan.data.separator;
        a.header = plan.data.header;
        a.rowCount = int(plan.data.rows.size());
        a.truncated = plan.data.truncated;
        a.titleColumn = plan.titleColumn >= 0 && options.titleTemplate.trimmed().isEmpty()
                                ? plan.columns[plan.titleColumn].name
                                : QString();
        a.bodyColumn = plan.bodyColumn >= 0 ? plan.columns[plan.bodyColumn].name : QString();
        for (const CsvColumnPlan &c : plan.columns) {
            CsvColumnInfo info;
            info.name = c.name;
            info.property = c.property;
            info.inferred = c.inferred;
            info.type = c.type;
            info.listSeparator = c.listSeparator;
            info.attachments = c.attachments;
            info.filled = c.filled;
            info.sample = c.sample;
            a.columns.append(info);
        }
        for (int r = 0; r < std::min<qsizetype>(plan.data.rows.size(), CsvPreviewRows); ++r) {
            a.rows.append(plan.data.rows[r]);
            const QString t = plan.titles.value(r);
            a.titles.append(t.isEmpty() ? u"%1 %2"_s.arg(plan.data.name).arg(r + 1) : t);
        }
        QSet<QString> urls;
        if (options.mode == CsvImportOptions::Mode::Pages && options.attachments) {
            for (int c = 0; c < plan.columns.size(); ++c) {
                if (plan.columns[c].type != CsvColumnType::Attachments)
                    continue;
                for (const QStringList &row : plan.data.rows) {
                    for (const CsvAttachmentRef &ref : parseAttachmentCell(row[c])) {
                        if (!urls.contains(ref.url)) {
                            urls.insert(ref.url);
                            a.attachmentUrls.append(ref.url);
                        }
                    }
                }
            }
        }
        if (options.mode == CsvImportOptions::Mode::Table) {
            bool truncated = false;
            a.samplePage = csvTablePage(plan, &truncated);
        } else if (!plan.data.rows.isEmpty()) {
            CsvRowContext context;
            context.zone = zone;
            context.link = [&titles](const QString &title) {
                return u"[["_s + titles.value(title.toCaseFolded(), title) + u"]]"_s;
            };
            const CsvRowPage page = csvRowPage(plan, 0, options, context);
            QVariantMap fm = page.frontmatter;
            addTitleAlias(fm, a.titles.value(0));
            QStringList order = page.order;
            order.prepend(u"aliases"_s);
            a.samplePage = noteWithFrontmatter(fm, order, page.body);
        }
        if (a.samplePage.size() > MaxSamplePage)
            a.samplePage = a.samplePage.left(MaxSamplePage) + u"\n…"_s;
        out.files.append(a);
    }
    return out;
}

} // namespace brulion::io
