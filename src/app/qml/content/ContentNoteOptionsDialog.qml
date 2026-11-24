pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentNoteOptionsDialog — the note's own options (docs/UI.md "Note options", docs/SYNTAX.md §4
// "Note options"): whether its tasks are in Tasks and the calendar, line numbers beside its code
// blocks, frames around its pictures, the light background under its pictures in a dark theme and
// its text width. "As in Settings" leaves the choice to
// the device's setting (Settings → Editor); any other choice is written at once into the note's
// properties (PageController.setNoteOption: one undo step each, the key in the interface's
// language), so it travels with the file and shows in the properties panel.
//
//   ContentNoteOptionsDialog { id: optionsDialog; controller: page; editor: editor }   optionsDialog.open()
BDialog {
    id: dialog

    property PageController controller
    property BlockEditor editor
    /// A map a call returned, read with AppController.route*() (compiled calls cannot take a map
    /// held in a local after another call: kept here).
    property var _held: ({})
    /// The note's options now (PageController.noteOptions).
    readonly property string lineNumbers: dialog.option("lineNumbers")
    readonly property string pictureFrames: dialog.option("pictureFrames")
    readonly property string pictureBacking: dialog.option("pictureBacking")
    readonly property string textWidth: dialog.option("textWidth")
    readonly property bool tasksIgnored: dialog.option("tasks") === "ignore"
    /// The device's text width as a name ("" for a width no choice has).
    readonly property string deviceWidth: AppSettings.editorWidth === 600 ? "narrow"
                                          : AppSettings.editorWidth === 680 ? "normal"
                                          : AppSettings.editorWidth === 960 ? "wide"
                                          : AppSettings.editorWidth === 0 ? "full" : ""

    function option(name: string): string {
        return dialog.controller ? AppController.routeText(dialog.controller.noteOptions, name) : ""
    }
    /// Write option `name` as `value` ("" — as in Settings); a refusal says why.
    function setOption(name: string, value: string): void {
        if (!dialog.controller)
            return
        dialog._held = dialog.controller.setNoteOption(name, value)
        if (!AppController.routeFlag(dialog._held, "ok"))
            AppController.toast(AppController.routeText(dialog._held, "reason"), "error")
    }
    function onOff(on: bool): string {
        return on ? qsTr("on") : qsTr("off")
    }
    function widthText(name: string): string {
        switch (name) {
        case "narrow": return qsTr("Narrow")
        case "normal": return qsTr("Normal", "text width")
        case "wide": return qsTr("Wide")
        case "full": return qsTr("Full")
        default: return qsTr("own")
        }
    }

    title: qsTr("Note options")
    subtitle: dialog.controller ? dialog.controller.title : ""
    iconName: "sliders-horizontal"
    showFooter: false

    BSwitch {
        id: tasksSwitch
        objectName: "noteOptionTasks"
        Layout.fillWidth: true
        text: qsTr("Tasks in Tasks and the calendar")
        description: qsTr("Off: this note's checkboxes stay on the page, but its tasks are left out of Tasks, the calendar and task queries.")
        onToggled: {
            dialog.setOption("tasks", tasksSwitch.checked ? "" : "ignore")
            // (A refused change: the switch shows the note as it is.)
            tasksSwitch.checked = !dialog.tasksIgnored
        }
    }
    // The note's state, also after the switch was toggled (a change from elsewhere: a sync).
    Binding {
        target: tasksSwitch
        property: "checked"
        value: !dialog.tasksIgnored
    }
    ShellSettingRow {
        Layout.fillWidth: true
        title: qsTr("Line numbers in code blocks")
        //: %1: "on" or "off", the device's setting
        description: qsTr("As in Settings: %1.").arg(dialog.onOff(AppSettings.codeLineNumbers))
        BSegmented {
            objectName: "noteOptionLineNumbers"
            accessibleName: qsTr("Line numbers in code blocks")
            model: [
                { value: "", text: qsTr("As in Settings") },
                { value: "on", text: qsTr("On") },
                { value: "off", text: qsTr("Off") }
            ]
            controlled: true
            currentValue: dialog.lineNumbers
            onActivated: (value) => dialog.setOption("lineNumbers", value)
        }
    }
    ShellSettingRow {
        Layout.fillWidth: true
        title: qsTr("Frames around pictures")
        description: qsTr("As in Settings: %1.").arg(dialog.onOff(AppSettings.pictureFrames))
        BSegmented {
            objectName: "noteOptionPictureFrames"
            accessibleName: qsTr("Frames around pictures")
            model: [
                { value: "", text: qsTr("As in Settings") },
                { value: "on", text: qsTr("On") },
                { value: "off", text: qsTr("Off") }
            ]
            controlled: true
            currentValue: dialog.pictureFrames
            onActivated: (value) => dialog.setOption("pictureFrames", value)
        }
    }
    ShellSettingRow {
        Layout.fillWidth: true
        title: qsTr("Light background under pictures")
        //: %1: "on" or "off", the device's setting
        description: qsTr("In a dark theme. As in Settings: %1.").arg(dialog.onOff(AppSettings.lightPictureBacking))
        BSegmented {
            objectName: "noteOptionPictureBacking"
            accessibleName: qsTr("Light background under pictures")
            model: [
                { value: "", text: qsTr("As in Settings") },
                { value: "on", text: qsTr("On") },
                { value: "off", text: qsTr("Off") }
            ]
            controlled: true
            currentValue: dialog.pictureBacking
            onActivated: (value) => dialog.setOption("pictureBacking", value)
        }
    }
    ShellSettingRow {
        Layout.fillWidth: true
        title: qsTr("Text width")
        //: %1: the device's text width ("Narrow", "Normal", "Wide", "Full")
        description: qsTr("As in Settings: %1.").arg(dialog.widthText(dialog.deviceWidth))
        BSegmented {
            objectName: "noteOptionTextWidth"
            accessibleName: qsTr("Text width")
            model: [
                { value: "", text: qsTr("As in Settings") },
                { value: "narrow", text: qsTr("Narrow") },
                { value: "normal", text: qsTr("Normal", "text width") },
                { value: "wide", text: qsTr("Wide") },
                { value: "full", text: qsTr("Full") }
            ]
            controlled: true
            currentValue: dialog.textWidth
            onActivated: (value) => dialog.setOption("textWidth", value)
        }
    }
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        text: qsTr("Kept in the note's properties, so the note looks the same on every device it syncs to.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
}
