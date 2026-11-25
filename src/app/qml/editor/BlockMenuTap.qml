pragma ComponentBehavior: Bound
import QtQuick

// BlockMenuTap — a right-click on a block's marker (BlockPartBullet, BlockPartNumber) opens the
// block menu. Mouse-like devices only: Qt gives a finger (no buttons) to every TapHandler, so a tap
// on a phone zoomed in and opened the menu at once (touch: the long press of the marker's own
// TapHandler). WithinBounds: the press is the marker's, the editor's right-click on the block (menu
// at the pointer) does not open it a second time.
TapHandler {
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
    acceptedButtons: Qt.RightButton
    gesturePolicy: TapHandler.WithinBounds
}
