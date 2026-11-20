pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellMacroDialog — what a macro's placeholder (or a ```view block of an extension that does not
// run) offers (docs/EXTENSIONS.md "Macros"): a click on
// `🔒 ext/name · run?` in the editor asks to run its extension on this device (its code came from
// elsewhere or changed: AppShell shows the approval with the code instead, ShellExtensionApproval;
// this dialog only when the extension has no page to read), a macro of an extension that is
// off offers to switch it on, one whose extension failed opens the code at its error, and one no
// running extension offers says so. Made by AppShell for Workspace.extensions.macroHelpRequested.
//
//   dialog.show("silownia", "{{ext:silownia/rekord Przysiad}}")
BConfirmDialog {
    id: dialog
    objectName: "macroDialog"

    property string extension
    property string macro
    /// Workspace.extensions.macroExtension(): {state, title, path, line, message, builtin}.
    MapHolder { id: info }
    readonly property string extState: AppController.routeText(info.map, "state")
    readonly property string path: AppController.routeText(info.map, "path")
    readonly property bool builtin: AppController.routeFlag(info.map, "builtin")

    function show(extensionId: string, macroSource: string): void {
        dialog.extension = extensionId
        dialog.macro = macroSource
        info.map = Workspace.extensions.macroExtension(extensionId)
        dialog.open()
    }

    title: {
        switch (dialog.extState) {
        case "approval": return qsTr("Run this extension?")
        case "off": return qsTr("Switch this extension on?")
        case "error": return qsTr("The extension failed")
        }
        return qsTr("Not found")
    }
    subject: dialog.extState === "unknown" ? dialog.macro : AppController.routeText(info.map, "title")
    subjectIcon: dialog.extState === "unknown" ? "code" : "puzzle"
    message: {
        switch (dialog.extState) {
        case "approval":
            return qsTr("%1 comes from this extension. Its code has not run on this device: it came from elsewhere or changed since it was approved. Read it first if you do not know where it comes from.").arg(dialog.macro)
        case "off": return qsTr("%1 comes from this extension, which is off on this device.").arg(dialog.macro)
        case "error": return AppController.routeText(info.map, "message")
        case "running": return qsTr("The extension runs but offers nothing of this name: check the name after its id.")
        }
        return qsTr("No extension on this device offers this. Its first part names the extension (its page's title, folded), the part after the slash a macro or a view of it.")
    }
    confirmText: {
        switch (dialog.extState) {
        case "approval": return qsTr("Run")
        case "off": return qsTr("Switch on")
        case "error": return qsTr("Open the code")
        }
        return qsTr("Extensions")
    }
    showReject: true

    footerLeading: BButton {
        objectName: "macroDialogCode"
        visible: dialog.extState === "approval" && dialog.path !== ""
        text: qsTr("Read the code")
        variant: "ghost"
        onClicked: {
            dialog.close()
            AppController.openPage(dialog.path, 0)
        }
    }

    onAccepted: {
        switch (dialog.extState) {
        case "approval": // (its code first: the approval of the version read)
            AppController.requestUiAction("approveExtension:" + dialog.path)
            break
        case "off":
            if (dialog.builtin || dialog.path === "")
                AppController.navigate({ view: "settings", section: "extensions" })
            else
                Workspace.extensions.setPageOn(dialog.path, true)
            break
        case "error":
            AppController.openPage(dialog.path, Math.max(0, AppController.routeInt(info.map, "line", 0)))
            break
        default:
            AppController.navigate({ view: "settings", section: "extensions" })
        }
    }
}
