#include "markdown/pageprops.h"

#include "markdown/frontmatter.h"
#include "markdown/frontmatter_p.h"
#include "markdown/lines_p.h"
#include "markdown/notemode.h"
#include "markdown/properties.h"
#include "text/text.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

/// Largest integer kept as a number (as the YAML parser does).
constexpr qlonglong MaxSafeInteger = 9007199254740991LL;

bool isListType(PropertyType t)
{
    return t == PropertyType::List || t == PropertyType::Tags || t == PropertyType::Aliases;
}

bool isListVariant(const QVariant &v)
{
    return v.typeId() == QMetaType::QVariantList || v.typeId() == QMetaType::QStringList;
}

bool isMapVariant(const QVariant &v)
{
    return v.typeId() == QMetaType::QVariantMap || v.typeId() == QMetaType::QVariantHash;
}

bool isNumberVariant(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Double:
    case QMetaType::Float: return true;
    default: return false;
    }
}

bool isEmptyVariant(const QVariant &v)
{
    return !v.isValid() || v.isNull();
}

QString nfcLower(const QString &s)
{
    return s.normalized(QString::NormalizationForm_C).toLower();
}

bool sameName(const QString &a, const QString &b)
{
    return nfcLower(a) == nfcLower(b);
}

bool isReservedKey(QStringView key)
{
    return key.trimmed().compare(NoteModeKey, Qt::CaseInsensitive) == 0;
}

bool isLogseqKey(QStringView key)
{
    if (key.isEmpty())
        return false;
    return std::all_of(key.begin(), key.end(),
                       [](QChar c) { return isAsciiLetter(c) || isAsciiDigit(c) || c == u'_' || c == u'-'; });
}

QString numberText(const QVariant &v)
{
    if (v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Float)
        return QString::number(v.toDouble(), 'g', QLocale::FloatingPointShortest);
    return v.toString();
}

/// A date / datetime as written: "YYYY-MM-DD" or "YYYY-MM-DD[T ]HH:mm[:ss]".
struct DateText {
    QDate date;
    QTime time;
    bool hasTime = false;
    QChar separator = u'T';
    bool seconds = false;
};

std::optional<DateText> parseDateText(QStringView s)
{
    static const QRegularExpression re(u"^(\\d{4})-(\\d{2})-(\\d{2})(?:([T ])(\\d{2}):(\\d{2})(?::(\\d{2}))?)?$"_s);
    const auto m = re.matchView(s);
    if (!m.hasMatch())
        return std::nullopt;
    DateText out;
    out.date = QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    if (!out.date.isValid())
        return std::nullopt;
    if (m.hasCaptured(4)) {
        out.hasTime = true;
        out.separator = m.captured(4).at(0);
        out.seconds = m.hasCaptured(7);
        out.time = QTime(m.captured(5).toInt(), m.captured(6).toInt(), out.seconds ? m.captured(7).toInt() : 0);
        if (!out.time.isValid())
            return std::nullopt;
    }
    return out;
}

QString formatDateText(const DateText &d)
{
    QString out = d.date.toString(u"yyyy-MM-dd"_s);
    if (d.hasTime)
        out += d.separator + d.time.toString(d.seconds ? u"HH:mm:ss"_s : u"HH:mm"_s);
    return out;
}

/// `[[YYYY-MM-DD]]` (a Logseq journal link) → the date text, else nullopt.
std::optional<QString> linkedDate(QStringView s)
{
    if (s.startsWith(u"[[") && s.endsWith(u"]]") && s.size() > 4) {
        const QStringView inner = s.sliced(2, s.size() - 4);
        if (const auto d = parseDateText(inner); d && !d->hasTime)
            return inner.toString();
    }
    return std::nullopt;
}

/// A Logseq number: `[-+]?\d+(\.\d+)?` (strict: what a person typed elsewhere stays text).
std::optional<QVariant> strictNumber(QStringView s)
{
    static const QRegularExpression re(u"^[-+]?\\d+(\\.\\d+)?$"_s);
    if (!re.matchView(s).hasMatch())
        return std::nullopt;
    if (!s.contains(u'.')) {
        bool ok = false;
        const qlonglong n = s.toLongLong(&ok);
        if (ok && n <= MaxSafeInteger && n >= -MaxSafeInteger)
            return QVariant(n);
    }
    bool ok = false;
    const double d = s.toDouble(&ok);
    if (!ok || !std::isfinite(d))
        return std::nullopt;
    return QVariant(d);
}

/// An item of a YAML list as text: scalars as written, an unquoted [[Page]] — a one-string
/// nested list; in a block list (`- [[Page]]`) two levels deep — as "[[Page]]"; nullopt for
/// anything else (maps, longer nested lists).
std::optional<QString> yamlItemText(const QVariant &item)
{
    if (item.typeId() == QMetaType::QString)
        return item.toString();
    if (item.typeId() == QMetaType::Bool)
        return item.toBool() ? u"true"_s : u"false"_s;
    if (isNumberVariant(item))
        return numberText(item);
    if (isListVariant(item)) {
        const QVariantList inner = item.toList();
        if (inner.size() != 1)
            return std::nullopt;
        QVariant one = inner.first();
        if (isListVariant(one)) {
            const QVariantList deeper = one.toList();
            if (deeper.size() != 1)
                return std::nullopt;
            one = deeper.first();
        }
        if (one.typeId() == QMetaType::QString)
            return QString(u"[["_s + one.toString() + u"]]"_s);
    }
    return std::nullopt;
}

/// A YAML value's list items as the panel shows them (yamlItemText(); empty items dropped).
QStringList yamlListItems(const QVariant &value)
{
    QStringList out;
    if (!isListVariant(value)) {
        if (const auto t = isEmptyVariant(value) ? std::nullopt : yamlItemText(value))
            out.append(*t);
        return out;
    }
    for (const QVariant &item : value.toList()) {
        if (isEmptyVariant(item))
            continue;
        if (const auto t = yamlItemText(item))
            out.append(*t);
    }
    return out;
}

QString unsupportedText(const QVariant &v)
{
    return isMapVariant(v) ? u"{…}"_s : u"[…]"_s;
}

/// The text of one value of a YAML scalar item, read with the parser: `- "a"` → "a".
QVariant yamlItemValue(QStringView itemText)
{
    return parseYaml(QString(u"k: "_s + itemText)).value(u"k"_s);
}

// ---- the value to write -------------------------------------------------------------------------

/// A value as it is to be written, in the canonical form of its type.
struct Intended {
    PropertyType type = PropertyType::Text;
    bool empty = false;
    QVariant scalar;   ///< Text: QString; Number: qlonglong / double; Checkbox: bool; Date / DateTime: QString
    QStringList items; ///< List / Tags / Aliases
};

QStringList rawItems(const QVariant &value)
{
    QStringList out;
    if (isListVariant(value)) {
        for (const QVariant &v : value.toList())
            out.append(v.toString());
    } else if (!isEmptyVariant(value)) {
        const QString s = value.toString();
        if (!s.trimmed().isEmpty())
            out.append(s);
    }
    return out;
}

QString oneLine(const QString &s)
{
    QString out = s;
    out.replace(u"\r\n"_s, u" "_s);
    out.replace(u'\r', u' ');
    out.replace(u'\n', u' ');
    return out.trimmed();
}

