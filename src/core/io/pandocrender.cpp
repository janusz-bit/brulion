#include "io/pandocrender_p.h"

#include "io/htmlrender_p.h"
#include "io/messages_p.h"
#include "io/rendercommon_p.h"
#include "markdown/inline.h"
#include "markdown/math.h"
#include "markdown/tokens.h"
#include "text/text.h"
#include "util/paths.h"

#include <QRegularExpression>
#include <QScopeGuard>

#include <algorithm>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

constexpr int MaxInlineDepth = 2;

/// Plain text between atoms: escaped, line breaks as hard breaks.
QString plainText(QStringView s)
{
    QString out = PandocRenderer::escape(s);
    out.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
    return out.replace(u'\n', u"\\\n"_s);
}

std::pair<QString, QString> pandocMarks(QChar ch, int count)
{
    if (ch == u'~')
        return {u"~~"_s, u"~~"_s};
    if (ch == u'=')
        return {u"["_s, u"]{.mark}"_s};
    return count == 2 ? std::pair{u"**"_s, u"**"_s} : std::pair{u"*"_s, u"*"_s};
}

/// A line of inline output that pandoc would read as block syntax at its start (a heading, a
/// quote, a list item, a definition, a table row, a setext underline) gets that character
/// escaped; leading spaces (an indented code block) are dropped.
QString guardLineStarts(const QString &text)
{
    QStringList lines = text.split(u'\n');
    for (QString &line : lines) {
        qsizetype i = 0;
        while (i < line.size() && (line[i] == u' ' || line[i] == u'\t'))
            ++i;
        line.remove(0, i);
        if (line.isEmpty())
            continue;
        static const QString special = u"#>:+-=|%"_s;
        if (special.contains(line.front())) {
            line.prepend(u'\\');
            continue;
        }
        qsizetype d = 0;
        while (d < line.size() && line[d].isDigit())
            ++d;
        if (d > 0 && d < line.size() && (line[d] == u'.' || line[d] == u')'))
            line.insert(d, u'\\');
    }
    return lines.join(u'\n');
}

/// `code` as a code span (a backtick fence longer than any run inside).
QString codeSpan(const QString &code)
{
    qsizetype longest = 0;
    for (qsizetype i = 0; i < code.size();) {
        qsizetype n = 0;
        while (i + n < code.size() && code[i + n] == u'`')
            ++n;
        longest = std::max(longest, n);
        i += n > 0 ? n : 1;
    }
    const QString fence(longest + 1, u'`');
    const bool pad = code.startsWith(u'`') || code.endsWith(u'`') || code.isEmpty();
    return fence + (pad ? u" "_s : QString()) + code + (pad ? u" "_s : QString()) + fence;
}

/// A fenced code block; fenced math becomes display TeX math (pandoc renders it in every
/// format), with blank lines dropped (pandoc's `$$` cannot hold them).
QString codeBlock(const QString &lang, const QString &code, const QString &id)
{
    if (isMathFence(lang)) {
        QStringList lines;
        for (const QString &line : code.split(u'\n')) {
            if (!line.trimmed().isEmpty())
                lines.append(line);
        }
        const QString math = u"$$\n"_s + lines.join(u'\n') + u"\n$$"_s;
        return id.isEmpty() ? math : u"::: {#"_s + id + u"}\n"_s + math + u"\n:::"_s;
    }
    qsizetype longest = 0;
    for (const QString &line : code.split(u'\n')) {
        const QString t = line.trimmed();
        qsizetype n = 0;
        while (n < t.size() && t[n] == u'`')
            ++n;
        longest = std::max(longest, n);
    }
    QString cls;
    for (const QChar c : lang) {
        if (c.isLetterOrNumber() || c == u'-' || c == u'_' || c == u'+' || c == u'#' || c == u'.')
            cls += c;
    }
    const QString fence(std::max<qsizetype>(3, longest + 1), u'`');
    QString info = cls;
    if (!id.isEmpty())
        info = u"{#"_s + id + (cls.isEmpty() ? QString() : u" ."_s + cls) + u'}';
    return fence + info + u'\n' + code + (code.isEmpty() ? QString() : u"\n"_s) + fence;
}

