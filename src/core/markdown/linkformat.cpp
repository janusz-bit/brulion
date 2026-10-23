#include "markdown/linkformat.h"

#include "markdown/inline_p.h"
#include "markdown/lines_p.h"
#include "markdown/tokens.h"
#include "text/text.h"

using namespace Qt::StringLiterals;

namespace brulion::markdown {

using namespace detail;

namespace {

/// Longest fragment uniqueHeadingFragment() gives (a wiki link holds at most 1000 characters).
constexpr qsizetype MaxFragment = 300;

QStringView firstLineOf(QStringView s)
{
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s[i] == u'\n' || s[i] == u'\r')
            return s.first(i);
    }
    return s;
}

/// The heading's text with links, Markdown links and tags as they are shown: a wiki link by its
/// alias (else its target and heading), a Markdown link by its label, a tag by its name.
QString shownText(QStringView line)
{
    const QString masked = maskInline(line);
    QString out;
    out.reserve(line.size());
    qsizetype pos = 0;
    lexInline(line, masked, [&](const InlineItem &item) {
        if (item.from < pos)
            return;
        QString shown;
        switch (item.kind) {
        case InlineKind::Wiki:
        case InlineKind::Embed:
            shown = !item.wiki.display.isEmpty()  ? item.wiki.display
                    : item.wiki.heading.isEmpty() ? item.wiki.target
                                                  : item.wiki.target + u' ' + item.wiki.heading;
            break;
        case InlineKind::Markdown:
        case InlineKind::Image:
            shown = line.sliced(item.md.textFrom, item.md.textTo - item.md.textFrom).toString();
            break;
        case InlineKind::Tag: shown = item.target; break;
        default: return; // block references, URLs: as written
        }
        out += line.sliced(pos, item.from - pos);
        out += shown;
        pos = item.to;
    });
    out += line.sliced(pos);
    return out;
}

/// Characters a heading fragment never holds: they split or end a wiki link (`#`, `|`, `^`, `[`,
/// `]`) or Obsidian drops them from heading links (`:`, `\`).
bool isBreaking(QChar c)
{
    switch (c.unicode()) {
    case u'#':
    case u'|':
    case u'^':
    case u':':
    case u'[':
    case u']':
    case u'\\': return true;
    default: return c.isSpace();
    }
}

/// Keys of every heading (headingKey()), computed once per lookup.
QStringList keysOf(const QList<Heading> &headings)
{
    QStringList keys;
    keys.reserve(headings.size());
    for (const Heading &h : headings)
        keys.append(headingKey(h.text));
    return keys;
}

/// The headings enclosing `headings[index]` (each an earlier heading of a lower level than the
/// one after it), outermost first.
QList<int> enclosingHeadings(const QList<Heading> &headings, int index)
{
    QList<int> chain;
    int level = headings.at(index).level;
    for (int j = index - 1; j >= 0 && level > 1; --j) {
        if (headings.at(j).level < level) {
            chain.prepend(j);
            level = headings.at(j).level;
        }
    }
    return chain;
}

/// The keys `parents` appear in this order among the keys of the headings enclosing `index`.
bool enclosedBy(const QList<Heading> &headings, const QStringList &keys, int index, const QStringList &parents)
{
    const QList<int> chain = enclosingHeadings(headings, index);
    qsizetype next = 0;
    for (int j : chain) {
        if (next < parents.size() && keys.at(j) == parents.at(next))
            ++next;
    }
    return next == parents.size();
}

/// The non-empty segment keys of a heading path ("A#B#C" -> keys of A, B, C).
QStringList segmentKeys(QStringView fragment)
{
    QStringList out;
    for (const QStringView part : fragment.split(u'#')) {
        const QString key = headingKey(part);
        if (!key.isEmpty())
            out.append(key);
    }
    return out;
}

int findHeadingWithKeys(const QList<Heading> &headings, const QStringList &keys, QStringView fragment)
{
    if (fragment.trimmed().isEmpty())
        return -1;
    const QString whole = headingKey(fragment);
    if (!whole.isEmpty()) {
        const qsizetype at = keys.indexOf(whole);
        if (at >= 0)
            return int(at);
    }
    QStringList parents = segmentKeys(fragment);
    if (parents.isEmpty())
        return -1;
    const QString last = parents.takeLast();
    int first = -1;
    for (int i = 0; i < keys.size(); ++i) {
        if (keys.at(i) != last)
            continue;
        if (parents.isEmpty())
            return i;
        if (first < 0)
            first = i;
        if (enclosedBy(headings, keys, i, parents))
            return i;
    }
    return first;
}

} // namespace

