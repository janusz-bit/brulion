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
    QStringList out;
    for (const QString &line : sync::splitText(text).lines) {
        if (!line.trimmed().isEmpty())
            out.append(line);
    }
    return out;
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
        QString text;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            text += lines.at(i);
            if (i + 1 < lines.size() || finalNewline)
                text += eol;
        }
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
        const bool isLast = i == last;
        if (h.kind == sync::MergeHunk::Kind::Ok) {
            if (h.lines.isEmpty())
                continue;
            out.append(QVariantMap{ { u"kind"_s, u"same"_s },
                                    { u"text"_s, join(h.lines, !isLast || merged.finalNewline) },
                                    { u"lines"_s, int(h.lines.size()) } });
        } else {
            out.append(QVariantMap{ { u"kind"_s, u"conflict"_s },
                                    { u"ours"_s, join(h.local, !isLast || merged.finalNewline) },
                                    { u"theirs"_s, join(h.remote, !isLast || merged.finalNewline) } });
        }
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
            const bool listed = std::any_of(list.cbegin(), list.cend(), [this](const QVariant &v) {
                return v.toMap().value(u"copy"_s).toString() == m_copy;
            });
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
    return loaded() ? sizeText(m_oursSize) : QString();
}

QString ConflictResolver::theirsSizeText() const
{
    return loaded() ? sizeText(m_theirsSize) : QString();
}

void ConflictResolver::reload()
{
    m_conflict.clear();
    m_ours.clear();
    m_theirs.clear();
    m_base.clear();
    m_hasBase = false;
    m_baseWhen.clear();
    m_hunks.clear();
    m_clean = false;
    m_merged.clear();
    m_collisions = 0;
    m_identical = false;
    m_copyIsOlder = false;
    m_oursSize = m_theirsSize = 0;
    m_canMergeDrawings = false;
    m_oursBytes.clear();
    m_theirsBytes.clear();
    m_mergedBytes.clear();
    m_drawingCounts.clear();
    m_notesDiffer = m_notesOnly = false;
    m_oursNotes.clear();
    m_theirsNotes.clear();
    Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    if (!vault || m_copy.isEmpty()) {
        Q_EMIT changed();
        return;
    }
    QVariantMap details = m_workspace->sync()->conflictDetails(m_copy);
    const QVariantMap texts = details;
    if (details.isEmpty()) {
        Q_EMIT changed();
        return;
    }
    const QString original = details.value(u"original"_s).toString();
    const QString copy = details.value(u"copy"_s).toString();
    if (!details.contains(u"originalExists"_s))
        details.insert(u"originalExists"_s, vault->exists(original));
    if (const auto st = vault->stat(original))
        m_oursSize = st->size;
    if (const auto st = vault->stat(copy))
        m_theirsSize = st->size;
    details.remove(u"ours"_s);
    details.remove(u"theirs"_s);
    m_conflict = details;

    if (isDrawing()) {
        loadDrawings(original, copy, details.value(u"originalExists"_s).toBool());
        Q_EMIT changed();
        return;
    }
    if (!isText()) {
        if (m_oursSize == m_theirsSize && details.value(u"originalExists"_s).toBool())
            m_identical = vault->readBinary(original) == vault->readBinary(copy);
        Q_EMIT changed();
        return;
    }
    m_ours = texts.value(u"ours"_s).toString();
    m_theirs = texts.value(u"theirs"_s).toString();
    m_identical = sameText(m_ours, m_theirs);
    if (m_identical) {
        Q_EMIT changed();
        return;
    }
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
            if (!ok || sameText(text, m_ours))
                continue;
            if (sameText(text, m_theirs)) {
                m_copyIsOlder = true; // "theirs" was this note on this device once
                continue;
            }
            candidates.append(text);
            times.append(entry.time);
        }
    }
    if (const std::optional<qsizetype> index = pickMergeBase(candidates, m_ours, m_theirs)) {
        m_hasBase = true;
        m_base = candidates.at(*index);
        m_baseWhen = times.at(*index).toString(u"yyyy-MM-dd HH:mm"_s);
        const sync::MergeResult merged = sync::merge3(m_base, m_ours, m_theirs);
        m_hunks = mergeHunksForView(merged, m_ours, m_theirs);
        m_clean = merged.clean;
        m_merged = merged.text;
        for (const QVariant &h : std::as_const(m_hunks)) {
            if (h.toMap().value(u"kind"_s).toString() == u"conflict")
                ++m_collisions;
        }
    }
    Q_EMIT changed();
}