/// A span of text with a class: `[text]{.cls}`.
QString classSpan(const QString &cls, const QString &text)
{
    return u'[' + PandocRenderer::escape(text) + u"]{."_s + cls + u'}';
}

/// External URL as a link destination: characters that end or confuse one are encoded.
QString urlDestination(const QString &url)
{
    QString out;
    for (const QChar c : url) {
        switch (c.unicode()) {
        case u' ': out += u"%20"_s; break;
        case u'(': out += u"%28"_s; break;
        case u')': out += u"%29"_s; break;
        case u'<': out += u"%3C"_s; break;
        case u'>': out += u"%3E"_s; break;
        default: out += c;
        }
    }
    return out;
}

/// An identifier pandoc accepts (letters, digits, `-_:.`).
QString identifier(const QString &raw)
{
    QString out;
    for (const QChar c : raw)
        out += c.isLetterOrNumber() || c == u'-' || c == u'_' || c == u':' || c == u'.' ? c : u'-';
    return out;
}

QString priorityText(Priority p)
{
    switch (p) {
    case Priority::Highest: return IoMessages::tr("highest");
    case Priority::High: return IoMessages::tr("high");
    case Priority::Medium: return IoMessages::tr("medium");
    case Priority::Low: return IoMessages::tr("low");
    case Priority::Lowest: return IoMessages::tr("lowest");
    case Priority::None: break;
    }
    return {};
}

QString when(const markdown::Token &t)
{
    return t.date.toString(u"yyyy-MM-dd"_s) + (t.time ? u' ' + t.time->toString(u"HH:mm"_s) : QString());
}

/// A metadata token as words in a span ("due: 2026-10-20") — emoji would not survive every
/// output format (pdflatex stops at them); empty for tokens an export leaves out.
QString tokenSpan(const markdown::Token &t)
{
    using markdown::TokenKind;
    switch (t.kind) {
    case TokenKind::Due: return classSpan(u"due"_s, IoMessages::tr("due: %1").arg(when(t)));
    case TokenKind::Scheduled: return classSpan(u"scheduled"_s, IoMessages::tr("scheduled: %1").arg(when(t)));
    case TokenKind::Start: return classSpan(u"start"_s, IoMessages::tr("starts: %1").arg(when(t)));
    case TokenKind::Done: return classSpan(u"done"_s, IoMessages::tr("done: %1").arg(when(t)));
    case TokenKind::Cancelled: return classSpan(u"cancelled"_s, IoMessages::tr("cancelled: %1").arg(when(t)));
    case TokenKind::Planning:
        return t.field == TokenKind::Due ? classSpan(u"due"_s, IoMessages::tr("due: %1").arg(when(t)))
                                         : classSpan(u"scheduled"_s, IoMessages::tr("scheduled: %1").arg(when(t)));
    case TokenKind::Recurrence: {
        const QString rule = t.value.trimmed().isEmpty() ? markdown::recurrenceOf(t) : t.value.trimmed();
        return classSpan(u"recurrence"_s, IoMessages::tr("repeats: %1").arg(rule));
    }
    case TokenKind::Priority:
    case TokenKind::LogseqPriority:
        return t.priority == Priority::None
                       ? QString()
                       : classSpan(u"priority"_s, IoMessages::tr("priority: %1").arg(priorityText(t.priority)));
    case TokenKind::Reminder:
    case TokenKind::ReminderCompat:
        return t.alarm ? classSpan(u"alarm"_s, IoMessages::tr("alarm: %1").arg(when(t)))
                       : classSpan(u"reminder"_s, IoMessages::tr("reminder: %1").arg(when(t)));
    case TokenKind::Created:
    case TokenKind::TaskId:
    case TokenKind::DependsOn:
    case TokenKind::OnCompletion: break;
    }
    return {};
}

/// `body` with every line after the first indented by `width` spaces (blank lines stay empty).
QString indentRest(const QString &body, qsizetype width)
{
    QStringList lines = body.split(u'\n');
    const QString pad(width, u' ');
    for (qsizetype i = 1; i < lines.size(); ++i) {
        if (!lines[i].isEmpty())
            lines[i].prepend(pad);
    }
    return lines.join(u'\n');
}