/// `value` of `type` in its canonical form; nullopt when it is no such value (InvalidValue).
/// `old`: the current value as read (a datetime keeps its separator and seconds).
std::optional<Intended> intendedValue(const QVariant &value, PropertyType type, const QVariant &old)
{
    Intended out;
    out.type = type;
    switch (type) {
    case PropertyType::Text:
    case PropertyType::Unsupported: {
        out.type = PropertyType::Text;
        QString s = isEmptyVariant(value) ? QString() : value.toString();
        s.replace(u"\r\n"_s, u"\n"_s);
        s.replace(u'\r', u'\n');
        s = s.trimmed();
        out.empty = s.isEmpty();
        if (!out.empty)
            out.scalar = s;
        return out;
    }
    case PropertyType::Number: {
        if (isNumberVariant(value)) {
            if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::Float) {
                const double d = value.toDouble();
                if (!std::isfinite(d))
                    return std::nullopt;
                out.scalar = d;
            } else {
                out.scalar = value.toLongLong();
            }
            return out;
        }
        const QString s = isEmptyVariant(value) ? QString() : value.toString().trimmed();
        if (s.isEmpty()) {
            out.empty = true;
            return out;
        }
        const auto n = parseNumberInput(s);
        if (!n)
            return std::nullopt;
        out.scalar = *n;
        return out;
    }
    case PropertyType::Checkbox: {
        if (value.typeId() == QMetaType::Bool) {
            out.scalar = value.toBool();
            return out;
        }
        const QString s = isEmptyVariant(value) ? QString() : value.toString().trimmed();
        if (s.isEmpty()) {
            out.empty = true;
            return out;
        }
        if (s.compare(u"true", Qt::CaseInsensitive) == 0)
            out.scalar = true;
        else if (s.compare(u"false", Qt::CaseInsensitive) == 0)
            out.scalar = false;
        else
            return std::nullopt;
        return out;
    }
    case PropertyType::Date:
    case PropertyType::DateTime: {
        QString s;
        if (value.typeId() == QMetaType::QDate)
            s = value.toDate().toString(u"yyyy-MM-dd"_s);
        else if (value.typeId() == QMetaType::QDateTime)
            s = value.toDateTime().toString(u"yyyy-MM-ddTHH:mm"_s);
        else if (!isEmptyVariant(value))
            s = value.toString().trimmed();
        if (s.isEmpty()) {
            out.empty = true;
            return out;
        }
        if (const auto d = linkedDate(s))
            s = *d;
        auto d = parseDateText(s);
        if (!d)
            return std::nullopt;
        if (type == PropertyType::Date) {
            if (d->hasTime)
                return std::nullopt;
            out.scalar = formatDateText(*d);
            return out;
        }
        const auto before = isEmptyVariant(old) ? std::nullopt : parseDateText(old.toString().trimmed());
        const bool typedSeconds = d->hasTime && d->seconds;
        if (!d->hasTime) {
            d->hasTime = true;
            d->time = QTime(0, 0);
        }
        // New values: Obsidian's form (`T`); an existing datetime keeps its separator and seconds.
        d->separator = before && before->hasTime ? before->separator : QChar(u'T');
        d->seconds = typedSeconds || (before && before->hasTime && before->seconds);
        out.scalar = formatDateText(*d);
        return out;
    }
    case PropertyType::List:
    case PropertyType::Tags:
    case PropertyType::Aliases: {
        QStringList items;
        for (const QString &raw : rawItems(value)) {
            QString item = oneLine(raw);
            if (type != PropertyType::List) {
                if (item.isEmpty())
                    continue;
                item = cleanListItem(type, item);
                if (item.isEmpty())
                    return std::nullopt;
            }
            if (!item.isEmpty())
                items.append(item);
        }
        out.items = uniqueCaseInsensitive(items);
        out.empty = out.items.isEmpty();
        return out;
    }
    }
    return std::nullopt;
}

/// The value YAML reads back for a scalar Intended (QVariant() when empty).
QVariant yamlScalarValue(const Intended &t)
{
    return t.empty ? QVariant() : t.scalar;
}

QString canonicalYaml(const QVariant &v)
{
    return stringifyYaml(QVariantMap{{u"k"_s, v}});
}

/// Names of the tags / aliases a YAML value holds (namesFrom()'s rules; list items as
/// yamlListItems() reads them, so `- [[Dom]]` is "Dom").
QStringList yamlNames(const QVariant &value, PropertyType kind)
{
    if (isListVariant(value))
        return namesFrom(QVariant(yamlListItems(value)), kind == PropertyType::Tags);
    return namesFrom(value, kind == PropertyType::Tags);
}

/// The YAML string `old` is text `t` once its ends are trimmed (as typed text is): a text
/// committed unchanged keeps its form (a `|` block scalar's last line break, quoted blanks).
bool sameTrimmedText(const QVariant &old, const Intended &t)
{
    if (t.type != PropertyType::Text || old.typeId() != QMetaType::QString)
        return false;
    QString s = old.toString();
    s.replace(u"\r\n"_s, u"\n"_s);
    s.replace(u'\r', u'\n');
    s = s.trimmed();
    return t.empty ? s.isEmpty() : s == t.scalar.toString();
}

/// The YAML value `read` (of a present key) reads as `t`.
bool yamlReadsAs(const QVariant &read, const Intended &t)
{
    switch (t.type) {
    case PropertyType::List: return isListVariant(read) && yamlListItems(read) == t.items;
    case PropertyType::Tags:
    case PropertyType::Aliases:
        if (t.items.isEmpty())
            return isEmptyVariant(read) || (isListVariant(read) && read.toList().isEmpty());
        return yamlNames(read, t.type) == t.items;
    default: return canonicalYaml(read) == canonicalYaml(yamlScalarValue(t));
    }
}

// ---- YAML documents -----------------------------------------------------------------------------

struct YamlDoc {
    FrontmatterBlock fm;
    QList<text::Line> lines;
    QVariantMap data;
    QStringList keys;
    QList<FrontmatterEntry> entries;
    QString problem; ///< "" | "notMapping" | "indented"
};

std::optional<YamlDoc> yamlDoc(QStringView content)
{
    const auto fm = findFrontmatter(content);
    if (!fm)
        return std::nullopt;
    YamlDoc doc;
    doc.fm = *fm;
    doc.lines = text::splitLines(content);
    doc.data = parseYaml(fm->yaml, &doc.keys);
    doc.entries = frontmatterEntries(doc.lines, fm->endLine);
    if (!isYamlMapping(fm->yaml)) {
        doc.problem = u"notMapping"_s;
    } else {
        QSet<QString> placed;
        for (const FrontmatterEntry &e : std::as_const(doc.entries))
            placed.insert(e.key);
        for (const QString &k : std::as_const(doc.keys)) {
            if (!placed.contains(k)) {
                doc.problem = u"indented"_s;
                break;
            }
        }
    }
    return doc;
}

QList<FrontmatterEntry> entriesOf(const YamlDoc &doc, const QString &key)
{
    QList<FrontmatterEntry> out;
    for (const FrontmatterEntry &e : doc.entries) {
        if (e.key == key)
            out.append(e);
    }
    return out;
}

QString canonicalWithout(QVariantMap data, const QStringList &skip)
{
    for (const QString &k : skip)
        data.remove(k);
    return stringifyYaml(data);
}

/// Checks an edit of `before` (frontmatter `fm`): the BOM kept, the body byte-identical, every key
/// but `touched` parses to the same value, and `check` accepts the edited mapping.
bool verifyYaml(const QString &before, const FrontmatterBlock &fm, const QString &after, const QStringList &touched,
                const std::function<bool(const QVariantMap &)> &check)
{
    if (startsWithBom(before) != startsWithBom(after))
        return false;
    const QStringView body = QStringView(before).sliced(fm.bodyStart);
    const QVariantMap beforeData = parseYaml(fm.yaml);
    const auto edited = findFrontmatter(after);
    if (!edited) {
        // The whole block went: only when nothing else was in it.
        const QString bom = startsWithBom(before) ? QString(QChar(ByteOrderMark)) : QString();
        return after == bom + body && canonicalWithout(beforeData, touched).isEmpty() && check({});
    }
    if (QStringView(after).sliced(edited->bodyStart) != body)
        return false;
    const QVariantMap afterData = parseYaml(edited->yaml);
    return canonicalWithout(afterData, touched) == canonicalWithout(beforeData, touched) && check(afterData);
}

/// A line with its terminator, for building entries.
struct OutLine {
    QString text;
    QString eol;
};

QString joined(const QList<OutLine> &lines)
{
    QString out;
    for (const OutLine &l : lines)
        out += l.text + l.eol;
    return out;
}

/// Longest common subsequence of `a` and `b` as index pairs (small lists).
QList<std::pair<int, int>> lcsPairs(const QStringList &a, const QStringList &b)
{
    const int n = int(a.size());
    const int m = int(b.size());
    std::vector<int> dp(size_t(n + 1) * size_t(m + 1), 0);
    const auto at = [&](int i, int j) -> int & { return dp[size_t(i) * size_t(m + 1) + size_t(j)]; };
    for (int i = n - 1; i >= 0; --i) {
        for (int j = m - 1; j >= 0; --j)
            at(i, j) = a[i] == b[j] ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
    }
    QList<std::pair<int, int>> out;
    int i = 0;
    int j = 0;
    while (i < n && j < m) {
        if (a[i] == b[j]) {
            out.append({i, j});
            ++i;
            ++j;
        } else if (at(i + 1, j) >= at(i, j + 1)) {
            ++i;
        } else {
            ++j;
        }
    }
    return out;
}

/// The key of a list item for matching old items with new ones.
QString itemMatchKey(const QVariant &itemValue, PropertyType type)
{
    if (type == PropertyType::List) {
        const QStringList items = yamlListItems(QVariantList{itemValue});
        return items.size() == 1 ? items.first() : QString(QChar(0)); // never matches a new item
    }
    const QStringList names = yamlNames(QVariantList{itemValue}, type);
    return names.size() == 1 ? names.first() : QString(QChar(0));
}

