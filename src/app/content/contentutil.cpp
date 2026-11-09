#include "contentutil.h"

#include "editorhost.h"

#include "dates/format.h"
#include "index/vaultindex.h"
#include "markdown/blockdocument.h"
#include "markdown/inline.h"
#include "text/text.h"
#include "util/paths.h"
#include "workspace.h"

#include <QColor>
#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace brulion::content {

using namespace Qt::StringLiterals;

QDate parseIsoDate(const QString &text)
{
    if (text.size() != 10)
        return {};
    return QDate::fromString(text, u"yyyy-MM-dd"_s);
}

QString isoDate(QDate date)
{
    return date.isValid() ? date.toString(u"yyyy-MM-dd"_s) : QString();
}

QDate effectiveToday(const QString &isoToday, const shell::Workspace *workspace)
{
    if (const QDate fixed = parseIsoDate(isoToday); fixed.isValid())
        return fixed;
    return workspace ? workspace->now().date() : QDate::currentDate();
}

dates::UiLocale uiLocale(const QString &locale)
{
    return locale.startsWith(u"en"_s, Qt::CaseInsensitive) ? dates::UiLocale::English : dates::UiLocale::Polish;
}

namespace {

QString colorString(const QVariant &v, const QString &fallback)
{
    if (!v.isValid())
        return fallback;
    const QColor c =
            v.metaType() == QMetaType::fromType<QColor>() ? v.value<QColor>() : QColor::fromString(v.toString());
    if (!c.isValid())
        return fallback;
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

/// Case-insensitive, Unicode whole-word regex for a page name; whitespace inside the name
/// matches any run of whitespace (a mention may wrap or use a no-break space).
QRegularExpression nameRegex(const QString &name)
{
    const QStringList words = name.simplified().split(u' ', Qt::SkipEmptyParts);
    QStringList escaped;
    for (const QString &w : words)
        escaped.append(QRegularExpression::escape(w));
    return text::unicodeRegex(text::wordStartAssertion() + escaped.join(u"\\s+"_s) + text::wordEndAssertion(),
                              QRegularExpression::CaseInsensitiveOption);
}

/// Names longest first (ties alphabetical), trimmed, NFC, deduplicated case-insensitively;
/// names shorter than two characters are dropped (as the index does).
QStringList cleanNames(const QStringList &names)
{
    QStringList out;
    QSet<QString> seen;
    for (const QString &raw : names) {
        const QString n = raw.normalized(QString::NormalizationForm_C).simplified();
        if (n.size() < 2)
            continue;
        const QString key = n.toCaseFolded();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out.append(n);
    }
    std::ranges::stable_sort(
            out, [](const QString &a, const QString &b) { return a.size() != b.size() ? a.size() > b.size() : a < b; });
    return out;
}

void markRange(QSet<int> &marks, qsizetype from, qsizetype to)
{
    for (qsizetype i = from; i < to; ++i)
        marks.insert(int(i));
}

QList<int> sortedMarks(const QSet<int> &marks)
{
    QList<int> out(marks.cbegin(), marks.cend());
    std::ranges::sort(out);
    return out;
}

/// Calls `found(from, to)` for the whole-word occurrences of `names` in `text`, longest name first;
/// `false` from it skips the rest of that name's occurrences.
template<typename Found>
void forEachMention(const QString &text, const QStringList &names, Found found)
{
    const QStringList clean = cleanNames(names);
    for (const QString &name : clean) {
        const QRegularExpression re = nameRegex(name);
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (!found(m.capturedStart(), m.capturedEnd()))
                break;
        }
    }
}

/// The text of the block with id `id` anywhere in the notebook.
std::optional<QString> blockTextById(const VaultIndex &index, const QString &id)
{
    const auto ref = index.blockById(id);
    if (!ref)
        return std::nullopt;
    const ParsedNote *note = index.note(ref->path);
    if (!note || ref->blockIndex < 0 || ref->blockIndex >= note->blocks.size())
        return std::nullopt;
    return note->blocks.at(ref->blockIndex).text;
}

} // namespace

