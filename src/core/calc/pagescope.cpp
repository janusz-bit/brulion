// Page-level calc scope: variables from all calc blocks of a page plus numeric page/block
// properties, used by inline calculations and table formulas. Fence and property detection is
// deliberately simple and self-contained (no dependency on the markdown module).
#include "calc/calc.h"

#include "calc/blocks_p.h"
#include "calc/util_p.h"

#include "text/text.h"

#include <QMutex>
#include <QMutexLocker>

#include <climits>
#include <memory>

namespace brulion::calc {

namespace {

struct OpenBlock {
    FenceOpen fence;
    int start = 0;
    QStringList lines;
    bool isCalc() const { return fence.lang == u"calc"; }
};

QStringList lineTexts(QStringView content)
{
    const QList<text::Line> lines = text::splitLines(content);
    QStringList out;
    out.reserve(lines.size());
    for (const text::Line &line : lines)
        out.append(line.text);
    return out;
}

/// Walks `lines` from `from` through code fences: `calcBlock(start, end, source, closed)` for every
/// calc fence (an unclosed one ends at the last line), `outside(line, text)` for each line outside
/// fences.
template<typename CalcBlockFn, typename OutsideFn>
void scanFences(const QStringList &lines, qsizetype from, CalcBlockFn calcBlock, OutsideFn outside)
{
    std::optional<OpenBlock> open;
    for (qsizetype i = from; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (!open) {
            if (const std::optional<FenceOpen> f = parseFenceOpen(line))
                open = OpenBlock{*f, static_cast<int>(i), {}};
            else
                outside(static_cast<int>(i), line);
        } else if (isFenceClose(line, open->fence)) {
            if (open->isCalc())
                calcBlock(open->start, static_cast<int>(i), open->lines.join(u'\n'), true);
            open.reset();
        } else if (open->isCalc()) {
            open->lines.append(dedent(line, open->fence.indent).toString());
        }
    }
    if (open && open->isCalc())
        calcBlock(open->start, static_cast<int>(lines.size() - 1), open->lines.join(u'\n'), false);
}

struct NumericProperty {
    QString name;
    Value value;
};

/// A numeric variable from a property (`12,50 zł`, `23%`, `"7"`), or nullopt.
std::optional<NumericProperty> propertyVariable(QStringView key, QStringView raw, CalcLocale locale)
{
    const QString name = detail::isIdentifier(key) ? detail::toNfc(key) : detail::sanitizeIdentifier(key);
    if (name.isEmpty())
        return std::nullopt;
    QStringView text = raw.trimmed();
    if (text.size() >= 2 && (text.front() == u'"' || text.front() == u'\'') && text.back() == text.front())
        text = text.mid(1, text.size() - 2);
    const std::optional<ParsedNumber> parsed = parseLocaleNumber(text, locale);
    if (!parsed)
        return std::nullopt;
    Value v = parsed->percent ? Value::percent(parsed->value) : Value::number(parsed->value);
    if (parsed->currency.isValid())
        v.setCurrency(parsed->currency);
    return NumericProperty{name, std::move(v)};
}

/// `key: value` of a YAML frontmatter line: `^([^\s:#][^:]*):\s+(.+)$`.
std::optional<std::pair<QStringView, QStringView>> frontmatterEntry(QStringView line)
{
    if (line.isEmpty() || line.front().isSpace() || line.front() == u':' || line.front() == u'#')
        return std::nullopt;
    const qsizetype colon = line.indexOf(u':');
    if (colon < 0 || colon + 1 >= line.size() || !line.at(colon + 1).isSpace())
        return std::nullopt;
    const QStringView value = line.mid(colon + 1).trimmed();
    if (value.isEmpty())
        return std::nullopt;
    return std::make_pair(line.left(colon).trimmed(), value);
}

/// `^(---|\.\.\.)\s*$`
bool isFrontmatterEnd(QStringView line)
{
    if (!line.startsWith(u"---") && !line.startsWith(u"..."))
        return false;
    return line.mid(3).trimmed().isEmpty();
}

/// `key:: value` (optionally after a list marker): `^\s*(?:[-*+]|\d+[.)])?\s*([\p{L}\p{N}_-]+)::\s*(.+?)\s*$`.
std::optional<std::pair<QStringView, QStringView>> propertyLine(QStringView line)
{
    qsizetype start = 0;
    const qsizetype item = listItemPrefixLength(line);
    if (item > 0) {
        start = item;
    } else {
        while (start < line.size() && line.at(start).isSpace())
            ++start;
    }
    qsizetype end = start;
    while (end < line.size()) {
        const QChar c = line.at(end);
        if (!(c.isLetterOrNumber() || c.isMark() || c == u'_' || c == u'-'))
            break;
        ++end;
    }
    if (end == start || !line.mid(end).startsWith(u"::"))
        return std::nullopt;
    const QStringView value = line.mid(end + 2).trimmed();
    if (value.isEmpty())
        return std::nullopt;
    return std::make_pair(line.mid(start, end - start), value);
}

} // namespace

// --- findCalcBlocks ----------------------------------------------------------------------------

QList<CalcBlock> findCalcBlocks(QStringView content)
{
    QList<CalcBlock> blocks;
    scanFences(
            lineTexts(content), 0,
            [&blocks](int start, int end, QString source, bool closed) {
                blocks.append({start, end, start + 1, std::move(source), closed});
            },
            [](int, QStringView) {});
    return blocks;
}

// --- PageCalc ----------------------------------------------------------------------------------

PageCalc::PageCalc(QStringView content, CalcLocale locale)
{
    const QStringList lines = lineTexts(content);
    Scope scope;
    WorkBudget budget;
    QHash<QString, Value> recorded;
    QHash<QString, Value> aliases; // lower-case aliases of property keys and their values

    const auto record = [&](int line, const QStringList &names) {
        Event event;
        event.line = line;
        for (const QString &name : names) {
            const Value *v = scope.find(name);
            if (!v)
                continue;
            const auto it = recorded.constFind(name);
            if (it != recorded.constEnd() && *it == *v)
                continue;
            recorded.insert(name, *v);
            event.changes.append({name, *v});
        }
        if (!event.changes.isEmpty())
            m_events.append(std::move(event));
    };

    // Frontmatter (only when terminated).
    qsizetype bodyStart = 0;
    if (!lines.isEmpty() && lines.front().trimmed() == u"---") {
        qsizetype end = 1;
        while (end < lines.size() && !isFrontmatterEnd(lines.at(end)))
            ++end;
        if (end < lines.size()) {
            for (qsizetype i = 1; i < end; ++i) {
                if (const auto entry = frontmatterEntry(lines.at(i))) {
                    if (const auto v = propertyVariable(entry->first, entry->second, locale))
                        scope.set(v->name, v->value);
                }
            }
            bodyStart = end + 1;
            record(-1, scope.names());
        }
    }

    const auto setProperty = [&](QStringView key, QStringView raw) -> QStringList {
        const std::optional<NumericProperty> v = propertyVariable(key, raw, locale);
        if (!v)
            return {};
        // Property keys are lower-cased by the dialect (SYNTAX §4): "Cena:: 5" also defines "cena"
        // unless something else does.
        scope.set(v->name, v->value);
        aliases.remove(v->name);
        const QString lower = v->name.toLower();
        if (lower == v->name)
            return {v->name};
        const auto alias = aliases.constFind(lower);
        if (!scope.contains(lower) || (alias != aliases.constEnd() && scope.value(lower) == *alias)) {
            scope.set(lower, v->value);
            aliases.insert(lower, v->value);
            return {v->name, lower};
        }
        return {v->name};
    };

    // A calc block's changes: only the names it assigned (a page may have thousands of variables).
    const auto evaluateBlock = [&](const QString &source) -> QStringList {
        QSet<QString> assigned;
        evaluateCalcInto(source, scope, locale, budget, &assigned);
        return QStringList(assigned.cbegin(), assigned.cend());
    };

    scanFences(
            lines, bodyStart,
            [&](int, int end, const QString &source, bool closed) {
                record(closed ? end : INT_MAX, evaluateBlock(source));
            },
            [&](int i, QStringView line) {
                if (const auto prop = propertyLine(line))
                    record(i, setProperty(prop->first, prop->second));
            });
}

Scope PageCalc::scopeAt(int uptoLine) const
{
    Scope scope;
    for (const Event &event : m_events) {
        if (uptoLine >= 0 && event.line >= uptoLine)
            break;
        for (const Change &change : event.changes)
            scope.set(change.name, change.value);
    }
    return scope;
}

// --- cached pageScope ----------------------------------------------------------------------------

namespace {

struct CacheEntry {
    CalcLocale locale = CalcLocale::Polish;
    QString content;
    std::shared_ptr<const PageCalc> calc;
};

constexpr qsizetype kPageCacheSize = 16;

} // namespace

Scope pageScope(QStringView content, int uptoLine, CalcLocale locale)
{
    static QMutex mutex;
    static QList<CacheEntry> cache; // most recently used first

    std::shared_ptr<const PageCalc> calc;
    {
        const QMutexLocker lock(&mutex);
        for (qsizetype i = 0; i < cache.size(); ++i) {
            if (cache.at(i).locale == locale && cache.at(i).content == content) {
                calc = cache.at(i).calc;
                cache.move(i, 0);
                break;
            }
        }
    }
    if (!calc) {
        calc = std::make_shared<const PageCalc>(content, locale);
        const QMutexLocker lock(&mutex);
        cache.prepend({locale, content.toString(), calc});
        while (cache.size() > kPageCacheSize)
            cache.removeLast();
    }
    return calc->scopeAt(uptoLine);
}

} // namespace brulion::calc
