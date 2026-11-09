// See cleanup.h.
#include "notebook/cleanup.h"

#include "index/query.h"
#include "index/vaultindex.h"
#include "periodic/periodic.h"
#include "util/paths.h"

#include <QHash>
#include <QSet>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

namespace {

/// Every file some note refers to, by links, `((id))` references, tags and ```query filters; a
/// note's references to itself do not count.
QSet<VaultPath> referencedFiles(const VaultIndex &index)
{
    QSet<VaultPath> referenced;
    // (The index's lists as const locals: a range-for over the copy of a list it keeps would detach it.)
    for (const QList<VaultPath> notes = index.paths(); const VaultPath &from : notes) {
        for (const QList<ResolvedLink> links = index.outgoingLinks(from); const ResolvedLink &link : links) {
            if (!link.target.isEmpty() && link.target != from)
                referenced.insert(link.target);
        }
    }
    for (const QList<TagCount> tags = index.tags(); const TagCount &tag : tags) {
        const std::optional<VaultPath> named = index.resolveTag(tag.tag);
        if (named && std::ranges::any_of(tag.paths, [&](const VaultPath &p) { return p != *named; }))
            referenced.insert(*named);
    }
    for (const QList<VaultPath> notes = index.notesWithQueries(); const VaultPath &from : notes) {
        const QList<QueryNameRef> names = findQueryNames(index.content(from), index.note(from));
        for (const QueryNameRef &name : names) {
            std::optional<VaultPath> named;
            if (name.kind == QueryFilterKind::Tag)
                named = index.resolveTag(name.value);
            else if (name.kind == QueryFilterKind::Ref || name.kind == QueryFilterKind::Page)
                named = index.resolveLink(name.value, from);
            if (named && *named != from)
                referenced.insert(*named);
        }
    }
    return referenced;
}

/// The attachments of `candidates` whose file name stands in the text of a note: one pass over
/// each text, looking up what ends at each ".extension" (a notebook may hold thousands of
/// attachments). A name inside a longer word ("plan.png" in "myplan.png") does not count.
QSet<VaultPath> attachmentsNamedInText(const VaultIndex &index, const QList<VaultPath> &candidates)
{
    QHash<QString, QList<VaultPath>> byName;      // lower-case name, also percent-encoded -> files
    QHash<QString, QList<qsizetype>> lengthsByExt; // lower-case extension -> the names' lengths
    const auto addName = [&](const QString &name, const VaultPath &path) {
        const qsizetype dot = name.lastIndexOf(u'.');
        if (dot <= 0 || dot == name.size() - 1)
            return; // nothing to tell the name from an ordinary word
        byName[name].append(path);
        QList<qsizetype> &lengths = lengthsByExt[name.mid(dot + 1)];
        if (!lengths.contains(name.size()))
            lengths.append(name.size());
    };
    for (const VaultPath &path : candidates) {
        const QString name = paths::basename(path).toLower();
        addName(name, path);
        const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(name, "!$&'()*+,;=@~")).toLower();
        if (encoded != name)
            addName(encoded, path);
    }

    QSet<VaultPath> named;
    if (byName.isEmpty())
        return named; // no name to look for: no text to read
    index.forEachNote([&](const VaultPath &from, const ParsedNote &) {
        const QString text = index.content(from).toLower();
        const QStringView view(text); // looked up as views: no string built per '.' of every note
        for (qsizetype dot = text.indexOf(u'.'); dot >= 0; dot = text.indexOf(u'.', dot + 1)) {
            qsizetype end = dot + 1;
            // A name's extension may end with what percent-encoding leaves: letters, digits.
            while (end < text.size() && text.at(end).isLetterOrNumber())
                ++end;
            const auto lengths = lengthsByExt.constFind(view.sliced(dot + 1, end - dot - 1));
            if (lengths == lengthsByExt.cend())
                continue;
            for (const qsizetype length : *lengths) {
                const qsizetype start = end - length;
                if (start < 0 || (start > 0 && text.at(start - 1).isLetterOrNumber()))
                    continue;
                const auto files = byName.constFind(view.sliced(start, length));
                if (files != byName.cend())
                    named.unite(QSet<VaultPath>(files->cbegin(), files->cend()));
            }
        }
    });
    return named;
}

/// A hidden file, or one in a hidden folder: a segment of its path starts with '.'.
bool isHidden(const VaultPath &path)
{
    return path.startsWith(u'.') || path.contains(u"/.");
}

/// Still to do: to do, in progress or deferred.
bool isOpen(TaskStatus status)
{
    return status != TaskStatus::Done && status != TaskStatus::Cancelled;
}

} // namespace

QList<UnreferencedFile> unreferencedFiles(const VaultIndex &index, const VaultConfig &config)
{
    const QSet<VaultPath> referenced = referencedFiles(index);
    QSet<VaultPath> journals;
    for (const QList<JournalRef> days = index.journals(); const JournalRef &journal : days)
        journals.insert(journal.path);
    const QString templates = paths::normalize(config.templatesFolder);
    const QString inTemplates = templates.isEmpty() ? QString() : templates + u'/';
    const VaultPath journalTemplate = paths::normalize(config.journalTemplate);
    const VaultPath weeklyTemplate = periodic::templatePath(periodic::Period::Week, config);
    const VaultPath monthlyTemplate = periodic::templatePath(periodic::Period::Month, config);
    const auto left = [&](const VaultPath &path) {
        return !referenced.contains(path) && !journals.contains(path) && path != journalTemplate
               && path != weeklyTemplate && path != monthlyTemplate && !isHidden(path)
               && (inTemplates.isEmpty() || !path.startsWith(inTemplates))
               && !periodic::periodicNoteFromPath(path, config); // weekly and monthly notes, like the days
    };

    QList<UnreferencedFile> out;
    for (const QList<VaultPath> notes = index.paths(); const VaultPath &path : notes) {
        if (!left(path))
            continue;
        const QList<TaskRef> tasks = index.tasksOf(path);
        const QList<ReminderRef> reminders = index.remindersOf(path);
        UnreferencedFile file{path};
        file.openTasks = int(std::ranges::count_if(tasks, [](const TaskRef &t) { return isOpen(t.task.status); }));
        // Without the reminders of closed tasks.
        file.reminders = int(std::ranges::count_if(
                reminders, [](const ReminderRef &r) { return !r.task || isOpen(r.task->status); }));
        out.append(file);
    }

    QList<VaultPath> attachments = index.attachments();
    attachments.removeIf([&](const VaultPath &path) { return !left(path); });
    const QSet<VaultPath> named = attachmentsNamedInText(index, attachments);
    for (const VaultPath &path : std::as_const(attachments)) {
        if (!named.contains(path))
            out.append(UnreferencedFile{path, true});
    }
    return out;
}

} // namespace brulion::notebook