/// `body` as a block quote.
QString quoted(const QString &body)
{
    QStringList lines = body.split(u'\n');
    for (QString &line : lines)
        line = line.isEmpty() ? u">"_s : u"> "_s + line;
    return lines.join(u'\n');
}

QString fencedDiv(const QString &attributes, const QString &body)
{
    return u"::: {"_s + attributes + u"}\n"_s + body + u"\n:::"_s;
}

} // namespace

PandocRenderer::PandocRenderer(PandocLinks links) : m_links(std::move(links)) { }

QString PandocRenderer::escape(QStringView text)
{
    static const QString special = u"\\`*_~^$@<[]&#|"_s;
    QString out;
    out.reserve(text.size() + 8);
    for (const QChar c : text) {
        if (special.contains(c))
            out += u'\\';
        out += c;
    }
    return out;
}

QString PandocRenderer::headingAnchor(const QString &heading)
{
    return identifier(HtmlRenderer::headingAnchor(heading));
}

QString PandocRenderer::blockAnchor(const QString &id)
{
    return identifier(HtmlRenderer::blockAnchor(id));
}

QString PandocRenderer::tagSpan(const QString &tag)
{
    return u"[#"_s + escape(tag) + u"]{.tag}"_s;
}

QString PandocRenderer::link(const QString &label, const std::optional<QString> &destination) const
{
    if (!destination)
        return u'[' + label + u"]{.missing}"_s;
    if (destination->isEmpty())
        return label;
    return u'[' + label + u"]("_s + *destination + u')';
}

QString PandocRenderer::webPictureDestination(const QString &url) const
{
    if (const std::optional<QString> copy = m_links.webPicture ? m_links.webPicture(url) : std::nullopt)
        return *copy;
    return urlDestination(url);
}

