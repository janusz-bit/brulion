// See cleanup.h.
#include "notebook/cleanup.h"

#include "index/query.h"
#include "index/vaultindex.h"
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
    for (const VaultPath &from : index.paths()) {
        for (const ResolvedLink &link : index.outgoingLinks(from)) {
            if (!link.target.isEmpty() && link.target != from)
                referenced.insert(link.target);
        }
    }
    for (const TagCount &tag : index.tags()) {
        const std::optional<VaultPath> named = index.resolveTag(tag.tag);
        if (named && std::any_of(tag.paths.cbegin(), tag.paths.cend(), [&](const VaultPath &p) { return p != *named; }))
            referenced.insert(*named);
    }
    for (const VaultPath &from : index.notesWithQueries()) {
        for (const QueryNameRef &name : findQueryNames(index.content(from), index.note(from))) {
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
    index.forEachNote([&](const VaultPath &from, const ParsedNote &) {
        const QString text = index.content(from).toLower();
        for (qsizetype dot = text.indexOf(u'.'); dot >= 0; dot = text.indexOf(u'.', dot + 1)) {
            qsizetype end = dot + 1;
            // A name's extension may end with what percent-encoding leaves: letters, digits.
            while (end < text.size() && text.at(end).isLetterOrNumber())
                ++end;
            const auto lengths = lengthsByExt.constFind(text.mid(dot + 1, end - dot - 1));
            if (lengths == lengthsByExt.cend())
                continue;
            for (const qsizetype length : *lengths) {
                const qsizetype start = end - length;
                if (start < 0 || (start > 0 && text.at(start - 1).isLetterOrNumber()))
                    continue;
                const auto files = byName.constFind(text.mid(start, length));
                if (files != byName.cend())
                    named.unite(QSet<VaultPath>(files->cbegin(), files->cend()));
            }
        }
    });
    return named;
}

bool isHidden(const VaultPath &path)
{
    const QStringList segments = path.split(u'/');
    return std::any_of(segments.cbegin(), segments.cend(), [](const QString &s) { return s.startsWith(u'.'); });
}

} // namespace

QList<UnreferencedFile> unreferencedFiles(const VaultIndex &index, const VaultConfig &config)
{
    const QSet<VaultPath> referenced = referencedFiles(index);
    QSet<VaultPath> journals;
    for (const JournalRef &journal : index.journals())
        journals.insert(journal.path);
    const QString templates = paths::normalize(config.templatesFolder);
    const VaultPath journalTemplate = paths::normalize(config.journalTemplate);
    const auto left = [&](const VaultPath &path) {
        return !referenced.contains(path) && !journals.contains(path) && path != journalTemplate && !isHidden(path)
            && (templates.isEmpty() || !path.startsWith(templates + u'/'));
    };

    QList<UnreferencedFile> out;
    for (const VaultPath &path : index.paths()) {
        if (!left(path))
            continue;
        UnreferencedFile file{path};
        for (const TaskRef &task : index.tasksOf(path)) {
            const TaskStatus status = task.task.status;
            if (status == TaskStatus::Todo || status == TaskStatus::Doing || status == TaskStatus::Deferred)
                ++file.openTasks;
        }
        for (const ReminderRef &reminder : index.remindersOf(path)) {
            const std::optional<TaskStatus> status = reminder.task ? std::optional(reminder.task->status) : std::nullopt;
            if (status != TaskStatus::Done && status != TaskStatus::Cancelled)
                ++file.reminders;
        }
        out.append(file);
    }

    QList<VaultPath> attachments;
    for (const VaultPath &path : index.attachments()) {
        if (left(path))
            attachments.append(path);
    }
    if (!attachments.isEmpty()) {
        const QSet<VaultPath> named = attachmentsNamedInText(index, attachments);
        for (const VaultPath &path : std::as_const(attachments)) {
            if (!named.contains(path))
                out.append(UnreferencedFile{path, true});
        }
    }
    return out;
}

} // namespace brulion::notebook
