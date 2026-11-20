#include "sync/cardmerge.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

/// A line's scheduling comment: where it starts, its length, its segments (without "!").
struct Comment {
    qsizetype from = -1;
    qsizetype length = 0;
    QStringList segments;
};

Comment commentOf(const QString &line)
{
    Comment c;
    const qsizetype start = line.indexOf(u"<!--SR:");
    if (start < 0)
        return c;
    const qsizetype stop = line.indexOf(u"-->", start + 7);
    if (stop < 0)
        return c;
    c.from = start;
    c.length = stop + 3 - start;
    for (const QString &part : line.mid(start + 7, stop - start - 7).split(u'!')) {
        const QString t = part.trimmed();
        if (!t.isEmpty())
            c.segments << t;
    }
    return c;
}

/// `card-repeats:: 4` → "card-repeats" (any letter case kept as written); "" for other lines.
QString cardPropertyKey(const QString &line)
{
    static const QRegularExpression re(uR"(^[ \t]*(card-[A-Za-z0-9_-]+)::(?:[ \t]|$))"_s);
    const QRegularExpressionMatch m = re.match(line);
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

QString propertyValue(const QString &line)
{
    const qsizetype at = line.indexOf(u"::");
    return at < 0 ? QString() : line.mid(at + 2).trimmed();
}

/// The line is nothing but card state: a comment alone, or a card-* property.
bool stateOnly(const QString &line)
{
    if (!cardPropertyKey(line).isEmpty())
        return true;
    const Comment c = commentOf(line);
    return c.from >= 0 && line.left(c.from).trimmed().isEmpty() && line.mid(c.from + c.length).trimmed().isEmpty();
}

QDateTime isoTime(const QString &text)
{
    QDateTime t = QDateTime::fromString(text.trimmed(), Qt::ISODateWithMs);
    if (!t.isValid())
        t = QDateTime::fromString(text.trimmed(), Qt::ISODate);
    return t.isValid() ? t.toUTC() : QDateTime();
}

/// When a segment's card was last reviewed (invalid: never). `fsrs,<due>,…,<last>` gives its last
/// review; an SM-2 segment `2026-11-24,4,250` its due minus its interval.
QDateTime reviewedAt(const QString &segment)
{
    const QStringList f = segment.split(u',');
    if (segment.startsWith(u"fsrs,"))
        return f.size() >= 10 ? isoTime(f.at(9)) : QDateTime();
    const QDate due = QDate::fromString(f.value(0).trimmed(), Qt::ISODate);
    if (!due.isValid() || due == QDate(2000, 1, 1))
        return {};
    bool ok = false;
    const int interval = f.value(1).trimmed().toInt(&ok);
    return QDateTime(due.addDays(ok ? -interval : 0), QTime(0, 0), QTimeZone::UTC);
}

/// The later of two reviews of one sibling (`base` the segment both started from).
QString laterSegment(const QString &base, const QString &local, const QString &remote)
{
    if (local == remote || remote == base)
        return local;
    if (local == base)
        return remote;
    const QDateTime a = reviewedAt(local), b = reviewedAt(remote);
    return b.isValid() && (!a.isValid() || b > a) ? remote : local;
}

/// Two versions of a line with a comment merged sibling by sibling; `base` is the line before.
QString mergeCommentLine(const QString &base, const QString &local, const QString &remote)
{
    const Comment o = commentOf(base), a = commentOf(local), b = commentOf(remote);
    if (a.from < 0)
        return remote; // only the other side has a state
    if (b.from < 0)
        return local;
    QStringList merged;
    const qsizetype n = std::max(a.segments.size(), b.segments.size());
    for (qsizetype i = 0; i < n; ++i) {
        const QString sa = a.segments.value(i), sb = b.segments.value(i), so = o.segments.value(i);
        if (sa.isEmpty())
            merged << sb;
        else if (sb.isEmpty())
            merged << sa;
        else
            merged << laterSegment(so, sa, sb);
    }
    const QString comment = u"<!--SR:!"_s + merged.join(u'!') + u"-->"_s;
    return local.left(a.from) + comment + local.mid(a.from + a.length);
}

/// The review of a run of Logseq property lines: card-last-reviewed, else card-fsrs's last field.
QDateTime runReviewedAt(const QStringList &lines)
{
    QDateTime fsrs;
    for (const QString &line : lines) {
        const QString key = cardPropertyKey(line);
        if (key == u"card-last-reviewed") {
            const QDateTime t = isoTime(propertyValue(line));
            if (t.isValid())
                return t;
        } else if (key == u"card-fsrs") {
            const QStringList f = propertyValue(line).split(u',');
            if (f.size() >= 9)
                fsrs = isoTime(f.at(8));
        }
    }
    return fsrs;
}

} // namespace

