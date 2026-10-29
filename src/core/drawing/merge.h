// Two versions of a drawing merged element by element (docs/UI.md "Drawing editor"): the version
// another device wrote (Syncthing, Nextcloud) taken in while the drawing is open here, with what
// was changed here since the last load or save (DrawingCanvas::takeExternal) — and the sync
// center's "Scal rysunki" of a conflict copy, with no known common ancestor (known = false).
//
//   const MergeBase base = mergeBase(lastSaved);
//   Drawing merged = merge(base, ours, std::move(theirs));
//
// One rule for every place: theirs (its order, paper, appState, files and other keys) plus what
// changed here — elements new here on top, elements changed here replacing theirs, elements
// deleted here removed; what was deleted there stays deleted. Never reorders theirs.
#pragma once

#include "drawing/drawing.h"

#include <QHash>
#include <QString>

namespace brulion::drawing {

/// What both versions had in common (a merge base). `known == false`: no common ancestor is known —
/// the merge is then a union: nothing counts as deleted, nothing as changed on one side.
struct MergeBase {
    bool known = false;
    /// mergeKey() → the element's version at the base.
    QHash<QString, int> versions;
    Background background = Background::Plain;
    PaperTheme theme = PaperTheme::Auto;
};
/// The base of a drawing (known = true).
MergeBase mergeBase(const Drawing &base);

/// `theirs` with the changes `ours` made since `base` applied onto it:
///  * an element of ours that `base` lacks (any element of ours theirs lacks, when the base is
///    unknown) is new here: added on top of theirs, in ours' order — except the elements of ours'
///    leading run of locked elements (a backdrop: index 0, 1, … while `locked`), which go to the
///    bottom of theirs, in ours' order;
///  * an element in base and theirs but not in ours was deleted here: removed;
///  * an element in base and ours but not in theirs was deleted there: stays deleted;
///  * an element both have: known base — ours when ours changed it (version ≠ base's, also when
///    both changed it), else theirs; unknown base — the higher `version`, a tie keeps theirs;
///  * the paper's pattern and the theme: ours when the base is known and ours changed them, else
///    theirs;
///  * files: theirs plus the entries only ours has.
/// Deterministic; never reorders theirs. Both are editable() drawings (theirs.foreign stays).
Drawing merge(const MergeBase &base, const Drawing &ours, Drawing theirs);

/// The key elements are matched by: the id; an element without an id ("" — hand-made scenes) by
/// "~" + the FNV-1a hash of its element JSON (strokeElement() for one drawn in Brulion), so equal
/// ones match and different ones never do.
QString mergeKey(const Stroke &s);

} // namespace brulion::drawing
