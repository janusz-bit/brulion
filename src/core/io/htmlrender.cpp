#include "io/htmlrender_p.h"

#include "calc/table.h"
#include "chart/chart.h"
#include "dates/recurrence.h"
#include "diagram/diagram.h"
#include "diagram/svg.h"
#include "io/messages_p.h"
#include "io/rendercommon_p.h"
#include "markdown/inline.h"
#include "markdown/math.h"
#include "markdown/tokens.h"
#include "text/text.h"
#include "util/paths.h"

#include <QRegularExpression>
#include <QScopeGuard>
#include <QStringTokenizer>

#include <algorithm>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

constexpr int MaxInlineDepth = 2;

QString esc(QStringView s)
{
    return HtmlRenderer::escape(s);
}

QString escapeWithBreaks(QStringView s)
{
    QString out = esc(s);
    out.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n').replace(u'\n', u"<br>"_s);
    return out;
}

QString attr(const QString &value)
{
    return u'"' + esc(value) + u'"';
}

QString chip(const QString &cls, const QString &text)
{
    return u"<span class=\"chip "_s + cls + u"\">"_s + esc(text) + u"</span>"_s;
}

/// A link to `href` showing `content` (HTML).
QString anchorTag(QStringView cls, const QString &href, const QString &content)
{
    return u"<a class=\""_s + cls + u"\" href="_s + attr(href) + u'>' + content + u"</a>"_s;
}

/// A link to the external `url` showing `content` (HTML).
QString externalAnchorTag(const QString &url, const QString &content)
{
    return u"<a class=\"external\" href="_s + attr(url) + u" rel=\"noopener\">"_s + content + u"</a>"_s;
}

/// What a link to something that does not exist shows.
QString missing(const QString &label)
{
    return u"<span class=\"missing\">"_s + esc(label) + u"</span>"_s;
}

QString imageTag(const QString &src, const QString &alt)
{
    return u"<img src="_s + attr(src) + u" alt="_s + attr(alt) + u" loading=\"lazy\">"_s;
}

/// An attachment as <img> / <audio> / <video> / link; the kind comes from `fileName`.
QString mediaHtml(const QString &url, const QString &label, const QString &fileName)
{
    switch (mediaKind(fileName)) {
    case MediaKind::Image: return imageTag(url, label);
    case MediaKind::Audio: return u"<audio controls src="_s + attr(url) + u"></audio>"_s;
    case MediaKind::Video: return u"<video controls src="_s + attr(url) + u"></video>"_s;
    case MediaKind::Other: break;
    }
    return anchorTag(u"attachment", url, esc(label));
}

/// href of the page `link` names (HtmlLinks::page); nullopt when it is missing or unset.
std::optional<QString> pageUrl(const HtmlLinks &links, const Link &link)
{
    return links.page ? links.page(link.target, link.heading, link.blockId) : std::nullopt;
}

std::pair<QString, QString> htmlMarks(QChar ch, int count)
{
    if (ch == u'~')
        return {u"<del>"_s, u"</del>"_s};
    if (ch == u'=')
        return {u"<mark>"_s, u"</mark>"_s};
    return count == 2 ? std::pair{u"<strong>"_s, u"</strong>"_s} : std::pair{u"<em>"_s, u"</em>"_s};
}

QString priorityName(Priority p)
{
    switch (p) {
    case Priority::Highest: return u"highest"_s;
    case Priority::High: return u"high"_s;
    case Priority::Medium: return u"medium"_s;
    case Priority::Low: return u"low"_s;
    case Priority::Lowest: return u"lowest"_s;
    case Priority::None: break;
    }
    return {};
}

QString statusName(TaskStatus s)
{
    switch (s) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return u"todo"_s;
}

QString priorityChip(Priority p)
{
    return u"<span class=\"chip priority priority-"_s + priorityName(p) + u"\">"_s
           + esc(markdown::tokens::priorityEmoji(p)) + u"</span>"_s;
}

