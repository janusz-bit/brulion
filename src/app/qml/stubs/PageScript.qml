import QtQuick
import Brulion

// PageScript stand-in for the Android and browser builds that are shipped (BRULION_DEV_QML off,
// src/app/CMakeLists.txt): PageView's `--editor-script` loader still finds the type, which does
// nothing here. The real one is qml/dev/PageScript.qml.
Item {
    property BlockEditor editor
    property PageController controller
}