QString headingFragment(QStringView headingText)
{
    QStringView line = firstLineOf(headingText);
    if (const auto id = blockIdAtEnd(line))
        line = line.first(id->from);
    const QString normalized = line.toString().normalized(QString::NormalizationForm_C);
    const QString shown = shownText(normalized);
    QString out;
    out.reserve(shown.size());
    for (qsizetype i = 0; i < shown.size(); ++i) {
        const QChar c = shown.at(i);
        const QChar next = i + 1 < shown.size() ? shown.at(i + 1) : QChar();
        if (c == u'*' || c == u'`')
            continue; // emphasis, code spans
        if ((c == u'~' || c == u'=') && next == c) {
            ++i; // ~~strike~~, ==highlight==
            continue;
        }
        if (c == u'%' && next == u'%') {
            ++i; // %%comment%%: its text stays (Obsidian keeps it in heading links too)
            out += u' ';
            continue;
        }
        out += isBreaking(c) ? QChar(u' ') : c;
    }
    return out.simplified().normalized(QString::NormalizationForm_C);
}

QString headingKey(QStringView headingText)
{
    return text::foldKey(headingFragment(headingText));
}

bool headingMatches(QStringView fragment, QStringView headingText)
{
    const QString key = headingKey(headingText);
    if (key.isEmpty())
        return false;
    if (headingKey(fragment) == key)
        return true;
    const qsizetype hash = fragment.lastIndexOf(u'#');
    return hash >= 0 && headingKey(fragment.sliced(hash + 1)) == key;
}

int findHeading(const QList<Heading> &headings, QStringView fragment)
{
    return findHeadingWithKeys(headings, keysOf(headings), fragment);
}

QString uniqueHeadingFragment(const QList<Heading> &headings, int index)
{
    if (index < 0 || index >= headings.size())
        return {};
    const QString own = headingFragment(headings.at(index).text);
    if (own.isEmpty() || own.size() > MaxFragment)
        return {};
    const QStringList keys = keysOf(headings);
    const QString ownKey = keys.at(index);
    if (keys.count(ownKey) == 1)
        return findHeadingWithKeys(headings, keys, own) == index ? own : QString();
    // A heading path through the enclosing headings, the nearest first: the shortest one that
    // names this heading and no other with its name.
    const QList<int> chain = enclosingHeadings(headings, index);
    QString path = own;
    QStringList parents;
    for (qsizetype k = chain.size() - 1; k >= 0; --k) {
        const QString part = headingFragment(headings.at(chain.at(k)).text);
        if (part.isEmpty())
            continue;
        path = part + u'#' + path;
        if (path.size() > MaxFragment)
            return {};
        parents.prepend(keys.at(chain.at(k)));
        int named = 0;
        for (int i = 0; i < keys.size() && named < 2; ++i) {
            if (keys.at(i) == ownKey && enclosedBy(headings, keys, i, parents))
                ++named;
        }
        if (named == 1 && findHeadingWithKeys(headings, keys, path) == index)
            return path;
    }
    return {};
}

QString formatReference(ReferenceKind kind, QStringView pageTarget, QStringView fragment, QStringView blockId)
{
    const QString page = pageTarget.toString();
    const auto wiki = [&page](const QString &fragmentWithHash, bool embed) {
        return (embed ? u"![["_s : u"[["_s) + page + fragmentWithHash + u"]]"_s;
    };
    const auto pageOnly = [&](bool embed) { return page.isEmpty() ? QString() : wiki(QString(), embed); };
    switch (kind) {
    case ReferenceKind::PageLink: return pageOnly(false);
    case ReferenceKind::PageEmbed: return pageOnly(true);
    case ReferenceKind::HeadingLink:
    case ReferenceKind::HeadingEmbed: {
        const bool embed = kind == ReferenceKind::HeadingEmbed;
        if (fragment.isEmpty())
            return pageOnly(embed);
        return wiki(u"#"_s + fragment.toString(), embed);
    }
    case ReferenceKind::BlockLink:
    case ReferenceKind::BlockLinkEmbed: {
        const bool embed = kind == ReferenceKind::BlockLinkEmbed;
        if (blockId.isEmpty())
            return pageOnly(embed);
        return wiki(u"#^"_s + blockId.toString(), embed);
    }
    case ReferenceKind::BlockRef: return blockId.isEmpty() ? pageOnly(false) : u"(("_s + blockId.toString() + u"))"_s;
    case ReferenceKind::BlockEmbed: return blockId.isEmpty() ? pageOnly(true) : u"!(("_s + blockId.toString() + u"))"_s;
    }
    return {};
}

} // namespace brulion::markdown