/// The leading blanks of the frontmatter's block list items ("  " when it has none: Obsidian).
QString blockListIndent(const YamlDoc &doc)
{
    for (const FrontmatterEntry &e : doc.entries) {
        for (int i = e.first + 1; i < e.last; ++i) {
            const QStringView t = doc.lines[i].text;
            const Indent ind = indentOf(t);
            const QStringView rest = t.sliced(ind.chars);
            if (rest == u"-" || rest.startsWith(u"- ") || rest.startsWith(u"-\t"))
                return t.first(ind.chars).toString();
        }
    }
    return u"  "_s;
}

/// The lines of YAML entry `key` holding `t` (with terminators): `old` is the entry it replaces
/// (its key part, comment and list style are kept), nullptr for a new one. `keepComments`: the
/// indented `# comment` lines under a value written on its key line (`status: a` then
/// `  # uwaga`) stay after the new value.
QString yamlEntryText(const YamlDoc &doc, const FrontmatterEntry *old, const QString &key, const Intended &t,
                      const QString &eol, const QString &lastEol, bool keepComments = true)
{
    QString keyPart = yamlString(key) + u':';
    QString comment;             // a `# comment` of the key line
    QString commentGap = u" "_s; // blanks before it
    QStringView rest;            // the key line's value text (without the comment)
    QStringView firstLine;
    if (old) {
        firstLine = doc.lines[old->first].text;
        if (const auto kl = splitYamlKeyLine(firstLine)) {
            keyPart = firstLine.first(kl->colon + 1).toString();
            const qsizetype c = yamlCommentStart(firstLine, kl->rest);
            if (c < firstLine.size()) {
                comment = firstLine.sliced(c).toString();
                qsizetype g = c;
                while (g > kl->rest && isSpaceOrTab(firstLine[g - 1]))
                    --g;
                if (g < c && g > kl->rest)
                    commentGap = firstLine.sliced(g, c - g).toString();
            }
            rest = trimEndBlanks(firstLine.sliced(kl->rest, c - kl->rest));
        }
    }
    // Comment lines under a value on the key line (only comments and blank lines follow it; a
    // block scalar's lines are its text, `#` too): they stay after the new value.
    QList<OutLine> keptComments;
    if (old && keepComments && old->last > old->first + 1 && !rest.startsWith(u'|') && !rest.startsWith(u'>')) {
        bool onlyComments = true;
        for (int i = old->first + 1; i < old->last && onlyComments; ++i) {
            const QStringView l = QStringView(doc.lines[i].text).trimmed();
            onlyComments = l.isEmpty() || l.startsWith(u'#');
        }
        if (onlyComments) {
            for (int i = old->first + 1; i < old->last; ++i)
                keptComments.append({doc.lines[i].text, doc.lines[i].eol});
        }
    }
    QList<OutLine> out;
    const auto withComment = [&](const QString &line, bool hasValue) -> QString {
        if (comment.isEmpty())
            return line;
        return line + (hasValue ? commentGap : u" "_s) + comment;
    };
    // The entry's last line ends as the old entry did (the kept comments end it when there are).
    const auto finish = [&]() -> QString {
        if (keptComments.isEmpty()) {
            out.last().eol = lastEol;
        } else {
            out.last().eol = eol.isEmpty() ? text::detectEol(doc.fm.yaml) : eol;
            out.append(keptComments);
        }
        return joined(out);
    };

    if (!isListType(t.type)) {
        if (t.empty) {
            out.append({withComment(keyPart, false), eol});
        } else if (t.type == PropertyType::Text && t.scalar.toString().contains(u'\n')) {
            const QString s = t.scalar.toString();
            if (const auto block = yamlBlockScalar(s, u"  "_s)) {
                out.append({keyPart + u' ' + block->first(), eol});
                for (qsizetype k = 1; k < block->size(); ++k)
                    out.append({block->at(k), eol});
            } else {
                out.append({keyPart + u' ' + yamlQuoted(s), eol});
            }
        } else {
            const QString value =
                    t.type == PropertyType::Number ? numberText(t.scalar) : yamlScalar(t.scalar).value_or(QString());
            out.append({withComment(keyPart + u' ' + value, true), eol});
        }
        return finish();
    }

    if (t.items.isEmpty()) {
        out.append({withComment(keyPart + u" []"_s, true), eol});
        return finish();
    }

    // The style of the old entry: a block list, a flow list or anything else.
    bool block = false;
    bool flow = false;
    if (old) {
        block = rest.isEmpty() && old->last > old->first + 1;
        flow = rest.startsWith(u'[') && rest != u"[]";
    }

    if (flow) {
        // One line; the items as written where they did not change.
        QString text = rest.toString();
        bool commentsInside = false;
        for (int i = old->first + 1; i < old->last; ++i) {
            const QStringView l = QStringView(doc.lines[i].text).trimmed();
            if (yamlCommentStart(l) < l.size())
                commentsInside = true;
            text += u' ' + l;
        }
        QStringList oldRaw;
        if (!commentsInside && text.endsWith(u']')) {
            const QStringView inner = QStringView(text).sliced(1, text.size() - 2);
            // Split on commas outside quotes and brackets.
            int depth = 0;
            QChar quote;
            qsizetype start = 0;
            for (qsizetype i = 0; i <= inner.size(); ++i) {
                const QChar c = i < inner.size() ? inner[i] : QChar(u',');
                if (!quote.isNull()) {
                    if (c == quote)
                        quote = QChar();
                    continue;
                }
                if ((c == u'"' || c == u'\'') && QStringView(inner.sliced(start, i - start)).trimmed().isEmpty()) {
                    quote = c;
                } else if (c == u'[' || c == u'{') {
                    ++depth;
                } else if (c == u']' || c == u'}') {
                    --depth;
                } else if (c == u',' && depth == 0) {
                    const QString item = inner.sliced(start, std::min(i, inner.size()) - start).trimmed().toString();
                    if (!item.isEmpty())
                        oldRaw.append(item);
                    start = i + 1;
                }
            }
        }
        QStringList oldKeys;
        for (const QString &raw : std::as_const(oldRaw))
            oldKeys.append(itemMatchKey(yamlItemValue(raw), t.type));
        QStringList parts;
        QHash<int, int> keptOld; // new index -> old index
        for (const auto &[o, n] : lcsPairs(oldKeys, t.items))
            keptOld.insert(n, o);
        for (int k = 0; k < t.items.size(); ++k)
            parts.append(keptOld.contains(k) ? oldRaw[keptOld.value(k)] : yamlFlowString(t.items[k]));
        const bool oneLineBefore = old->last == old->first + 1 || !keptComments.isEmpty();
        const QString line = keyPart + u" ["_s + parts.join(u", "_s) + u']';
        out.append({oneLineBefore ? withComment(line, true) : line, eol});
        return finish();
    }

    // A block list: the key line, then `- item` lines.
    QString itemPrefix = blockListIndent(doc) + u"- "_s;
    struct Unit {
        QList<OutLine> lines; ///< leading comments / blank lines, then the item line
        QString key;
    };
    QList<Unit> units;
    QList<OutLine> trailing; // comments after the last item
    bool simple = block;
    if (block) {
        Unit current;
        int itemIndent = -1;
        for (int i = old->first + 1; i < old->last && simple; ++i) {
            const text::Line &l = doc.lines[i];
            const Indent ind = indentOf(l.text);
            const QStringView body = QStringView(l.text).sliced(ind.chars);
            if (isBlank(l.text) || body.startsWith(u'#')) {
                current.lines.append({l.text, l.eol});
                continue;
            }
            const bool item = body == u"-" || body.startsWith(u"- ") || body.startsWith(u"-\t");
            if (!item || (itemIndent >= 0 && ind.col != itemIndent)) {
                simple = false; // a multi-line item or a nested list: rewritten whole
                break;
            }
            if (itemIndent < 0) {
                itemIndent = ind.col;
                qsizetype v = ind.chars + 1;
                while (v < l.text.size() && isSpaceOrTab(l.text[v]))
                    ++v;
                // `- ` / `-   ` / `-\t` as written; a bare `-` gives the usual `- `.
                itemPrefix = v > ind.chars + 1 ? l.text.first(v) : l.text.first(ind.chars) + u"- "_s;
            }
            QStringView itemText = body.sliced(1).trimmed();
            current.lines.append({l.text, l.eol});
            current.key = itemMatchKey(yamlItemValue(itemText), t.type);
            units.append(current);
            current = Unit();
        }
        trailing = current.lines;
    }
    if (simple && block)
        out.append({firstLine.toString(), doc.lines[old->first].eol});
    else
        out.append({withComment(keyPart, false), eol});
    QStringList oldKeys;
    if (simple) {
        for (const Unit &u : std::as_const(units))
            oldKeys.append(u.key);
    }
    QHash<int, int> keptOld;
    for (const auto &[o, n] : lcsPairs(oldKeys, t.items))
        keptOld.insert(n, o);
    for (int k = 0; k < t.items.size(); ++k) {
        if (keptOld.contains(k))
            out.append(units[keptOld.value(k)].lines);
        else
            out.append({itemPrefix + yamlScalar(t.items[k]).value_or(yamlQuoted(t.items[k])), eol});
    }
    if (simple)
        out.append(trailing);
    return finish();
}