/// The chip class of a metadata token of inline text.
QString chipClass(const markdown::Token &t)
{
    using markdown::TokenKind;
    switch (t.kind) {
    case TokenKind::Due: return u"due"_s;
    case TokenKind::Scheduled:
    case TokenKind::Start: return u"scheduled"_s;
    case TokenKind::Reminder:
    case TokenKind::ReminderCompat: return t.alarm ? u"alarm"_s : u"reminder"_s;
    case TokenKind::Recurrence: return u"recurrence"_s;
    case TokenKind::Planning: return t.field == TokenKind::Due ? u"due"_s : u"scheduled"_s;
    default: return u"meta"_s;
    }
}

/// `scene` as SVG: light, and dark when the reader's browser prefers dark (core diagram/svg.h).
QString svgHtml(const diagram::Scene &scene, const QString &fallbackTitle)
{
    const diagram::Palette dark = diagram::darkPalette();
    diagram::SvgOptions options;
    options.fallbackTitle = fallbackTitle;
    return diagram::toSvg(scene, diagram::lightPalette(), &dark, options);
}

/// A fenced code block as HTML: fenced math (```math, latex, tex) is display-only, so the export
/// shows the formula, not its LaTeX source (docs/SYNTAX.md §11); a ```mermaid diagram Brulion
/// draws is its SVG (light, and dark when the reader's browser prefers dark: core diagram/svg.h)
/// with the source folded under it (docs/SYNTAX.md §11 "Mermaid diagrams") — one it cannot draw (a mistake,
/// a type it does not draw yet) and every diagram with `diagramsAsSource` stay code. `id`:
/// attribute text or empty.
QString codeBlockHtml(const QString &lang, const QString &code, const QString &id, bool diagramsAsSource)
{
    if (isMathFence(lang))
        return u"<div class=\"math\""_s + id + u'>' + markdown::renderMath(code, true) + u"</div>"_s;
    if (!diagramsAsSource && lang.compare(u"mermaid", Qt::CaseInsensitive) == 0) {
        if (const diagram::Result r = diagram::render(code); r.ok())
            return u"<figure class=\"mermaid\""_s + id + u'>' + svgHtml(r.scene, IoMessages::tr("Diagram"))
                   + u"<details><summary>"_s + esc(IoMessages::tr("Diagram source"))
                   + u"</summary><pre><code class=\"language-mermaid\">"_s + esc(code)
                   + u"</code></pre></details></figure>"_s;
    }
    const QString cls = lang.isEmpty() ? QString() : u" class="_s + attr(u"language-"_s + lang);
    return u"<pre"_s + id + u"><code"_s + cls + u'>' + esc(code) + u"</code></pre>"_s;
}

} // namespace

HtmlRenderer::HtmlRenderer(HtmlLinks links) : m_links(std::move(links)) { }

QString HtmlRenderer::escape(QStringView text)
{
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'&': out += u"&amp;"; break;
        case u'<': out += u"&lt;"; break;
        case u'>': out += u"&gt;"; break;
        case u'"': out += u"&quot;"; break;
        case u'\'': out += u"&#39;"; break;
        default: out += c;
        }
    }
    return out;
}

QString HtmlRenderer::headingAnchor(const QString &heading)
{
    QString out;
    bool dash = false;
    for (const QString lower = heading.toLower(); const QChar c : lower) {
        if (c.isLetterOrNumber() || c == u'_') {
            if (dash && !out.isEmpty())
                out += u'-';
            out += c;
            dash = false;
        } else {
            dash = true;
        }
    }
    return out.isEmpty() ? u"section"_s : out;
}

QString HtmlRenderer::blockAnchor(const QString &id)
{
    return u"block-"_s + id;
}

