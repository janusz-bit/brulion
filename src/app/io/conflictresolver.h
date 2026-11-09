// ConflictResolver — one conflict copy in the sync center (QML: `ConflictResolver { workspace:
// Workspace; copy: <conflict copy path> }`): the two texts, a three-way merge when a common
// ancestor can be found in this device's version history, and the three ways out.
//
//   ours / theirs      the original's text (this device's version) and the copy's
//   base, baseWhen     the most plausible ancestor from History (pickMergeBase()) and when it was
//                      saved ("YYYY-MM-DD HH:mm", for DatesHelper.formatHuman); "" without one
//   hunks              BMergeView hunks of sync::merge3(base, ours, theirs) — changes made on one
//                      side only are already merged into the "same" parts, only real collisions
//                      are left; [] without a base (BMergeView then diffs the two versions itself)
//   clean, mergedText  everything merged automatically (review it, then applyMerged(mergedText))
//   identical          both texts are equal (the copy can simply go)
//   copyIsOlder        the copy equals an older version of the original on this device: every
//                      change in it is already contained in "mine"
//
// Drawings (`drawing`: the list's `drawing` — an SVG with an Excalidraw scene, `*.excalidraw.md`,
// `.excalidraw`; `text` is false for them: never merged line by line, the payload would be cut):
//   oursBytes, theirsBytes   both files, mergedBytes the merge (the previews load them)
//   canMergeDrawings   both are drawings Brulion edits, of one format: drawing::merge with no
//                      common version (a union) — the original's elements, order, paper and
//                      Markdown around the scene, plus the elements only the copy has (on top);
//                      for an element both have, the higher version (a tie: the original's)
//   drawingCounts      { ours, theirs, common, onlyOurs, onlyTheirs, merged } (elements)
//   identical          the same elements (another program wrote the picture around them) and,
//                      for Obsidian Excalidraw plugin files, the same Markdown around the scene
//   copyIsOlder        the original has every element of the copy (drawing::containsDrawing)
//                      and every note of it (below)
//   notesDiffer        plugin files: the copy's Markdown around its scene (frontmatter, the back
//                      of the card, links, embedded files, text entries) would not survive in the
//                      original's (drawing::keepsMarkdownOf) — oursNotes / theirsNotes show both
//                      (drawing::markdownAroundScene); notesOnly: nothing else differs (the copy's
//                      elements are all in the original, or a merge would not change it — the
//                      copy's paper, a same-version hand edit): only "remove the copy" or "keep one"
//   mergeDrawings()    mergedBytes replace the original, the copy goes — but while notesDiffer
//                      the copy stays (updated(), not resolved()): its notes are carried over by
//                      hand, then it is removed (notesOnly)
//
//   keepMine()         the original stays, the copy goes to the trash
//   keepCopy()         the copy's content replaces the original, the copy goes (binary files: the
//                      original goes to the trash and the copy is renamed into its place; a
//                      drawing: the copy's bytes, the original's in the history)
//   applyMerged(text)  `text` replaces the original, the copy goes
// The replaced text of the original stays in the version history (Workspace.sync.resolveConflict,
// resolveConflictBytes; unthrottled). keepCopy(), applyMerged() and mergeDrawings() refuse
// (failed(), reload()) when the original or the copy changed after they were loaded: the decision
// was made on the versions shown.
#pragma once

#include "workspace.h"

#include "sync/merge.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace brulion::ioviews {

/// Index of the most plausible common ancestor of `ours` and `theirs` among `candidates` (older
/// versions of the original, newest first): the one whose lines are most completely still in
/// both versions (ties: the newest), as long as at least half of its lines survive in each;
/// std::nullopt when none fits. A base that is too new (with this device's later additions)
/// would make a clean merge drop them, so lines missing from either side weigh against it.
std::optional<qsizetype> pickMergeBase(const QStringList &candidates, const QString &ours, const QString &theirs);