/// Replace (or add) entry `key` of a frontmatter with `t`; verified. nullopt: not safely possible.
std::optional<QString> writeYamlEntry(const QString &content, const YamlDoc &doc, const QString &key, const Intended &t)
{
    const QList<FrontmatterEntry> entries = entriesOf(doc, key);
    const auto verified = [&](const QString &edited) {
        return verifyYaml(content, doc.fm, edited, {key}, [&](const QVariantMap &data) {
            return data.contains(key) && yamlReadsAs(data.value(key), t);
        });
    };
    if (entries.isEmpty()) {
        const QString eol = doc.lines[doc.fm.endLine - 1].eol;
        const QString edited = rebuildLines(doc.lines, {}, doc.fm.endLine, yamlEntryText(doc, nullptr, key, t, eol, eol));
        return verified(edited) ? std::optional<QString>(edited) : std::nullopt;
    }
    const FrontmatterEntry &first = entries.first();
    // Comment lines under the old value stay — unless they would read as part of the new one
    // (under a block scalar): then they go with the old value.
    for (const bool keepComments : {true, false}) {
        const QString text = yamlEntryText(doc, &first, key, t, doc.lines[first.first].eol,
                                           doc.lines[first.last - 1].eol, keepComments);
        const QString edited = rebuildLines(doc.lines, entries, first.first, text);
        if (verified(edited))
            return edited;
    }
    return std::nullopt;
}

/// A new frontmatter block holding only `key` in front of `content`.
std::optional<QString> createFrontmatter(const QString &content, const QString &key, const Intended &t)
{
    const QString eol = text::detectEol(content);
    const bool bom = startsWithBom(content);
    YamlDoc empty; // no entries: default styles
    QString entry = yamlEntryText(empty, nullptr, key, t, eol, eol);
    QString out = bom ? QString(QChar(ByteOrderMark)) : QString();
    out += u"---"_s + eol + entry + u"---"_s + eol;
    const QStringView body = QStringView(content).sliced(bom ? 1 : 0);
    out += body;
    const auto fm = findFrontmatter(out);
    if (!fm || QStringView(out).sliced(fm->bodyStart) != body)
        return std::nullopt;
    QStringList keys;
    const QVariantMap data = parseYaml(fm->yaml, &keys);
    if (keys != QStringList{key} || !yamlReadsAs(data.value(key), t))
        return std::nullopt;
    return out;
}

// ---- Logseq lines -------------------------------------------------------------------------------

/// One `key:: value` line of the leading block; positions in the line's text.
struct LogseqLine {
    int line = 0;
    QString key;
    qsizetype keyFrom = 0;
    qsizetype afterColons = 0;
    qsizetype valueFrom = 0;
    qsizetype valueTo = 0;
    QString raw; ///< the value as written (trimmed; a `%%comment%%` in it cut off)
};

/// The leading `key:: value` lines from line `from` on (after blank lines), found as
/// parseStructure() finds them: on the text with `%%comments%%` masked, a BOM ignored.
QList<LogseqLine> logseqLines(QStringView content, const QList<text::Line> &lines, int from)
{
    QList<LogseqLine> out;
    const QList<Range> comments = docCommentRanges(content, lines);
    const QString masked = comments.isEmpty() ? QString() : maskRanges(content, comments);
    const qsizetype bom = startsWithBom(content) ? 1 : 0;
    const auto maskedLine = [&](int k) {
        const QStringView whole = comments.isEmpty() ? QStringView(lines[k].text)
                                                     : QStringView(masked).sliced(lines[k].start, lines[k].text.size());
        return whole.sliced(k == 0 ? std::min(bom, whole.size()) : 0);
    };
    const int n = int(lines.size());
    int j = from;
    while (j < n && isBlank(maskedLine(j)))
        ++j;
    for (; j < n; ++j) {
        const QStringView m = maskedLine(j);
        const auto pm = matchPropertyLine(m);
        if (!pm)
            break;
        const qsizetype skip = lines[j].text.size() - m.size();
        LogseqLine l;
        l.line = j;
        l.key = pm->key;
        l.keyFrom = skip;
        l.afterColons = skip + pm->key.size() + 2;
        l.valueFrom = skip + pm->valueFrom;
        l.valueTo = skip + pm->valueTo;
        QStringView value = QStringView(lines[j].text).sliced(l.valueFrom, l.valueTo - l.valueFrom);
        const QStringView maskedValue = m.sliced(pm->valueFrom, pm->valueTo - pm->valueFrom);
        for (qsizetype k = 0; k < value.size(); ++k) {
            if (value[k] != maskedValue[k]) {
                value = value.first(k);
                break;
            }
        }
        l.raw = value.trimmed().toString();
        out.append(l);
    }
    return out;
}

int bodyStartLine(QStringView content)
{
    const auto fm = findFrontmatter(content);
    return fm ? fm->bodyStartLine : 0;
}

PageProperty logseqProperty(const LogseqLine &l)
{
    PageProperty p;
    p.key = l.key;
    p.source = PropertySource::Logseq;
    p.line = l.line;
    p.empty = l.raw.isEmpty();
    p.type = inferPropertyType(l.key, l.raw, PropertySource::Logseq, PropertyType::Text);
    p.text = l.raw;
    switch (p.type) {
    case PropertyType::Number: p.value = strictNumber(l.raw).value_or(QVariant()); break;
    case PropertyType::Checkbox: p.value = l.raw.compare(u"true", Qt::CaseInsensitive) == 0; break;
    case PropertyType::Date:
        if (const auto d = linkedDate(l.raw)) {
            p.value = *d;
            p.linkDate = true;
        } else {
            p.value = l.raw;
        }
        break;
    case PropertyType::List:
        for (const QString &part : splitTopLevelCommas(l.raw)) {
            const QString item = part.trimmed();
            if (!item.isEmpty())
                p.items.append(item);
        }
        p.value = p.items;
        break;
    case PropertyType::Tags:
    case PropertyType::Aliases:
        p.items = parsePropertyValue(l.key.toLower(), l.raw).toStringList();
        p.value = p.items;
        p.empty = p.items.isEmpty();
        break;
    default:
        if (!p.empty)
            p.value = l.raw;
        break;
    }
    if (p.empty && !isListType(p.type))
        p.value = QVariant();
    if (isListType(p.type))
        p.text = p.items.join(u", "_s);
    return p;
}

PageProperty yamlProperty(const QString &key, const QVariant &v, int line, bool blockEditable)
{
    PageProperty p;
    p.key = key;
    p.source = PropertySource::Yaml;
    p.line = line;
    p.type = inferPropertyType(key, v, PropertySource::Yaml, PropertyType::Text);
    p.empty = isEmptyVariant(v);
    switch (p.type) {
    case PropertyType::List:
        p.items = yamlListItems(v);
        p.value = p.items;
        p.text = p.items.join(u", "_s);
        p.empty = p.items.isEmpty();
        break;
    case PropertyType::Tags:
    case PropertyType::Aliases:
        p.items = yamlNames(v, p.type);
        p.value = p.items;
        p.text = p.items.join(u", "_s);
        p.empty = p.items.isEmpty();
        break;
    case PropertyType::Number:
        p.value = v;
        p.text = numberText(v);
        break;
    case PropertyType::Checkbox:
        p.value = v.toBool();
        p.text = v.toBool() ? u"true"_s : u"false"_s;
        break;
    case PropertyType::Unsupported: p.text = unsupportedText(v); break;
    default:
        if (!p.empty) {
            p.value = v.toString();
            p.multiline = p.value.toString().contains(u'\n');
            p.text = oneLine(p.value.toString());
        }
        break;
    }
    p.editable = blockEditable && p.type != PropertyType::Unsupported;
    return p;
}

/// The page properties of `content` with the lines they were read from.
struct PageRead {
    PageProperties props;
    std::optional<YamlDoc> yaml;
    QList<LogseqLine> logseq;
    QList<text::Line> lines;
};

