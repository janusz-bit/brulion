pragma Singleton
pragma ComponentBehavior: Bound
import QtQuick

// OpenIn — where a link or a page's row opens besides "here": a new tab, or a window of its
// own (docs/UI.md "Tabs, split view and windows"). The app's shell sets `tabs` and connects
// beginRequested / endRequested (AppShell: AppController.beginOpenIn / endOpenIn); without it
// (the editor's tests and playground) only the usual click is offered, so the editor and the lists
// need no shell.
//
//   TapHandler { acceptedButtons: Qt.LeftButton | Qt.MiddleButton
//                onTapped: (point, button) => {
//                    OpenIn.begin(OpenIn.clickTarget(button, point.modifiers))
//                    open()            // what a click does: its navigation goes to the target
//                    OpenIn.end()
//                } }
QtObject {
    id: openIn

    /// New tabs can be opened (the app's shell sets it).
    property bool tabs: false
    /// Pages may open in windows of their own (desktop).
    property bool windows: false

    /// The navigation that follows (until end()) goes to `target` ("tab" | "window").
    signal beginRequested(string target)
    /// end(): navigation goes where it usually does again.
    signal endRequested()

    /// Where a click with `button` and `modifiers` asks to open: "tab" for the middle button or
    /// Ctrl+click, else "" (here).
    function clickTarget(button: int, modifiers: int): string {
        if (!tabs)
            return ""
        if (button === Qt.MiddleButton)
            return "tab"
        return (modifiers & Qt.ControlModifier) ? "tab" : ""
    }
    /// The navigation until end() opens its page in `target` ("" or no tabs: as usual).
    function begin(target: string): void {
        if (target !== "" && tabs)
            beginRequested(target)
    }
    /// After the navigation that begin() was for.
    function end(): void {
        endRequested()
    }
}
