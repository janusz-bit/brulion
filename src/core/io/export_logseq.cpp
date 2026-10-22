// Logseq exporter: a graph folder in a ZIP — pages/a___b.md, journals/yyyy_MM_dd.md, assets/,
// logseq/config.edn. Every block becomes a bullet, checkboxes become TODO/DOING/DONE/CANCELED/
// WAITING, 📅/⏳ become DEADLINE/SCHEDULED planning lines (🔁 as repeater), priorities [#A]-[#C],
// block ids `id::` UUIDs (deterministic) with references rewritten, embeds {{embed …}},
// frontmatter page properties.
#include "dates/recurrence.h"
#include "index/vaultindex.h"
#include "io/exporters_p.h"
#include "io/exportutil_p.h"
#include "io/importutil_p.h"
#include "io/links_p.h"
#include "io/logseq_p.h"
#include "io/messages_p.h"
#include "markdown/inline.h"
#include "markdown/parse.h"
#include "markdown/tasks.h"
#include "markdown/tokens.h"
#include "text/text.h"
#include "util/paths.h"

#include <QRegularExpression>

#include <algorithm>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

int visualColumn(QStringView line, qsizetype end)
{
    int col = 0;
    for (qsizetype i = 0; i < end && i < line.size(); ++i)
        col = line[i] == u'\t' ? col + 4 - (col % 4) : col + 1;
    return col;
}

/// Remove up to `columns` columns of leading whitespace.
QString dedent(const QString &line, int columns)
{
    qsizetype i = 0;
    int col = 0;
    while (i < line.size() && (line[i] == u' ' || line[i] == u'\t') && col < columns) {
        col = line[i] == u'\t' ? col + 4 - (col % 4) : col + 1;
        ++i;
    }
    return line.sliced(i);
}

QString keyword(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Todo: return u"TODO"_s;
    case TaskStatus::Doing: return u"DOING"_s;
    case TaskStatus::Done: return u"DONE"_s;
    case TaskStatus::Cancelled: return u"CANCELED"_s;
    case TaskStatus::Deferred: return u"WAITING"_s;
    }
    return u"TODO"_s;
}

QString priorityLetter(Priority p)
{
    switch (p) {
    case Priority::High: return u"A"_s;
    case Priority::Medium: return u"B"_s;
    case Priority::Low: return u"C"_s;
    case Priority::Highest:
    case Priority::Lowest:
    case Priority::None: break;
    }
    return {};
}

/// A Logseq property key: lower case, [a-z0-9_-] only.
QString propertyKey(const QString &key)
{
    QString out;
    for (const QChar c : key.trimmed().toLower()) {
        if (c.isLetterOrNumber() || c == u'_' || c == u'-')
            out += c;
        else if (c.isSpace())
            out += u'-';
    }
    return out;
}

QString propertyValue(const QVariant &value, bool tags)
{
    QStringList items;
    if (value.typeId() == QMetaType::QStringList || value.typeId() == QMetaType::QVariantList) {
        for (const QVariant &v : value.toList()) {
            if (v.typeId() == QMetaType::QVariantMap || v.typeId() == QMetaType::QVariantList)
                continue;
            items.append(v.toString().trimmed());
        }
    } else if (value.typeId() == QMetaType::QVariantMap || !value.isValid()) {
        return {};
    } else if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? u"true"_s : u"false"_s;
    } else {
        items.append(value.toString().trimmed());
    }
    items.removeAll(QString());
    if (tags) {
        for (QString &t : items) {
            t.remove(QRegularExpression(u"^#"_s));
            if (t.contains(u' ') || t.contains(u','))
                t = u"[["_s + t + u"]]"_s;
        }
    }
    return items.join(u", "_s);
}

/// First line of a block outside lists written as a bullet: a leading `[ ]` or `TODO` would make
/// the bullet a task (in Logseq and in Brulion), so the space after the marker becomes a
/// no-break space — the same text on screen, no task in either app.
QString withoutTaskMarker(const QString &line)
{
    const auto marker = markdown::matchTaskMarker(line, markdown::maskInlineCode(line), 0);
    if (!marker)
        return line;
    QString out = line;
    if (marker->to < out.size() && (out[marker->to] == u' ' || out[marker->to] == u'\t'))
        out[marker->to] = QChar(0x00A0);
    else
        out.insert(marker->to, QChar(0x00A0));
    return out;
}

