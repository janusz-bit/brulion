#include "media/media.h"

#include "util/paths.h"

#include <QUrl>

#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::media {

namespace {

constexpr qint64 MaxFragmentMs = 24LL * 60 * 60 * 1000;

bool allDigits(QStringView s)
{
    if (s.isEmpty())
        return false;
    for (const QChar c : s) {
        if (c < u'0' || c > u'9')
            return false;
    }
    return true;
}

/// "83", "83.5", ".5" as seconds (ASCII digits only); nullopt otherwise.
std::optional<double> plainSeconds(QStringView s)
{
    const qsizetype dot = s.indexOf(u'.');
    const QStringView whole = dot < 0 ? s : s.first(dot);
    const QStringView fraction = dot < 0 ? QStringView() : s.sliced(dot + 1);
    if ((whole.isEmpty() && fraction.isEmpty()) || (!whole.isEmpty() && !allDigits(whole))
        || (dot >= 0 && !allDigits(fraction)) || whole.size() > 9)
        return std::nullopt;
    double seconds = whole.isEmpty() ? 0 : whole.toLongLong();
    double scale = 0.1;
    for (const QChar c : fraction.first(std::min<qsizetype>(fraction.size(), 6))) {
        seconds += scale * (c.unicode() - u'0');
        scale /= 10;
    }
    return seconds;
}

/// One time of a `t=` value in milliseconds: -1 for an empty one (an open start or end),
/// nullopt for one that is no time.
std::optional<qint64> nptTime(QStringView s)
{
    s = s.trimmed();
    if (s.isEmpty())
        return qint64(-1);
    double seconds = 0;
    if (s.contains(u':')) {
        // [hh:]mm:ss[.fraction] — minutes and seconds below 60.
        const QList<QStringView> fields = s.split(u':');
        if (fields.size() > 3)
            return std::nullopt;
        const std::optional<double> sec = plainSeconds(fields.last());
        if (!sec || fields.last().startsWith(u'.') || *sec >= 60)
            return std::nullopt;
        seconds = *sec;
        for (qsizetype i = 0; i + 1 < fields.size(); ++i) {
            const QStringView f = fields.at(i);
            if (!allDigits(f) || f.size() > 6)
                return std::nullopt;
            const qint64 v = f.toLongLong();
            const bool minutes = i + 2 == fields.size();
            if (minutes && fields.size() == 3 && v >= 60)
                return std::nullopt;
            seconds += double(v) * (minutes ? 60 : 3600);
        }
    } else if (s.last() == u's' || s.last() == u'S' || s.last() == u'm' || s.last() == u'M' || s.last() == u'h'
               || s.last() == u'H') {
        // 1h2m3s, 90s, 5m: each unit at most once, in that order.
        const QString units = u"hms"_s;
        qsizetype at = 0;
        int lastUnit = -1;
        while (at < s.size()) {
            qsizetype end = at;
            while (end < s.size() && (s[end].isDigit() || s[end] == u'.') && s[end].unicode() < 128)
                ++end;
            if (end == at || end >= s.size())
                return std::nullopt;
            const int unit = int(units.indexOf(s[end].toLower()));
            if (unit <= lastUnit)
                return std::nullopt;
            const std::optional<double> v = plainSeconds(s.sliced(at, end - at));
            if (!v || (unit < 2 && s.sliced(at, end - at).contains(u'.')))
                return std::nullopt;
            seconds += *v * (unit == 0 ? 3600 : unit == 1 ? 60 : 1);
            lastUnit = unit;
            at = end + 1;
        }
    } else {
        const std::optional<double> sec = plainSeconds(s);
        if (!sec)
            return std::nullopt;
        seconds = *sec;
    }
    const qint64 ms = qint64(std::llround(seconds * 1000));
    if (ms > MaxFragmentMs)
        return std::nullopt;
    return ms;
}

QString seconds(qint64 ms)
{
    if (ms % 1000 == 0)
        return QString::number(ms / 1000);
    // Tenths when they say it all, else milliseconds (trailing zeros dropped).
    QString text = QString::number(double(ms) / 1000.0, 'f', 3);
    while (text.endsWith(u'0'))
        text.chop(1);
    return text;
}

} // namespace

Kind kindOf(QStringView path)
{
    const QString p = path.toString();
    if (paths::isVideoPath(p))
        return Kind::Video;
    if (paths::isAudioPath(p))
        return Kind::Audio;
    return Kind::None;
}

TimeRange parseTimeFragment(QStringView fragment)
{
    TimeRange found;
    for (const QStringView pair : fragment.split(u'&')) {
        const qsizetype eq = pair.indexOf(u'=');
        if (eq < 0 || pair.first(eq).trimmed().compare(u"t", Qt::CaseInsensitive) != 0)
            continue;
        // (A Markdown link may write ',' and ':' percent-encoded.)
        QString value = QUrl::fromPercentEncoding(pair.sliced(eq + 1).trimmed().toUtf8()).trimmed();
        if (value.startsWith(u"npt:", Qt::CaseInsensitive))
            value.remove(0, 4);
        const QList<QStringView> parts = QStringView(value).split(u',');
        if (parts.size() > 2)
            continue;
        const std::optional<qint64> start = nptTime(parts.first());
        const std::optional<qint64> end = parts.size() == 2 ? nptTime(parts.at(1)) : std::optional<qint64>(-1);
        if (!start || !end || (*start < 0 && *end < 0) || (*start >= 0 && *end >= 0 && *end <= *start))
            continue;
        found = TimeRange{*start, *end};
    }
    return found;
}

QString timeFragment(const TimeRange &range)
{
    if (!range.isValid() || (range.startMs >= 0 && range.endMs >= 0 && range.endMs <= range.startMs))
        return {};
    QString out = u"t="_s;
    if (range.startMs >= 0)
        out += seconds(range.startMs);
    if (range.endMs >= 0)
        out += u',' + seconds(range.endMs);
    return out;
}

QString formatTime(qint64 ms, qint64 totalMs)
{
    const qint64 s = std::max<qint64>(0, ms) / 1000;
    const bool hours = std::max(ms, totalMs) >= 3600 * 1000;
    const QString ss = u"%1"_s.arg(s % 60, 2, 10, u'0');
    if (hours)
        return u"%1:%2:%3"_s.arg(s / 3600).arg(s / 60 % 60, 2, 10, u'0').arg(ss);
    return u"%1:%2"_s.arg(s / 60).arg(ss);
}

QString recordingName(const QString &prefix, const QDateTime &when, const QString &extension)
{
    QString ext = extension.trimmed().toLower();
    while (ext.startsWith(u'.'))
        ext.remove(0, 1);
    const QString stem = paths::sanitizeFileName(prefix.trimmed() + u' ' + when.toString(u"yyyy-MM-dd HH.mm"_s));
    return ext.isEmpty() ? stem : stem + u'.' + ext;
}

} // namespace brulion::media