PageRead readPage(QStringView content)
{
    PageRead r;
    r.yaml = yamlDoc(content);
    r.lines = r.yaml ? r.yaml->lines : text::splitLines(content);
    int from = 0;
    if (r.yaml) {
        const YamlDoc &doc = *r.yaml;
        PagePropertyBlock block;
        block.source = PropertySource::Yaml;
        block.firstLine = 0;
        block.lastLine = doc.fm.endLine;
        block.problem = doc.problem;
        block.editable = doc.problem.isEmpty();
        QHash<QString, int> lineOf;
        for (const FrontmatterEntry &e : doc.entries) {
            if (!lineOf.contains(e.key))
                lineOf.insert(e.key, e.first);
        }
        for (const QString &key : doc.keys) {
            if (key == NoteModeKey)
                continue; // the page header's mode toggle stands for it
            r.props.items.append(yamlProperty(key, doc.data.value(key), lineOf.value(key, -1), block.editable));
        }
        r.props.yaml = block;
        from = doc.fm.bodyStartLine;
    }
    r.logseq = logseqLines(content, r.lines, from);
    if (!r.logseq.isEmpty()) {
        PagePropertyBlock block;
        block.source = PropertySource::Logseq;
        block.firstLine = r.logseq.first().line;
        block.lastLine = r.logseq.last().line;
        r.props.logseq = block;
        for (const LogseqLine &l : std::as_const(r.logseq))
            r.props.items.append(logseqProperty(l));
    }
    return r;
}

/// Another property of the page (not `key` of `source` itself) has the key `key`.
bool pageHasKey(const PageProperties &props, const QString &key, std::optional<PropertySource> except = {},
                const QString &exceptKey = {})
{
    for (const PageProperty &p : props.items) {
        if (except && p.source == *except
            && (p.source == PropertySource::Logseq ? sameName(p.key, exceptKey) : p.key == exceptKey))
            continue;
        if (sameName(p.key, key))
            return true;
    }
    return false;
}

QString replaceLine(const QList<text::Line> &lines, int at, const QString &text)
{
    QString out;
    for (int i = 0; i < lines.size(); ++i)
        out += (i == at ? text : lines[i].text) + lines[i].eol;
    return out;
}

QString logseqItem(const QString &name, bool hash)
{
    if (name.contains(u',') || (hash && name.contains(u' ')))
        return QString((hash ? u"#[["_s : u"[["_s) + name + u"]]"_s);
    if (hash)
        return QString(u'#' + name);
    return name;
}

/// The value text of a Logseq line holding `t` (`old`: the line now, nullptr for a new one).
QString logseqValueText(const Intended &t, const LogseqLine *old)
{
    if (t.empty && !isListType(t.type))
        return {};
    switch (t.type) {
    case PropertyType::Text: return oneLine(t.scalar.toString());
    case PropertyType::Number: return numberText(t.scalar);
    case PropertyType::Checkbox: return t.scalar.toBool() ? u"true"_s : u"false"_s;
    case PropertyType::Date:
        return old && linkedDate(old->raw) ? QString(u"[["_s + t.scalar.toString() + u"]]"_s) : t.scalar.toString();
    case PropertyType::DateTime: return t.scalar.toString();
    case PropertyType::List: {
        // A Logseq list is links and tags (parsePropertyValue()): those stay as written, any other
        // item becomes a link — also an old text's parts ("a, b" as a list: `[[a]], [[b]]`).
        QStringList out;
        for (const QString &item : t.items)
            out.append(!refName(item).isEmpty() ? item : QString(u"[["_s + item.trimmed() + u"]]"_s));
        return out.join(u", "_s);
    }
    case PropertyType::Tags:
    case PropertyType::Aliases: {
        QHash<QString, QString> oldByName; // name -> part as written
        bool hash = false;
        if (old) {
            for (const QString &part : splitTopLevelCommas(old->raw)) {
                const QString p = part.trimmed();
                hash = hash || p.startsWith(u'#');
                const QString name = cleanName(p);
                if (!name.isEmpty() && !oldByName.contains(name))
                    oldByName.insert(name, p);
            }
        }
        QStringList out;
        for (const QString &item : t.items)
            out.append(oldByName.value(item, logseqItem(item, hash)));
        return out.join(u", "_s);
    }
    default: return {};
    }
}

/// The Logseq line `old` holds `t` already, as its own type reads it (`ilosc:: 1.50` is 1.5,
/// `done:: TRUE` is true): a value committed unchanged keeps its form.
bool logseqHolds(const LogseqLine &old, const Intended &t)
{
    if (t.empty)
        return old.raw.isEmpty();
    switch (t.type) {
    case PropertyType::Number: {
        const auto n = strictNumber(old.raw);
        return n && numberText(*n) == numberText(t.scalar);
    }
    case PropertyType::Checkbox:
        return old.raw.compare(t.scalar.toBool() ? u"true" : u"false", Qt::CaseInsensitive) == 0;
    default: return false;
    }
}

/// The (key, raw value) pairs of the Logseq block, for verification.
QList<std::pair<QString, QString>> logseqPairs(const QList<LogseqLine> &lines)
{
    QList<std::pair<QString, QString>> out;
    for (const LogseqLine &l : lines)
        out.append({l.key, l.raw});
    return out;
}

/// `edited` (an edit of `before`) holds the Logseq lines `expected`, and its frontmatter is the
/// one `before` had (none stays none: a body starting with `---` must not become one).
bool verifyLogseq(const QString &before, const QString &edited, const QList<std::pair<QString, QString>> &expected)
{
    const auto fmBefore = findFrontmatter(before);
    const auto fmAfter = findFrontmatter(edited);
    if (fmBefore.has_value() != fmAfter.has_value()
        || (fmBefore && (fmBefore->yaml != fmAfter->yaml || fmBefore->endLine != fmAfter->endLine)))
        return false;
    const QList<text::Line> lines = text::splitLines(edited);
    return logseqPairs(logseqLines(edited, lines, bodyStartLine(edited))) == expected;
}

PropertyEditResult unchanged(const QString &content, PropertyEditStatus status = PropertyEditStatus::Unchanged)
{
    return {content, status};
}

PropertyEditResult changed(const QString &content)
{
    return {content, PropertyEditStatus::Changed};
}

PropertyEditResult setLogseq(const QString &content, const PageRead &page, const QString &key, const QVariant &value,
                             PropertyType type)
{
    if (!isLogseqKey(key))
        return unchanged(content, PropertyEditStatus::InvalidKey);
    if (page.logseq.isEmpty())
        return unchanged(content, PropertyEditStatus::NotFound);
    qsizetype found = -1;
    for (qsizetype k = 0; k < page.logseq.size(); ++k) {
        if (sameName(page.logseq[k].key, key)) {
            found = k;
            break;
        }
    }
    const LogseqLine *old = found >= 0 ? &page.logseq[found] : nullptr;
    if (!old && pageHasKey(page.props, key))
        return unchanged(content, PropertyEditStatus::KeyExists);
    const QVariant oldValue = old ? QVariant(old->raw) : QVariant();
    const auto t = intendedValue(value, type, oldValue);
    if (!t)
        return unchanged(content, PropertyEditStatus::InvalidValue);
    const QString valueText = logseqValueText(*t, old);
    QList<std::pair<QString, QString>> expected = logseqPairs(page.logseq);
    QString edited;
    if (old) {
        if (old->raw == valueText || logseqHolds(*old, *t))
            return unchanged(content);
        const QStringView line = page.lines[old->line].text;
        QString text;
        if (valueText.isEmpty())
            text = line.first(old->afterColons) + line.sliced(old->valueTo);
        else if (old->valueFrom == old->valueTo)
            text = line.first(old->afterColons) + u' ' + valueText + line.sliced(old->afterColons);
        else
            text = line.first(old->valueFrom) + valueText + line.sliced(old->valueTo);
        edited = replaceLine(page.lines, old->line, text);
        expected[found].second = valueText;
    } else {
        const int last = page.logseq.last().line;
        QList<text::Line> lines = page.lines;
        const QString newLine = key + u"::"_s + (valueText.isEmpty() ? QString() : QString(u' ' + valueText));
        QString out;
        for (int i = 0; i < lines.size(); ++i) {
            out += lines[i].text;
            if (i == last && lines[i].eol.isEmpty())
                out += text::detectEol(content) + newLine;
            else
                out += lines[i].eol;
            if (i == last && !lines[i].eol.isEmpty())
                out += newLine + lines[i].eol;
        }
        edited = out;
        expected.append({key, valueText});
    }
    if (!verifyLogseq(content, edited, expected))
        return unchanged(content, PropertyEditStatus::Unsafe);
    if (t->type == PropertyType::List && !t->empty) {
        // A list must read back as one (an item like `a]]b` cannot be a link).
        const PageRead after = readPage(edited);
        const PageProperty *p = after.props.find(PropertySource::Logseq, key);
        if (!p || p->type != PropertyType::List || p->items.size() != t->items.size())
            return unchanged(content, PropertyEditStatus::InvalidValue);
    }
    return changed(edited);
}

