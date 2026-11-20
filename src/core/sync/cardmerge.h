// Flashcards' scheduling state in a three-way merge (docs/SYNTAX.md "Flashcards", the built-in
// extension fiszki.luau): a region where the two devices changed only cards' state — the
// scheduling comment `<!--SR:!…-->` (the Obsidian Spaced Repetition plugin's, which Brulion writes
// too) or Logseq's `card-*::` properties — merges by itself instead of being a conflict.
//
//   mergeCardState({"a::b", "c::d"},
//                  {"a::b <!--SR:!fsrs,…,2026-11-20T08:00:00.000Z-->", "c::d"},
//                  {"a::b", "c::d <!--SR:!2026-11-24,4,250-->"})
//       == {"a::b <!--SR:!fsrs,…-->", "c::d <!--SR:!2026-11-24,4,250-->"}
//
// Rules: the three sides have the same lines but for their state (a line's text without its
// comment, a property's key), or both sides added only state lines at one place (the first review
// of one card on two devices). Line by line, a change of one side is taken; a line both changed
// keeps the later review — in a comment sibling by sibling (one device may have reviewed a cloze's
// first deletion, the other its second): a segment's review is its `fsrs` segment's last review,
// or an SM-2 segment's due minus its interval; for Logseq's properties the later `card-last-
// reviewed` (else `card-fsrs`'s last review) of their run of property lines. Equal reviews: the
// local side. Anything else is no card state: nullopt, and merge3 goes on with its own rules.
#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace brulion::sync {

/// The line without its scheduling comment (and the one space before it) and, for a `card-*::`
/// property line, without its value: what must be equal on the three sides.
QString cardStateKey(const QString &line);

/// Which side's Logseq properties win when a region alone cannot tell (its run of card-*
/// properties goes on beyond it: card-last-reviewed changed in another region).
enum class CardSide { Unknown, Local, Remote };

/// A Logseq card property line (`card-repeats:: 4`, any `card-…::`).
bool isCardProperty(const QString &line);

/// The side of the later review of a run of card-* property lines, as each side has it
/// (card-last-reviewed, else card-fsrs's last review); Unknown when neither tells.
CardSide laterCardSide(const QStringList &localRun, const QStringList &remoteRun);

/// The region merged as above, or nullopt when it is not only cards' state. `side`: what
/// laterCardSide() said of the run of properties the region is part of (merge3 looks across
/// regions), used where the region's own lines cannot tell.
std::optional<QStringList> mergeCardState(const QStringList &base, const QStringList &local, const QStringList &remote,
                                          CardSide side = CardSide::Unknown);

} // namespace brulion::sync
