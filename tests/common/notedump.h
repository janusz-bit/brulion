// dumpNote(): every field of a ParsedNote as text, value by value — for comparing parses (the
// index cache's round trips, golden digests of the parser's output). Independent of how the model
// stores its values (std::optional or Boxed tasks, QString or BlockSource raw text, QHash or
// compact properties and block ids): keys of properties and block ids are sorted, QVariants carry
// their type, null and empty strings differ ("∅" / ""). Header-only, for tests.
#pragma once

#include "types.h"

#include <QCryptographicHash>
#include <QStringList>
#include <QVariant>

#include <algorithm>

namespace brulion::testing {

namespace dump_detail {

inline QString str(QStringView s, bool isNull = false)
{
    if (isNull)
        return QStringLiteral("∅");
    QString out = QStringLiteral("\"");
    for (const QChar c : s) {
        if (c == u'\n')
            out += QStringLiteral("\\n");
        else if (c == u'\r')
            out += QStringLiteral("\\r");
        else if (c == u'\\')
            out += QStringLiteral("\\\\");
        else if (c == u'"')
            out += QStringLiteral("\\\"");
        else if (c.unicode() < 0x20)
            out += QStringLiteral("\\x%1").arg(int(c.unicode()), 2, 16, QLatin1Char('0'));
        else
            out += c;
    }
    return out + u'"';
}

inline QString str(const QString &s)
{
    return str(QStringView(s), s.isNull());
}

inline QString list(const QStringList &l)
{
    QStringList parts;
    for (const QString &s : l)
        parts.append(str(s));
    return u'[' + parts.join(u',') + u']';
}

inline QString ints(const QList<int> &l)
{
    QStringList parts;
    for (const int i : l)
        parts.append(QString::number(i));
    return u'[' + parts.join(u',') + u']';
}

inline QString variant(const QVariant &v, int depth = 0)
{
    if (depth > 64)
        return QStringLiteral("…");
    const int type = v.typeId();
    QString out = QString::number(type) + u':';
    switch (type) {
    case QMetaType::UnknownType: return out;
    case QMetaType::QStringList: return out + list(v.toStringList());
    case QMetaType::QVariantList: {
        QStringList parts;
        for (const QVariant &e : v.toList())
            parts.append(variant(e, depth + 1));
        return out + u'[' + parts.join(u',') + u']';
    }
    case QMetaType::QVariantMap: {
        const QVariantMap m = v.toMap();
        QStringList parts;
        for (auto it = m.cbegin(); it != m.cend(); ++it)
            parts.append(str(it.key()) + u'=' + variant(it.value(), depth + 1));
        return out + u'{' + parts.join(u',') + u'}';
    }
    case QMetaType::Double: return out + QString::number(v.toDouble(), 'g', 17);
    case QMetaType::QString: return out + str(v.toString());
    default: return out + str(v.toString());
    }
}

template<typename P>
QString properties(const P &props)
{
    QStringList keys = props.keys();
    std::sort(keys.begin(), keys.end());
    QStringList parts;
    for (const QString &k : std::as_const(keys))
        parts.append(str(k) + u'=' + variant(props.value(k)));
    return u'{' + parts.join(u',') + u'}';
}

inline QString date(const QDate &d)
{
    return d.isValid() ? d.toString(Qt::ISODate) : (d.isNull() ? QStringLiteral("∅") : QStringLiteral("invalid"));
}

inline QString spec(const std::optional<DateSpec> &s)
{
    if (!s)
        return QStringLiteral("-");
    return date(s->date) + (s->time ? u'T' + s->time->toString(QStringLiteral("HH:mm:ss.zzz")) : QString());
}

inline QString optDate(const std::optional<QDate> &d)
{
    return d ? date(*d) : QStringLiteral("-");
}

inline QString task(const Task &t)
{
    return QStringLiteral("task{%1 %2 %3 L%4 due=%5 sch=%6 start=%7 cr=%8 done=%9 canc=%10 rec=%11 prio=%12}")
                                  .arg(int(t.status))
                                  .arg(str(t.marker), str(t.text))
                                  .arg(t.line)
                                  .arg(spec(t.due), spec(t.scheduled), spec(t.start), optDate(t.created),
                                       optDate(t.doneDate), optDate(t.cancelledDate), str(t.recurrence))
                                  .arg(int(t.priority));
}

inline QString reminder(const Reminder &r)
{
    return QStringLiteral("rem{%1 alarm=%2 rec=%3 %4 L%5}") .arg(
            spec(r.at),
            r.alarm ? QStringLiteral("1") : QStringLiteral("0"), str(r.recurrence), str(r.text)).arg(r.line);
}

inline QString link(const Link &l)
{
    return QStringLiteral("link{%1 %2 %3 %4 %5 L%6 %7-%8}")
                                  .arg(int(l.kind))
                                  .arg(str(l.target), str(l.display), str(l.heading), str(l.blockId))
                                  .arg(l.line)
                                  .arg(l.from)
                                  .arg(l.to);
}

} // namespace dump_detail

/// Every field of `note`, one line per item (see the file comment).
inline QString dumpNote(const ParsedNote &note)
{
    using namespace dump_detail;
    QStringList out;
    out << QStringLiteral("path ") + str(note.path) << QStringLiteral("title ") + str(note.title) << QStringLiteral("frontmatter ") + variant(QVariant(note.frontmatter))
            << QStringLiteral("properties ") + properties(note.properties) << QStringLiteral("aliases ") + list(note.aliases)
                       << QStringLiteral("tags ") + list(note.tags)
                                         << QStringLiteral("journal ") + optDate(note.journalDate);
    for (qsizetype i = 0; i < note.blocks.size(); ++i) {
        const Block &b = note.blocks[i];
        QString line = QStringLiteral("block %1 id=%2 kind=%3 L%4-%5 own=%6 depth=%7 h=%8 lang=%9")
                                              .arg(i)
                                              .arg(str(b.id))
                                              .arg(int(b.kind))
                                              .arg(b.line)
                                              .arg(b.endLine)
                                              .arg(b.ownEndLine)
                                              .arg(b.depth)
                                              .arg(b.headingLevel)
                                              .arg(str(b.lang));
        const QStringView raw(b.raw);
        line += QStringLiteral(" raw=") + str(raw)
                               + QStringLiteral(" text=") + str(b.text)
                                                + QStringLiteral(" props=") + properties(b.properties);
        line += QStringLiteral(" task=") + (b.task ? task(*b.task) : QStringLiteral("-"));
        for (const Reminder &r : b.reminders)
            line += u' ' + reminder(r);
        line += QStringLiteral(" tags=") + list(b.tags)
                               + QStringLiteral(" children=") + ints(b.children)
                                                + QStringLiteral(" parent=") + QString::number(b.parent);
        out << line;
    }
    for (const Link &l : note.links)
        out << link(l);
    for (const Heading &h : note.headings)
        out << QStringLiteral("heading %1 %2 L%3").arg(h.level).arg(str(h.text)).arg(h.line);
    for (const Task &t : note.tasks)
        out << task(t);
    for (const Reminder &r : note.reminders)
        out << reminder(r);
    QStringList ids;
    for (auto it = note.blockIds.cbegin(); it != note.blockIds.cend(); ++it)
        ids.append(str(it.key()) + u'=' + QString::number(it.value()));
    std::sort(ids.begin(), ids.end());
    out << QStringLiteral("blockIds ") + ids.join(u',');
    return out.join(u'\n');
}

/// SHA-256 (hex, first 16 bytes) of the dumps of `notes`, in order.
inline QByteArray dumpDigest(const QList<ParsedNote> &notes)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const ParsedNote &n : notes) {
        hash.addData(dumpNote(n).toUtf8());
        hash.addData(QByteArrayView("\n\x1e\n"));
    }
    return hash.result().toHex().left(32);
}

} // namespace brulion::testing