PropertyEditResult removeLogseq(const QString &content, const PageRead &page, const QString &key)
{
    QSet<int> drop;
    QList<std::pair<QString, QString>> expected;
    for (const LogseqLine &l : page.logseq) {
        if (sameName(l.key, key))
            drop.insert(l.line);
        else
            expected.append({l.key, l.raw});
    }
    if (drop.isEmpty())
        return unchanged(content, PropertyEditStatus::NotFound);
    // The lines without `dropped`; `blank`: the first dropped line stays as an empty line.
    const auto without = [&](const QSet<int> &dropped, bool blank) {
        QString out;
        int lastKept = -1;
        const int firstDropped = *std::min_element(dropped.begin(), dropped.end());
        for (int i = 0; i < page.lines.size(); ++i) {
            if (blank && i == firstDropped) {
                out += page.lines[i].eol;
                lastKept = i;
                continue;
            }
            if (dropped.contains(i))
                continue;
            out += page.lines[i].text + page.lines[i].eol;
            lastKept = i;
        }
        // The document ended inside the dropped lines without a final line break: so does the
        // result. (A trailing terminator gives a last empty line: dropping that one changes nothing.)
        const bool endedWithout = !page.lines.isEmpty() && page.lines.last().eol.isEmpty()
                                  && !page.lines.last().text.isEmpty()
                                  && dropped.contains(int(page.lines.size()) - 1);
        if (endedWithout && lastKept >= 0)
            out.chop(page.lines[lastKept].eol.size());
        if (startsWithBom(content) && !startsWithBom(out))
            out.prepend(QChar(ByteOrderMark));
        return out;
    };
    QList<QString> tries;
    if (expected.isEmpty()) {
        // The block goes with the blank lines after it: the page gets back its old start. Unless
        // the body would then read differently (it starts with `---` and would become a
        // frontmatter): then the blank lines stay, else an empty line stands for the block.
        QSet<int> withBlanks = drop;
        int j = page.logseq.last().line + 1;
        while (j < page.lines.size() && isBlank(page.lines[j].text))
            withBlanks.insert(j++);
        tries = {without(withBlanks, false), without(drop, false), without(drop, true)};
    } else {
        tries = {without(drop, false)};
    }
    for (const QString &out : std::as_const(tries)) {
        if (verifyLogseq(content, out, expected))
            return changed(out);
    }
    return unchanged(content, PropertyEditStatus::Unsafe);
}

PropertyEditResult renameLogseq(const QString &content, const PageRead &page, const QString &oldKey,
                                const QString &newKey)
{
    if (!isLogseqKey(newKey))
        return unchanged(content, PropertyEditStatus::InvalidKey);
    qsizetype found = -1;
    for (qsizetype k = 0; k < page.logseq.size(); ++k) {
        if (sameName(page.logseq[k].key, oldKey)) {
            found = k;
            break;
        }
    }
    if (found < 0)
        return unchanged(content, PropertyEditStatus::NotFound);
    const LogseqLine &l = page.logseq[found];
    if (l.key == newKey)
        return unchanged(content);
    if (!sameName(newKey, oldKey) && pageHasKey(page.props, newKey, PropertySource::Logseq, oldKey))
        return unchanged(content, PropertyEditStatus::KeyExists);
    const QStringView line = page.lines[l.line].text;
    const QString text = line.first(l.keyFrom) + newKey + line.sliced(l.keyFrom + l.key.size());
    const QString edited = replaceLine(page.lines, l.line, text);
    QList<std::pair<QString, QString>> expected = logseqPairs(page.logseq);
    expected[found].first = newKey;
    if (!verifyLogseq(content, edited, expected))
        return unchanged(content, PropertyEditStatus::Unsafe);
    return changed(edited);
}

const PageProperty *holderOf(const PageProperties &props, PropertySource source, PropertyType kind)
{
    // The plural key first (frontmatterGet()'s order), then the singular one.
    const PageProperty *singular = nullptr;
    for (const PageProperty &p : props.items) {
        if (p.source != source || fixedPropertyType(p.key) != kind)
            continue;
        const bool plural = p.key.compare(kind == PropertyType::Tags ? u"tags" : u"aliases", Qt::CaseInsensitive) == 0;
        if (plural)
            return &p;
        if (!singular)
            singular = &p;
    }
    return singular;
}

} // namespace

// ---- names --------------------------------------------------------------------------------------

QString propertyTypeName(PropertyType t)
{
    switch (t) {
    case PropertyType::Text: return u"text"_s;
    case PropertyType::Number: return u"number"_s;
    case PropertyType::Checkbox: return u"checkbox"_s;
    case PropertyType::Date: return u"date"_s;
    case PropertyType::DateTime: return u"datetime"_s;
    case PropertyType::List: return u"list"_s;
    case PropertyType::Tags: return u"tags"_s;
    case PropertyType::Aliases: return u"aliases"_s;
    case PropertyType::Unsupported: break;
    }
    return u"unsupported"_s;
}

std::optional<PropertyType> parsePropertyType(QStringView name)
{
    for (const PropertyType t :
         {PropertyType::Text, PropertyType::Number, PropertyType::Checkbox, PropertyType::Date, PropertyType::DateTime,
          PropertyType::List, PropertyType::Tags, PropertyType::Aliases, PropertyType::Unsupported}) {
        if (name.compare(propertyTypeName(t), Qt::CaseInsensitive) == 0)
            return t;
    }
    return std::nullopt;
}

QString propertySourceName(PropertySource s)
{
    return s == PropertySource::Logseq ? u"logseq"_s : u"yaml"_s;
}

std::optional<PropertySource> parsePropertySource(QStringView name)
{
    if (name.compare(u"yaml", Qt::CaseInsensitive) == 0)
        return PropertySource::Yaml;
    if (name.compare(u"logseq", Qt::CaseInsensitive) == 0)
        return PropertySource::Logseq;
    return std::nullopt;
}

QString propertyEditStatusName(PropertyEditStatus s)
{
    switch (s) {
    case PropertyEditStatus::Changed: return u"changed"_s;
    case PropertyEditStatus::Unchanged: return u"unchanged"_s;
    case PropertyEditStatus::NotFound: return u"notFound"_s;
    case PropertyEditStatus::Unsafe: return u"unsafe"_s;
    case PropertyEditStatus::InvalidKey: return u"invalidKey"_s;
    case PropertyEditStatus::KeyExists: return u"keyExists"_s;
    case PropertyEditStatus::Reserved: return u"reserved"_s;
    case PropertyEditStatus::InvalidValue: return u"invalidValue"_s;
    }
    return {};
}

std::optional<PropertyType> fixedPropertyType(QStringView key)
{
    const QStringView k = key.trimmed();
    if (k.compare(u"tags", Qt::CaseInsensitive) == 0 || k.compare(u"tag", Qt::CaseInsensitive) == 0)
        return PropertyType::Tags;
    if (k.compare(u"aliases", Qt::CaseInsensitive) == 0 || k.compare(u"alias", Qt::CaseInsensitive) == 0)
        return PropertyType::Aliases;
    return std::nullopt;
}

// ---- types --------------------------------------------------------------------------------------

PropertyType inferPropertyType(const QString &key, const QVariant &value, PropertySource source, PropertyType empty)
{
    const auto fixed = fixedPropertyType(key);
    if (source == PropertySource::Yaml) {
        if (isMapVariant(value))
            return PropertyType::Unsupported;
        if (isListVariant(value)) {
            for (const QVariant &item : value.toList()) {
                if (isMapVariant(item) || (isListVariant(item) && !yamlItemText(item)))
                    return PropertyType::Unsupported;
            }
            return fixed.value_or(PropertyType::List);
        }
        if (fixed)
            return *fixed;
        if (isEmptyVariant(value))
            return empty;
        if (value.typeId() == QMetaType::Bool)
            return PropertyType::Checkbox;
        if (isNumberVariant(value))
            return PropertyType::Number;
        if (value.typeId() == QMetaType::QString) {
            if (const auto d = parseDateText(value.toString()))
                return d->hasTime ? PropertyType::DateTime : PropertyType::Date;
        }
        return PropertyType::Text;
    }
    if (fixed)
        return *fixed;
    if (isListVariant(value))
        return PropertyType::List;
    const QString raw = isEmptyVariant(value) ? QString() : value.toString().trimmed();
    if (raw.isEmpty())
        return empty;
    if (raw.compare(u"true", Qt::CaseInsensitive) == 0 || raw.compare(u"false", Qt::CaseInsensitive) == 0)
        return PropertyType::Checkbox;
    if (strictNumber(raw))
        return PropertyType::Number;
    if (linkedDate(raw))
        return PropertyType::Date;
    if (const auto d = parseDateText(raw))
        return d->hasTime ? PropertyType::DateTime : PropertyType::Date;
    if (parsePropertyValue(key.toLower(), raw).typeId() == QMetaType::QStringList)
        return PropertyType::List;
    return PropertyType::Text;
}

