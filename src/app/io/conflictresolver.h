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
//   identical          the same elements (another program wrote the picture around them)
//   copyIsOlder        the original has every element of the copy (drawing::containsDrawing)
//   mergeDrawings()    mergedBytes replace the original, the copy goes
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
    /// Both are drawings Brulion edits, of one format: "Scal rysunki".
    Q_PROPERTY(bool canMergeDrawings READ canMergeDrawings NOTIFY changed FINAL)
    Q_PROPERTY(QByteArray oursBytes READ oursBytes NOTIFY changed FINAL)
    Q_PROPERTY(QByteArray theirsBytes READ theirsBytes NOTIFY changed FINAL)
    Q_PROPERTY(QByteArray mergedBytes READ mergedBytes NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap drawingCounts READ drawingCounts NOTIFY changed FINAL)
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

public:
    explicit ConflictResolver(QObject *parent = nullptr);

    shell::Workspace *workspace() const { return m_workspace; }
    void setWorkspace(shell::Workspace *workspace);
    QString copy() const { return m_copy; }
    void setCopy(const QString &copy);

    bool loaded() const { return !m_conflict.isEmpty(); }
    QVariantMap conflict() const { return m_conflict; }
    bool isText() const { return m_conflict.value(QStringLiteral("text")).toBool() && !isDrawing(); }
    bool isDrawing() const { return m_conflict.value(QStringLiteral("drawing")).toBool(); }
    bool canMergeDrawings() const { return m_canMergeDrawings; }
    QByteArray oursBytes() const { return m_oursBytes; }
    QByteArray theirsBytes() const { return m_theirsBytes; }
    QByteArray mergedBytes() const { return m_mergedBytes; }
    QVariantMap drawingCounts() const { return m_drawingCounts; }
    QString ours() const { return m_ours; }
    QString theirs() const { return m_theirs; }
    QString base() const { return m_base; }
    bool hasBase() const { return m_hasBase; }
    QString baseWhen() const { return m_baseWhen; }
    QVariantList hunks() const { return m_hunks; }
    bool clean() const { return m_clean; }
    QString mergedText() const { return m_merged; }
    int collisionCount() const { return m_collisions; }
    bool identical() const { return m_identical; }
    bool copyIsOlder() const { return m_copyIsOlder; }
    QString oursSizeText() const;
    QString theirsSizeText() const;

    /// Load the copy's conflict again (after the files changed).
    Q_INVOKABLE void reload();
    Q_INVOKABLE bool keepMine();
    Q_INVOKABLE bool keepCopy();
    Q_INVOKABLE bool applyMerged(const QString &text);
    /// Write mergedBytes into the original and move the copy to the trash (canMergeDrawings).
    Q_INVOKABLE bool mergeDrawings();

Q_SIGNALS:
    void workspaceChanged();
    void changed();
    /// The conflict is gone (translated message for a toast).
    void resolved(const QString &message);
    void failed(const QString &message);

private:
    void finish(bool ok, const QString &message);
    /// Both files still hold the texts the user decided on; otherwise reload() and failed().
    bool textsUnchanged();
    /// The same for drawings: both files still hold the bytes shown.
    bool drawingsUnchanged();
    /// Read both drawings and merge them (reload() of a drawing conflict).
    void loadDrawings(const QString &original, const QString &copy, bool originalExists);

    QPointer<shell::Workspace> m_workspace;
    QString m_copy;
    QVariantMap m_conflict;
    QString m_ours;
    QString m_theirs;
    QString m_base;
    bool m_hasBase = false;
    QString m_baseWhen;
    QVariantList m_hunks;
    bool m_clean = false;
    QString m_merged;
    int m_collisions = 0;
    bool m_identical = false;
    bool m_copyIsOlder = false;
    qint64 m_oursSize = 0;
    qint64 m_theirsSize = 0;
    bool m_canMergeDrawings = false;
    QByteArray m_oursBytes;
    QByteArray m_theirsBytes;
    QByteArray m_mergedBytes;
    QVariantMap m_drawingCounts;
};

} // namespace brulion::ioviews
