pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// MathBlockView — a ```math block (docs/SYNTAX.md §11): the formula of the LaTeX source rendered
// as rich text (markdown::renderMath: Unicode symbols, <sup>/<sub>, fractions, radicals, matrices)
// on a sunken panel, centred. The fence lines stay in the file — the delegate draws the frame —
// and the LaTeX source between them is edited in place (BlockText: the rendered formula while the
// block is not edited, the source in the editor, click starts editing). Enter inserts a line
// break of the formula (a matrix, `aligned`); the source is never touched by anything else.
Rectangle {
    id: root

    property BlockDelegate block
    readonly property real firstLineCenter: formula.y + formula.firstLineCenter

    implicitHeight: formula.y + formula.implicitHeight + Theme.space3
    height: implicitHeight
    radius: Theme.radius
    color: Theme.bgSunken
    border.width: 1
    border.color: root.block.editing ? Theme.alpha(Theme.focusRing, 0.5) : Theme.borderSubtle

    BlockText {
        id: formula
        x: Theme.space4
        y: Theme.space3
        width: parent.width - 2 * Theme.space4
        block: root.block
        centered: true
    }
}