struct ConvertedLine {
    QString text;         ///< the bullet's first line (without "- ")
    QStringList planning; ///< SCHEDULED: / DEADLINE: lines
    QString id;           ///< block id removed from the line
};

/// First line of a list item: task marker -> keyword, emoji dates -> planning, priority -> [#X].
ConvertedLine convertFirstLine(QString content)
{
    ConvertedLine out;
    if (const auto caret = markdown::blockIdAtEnd(content)) {
        out.id = caret->id;
        content = content.first(caret->from);
    }
    const QString masked = markdown::maskInlineCode(content);
    const auto marker = markdown::matchTaskMarker(content, masked, 0);
    if (!marker) {
        out.text = content;
        return out;
    }
    QString word = marker->kind == markdown::TaskMarker::Kind::Keyword ? marker->marker : keyword(marker->status);
    QString rest = content.sliced(marker->contentStart);

    struct Removal {
        qsizetype from;
        qsizetype to;
    };
    QList<Removal> removals;
    std::optional<DateSpec> due;
    std::optional<DateSpec> scheduled;
    QString repeater;
    Priority priority = Priority::None;
    for (const markdown::Token &t : markdown::findTokens(markdown::maskInlineCode(rest))) {
        switch (t.kind) {
        case markdown::TokenKind::Due:
            if (!due) {
                due = t.spec();
                removals.append({t.from, t.to});
            }
            break;
        case markdown::TokenKind::Scheduled:
            if (!scheduled) {
                scheduled = t.spec();
                removals.append({t.from, t.to});
            }
            break;
        case markdown::TokenKind::Priority:
            // Logseq knows A/B/C only: 🔺 / ⏬ stay as written (shown as text, read back exactly).
            if (priority == Priority::None && t.priority != Priority::Highest && t.priority != Priority::Lowest) {
                priority = t.priority;
                removals.append({t.from, t.to});
            }
            break;
        case markdown::TokenKind::Recurrence: {
            const auto rule = dates::parseRecurrence(t.value.isEmpty() ? t.canonical : t.value);
            const QString rep = rule ? dates::ruleToRepeater(*rule) : QString();
            if (!rep.isEmpty() && repeater.isEmpty()) {
                repeater = rep;
                removals.append({t.from, t.to});
            }
            break;
        }
        default: break;
        }
    }
    // A repeater needs a date; without one the 🔁 token stays as text.
    if (!due && !scheduled && !repeater.isEmpty()) {
        repeater.clear();
        removals.erase(std::remove_if(removals.begin(), removals.end(),
                                      [&](const Removal &r) {
                                          return QStringView(rest).sliced(r.from).startsWith(
                                                  markdown::tokens::Recurrence);
                                      }),
                       removals.end());
    }
    std::sort(removals.begin(), removals.end(), [](const Removal &a, const Removal &b) { return a.from > b.from; });
    for (const Removal &r : std::as_const(removals)) {
        qsizetype from = r.from;
        if (from > 0 && rest[from - 1] == u' ')
            --from;
        rest.remove(from, r.to - from);
    }
    rest = rest.trimmed();
    const QString letter = priorityLetter(priority);
    out.text = word + (letter.isEmpty() ? QString() : u" [#"_s + letter + u']')
               + (rest.isEmpty() ? QString() : u' ' + rest);
    if (scheduled)
        out.planning.append(u"SCHEDULED: "_s + logseqTimestamp(*scheduled, repeater));
    if (due)
        out.planning.append(u"DEADLINE: "_s + logseqTimestamp(*due, scheduled ? QString() : repeater));
    return out;
}