QString HtmlRenderer::webPictureHtml(const QString &url, const QString &alt) const
{
    if (m_links.mediaAsLinks)
        return mediaHtml(url, alt.isEmpty() ? url : alt, QString());
    const std::optional<QString> copy = m_links.webPicture ? m_links.webPicture(url) : std::nullopt;
    return imageTag(copy.value_or(url), alt);
}

QString HtmlRenderer::renderInline(QStringView text, bool insideLink) const
{
    if (text.isEmpty())
        return {};
    static thread_local int depth = 0;
    if (depth > MaxInlineDepth)
        return escapeWithBreaks(text);
    ++depth;
    const auto restore = qScopeGuard([] { --depth; });

    const QString masked = markdown::maskInlineCode(text);
    QList<InlineAtom> atoms;
    codeAndCommentAtoms(text, masked, atoms,
                        [](const QString &content) -> QString { return u"<code>"_s + esc(content) + u"</code>"_s; });
    // Coloured highlights: `<mark>` with the palette's colour as Brulion writes it (Highlightr's).
    markAtoms(masked, atoms, [](markdown::HighlightColor color) {
        const QString open = color == markdown::HighlightColor::Default ? u"<mark>"_s : markdown::markOpenTag(color);
        return std::pair{open, u"</mark>"_s};
    });

    // Formulas: $…$ and $$…$$ (docs/SYNTAX.md §11). The scan runs on the masked
    // text, so a formula inside code or a comment is not one; a link label gets one through
    // renderInline() of the label.
    for (const auto spans = markdown::findMathSpans(masked); const markdown::MathSpan &span : spans) {
        const QString cls = span.display ? u"math display"_s : u"math"_s;
        atoms.append(InlineAtom{
                span.from, span.to,
                u"<span class=\""_s + cls + u"\">"_s
                        + markdown::renderMath(text.sliced(span.contentFrom, span.contentTo - span.contentFrom),
                                               span.display)
                        + u"</span>"_s});
    }

    // Links, embeds, block references, URLs; inside a link only their visible text.
    const markdown::InlineScan scan = markdown::extractInline(text);
    for (const Link &l : scan.links) {
        if (l.from < 0 || l.to > text.size() || l.from >= l.to)
            continue;
        const QStringView source = text.sliced(l.from, l.to - l.from);
        atoms.append(InlineAtom{l.from, l.to, insideLink ? esc(visibleText(l, source)) : linkHtml(l, source)});
    }

    // Tags: exactly the ones the parser sees (SYNTAX §3); none inside link labels.
    const QList<markdown::TagSpan> tags = insideLink ? QList<markdown::TagSpan>() : markdown::findTags(text);
    for (const markdown::TagSpan &tag : tags) {
        const QString href = m_links.tag ? m_links.tag(tag.name) : QString();
        const QString shown = u'#' + esc(tag.name);
        atoms.append(InlineAtom{tag.from, tag.to,
                                href.isEmpty() ? QString(u"<span class=\"tag\">"_s + shown + u"</span>"_s)
                                               : anchorTag(u"tag", href, shown)});
    }

    // Metadata tokens as chips.
    for (const auto tokens = markdown::findTokens(masked); const markdown::Token &t : tokens)
        atoms.append(InlineAtom{t.from, t.to, chip(chipClass(t), text.sliced(t.from, t.to - t.from).toString())});

    // Block ids at line ends (hidden), footnote references, backslash escapes.
    for (const auto lines = text::splitLines(masked); const text::Line &line : lines) {
        if (const auto id = markdown::blockIdAtEnd(line.text))
            atoms.append(InlineAtom{line.start + id->from, line.start + id->to, QString()});
    }
    for (qsizetype i = masked.indexOf(u"[^"); i >= 0; i = masked.indexOf(u"[^", i + 2)) {
        qsizetype j = i + 2;
        while (j < masked.size() && masked[j] != u']' && !masked[j].isSpace())
            ++j;
        if (j > i + 2 && j < masked.size() && masked[j] == u']' && (j + 1 >= masked.size() || masked[j + 1] != u':'))
            atoms.append(InlineAtom{i, j + 1,
                                    u"<sup class=\"footnote\">"_s + esc(text.sliced(i + 2, j - i - 2)) + u"</sup>"_s});
    }
    for (qsizetype i = 0; i + 1 < masked.size(); ++i) {
        if (masked[i] == u'\\' && isAsciiPunct(masked[i + 1])) {
            atoms.append(InlineAtom{i, i + 2, esc(text.sliced(i + 1, 1))});
            ++i;
        }
    }

    QString out = renderAtoms(text, std::move(atoms), htmlMarks, escapeWithBreaks);
    while (out.endsWith(u"<br>"))
        out.chop(4);
    return out;
}