/// sync::merge3 hunks in BMergeView's shape: { kind: "same", text, lines } and { kind: "conflict",
/// ours, theirs } with line terminators (the style of `ours`); the last hunk ends with the final
/// newline of the merged text (merge3 decides it three-way), whichever side is kept.
QVariantList mergeHunksForView(const sync::MergeResult &result, const QString &ours, const QString &theirs);

class ConflictResolver : public QObject {
    Q_OBJECT

    Q_PROPERTY(brulion::shell::Workspace *workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged FINAL)
    Q_PROPERTY(QString copy READ copy WRITE setCopy NOTIFY changed FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed FINAL)
    /// The conflict as listed by Workspace.sync.conflicts (+ originalExists).
    Q_PROPERTY(QVariantMap conflict READ conflict NOTIFY changed FINAL)
    /// Both are text files, merged line by line (never drawings).
    Q_PROPERTY(bool text READ isText NOTIFY changed FINAL)
    /// The copy or the original is a drawing: shown as drawings.
    Q_PROPERTY(bool drawing READ isDrawing NOTIFY changed FINAL)
    /// Both are drawings Brulion edits, of one format: "Merge drawings".
    Q_PROPERTY(bool canMergeDrawings READ canMergeDrawings NOTIFY changed FINAL)
    Q_PROPERTY(QByteArray oursBytes READ oursBytes NOTIFY changed FINAL)
    Q_PROPERTY(QByteArray theirsBytes READ theirsBytes NOTIFY changed FINAL)
    Q_PROPERTY(QByteArray mergedBytes READ mergedBytes NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap drawingCounts READ drawingCounts NOTIFY changed FINAL)
    /// Plugin files whose Markdown around the scene a merge would lose (see above).
    Q_PROPERTY(bool notesDiffer READ notesDiffer NOTIFY changed FINAL)
    Q_PROPERTY(bool notesOnly READ notesOnly NOTIFY changed FINAL)
    Q_PROPERTY(QString oursNotes READ oursNotes NOTIFY changed FINAL)
    Q_PROPERTY(QString theirsNotes READ theirsNotes NOTIFY changed FINAL)
    Q_PROPERTY(QString ours READ ours NOTIFY changed FINAL)
    Q_PROPERTY(QString theirs READ theirs NOTIFY changed FINAL)
    Q_PROPERTY(QString base READ base NOTIFY changed FINAL)
    Q_PROPERTY(bool hasBase READ hasBase NOTIFY changed FINAL)
    Q_PROPERTY(QString baseWhen READ baseWhen NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList hunks READ hunks NOTIFY changed FINAL)
    Q_PROPERTY(bool clean READ clean NOTIFY changed FINAL)
    Q_PROPERTY(QString mergedText READ mergedText NOTIFY changed FINAL)
    Q_PROPERTY(int collisionCount READ collisionCount NOTIFY changed FINAL)
    Q_PROPERTY(bool identical READ identical NOTIFY changed FINAL)
    Q_PROPERTY(bool copyIsOlder READ copyIsOlder NOTIFY changed FINAL)
    Q_PROPERTY(QString oursSizeText READ oursSizeText NOTIFY changed FINAL)
    Q_PROPERTY(QString theirsSizeText READ theirsSizeText NOTIFY changed FINAL)
    /// Sizes of oursBytes, theirsBytes and mergedBytes (compiled QML reads no QByteArray length).
    Q_PROPERTY(int oursByteCount READ oursByteCount NOTIFY changed FINAL)
    Q_PROPERTY(int theirsByteCount READ theirsByteCount NOTIFY changed FINAL)
    Q_PROPERTY(int mergedByteCount READ mergedByteCount NOTIFY changed FINAL)

public:
    explicit ConflictResolver(QObject *parent = nullptr);

    shell::Workspace *workspace() const { return m_workspace; }
    void setWorkspace(shell::Workspace *workspace);
    QString copy() const { return m_copy; }
    void setCopy(const QString &copy);