class LogseqZipExporter final : public Exporter {
public:
    QString id() const override { return u"logseq-zip"_s; }
    QString name() const override { return IoMessages::tr("Logseq (ZIP)"); }
    QString fileExtension() const override { return u".zip"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        const ExportVault vault(input);
        Context ctx{vault, {}};
        PathClaims claims;
        const QString attachmentsPrefix = paths::normalize(vault.config().attachmentsFolder) + u'/';
        for (const FileEntry &a : vault.attachments()) {
            QString rel = a.path;
            if (attachmentsPrefix.size() > 1 && rel.startsWith(attachmentsPrefix))
                rel = rel.sliced(attachmentsPrefix.size());
            ctx.assets.insert(a.path, claims.claim(u"assets/"_s + QString(rel).replace(u'/', u'_')));
        }
        const QString root = paths::sanitizeFileName(vault.vaultName()) + u'/';
        QList<FileEntry> entries;
        PathClaims pageFiles;
        for (const ExportVault::Note &n : vault.notes()) {
            const QString file =
                    n.journal ? u"journals/"_s + n.journal->toString(u"yyyy_MM_dd"_s) + u".md"_s
                              : u"pages/"_s + logseqFileNameForPage(paths::stripMarkdownExtension(n.path)) + u".md"_s;
            entries.append(FileEntry{root + pageFiles.claim(file), convert(ctx, n).toUtf8()});
        }
        for (const FileEntry &a : vault.attachments())
            entries.append(FileEntry{root + ctx.assets.value(a.path), a.data});
        entries.append(FileEntry{root + u"logseq/config.edn"_s, configEdn()});
        ExportResult r;
        r.files.append(ExportedFile{vault.fileName(u"logseq"_s, u".zip"_s), u"application/zip"_s, vault.zip(entries)});
        return r;
    }

private:
    struct Context {
        const ExportVault &vault;
        QHash<VaultPath, QString> assets; ///< vault path -> "assets/<flat name>"
    };

    static QByteArray configEdn()
    {
        return QByteArrayLiteral("{:meta/version 1\n"
                                 " :preferred-format \"Markdown\"\n"
                                 " :journal/page-title-format \"yyyy-MM-dd\"\n"
                                 " :journal/file-name-format \"yyyy_MM_dd\"\n"
                                 " :file/name-format :triple-lowbar}\n");
    }

    static QString pageName(const ExportVault &vault, const VaultPath &path)
    {
        if (const ExportVault::Note *n = vault.note(path); n && n->journal)
            return n->journal->toString(u"yyyy-MM-dd"_s);
        return paths::stripMarkdownExtension(path);
    }

    static QString convert(const Context &ctx, const ExportVault::Note &note)
    {
        const ParsedNote &parsed = *note.parsed;
        const QList<text::Line> lines = text::splitLines(note.content);
        QStringList out;
        pageProperties(parsed, lines, out);
        for (int i = 0; i < parsed.blocks.size(); ++i) {
            const Block &b = parsed.blocks[i];
            if (b.parent >= 0 || b.kind == BlockKind::Frontmatter)
                continue;
            if (b.kind == BlockKind::ListItem)
                emitItem(parsed, i, lines, 0, out);
            else
                emitLoose(b, lines, 0, out);
        }
        return rewriteTags(ctx, rewriteLinks(ctx, note, out.join(u'\n'))) + u'\n';
    }

    static void pageProperties(const ParsedNote &parsed, const QList<text::Line> &lines, QStringList &out)
    {
        for (const Block &b : parsed.blocks) {
            if (b.kind != BlockKind::Frontmatter || b.parent >= 0)
                continue;
            if (!b.raw.startsWith(u"---"_s)) {
                // Logseq-style page properties: already in the right syntax.
                for (int l = b.line; l <= b.ownEndLine && l < lines.size(); ++l) {
                    if (!lines[l].text.trimmed().isEmpty())
                        out.append(lines[l].text.trimmed());
                }
            }
        }
        const QVariantMap &fm = parsed.frontmatter;
        QStringList keys = fm.keys();
        const auto order = [](const QString &k) {
            return k == u"aliases" || k == u"alias" ? 0 : k == u"tags" || k == u"tag" ? 1 : 2;
        };
        std::stable_sort(keys.begin(), keys.end(),
                         [&](const QString &a, const QString &b) { return order(a) < order(b); });
        for (const QString &key : std::as_const(keys)) {
            const bool tags = key == u"tags" || key == u"tag";
            const QString name = key == u"aliases" || key == u"alias" ? u"alias"_s
                                 : tags                               ? u"tags"_s
                                                                      : propertyKey(key);
            const QString value = propertyValue(fm.value(key), tags);
            if (!name.isEmpty() && !value.isEmpty())
                out.append(name + u":: "_s + value);
        }
        if (!out.isEmpty())
            out.append(QString());
    }