markdown::Palette paletteFromMap(const QVariantMap &map)
{
    markdown::Palette p;
    const auto get = [&map](const QString &key, const QString &fallback) { return colorString(map.value(key), fallback); };
    p.link = get(u"link"_s, p.link);
    p.unresolved = get(u"unresolved"_s, p.unresolved);
    p.muted = get(u"muted"_s, p.muted);
    p.codeBackground = get(u"codeBackground"_s, p.codeBackground);
    p.highlightBackground = get(u"highlightBackground"_s, p.highlightBackground);
    const auto chip = [&](const QString &name, markdown::ChipColors &c) {
        c.foreground = get(name + u"Fg"_s, c.foreground);
        c.background = get(name + u"Bg"_s, c.background);
    };
    chip(u"tag"_s, p.tag);
    chip(u"due"_s, p.due);
    chip(u"overdue"_s, p.overdue);
    chip(u"scheduled"_s, p.scheduled);
    chip(u"reminder"_s, p.reminder);
    chip(u"alarm"_s, p.alarm);
    chip(u"recurrence"_s, p.recurrence);
    chip(u"done"_s, p.done);
    chip(u"neutral"_s, p.neutral);
    return p;
}

markdown::RenderContext renderContext(const VaultIndex *index, const VaultPath &fromPath, QDate today,
                                      dates::UiLocale locale, const markdown::Palette &palette)
{
    markdown::RenderContext ctx;
    if (index) {
        ctx.resolvePage = [index, fromPath](const QString &target) -> std::optional<QString> {
            return index->resolveLink(target, fromPath);
        };
        // A page's icon in front of links to it (docs/SYNTAX.md §4 "Page icon and cover").
        ctx.pageIcon = [index, fromPath](const QString &target) -> markdown::PageIcon {
            const std::optional<QString> path = index->resolveLink(target, fromPath);
            const ParsedNote *note = path ? index->note(*path) : nullptr;
            return note ? editor::availablePageIcon(markdown::pageLook(note->properties).icon) : markdown::PageIcon();
        };
        // [[Firma/Adam]]: every path part a link (docs/SYNTAX.md §2 "Path parts").
        ctx.resolveLinkPart = [index, fromPath](const QString &target, int part) {
            const auto p = index->resolveLinkPart(target, part, fromPath);
            return p ? p->kind : markdown::LinkPartTarget::Kind::New;
        };
        ctx.blockText = [index](const QString &id) { return blockTextById(*index, id); };
        // [[Page#^id]]: the block of that page, else (moved) the notebook's block with the id.
        ctx.linkedBlockText = [index, fromPath](const QString &target, const QString &id) -> std::optional<QString> {
            const std::optional<QString> path =
                    target.isEmpty() ? std::optional<QString>(fromPath) : index->resolveLink(target, fromPath);
            const ParsedNote *note = path ? index->note(*path) : nullptr;
            if (!note)
                return std::nullopt;
            if (const int i = note->blockIds.value(id, -1); i >= 0 && i < note->blocks.size())
                return note->blocks.at(i).text;
            return blockTextById(*index, id);
        };
    }
    ctx.today = today;
    ctx.locale = locale;
    ctx.palette = palette;
    ctx.taskCheckbox = true;
    // [[2026-10-13]] shows as "Tue, Oct 13" (the note keeps the date as written).
    ctx.linkTitle = markdown::journalLinkTitle(index ? index->config() : VaultConfig(), locale, today);
    return ctx;
}

QString renderBlockAt(const QString &content, int line, const markdown::RenderContext &context)
{
    const markdown::BlockDocument doc(content);
    const int i = doc.indexAtLine(line);
    if (i < 0)
        return {};
    const markdown::BlockEntry &entry = doc.at(i);
    return markdown::InlineRenderer(context).renderBlock(entry.body, entry.kind);
}