QStringList propertyValueTexts(const QString &key, const QVariant &value, PropertySource source)
{
    const PropertyType type = inferPropertyType(key, value, source, PropertyType::Text);
    if (isEmptyVariant(value) || type == PropertyType::Unsupported)
        return {};
    if (source == PropertySource::Logseq) {
        if (isListVariant(value) || type == PropertyType::Tags || type == PropertyType::Aliases) {
            QStringList out = isListVariant(value) ? value.toStringList()
                                                   : parsePropertyValue(key.toLower(), value.toString()).toStringList();
            out.removeAll(QString());
            return out;
        }
        const QString raw = value.toString().trimmed();
        return raw.isEmpty() ? QStringList() : QStringList{raw};
    }
    switch (type) {
    case PropertyType::List: return yamlListItems(value);
    case PropertyType::Tags:
    case PropertyType::Aliases: return yamlNames(value, type);
    case PropertyType::Number: return {numberText(value)};
    case PropertyType::Checkbox: return {value.toBool() ? u"true"_s : u"false"_s};
    default: {
        const QString s = value.toString().trimmed();
        return s.isEmpty() ? QStringList() : QStringList{s};
    }
    }
}

// ---- reading ------------------------------------------------------------------------------------

PropertySource PageProperties::addTarget() const
{
    return !yaml && logseq ? PropertySource::Logseq : PropertySource::Yaml;
}

bool PageProperties::canAdd() const
{
    if (addTarget() == PropertySource::Logseq)
        return logseq->editable;
    return !yaml || yaml->editable;
}

const PageProperty *PageProperties::find(PropertySource source, const QString &key) const
{
    for (const PageProperty &p : items) {
        if (p.source != source)
            continue;
        if (source == PropertySource::Yaml ? p.key == key : sameName(p.key, key))
            return &p;
    }
    return nullptr;
}

PageProperties readPageProperties(QStringView content)
{
    return readPage(content).props;
}

QStringList pageListItems(const PageProperties &props, PropertyType kind)
{
    QStringList out;
    for (const PageProperty &p : props.items) {
        if (fixedPropertyType(p.key) == kind && p.type == kind)
            out.append(p.items);
    }
    return uniqueCaseInsensitive(out);
}

// ---- edits --------------------------------------------------------------------------------------

QString checkPropertyKey(QStringView key, PropertySource source)
{
    if (key.trimmed().isEmpty())
        return u"empty"_s;
    if (key.contains(u'\n') || key.contains(u'\r'))
        return u"lineBreak"_s;
    if (source == PropertySource::Logseq)
        return isLogseqKey(key) ? QString() : u"logseqKey"_s;
    if (isReservedKey(key))
        return u"reserved"_s;
    return {};
}

QString cleanListItem(PropertyType kind, const QString &item)
{
    if (item.contains(u'\n') || item.contains(u'\r'))
        return {};
    QString s = item.trimmed();
    if (kind == PropertyType::Aliases) {
        if (s.contains(u"[[") || s.contains(u"]]") || s.contains(u'|') || s.contains(u'#') || s.contains(u'^'))
            return {};
        return s;
    }
    if (kind == PropertyType::Tags)
        return cleanName(s);
    return s;
}

PropertyEditResult setPageProperty(const QString &content, PropertySource source, const QString &key,
                                   const QVariant &value, PropertyType type)
{
    const QString keyProblem = checkPropertyKey(key, source);
    if (keyProblem == u"reserved")
        return unchanged(content, PropertyEditStatus::Reserved);
    if (!keyProblem.isEmpty())
        return unchanged(content, PropertyEditStatus::InvalidKey);
    if (const auto fixed = fixedPropertyType(key))
        type = *fixed; // tags and aliases are lists of names whatever the caller says
    const PageRead page = readPage(content);
    if (source == PropertySource::Logseq)
        return setLogseq(content, page, key, value, type);

    if (!page.yaml) {
        if (pageHasKey(page.props, key))
            return unchanged(content, PropertyEditStatus::KeyExists);
        const auto t = intendedValue(value, type, QVariant());
        if (!t)
            return unchanged(content, PropertyEditStatus::InvalidValue);
        const auto out = createFrontmatter(content, key, *t);
        return out ? changed(*out) : unchanged(content, PropertyEditStatus::Unsafe);
    }
    const YamlDoc &doc = *page.yaml;
    if (!doc.problem.isEmpty())
        return unchanged(content, PropertyEditStatus::Unsafe);
    const QList<FrontmatterEntry> entries = entriesOf(doc, key);
    if (entries.isEmpty() && pageHasKey(page.props, key))
        return unchanged(content, PropertyEditStatus::KeyExists);
    const QVariant old = doc.data.value(key);
    const auto t = intendedValue(value, type, old);
    if (!t)
        return unchanged(content, PropertyEditStatus::InvalidValue);
    if (entries.size() == 1 && sameTrimmedText(old, *t))
        return unchanged(content);
    if (entries.size() == 1 && yamlReadsAs(old, *t)) {
        // Already so; a list keeps a form that reads the same (`tags: a, b`) — but an empty list
        // of a type the key has nowhere else (`key:` set as a list) is written as `key: []`.
        const bool emptyList = t->type == PropertyType::List && t->empty && !isListVariant(old);
        if (!emptyList)
            return unchanged(content);
    }
    const auto out = writeYamlEntry(content, doc, key, *t);
    if (!out)
        return unchanged(content, PropertyEditStatus::Unsafe);
    return *out == content ? unchanged(content) : changed(*out);
}

PropertyEditResult addPageProperty(const QString &content, PropertySource source, const QString &key,
                                   const QVariant &value, PropertyType type)
{
    const QString keyProblem = checkPropertyKey(key, source);
    if (keyProblem == u"reserved")
        return unchanged(content, PropertyEditStatus::Reserved);
    if (!keyProblem.isEmpty())
        return unchanged(content, PropertyEditStatus::InvalidKey);
    if (pageHasKey(readPageProperties(content), key))
        return unchanged(content, PropertyEditStatus::KeyExists);
    return setPageProperty(content, source, key, value, type);
}

PropertyEditResult removePageProperty(const QString &content, PropertySource source, const QString &key)
{
    const PageRead page = readPage(content);
    if (source == PropertySource::Logseq)
        return removeLogseq(content, page, key);
    if (isReservedKey(key))
        return unchanged(content, PropertyEditStatus::Reserved);
    if (!page.yaml)
        return unchanged(content, PropertyEditStatus::NotFound);
    const YamlDoc &doc = *page.yaml;
    const QList<FrontmatterEntry> entries = entriesOf(doc, key);
    if (entries.isEmpty())
        return unchanged(content, doc.problem.isEmpty() ? PropertyEditStatus::NotFound : PropertyEditStatus::Unsafe);
    if (!doc.problem.isEmpty())
        return unchanged(content, PropertyEditStatus::Unsafe);
    const auto absent = [&](const QVariantMap &data) { return !data.contains(key); };
    bool empty = true;
    for (int i = 1; i < doc.fm.endLine && empty; ++i) {
        const bool removed = std::any_of(entries.begin(), entries.end(),
                                         [i](const FrontmatterEntry &e) { return i >= e.first && i < e.last; });
        empty = removed || isBlank(doc.lines[i].text);
    }
    if (empty) {
        // Nothing but blank lines left: the whole block goes — unless the body would then start a
        // frontmatter of its own (then an empty block stays).
        const QString bom = startsWithBom(content) ? QString(QChar(ByteOrderMark)) : QString();
        const QString whole = bom + QStringView(content).sliced(doc.fm.bodyStart);
        if (verifyYaml(content, doc.fm, whole, {key}, absent))
            return changed(whole);
    }
    const QString edited = rebuildLines(doc.lines, entries, -1, QString());
    if (!verifyYaml(content, doc.fm, edited, {key}, absent))
        return unchanged(content, PropertyEditStatus::Unsafe);
    return changed(edited);
}

PropertyEditResult renamePageProperty(const QString &content, PropertySource source, const QString &oldKey,
                                      const QString &newKey)
{
    const QString keyProblem = checkPropertyKey(newKey, source);
    if (keyProblem == u"reserved" || (source == PropertySource::Yaml && isReservedKey(oldKey)))
        return unchanged(content, PropertyEditStatus::Reserved);
    if (!keyProblem.isEmpty())
        return unchanged(content, PropertyEditStatus::InvalidKey);
    const PageRead page = readPage(content);
    if (source == PropertySource::Logseq)
        return renameLogseq(content, page, oldKey, newKey);
    if (!page.yaml)
        return unchanged(content, PropertyEditStatus::NotFound);
    const YamlDoc &doc = *page.yaml;
    if (!doc.problem.isEmpty())
        return unchanged(content, PropertyEditStatus::Unsafe);
    const QList<FrontmatterEntry> entries = entriesOf(doc, oldKey);
    if (entries.isEmpty())
        return unchanged(content, PropertyEditStatus::NotFound);
    if (newKey == oldKey)
        return unchanged(content);
    if (!sameName(newKey, oldKey) && pageHasKey(page.props, newKey, PropertySource::Yaml, oldKey))
        return unchanged(content, PropertyEditStatus::KeyExists);
    // The entry whose value the page shows (the last of duplicates) keeps its value lines; the
    // other duplicates go.
    const FrontmatterEntry &kept = entries.last();
    const QStringView first = doc.lines[kept.first].text;
    const auto kl = splitYamlKeyLine(first);
    if (!kl)
        return unchanged(content, PropertyEditStatus::Unsafe);
    QString renamed = yamlString(newKey) + first.sliced(kl->colon) + doc.lines[kept.first].eol;
    for (int i = kept.first + 1; i < kept.last; ++i)
        renamed += doc.lines[i].text + doc.lines[i].eol;
    const QVariant value = doc.data.value(oldKey);
    const QString edited = rebuildLines(doc.lines, entries, kept.first, renamed);
    const bool ok = verifyYaml(content, doc.fm, edited, {oldKey, newKey}, [&](const QVariantMap &data) {
        return !data.contains(oldKey) && data.contains(newKey)
               && canonicalYaml(data.value(newKey)) == canonicalYaml(value);
    });
    return ok ? changed(edited) : unchanged(content, PropertyEditStatus::Unsafe);
}

