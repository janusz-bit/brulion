// Column summaries (view.h summarize()): sums, averages, dates, counts over cell values.
#include "views/messages_p.h"
#include "views/view.h"

#include "markdown/pageprops.h"
#include "text/text.h"

#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::views {

namespace {

bool isEmptyValue(const QVariant &v)
{
    if (!v.isValid() || v.isNull())
        return true;
    switch (v.typeId()) {
    case QMetaType::QString: return v.toString().trimmed().isEmpty();
    case QMetaType::QStringList: return v.toStringList().isEmpty();
    case QMetaType::QVariantList: return v.toList().isEmpty();
    default: return false;
    }
}

std::optional<double> numberOf(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Double:
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong: {
        const double n = v.toDouble();
        return std::isfinite(n) ? std::optional(n) : std::nullopt;
    }
    case QMetaType::QString:
        if (const auto n = markdown::parseNumberInput(v.toString()))
            return n->toDouble();
        return std::nullopt;
    default: return std::nullopt;
    }
}

std::optional<QDateTime> dateOf(const QVariant &v)
{
    if (v.typeId() == QMetaType::QDate)
        return QDateTime(v.toDate(), QTime(0, 0));
    if (v.typeId() == QMetaType::QDateTime)
        return v.toDateTime();
    if (v.typeId() != QMetaType::QString)
        return std::nullopt;
    const QString s = v.toString().trimmed();
    if (s.size() < 10 || s[4] != u'-' || s[7] != u'-')
        return std::nullopt;
    const QDate d = QDate::fromString(s.first(10), Qt::ISODate);
    if (!d.isValid())
        return std::nullopt;
    if (s.size() == 10)
        return QDateTime(d, QTime(0, 0));
    QTime t = QTime::fromString(s.sliced(11, std::min<qsizetype>(5, s.size() - 11)), u"HH:mm"_s);
    return QDateTime(d, t.isValid() ? t : QTime(0, 0));
}

std::optional<bool> boolOf(const QVariant &v)
{
    if (v.typeId() == QMetaType::Bool)
        return v.toBool();
    if (v.typeId() == QMetaType::QString) {
        const QString s = v.toString().trimmed().toLower();
        if (s == u"true")
            return true;
        if (s == u"false")
            return false;
    }
    return std::nullopt;
}

/// A number for a summary: whole numbers as they are, others with at most two decimals.
QString formatNumber(double n)
{
    if (std::abs(n - std::round(n)) < 1e-9 && std::abs(n) < 1e15)
        return QString::number(qint64(std::llround(n)));
    QString s = QString::number(n, 'f', 2);
    while (s.endsWith(u'0'))
        s.chop(1);
    if (s.endsWith(u'.'))
        s.chop(1);
    return s;
}

QString formatDate(const QDateTime &d)
{
    if (d.time() == QTime(0, 0))
        return d.date().toString(Qt::ISODate);
    return d.toString(u"yyyy-MM-dd HH:mm"_s);
}

/// The text a value is counted by for `unique`.
QString uniqueKey(const QVariant &v)
{
    if (v.typeId() == QMetaType::QStringList)
        return text::foldKey(v.toStringList().join(u'\x1f'));
    if (const auto n = numberOf(v); n && v.typeId() != QMetaType::QString)
        return u"n:"_s + QString::number(*n, 'g', 15);
    return text::foldKey(v.toString().trimmed());
}

} // namespace

QString summarize(const QString &function, const QList<QVariant> &values)
{
    const qsizetype empty = std::ranges::count_if(values, isEmptyValue);
    if (function == u"count")
        return QString::number(values.size());
    if (function == u"empty")
        return QString::number(empty);
    if (function == u"filled")
        return QString::number(values.size() - empty);
    if (function == u"unique") {
        QSet<QString> seen;
        for (const QVariant &v : values) {
            if (!isEmptyValue(v))
                seen.insert(uniqueKey(v));
        }
        return QString::number(seen.size());
    }
    if (function == u"checked" || function == u"unchecked") {
        const bool want = function == u"checked";
        return QString::number(std::ranges::count_if(values, [&](const QVariant &v) { return boolOf(v) == want; }));
    }
    QList<double> numbers;
    QList<QDateTime> dates;
    for (const QVariant &v : values) {
        if (const auto d = dateOf(v))
            dates.append(*d);
        else if (const auto n = numberOf(v))
            numbers.append(*n);
    }
    const bool dateFunction = function == u"earliest" || function == u"latest";
    if (dateFunction
        || (numbers.isEmpty() && !dates.isEmpty()
            && (function == u"min" || function == u"max" || function == u"range"))) {
        if (dates.isEmpty())
            return {};
        const auto [lo, hi] = std::ranges::minmax(dates);
        if (function == u"earliest" || function == u"min")
            return formatDate(lo);
        if (function == u"latest" || function == u"max")
            return formatDate(hi);
        return messages::days(int(lo.date().daysTo(hi.date())));
    }
    if (numbers.isEmpty())
        return {};
    double sum = 0;
    for (const double n : std::as_const(numbers))
        sum += n;
    const double mean = sum / double(numbers.size());
    if (function == u"sum")
        return formatNumber(sum);
    if (function == u"average")
        return formatNumber(mean);
    const auto [lo, hi] = std::ranges::minmax(numbers);
    if (function == u"min")
        return formatNumber(lo);
    if (function == u"max")
        return formatNumber(hi);
    if (function == u"range")
        return formatNumber(hi - lo);
    if (function == u"median") {
        QList<double> sorted = numbers;
        std::ranges::sort(sorted);
        const qsizetype mid = sorted.size() / 2;
        return formatNumber(sorted.size() % 2 ? sorted.at(mid) : (sorted.at(mid - 1) + sorted.at(mid)) / 2);
    }
    if (function == u"stddev") {
        double sq = 0;
        for (const double n : std::as_const(numbers))
            sq += (n - mean) * (n - mean);
        return formatNumber(std::sqrt(sq / double(numbers.size())));
    }
    return {};
}

QString summaryLabel(const QString &function)
{
    return messages::summaryName(function);
}

} // namespace brulion::views
