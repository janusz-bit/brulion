pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BIcon — a Lucide icon (assets/icons/<name>.svg, ISC licence) at any size and colour.
//
//   BIcon { name: "bell"; size: Theme.iconSize; color: Theme.fg }
//
// The monochrome SVGs are rasterised at size × device pixel ratio and recoloured on the CPU, so
// they stay crisp with the software renderer and on WebAssembly. Icons are decorative for
// assistive technology (Accessible.ignored); label the control instead. An empty name renders
// nothing (and takes no space when `collapseWhenEmpty`).
//
// An Item and its Image, no Loader: the picture comes from Icons (src/app/ui/iconprovider.h,
// "image://bicon/…"), drawn as Qt's IconImage drew it, and Qt Quick's pixmap cache shares one image
// and texture among the icons of a name, size and colour. (Until 2026-11 the Item held a Loader of
// an IconImage: five objects per icon, each recoloured and uploaded on its own — docs/PERFORMANCE.md
// "L10".) Without a name the Image has no source: nothing is loaded or looked up. (Not an Image
// itself: Image's implicit size is read-only and its picture's, BIcon's is its width and height.)
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

    Image {
        width: icon.width
        height: icon.height
        source: Icons.source(icon.name, icon.color)
        sourceSize: Qt.size(icon.size, icon.size)
        // IconImage's choice: the picture at its own size, centred, while it fits; else scaled down.
        fillMode: icon.width < Math.round(icon.size) || icon.height < Math.round(icon.size) ? Image.PreserveAspectFit
                                                                                              : Image.Pad
    }
}
