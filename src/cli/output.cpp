// What brulion-cli prints: the JSON envelope, text for people, unified diffs.
#include "cli_p.h"

#include "sync/diff.h"
#include "text/text.h"

#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace brulion::cli {

QJsonObject envelope(const Context &context, const Command *command, const Result &result)
{
    QJsonObject o{{u"ok"_s, !result.error.has_value()},
                  {u"command"_s, command ? command->id : QString()},
                  {u"vault"_s, context.vaultRoot().isEmpty() ? QJsonValue() : QJsonValue(context.vaultRoot())}};
    if (result.error) {
        QJsonObject e{{u"code"_s, result.error->code}, {u"message"_s, result.error->message}};
        if (!result.error->details.isEmpty())
            e.insert(u"details"_s, result.error->details);
        o.insert(u"error"_s, e);
    } else {
        o.insert(u"result"_s, result.value);
    }
    o.insert(u"warnings"_s, result.warnings);
    return o;
}

namespace {

QString scalar(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Bool: return v.toBool() ? u"yes"_s : u"no"_s;
    case QJsonValue::Double: {
        const double d = v.toDouble();
        return d == double(qint64(d)) ? QString::number(qint64(d)) : QString::number(d, 'g', 6);
    }
    case QJsonValue::String: return v.toString();
    case QJsonValue::Null:
    case QJsonValue::Undefined: return u"—"_s;
    default: return QString::fromUtf8(QJsonDocument::fromVariant(v.toVariant()).toJson(QJsonDocument::Compact));
    }
}

/// One line for a list item: the fields people look for first.
QString itemLine(const QJsonObject &o)
{
    static const char *const order[] = {"ref", "path",  "date", "time",    "at",    "line",  "kind",  "status",
                                        "tag", "title", "text", "excerpt", "value", "error", "count", "trashPath"};
    QStringList parts;
    for (const char *key : order) {
        const QString k = QString::fromLatin1(key);
        if (!o.contains(k))
            continue;
        const QJsonValue v = o.value(k);
        if ((v.isString() && v.toString().isEmpty()) || v.isNull())
            continue;
        if (k == u"path" && o.contains(u"ref"_s))
            continue; // the ref names it
        if (k == u"line" && o.contains(u"ref"_s))
            continue;
        QString s = scalar(v).simplified();
        if (s.size() > 160)
            s = s.left(159) + u'…';
        parts.append(k == u"text" || k == u"title" || k == u"excerpt" || k == u"path" || k == u"ref" ? s
                                                                                                     : k + u": "_s + s);
    }
    if (o.contains(u"task"_s))
        parts.prepend(u'[' + o.value(u"task"_s).toObject().value(u"status"_s).toString() + u']');
    return parts.isEmpty() ? scalar(o) : parts.join(u"  "_s);
}

void render(QString &out, const QString &key, const QJsonValue &v, int depth)
{
    const QString indent(depth * 2, u' ');
    const QString label = key.isEmpty() ? QString() : key + u':';
    if (v.isObject()) {
        if (!label.isEmpty())
            out += indent + label + u'\n';
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            render(out, it.key(), it.value(), key.isEmpty() ? depth : depth + 1);
        return;
    }
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        if (!label.isEmpty())
            out += indent + label + (a.isEmpty() ? u" —\n"_s : u"\n"_s);
        for (const QJsonValue &item : a)
            out += indent + (key.isEmpty() ? QString() : u"  "_s) + u"- "_s
                   + (item.isObject() ? itemLine(item.toObject()) : scalar(item)) + u'\n';
        return;
    }
    // Multi-line strings (page content, diffs) as a block.
    const QString s = scalar(v);
    if (s.contains(u'\n')) {
        out += indent + label + u'\n' + s;
        if (!s.endsWith(u'\n'))
            out += u'\n';
        return;
    }
    out += indent + (label.isEmpty() ? QString() : label + u' ') + s + u'\n';
}

} // namespace

QString renderText(const Command *command, const Result &result)
{
    Q_UNUSED(command);
    QString out;
    if (result.error) {
        out += u"error: "_s + result.error->message + u" ("_s + result.error->code + u")\n"_s;
        if (!result.error->details.isEmpty())
            render(out, QString(), result.error->details, 1);
    } else if (!result.text.isEmpty()) {
        out += result.text;
        if (!out.endsWith(u'\n'))
            out += u'\n';
    } else {
        render(out, QString(), result.value, 0);
    }
    for (const QJsonValue &w : result.warnings)
        out += u"warning: "_s + w.toObject().value(u"message"_s).toString() + u'\n';
    return out;
}

QString unifiedDiff(const QString &before, const QString &after, const QString &path, const QString &newPath)
{
    const auto lines = [](const QString &text) {
        QStringList out;
        const QList<text::Line> split = text::splitLines(text);
        for (qsizetype i = 0; i < split.size(); ++i) {
            if (i + 1 == split.size() && split.at(i).text.isEmpty() && split.at(i).eol.isEmpty() && i > 0)
                break; // the "line" after the final line break
            out.append(split.at(i).text);
        }
        if (text.isEmpty())
            out.clear();
        return out;
    };
    const QStringList a = lines(before);
    const QStringList b = lines(after);
    const QList<sync::DiffHunk> hunks = sync::diffLines(a, b);
    if (hunks.isEmpty())
        return {};
    constexpr qsizetype context = 3;
    const QString to = newPath.isEmpty() ? u"b/"_s + path : newPath == u"/dev/null" ? newPath : u"b/"_s + newPath;
    QString out = u"--- a/"_s + path + u"\n+++ "_s + to + u'\n';
    // Hunks closer than 2 × context lines share one block.
    qsizetype i = 0;
    while (i < hunks.size()) {
        qsizetype j = i;
        while (j + 1 < hunks.size()
               && hunks.at(j + 1).aStart - (hunks.at(j).aStart + hunks.at(j).aLength) <= 2 * context)
            ++j;
        const qsizetype aFrom = std::max<qsizetype>(0, hunks.at(i).aStart - context);
        const qsizetype bFrom = std::max<qsizetype>(0, hunks.at(i).bStart - context);
        const qsizetype aTo = std::min(a.size(), hunks.at(j).aStart + hunks.at(j).aLength + context);
        const qsizetype bTo = std::min(b.size(), hunks.at(j).bStart + hunks.at(j).bLength + context);
        const auto range = [](qsizetype from, qsizetype count) {
            return count == 0 ? u"%1,0"_s.arg(from) : u"%1,%2"_s.arg(from + 1).arg(count);
        };
        out += u"@@ -"_s + range(aFrom, aTo - aFrom) + u" +"_s + range(bFrom, bTo - bFrom) + u" @@\n"_s;
        qsizetype ai = aFrom;
        for (qsizetype k = i; k <= j; ++k) {
            const sync::DiffHunk &h = hunks.at(k);
            for (; ai < h.aStart; ++ai)
                out += u' ' + a.at(ai) + u'\n';
            for (qsizetype x = 0; x < h.aLength; ++x)
                out += u'-' + a.at(h.aStart + x) + u'\n';
            for (qsizetype x = 0; x < h.bLength; ++x)
                out += u'+' + b.at(h.bStart + x) + u'\n';
            ai = h.aStart + h.aLength;
        }
        for (; ai < aTo; ++ai)
            out += u' ' + a.at(ai) + u'\n';
        i = j + 1;
    }
    return out;
}

} // namespace brulion::cli
