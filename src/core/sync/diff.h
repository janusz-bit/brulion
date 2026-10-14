// Line diff (Myers' O((N+M)·D) algorithm with linear-space bisection, common prefix/suffix
// trimming and a work budget) and the diff3 region split built on it.
//
// The budget bounds pathological inputs (huge, completely different texts): when it runs out
// the remaining range is reported as fully changed, which can only turn a clean merge into a
// conflict, never lose text. Iterative (no recursion), so very long notes cannot overflow the
// stack.
#pragma once

#include <QList>
#include <QStringList>

namespace brulion::sync {

/// A run of differing lines: a[aStart, aStart + aLength) became b[bStart, bStart + bLength).
struct DiffHunk {
    qsizetype aStart = 0;
    qsizetype aLength = 0;
    qsizetype bStart = 0;
    qsizetype bLength = 0;

    friend bool operator==(const DiffHunk &, const DiffHunk &) = default;
};

/// Default work budget per diff (inner-loop steps; well under a second on a Raspberry Pi 4).
inline constexpr qint64 kDiffBudget = 8'000'000;

/// Differences between two line lists as hunks in order; lines between hunks are equal.
QList<DiffHunk> diffLines(const QStringList &a, const QStringList &b, qint64 budget = kDiffBudget);

/// One part of a two-way comparison: lines common to both, or lines only in `a` / only in `b`.
struct CommPart {
    bool common = false;
    QStringList commonLines; ///< when `common`
    QStringList a, b;        ///< otherwise (either may be empty)

    friend bool operator==(const CommPart &, const CommPart &) = default;
};

/// Two-way comparison in document order (like node-diff3's diffComm).
QList<CommPart> diffComm(const QStringList &a, const QStringList &b, qint64 budget = kDiffBudget);

/// One region of a three-way comparison of `a` (local) and `b` (remote) against `o` (base).
struct Diff3Region {
    bool conflict = false;
    QStringList ok;      ///< merged lines of a stable region
    QStringList a, o, b; ///< the three versions of a conflicting region

    friend bool operator==(const Diff3Region &, const Diff3Region &) = default;
};

/// Three-way regions (semantics of node-diff3's diff3Merge with excludeFalseConflicts): a
/// change of one side is taken; overlapping or adjacent changes of both sides form a conflict
/// region unless both made the same change. Adjacent stable lines are joined.
QList<Diff3Region> diff3Regions(const QStringList &a, const QStringList &o, const QStringList &b,
                                qint64 budget = kDiffBudget);

} // namespace brulion::sync
