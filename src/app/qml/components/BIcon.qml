pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.impl as Impl
import Brulion

// BIcon — a Lucide icon (assets/icons/<name>.svg, ISC licence) at any size and colour.
//
//   BIcon { name: "bell"; size: Theme.iconSize; color: Theme.fg }
//
// The monochrome SVGs are rasterised at size × device pixel ratio and recoloured on the CPU
// (IconImage), so they stay crisp with the software renderer and on WebAssembly. Icons are
// decorative for assistive technology (Accessible.ignored); label the control instead.
// An empty name renders nothing (and takes no space when `collapseWhenEmpty`).
Item {
    id: icon

    property string name
    property real size: Theme.iconSize
    property color color: Theme.fg
    property bool collapseWhenEmpty: true

    width: name === "" && collapseWhenEmpty ? 0 : size
    height: name === "" && collapseWhenEmpty ? 0 : size
    implicitWidth: width
    implicitHeight: height
    Accessible.ignored: true

    // No IconImage without a name: one without a source looks its (empty) name up in the icon
    // theme on the file system — every icon theme directory of XDG_DATA_DIRS — whenever it is
    // created or resized (QQuickIconImage::load), which made rows with such icons (menu items,
    // empty slots) slow to create (docs/UI.md "Scrolling performance").
    Loader {
        anchors.fill: parent
        active: icon.name !== ""
        sourceComponent: Impl.IconImage {
            sourceSize: Qt.size(icon.size, icon.size)
            source: "qrc:/icons/" + icon.name + ".svg"
            color: icon.color
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
    }
}