void ConflictResolver::loadDrawings(const QString &original, const QString &copy, bool originalExists)
{
    Vault *vault = m_workspace->vault();
    m_oursBytes = originalExists ? vault->readBinary(original) : QByteArray();
    m_theirsBytes = vault->readBinary(copy);
    if (!originalExists)
        return; // "Restore from the copy"
    m_identical = m_oursBytes == m_theirsBytes;
    const std::optional<drawing::DrawingFile> mine = drawing::readDrawingFile(m_oursBytes);
    const std::optional<drawing::DrawingFile> other = drawing::readDrawingFile(m_theirsBytes);
    if (!mine || !other)
        return; // a picture, a broken scene: keep one of them
    // An Obsidian plugin file is a note as well: the Markdown around its scene (frontmatter, the
    // back of the card, links) counts — a merge writes the original's, so the copy's must be in it.
    const bool keepsNotes = drawing::keepsMarkdownOf(*mine, *other);
    if (!m_identical)
        m_identical = drawing::sameDrawing(mine->drawing, other->drawing) && keepsNotes
                && drawing::keepsMarkdownOf(*other, *mine);
    const bool contained = !m_identical && drawing::containsDrawing(mine->drawing, other->drawing);
    m_copyIsOlder = contained && keepsNotes;
    m_notesDiffer = !m_identical && !keepsNotes;
    if (m_notesDiffer) {
        m_oursNotes = QString::fromUtf8(drawing::markdownAroundScene(*mine));
        m_theirsNotes = QString::fromUtf8(drawing::markdownAroundScene(*other));
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
    m_notesOnly = m_notesDiffer && (contained || (mergeable && drawing::sameDrawing(merged, mine->drawing)));
    if (m_notesOnly || !mergeable)
        return;
    m_mergedBytes = drawing::writeDrawingFile(*mine, merged, m_workspace->now().toMSecsSinceEpoch());
    if (m_mergedBytes.isEmpty())
        return;
    m_canMergeDrawings = true;
    const drawing::MergeCounts c = drawing::mergeCounts(mine->drawing, other->drawing, merged);
    m_drawingCounts = QVariantMap{ { u"ours"_s, c.ours },         { u"theirs"_s, c.theirs },
                                   { u"common"_s, c.common },     { u"onlyOurs"_s, c.onlyOurs },
                                   { u"onlyTheirs"_s, c.onlyTheirs }, { u"merged"_s, c.merged } };
}

void ConflictResolver::finish(bool ok, const QString &message)
{
    if (!ok) {
        Q_EMIT failed(message);
        return;
    }
    m_copy.clear();
    reload();
    Q_EMIT resolved(message);
}

bool ConflictResolver::keepMine()
{
    if (!loaded())
        return false;
    const QString title = m_conflict.value(u"title"_s).toString();
    const bool ok = m_workspace->sync()->discardConflictCopy(m_copy);
    finish(ok, ok ? tr("Kept your version of “%1”. The copy is in the trash.").arg(title)
                  : tr("The conflict copy could not be removed."));
    return ok;
}

bool ConflictResolver::textsUnchanged()
{
    // The decision was made on the texts loaded earlier. When either file changed since (an edit
    // in another pane, a sync, Syncthing), writing now would silently drop that change: show the
    // conflict again instead.
    const QVariantMap current = m_workspace->sync()->conflictDetails(m_copy);
    if (!current.isEmpty() && current.value(u"ours"_s).toString() == m_ours
        && current.value(u"theirs"_s).toString() == m_theirs)
        return true;
    const QString title = m_conflict.value(u"title"_s).toString();
    reload();
    Q_EMIT failed(tr("“%1” has changed in the meantime. Look at both versions again before deciding.").arg(title));
    return false;
}

bool ConflictResolver::drawingsUnchanged()
{
    Vault *vault = m_workspace->vault();
    const QString original = m_conflict.value(u"original"_s).toString();
    const bool existed = m_conflict.value(u"originalExists"_s).toBool();
    bool readOurs = false;
    bool readTheirs = false;
    const QByteArray ours = vault ? vault->readBinary(original, &readOurs) : QByteArray();
    const QByteArray theirs = vault ? vault->readBinary(m_copy, &readTheirs) : QByteArray();
    if (vault && readTheirs && theirs == m_theirsBytes && readOurs == existed && (!existed || ours == m_oursBytes))
        return true;
    const QString title = m_conflict.value(u"title"_s).toString();
    reload();
    Q_EMIT failed(tr("“%1” has changed in the meantime. Look at both versions again before deciding.").arg(title));
    return false;
}

bool ConflictResolver::mergeDrawings()
{
    if (!loaded() || !isDrawing() || !m_canMergeDrawings)
        return false;
    if (!drawingsUnchanged())
        return false;
    const QString original = m_conflict.value(u"original"_s).toString();
    const QString title = m_conflict.value(u"title"_s).toString();
    if (m_notesDiffer) {
        // The copy's notes would be lost with it: it stays until they are carried over.
        if (!m_workspace->sync()->resolveConflictBytes(original, m_copy, m_mergedBytes, false)) {
            Q_EMIT failed(tr("The merged drawing could not be saved."));
            return false;
        }
        reload();
        Q_EMIT updated(tr("Merged the drawing “%1”. The copy stays: its notes differ from yours.").arg(title));
        return true;
    }
    const bool ok = m_workspace->sync()->resolveConflictBytes(original, m_copy, m_mergedBytes);
    finish(ok, ok ? tr("Merged the drawing “%1”. Your previous version is in the history, the copy in the trash.")
                            .arg(title)
                  : tr("The merged drawing could not be saved."));
    return ok;
}

bool ConflictResolver::keepCopy()
{
    if (!loaded())
        return false;
    const QString original = m_conflict.value(u"original"_s).toString();
    const QString title = m_conflict.value(u"title"_s).toString();
    bool ok = false;
    if (isDrawing()) {
        // The copy's bytes as they are; the drawing they replace stays in the history.
        if (!drawingsUnchanged())
            return false;
        ok = m_workspace->sync()->resolveConflictBytes(original, m_copy, m_theirsBytes);
        finish(ok, ok ? tr("“%1” now has the copy's content. Your previous version is in the history.").arg(title)
                      : tr("The copy could not be applied."));
        return ok;
    }
    if (isText()) {
        if (!textsUnchanged())
            return false;
        ok = m_workspace->sync()->resolveConflict(original, m_copy, m_theirs);
        finish(ok, ok ? tr("“%1” now has the copy's content. Your previous version is in the history.").arg(title)
                      : tr("The copy could not be applied."));
        return ok;
    }
    // A picture or another binary file: History keeps text only, so the original goes to the
    // trash (restorable) and the copy takes its place.
    Vault *vault = m_workspace->vault();
    QString error;
    if (vault) {
        const bool hadOriginal = vault->exists(original);
        ok = (!hadOriginal || vault->remove(original, ChangeSource::User))
            && vault->rename(m_copy, original, ChangeSource::User);
        if (!ok)
            error = vault->errorString();
    }
    finish(ok, ok ? tr("“%1” now has the copy's content. Your previous version is in the trash.").arg(title)
                  : tr("The copy could not be applied: %1").arg(error));
    return ok;
}

bool ConflictResolver::applyMerged(const QString &text)
{
    if (!loaded() || !isText())
        return false;
    if (!textsUnchanged())
        return false;
    const QString original = m_conflict.value(u"original"_s).toString();
    const QString title = m_conflict.value(u"title"_s).toString();
    const bool ok = m_workspace->sync()->resolveConflict(original, m_copy, text);
    finish(ok, ok ? tr("Merged “%1”. Your previous version is in the history.").arg(title)
                  : tr("The merged text could not be saved."));
    return ok;
}

} // namespace brulion::ioviews
