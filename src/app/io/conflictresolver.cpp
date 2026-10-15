#include "conflictresolver.h"

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

bool ConflictResolver::keepCopy()
{
    if (!loaded())
        return false;
    const QString original = m_conflict.value(u"original"_s).toString();
    const QString title = m_conflict.value(u"title"_s).toString();
    bool ok = false;
    if (isText()) {
        ok = m_workspace->sync()->resolveConflict(original, m_copy, m_theirs);
    } else if (Vault *vault = m_workspace->vault()) {
        bool read = false;
        const QByteArray data = vault->readBinary(m_copy, &read);
        ok = read && vault->writeBinary(original, data, ChangeSource::User)
            && m_workspace->sync()->discardConflictCopy(m_copy);
    }
    finish(ok, ok ? tr("“%1” now has the copy's content. Your previous version is in the history.").arg(title)
                  : tr("The copy could not be applied."));
    return ok;
}

bool ConflictResolver::applyMerged(const QString &text)
{
    if (!loaded() || !isText())
        return false;
    const QString original = m_conflict.value(u"original"_s).toString();
    const QString title = m_conflict.value(u"title"_s).toString();
    const bool ok = m_workspace->sync()->resolveConflict(original, m_copy, text);
    finish(ok, ok ? tr("Merged “%1”. Your previous version is in the history.").arg(title)
                  : tr("The merged text could not be saved."));
    return ok;
}

} // namespace brulion::ioviews