QString HtmlRenderer::linkHtml(const Link &l, QStringView source) const
{
    switch (l.kind) {
    case LinkKind::Wiki: {
        QString label = l.display;
        if (label.isEmpty()) {
            const QString frag = !l.heading.isEmpty() ? l.heading : !l.blockId.isEmpty() ? u'^' + l.blockId : QString();
            label = l.target.isEmpty() ? frag : frag.isEmpty() ? l.target : QString(l.target + u" › "_s + frag);
        }
        const auto href = pageUrl(m_links, l);
        return href ? anchorTag(u"internal", *href, esc(label)) : missing(label);
    }
    case LinkKind::Embed: {
        // `![[https://…/kot.jpg]]`: a picture from the internet, like `![](https://…)` — never a
        // missing attachment for its extension.
        if (isWebPictureTarget(l.target))
            return webPictureHtml(l.target, l.display);
        const QString label = l.display.isEmpty() ? paths::basename(l.target) : l.display;
        // An Obsidian Excalidraw plugin drawing: its picture.
        if (const auto drawing = m_links.drawing ? m_links.drawing(l.target) : std::nullopt) {
            return m_links.mediaAsLinks ? mediaHtml(*drawing, label, QString())
                                        : u"<img class=\"drawing\" src="_s + attr(*drawing) + u" alt="_s + attr(label)
                                                  + u" loading=\"lazy\">"_s;
        }
        if (isAttachmentTarget(l.target)) {
            const auto url = m_links.attachment ? m_links.attachment(l.target) : std::nullopt;
            return url ? mediaHtml(*url, label, m_links.mediaAsLinks ? QString() : l.target) : missing(label);
        }
        const QString pageLabel = u"⧉ "_s + (l.display.isEmpty() ? l.target : l.display);
        const auto href = pageUrl(m_links, l);
        return href ? anchorTag(u"embed-link", *href, esc(pageLabel)) : missing(pageLabel);
    }
    case LinkKind::Markdown: return markdownLinkHtml(l, source);
    case LinkKind::BlockRef:
    case LinkKind::BlockEmbed: {
        const auto ref = m_links.block ? m_links.block(l.target) : std::nullopt;
        if (!ref)
            return missing(u"(("_s + l.target + u"))"_s);
        const QString cls = l.kind == LinkKind::BlockRef ? u"block-ref"_s : u"block-embed"_s;
        const QString content = renderInline(ref->second, true);
        return ref->first.isEmpty() ? QString(u"<span class=\""_s + cls + u"\">"_s + content + u"</span>"_s)
                                    : anchorTag(cls, ref->first, content);
    }
    case LinkKind::Url: {
        if (!safeExternal(l.target))
            return esc(source);
        // `<https://…>` shows the URL between the brackets.
        const QStringView shown = source.startsWith(u'<') ? source.sliced(1, source.size() - 2) : QStringView(l.target);
        return externalAnchorTag(l.target, esc(shown));
    }
    }
    return {};
}

