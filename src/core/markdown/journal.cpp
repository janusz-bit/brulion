#include "markdown/journal.h"

#include "util/paths.h"

#include <QStringList>

namespace brulion::markdown {

using namespace Qt::StringLiterals;

namespace {

const QString DefaultFormat = u"yyyy-MM-dd"_s;

QString journalFormat(const VaultConfig &config)
{
    return config.journalFileFormat.isEmpty() ? DefaultFormat : config.journalFileFormat;
}

/// Split a format on '/' outside 'quoted' literals.
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

QDate tryParse(const QString &text, const QString &format, const QLocale &locale)
{
    const QDate d = locale.toDate(text, format, 2000);
    return d.isValid() && locale.toString(d, format) == text ? d : QDate();
}

/// Parse `text` with `format` strictly: the date must format back to exactly `text`. Nested
/// formats are also tried path part by path part (last first).
QDate parseStrict(const QString &text, const QString &format, const QLocale &locale)
{
    if (const QDate d = tryParse(text, format, locale); d.isValid())
        return d;
    const QStringList formatParts = splitFormatPath(format);
    const QStringList textParts = text.split(u'/');
    if (formatParts.size() < 2 || formatParts.size() != textParts.size())
        return {};
    for (qsizetype k = formatParts.size() - 1; k >= 0; --k) {
        const QDate candidate = locale.toDate(textParts[k], formatParts[k], 2000);
        if (candidate.isValid() && locale.toString(candidate, format) == text)
            return candidate;
    }
    return {};
}

bool containsDigit(const QString &s)
{
    for (const QChar c : s) {
        if (c.isDigit())
            return true;
    }
    return false;
}

bool containsLetter(const QString &s)
{
    for (const QChar c : s) {
        if (c.isLetter())
            return true;
    }
    return false;
}

} // namespace

VaultPath journalPathForDate(QDate date, const VaultConfig &config, const QLocale &locale)
{
    const QString format = journalFormat(config);
    QString name = locale.toString(date, format);
    if (name.isEmpty() || parseStrict(name, format, locale) != date)
        name = QLocale::c().toString(date, DefaultFormat);
    return paths::join(config.journalsFolder, name + u".md"_s);
}

std::optional<QDate> journalDateFromPath(const VaultPath &path, const VaultConfig &config)
{
    const VaultPath p = paths::normalize(path);
    if (!paths::isMarkdown(p))
        return std::nullopt;
    const QString folder = paths::normalize(config.journalsFolder);
    QString rel = p;
    if (!folder.isEmpty()) {
        if (!p.startsWith(folder + u'/', Qt::CaseInsensitive))
            return std::nullopt;
        rel = p.sliced(folder.size() + 1);
    }
    rel = paths::stripMarkdownExtension(rel);
    if (!containsDigit(rel))
        return std::nullopt;
    QStringList formats{journalFormat(config)};
    for (const QString &f : {DefaultFormat, u"yyyy_MM_dd"_s}) {
        if (!formats.contains(f))
            formats.append(f);
    }
    QList<QLocale> locales{QLocale::c()};
    if (containsLetter(rel))
        locales.append(QLocale(QLocale::Polish, QLocale::Poland));
    for (const QString &format : std::as_const(formats)) {
        for (const QLocale &locale : std::as_const(locales)) {
            const QDate d = parseStrict(rel, format, locale);
            if (d.isValid() && d.year() >= 1 && d.year() <= 9999)
                return d;
        }
    }
    return std::nullopt;
}

} // namespace brulion::markdown