    static void emitItem(const ParsedNote &parsed, int index, const QList<text::Line> &lines, int depth,
                         QStringList &out)
    {
        static const QRegularExpression bullet(u"^([ \\t]*)([-*+]|(\\d{1,9})[.)])([ \\t]+|$)"_s);
        const Block &b = parsed.blocks[index];
        if (b.line >= lines.size())
            return;
        const QString &firstRaw = lines[b.line].text;
        const auto m = bullet.match(firstRaw);
        const qsizetype contentStart = m.hasMatch() ? m.capturedEnd() : 0;
        const int contentCol = visualColumn(firstRaw, contentStart);
        const bool ordered = m.hasMatch() && m.capturedLength(3) > 0;
        const ConvertedLine first = convertFirstLine(firstRaw.sliced(contentStart));
        QString id = first.id;
        QStringList rest;
        for (int l = b.line + 1; l <= b.ownEndLine && l < lines.size(); ++l) {
            QString line = dedent(lines[l].text, contentCol);
            if (id.isEmpty() && l == b.ownEndLine) {
                if (const auto caret = markdown::blockIdAtEnd(line)) {
                    id = caret->id;
                    line = line.first(caret->from);
                }
            }
            if (!b.id.isEmpty() && markdown::standaloneBlockId(line) == b.id) {
                id = b.id;
                continue;
            }
            rest.append(line);
        }
        if (id.isEmpty() && !b.id.isEmpty() && !b.properties.contains(u"id"_s))
            id = b.id;
        const QString indent(depth, u'\t');
        out.append(indent + u"- "_s + first.text);
        const QString cont = indent + u"  "_s;
        if (ordered)
            out.append(cont + u"logseq.order-list-type:: number"_s);
        if (!id.isEmpty())
            out.append(cont + u"id:: "_s + logseqUuid(id));
        for (const QString &p : first.planning)
            out.append(cont + p);
        static const QRegularExpression idProperty(u"^id::\\s*(\\S+)\\s*$"_s,
                                                   QRegularExpression::CaseInsensitiveOption);
        for (const QString &line : std::as_const(rest)) {
            // An existing `id::` gets the same UUID as references to it.
            if (const auto pm = idProperty.match(line.trimmed()); pm.hasMatch()) {
                out.append(cont + u"id:: "_s + logseqUuid(pm.captured(1)));
                continue;
            }
            out.append(line.trimmed().isEmpty() ? QString() : cont + line);
        }
        for (int child : b.children) {
            if (parsed.blocks[child].kind == BlockKind::ListItem)
                emitItem(parsed, child, lines, depth + 1, out);
            else
                emitLoose(parsed.blocks[child], lines, depth + 1, out);
        }
    }

    /// A block outside lists (paragraph, heading, code, table, quote, …) as one bullet.
    static void emitLoose(const Block &b, const QList<text::Line> &lines, int depth, QStringList &out)
    {
        QStringList source;
        for (int l = b.line; l <= b.endLine && l < lines.size(); ++l)
            source.append(lines[l].text);
        QString id;
        while (!source.isEmpty() && source.last().trimmed().isEmpty())
            source.removeLast();
        if (!source.isEmpty() && !b.id.isEmpty()) {
            if (markdown::standaloneBlockId(source.last()) == b.id) {
                source.removeLast();
                id = b.id;
            } else if (const auto caret = markdown::blockIdAtEnd(source.last()); caret && caret->id == b.id) {
                source.last() = source.last().first(caret->from);
                id = b.id;
            } else if (const auto head = markdown::blockIdAtEnd(source.first()); head && head->id == b.id) {
                source.first() = source.first().first(head->from);
                id = b.id;
            }
        }
        if (source.isEmpty())
            return;
        int common = -1;
        for (const QString &s : std::as_const(source)) {
            if (s.trimmed().isEmpty())
                continue;
            qsizetype k = 0;
            while (k < s.size() && (s[k] == u' ' || s[k] == u'\t'))
                ++k;
            const int col = visualColumn(s, k);
            common = common < 0 ? col : std::min(common, col);
        }
        common = std::max(common, 0);
        const QString indent(depth, u'\t');
        out.append(indent + u"- "_s + withoutTaskMarker(dedent(source.first(), common)));
        const QString cont = indent + u"  "_s;
        // The id property follows a title line; inside a code fence, a table, HTML or a quote it
        // would become content, so there it goes after the block (as Logseq writes it).
        const bool idAfter = b.kind == BlockKind::Code || b.kind == BlockKind::Table || b.kind == BlockKind::Html
                             || b.kind == BlockKind::Quote || b.kind == BlockKind::Callout;
        if (!id.isEmpty() && !idAfter)
            out.append(cont + u"id:: "_s + logseqUuid(id));
        for (qsizetype k = 1; k < source.size(); ++k)
            out.append(source[k].trimmed().isEmpty() ? QString() : cont + dedent(source[k], common));
        if (!id.isEmpty() && idAfter)
            out.append(cont + u"id:: "_s + logseqUuid(id));
    }

