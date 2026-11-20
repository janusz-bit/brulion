// Two versions of a canvas merged (part of the canvas module, canvas.h): the version another device
// wrote taken in while the canvas is open here (the view's external change), Nextcloud's and
// GitHub's sync of a canvas changed on two devices (mergeCanvasFiles(): never a line merge — two
// line-merged arrays may lose a comma and stop being JSON), the sync center's "Merge canvases" of a
// conflict copy (no common version: a union).
//
// One rule everywhere: theirs (its order, its file's layout) plus what changed here — nodes and
// edges new here on top in our order, those changed here changed field by field (a card moved
// here and edited there keeps both), those deleted here gone; what was deleted there stays
// deleted; a field both changed takes ours. Edges whose nodes the merge removed go with them.
#pragma once

#include "canvas/canvas.h"

#include <optional>

namespace brulion::canvas {

/// `theirs` with the changes `ours` made since `base` applied onto it; with no `base` (nullptr) a
/// union: nothing counts as deleted, an item both have is theirs. Deterministic; never reorders
/// theirs. The result has theirs' layout: what it takes from theirs unchanged is written as theirs
/// has it.
Canvas merge(const Canvas *base, const Canvas &ours, const Canvas &theirs);

/// How two versions and their merge relate (the sync center's card).
struct MergeCounts {
    int ours = 0;     ///< nodes and edges of each version
    int theirs = 0;
    int common = 0;   ///< items both have (by id), whatever their fields
    int onlyOurs = 0;
    int onlyTheirs = 0;
    int changed = 0;  ///< items both have with other fields
    int merged = 0;   ///< items of the merge
};
MergeCounts mergeCounts(const Canvas &ours, const Canvas &theirs, const Canvas &merged);

/// Nextcloud's (and GitHub's) merge of a canvas file changed on both devices: `local` and `remote`
/// merged against `base` (merge(&base, local, remote)), written in the remote's layout. nullopt —
/// the caller keeps both versions — when any of them is no canvas (broken JSON, beyond the limits).
std::optional<QString> mergeCanvasFiles(QStringView base, QStringView local, QStringView remote);

} // namespace brulion::canvas
