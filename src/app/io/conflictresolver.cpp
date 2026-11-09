#include "conflictresolver.h"

#include "drawing/drawingfile.h"
#include "drawing/merge.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "workspacesync.h"

#include <QLocale>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

namespace {

constexpr int MaxBaseCandidates = 30;

QStringList nonBlankLines(const QString &text)
{
    QStringList lines = sync::splitText(text).lines;
    lines.removeIf([](const QString &line) { return QStringView(line).trimmed().isEmpty(); });
    return lines;
}

QSet<QString> lineSet(const QString &text)
{
    const QStringList lines = nonBlankLines(text);
    return QSet<QString>(lines.cbegin(), lines.cend());
}

/// Equal apart from line endings.
bool sameText(const QString &a, const QString &b)
{
    const sync::SplitText sa = sync::splitText(a);
    const sync::SplitText sb = sync::splitText(b);
    return sa.lines == sb.lines && sa.finalNewline == sb.finalNewline;
}

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace

std::optional<qsizetype> pickMergeBase(const QStringList &candidates, const QString &ours, const QString &theirs)
{
    const QSet<QString> mine = lineSet(ours);
    const QSet<QString> other = lineSet(theirs);
    std::optional<qsizetype> best;
    qsizetype bestScore = 0;
    for (qsizetype i = 0; i < candidates.size(); ++i) {
        const QStringList lines = nonBlankLines(candidates.at(i));
        if (lines.isEmpty())
            continue;
        qsizetype common = 0, missingMine = 0, missingOther = 0;
        for (const QString &line : lines) {
            const bool inMine = mine.contains(line);
            const bool inOther = other.contains(line);
            if (inMine && inOther)
                ++common;
            if (!inMine)
                ++missingMine;
            if (!inOther)
                ++missingOther;
        }
        const qsizetype n = lines.size();
        if (common == 0 || 2 * missingMine > n || 2 * missingOther > n)
            continue;
        const qsizetype score = common - missingMine - missingOther;
        if (!best || score > bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

QVariantList mergeHunksForView(const sync::MergeResult &result, const QString &ours, const QString &theirs)
{
    const sync::SplitText mine = sync::splitText(ours);
    const sync::SplitText other = sync::splitText(theirs);
    const sync::SplitText merged = sync::splitText(result.text);
    const QString eol = !mine.eol.isEmpty() ? mine.eol : !other.eol.isEmpty() ? other.eol : u"\n"_s;
    const auto join = [&eol](const QStringList &lines, bool finalNewline) {
        QString text = lines.join(eol);
        if (finalNewline && !lines.isEmpty())
            text += eol;
        return text;
    };
    // merge3 merges the final newline on its own (three-way): the last hunk ends like the merged
    // text, whichever side the user keeps there.
    qsizetype last = -1;
    for (qsizetype i = 0; i < result.hunks.size(); ++i) {
        const sync::MergeHunk &h = result.hunks.at(i);
        if (h.kind == sync::MergeHunk::Kind::Conflict || !h.lines.isEmpty())
            last = i;
    }
    QVariantList out;
    for (qsizetype i = 0; i < result.hunks.size(); ++i) {
        const sync::MergeHunk &h = result.hunks.at(i);
        const bool newline = i != last || merged.finalNewline;
        if (h.kind == sync::MergeHunk::Kind::Conflict)
            out.append(QVariantMap{{u"kind"_s, u"conflict"_s},
                                   {u"ours"_s, join(h.local, newline)},
                                   {u"theirs"_s, join(h.remote, newline)}});
        else if (!h.lines.isEmpty())
            out.append(QVariantMap{
                    {u"kind"_s, u"same"_s}, {u"text"_s, join(h.lines, newline)}, {u"lines"_s, int(h.lines.size())}});
    }
    return out;
}

// ---- ConflictResolver -----------------------------------------------------------------------------

ConflictResolver::ConflictResolver(QObject *parent)
    : QObject(parent)
{
}

void ConflictResolver::setWorkspace(shell::Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    if (m_workspace)
        disconnect(m_workspace->sync(), nullptr, this, nullptr);
    m_workspace = workspace;
    if (m_workspace) {
        // A copy resolved elsewhere (another window, a sync that brought the merge) disappears.
        connect(m_workspace->sync(), &shell::WorkspaceSync::conflictsChanged, this, [this] {
            if (m_copy.isEmpty() || !m_workspace)
                return;
            const QVariantList list = m_workspace->sync()->conflicts();
            const bool listed = std::ranges::any_of(
                    list, [this](const QVariant &v) { return v.toMap().value(u"copy"_s).toString() == m_copy; });
            if (!listed)
                setCopy(QString());
        });
    }
    Q_EMIT workspaceChanged();
    reload();
}

void ConflictResolver::setCopy(const QString &copy)
{
    if (copy == m_copy)
        return;
    m_copy = copy;
    reload();
}

QString ConflictResolver::oursSizeText() const
{
    return loaded() ? sizeText(m_loaded.oursSize) : QString();
}

QString ConflictResolver::theirsSizeText() const
{
    return loaded() ? sizeText(m_loaded.theirsSize) : QString();
}

void ConflictResolver::reload()
{
    m_loaded = {};
    load();
    Q_EMIT changed();
}

void ConflictResolver::load()
{
    Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    QVariantMap details = vault && !m_copy.isEmpty() ? m_workspace->sync()->conflictDetails(m_copy) : QVariantMap();
    if (details.isEmpty())
        return;
    const QString original = details.value(u"original"_s).toString();
    const QString copy = details.value(u"copy"_s).toString();
    if (!details.contains(u"originalExists"_s))
        details.insert(u"originalExists"_s, vault->exists(original));
    const bool originalExists = details.value(u"originalExists"_s).toBool();
    if (const auto st = vault->stat(original))
        m_loaded.oursSize = st->size;
    if (const auto st = vault->stat(copy))
        m_loaded.theirsSize = st->size;
    const QString ours = details.take(u"ours"_s).toString();
    const QString theirs = details.take(u"theirs"_s).toString();
    m_loaded.conflict = std::move(details);

    if (isDrawing()) {
        loadDrawings(original, copy, originalExists);
        return;
    }
    if (!isText()) {
        if (m_loaded.oursSize == m_loaded.theirsSize && originalExists)
            m_loaded.identical = vault->readBinary(original) == vault->readBinary(copy);
        return;
    }
    m_loaded.ours = ours;
    m_loaded.theirs = theirs;
    m_loaded.identical = sameText(ours, theirs);
    if (m_loaded.identical)
        return;
    // A common ancestor from this device's version history of the original.
    QStringList candidates;
    QList<QDateTime> times;
    if (History *history = vault->history()) {
        const QList<HistoryEntry> entries = history->list(original);
        for (const HistoryEntry &entry : entries) {
            if (candidates.size() >= MaxBaseCandidates)
                break;
            bool ok = false;
            const QString text = history->read(entry.id, &ok);
            if (!ok || sameText(text, ours))
                continue;
            if (sameText(text, theirs)) {
                m_loaded.copyIsOlder = true; // "theirs" was this note on this device once
                continue;
            }
            candidates.append(text);
            times.append(entry.time);
        }
    }
    const std::optional<qsizetype> index = pickMergeBase(candidates, ours, theirs);
    if (!index)
        return;
    m_loaded.hasBase = true;
    m_loaded.base = candidates.at(*index);
    m_loaded.baseWhen = times.at(*index).toString(u"yyyy-MM-dd HH:mm"_s);
    const sync::MergeResult merged = sync::merge3(m_loaded.base, ours, theirs);
    m_loaded.hunks = mergeHunksForView(merged, ours, theirs);
    m_loaded.clean = merged.clean;
    m_loaded.merged = merged.text;
    // (mergeHunksForView() shows each conflict hunk.)
    m_loaded.collisions =
            int(std::ranges::count(merged.hunks, sync::MergeHunk::Kind::Conflict, &sync::MergeHunk::kind));
}

void ConflictResolver::loadDrawings(const QString &original, const QString &copy, bool originalExists)
{
    Vault *vault = m_workspace->vault();
    m_loaded.theirsBytes = vault->readBinary(copy);
    if (!originalExists)
        return; // "Restore from the copy"
    m_loaded.oursBytes = vault->readBinary(original);
    m_loaded.identical = m_loaded.oursBytes == m_loaded.theirsBytes;
    // (readDrawingFile() bounds what it parses: MaxSceneBytes, MaxSceneContainers.)
    const std::optional<drawing::DrawingFile> mine = drawing::readDrawingFile(m_loaded.oursBytes);
    const std::optional<drawing::DrawingFile> other = drawing::readDrawingFile(m_loaded.theirsBytes);
    if (!mine || !other)
        return; // a picture, a broken scene: keep one of them
    // An Obsidian plugin file is a note as well: the Markdown around its scene (frontmatter, the
    // back of the card, links) counts — a merge writes the original's, so the copy's must be in it.
    const bool keepsNotes = drawing::keepsMarkdownOf(*mine, *other);
    if (!m_loaded.identical)
        m_loaded.identical = drawing::sameDrawing(mine->drawing, other->drawing) && keepsNotes
                             && drawing::keepsMarkdownOf(*other, *mine);
    const bool contained = !m_loaded.identical && drawing::containsDrawing(mine->drawing, other->drawing);
    m_loaded.copyIsOlder = contained && keepsNotes;
    m_loaded.notesDiffer = !m_loaded.identical && !keepsNotes;
    if (m_loaded.notesDiffer) {
        m_loaded.oursNotes = QString::fromUtf8(drawing::markdownAroundScene(*mine));
        m_loaded.theirsNotes = QString::fromUtf8(drawing::markdownAroundScene(*other));
    }
    const bool mergeable = mine->format == other->format && mine->writable && drawing::editable(mine->drawing)
                           && drawing::editable(other->drawing);
    // No common version is known (a Syncthing copy): a union. The original is the primary side —
    // its order, paper and Markdown around the scene; the copy's elements it lacks on top.
    const drawing::Drawing merged =
            mergeable ? drawing::merge(drawing::MergeBase{}, other->drawing, mine->drawing) : drawing::Drawing{};
    // Only the notes are left when the drawing would not change: the copy contained, or a union
    // that is the original as it is (the copy's paper, an element edited by hand at the same
    // version: never taken) — a merge offered again and again would never get to them.
    m_loaded.notesOnly =
            m_loaded.notesDiffer && (contained || (mergeable && drawing::sameDrawing(merged, mine->drawing)));
    if (m_loaded.notesOnly || !mergeable)
        return;
    // (The pictures of both together may be more than a file holds: then no merge is offered.)
    const qint64 now = m_workspace->now().toMSecsSinceEpoch();
    if (!drawing::readsBack(*mine, merged, now))
        return;
    m_loaded.mergedBytes = drawing::writeDrawingFile(*mine, merged, now);
    if (m_loaded.mergedBytes.isEmpty())
        return;
    m_loaded.canMergeDrawings = true;
    const drawing::MergeCounts c = drawing::mergeCounts(mine->drawing, other->drawing, merged);
    m_loaded.drawingCounts =
            QVariantMap{{u"ours"_s, c.ours},         {u"theirs"_s, c.theirs},         {u"common"_s, c.common},
                        {u"onlyOurs"_s, c.onlyOurs}, {u"onlyTheirs"_s, c.onlyTheirs}, {u"merged"_s, c.merged}};
}

bool ConflictResolver::finish(bool ok, const QString &message)
{
    if (!ok) {
        Q_EMIT failed(message);
        return false;
    }
    m_copy.clear();
    reload();
    Q_EMIT resolved(message);
    return true;
}

bool ConflictResolver::keepMine()
{
    if (!loaded())
        return false;
    const QString title = m_loaded.conflict.value(u"title"_s).toString();
    const bool ok = m_workspace->sync()->discardConflictCopy(m_copy);
    return finish(ok, ok ? tr("Kept your version of “%1”. The copy is in the trash.").arg(title)
                         : tr("The conflict copy could not be removed."));
}

bool ConflictResolver::unchangedSinceLoaded()
{
    // The decision was made on the versions loaded earlier. When either file changed since (an edit
    // in another pane, a sync, Syncthing), writing now would silently drop that change: show the
    // conflict again instead.
    bool unchanged = false;
    if (!isDrawing()) {
        const QVariantMap current = m_workspace->sync()->conflictDetails(m_copy);
        unchanged = !current.isEmpty() && current.value(u"ours"_s).toString() == m_loaded.ours
                    && current.value(u"theirs"_s).toString() == m_loaded.theirs;
    } else if (Vault *vault = m_workspace->vault()) {
        const bool existed = m_loaded.conflict.value(u"originalExists"_s).toBool();
        bool readOurs = false;
        bool readTheirs = false;
        const QByteArray ours = vault->readBinary(m_loaded.conflict.value(u"original"_s).toString(), &readOurs);
        const QByteArray theirs = vault->readBinary(m_copy, &readTheirs);
        unchanged = readTheirs && theirs == m_loaded.theirsBytes && readOurs == existed
                    && (!existed || ours == m_loaded.oursBytes);
    }
    if (unchanged)
        return true;
    const QString title = m_loaded.conflict.value(u"title"_s).toString();
    reload();
    Q_EMIT failed(tr("“%1” has changed in the meantime. Look at both versions again before deciding.").arg(title));
    return false;
}

bool ConflictResolver::mergeDrawings()
{
    if (!loaded() || !isDrawing() || !m_loaded.canMergeDrawings || !unchangedSinceLoaded())
        return false;
    const QString original = m_loaded.conflict.value(u"original"_s).toString();
    const QString title = m_loaded.conflict.value(u"title"_s).toString();
    if (m_loaded.notesDiffer) {
        // The copy's notes would be lost with it: it stays until they are carried over.
        if (!m_workspace->sync()->resolveConflictBytes(original, m_copy, m_loaded.mergedBytes, false))
            return finish(false, tr("The merged drawing could not be saved."));
        reload();
        Q_EMIT updated(tr("Merged the drawing “%1”. The copy stays: its notes differ from yours.").arg(title));
        return true;
    }
    const bool ok = m_workspace->sync()->resolveConflictBytes(original, m_copy, m_loaded.mergedBytes);
    return finish(ok,
                  ok ? tr("Merged the drawing “%1”. Your previous version is in the history, the copy in the trash.")
                                  .arg(title)
                     : tr("The merged drawing could not be saved."));
}

bool ConflictResolver::keepCopy()
{
    if (!loaded())
        return false;
    const QString original = m_loaded.conflict.value(u"original"_s).toString();
    const QString title = m_loaded.conflict.value(u"title"_s).toString();
    if (isDrawing() || isText()) {
        // A drawing: the copy's bytes as they are; the drawing they replace stays in the history.
        if (!unchangedSinceLoaded())
            return false;
        const bool ok = isDrawing() ? m_workspace->sync()->resolveConflictBytes(original, m_copy, m_loaded.theirsBytes)
                                    : m_workspace->sync()->resolveConflict(original, m_copy, m_loaded.theirs);
        return finish(ok,
                      ok ? tr("“%1” now has the copy's content. Your previous version is in the history.").arg(title)
                         : tr("The copy could not be applied."));
    }
    // A picture or another binary file: History keeps text only, so the original goes to the
    // trash (restorable) and the copy takes its place.
    Vault *vault = m_workspace->vault();
    bool ok = false;
    QString error;
    if (vault) {
        const bool hadOriginal = vault->exists(original);
        ok = (!hadOriginal || vault->remove(original, ChangeSource::User))
            && vault->rename(m_copy, original, ChangeSource::User);
        if (!ok)
            error = vault->errorString();
    }
    return finish(ok, ok ? tr("“%1” now has the copy's content. Your previous version is in the trash.").arg(title)
                         : tr("The copy could not be applied: %1").arg(error));
}

bool ConflictResolver::applyMerged(const QString &text)
{
    if (!loaded() || !isText() || !unchangedSinceLoaded())
        return false;
    const QString original = m_loaded.conflict.value(u"original"_s).toString();
    const QString title = m_loaded.conflict.value(u"title"_s).toString();
    const bool ok = m_workspace->sync()->resolveConflict(original, m_copy, text);
    return finish(ok, ok ? tr("Merged “%1”. Your previous version is in the history.").arg(title)
                         : tr("The merged text could not be saved."));
}

} // namespace brulion::ioviews