PropertyEditResult addPageListItem(const QString &content, PropertyType kind, const QString &item)
{
    if (kind != PropertyType::Tags && kind != PropertyType::Aliases)
        return unchanged(content, PropertyEditStatus::InvalidValue);
    const QString name = cleanListItem(kind, item);
    if (name.isEmpty())
        return unchanged(content, PropertyEditStatus::InvalidValue);
    const PageProperties props = readPageProperties(content);
    for (const QString &have : pageListItems(props, kind)) {
        if (sameName(have, name))
            return unchanged(content);
    }
    const PageProperty *holder = holderOf(props, PropertySource::Yaml, kind);
    if (!holder)
        holder = holderOf(props, PropertySource::Logseq, kind);
    if (holder) {
        if (!holder->editable || holder->type != kind)
            return unchanged(content, PropertyEditStatus::Unsafe);
        QStringList items = holder->items;
        items.append(name);
        return setPageProperty(content, holder->source, holder->key, items, kind);
    }
    if (!props.canAdd())
        return unchanged(content, PropertyEditStatus::Unsafe);
    return setPageProperty(content, props.addTarget(), kind == PropertyType::Tags ? u"tags"_s : u"aliases"_s,
                           QStringList{name}, kind);
}

PropertyEditResult removePageListItem(const QString &content, PropertyType kind, const QString &item)
{
    if (kind != PropertyType::Tags && kind != PropertyType::Aliases)
        return unchanged(content, PropertyEditStatus::NotFound);
    const QString name = kind == PropertyType::Tags ? cleanName(item) : item.trimmed();
    if (name.isEmpty())
        return unchanged(content, PropertyEditStatus::NotFound);
    QString current = content;
    bool any = false;
    // Each holder in turn (lines move after an edit: read again every time).
    QSet<QString> done;
    for (int round = 0; round < 16; ++round) {
        const PageProperties props = readPageProperties(current);
        const PageProperty *holder = nullptr;
        for (const PageProperty &p : props.items) {
            const QString id = propertySourceName(p.source) + u':' + p.key;
            if (done.contains(id) || fixedPropertyType(p.key) != kind || p.type != kind)
                continue;
            if (std::any_of(p.items.begin(), p.items.end(), [&](const QString &s) { return sameName(s, name); })) {
                holder = &p;
                done.insert(id);
                break;
            }
        }
        if (!holder)
            break;
        if (!holder->editable)
            return unchanged(content, PropertyEditStatus::Unsafe);
        QStringList items;
        for (const QString &s : holder->items) {
            if (!sameName(s, name))
                items.append(s);
        }
        const PropertyEditResult r = items.isEmpty()
                                             ? removePageProperty(current, holder->source, holder->key)
                                             : setPageProperty(current, holder->source, holder->key, items, kind);
        if (r.status != PropertyEditStatus::Changed)
            return unchanged(content,
                             r.status == PropertyEditStatus::Unchanged ? PropertyEditStatus::Unsafe : r.status);
        current = r.content;
        any = true;
    }
    return any ? changed(current) : unchanged(content, PropertyEditStatus::NotFound);
}

// ---- conversions --------------------------------------------------------------------------------

std::optional<QVariant> convertPropertyValue(const QVariant &value, PropertyType from, PropertyType to)
{
    const bool listFrom = isListType(from);
    if (isEmptyVariant(value) || (listFrom && value.toStringList().isEmpty())) {
        if (isListType(to))
            return QVariant(QStringList());
        return QVariant();
    }
    if (from == to)
        return value;
    // Everything as text first (lists joined), then into the target type.
    QString text;
    QStringList items;
    if (listFrom) {
        items = value.toStringList();
        text = items.join(u", "_s);
    } else if (from == PropertyType::Checkbox) {
        text = value.toBool() ? u"true"_s : u"false"_s;
    } else if (from == PropertyType::Number) {
        text = numberText(value);
    } else {
        text = value.toString();
    }
    switch (to) {
    case PropertyType::Text: return QVariant(text);
    case PropertyType::Number: {
        if (from == PropertyType::Checkbox || listFrom)
            return std::nullopt;
        if (const auto n = parseNumberInput(text))
            return *n;
        return std::nullopt;
    }
    case PropertyType::Checkbox: {
        const QString t = text.trimmed().toLower();
        if (t == u"true" || t == u"tak" || t == u"yes" || t == u"1")
            return QVariant(true);
        if (t == u"false" || t == u"nie" || t == u"no" || t == u"0")
            return QVariant(false);
        return std::nullopt;
    }
    case PropertyType::Date: {
        const auto d = parseDateText(text.trimmed());
        if (!d)
            return std::nullopt;
        return QVariant(d->date.toString(u"yyyy-MM-dd"_s));
    }
    case PropertyType::DateTime: {
        auto d = parseDateText(text.trimmed());
        if (!d)
            return std::nullopt;
        if (!d->hasTime) {
            d->hasTime = true;
            d->time = QTime(0, 0);
        }
        return QVariant(formatDateText(*d));
    }
    case PropertyType::List:
    case PropertyType::Tags:
    case PropertyType::Aliases: {
        if (!listFrom) {
            for (const QString &part : splitTopLevelCommas(text)) {
                const QString p = part.trimmed();
                if (!p.isEmpty())
                    items.append(p);
            }
        }
        if (to != PropertyType::List) {
            QStringList names;
            for (const QString &s : std::as_const(items)) {
                const QString n = cleanListItem(to, s);
                if (n.isEmpty())
                    return std::nullopt;
                names.append(n);
            }
            items = names;
        }
        return QVariant(uniqueCaseInsensitive(items));
    }
    case PropertyType::Unsupported: break;
    }
    return std::nullopt;
}

std::optional<QVariant> parseNumberInput(QStringView text)
{
    QString s;
    s.reserve(text.size());
    for (const QChar c : text.trimmed()) {
        const char16_t u = c.unicode();
        if (u == u' ' || u == 0x00a0 || u == 0x2009 || u == 0x202f || u == 0x2007 || u == u'\t' || u == u'\'')
            continue; // thousands separators
        s += u == 0x2212 ? QChar(u'-') : c;
    }
    if (s.isEmpty())
        return std::nullopt;
    const qsizetype lastDot = s.lastIndexOf(u'.');
    const qsizetype lastComma = s.lastIndexOf(u',');
    if (lastDot >= 0 && lastComma >= 0) {
        // Both: the last one is the decimal mark, the other one groups thousands.
        const QChar decimal = lastDot > lastComma ? u'.' : u',';
        const QChar group = decimal == u'.' ? u',' : u'.';
        if (s.count(decimal) != 1)
            return std::nullopt;
        s.remove(group);
        s.replace(decimal, u'.');
    } else if (lastComma >= 0) {
        if (s.count(u',') != 1)
            return std::nullopt;
        s.replace(u',', u'.');
    } else if (lastDot >= 0 && s.count(u'.') != 1) {
        return std::nullopt;
    }
    static const QRegularExpression re(u"^[-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][-+]?\\d+)?$"_s);
    if (!re.match(s).hasMatch())
        return std::nullopt;
    const bool floating = s.contains(u'.') || s.contains(u'e') || s.contains(u'E');
    if (!floating) {
        bool ok = false;
        const qlonglong n = s.toLongLong(&ok);
        if (ok && n <= MaxSafeInteger && n >= -MaxSafeInteger)
            return QVariant(n);
    }
    bool ok = false;
    const double d = s.toDouble(&ok);
    if (!ok || !std::isfinite(d))
        return std::nullopt;
    if (d == std::trunc(d) && std::fabs(d) <= double(MaxSafeInteger) && floating)
        return QVariant(qlonglong(d));
    return QVariant(d);
}

} // namespace brulion::markdown
