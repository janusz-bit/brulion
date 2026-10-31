// Two versions of a drawing merged element by element (docs/UI.md "Drawing editor"): the version
// another device wrote (Syncthing, Nextcloud) taken in while the drawing is open here, with what
// was changed here since the last load or save (DrawingCanvas::takeExternal); the sync center's
// "Scal rysunki" of a conflict copy, with no known common ancestor (known = false: a union —
// ConflictResolver); and Nextcloud's sync of a drawing changed on two devices, against the version
// both had last (mergeDrawingFiles(): SyncRun::mergeText — never a line merge of a drawing).
//
//   const MergeBase base = mergeBase(lastSaved);
//   Drawing merged = merge(base, ours, std::move(theirs));
//   std::optional<QByteArray> file = mergeDrawingFiles(baseFile, localFile, remoteFile, nowMs);
//
// One rule for every place: theirs (its order, paper, appState, files and other keys) plus what
// changed here — elements new here on top, elements changed here replacing theirs, elements
// deleted here removed; what was deleted there stays deleted. Never reorders theirs.
#pragma once

#include "drawing/drawing.h"
#include "drawing/drawingfile.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QString>

#include <optional>

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
///  * a backdrop (backdrop.h) each side set of its own (two elements): the one set last (`updated`;
///    a tie keeps theirs) at the bottom, the other left out;
///  * the paper's pattern and the theme: ours when the base is known and ours changed them, else
///    theirs;
///  * files: theirs plus the entries only ours has.
/// Deterministic; never reorders theirs. Both are editable() drawings (theirs.foreign stays).
Drawing merge(const MergeBase &base, const Drawing &ours, Drawing theirs);

/// The key elements are matched by: the id; an element without an id ("" — hand-made scenes) by
/// "~" + the FNV-1a hash of its element JSON (strokeElement() for one drawn in Brulion), so equal
/// ones match and different ones never do.
QString mergeKey(const Stroke &s);

/// How two versions of a drawing and their merge relate (the sync center's cards): the elements of
/// each, those both have (by mergeKey, whatever their versions), those only one has, and the
/// elements of the merge.
struct MergeCounts {
    int ours = 0;
    int theirs = 0;
    int common = 0;
    int onlyOurs = 0;
    int onlyTheirs = 0;
    int merged = 0;
};
MergeCounts mergeCounts(const Drawing &ours, const Drawing &theirs, const Drawing &merged);

/// Nothing to merge: the same elements (keys, versions and content, in the same order), the same
/// pattern, theme and paper — another program may have written the picture around the scene.
bool sameDrawing(const Drawing &a, const Drawing &b);
/// `inner` adds nothing to `outer`: every element of inner is in outer with a higher version (or
/// the same version and content), and inner's pattern, theme and paper are outer's or the
/// defaults. (A conflict copy that is an older version of the drawing: keeping the original loses
/// nothing.)
bool containsDrawing(const Drawing &outer, const Drawing &inner);

/// The Markdown around an Excalidraw plugin file's scene (drawingfile.h DrawingFile::markdown):
/// the file with its scene block emptied (the fence lines stay) — the frontmatter, the back of the
/// card, the text entries, element links, embedded files, `%%`; "" for the other formats (an SVG
/// and a `.excalidraw` scene have nothing around it). For showing how two versions' notes differ.
QByteArray markdownAroundScene(const DrawingFile &file);
/// Writing a merge of `other`'s drawing into `into`'s file (writeDrawingFile(into, …): the sync
/// center's "Scal rysunki" — `into` the original) keeps everything of `other`'s Markdown around its
/// scene: the frontmatter, the back of the card, element links, embedded files and `%%` are the
/// same byte for byte, and each "## Text Elements" entry of `other` is `into`'s as well — or
/// follows its element (an element only `other` has: its text is in the scene's `rawText`; one
/// `into` holds in a higher version: other's entry is older; one `other` holds in a higher version
/// with another text: the merge writes that one's text). An entry that differs for an element of
/// the same version (an edit by hand), or of a higher version with the same text (only its links
/// changed: the writer keeps into's entry), is lost. True when `other` is no plugin file.
bool keepsMarkdownOf(const DrawingFile &into, const DrawingFile &other);

/// Nextcloud's merge of a drawing file changed on both devices (docs/SYNTAX.md §2 "Drawings"):
/// `local` and `remote` merged element by element against `base`, the version both had last
/// (merge(mergeBase(base), local, remote): the remote order, the local elements new here on top),
/// written in the remote's format (drawingfile.h writeDrawingFile(): an SVG as toSvg(), a
/// `.excalidraw` scene, an Excalidraw plugin file with the Markdown around its scene; `now` stamps
/// the plugin's tombstones). The plugin's Markdown around the scene (frontmatter, the back of the
/// card, the text entries) is the remote's — or the local one's when only that side changed it.
/// nullopt — the caller keeps both versions (a conflict copy) — when any of the three is no
/// drawing (no scene, a broken one, an empty base), a side is not editable() or not writable, the
/// formats differ, both sides changed the plugin's Markdown differently, an element changed
/// without a new version (an edit by hand: no safe merge by versions), or the merge would not read
/// back (drawingfile.h readsBack(): the pictures of both together beyond the readers' limits).
std::optional<QByteArray> mergeDrawingFiles(QByteArrayView base, QByteArrayView local, QByteArrayView remote,
                                            qint64 now = 0);

} // namespace brulion::drawing
