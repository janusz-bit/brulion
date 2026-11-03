pragma Singleton
import QtQuick

// OpenIn — where a link or a page's row opens besides "here": a new tab, or a window of its
// own (docs/UI.md "Tabs, split view and windows"). The app's shell sets `openIn` (AppShell:
// AppController.openIn); without it (the editor's tests and playground) only the usual click is
// offered, so the editor and the lists need no shell.
//
//   TapHandler { acceptedButtons: Qt.LeftButton | Qt.MiddleButton
//                onTapped: (point, button) => OpenIn.run(OpenIn.clickTarget(button, point.modifiers),
//                                                              () => open()) }
QtObject {
    /// function (target, open): runs `open` — what a click does — with its navigation going to
    /// `target` ("tab" | "window"); null where there are no tabs.
    property var openIn: null
    /// Pages may open in windows of their own (desktop).
    property bool windows: false
    /// New tabs can be opened (the app's shell).
    readonly property bool tabs: openIn !== null

    /// Where a click with `button` and `modifiers` asks to open: "tab" for the middle button or
    /// Ctrl+click, else "" (here).
    function clickTarget(button: int, modifiers: int): string {
        if (!tabs)
            return ""
        if (button === Qt.MiddleButton)
            return "tab"
        return (modifiers & Qt.ControlModifier) ? "tab" : ""
    }
    /// Run `open` (a function that navigates as a click does) with its page opening in `target`
    /// ("" or no tabs: as usual).
    function run(target: string, open: var): void {
        if (target !== "" && openIn !== null)
            openIn(target, open)
        else
            open()
    }
}
