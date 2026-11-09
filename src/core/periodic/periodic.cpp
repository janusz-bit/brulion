#include "periodic/periodic.h"

#include "dates/local.h"
#include "dates/natural.h"
#include "text/text.h"
#include "util/paths.h"

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::periodic {

namespace {

// ---- Weekly file name formats ------------------------------------------------------------------

struct WeekToken {
    enum Kind { Literal, Year4, Year2, Week2, Week1 } kind = Literal;
    QString text; ///< Literal only
};

/// The tokens of a weekly format, nullopt when it has a letter outside quotes that is no token, an
/// unclosed quote or no year or week.
std::optional<QList<WeekToken>> weekTokens(const QString &format)
{
    QList<WeekToken> out;
    const auto literal = [&](QChar c) {
        if (out.isEmpty() || out.last().kind != WeekToken::Literal)
            out.append(WeekToken{});
        out.last().text += c;
    };
    bool year = false;
    bool week = false;
    bool quoted = false;
    for (qsizetype i = 0; i < format.size(); ++i) {
        const QChar c = format.at(i);
        if (c == u'\'' && i + 1 < format.size() && format.at(i + 1) == u'\'') { // '': a quote, quoted or not
            literal(c);
            ++i;
        } else if (c == u'\'') {
            quoted = !quoted;
        } else if (quoted || !c.isLetter()) {
            literal(c);
        } else if (c == u'y' || c == u'w') {
            qsizetype n = 1;
            while (i + n < format.size() && format.at(i + n) == c)
                ++n;
            if (c == u'y' && (n == 4 || n == 2)) {
                out.append(WeekToken{n == 4 ? WeekToken::Year4 : WeekToken::Year2, {}});
                year = true;
            } else if (c == u'w' && (n == 2 || n == 1)) {
                out.append(WeekToken{n == 2 ? WeekToken::Week2 : WeekToken::Week1, {}});
                week = true;
            } else {
                return std::nullopt;
            }
            i += n - 1;
        } else {
            return std::nullopt; // a letter that is no token
        }
    }
    if (quoted || !year || !week) // an unclosed quote, no year or no week
        return std::nullopt;
    return out;
}

QString formatWeek(const QList<WeekToken> &tokens, IsoWeek w)
{
    QString out;
    for (const WeekToken &t : tokens) {
        switch (t.kind) {
        case WeekToken::Literal: out += t.text; break;
        case WeekToken::Year4: out += u"%1"_s.arg(w.year, 4, 10, QChar(u'0')); break;
        case WeekToken::Year2: out += u"%1"_s.arg(w.year % 100, 2, 10, QChar(u'0')); break;
        case WeekToken::Week2: out += u"%1"_s.arg(w.week, 2, 10, QChar(u'0')); break;
        case WeekToken::Week1: out += QString::number(w.week); break;
        }
    }
    return out;
}

std::optional<QDate> parseWeekName(const QList<WeekToken> &tokens, const QString &name)
{
    QString pattern = u"^"_s;
    for (const WeekToken &t : tokens) {
        switch (t.kind) {
        case WeekToken::Literal: pattern += QRegularExpression::escape(t.text); break;
        case WeekToken::Year4: pattern += u"(\\d{4})"_s; break;
        case WeekToken::Year2: pattern += u"(\\d{2})"_s; break;
        case WeekToken::Week2: pattern += u"(\\d{2})"_s; break;
        case WeekToken::Week1: pattern += u"(\\d{1,2})"_s; break;
        }
    }
    pattern += u"$"_s;
    // Compiled once per format and thread (a calendar asks for every note of a folder).
    thread_local QHash<QString, QRegularExpression> compiled;
    auto it = compiled.find(pattern);
    if (it == compiled.end()) {
        if (compiled.size() > 16)
            compiled.clear();
        it = compiled.insert(pattern, QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption));
    }
    const QRegularExpressionMatch m = it->match(name);
    if (!m.hasMatch())
        return std::nullopt;
    int year4 = -1;
    int year2 = -1;
    int week = -1;
    int group = 1;
    for (const WeekToken &t : tokens) {
        if (t.kind == WeekToken::Literal)
            continue;
        int &field = t.kind == WeekToken::Year4 ? year4 : t.kind == WeekToken::Year2 ? year2 : week;
        field = m.captured(group++).toInt();
    }
    // Parts that disagree ("2026-W45 (2027)", "2026-W45-27") do not format back to the name.
    const QDate start = isoWeekStart(year4 >= 0 ? year4 : 2000 + year2, week);
    if (!start.isValid() || formatWeek(tokens, isoWeek(start)).compare(name, Qt::CaseInsensitive) != 0)
        return std::nullopt;
    return start;
}

// ---- Monthly file name formats (Qt QDate formats) ----------------------------------------------

/// Split a format on '/' outside 'quoted' literals (as markdown/journal.cpp).
QStringList splitFormatPath(const QString &format)
{
    QStringList parts;
    bool quoted = false;
    qsizetype start = 0;
    for (qsizetype i = 0; i < format.size(); ++i) {
        if (format[i] == u'\'')
            quoted = !quoted;
        else if (format[i] == u'/' && !quoted) {
            parts.append(format.sliced(start, i - start));
            start = i + 1;
        }
    }
    parts.append(format.sliced(start));
    return parts;
}

/// `text` is what `format` writes for `d`: letter case aside, and a month's name also in its own
/// (standalone) form — Polish "listopad 2026" for the "listopada 2026" Qt writes.
bool formatsBackTo(QDate d, const QString &format, const QString &text, const QLocale &locale)
{
    const QString written = locale.toString(d, format);
    if (written.compare(text, Qt::CaseInsensitive) == 0)
        return true;
    for (const QLocale::FormatType type : {QLocale::LongFormat, QLocale::ShortFormat}) {
        const QString name = locale.monthName(d.month(), type);
        const QString standalone = locale.standaloneMonthName(d.month(), type);
        if (name != standalone && written.contains(name, Qt::CaseInsensitive)
            && QString(written).replace(name, standalone, Qt::CaseInsensitive).compare(text, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

using MonthWords = QList<std::pair<QRegularExpression, QString>>;

/// `locale`'s standalone month names (Polish "listopad") that differ from the ones Qt reads
/// ("listopada"), each as a whole word with the name Qt reads, by month. Compiled once per locale
/// and thread.
const MonthWords &standaloneMonthWords(const QLocale &locale)
{
    thread_local QHash<QLocale, MonthWords> cache;
    auto it = cache.find(locale);
    if (it != cache.end())
        return *it;
    it = cache.insert(locale, {});
    for (int m = 1; m <= 12; ++m) {
        for (const QLocale::FormatType type : {QLocale::LongFormat, QLocale::ShortFormat}) {
            const QString standalone = locale.standaloneMonthName(m, type);
            const QString name = locale.monthName(m, type);
            if (standalone == name || standalone.isEmpty())
                continue;
            it->append({QRegularExpression(u"(?<!\\w)"_s + QRegularExpression::escape(standalone) + u"(?!\\w)"_s,
                                           QRegularExpression::CaseInsensitiveOption),
                        name});
        }
    }
    return *it;
}

/// `text` with a month's standalone name (Polish "listopad") as the name Qt reads ("listopada").
QString withFormatMonthNames(const QString &text, const QLocale &locale)
{
    for (const auto &[word, name] : standaloneMonthWords(locale)) {
        if (word.match(text).hasMatch())
            return QString(text).replace(word, name);
    }
    return text;
}

QDate tryParseMonth(const QString &text, const QString &format, const QLocale &locale)
{
    QDate d = locale.toDate(text, format, 2000);
    if (!d.isValid())
        d = locale.toDate(withFormatMonthNames(text, locale), format, 2000);
    if (!d.isValid() || d.day() != 1)
        return {};
    return formatsBackTo(d, format, text, locale) ? d : QDate();
}

QDate parseMonthStrict(const QString &text, const QString &format, const QLocale &locale)
{
    if (const QDate d = tryParseMonth(text, format, locale); d.isValid())
        return d;
    const QStringList formatParts = splitFormatPath(format);
    const QStringList textParts = text.split(u'/');
    if (formatParts.size() < 2 || formatParts.size() != textParts.size())
        return {};
    for (qsizetype k = formatParts.size() - 1; k >= 0; --k) {
        QDate candidate = locale.toDate(textParts[k], formatParts[k], 2000);
        if (!candidate.isValid())
            continue;
        candidate = QDate(candidate.year(), candidate.month(), 1);
        if (formatsBackTo(candidate, format, text, locale))
            return candidate;
    }
    return {};
}

bool containsDigit(QStringView s)
{
    return std::ranges::any_of(s, [](QChar c) { return c.isDigit(); });
}

bool containsLetter(QStringView s)
{
    return std::ranges::any_of(s, [](QChar c) { return c.isLetter(); });
}

/// A name may not leave its folder or hide itself.
bool safeRelativeName(const QString &name)
{
    if (name.isEmpty() || name.startsWith(u'/') || name.endsWith(u'/') || name.contains(u'\\'))
        return false;
    for (const QStringView part : name.tokenize(u'/')) {
        if (part.trimmed().isEmpty() || part.startsWith(u'.')) // ".." too
            return false;
    }
    return true;
}

QString relativeInFolder(const VaultPath &path, const QString &folder)
{
    const VaultPath p = paths::normalize(path);
    if (!paths::isMarkdown(p))
        return {};
    QString rel = p;
    if (!folder.isEmpty()) {
        if (!p.startsWith(folder + u'/', Qt::CaseInsensitive))
            return {};
        rel = p.sliced(folder.size() + 1);
    }
    return paths::stripMarkdownExtension(rel);
}

// ---- Phrases ----------------------------------------------------------------------------------

/// "ten tydzień", "w przyszłym miesiącu", "next month", … (folded text): how many periods from the
/// current one, nullopt when the phrase is none.
std::optional<int> relativeOffset(Period period, const QString &folded)
{
    static const QStringList weekWords{u"tydzien"_s,  u"tydz"_s,      u"tydz."_s, u"tygodnia"_s,
                                       u"tygodniu"_s, u"tygodniem"_s, u"week"_s};
    static const QStringList monthWords{u"miesiac"_s,  u"mies"_s,      u"mies."_s, u"miesiaca"_s,
                                        u"miesiacu"_s, u"miesiacem"_s, u"month"_s};
    static const QStringList thisWords{u"ten"_s,      u"tym"_s,       u"tego"_s,   u"biezacy"_s,
                                       u"biezacym"_s, u"biezacego"_s, u"obecny"_s, u"obecnym"_s,
                                       u"aktualny"_s, u"this"_s,      u"current"_s};
    static const QStringList nextWords{u"przyszly"_s,   u"przyszlym"_s, u"przyszlego"_s, u"nastepny"_s, u"nastepnym"_s,
                                       u"nastepnego"_s, u"kolejny"_s,   u"kolejnym"_s,   u"next"_s,     u"following"_s};
    static const QStringList lastWords{u"zeszly"_s,       u"zeszlym"_s, u"zeszlego"_s, u"poprzedni"_s, u"poprzednim"_s,
                                       u"poprzedniego"_s, u"ubiegly"_s, u"ubieglym"_s, u"ubieglego"_s, u"miniony"_s,
                                       u"minionym"_s,     u"last"_s,    u"previous"_s, u"past"_s};
    const QStringList &nouns = period == Period::Week ? weekWords : monthWords;
    QStringList words = folded.split(u' ', Qt::SkipEmptyParts);
    if (words.size() > 1 && (words.first() == u"w"_s || words.first() == u"the"_s))
        words.removeFirst(); // "w przyszłym tygodniu", "the next week"
    if (words.isEmpty())
        return 0;
    if (words.size() == 1)
        return nouns.contains(words.first()) ? std::optional<int>(0) : std::nullopt;
    if (words.size() != 2 || !nouns.contains(words.last()))
        return std::nullopt;
    const QString &w = words.first();
    if (thisWords.contains(w))
        return 0;
    if (nextWords.contains(w))
        return 1;
    if (lastWords.contains(w))
        return -1;
    return std::nullopt;
}

/// The month a (folded) word names: Polish or English, full, genitive or short; 0 for none.
int monthOfWord(const QString &word)
{
    for (int m = 1; m <= 12; ++m) {
        for (const dates::UiLocale locale : {dates::UiLocale::Polish, dates::UiLocale::English}) {
            for (const dates::NameForm form :
                 {dates::NameForm::Nominative, dates::NameForm::Genitive, dates::NameForm::Short}) {
                QString name = text::foldKey(dates::monthName(m, locale, form));
                if (name.endsWith(u'.'))
                    name.chop(1);
                if (!name.isEmpty() && name == word)
                    return m;
            }
        }
    }
    return 0;
}

std::optional<QDate> explicitWeek(const QString &folded, QDate today)
{
    // "2026-w45", "2026w45", "2026 w45"
    static const QRegularExpression isoWeek(u"^(\\d{4})[- ]?w(\\d{1,2})$"_s);
    // "w45", "w45 2026", "tydzien 45", "tydz. 45 2026", "week 45, 2026"
    static const QRegularExpression named(u"^(?:w|tydz\\.?|tydzien|week|wk)\\s*(\\d{1,2})(?:[ ,/]+(\\d{4}))?$"_s);
    QRegularExpressionMatch m = isoWeek.match(folded);
    int year = 0;
    int week = 0;
    if (m.hasMatch()) {
        year = m.captured(1).toInt();
        week = m.captured(2).toInt();
    } else if (m = named.match(folded); m.hasMatch()) {
        week = m.captured(1).toInt();
        year = m.captured(2).isEmpty() ? periodic::isoWeek(today).year : m.captured(2).toInt();
    } else {
        return std::nullopt;
    }
    const QDate start = isoWeekStart(year, week);
    return start.isValid() ? std::optional<QDate>(start) : std::nullopt;
}

std::optional<QDate> explicitMonth(const QString &folded, QDate today)
{
    // "2026-11", "2026/11", "11.2026", "11/2026", "11-2026"
    static const QRegularExpression yearFirst(u"^(\\d{4})[-/.](\\d{1,2})$"_s);
    static const QRegularExpression monthFirst(u"^(\\d{1,2})[-/.](\\d{4})$"_s);
    // "listopad", "listopad 2026", "nov 2026", "november, 2026"
    static const QRegularExpression named(u"^([a-z]+)\\.?(?:[ ,]+(\\d{4}))?$"_s);
    int year = 0;
    int month = 0;
    if (const auto m = yearFirst.match(folded); m.hasMatch()) {
        year = m.captured(1).toInt();
        month = m.captured(2).toInt();
    } else if (const auto n = monthFirst.match(folded); n.hasMatch()) {
        month = n.captured(1).toInt();
        year = n.captured(2).toInt();
    } else if (const auto w = named.match(folded); w.hasMatch()) {
        month = monthOfWord(w.captured(1));
        year = w.captured(2).isEmpty() ? today.year() : w.captured(2).toInt();
    } else {
        return std::nullopt;
    }
    if (month < 1 || month > 12 || year < 1 || year > 9999)
        return std::nullopt;
    return QDate(year, month, 1);
}

/// `format` (trimmed) when usable, else the period's default.
QString usableFormat(Period period, const QString &format)
{
    return isUsableFormat(period, format) ? format.trimmed() : defaultFileFormat(period);
}

/// The vault's file name format for a period as set: nameFor() and dateFromName() check it as
/// fileFormat() does, once (a folder's every note may be asked about).
const QString &formatSetting(Period period, const VaultConfig &config)
{
    return period == Period::Week ? config.weeklyFileFormat : config.monthlyFileFormat;
}

} // namespace

QString periodName(Period period)
{
    return period == Period::Week ? u"week"_s : u"month"_s;
}

std::optional<Period> parsePeriodName(QStringView name)
{
    static const QStringList weeks{u"week"_s,       u"weekly"_s,     u"weeks"_s,      u"tydzien"_s,
                                   u"tygodniowa"_s, u"tygodniowy"_s, u"tygodniowe"_s, u"w"_s};
    static const QStringList months{u"month"_s,      u"monthly"_s,    u"months"_s,     u"miesiac"_s,
                                    u"miesieczna"_s, u"miesieczny"_s, u"miesieczne"_s, u"m"_s};
    const QString key = text::foldKey(name.trimmed());
    if (weeks.contains(key))
        return Period::Week;
    if (months.contains(key))
        return Period::Month;
    return std::nullopt;
}

IsoWeek isoWeek(QDate date)
{
    if (!date.isValid())
        return {};
    int year = 0;
    const int week = date.weekNumber(&year);
    return {year, week};
}

int isoWeeksInYear(int year)
{
    if (year < 1 || year > 9999)
        return 0;
    // 28 December is always in the year's last week.
    return QDate(year, 12, 28).weekNumber();
}

QDate isoWeekStart(int year, int week)
{
    if (week < 1 || week > isoWeeksInYear(year))
        return {};
    // 4 January is always in week 1.
    const QDate jan4(year, 1, 4);
    return jan4.addDays(1 - jan4.dayOfWeek() + 7 * (week - 1));
}

QDate periodStart(Period period, QDate date)
{
    if (!date.isValid())
        return {};
    return period == Period::Week ? dates::startOfWeek(date, Qt::Monday) : dates::startOfMonth(date);
}

QDate periodEnd(Period period, QDate date)
{
    if (!date.isValid())
        return {};
    return period == Period::Week ? dates::endOfWeek(date, Qt::Monday) : dates::endOfMonth(date);
}

QDate shiftPeriod(Period period, QDate date, int count)
{
    const QDate start = periodStart(period, date);
    if (!start.isValid())
        return {};
    const QDate out = period == Period::Week ? start.addDays(7LL * count) : start.addMonths(count);
    return out.isValid() && out.year() >= 1 && out.year() <= 9999 ? out : QDate();
}

QString defaultFileFormat(Period period)
{
    return period == Period::Week ? VaultConfig().weeklyFileFormat : VaultConfig().monthlyFileFormat;
}

bool isUsableFormat(Period period, const QString &format)
{
    const QString f = format.trimmed();
    if (f.isEmpty())
        return false;
    if (period == Period::Week) {
        const auto tokens = weekTokens(f);
        if (!tokens)
            return false;
        // Every week of a year apart and readable back, across years too.
        for (const QDate d : {QDate(2026, 12, 28), QDate(2027, 1, 4), QDate(2020, 12, 31), QDate(2026, 3, 2)}) {
            const QString name = formatWeek(*tokens, isoWeek(d));
            if (!safeRelativeName(name) || parseWeekName(*tokens, name) != periodStart(Period::Week, d))
                return false;
        }
        return formatWeek(*tokens, isoWeek(QDate(2026, 1, 5))) != formatWeek(*tokens, isoWeek(QDate(2027, 1, 4)));
    }
    // A month's name: the same for each of its days, another for the next month and year.
    const QLocale c = QLocale::c();
    const QDate base(2026, 1, 1);
    const QString name = c.toString(base, f);
    if (name != c.toString(QDate(2026, 1, 15), f) || name != c.toString(QDate(2026, 1, 31), f)
        || name == c.toString(base.addMonths(1), f) || name == c.toString(base.addYears(1), f)
        || !safeRelativeName(name))
        return false;
    const auto readsBack = [&](QDate d) { return parseMonthStrict(c.toString(d, f), f, c) == d; };
    return readsBack(base) && readsBack(QDate(2026, 11, 1)) && readsBack(QDate(1999, 12, 1));
}

QString fileFormat(Period period, const VaultConfig &config)
{
    return usableFormat(period, formatSetting(period, config));
}

QString folder(Period period, const VaultConfig &config)
{
    return paths::normalize(period == Period::Week ? config.weeklyFolder : config.monthlyFolder);
}

QString templatePath(Period period, const VaultConfig &config)
{
    return paths::normalize(period == Period::Week ? config.weeklyTemplate : config.monthlyTemplate);
}

QString nameFor(Period period, QDate date, const QString &format, const QLocale &locale)
{
    const QDate start = periodStart(period, date);
    if (!start.isValid() || start.year() < 1 || start.year() > 9999)
        return {};
    const QString f = usableFormat(period, format);
    if (period == Period::Week) {
        const auto tokens = weekTokens(f);
        const IsoWeek w = isoWeek(start);
        if (!tokens || w.year < 1 || w.year > 9999)
            return {};
        return formatWeek(*tokens, w);
    }
    const QString name = locale.toString(start, f);
    return parseMonthStrict(name, f, locale) == start ? name : QLocale::c().toString(start, defaultFileFormat(period));
}

std::optional<QDate> dateFromName(Period period, const QString &name, const QString &format)
{
    if (name.isEmpty() || (period == Period::Week && !containsDigit(name)))
        return std::nullopt;
    const QString f = usableFormat(period, format);
    if (period == Period::Week) {
        const auto tokens = weekTokens(f);
        return tokens ? parseWeekName(*tokens, name) : std::nullopt;
    }
    const auto parse = [&](const QLocale &locale) -> std::optional<QDate> {
        const QDate d = parseMonthStrict(name, f, locale);
        return d.isValid() && d.year() >= 1 && d.year() <= 9999 ? std::optional(d) : std::nullopt;
    };
    // English month names; Polish ones too when the name has letters.
    if (const auto d = parse(QLocale::c()); d || !containsLetter(name))
        return d;
    return parse(QLocale(QLocale::Polish, QLocale::Poland));
}

VaultPath pathFor(Period period, QDate date, const VaultConfig &config)
{
    const QString name = nameFor(period, date, formatSetting(period, config));
    if (name.isEmpty())
        return {};
    return paths::join(folder(period, config), name + u".md"_s);
}

std::optional<QDate> dateFromPath(Period period, const VaultPath &path, const VaultConfig &config)
{
    const QString rel = relativeInFolder(path, folder(period, config));
    if (rel.isEmpty())
        return std::nullopt;
    return dateFromName(period, rel, formatSetting(period, config));
}

std::optional<PeriodicNote> periodicNoteFromPath(const VaultPath &path, const VaultConfig &config)
{
    for (const Period period : {Period::Week, Period::Month}) {
        if (const auto d = dateFromPath(period, path, config))
            return PeriodicNote{period, *d};
    }
    return std::nullopt;
}

QString periodLabel(Period period, QDate date, dates::UiLocale locale)
{
    const QDate start = periodStart(period, date);
    if (!start.isValid())
        return {};
    const bool pl = locale == dates::UiLocale::Polish;
    if (period == Period::Month) {
        QString month = dates::monthName(start.month(), locale, dates::NameForm::Nominative);
        if (!month.isEmpty())
            month[0] = month[0].toUpper();
        return month + u' ' + QString::number(start.year());
    }
    const QDate end = periodEnd(period, date);
    const auto month = [&](QDate d) {
        return dates::monthName(d.month(), locale, pl ? dates::NameForm::Genitive : dates::NameForm::Short);
    };
    const auto full = [&](QDate d) { // "2 listopada 2026" / "Nov 2, 2026"
        return pl ? u"%1 %2 %3"_s.arg(d.day()).arg(month(d)).arg(d.year())
                  : u"%1 %2, %3"_s.arg(month(d)).arg(d.day()).arg(d.year());
    };
    QString range;
    if (start.year() != end.year()) {
        range = full(start) + u" – "_s + full(end);
    } else if (start.month() != end.month()) {
        range = pl ? u"%1 %2 – %3"_s.arg(start.day()).arg(month(start), full(end))
                   : u"%1 %2 – %3"_s.arg(month(start)).arg(start.day()).arg(full(end));
    } else {
        range = pl ? u"%1–%2"_s.arg(start.day()).arg(full(end))
                   : u"%1 %2–%3, %4"_s.arg(month(start)).arg(start.day()).arg(end.day()).arg(end.year());
    }
    return (pl ? u"Tydzień %1 (%2)"_s : u"Week %1 (%2)"_s).arg(isoWeek(start).week).arg(range);
}

std::optional<QDate> parsePeriodPhrase(Period period, QStringView text, const QDateTime &now, Qt::DayOfWeek weekStart,
                                       dates::UiLocale locale, const VaultConfig *config)
{
    const QDate today = now.date();
    if (!today.isValid())
        return std::nullopt;
    const QString trimmed = text.trimmed().toString();
    static const QRegularExpression spaces(u"\\s+"_s);
    QString folded = text::foldKey(trimmed);
    folded.replace(spaces, u" "_s);
    if (const auto offset = relativeOffset(period, folded))
        return shiftPeriod(period, today, *offset);
    if (const auto d = period == Period::Week ? explicitWeek(folded, today) : explicitMonth(folded, today))
        return d;
    if (config) {
        if (const auto d = dateFromName(period, trimmed, formatSetting(period, *config)))
            return d;
    }
    if (const auto iso = dates::parseIsoDate(trimmed))
        return periodStart(period, *iso);
    const auto found = dates::parseNaturalDate(trimmed, now, weekStart, locale);
    if (found && found->start == 0 && found->length >= trimmed.size()) {
        const QDate start = periodStart(period, found->spec.date);
        if (start.isValid())
            return start;
    }
    return std::nullopt;
}

} // namespace brulion::periodic