QString HtmlRenderer::markdownLinkHtml(const Link &l, QStringView source) const
{
    const bool image = source.startsWith(u'!');
    // The label as HTML, else the target.
    const auto shown = [&] {
        const QString label = renderInline(l.display, true);
        return label.isEmpty() ? esc(l.target) : label;
    };
    if (hasScheme(l.target)) {
        if (!safeExternal(l.target))
            return esc(source);
        if (image && isWebPictureTarget(l.target))
            return webPictureHtml(l.target, l.display);
        if (image && m_links.mediaAsLinks)
            return mediaHtml(l.target, l.display.isEmpty() ? l.target : l.display, QString());
        return image ? imageTag(l.target, l.display) : externalAnchorTag(l.target, shown());
    }
    std::optional<QString> url;
    if (!image && !isAttachmentTarget(l.target))
        url = pageUrl(m_links, l);
    if (!url && m_links.attachment)
        url = m_links.attachment(l.target);
    if (!url && !image)
        url = pageUrl(m_links, l);
    if (!url)
        return missing(l.display.isEmpty() ? l.target : l.display);
    if (!image)
        return anchorTag(u"internal", *url, shown());
    if (m_links.mediaAsLinks)
        return mediaHtml(*url, l.display.isEmpty() ? paths::basename(l.target) : l.display, QString());
    return mediaHtml(*url, l.display, l.target);
}

QString HtmlRenderer::metaChips(const Block &block) const
{
    QStringList chips;
    if (block.task) {
        const Task &t = *block.task;
        if (t.priority != Priority::None)
            chips.append(priorityChip(t.priority));
        if (t.start)
            chips.append(chip(u"scheduled"_s, u"🛫 "_s + t.start->toString()));
        if (t.scheduled)
            chips.append(chip(u"scheduled"_s, u"⏳ "_s + t.scheduled->toString()));
        if (t.due)
            chips.append(chip(u"due"_s, u"📅 "_s + t.due->toString()));
        if (!t.recurrence.isEmpty())
            chips.append(chip(u"recurrence"_s, u"🔁 "_s + t.recurrence));
        if (t.doneDate)
            chips.append(chip(u"done"_s, u"✅ "_s + t.doneDate->toString(u"yyyy-MM-dd"_s)));
        if (t.cancelledDate)
            chips.append(chip(u"done"_s, u"❌ "_s + t.cancelledDate->toString(u"yyyy-MM-dd"_s)));
    } else {
        // A block that is no task keeps the dates, recurrence and priority written in it (the
        // parsed note has them for tasks only; its text lost them with the other tokens).
        using markdown::TokenKind;
        for (const auto tokens = markdown::findTokens(markdown::maskInlineCode(block.raw));
             const markdown::Token &tok : tokens) {
            const QString when =
                    tok.date.toString(u"yyyy-MM-dd"_s) + (tok.time ? u' ' + tok.time->toString(u"HH:mm"_s) : QString());
            switch (tok.kind) {
            case TokenKind::Priority:
                if (tok.priority != Priority::None)
                    chips.append(priorityChip(tok.priority));
                break;
            case TokenKind::Start: chips.append(chip(u"scheduled"_s, u"🛫 "_s + when)); break;
            case TokenKind::Scheduled: chips.append(chip(u"scheduled"_s, u"⏳ "_s + when)); break;
            case TokenKind::Due: chips.append(chip(u"due"_s, u"📅 "_s + when)); break;
            case TokenKind::Done: chips.append(chip(u"done"_s, u"✅ "_s + when)); break;
            case TokenKind::Cancelled: chips.append(chip(u"done"_s, u"❌ "_s + when)); break;
            case TokenKind::Recurrence:
                if (block.reminders.isEmpty()) // else the reminder's chip has it
                    chips.append(chip(u"recurrence"_s, u"🔁 "_s + markdown::recurrenceOf(tok)));
                break;
            default: break;
            }
        }
    }
    for (const Reminder &r : block.reminders) {
        QString text = (r.alarm ? u"⏰! "_s : u"⏰ "_s) + r.at.toString();
        if (!r.recurrence.isEmpty() && !block.task)
            text += u" 🔁 "_s + r.recurrence;
        chips.append(chip(r.alarm ? u"alarm"_s : u"reminder"_s, text));
    }
    QStringList props;
    for (const auto &[key, value] : block.properties.asKeyValueRange()) {
        if (key == u"id" || key == u"collapsed" || key == u"callouttype" || key == u"calloutfold")
            continue;
        const QString shown =
                value.typeId() == QMetaType::QStringList ? value.toStringList().join(u", "_s) : value.toString();
        props.append(u"<span class=\"prop\"><span class=\"key\">"_s + esc(key) + u"</span> "_s + esc(shown)
                     + u"</span>"_s);
    }
    props.sort();
    chips.append(props);
    return chips.isEmpty() ? QString() : u" <span class=\"meta\">"_s + chips.join(u' ') + u"</span>"_s;
}

