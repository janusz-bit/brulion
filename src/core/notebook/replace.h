// Replacing text in the whole notebook, with a preview (docs/UI.md "Find and replace"). Part of the
// notebook module — see notebook/notebook.h.
//
// A plan lists what would change, file by file, and holds no offsets to be trusted later: applying
// reads each file again, refuses one whose bytes differ from what was planned (ReplaceStatus::Changed)
// and finds the hits again in the same text — hits are named by their number in the file, so a file
// with thousands of them costs little memory and the preview can stop early (ReplaceFile::hits). The
// write is a compare-and-swap through the Vault with the History reason "replace" (always kept, also
// right after another snapshot): line endings, a BOM and every byte outside the hits stay, and an editor
// showing the file takes the change in like any other (reload, three-way merge).
//
// Only notes (isReplaceableNote(), and not a drawing by its content): never `.trash/` or ignored
// paths (Vault::files() leaves them out), hidden files, conflict copies, drawings (`*.excalidraw.md`,
// a note whose frontmatter has the plugin's key; SVGs are no notes), files that are not valid UTF-8
// (writing them back would change their other bytes) or over ReplaceMaxFileBytes.
#pragma once

#include "notebook/indexloader.h"
#include "text/find.h"
#include "types.h"
#include "vault/vault.h"

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>

namespace brulion::notebook {

/// A larger file is left out, as the vault's scans leave out what they will not keep in History.
constexpr qint64 ReplaceMaxFileBytes = 16 * 1024 * 1024;

/// One occurrence shown in a preview.
struct ReplaceHit {
    int index = 0;        ///< its number in the file (0-based): what ReplaceChoice::excluded names
    int line = 0;         ///< 0-based line of the file
    qsizetype start = 0;  ///< in the text as planned
    qsizetype length = 0;
    QString before;       ///< the line before the match, cut to a short context ("…" when cut)
    QString matched;      ///< the text matched, as written
    QString after;        ///< the line after the match, cut likewise
    bool inLink = false;  ///< inside the target of a `[[link]]` or `[text](link)`: the link will change

    friend bool operator==(const ReplaceHit &, const ReplaceHit &) = default;
};

/// A note with hits.
struct ReplaceFile {
    VaultPath path;
    QString hash;           ///< of the file's bytes when planned
    int hitCount = 0;       ///< every hit of the file
    QList<ReplaceHit> hits; ///< the first ones, at most the preview's limits
};

struct ReplaceLimits {
    int previewPerFile = 50; ///< hits of one file with their context
    int previewTotal = 2000; ///< hits of the plan with their context; the rest are only counted
};

struct ReplacePlan {
    QString query;
    text::FindOptions options;
    QList<ReplaceFile> files; ///< sorted by path
    int notesScanned = 0;
    int totalHits = 0;
    int previewHits = 0;      ///< hits with their context (the rest only counted)
    QList<VaultPath> skipped; ///< notes left out: not valid UTF-8, too large, unreadable
};

/// A note the replace may touch by its path alone: Markdown, not hidden, no conflict copy, no
/// `*.excalidraw.md`.
bool isReplaceableNote(const VaultPath &path);

/// The hits of `text` with their contexts, the first `previewHits` of them (pure).
ReplaceFile planFile(const VaultPath &path, QStringView text, QStringView query, const text::FindOptions &options,
                     int previewHits);

/// What a file turned out to be when read: a note the replace may write; a drawing by its content
/// (no note at all); unreadable, not valid UTF-8 or too large (told as ReplacePlan::skipped).
enum class ReadKind { Note, Drawing, Unusable };

/// planFile() for the file as read (Vault::readDetached): no hits unless it is a note (`kind`).
ReplaceFile planDetachedRead(const DetachedRead &read, QStringView query, const text::FindOptions &options,
                             int previewHits, ReadKind *kind = nullptr);

/// The plan of the whole notebook at once. The app goes through ReplaceScan.
ReplacePlan planReplace(Vault &vault, const QString &query, const text::FindOptions &options,
                        const ReplaceLimits &limits = {});

/// What to replace in one file: its hits except `excluded`.
struct ReplaceChoice {
    VaultPath path;
    QSet<int> excluded;
};

enum class ReplaceStatus {
    Written,
    Changed,  ///< the file is not what was planned (edited, synced, deleted meanwhile): left alone
    Failed,   ///< could not be read or written
};

/// The result for one file; `before` and `after` are what undoReplacedFile() needs.
struct ReplacedFile {
    VaultPath path;
    ReplaceStatus status = ReplaceStatus::Failed;
    int replaced = 0;
    QString before;
    QString after;
};

/// One file of a plan (see the file comment): nothing written unless its bytes are the planned ones.
/// An excluded hit stays as it is; nothing left to replace writes nothing (Written, replaced 0).
ReplacedFile applyToFile(Vault &vault, const ReplaceFile &file, const ReplacePlan &plan, const QString &replacement,
                         const QSet<int> &excluded = {}, ChangeSource source = ChangeSource::User);

/// applyToFile() for each file of `choices` that the plan has.
QList<ReplacedFile> applyReplace(Vault &vault, const ReplacePlan &plan, const QString &replacement,
                                 const QList<ReplaceChoice> &choices, ChangeSource source = ChangeSource::User);

enum class UndoReplaceStatus {
    Restored,
    Changed, ///< the file no longer holds what the replace wrote: left alone
    Failed,
};

/// Put back what applyToFile() replaced — only while the file still holds what it wrote.
UndoReplaceStatus undoReplacedFile(Vault &vault, const ReplacedFile &file, ChangeSource source = ChangeSource::User);

/// Builds a plan without stalling its thread: reads the notes and looks through them on the pool's
/// threads, merges the results on the owner's thread (step()). Inline without threads (WebAssembly):
/// a slice of the notes per step(). The vault is touched on the owner's thread only, when starting.
class ReplaceScan : public QObject {
    Q_OBJECT

public:
    enum class Step {
        Done,    ///< every note looked through
        More,    ///< more to do now
        Waiting, ///< nothing ready yet: resultsReady() follows
    };

    ReplaceScan(Vault &vault, WorkerPool pool, const QString &query, const text::FindOptions &options,
                const ReplaceLimits &limits = {}, QObject *parent = nullptr);
    /// Cancels: the workers stop after their current note (never waits for them).
    ~ReplaceScan() override;

    /// Merge what is ready for about `budgetMs` (inline: also read and search).
    Step step(int budgetMs);
    bool isDone() const;
    /// Notes looked through and merged.
    int done() const;
    /// Notes to look through.
    int total() const;
    /// The plan so far.
    const ReplacePlan &plan() const;
    /// The files merged since the last call (their rows of the plan: `plan().files` holds them).
    QList<VaultPath> takeMerged();

Q_SIGNALS:
    /// Results are ready for step() (queued; once per empty → non-empty).
    void resultsReady();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace brulion::notebook
