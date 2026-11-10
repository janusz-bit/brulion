#include "commands_p.h"

#include "index/vaultindex.h"
#include "markdown/blockdocument.h"
#include "markdown/journal.h"
#include "markdown/linkformat.h"
#include "notebook/journals.h"
#include "notebook/periodicnotes.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QFileInfo>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::cli {

Arg option(const QString &name, ArgType type, const char *help, bool required)
{
    Arg a;
    a.name = name;
    a.type = type;
    a.help = cliText(help);
    a.required = required;
    return a;
}

Arg positional(const QString &name, ArgType type, const char *help, bool required, bool rest)
{
    Arg a = option(name, type, help, required);
    a.positional = true;
    a.rest = rest;
    return a;
}

Arg flag(const QString &name, const char *help)
{
    return option(name, ArgType::Bool, help);
}

Arg choice(const QString &name, const QStringList &values, const char *help, bool isPositional, bool required)
{
    Arg a = isPositional ? positional(name, ArgType::Enum, help, required)
                         : option(name, ArgType::Enum, help, required);
    a.values = values;
    return a;
}

Arg stdinable(Arg arg)
{
    arg.fromStdin = true;
    return arg;
}

Error problemError(const notebook::PathProblem &p)
{
    using P = notebook::PathProblem;
    const QJsonObject details = p.name.isEmpty() ? QJsonObject() : QJsonObject{{u"name"_s, p.name}};
    switch (p.kind) {
    case P::None: break;
    case P::Missing: return {ExitNotFound, u"not_found"_s, tr("%1 does not exist").arg(p.name), details};
    case P::NoName: return {ExitUsage, u"name_required"_s, tr("Give a usable name"), details};
    case P::IntoItself: return {ExitUsage, u"into_itself"_s, tr("A folder cannot be moved into itself"), details};
    case P::HiddenPath:
    case P::HiddenFolder:
        return {ExitUsage, u"hidden_name"_s, tr("%1 would be hidden: choose another name").arg(p.name), details};
    case P::NotAFolder: return {ExitUsage, u"not_a_folder"_s, tr("%1 is a file, not a folder").arg(p.name), details};
    case P::FolderExists:
    case P::PageExists:
    case P::FileExists: return {ExitConflict, u"target_exists"_s, tr("%1 already exists there").arg(p.name), details};
    case P::Failed: return {ExitInternal, u"io_error"_s, p.detail, details};
    }
    return {ExitInternal, u"internal"_s, tr("Something went wrong")};
}

Result failWith(const Error &error)
{
    Result r;
    r.error = error;
    return r;
}

namespace {

/// A block's text as the body of a new list item: no marker, continuation lines unindented.
QString itemBody(const QString &block)
{
    static const QRegularExpression marker(u"^([-*+]|\\d+[.)])[ \\t]+"_s);
    QStringList lines = block.split(u'\n');
    if (!lines.isEmpty())
        lines.first().remove(marker);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        if (lines.at(i).startsWith(u"  "))
            lines[i].remove(0, 2);
    }
    return lines.join(u'\n');
}

} // namespace

std::optional<QString> insertUnder(Call &call, const QString &content, const QString &rawUnder, const QString &block,
                                   int *insertedLine)
{
    markdown::BlockDocument doc(content);
    const QString under = rawUnder.trimmed();
    int target = -1;
    if (under.startsWith(u'^') || under.startsWith(u"((")) {
        // A malformed ref ("((id" without its "))" reads as a page name) has no block id: it must
        // not match the blocks without one.
        if (const auto ref = notebook::parseItemRef(under); ref && ref->hasBlock()) {
            for (int i = 0; i < doc.count(); ++i) {
                if (doc.at(i).blockId == ref->blockId)
                    target = i;
            }
        }
    } else {
        QList<Heading> headings;
        QList<int> rows;
        for (int i = 0; i < doc.count(); ++i) {
            if (const markdown::BlockEntry &e = doc.at(i); e.headingLevel > 0) {
                headings.append(Heading{e.headingLevel, e.body.section(u'\n', 0, 0), e.line});
                rows.append(i);
            }
        }
        QStringView fragment = under;
        while (fragment.startsWith(u'#'))
            fragment = fragment.sliced(1);
        const int h = markdown::findHeading(headings, fragment.trimmed());
        target = h >= 0 ? rows.at(h) : -1;
    }
    if (target < 0) {
        call.setError(ExitNotFound, u"under_not_found"_s, tr("No heading or block %1 in the page").arg(under),
                      {{u"under"_s, under}});
        return std::nullopt;
    }
    const markdown::BlockEntry entry = doc.at(target);
    int after = target;
    int depth = 0;
    if (entry.headingLevel > 0 && entry.prefix.isEmpty()) {
        // A heading: the end of its section (before the next heading of its level or higher).
        after = doc.count() - 1;
        for (int i = target + 1; i < doc.count(); ++i) {
            const markdown::BlockEntry &e = doc.at(i);
            if (e.headingLevel > 0 && e.prefix.isEmpty() && e.headingLevel <= entry.headingLevel) {
                after = i - 1;
                break;
            }
        }
    } else if (entry.kind == BlockKind::ListItem) {
        after = doc.subtreeRange(target).last(); // the block's last child
        depth = entry.depth + 1;
    }
    const markdown::EditResult edit = doc.insertAfter(after, itemBody(block), depth, true);
    if (insertedLine)
        *insertedLine = edit.focus >= 0 ? doc.at(edit.focus).line : -1;
    return doc.toString();
}