QString HtmlRenderer::renderNote(const ParsedNote &note) const
{
    QList<int> roots;
    for (int i = 0; i < note.blocks.size(); ++i) {
        if (note.blocks[i].parent < 0)
            roots.append(i);
    }
    return renderBlocks(note, roots);
}

QString HtmlRenderer::renderBlocks(const ParsedNote &note, const QList<int> &indices, int level) const
{
    const QList<int> blocks = listedItems(note, indices, level);
    QString out;
    for (qsizetype k = 0; k < blocks.size();) {
        const int idx = blocks[k];
        const Block &b = note.blocks[idx];
        if (b.kind != BlockKind::ListItem) {
            out += renderBlock(note, idx);
            ++k;
            continue;
        }
        const bool ordered = isOrderedItem(b);
        out += ordered ? u"<ol>"_s : u"<ul>"_s;
        while (k < blocks.size() && note.blocks[blocks[k]].kind == BlockKind::ListItem
               && isOrderedItem(note.blocks[blocks[k]]) == ordered) {
            out += renderListItem(note, blocks[k], level);
            ++k;
        }
        out += ordered ? u"</ol>"_s : u"</ul>"_s;
    }
    return out;
}

QString HtmlRenderer::renderListItem(const ParsedNote &note, int index, int level) const
{
    const Block &b = note.blocks[index];
    QString out = u"<li"_s;
    if (b.task)
        out += u" class=\"task "_s + statusName(b.task->status) + u'"';
    if (!b.id.isEmpty())
        out += u" id="_s + attr(m_links.idPrefix + blockAnchor(b.id));
    out += u'>';
    if (b.task) {
        const bool checked = b.task->status == TaskStatus::Done || b.task->status == TaskStatus::Cancelled;
        out += u"<input type=\"checkbox\" disabled"_s + (checked ? u" checked"_s : QString()) + u"> "_s;
    }
    // A list item that is one fenced block (`/Math formula` in an outline or a journal writes
    // "- ```math") is shown as that block, like the editor does.
    const auto fence = b.task ? std::nullopt : itemFence(b.text);
    const QString embedded = b.task || fence ? QString() : embeddedContent(b.text);
    if (fence)
        out += fenceHtml(note, b.line, fence->first, fence->second, QString());
    else if (!embedded.isEmpty())
        out += u"<div class=\"embed\">"_s + embedded + u"</div>"_s;
    else
        out += renderInline(b.text) + metaChips(b);
    if (!b.children.isEmpty() && level + 1 < MaxListLevels) // (else listedItems() lists them after it)
        out += renderBlocks(note, b.children, level + 1);
    return out + u"</li>"_s;
}

