// Copying elements between drawings — and between Brulion and excalidraw.com — through the system
// clipboard (docs/UI.md "Drawing editor" → Clipboard; docs/SYNTAX.md §2 "Clipboard"). Excalidraw
// copies its elements as text: the JSON
//
//   {"type":"excalidraw/clipboard","elements":[…],"files":{…}}
//
// (its serializeAsClipboardJSON: the elements as a scene stores them, `files` the pictures they
// show), and pastes that, a whole scene ({"type":"excalidraw",…}) or plain text (a new text
// element). Brulion writes and reads the same, so a selection copied in either program pastes in
// the other: Excalidraw gives what it pastes new ids and places it at the pointer, and so does
// Brulion. Pure functions (the app passes ids, random numbers and the clock in an EditStamp).
#pragma once

#include "drawing/drawing.h"
#include "drawing/edit.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <optional>

namespace brulion::drawing {

/// The `type` of Excalidraw's clipboard.
inline constexpr QStringView ClipboardType = u"excalidraw/clipboard";
/// The most elements one paste takes (a clipboard is text anyone may put there).
inline constexpr qsizetype MaxPastedElements = 20'000;

/// The elements `ids` of `d` (in drawing order; the caller passes a whole selection —
/// selectionClosure() —, locked elements are left out) as Excalidraw's clipboard JSON (compact,
/// UTF-8): each as a scene stores it (excalidraw.h sceneObject(): a polygon's label as a text in a
/// group with it), bindings to elements not copied dropped (Excalidraw would bind the pasted arrow
/// to nothing), and the files of the pictures among them. Empty when nothing is copied.
QByteArray clipboardJson(const Drawing &d, const QStringList &ids);

/// What a clipboard's text holds for a drawing.
struct ClipboardContent {
    /// The elements Brulion draws, in their order, with the ids they had there (pasteElements()
    /// gives them new ones).
    QList<Stroke> elements;
    /// The pictures they show (Excalidraw's files by id).
    QJsonObject files;
    /// Elements of the clipboard Brulion does not draw (frames, embedded pages, unknown kinds,
    /// beyond CoordinateLimit) or beyond MaxPastedElements: left out.
    int skipped = 0;
};
/// The clipboard's text as elements: Excalidraw's clipboard ("excalidraw/clipboard", also its
/// "excalidraw-api/clipboard") or a whole scene ("excalidraw") with an `elements` array —
/// deleted elements dropped, polygons' labels read as Brulion keeps them; nullopt for anything
/// else (plain text: the app writes it as a new text). Within MaxSceneBytes and
/// MaxSceneContainers, as a scene.
std::optional<ClipboardContent> parseClipboard(QByteArrayView text);

/// Add the pasted elements on top of `d`, as Excalidraw pastes them: new ids, seeds and groups
/// (renamed consistently, so a pasted group is a group of its own), bindings and labels kept among
/// them and dropped to anything else, unlocked (a pasted backdrop is a picture), stamped as new,
/// moved so the middle of what they cover lies at `at` (kept within CoordinateLimit); their
/// pictures' files added (one the drawing has already stays). The new elements' ids, in drawing
/// order (empty: nothing pasted).
QStringList pasteElements(Drawing &d, const ClipboardContent &content, QPointF at, const EditStamp &s);

} // namespace brulion::drawing
