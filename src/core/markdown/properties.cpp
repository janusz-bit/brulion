#include "markdown/properties.h"

#include "markdown/inline_p.h"
#include "markdown/lines_p.h"

#include <QRegularExpression>
#include <QSet>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

bool isListKey(const QString &key)
{
    return key == u"tags" || key == u"tag" || key == u"alias" || key == u"aliases";
}

bool isTagBodyChar(QChar c)
{
    return c.isLetterOrNumber() || c.isMark() || c == u'_' || c == u'-' || c == u'/' || c.isSurrogate();
}

} // namespace

std::optional<PropertyLine> matchPropertyLine(QStringView line)
{
    qsizetype i = 0;
    while (i < line.size() && (isAsciiLetter(line[i]) || isAsciiDigit(line[i]) || line[i] == u'_' || line[i] == u'-'))
        ++i;
    if (i == 0 || charAt(line, i) != u':' || charAt(line, i + 1) != u':')
        return std::nullopt;
    const qsizetype afterColons = i + 2;
    const QStringView tail = trimEndBlanks(line.sliced(afterColons));
    PropertyLine out;
    out.key = line.first(i).toString();
    if (tail.isEmpty()) {
        out.valueFrom = out.valueTo = afterColons;
        return out;
    }
    if (!isSpaceOrTab(tail[0]))
        return std::nullopt;
    qsizetype v = afterColons;
    while (isSpaceOrTab(charAt(line, v)))
        ++v;
    out.valueFrom = v;
    out.valueTo = afterColons + tail.size();
    return out;
}

namespace detail {

QStringList splitTopLevelCommas(QStringView s)
{
    QStringList out;
    int depth = 0;
    bool quoted = false;
    qsizetype start = 0;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (quoted) {
            if (c == u'"')
                quoted = false;
            continue;
        }
        const QChar next = charAt(s, i + 1);
        if (c == u'"') {
            quoted = true;
        } else if ((c == u'[' && next == u'[') || (c == u'(' && next == u'(')) {
            ++depth;
            ++i;
        } else if (((c == u']' && next == u']') || (c == u')' && next == u')')) && depth > 0) {
            --depth;
            ++i;
        } else if (c == u',' && depth == 0) {
            out.append(s.sliced(start, i - start).toString());
            start = i + 1;
        }
    }
    out.append(s.sliced(start).toString());
    return out;
}

QString refName(QStringView part)
{
    const QStringView t = part.trimmed();
    // ^#?\[\[([^[\]]+)\]\]$
    QStringView inner = t;
    if (inner.startsWith(u'#'))
        inner = inner.sliced(1);
    if (inner.startsWith(u"[[") && inner.endsWith(u"]]") && inner.size() > 4) {
        const QStringView name = inner.sliced(2, inner.size() - 4);
        if (!name.contains(u'[') && !name.contains(u']'))
            return name.trimmed().toString();
        return {};
    }
    // ^#([\p{L}\p{N}\p{M}_\-/]+)$ with at least one non-digit
    if (t.size() < 2 || t[0] != u'#')
        return {};
    const QStringView body = t.sliced(1);
    bool allDigits = true;
    for (const QChar c : body) {
        if (!isTagBodyChar(c))
            return {};
        if (!isAsciiDigit(c))
            allDigits = false;
    }
    return allDigits ? QString() : body.toString();
}

QString cleanName(QStringView part)
{
    QStringView t = part.trimmed();
    if (t.size() >= 2 && ((t.startsWith(u'"') && t.endsWith(u'"')) || (t.startsWith(u'\'') && t.endsWith(u'\''))))
        t = t.sliced(1, t.size() - 2).trimmed();
    if (t.startsWith(u'#'))
        t = t.sliced(1);
    if (t.startsWith(u"[[") && t.endsWith(u"]]") && t.size() >= 4)
        t = t.sliced(2, t.size() - 4);
    return t.trimmed().toString();
}

} // namespace detail