QString HtmlRenderer::embeddedContent(const QString &text) const
{
    if (!m_links.embed)
        return {};
    static const QRegularExpression onlyEmbed(u"^!\\[\\[([^\\]|#]+)(?:#(\\^?)([^\\]|]+))?(?:\\|[^\\]]*)?\\]\\]$"_s);
    const auto m = onlyEmbed.match(text.trimmed());
    if (!m.hasMatch() || isAttachmentTarget(m.captured(1)))
        return {};
    const bool isBlock = m.capturedLength(2) > 0;
    const QString frag = m.captured(3);
    return m_links.embed(m.captured(1).trimmed(), isBlock ? QString() : frag, isBlock ? frag : QString());
}

QString HtmlRenderer::renderTable(const QString &source) const
{
    const QStringList rows = source.split(u'\n', Qt::SkipEmptyParts);
    if (rows.isEmpty())
        return {};
    const QStringList delimiter = rows.size() > 1 ? tableCells(rows[1]) : QStringList();
    const bool hasDelimiter =
            rows.size() > 1 && delimiter.join(QString()).remove(u'-').remove(u':').trimmed().isEmpty();
    QStringList aligns;
    if (hasDelimiter) {
        for (const QString &cell : delimiter) {
            const bool left = cell.startsWith(u':');
            const bool right = cell.endsWith(u':');
            aligns.append(left && right ? u"center"_s : right ? u"right"_s : left ? u"left"_s : QString());
        }
    }
    const auto cellHtml = [&](const QString &tag, const QString &cell, qsizetype column) -> QString {
        const QString align = column < aligns.size() ? aligns[column] : QString();
        const QString style = align.isEmpty() ? QString() : u" style=\"text-align:"_s + align + u'"';
        const QString content = cell.startsWith(u'=') ? u"<code>"_s + esc(cell) + u"</code>"_s : renderInline(cell);
        return u'<' + tag + style + u'>' + content + u"</"_s + tag + u'>';
    };
    QString out = u"<table><thead><tr>"_s;
    const QStringList header = tableCells(rows.first());
    for (qsizetype c = 0; c < header.size(); ++c)
        out += cellHtml(u"th"_s, header[c], c);
    out += u"</tr></thead><tbody>"_s;
    for (qsizetype r = hasDelimiter ? 2 : 1; r < rows.size(); ++r) {
        out += u"<tr>"_s;
        const QStringList cells = tableCells(rows[r]);
        for (qsizetype c = 0; c < cells.size(); ++c)
            out += cellHtml(u"td"_s, cells[c], c);
        out += u"</tr>"_s;
    }
    return out + u"</tbody></table>"_s;
}

QString HtmlRenderer::fenceHtml(const ParsedNote &note, int line, const QString &lang, const QString &code,
                                const QString &id) const
{
    if (m_links.diagramsAsSource || lang.compare(u"chart", Qt::CaseInsensitive) != 0)
        return codeBlockHtml(lang, code, id, m_links.diagramsAsSource);
    // The page's tables and calc blocks, also those of list items (formulas see the calc blocks
    // above them).
    chart::PageSources page;
    page.chartLine = line;
    for (const Block &other : note.blocks) {
        if (other.kind == BlockKind::Table) {
            page.tables.append({other.line, other.text});
        } else if (other.kind == BlockKind::Code) {
            if (other.lang.section(u' ', 0, 0).compare(u"calc", Qt::CaseInsensitive) == 0)
                page.calcs.append({other.line, other.text});
        } else if (other.kind == BlockKind::ListItem && !other.task) {
            if (const auto fence = itemFence(other.text)) {
                if (fence->first.compare(u"calc", Qt::CaseInsensitive) == 0)
                    page.calcs.append({other.line, fence->second});
            } else if (other.text.startsWith(u'|') && calc::parseTable(other.text)) {
                page.tables.append({other.line, other.text});
            }
        }
    }
    page.runQuery = m_links.chartQuery;
    const chart::Result r = chart::render(code, page);
    if (!r.ok())
        return codeBlockHtml(lang, code, id, m_links.diagramsAsSource);
    // The values in words and the source under the chart.
    return u"<figure class=\"mermaid chart\""_s + id + u'>' + svgHtml(r.scene, chart::typeName(r.spec.type))
           + u"<details><summary>"_s + esc(IoMessages::tr("Chart data and source")) + u"</summary><p>"_s
           + esc(chart::describeChart(r.spec, r.data)) + u"</p><pre><code class=\"language-chart\">"_s + esc(code)
           + u"</code></pre></details></figure>"_s;
}

