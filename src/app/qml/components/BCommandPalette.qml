pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BCommandPalette — Ctrl+K: run any command by typing part of its name.
//
//   BCommandPalette {
//       id: palette
//       commands: [
//           { id: "today", title: qsTr("Today's journal"), icon: "book-open", shortcut: "Ctrl+J" },
//           { id: "sync", title: qsTr("Sync now"), subtitle: qsTr("Nextcloud"), icon: "refresh-cw" }
//       ]
//       onCommandIdActivated: (id) => app.run(id)
//   }
//   Shortcut { sequence: "Ctrl+K"; onActivated: palette.open() }
//
// Each command: { id?, title, subtitle?, icon?, shortcut?, enabled? }. Disabled commands
// (enabled: false) are hidden. Activating closes the palette, then emits commandIdActivated(id)
// (its `id` as text, "" without one).
BPalette {
    id: control

    property list<var> commands

    signal commandIdActivated(string id)

    items: UiModels.enabledRows(commands)
    placeholderText: qsTr("Type a command…")
    emptyTitle: qsTr("No matching commands")
    defaultIcon: "command"

    onRowActivated: (row) => control.commandIdActivated(control.itemString(row, "id"))
}