QString PandocRenderer::renderInline(QStringView text, bool insideLink) const
{
    if (text.isEmpty())
        return {};
    static thread_local int depth = 0;
    if (depth > MaxInlineDepth)
        return guardLineStarts(plainText(text));
    ++depth;
    const auto restore = qScopeGuard([] { --depth; });

    const QString masked = markdown::maskInlineCode(text);
    QList<InlineAtom> atoms;
    codeAndCommentAtoms(text, masked, atoms, codeSpan);

    // Formulas (docs/SYNTAX.md §11) as pandoc's TeX math: no space inside the dollars.
    for (const markdown::MathSpan &span : markdown::findMathSpans(masked)) {
        QString tex = text.sliced(span.contentFrom, span.contentTo - span.contentFrom).toString();
        tex.replace(u"\r\n"_s, u" "_s).replace(u'\n', u' ').replace(u'\r', u' ');
        tex = tex.trimmed();
        const QString dollars = span.display ? u"$$"_s : u"$"_s;
        atoms.append(InlineAtom{span.from, span.to,
                                tex.isEmpty() ? plainText(text.sliced(span.from, span.to - span.from))
                                              : dollars + tex + dollars});
    }

    // Links, embeds, block references, URLs.
    const markdown::InlineScan scan = markdown::extractInline(text);
    for (const Link &l : scan.links) {
        const qsizetype from = l.from;
        const qsizetype to = l.to;
        if (from < 0 || to > text.size() || from >= to)
            continue;
        const QStringView source = text.sliced(from, to - from);
        if (insideLink) {
            atoms.append(InlineAtom{from, to, escape(visibleText(l, source))});
            continue;
        }
        QString out;
        switch (l.kind) {
        case LinkKind::Wiki: {
            QString label = l.display;
            if (label.isEmpty()) {
                const QString frag = !l.heading.isEmpty()   ? l.heading
                                     : !l.blockId.isEmpty() ? u'^' + l.blockId
                                                            : QString();
                label = l.target.isEmpty() ? frag : frag.isEmpty() ? l.target : QString(l.target + u" > "_s + frag);
            }
            out = link(escape(label), m_links.page ? m_links.page(l.target, l.heading, l.blockId) : std::nullopt);
            break;
        }
        case LinkKind::Embed: {
            if (isWebPictureTarget(l.target)) {
                // `![[https://…/kot.jpg]]`: a picture from the internet, like `![](https://…)`.
                out = u"!["_s + escape(l.display) + u"]("_s + webPictureDestination(l.target) + u')';
            } else if (isAttachmentTarget(l.target)) {
                const auto url = m_links.attachment ? m_links.attachment(l.target) : std::nullopt;
                const QString label = escape(l.display.isEmpty() ? paths::basename(l.target) : l.display);
                out = url && mediaKind(l.target) == MediaKind::Image ? u"!["_s + label + u"]("_s + *url + u')'
                                                                     : link(label, url);
            } else {
                out = link(escape(l.display.isEmpty() ? l.target : l.display),
                           m_links.page ? m_links.page(l.target, l.heading, l.blockId) : std::nullopt);
            }
            break;
        }
        case LinkKind::Markdown: {
            const bool image = source.startsWith(u'!');
            if (hasScheme(l.target)) {
                if (!safeExternal(l.target)) {
                    out = plainText(source);
                } else if (image) {
                    out = u"!["_s + escape(l.display) + u"]("_s
                          + (isWebPictureTarget(l.target) ? webPictureDestination(l.target) : urlDestination(l.target))
                          + u')';
                } else {
                    QString shown = renderInline(l.display, true);
                    if (shown.isEmpty())
                        shown = escape(l.target);
                    out = u'[' + shown + u"]("_s + urlDestination(l.target) + u')';
                }
                break;
            }
            std::optional<QString> url;
            if (!image && m_links.page && !isAttachmentTarget(l.target))
                url = m_links.page(l.target, l.heading, l.blockId);
            if (!url && m_links.attachment)
                url = m_links.attachment(l.target);
            if (!url && !image && m_links.page)
                url = m_links.page(l.target, l.heading, l.blockId);
            QString shown = image ? escape(l.display) : renderInline(l.display, true);
            if (shown.isEmpty() && (!image || !url))
                shown = escape(l.display.isEmpty() ? paths::basename(l.target) : l.display);
            if (url && image && mediaKind(l.target) == MediaKind::Image)
                out = u"!["_s + shown + u"]("_s + *url + u')';
            else
                out = link(shown, url);
            break;
        }
        case LinkKind::BlockRef:
        case LinkKind::BlockEmbed: {
            const auto ref = m_links.block ? m_links.block(l.target) : std::nullopt;
            if (!ref)
                out = u'[' + escape(QString(u"(("_s + l.target + u"))"_s)) + u"]{.missing}"_s;
            else
                out = link(renderInline(ref->second, true), ref->first);
            break;
        }
        case LinkKind::Url: {
            out = safeExternal(l.target) ? u'<' + urlDestination(l.target) + u'>' : plainText(source);
            break;
        }
        }
        atoms.append(InlineAtom{from, to, out});
    }

    // Tags: exactly the ones the parser sees (SYNTAX §3); none inside link labels.
    if (!insideLink) {
        for (const markdown::TagSpan &tag : markdown::findTags(text)) {
            const auto page = m_links.tag ? m_links.tag(tag.name) : std::nullopt;
            atoms.append(InlineAtom{tag.from, tag.to,
                                    page && !page->isEmpty()
                                            ? u"[#"_s + escape(tag.name) + u"]("_s + *page + u"){.tag}"_s
                                            : tagSpan(tag.name)});
        }
    }

    // Metadata tokens as words.
    for (const markdown::Token &t : markdown::findTokens(masked))
        atoms.append(InlineAtom{t.from, t.to, tokenSpan(t)});

    // Block ids at line ends (hidden), footnote references, backslash escapes.
    for (const text::Line &line : text::splitLines(masked)) {
        if (const auto id = markdown::blockIdAtEnd(line.text))
            atoms.append(InlineAtom{line.start + id->from, line.start + id->to, QString()});
    }
    for (qsizetype i = masked.indexOf(u"[^"); i >= 0; i = masked.indexOf(u"[^", i + 2)) {
        qsizetype j = i + 2;
        while (j < masked.size() && masked[j] != u']' && !masked[j].isSpace())
            ++j;
        if (j > i + 2 && j < masked.size() && masked[j] == u']' && (j + 1 >= masked.size() || masked[j + 1] != u':'))
            atoms.append(InlineAtom{
                    i, j + 1,
                    u"[^"_s + identifier(m_links.footnotePrefix + text.sliced(i + 2, j - i - 2).toString()) + u']'});
    }
    for (qsizetype i = 0; i + 1 < masked.size(); ++i) {
        if (masked[i] == u'\\' && isAsciiPunct(masked[i + 1])) {
            atoms.append(InlineAtom{i, i + 2, text.sliced(i, 2).toString()});
            ++i;
        }
    }

    QString out = renderAtoms(text, std::move(atoms), pandocMarks, plainText);
    while (out.endsWith(u"\\\n"))
        out.chop(2);
    return guardLineStarts(out);
}