QString HtmlRenderer::renderBlock(const ParsedNote &note, int index) const
{
    const Block &b = note.blocks[index];
    const QString id = b.id.isEmpty() ? QString() : u" id="_s + attr(m_links.idPrefix + blockAnchor(b.id));
    switch (b.kind) {
    case BlockKind::Frontmatter: return {};
    case BlockKind::Heading: {
        const int level = std::clamp(b.headingLevel, 1, 6);
        const QString anchor = m_links.idPrefix + (b.id.isEmpty() ? headingAnchor(b.text) : blockAnchor(b.id));
        return u"<h"_s + QString::number(level) + u" id="_s + attr(anchor) + u'>' + renderInline(b.text) + metaChips(b)
               + u"</h"_s + QString::number(level) + u'>';
    }
    case BlockKind::Paragraph: {
        // A paragraph that is only a page embed shows the embedded page.
        if (const QString embedded = embeddedContent(b.text); !embedded.isEmpty())
            return u"<div class=\"embed\""_s + id + u'>' + embedded + u"</div>"_s;
        return u"<p"_s + id + u'>' + renderInline(b.text) + metaChips(b) + u"</p>"_s;
    }
    case BlockKind::Code: return fenceHtml(note, b.line, b.lang.section(u' ', 0, 0), b.text, id);
    case BlockKind::Table: return u"<div class=\"table\""_s + id + u'>' + renderTable(b.text) + u"</div>"_s;
    case BlockKind::Quote: {
        if (!b.children.isEmpty())
            return u"<blockquote"_s + id + u'>' + renderBlocks(note, b.children) + u"</blockquote>"_s;
        // The lines without their quote marks; a blank one ends a paragraph.
        QString out = u"<blockquote"_s + id + u'>';
        QStringList paragraph;
        const auto endParagraph = [&] {
            if (!paragraph.isEmpty())
                out += u"<p>"_s + renderInline(paragraph.join(u'\n')) + u"</p>"_s;
            paragraph.clear();
        };
        for (const QStringView line : qTokenize(b.text, u'\n')) {
            QStringView v = line.trimmed();
            while (v.startsWith(u'>'))
                v = v.sliced(1).trimmed();
            if (v.isEmpty())
                endParagraph();
            else
                paragraph.append(v.toString());
        }
        endParagraph();
        return out + u"</blockquote>"_s;
    }
    case BlockKind::Callout: {
        const QString type = b.properties.value(u"callouttype"_s).toString();
        QString out = u"<div class=\"callout callout-"_s + esc(type.isEmpty() ? u"note"_s : type) + u'"' + id
                      + u"><div class=\"callout-title\">"_s + renderInline(calloutTitle(b)) + u"</div>"_s;
        const QString rest = b.text.section(u'\n', 1);
        if (!rest.trimmed().isEmpty())
            out += u"<p>"_s + renderInline(rest.trimmed()) + u"</p>"_s;
        if (!b.children.isEmpty())
            out += renderBlocks(note, b.children);
        return out + u"</div>"_s;
    }
    case BlockKind::Rule: return u"<hr>"_s;
    case BlockKind::Html: return u"<pre class=\"raw-html\""_s + id + u'>' + esc(b.text) + u"</pre>"_s;
    case BlockKind::ListItem: return u"<ul>"_s + renderListItem(note, index, 0) + u"</ul>"_s;
    }
    return {};
}

} // namespace brulion::io::detail
