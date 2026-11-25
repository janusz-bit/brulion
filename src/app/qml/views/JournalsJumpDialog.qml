pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// JournalsJumpDialog — the journal stream's "Go to day" (JournalsView): a month grid marking the
// days with a journal; a day picked or typed is `dayPicked(day)` ("YYYY-MM-DD"). Loaded from its
// file the first time it opens (docs/PERFORMANCE.md "L10" and "QML files loaded when first used":
// neither it nor its date picker is on the first screen). Its texts keep JournalsView's translation
// context, where it was written first.
BDialog {
    id: jumpDialog

    signal dayPicked(day: string)

    parent: T.Overlay.overlay
    title: qsTranslate("JournalsView", "Go to day")
    iconName: "calendar-days"
    showFooter: false
    preferredWidth: Theme.dialogWidthSm

    // The days with a journal: only while the dialog is open, following the names
    // (journals created or deleted; read for real: a bare `void Workspace.namesRevision`
    // is compiled away with the dependency).
    readonly property var marks: jumpDialog.visible && Workspace.namesRevision >= 0
                                 ? Workspace.journalMarks(jumpPicker.cells) : ({})

    BDatePicker {
        id: jumpPicker
        Layout.fillWidth: true
        selectedDate: DatesHelper.today
        marks: jumpDialog.marks
        onPicked: (value) => {
            const d = "" + Js.slice(value, 0, 10)
            jumpDialog.close()
            jumpDialog.dayPicked(d)
        }
    }
    BText {
        Layout.fillWidth: true
        text: qsTranslate("JournalsView", "Days with a dot have a journal. Type a date like “Tuesday” or “October 12”.")
        variant: "muted"
        wrapMode: Text.Wrap
    }
}
