pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BTintShapes — the rounded shapes of a rich text's tinted spans (RichTextPills: chips and tags as
// pills, inline code and highlights as boxes) and a page's app icon in front of a link to it, drawn
// under the text: a child of the TextEdit with a negative z.
//
//   TextEdit { id: edit; …
//       RichTextPills { id: tints; textDocument: edit.textDocument; lineHeight: 26 }
//       BTintShapes { pills: tints } }
//
// Two Repeaters: the shapes (`plainShapes`) and the icons (`iconShapes`), so that only an icon
// makes a BIcon — every shape carried one before, part of the cost of creating a block with tags,
// as rows are created while a page or the journal scrolls (docs/UI.md "Scrolling performance").
// (The models stay lists of values: compiled QML in Qt 6.11 indexes no list of value types, so a
// Repeater over a count reading shapes by index would leave its bindings to the interpreter.)
Item {
    id: root
    /// The RichTextPills of the text (required: set where it is created).
    required property RichTextPills pills
    z: -1

    Repeater {
        model: root.pills.plainShapes
        delegate: Rectangle {
            required property richTextPill modelData
            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height
            radius: modelData.kind === "pill" ? height / 2 : Theme.radiusXs
            antialiasing: true
            color: modelData.color
        }
    }
    Repeater {
        model: root.pills.iconShapes
        delegate: BIcon {
            required property richTextPill modelData
            x: modelData.x + (modelData.width - width) / 2
            y: modelData.y + (modelData.height - height) / 2
            name: modelData.icon
            size: Math.round(Math.min(modelData.width, modelData.height) * 0.9)
            color: modelData.color
        }
    }
}