QString plainText(const QString &markdown, const VaultIndex *index)
{
    const markdown::InlineScan scan = markdown::extractInline(markdown);
    QString out;
    qsizetype last = 0;
    for (const Link &link : scan.links) {
        if (link.from < last || link.to > markdown.size())
            continue;
        QString shown;
        switch (link.kind) {
        case LinkKind::Wiki:
        case LinkKind::Embed:
        case LinkKind::Markdown:
            shown = link.display.trimmed();
            if (shown.isEmpty()) {
                shown = link.target;
                if (link.kind != LinkKind::Markdown && !link.heading.isEmpty())
                    shown += (shown.isEmpty() ? QString() : u" › "_s) + link.heading;
            }
            break;
        case LinkKind::BlockRef:
        case LinkKind::BlockEmbed: {
            const QString id = link.blockId.isEmpty() ? link.target : link.blockId;
            if (index) {
                if (const std::optional<QString> text = blockTextById(*index, id))
                    shown = text->simplified();
            }
            if (shown.isEmpty())
                shown = u"(("_s + id + u"))"_s;
            break;
        }
        case LinkKind::Url:
            continue; // shown as written
        }
        out += QStringView(markdown).mid(last, link.from - last);
        out += shown;
        last = link.to;
    }
    out += QStringView(markdown).mid(last);
    static const QRegularExpression emphasis(u"(\\*\\*|__|~~|==|`)"_s);
    out.replace(emphasis, QString());
    return out.simplified();
}

QString oneLine(const QString &text, int max)
{
    QString out = text.simplified();
    if (max > 1 && out.size() > max)
        out = out.left(max - 1).trimmed() + u'…';
    return out;
}

QStringList searchTerms(const QString &query)
{
    static const QRegularExpression whitespace(u"\\s+"_s);
    QStringList out;
    QString q = query;
    q.replace(u'"', u' ');
    const QStringList parts = q.split(whitespace, Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        // Search operators ("-wyklucz", "tag:") are not highlighted as typed.
        if (part.startsWith(u'-') && part.size() > 1)
            continue;
        const qsizetype colon = part.indexOf(u':');
        const QString folded = text::foldKey(colon > 0 && colon + 1 < part.size() ? part.mid(colon + 1) : part);
        const bool hasWordChar = std::ranges::any_of(folded, [](QChar c) { return text::isWordChar(c); });
        if (!hasWordChar || out.contains(folded))
            continue;
        out.append(folded);
    }
    // Longer terms first: "dom" must not hide the end of "domek".
    std::ranges::stable_sort(out, [](const QString &a, const QString &b) { return a.size() > b.size(); });
    return out;
}

QList<int> matchPositions(const QString &text, const QStringList &terms, bool wordStart)
{
    if (text.isEmpty() || terms.isEmpty())
        return {};
    const text::FoldedText folded = text::foldKeyWithMap(text);
    const QString &f = folded.text;
    QSet<int> marks;
    for (const QString &term : terms) {
        if (term.isEmpty())
            continue;
        qsizetype at = 0;
        while ((at = f.indexOf(term, at)) >= 0) {
            const bool atWordStart = !wordStart || at == 0 || !text::isWordChar(f.at(at - 1))
                                     || !text::isWordChar(term.at(0));
            if (atWordStart)
                markRange(marks, folded.toOriginal(at), folded.toOriginal(at + term.size()));
            at += 1;
        }
    }
    return sortedMarks(marks);
}

bool containsAll(const QString &text, const QStringList &terms)
{
    if (terms.isEmpty())
        return true;
    const QString folded = text::foldKey(text);
    return std::ranges::all_of(terms, [&folded](const QString &t) { return folded.contains(t); });
}

QList<int> mentionPositions(const QString &text, const QStringList &names)
{
    QSet<int> marks;
    forEachMention(text, names, [&marks](qsizetype from, qsizetype to) {
        markRange(marks, from, to);
        return true;
    });
    return sortedMarks(marks);
}