QString PandocRenderer::idSpan(const Block &block) const
{
    if (!m_links.ids || block.id.isEmpty())
        return {};
    return u"[]{#"_s + m_links.idPrefix + blockAnchor(block.id) + u'}';
}

QString PandocRenderer::metaSpans(const Block &block) const
{
    QStringList spans;
    bool recurrence = false;
    // The tokens as written in the block's own lines (tasks and other blocks alike; reminders
    // come from the parsed block, with the default time filled in).
    const QString raw = block.raw;
    for (const markdown::Token &t : markdown::findTokens(markdown::maskInlineCode(raw))) {
        if (t.kind == markdown::TokenKind::Reminder || t.kind == markdown::TokenKind::ReminderCompat)
            continue;
        recurrence = recurrence || t.kind == markdown::TokenKind::Recurrence;
        const QString span = tokenSpan(t);
        if (!span.isEmpty())
            spans.append(span);
    }
    if (!recurrence && block.task && !block.task->recurrence.isEmpty()) // a Logseq repeater
        spans.append(classSpan(u"recurrence"_s, IoMessages::tr("repeats: %1").arg(block.task->recurrence)));
    for (const Reminder &r : block.reminders) {
        spans.append(r.alarm ? classSpan(u"alarm"_s, IoMessages::tr("alarm: %1").arg(r.at.toString()))
                             : classSpan(u"reminder"_s, IoMessages::tr("reminder: %1").arg(r.at.toString())));
    }
    QStringList props;
    for (auto it = block.properties.constBegin(); it != block.properties.constEnd(); ++it) {
        const QString &key = it.key();
        if (key == u"id" || key == u"collapsed" || key == u"callouttype" || key == u"calloutfold")
            continue;
        const QString value = it.value().typeId() == QMetaType::QStringList ? it.value().toStringList().join(u", "_s)
                                                                            : it.value().toString();
        props.append(classSpan(u"property"_s, key + u": "_s + value));
    }
    props.sort();
    spans.append(props);
    return spans.isEmpty() ? QString() : u' ' + spans.join(u' ');
}

QString PandocRenderer::renderNote(const ParsedNote &note) const
{
    QList<int> roots;
    for (int i = 0; i < note.blocks.size(); ++i) {
        if (note.blocks[i].parent < 0)
            roots.append(i);
    }
    return renderBlocks(note, roots);
}

QString PandocRenderer::renderBlocks(const ParsedNote &note, const QList<int> &indices) const
{
    QStringList parts;
    for (qsizetype k = 0; k < indices.size();) {
        const int idx = indices[k];
        const Block &b = note.blocks[idx];
        if (idx == m_links.skipBlock) {
            ++k;
            continue;
        }
        if (b.kind != BlockKind::ListItem) {
            const QString block = renderBlock(note, idx);
            if (!block.isEmpty())
                parts.append(block);
            ++k;
            continue;
        }
        // One list: items of the same kind, one per line (a tight list unless an item holds
        // blank lines itself).
        const bool ordered = isOrderedItem(b);
        QStringList items;
        while (k < indices.size() && note.blocks[indices[k]].kind == BlockKind::ListItem
               && isOrderedItem(note.blocks[indices[k]]) == ordered) {
            items.append(renderListItem(note, indices[k]));
            ++k;
        }
        parts.append(items.join(u'\n'));
    }
    return parts.join(u"\n\n"_s);
}

