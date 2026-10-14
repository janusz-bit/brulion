// DiffHelper — line diffs for BDiffView and two-way conflict hunks for BMergeView.
// QML singleton `DiffHelper` in module Brulion (registered by qmlforeign.h; the class itself is
// QML-free, library brulion_ui_helpers). Self-contained (Myers O(ND) diff); the core sync
// module's merge3 is independent of this.
//
// Lines are compared without their terminators ("a\r\n" equals "a\n"); hunk texts handed to
// BMergeView keep the original bytes, terminators included, so resolving a merge never
// changes line endings or untouched text.
//
// Row objects (unifiedRows / splitRows):
//   kind      "equal" | "delete" | "insert" | "change" (split only: a deleted line paired with
//             its replacement) | "skip" (collapsed unchanged lines; `count` of them)
//   oldNo, newNo   1-based line numbers (0 = none on that side)
//   text (unified) / left, right (split)            the line text without terminator
//   segments (unified) / leftSegments, rightSegments  [{ text, changed }] word-level changes
//             of paired lines; empty when the whole line is new/removed or too different
//   count, oldStart, newStart   ("skip" rows) hidden line count and the first hidden lines
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::ui {

/// One step of an edit script: Equal (both), Delete (old only) or Insert (new only).
struct DiffOp {
    enum Kind { Equal, Delete, Insert };
    Kind kind = Equal;
    int oldIndex = -1; ///< index into the old sequence (Equal/Delete)
    int newIndex = -1; ///< index into the new sequence (Equal/Insert)
    friend bool operator==(const DiffOp &, const DiffOp &) = default;
};

/// Minimal edit script between two sequences of interned tokens (Myers). Deletions come
/// before insertions within a changed region. When the edit distance exceeds `maxCost`, the
/// remaining middle part is reported as one delete block followed by one insert block.
QList<DiffOp> diffSequences(const QList<int> &a, const QList<int> &b, int maxCost = 2000);

/// Line diff of two texts (lines compared without terminators).
QList<DiffOp> diffLines(QStringView oldText, QStringView newText);

class DiffHelper : public QObject {
    Q_OBJECT

public:
    explicit DiffHelper(QObject *parent = nullptr);

    /// One row per line, deletions before insertions; `context` unchanged lines are kept
    /// around changes, longer unchanged runs collapse into "skip" rows (context < 0 = keep all).
    Q_INVOKABLE QVariantList unifiedRows(const QString &oldText, const QString &newText, int context = 3) const;
    /// Side-by-side rows: changed lines are paired ("change"), extra ones are "delete"/"insert".
    Q_INVOKABLE QVariantList splitRows(const QString &oldText, const QString &newText, int context = 3) const;
    /// { added, removed, unchanged } line counts.
    Q_INVOKABLE QVariantMap stats(const QString &oldText, const QString &newText) const;

    /// Two-way conflict hunks for BMergeView, in document order:
    ///   { kind: "same", text, lines }  or
    ///   { kind: "conflict", ours, theirs, oursLine, theirsLine }  (1-based first lines)
    /// Hunk texts keep their terminators; joining all "same" texts with either side of every
    /// conflict reproduces that side exactly.
    Q_INVOKABLE QVariantList conflictHunks(const QString &ours, const QString &theirs) const;
    /// Join hunks with per-conflict choices ("ours" | "theirs" | "both" | "manual"; the manual
    /// text is used for "manual"). `choices` has one entry per conflict hunk:
    /// { choice, text }. Line breaks are added between pieces that would otherwise run together.
    /// Line breaks of a manual text (edited in a TextArea, which only knows "\n") are written
    /// with the conflict's own terminator ("\r\n" files stay "\r\n").
    Q_INVOKABLE QString resolveHunks(const QVariantList &hunks, const QVariantList &choices) const;

    /// Escaped rich text (white-space preserved) of `segments` with changed parts highlighted.
    Q_INVOKABLE QString segmentsHtml(const QVariantList &segments, const QString &highlight) const;

private:
    /// diffLines() of the last pair of texts: BDiffView asks for the stats and the rows of the
    /// same pair (and again when switching layouts), so the diff runs once per change.
    QList<DiffOp> cachedDiff(const QString &oldText, const QString &newText) const;

    mutable QString m_cacheOld;
    mutable QString m_cacheNew;
    mutable QList<DiffOp> m_cacheOps;
    mutable bool m_cacheValid = false;
};

} // namespace brulion::ui