MentionEdit linkMention(const QString &content, int line, const QStringList &names,
                        const std::function<QString(const QString &mention)> &linkFor)
{
    MentionEdit edit;
    edit.content = content;
    const QList<text::Line> lines = text::splitLines(content);
    if (line < 0 || line >= lines.size())
        return edit;
    const text::Line &l = lines.at(line);
    const QString &lineText = l.text;

    // Where a mention may not be: inline code (and comments), links, URLs, tags.
    QList<bool> masked(lineText.size(), false);
    const QString code = markdown::maskInlineCode(lineText);
    for (qsizetype i = 0; i < lineText.size() && i < code.size(); ++i)
        masked[i] = code.at(i) != lineText.at(i);
    const auto mask = [&masked](qsizetype from, qsizetype to) {
        for (qsizetype i = std::max<qsizetype>(0, from); i < std::min<qsizetype>(to, masked.size()); ++i)
            masked[i] = true;
    };
    const markdown::InlineScan scan = markdown::extractInline(lineText);
    for (const Link &link : scan.links)
        mask(link.from, link.to);
    const QList<markdown::TagSpan> tags = markdown::findTags(lineText);
    for (const markdown::TagSpan &tag : tags)
        mask(tag.from, tag.to);
    const auto free = [&masked](qsizetype from, qsizetype to) {
        for (qsizetype i = from; i < to; ++i) {
            if (masked.at(i))
                return false;
        }
        return true;
    };

    qsizetype bestFrom = -1;
    qsizetype bestTo = -1;
    forEachMention(lineText, names, [&](qsizetype from, qsizetype to) {
        if (!free(from, to))
            return true; // the name's next occurrence
        // Earliest wins; names come longest first, so a later name at the same start is shorter.
        if (bestFrom < 0 || from < bestFrom) {
            bestFrom = from;
            bestTo = to;
        }
        return false; // a name's first free occurrence is its earliest
    });
    if (bestFrom < 0)
        return edit;

    edit.mention = lineText.mid(bestFrom, bestTo - bestFrom);
    edit.replacement = linkFor ? linkFor(edit.mention) : u"[["_s + edit.mention + u"]]"_s;
    if (edit.replacement.isEmpty())
        return edit;
    edit.from = l.start + bestFrom;
    edit.to = l.start + bestTo;
    edit.content = content.left(edit.from) + edit.replacement + content.mid(edit.to);
    return edit;
}

QString mentionLink(const VaultIndex *index, const QString &mention, const VaultPath &target, const VaultPath &fromPath)
{
    if (index) {
        const auto resolved = index->resolveLink(mention, fromPath);
        if (resolved && *resolved == target)
            return u"[["_s + mention + u"]]"_s;
    }
    const QString name = paths::stripMarkdownExtension(target);
    if (name.compare(mention, Qt::CaseSensitive) == 0)
        return u"[["_s + mention + u"]]"_s;
    return u"[["_s + name + u'|' + mention + u"]]"_s;
}

QString humanSize(qint64 bytes, dates::UiLocale locale)
{
    const QLocale l = locale == dates::UiLocale::English ? QLocale(QLocale::English) : QLocale(QLocale::Polish);
    if (bytes < 1024)
        return u"%1 B"_s.arg(std::max<qint64>(0, bytes));
    if (bytes < 1024 * 1024)
        return u"%1 kB"_s.arg(l.toString(double(bytes) / 1024.0, 'f', bytes < 10 * 1024 ? 1 : 0));
    return u"%1 MB"_s.arg(l.toString(double(bytes) / (1024.0 * 1024.0), 'f', 1));
}

QString humanTime(qint64 msecsSinceEpoch, QDate today, dates::UiLocale locale)
{
    if (msecsSinceEpoch <= 0)
        return {};
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(msecsSinceEpoch);
    return dates::formatHuman(DateSpec{ t.date(), QTime(t.time().hour(), t.time().minute()) }, locale, today);
}

QString normalizeTag(const QString &tag)
{
    QString t = tag.trimmed();
    if (t.startsWith(u'#'))
        t.remove(0, 1);
    if (t.startsWith(u"[["_s) && t.endsWith(u"]]"_s) && t.size() >= 4)
        t = t.mid(2, t.size() - 4);
    return t.trimmed().normalized(QString::NormalizationForm_C).toLower();
}

QStringList pageTags(const ParsedNote &note)
{
    QStringList out;
    const auto add = [&out](const QVariant &value) {
        QStringList names;
        if (value.metaType() == QMetaType::fromType<QStringList>())
            names = value.toStringList();
        else if (value.metaType() == QMetaType::fromType<QVariantList>()) {
            const QVariantList list = value.toList();
            for (const QVariant &v : list)
                names.append(v.toString());
        } else if (value.isValid())
            names = value.toString().split(u',');
        for (const QString &n : std::as_const(names)) {
            // "a b" in a Logseq property is one tag per word only when written with '#'.
            const QString key = normalizeTag(n);
            if (!key.isEmpty() && !out.contains(key))
                out.append(key);
        }
    };
    for (const QString &k : { u"tags"_s, u"tag"_s }) {
        add(note.frontmatter.value(k));
        add(note.properties.value(k));
    }
    return out;
}

int compareNames(const QString &a, const QString &b)
{
    return text::compareNames(a, b);
}

} // namespace brulion::content