bool isCardProperty(const QString &line)
{
    return !cardPropertyKey(line).isEmpty();
}

CardSide laterCardSide(const QStringList &localRun, const QStringList &remoteRun)
{
    const QDateTime a = runReviewedAt(localRun), b = runReviewedAt(remoteRun);
    if (!a.isValid() && !b.isValid())
        return CardSide::Unknown;
    return b.isValid() && (!a.isValid() || b > a) ? CardSide::Remote : CardSide::Local;
}

QString cardStateKey(const QString &line)
{
    const QString key = cardPropertyKey(line);
    if (!key.isEmpty())
        return line.left(line.indexOf(u"::") + 2);
    const Comment c = commentOf(line);
    if (c.from < 0)
        return line;
    QString before = line.left(c.from);
    if (before.endsWith(u' '))
        before.chop(1);
    return before + line.mid(c.from + c.length);
}

std::optional<QStringList> mergeCardState(const QStringList &base, const QStringList &local, const QStringList &remote,
                                          CardSide side)
{
    if (local.size() != remote.size() || local.isEmpty())
        return std::nullopt;
    const bool added = base.isEmpty();
    if (!added && base.size() != local.size())
        return std::nullopt;
    bool anyState = false;
    for (qsizetype i = 0; i < local.size(); ++i) {
        const QString key = cardStateKey(local.at(i));
        if (key != cardStateKey(remote.at(i)) || (!added && key != cardStateKey(base.at(i))))
            return std::nullopt;
        if (added && (!stateOnly(local.at(i)) || !stateOnly(remote.at(i))))
            return std::nullopt;
        if (key != local.at(i) || key != remote.at(i) || (!added && key != base.at(i)))
            anyState = true;
    }
    if (!anyState)
        return std::nullopt;
    // Logseq's property lines both sides changed: by runs of property lines, the side of the
    // later review takes the whole run.
    QStringList out;
    qsizetype i = 0;
    while (i < local.size()) {
        if (cardPropertyKey(local.at(i)).isEmpty()) {
            const QString o = added ? QString() : base.at(i);
            const QString &a = local.at(i), &b = remote.at(i);
            if (a == b || b == o)
                out << a;
            else if (a == o)
                out << b;
            else
                out << mergeCommentLine(o, a, b);
            ++i;
            continue;
        }
        qsizetype end = i;
        while (end < local.size() && !cardPropertyKey(local.at(end)).isEmpty())
            ++end;
        const QStringList runA = local.mid(i, end - i), runB = remote.mid(i, end - i);
        const QStringList runO = added ? QStringList() : base.mid(i, end - i);
        bool bothChanged = false;
        for (qsizetype k = 0; k < runA.size(); ++k) {
            const QString o = added ? QString() : runO.at(k);
            bothChanged = bothChanged || (runA.at(k) != runB.at(k) && runA.at(k) != o && runB.at(k) != o);
        }
        if (!bothChanged) {
            for (qsizetype k = 0; k < runA.size(); ++k) {
                const QString o = added ? QString() : runO.at(k);
                out << (runA.at(k) == o ? runB.at(k) : runA.at(k));
            }
        } else {
            CardSide later = laterCardSide(runA, runB);
            if (later == CardSide::Unknown)
                later = side;
            if (later == CardSide::Unknown)
                return std::nullopt; // two changes nobody can order
            out += later == CardSide::Remote ? runB : runA;
        }
        i = end;
    }
    return out;
}

} // namespace brulion::sync