QString PandocRenderer::renderListItem(const ParsedNote &note, int index) const
{
    const Block &b = note.blocks[index];
    QString marker = u"-"_s;
    if (isOrderedItem(b)) {
        const QString first = b.raw.trimmed();
        qsizetype d = 0;
        while (d < first.size() && first[d].isDigit())
            ++d;
        marker = first.first(d + 1);
    }
    QString content;
    // A list item that is one fenced block (`/Wzór matematyczny` in an outline or a journal writes
    // "- ```math") is that block, like the editor shows it.
    const auto fence = b.task ? std::nullopt : itemFence(b.text);
    const QString embedded = b.task || fence ? QString() : embeddedContent(b.text);
    if (fence) {
        content = codeBlock(fence->first, fence->second,
                            m_links.ids && !b.id.isEmpty() ? m_links.idPrefix + blockAnchor(b.id) : QString());
    } else if (!embedded.isEmpty()) {
        content = embedded;
    } else {
        QString text = renderInline(b.text);
        QString box;
        QString status;
        if (b.task) {
            switch (b.task->status) {
            case TaskStatus::Todo: box = u"[ ] "_s; break;
            case TaskStatus::Done: box = u"[x] "_s; break;
            case TaskStatus::Cancelled:
                box = u"[x] "_s;
                if (!text.isEmpty())
                    text = u"~~"_s + text + u"~~"_s;
                break;
            case TaskStatus::Doing:
                box = u"[ ] "_s;
                status = u' ' + classSpan(u"status"_s, IoMessages::tr("in progress"));
                break;
            case TaskStatus::Deferred:
                box = u"[ ] "_s;
                status = u' ' + classSpan(u"status"_s, IoMessages::tr("deferred"));
                break;
            }
        }
        content = box + idSpan(b) + text + status + metaSpans(b);
    }
    if (!b.children.isEmpty()) {
        bool lists = true;
        for (const int c : b.children)
            lists = lists && note.blocks[c].kind == BlockKind::ListItem;
        const QString children = renderBlocks(note, b.children);
        if (!children.isEmpty())
            content += (content.isEmpty() ? QString() : lists ? u"\n"_s : u"\n\n"_s) + children;
    }
    return indentRest(marker + u' ' + content, marker.size() + 1);
}

QString PandocRenderer::embeddedContent(const QString &text) const
{
    if (!m_links.embed)
        return {};
    static const QRegularExpression onlyEmbed(u"^!\\[\\[([^\\]|#]+)(?:#(\\^?)([^\\]|]+))?(?:\\|[^\\]]*)?\\]\\]$"_s);
    const auto m = onlyEmbed.match(text.trimmed());
    if (!m.hasMatch() || isAttachmentTarget(m.captured(1)))
        return {};
    const bool isBlock = m.capturedLength(2) > 0;
    const QString frag = m.captured(3);
    const QString content =
            m_links.embed(m.captured(1).trimmed(), isBlock ? QString() : frag, isBlock ? frag : QString());
    return content.isEmpty() ? QString() : fencedDiv(u".embed"_s, content);
}

QString PandocRenderer::renderTable(const QString &source) const
{
    const QStringList rows = source.split(u'\n', Qt::SkipEmptyParts);
    if (rows.isEmpty())
        return {};
    const QStringList header = tableCells(rows.first());
    const bool hasDelimiter =
            rows.size() > 1 && tableCells(rows[1]).join(QString()).remove(u'-').remove(u':').trimmed().isEmpty();
    QStringList delimiter;
    for (qsizetype c = 0; c < header.size(); ++c) {
        const QString cell = hasDelimiter && c < tableCells(rows[1]).size() ? tableCells(rows[1]).at(c) : QString();
        const bool left = cell.startsWith(u':');
        const bool right = cell.endsWith(u':');
        delimiter.append(left && right ? u":-:"_s : right ? u"--:"_s : left ? u":--"_s : u"---"_s);
    }
    const auto row = [&](const QStringList &cells) {
        QStringList out;
        for (const QString &cell : cells) {
            QString content = cell.startsWith(u'=') ? codeSpan(cell) : renderInline(cell);
            // Every '|' of the cell escaped once (text escapes its own; code and links may hold some).
            out.append(content.replace(u'\n', u' ').replace(u"\\|"_s, u"|"_s).replace(u'|', u"\\|"_s));
        }
        return u"| "_s + out.join(u" | "_s) + u" |"_s;
    };
    QStringList out = {row(header), u"|"_s + delimiter.join(u'|') + u'|'};
    for (qsizetype r = hasDelimiter ? 2 : 1; r < rows.size(); ++r)
        out.append(row(tableCells(rows[r])));
    return out.join(u'\n');
}

