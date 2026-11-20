pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellExtensionApproval — "Run" of an extension page that waits for approval on this device
// (docs/EXTENSIONS.md "Extensions in the notebook", docs/UI.md "Extensions"): its code is read
// before it runs. The code against the version approved here before (BDiffView: what a sync or
// another device changed), all of it when it is new; what it may do, and what it asks for beyond
// the version before. "Run this version" approves the version shown (its hash,
// Workspace.extensions.approveVersion): when the code changes while the dialog is open (a sync
// arrives), it says so and shows the new version only on "Show the new version" — code nobody
// read is never approved. The shell makes it (AppShell.openExtensionApproval(path)).
BDialog {
    id: dialog
    objectName: "extensionApproval"

    property string path
    /// The review shown (approvalReview()): the version "Run" approves.
    MapHolder { id: review }
    readonly property string shownHash: "" + review.map.hash
    /// The code changed after it was shown.
    property bool changedMeanwhile: false
    readonly property bool isNew: review.map.before !== true
    readonly property string builtinName: "" + review.map.builtin
    readonly property list<string> permissions: AppController.routeStrings(review.map, "permissions")
    readonly property list<string> newPermissions: AppController.routeStrings(review.map, "newPermissions")

    title: qsTr("Run “%1”?").arg("" + review.map.title)
    iconName: "shield-check"
    preferredWidth: Math.min(960, Theme.windowWidth - 2 * Theme.space6)
    acceptText: qsTr("Run this version")
    acceptEnabled: !dialog.changedMeanwhile && dialog.shownHash !== ""

    /// Shows the review of `pagePath` and opens.
    function show(pagePath: string): void {
        dialog.path = pagePath
        dialog.refresh()
        dialog.open()
    }
    function refresh(): void {
        review.map = Workspace.extensions.approvalReview(dialog.path)
        dialog.changedMeanwhile = false
    }

    // (BDialog closes after this unless acceptEnabled turned false: a version that changed stays.)
    onAcceptRequested: {
        const outcome = Workspace.extensions.approveVersion(dialog.path, dialog.shownHash)
        // (Approved, it may still not run: an error on a line, shared code that waits too.)
        const state = outcome === "" ? Workspace.extensions.pageState(dialog.path) : ""
        if (outcome === "" && state === "running")
            AppController.toast(qsTr("“%1” runs on this device").arg("" + review.map.title), "success")
        else if (outcome === "")
            AppController.toast(qsTr("Approved, but it does not run: %1").arg(Workspace.extensions.pageMessage(dialog.path)), "warning")
        else if (outcome === "changed")
            dialog.changedMeanwhile = true
        else
            AppController.toast(qsTr("The page is no extension any more."), "info")
    }

    /// The code is another one now than the one shown: say so (never approved unread).
    function checkChanged(): void {
        const now = Workspace.extensions.pageHash(dialog.path)
        if (dialog.shownHash !== "" && now !== dialog.shownHash)
            dialog.changedMeanwhile = true
    }
    // From the moment it shows (its opening animation too: a sync does not wait for it).
    Connections {
        target: Workspace.extensions
        enabled: dialog.visible
        function onRevisionChanged() { dialog.checkChanged() }
    }
    onOpened: dialog.checkChanged()

    BText {
        objectName: "extensionApprovalSummary"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: dialog.builtinName !== ""
              ? qsTr("This page replaces the built-in %1. What differs from the built-in code is marked: read it before it runs.").arg(dialog.builtinName)
              : dialog.isNew
                ? qsTr("This code is new on this device: it came from a sync, an import or another program. Read it before it runs.")
                : qsTr("Its code changed since you approved it on this device: %1 line(s) added, %2 removed. Read what changed before it runs.")
                    .arg(0 + review.map.added).arg(0 + review.map.removed)
    }

    Rectangle {
        objectName: "extensionApprovalChanged"
        Layout.fillWidth: true
        visible: dialog.changedMeanwhile
        implicitHeight: changedRow.implicitHeight + 2 * Theme.space2
        radius: Theme.radius
        color: Theme.warningBg
        border.color: Theme.warning
        RowLayout {
            id: changedRow
            x: Theme.space2
            y: Theme.space2
            width: parent.width - 2 * Theme.space2
            spacing: Theme.space2
            BIcon { name: "triangle-alert"; size: Theme.iconSizeSm; color: Theme.warningText }
            BText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("The code changed while you read it (a sync?). Nothing was approved: read the new version.")
            }
            BButton {
                objectName: "extensionApprovalReload"
                size: "small"
                text: qsTr("Show the new version")
                onClicked: dialog.refresh()
            }
        }
    }

    // What it may do: reading always; the permissions of its page, those not approved before marked.
    Flow {
        Layout.fillWidth: true
        spacing: Theme.space2
        BBadge { text: qsTr("May read notes"); variant: "neutral" }
        Repeater {
            model: dialog.permissions
            delegate: BBadge {
                required property string modelData
                text: qsTr("May %1").arg(modelData)
                variant: dialog.newPermissions.indexOf(modelData) >= 0 ? "warning" : "neutral"
            }
        }
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.newPermissions.length > 0
        wrapMode: Text.Wrap
        variant: "small"
        color: Theme.warningText
        text: qsTr("It asks for more than the version approved before: %1.").arg(dialog.newPermissions.join(", "))
    }

    BDiffView {
        objectName: "extensionApprovalDiff"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(220, Math.min(520, Theme.windowHeight - 360))
        oldText: "" + review.map.oldCode
        newText: "" + review.map.code
        oldLabel: dialog.builtinName !== "" ? qsTr("The built-in code") : dialog.isNew ? qsTr("Nothing approved") : qsTr("Approved before")
        newLabel: qsTr("Now")
        // (All of the code is new: one column. A change: side by side where there is room.)
        mode: Theme.narrow || dialog.isNew ? "unified" : "auto"
    }
}
