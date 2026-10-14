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
    QStringList out;
    for (const text::Line &line : text::splitLines(content))
        out.append(line.text);
    return out;
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
    return NumericProperty{name, v};
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
    const QStringList lines = lineTexts(content);
    QList<CalcBlock> blocks;
    std::optional<OpenBlock> open;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (open) {
            if (isFenceClose(line, open->fence)) {
                if (open->isCalc())
                    blocks.append({open->start, static_cast<int>(i), open->start + 1, open->lines.join(u'\n'), true});
                open.reset();
            } else if (open->isCalc()) {
                open->lines.append(dedent(line, open->fence.indent).toString());
            }
            continue;
        }
        if (const std::optional<FenceOpen> f = parseFenceOpen(line))
            open = OpenBlock{*f, static_cast<int>(i), {}};
    }
    if (open && open->isCalc()) {
        blocks.append({open->start, static_cast<int>(lines.size() - 1), open->start + 1, open->lines.join(u'\n'),
                       false});
    }
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
        qsizetype end = -1;
        for (qsizetype i = 1; i < lines.size() && end < 0; ++i) {
            if (isFrontmatterEnd(lines.at(i)))
                end = i;
        }
        if (end > 0) {
            for (qsizetype i = 1; i < end; ++i) {
                const auto entry = frontmatterEntry(lines.at(i));
                if (!entry)
                    continue;
                if (const auto v = propertyVariable(entry->first, entry->second, locale))
                    scope.set(v->name, v->value);
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

    std::optional<OpenBlock> open;
    for (qsizetype i = bodyStart; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (open) {
            if (isFenceClose(line, open->fence)) {
                if (open->isCalc())
                    record(static_cast<int>(i), evaluateBlock(open->lines.join(u'\n')));
                open.reset();
            } else if (open->isCalc()) {
                open->lines.append(dedent(line, open->fence.indent).toString());
            }
            continue;
        }
        if (const std::optional<FenceOpen> f = parseFenceOpen(line)) {
            open = OpenBlock{*f, static_cast<int>(i), {}};
            continue;
        }
        if (const auto prop = propertyLine(line))
            record(static_cast<int>(i), setProperty(prop->first, prop->second));
    }
    if (open && open->isCalc())
        record(INT_MAX, evaluateBlock(open->lines.join(u'\n')));
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
    CalcLocale locale;
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
                CacheEntry entry = cache.takeAt(i);
                calc = entry.calc;
                cache.prepend(std::move(entry));
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