    /// A tag names a page (SYNTAX §3): when Logseq knows that page under another name (a page in a
    /// folder is the namespace "Projekty/Dom"), the tag says it — `#[[Projekty/Dom]]`, a
    /// `tags::` item `[[Projekty/Dom]]`. Aliases stay (Logseq resolves the exported `alias::`).
    static QString rewriteTags(const Context &ctx, const QString &text)
    {
        const ExportVault &vault = ctx.vault;
        QString out;
        qsizetype last = 0;
        for (const markdown::NoteTag &t : markdown::findNoteTags(text)) {
            if (t.from < last || t.form == markdown::TagForm::Frontmatter)
                continue;
            const auto page = vault.index().resolveTag(t.name);
            const ExportVault::Note *target = page ? vault.note(*page) : nullptr;
            if (!target || !target->parsed)
                continue;
            const QString key = t.name.toLower();
            const bool alias = std::any_of(
                    target->parsed->aliases.cbegin(), target->parsed->aliases.cend(),
                    [&](const QString &a) { return a.normalized(QString::NormalizationForm_C).toLower() == key; });
            const QString name = pageName(vault, *page);
            if (alias || name.compare(t.name, Qt::CaseInsensitive) == 0 || markdown::tagSyntax(name).isEmpty())
                continue;
            const bool property = t.form == markdown::TagForm::Property;
            out += QStringView(text).sliced(last, t.from - last);
            out += (property ? u"[["_s : u"#[["_s) + name + u"]]"_s;
            last = t.to;
        }
        if (last == 0)
            return text;
        out += QStringView(text).sliced(last);
        return out;
    }

    static QString rewriteLinks(const Context &ctx, const ExportVault::Note &note, const QString &text)
    {
        const ExportVault &vault = ctx.vault;
        const auto asset = [&](const VaultPath &path) -> QString { return u"../"_s + ctx.assets.value(path); };
        return replaceLinks(text, [&](const FoundLink &l) -> std::optional<QString> {
            switch (l.kind) {
            case LinkKind::BlockRef: return u"(("_s + logseqUuid(l.target) + u"))"_s;
            case LinkKind::BlockEmbed: return u"{{embed (("_s + logseqUuid(l.target) + u"))}}"_s;
            case LinkKind::Wiki:
            case LinkKind::Embed: {
                const bool embed = l.kind == LinkKind::Embed;
                if (!l.blockId.isEmpty()) {
                    const QString ref = u"(("_s + logseqUuid(l.blockId) + u"))"_s;
                    return embed ? u"{{embed "_s + ref + u"}}"_s : ref;
                }
                if (l.target.isEmpty())
                    return l.display.isEmpty() ? l.heading : l.display;
                const auto resolved = vault.resolve(l.target, note.path);
                if (resolved && vault.isAttachment(*resolved)) {
                    const QString label = l.display.isEmpty() ? paths::basename(*resolved) : l.display;
                    return markdownLink(label, encodeLinkPath(asset(*resolved)), embed);
                }
                const QString page = resolved ? pageName(vault, *resolved) : paths::stripMarkdownExtension(l.target);
                if (embed)
                    return u"{{embed [["_s + page + u"]]}}"_s;
                if (!l.display.isEmpty() && l.display.compare(page, Qt::CaseInsensitive) != 0)
                    return u'[' + l.display + u"]([["_s + page + u"]])"_s;
                return u"[["_s + page + u"]]"_s;
            }
            case LinkKind::Markdown: {
                if (l.hasUrlScheme)
                    return std::nullopt;
                const auto resolved = vault.resolve(l.target, note.path);
                if (!resolved)
                    return std::nullopt;
                if (vault.isAttachment(*resolved))
                    return markdownLink(l.display, encodeLinkPath(asset(*resolved)), l.image, l.title);
                const QString page = pageName(vault, *resolved);
                if (l.display.trimmed().isEmpty() || l.display.trimmed().compare(page, Qt::CaseInsensitive) == 0)
                    return u"[["_s + page + u"]]"_s;
                return u'[' + l.display + u"]([["_s + page + u"]])"_s;
            }
            default: return std::nullopt;
            }
        });
    }
};

} // namespace

std::unique_ptr<Exporter> makeLogseqZipExporter()
{
    return std::make_unique<LogseqZipExporter>();
}

} // namespace brulion::io::detail