notebook::Notebook::WriteOptions creatingJournal(Call &call, const VaultPath &path)
{
    notebook::Notebook::WriteOptions options;
    Vault *v = call.vault();
    const std::optional<QDate> day = v ? markdown::journalDateFromPath(path, v->config()) : std::nullopt;
    // A weekly or monthly note is created from its own template (notebook/periodicnotes.h).
    const auto period = v && !day ? periodic::periodicNoteFromPath(path, v->config()) : std::nullopt;
    if (!day && !period)
        return options;
    options.mayCreate = true;
    if (!QFileInfo(v->absolutePath(path)).isFile())
        options.initialText =
                day ? notebook::newJournalText(*v, *day, call.ctx.now(), call.ctx.locale())
                    : notebook::newPeriodicText(*v, period->period, period->start, call.ctx.now(), call.ctx.locale());
    return options;
}

int appendedLine(const QString &after, const QString &block)
{
    const QList<text::Line> lines = text::splitLines(after);
    qsizetype last = lines.size() - 1;
    if (last > 0 && lines.at(last).text.isEmpty() && lines.at(last).eol.isEmpty())
        --last;
    return int(last - block.count(u'\n'));
}

QString lineText(const QString &content, int line)
{
    const QList<text::Line> lines = text::splitLines(content);
    return line >= 0 && line < lines.size() ? lines.at(line).text : QString();
}

QString noteText(Call &call, const VaultPath &path)
{
    VaultIndex *index = call.index();
    const QString indexed = index ? index->content(path) : QString();
    if (!indexed.isNull())
        return indexed;
    Vault *v = call.vault();
    return v ? v->readText(path) : QString();
}

QJsonArray backlinksJson(Call &call, const VaultPath &path)
{
    QJsonArray out;
    VaultIndex *index = call.index();
    if (!index)
        return out;
    const VaultConfig &config = index->config();
    // {kind, path, title, journal, text, blockId?} of a mention in the note at `p`, with the block
    // `blockIndex` of that note it is in (nullptr: none).
    const auto mention = [&](const QString &kind, const VaultPath &p, int blockIndex) {
        const ParsedNote *note = index->note(p);
        const Block *block =
                note && blockIndex >= 0 && blockIndex < note->blocks.size() ? &note->blocks.at(blockIndex) : nullptr;
        const auto day = markdown::journalDateFromPath(p, config);
        QJsonObject o{{u"kind"_s, kind},
                      {u"path"_s, p},
                      {u"title"_s, note ? note->title : paths::pageTitle(p)},
                      {u"journal"_s, day ? day->toString(Qt::ISODate) : QString()},
                      {u"text"_s, block ? block->text.simplified() : QString()}};
        if (block && !block->id.isEmpty())
            o.insert(u"blockId"_s, block->id);
        return std::pair{o, block};
    };
    for (const QList<Backlink> links = index->backlinks(path); const Backlink &b : links) {
        QJsonObject o = mention(u"link"_s, b.path, b.blockIndex).first;
        o.insert(u"line"_s, b.link.line);
        o.insert(u"ref"_s, Call::lineRef(b.path, noteText(call, b.path), b.link.line));
        out.append(o);
    }
    for (const QList<TagReference> tags = index->tagReferences(path); const TagReference &t : tags) {
        auto [o, block] = mention(u"tag"_s, t.path, t.blockIndex);
        o.insert(u"tag"_s, t.tag);
        o.insert(u"line"_s, block ? block->line : -1); // -1: the page as a whole (frontmatter tags:)
        if (block)
            o.insert(u"ref"_s, Call::lineRef(t.path, noteText(call, t.path), block->line));
        out.append(o);
    }
    return out;
}

void addRefs(Call &call, QJsonObject &result)
{
    const auto withRefs = [&call](QJsonArray items) {
        for (qsizetype i = 0; i < items.size(); ++i) {
            QJsonObject o = items.at(i).toObject();
            const QString path = o.value(u"path"_s).toString();
            const int line = o.value(u"line"_s).toInt(-1);
            if (!path.isEmpty() && line >= 0 && !o.value(u"raw"_s).toString().isEmpty())
                o.insert(u"ref"_s, Call::lineRef(path, noteText(call, path), line));
            items[i] = o;
        }
        return items;
    };
    result.insert(u"items"_s, withRefs(result.value(u"items"_s).toArray()));
    QJsonArray groups = result.value(u"groups"_s).toArray();
    for (qsizetype i = 0; i < groups.size(); ++i) {
        QJsonObject g = groups.at(i).toObject();
        g.insert(u"items"_s, withRefs(g.value(u"items"_s).toArray()));
        groups[i] = g;
    }
    result.insert(u"groups"_s, groups);
}

QString cleanItemText(const QString &text)
{
    static const QRegularExpression marker(u"^\\s*(?:[-*+]|\\d+[.)])\\s+"_s);
    static const QRegularExpression checkbox(u"^\\[[ xX/>\\-]\\]\\s*"_s);
    static const QRegularExpression breaks(u"\\s*[\\r\\n]+\\s*"_s);
    QString t = text;
    t.replace(breaks, u" "_s);
    t = t.trimmed();
    t.remove(marker);
    t.remove(checkbox);
    return t.trimmed();
}

} // namespace brulion::cli