QVariant parsePropertyValue(const QString &key, QStringView raw)
{
    const QStringView value = raw.trimmed();
    if (isListKey(key)) {
        QStringList out;
        for (const QString &part : splitTopLevelCommas(value)) {
            const QString name = cleanName(part);
            if (!name.isEmpty())
                out.append(name);
        }
        return out;
    }
    if (value.isEmpty())
        return QString();
    QStringList names;
    for (const QString &part : splitTopLevelCommas(value)) {
        const QStringView p = QStringView(part).trimmed();
        if (p.isEmpty())
            continue;
        const QString name = refName(p);
        if (name.isEmpty())
            return value.toString();
        names.append(name);
    }
    if (names.isEmpty())
        return value.toString();
    return names;
}

namespace {

/// Frontmatter value as a property value; invalid QVariant for unsupported values.
QVariant frontmatterValue(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::QString: return v.toString();
    case QMetaType::Bool: return v.toBool() ? u"true"_s : u"false"_s;
    case QMetaType::LongLong:
    case QMetaType::Int:
    case QMetaType::Double:
        return v.typeId() == QMetaType::Double ? QString::number(v.toDouble(), 'g', QLocale::FloatingPointShortest)
                                               : v.toString();
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        QStringList out;
        for (const QVariant &item : v.toList()) {
            if (item.typeId() == QMetaType::QVariantList) {
                // An unquoted `[[Page]]` in YAML parses as a nested list: keep it as a wiki link.
                const QVariantList inner = item.toList();
                if (inner.size() == 1 && inner.first().typeId() == QMetaType::QString)
                    out.append(u"[["_s + inner.first().toString() + u"]]"_s);
            } else if (item.isValid() && !item.isNull() && item.typeId() != QMetaType::QVariantMap) {
                const QVariant scalar = frontmatterValue(item);
                if (scalar.typeId() == QMetaType::QString)
                    out.append(scalar.toString());
            }
        }
        return out;
    }
    default: return {};
    }
}

} // namespace

Properties frontmatterProperties(const QVariantMap &data)
{
    Properties out;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const QVariant pv = frontmatterValue(it.value());
        if (pv.isValid())
            out.insert(it.key().toLower(), pv);
    }
    return out;
}

QStringList namesFrom(const QVariant &value, bool splitWhitespace)
{
    QStringList out;
    const auto add = [&](QStringView s) {
        const QString n = cleanName(s);
        if (!n.isEmpty())
            out.append(n);
    };
    const auto addString = [&](QStringView s) {
        for (const QString &part : splitTopLevelCommas(s)) {
            const QString t = part.trimmed();
            if (splitWhitespace && !t.startsWith(u"[[") && !t.startsWith(u"#[[")) {
                for (const QString &w : t.split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts))
                    add(w);
            } else {
                add(t);
            }
        }
    };
    switch (value.typeId()) {
    case QMetaType::QString: addString(value.toString()); break;
    case QMetaType::LongLong:
    case QMetaType::Int:
    case QMetaType::Double: add(frontmatterValue(value).toString()); break;
    case QMetaType::QVariantList:
    case QMetaType::QStringList:
        for (const QVariant &item : value.toList()) {
            if (item.typeId() == QMetaType::QString) {
                const QString s = item.toString().trimmed();
                // "#a #b" as one list item: several tags.
                static const QRegularExpression severalTags(u"^#\\S+(\\s+#\\S+)+$"_s);
                if (splitWhitespace && severalTags.match(s).hasMatch())
                    addString(s);
                else
                    add(s);
            } else if (item.typeId() == QMetaType::LongLong || item.typeId() == QMetaType::Int
                       || item.typeId() == QMetaType::Double) {
                add(frontmatterValue(item).toString());
            } else if (item.typeId() == QMetaType::QVariantList) {
                const QVariantList inner = item.toList();
                if (inner.size() == 1 && inner.first().typeId() == QMetaType::QString)
                    add(inner.first().toString());
            }
        }
        break;
    default: break;
    }
    return out;
}

QVariant frontmatterGet(const QVariantMap &data, const QStringList &keys)
{
    for (const QString &want : keys) {
        for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
            if (it.key().toLower() == want)
                return it.value();
        }
    }
    return {};
}

QStringList uniqueCaseInsensitive(const QStringList &items)
{
    QSet<QString> seen;
    QStringList out;
    for (const QString &s : items) {
        const QString key = s.normalized(QString::NormalizationForm_C).toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out.append(s);
    }
    return out;
}

} // namespace brulion::markdown