QString PandocRenderer::renderBlock(const ParsedNote &note, int index) const
{
    const Block &b = note.blocks[index];
    const QString id = m_links.ids && !b.id.isEmpty() ? m_links.idPrefix + blockAnchor(b.id) : QString();
    const auto withId = [&](const QString &body) { return id.isEmpty() ? body : fencedDiv(u'#' + id, body); };
    switch (b.kind) {
    case BlockKind::Frontmatter: return {};
    case BlockKind::Heading: {
        const int level = std::clamp(b.headingLevel + m_links.headingShift, 1, 6);
        QString out = QString(level, u'#') + u' ' + renderInline(b.text) + metaSpans(b);
        if (m_links.ids)
            out += u" {#"_s + m_links.idPrefix + (b.id.isEmpty() ? headingAnchor(b.text) : blockAnchor(b.id)) + u'}';
        return out;
    }
    case BlockKind::Paragraph: {
        // A paragraph that is only a page embed shows the embedded page.
        if (const QString embedded = embeddedContent(b.text); !embedded.isEmpty())
            return withId(embedded);
        // Footnote definitions, one per line that starts with "[^label]:".
        static const QRegularExpression footnote(u"^\\[\\^([^\\]\\s]+)\\]:[ \\t]*"_s);
        if (footnote.match(b.text).hasMatch()) {
            QStringList notes;
            QString label;
            QStringList lines;
            const auto flush = [&] {
                if (!label.isEmpty())
                    notes.append(u"[^"_s + identifier(m_links.footnotePrefix + label) + u"]: "_s
                                 + indentRest(renderInline(lines.join(u'\n')), 4));
                lines.clear();
            };
            for (const QString &line : b.text.split(u'\n')) {
                const auto m = footnote.match(line);
                if (m.hasMatch()) {
                    flush();
                    label = m.captured(1);
                    lines.append(line.sliced(m.capturedLength()));
                } else {
                    lines.append(line);
                }
            }
            flush();
            return notes.join(u"\n\n"_s);
        }
        return idSpan(b) + renderInline(b.text) + metaSpans(b);
    }
    case BlockKind::Code: return codeBlock(b.lang.section(u' ', 0, 0), b.text, id);
    case BlockKind::Table: return withId(renderTable(b.text));
    case BlockKind::Quote: {
        if (!b.children.isEmpty())
            return withId(quoted(renderBlocks(note, b.children)));
        QStringList paragraphs;
        QStringList current;
        for (const QString &line : b.text.split(u'\n')) {
            QStringView v = QStringView(line).trimmed();
            while (v.startsWith(u'>'))
                v = v.sliced(1).trimmed();
            if (v.isEmpty()) {
                if (!current.isEmpty())
                    paragraphs.append(renderInline(current.join(u'\n')));
                current.clear();
            } else {
                current.append(v.toString());
            }
        }
        if (!current.isEmpty())
            paragraphs.append(renderInline(current.join(u'\n')));
        return withId(quoted(paragraphs.join(u"\n\n"_s)));
    }
    case BlockKind::Callout: {
        QString type = b.properties.value(u"callouttype"_s).toString().toLower();
        type = identifier(type.isEmpty() ? u"note"_s : type);
        QStringList parts;
        const QString title = renderInline(calloutTitle(b));
        if (!title.isEmpty())
            parts.append(title.contains(u"**"_s) ? QString(u'[' + title + u"]{.callout-title}"_s)
                                                 : QString(u"**"_s + title + u"**"_s));
        const QString rest = b.text.section(u'\n', 1);
        if (!rest.trimmed().isEmpty())
            parts.append(renderInline(rest.trimmed()));
        if (!b.children.isEmpty())
            parts.append(renderBlocks(note, b.children));
        return fencedDiv((id.isEmpty() ? QString() : u'#' + id + u' ') + u".callout .callout-"_s + type,
                         parts.join(u"\n\n"_s));
    }
    case BlockKind::Rule: return u"* * *"_s;
    case BlockKind::Html: return codeBlock(QString(), b.text, id);
    case BlockKind::ListItem: return renderListItem(note, index);
    }
    return {};
}

} // namespace brulion::io::detail