    bool loaded() const { return !m_loaded.conflict.isEmpty(); }
    QVariantMap conflict() const { return m_loaded.conflict; }
    bool isText() const { return m_loaded.conflict.value(QStringLiteral("text")).toBool() && !isDrawing(); }
    bool isDrawing() const { return m_loaded.conflict.value(QStringLiteral("drawing")).toBool(); }
    bool canMergeDrawings() const { return m_loaded.canMergeDrawings; }
    QByteArray oursBytes() const { return m_loaded.oursBytes; }
    QByteArray theirsBytes() const { return m_loaded.theirsBytes; }
    QByteArray mergedBytes() const { return m_loaded.mergedBytes; }
    int oursByteCount() const { return int(m_loaded.oursBytes.size()); }
    int theirsByteCount() const { return int(m_loaded.theirsBytes.size()); }
    int mergedByteCount() const { return int(m_loaded.mergedBytes.size()); }
    QVariantMap drawingCounts() const { return m_loaded.drawingCounts; }
    bool notesDiffer() const { return m_loaded.notesDiffer; }
    bool notesOnly() const { return m_loaded.notesOnly; }
    QString oursNotes() const { return m_loaded.oursNotes; }
    QString theirsNotes() const { return m_loaded.theirsNotes; }
    QString ours() const { return m_loaded.ours; }
    QString theirs() const { return m_loaded.theirs; }
    QString base() const { return m_loaded.base; }
    bool hasBase() const { return m_loaded.hasBase; }
    QString baseWhen() const { return m_loaded.baseWhen; }
    QVariantList hunks() const { return m_loaded.hunks; }
    bool clean() const { return m_loaded.clean; }
    QString mergedText() const { return m_loaded.merged; }
    int collisionCount() const { return m_loaded.collisions; }
    bool identical() const { return m_loaded.identical; }
    bool copyIsOlder() const { return m_loaded.copyIsOlder; }
    QString oursSizeText() const;
    QString theirsSizeText() const;

    /// Load the copy's conflict again (after the files changed).
    Q_INVOKABLE void reload();
    Q_INVOKABLE bool keepMine();
    Q_INVOKABLE bool keepCopy();
    Q_INVOKABLE bool applyMerged(const QString &text);
    /// Write mergedBytes into the original and move the copy to the trash (canMergeDrawings) —
    /// unless notesDiffer: then the copy stays, reloaded (updated()).
    Q_INVOKABLE bool mergeDrawings();

Q_SIGNALS:
    void workspaceChanged();
    void changed();
    /// The conflict is gone (translated message for a toast).
    void resolved(const QString &message);
    void failed(const QString &message);
    /// The original changed, the conflict stays (translated message for a toast).
    void updated(const QString &message);

private:
    /// What reload() reads of the copy's conflict (reset there as a whole).
    struct Loaded {
        QVariantMap conflict;
        QString ours;
        QString theirs;
        QString base;
        bool hasBase = false;
        QString baseWhen;
        QVariantList hunks;
        bool clean = false;
        QString merged;
        int collisions = 0;
        bool identical = false;
        bool copyIsOlder = false;
        qint64 oursSize = 0;
        qint64 theirsSize = 0;
        bool canMergeDrawings = false;
        QByteArray oursBytes;
        QByteArray theirsBytes;
        QByteArray mergedBytes;
        QVariantMap drawingCounts;
        bool notesDiffer = false;
        bool notesOnly = false;
        QString oursNotes;
        QString theirsNotes;
    };

    /// reload() into the reset m_loaded (without the signal).
    void load();
    /// resolved() (the copy is gone, reload()) or failed() with `message`; returns `ok`.
    bool finish(bool ok, const QString &message);
    /// Both files still hold what the user decided on (the texts; drawings: the bytes shown);
    /// otherwise reload() and failed().
    bool unchangedSinceLoaded();
    /// Read both drawings and merge them (reload() of a drawing conflict).
    void loadDrawings(const QString &original, const QString &copy, bool originalExists);

    QPointer<shell::Workspace> m_workspace;
    QString m_copy;
    Loaded m_loaded;
};

} // namespace brulion::ioviews
