#include "commands_p.h"

#include "index/vaultindex.h"
#include "markdown/blockdocument.h"
#include "markdown/journal.h"
#include "markdown/linkformat.h"
#include "notebook/journals.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QFileInfo>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::cli {

Arg positional(const QString &name, ArgType type, const QString &help, bool required, bool rest)
{
    Arg a;
    a.name = name;
    a.type = type;
    a.help = help;
    a.required = required;
    a.positional = true;
    a.rest = rest;
    return a;
}

Arg option(const QString &name, ArgType type, const QString &help, bool required)
{
    Arg a;
    a.name = name;
    a.type = type;
    a.help = help;
    a.required = required;
    return a;
}

Arg flag(const QString &name, const QString &help)
{
    return option(name, ArgType::Bool, help);
}

Arg choice(const QString &name, const QStringList &values, const QString &help, bool isPositional, bool required)
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
        if (lines.at(i).startsWith(u"  "_s))
            lines[i] = lines.at(i).mid(2);
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
        const auto ref = notebook::parseItemRef(under);
        for (int i = 0; ref && i < doc.count(); ++i) {
            if (doc.at(i).blockId == ref->blockId)
                target = i;
        }
    } else {
        QList<Heading> headings;
        QList<int> rows;
        for (int i = 0; i < doc.count(); ++i) {
            if (doc.at(i).headingLevel > 0) {
                headings.append(Heading{doc.at(i).headingLevel, doc.at(i).body.section(u'\n', 0, 0), doc.at(i).line});
                rows.append(i);
            }
        }
        QString fragment = under;
        while (fragment.startsWith(u'#'))
            fragment.remove(0, 1);
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
    if (!day)
        return options;
    options.mayCreate = true;
    if (!QFileInfo(v->absolutePath(path)).isFile())
        options.initialText = notebook::newJournalText(*v, *day, call.ctx.now(), call.ctx.locale());
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
    const VaultConfig config = index->config();
    const auto journal = [&](const VaultPath &p) {
        const auto d = markdown::journalDateFromPath(p, config);
        return d ? d->toString(Qt::ISODate) : QString();
    };
    for (const Backlink &b : index->backlinks(path)) {
        const ParsedNote *note = index->note(b.path);
        QJsonObject o{{u"kind"_s, u"link"_s},
                      {u"path"_s, b.path},
                      {u"title"_s, note ? note->title : paths::pageTitle(b.path)},
                      {u"line"_s, b.link.line},
                      {u"journal"_s, journal(b.path)},
                      {u"text"_s, QString()}};
        if (note && b.blockIndex >= 0 && b.blockIndex < note->blocks.size()) {
            const Block &block = note->blocks.at(b.blockIndex);
            o.insert(u"text"_s, block.text.simplified());
            if (!block.id.isEmpty())
                o.insert(u"blockId"_s, block.id);
        }
        o.insert(u"ref"_s, Call::lineRef(b.path, noteText(call, b.path), b.link.line));
        out.append(o);
    }
    for (const TagReference &t : index->tagReferences(path)) {
        const ParsedNote *note = index->note(t.path);
        QJsonObject o{{u"kind"_s, u"tag"_s},
                      {u"path"_s, t.path},
                      {u"title"_s, note ? note->title : paths::pageTitle(t.path)},
                      {u"tag"_s, t.tag},
                      {u"journal"_s, journal(t.path)}};
        if (note && t.blockIndex >= 0 && t.blockIndex < note->blocks.size()) {
            const Block &block = note->blocks.at(t.blockIndex);
            o.insert(u"line"_s, block.line);
            o.insert(u"text"_s, block.text.simplified());
            if (!block.id.isEmpty())
                o.insert(u"blockId"_s, block.id);
            o.insert(u"ref"_s, Call::lineRef(t.path, noteText(call, t.path), block.line));
        } else {
            o.insert(u"line"_s, -1); // the page as a whole (frontmatter tags:)
            o.insert(u"text"_s, QString());
        }
        out.append(o);
    }
    return out;
}

void addRefs(Call &call, QJsonObject &result)
{
    const auto withRefs = [&](QJsonArray items) {
        for (qsizetype i = 0; i < items.size(); ++i) {
            QJsonObject o = items.at(i).toObject();
            const QString path = o.value(u"path"_s).toString();
            const int line = o.value(u"line"_s).toInt(-1);
            if (!path.isEmpty() && line >= 0 && o.contains(u"raw"_s) && !o.value(u"raw"_s).toString().isEmpty())
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
