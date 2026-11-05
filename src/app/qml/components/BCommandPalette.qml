pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BCommandPalette — Ctrl+K: run any command by typing part of its name.
//
//   BCommandPalette {
//       id: palette
//       commands: [
//           { id: "today", title: qsTr("Dzisiejszy dziennik"), icon: "book-open", shortcut: "Ctrl+J" },
//           { id: "sync", title: qsTr("Synchronizuj teraz"), subtitle: qsTr("Nextcloud"), icon: "refresh-cw" }
//       ]
//       onCommandIdActivated: (id) => app.run(id)
//   }
//   Shortcut { sequence: "Ctrl+K"; onActivated: palette.open() }
//
// Each command: { id?, title, subtitle?, icon?, shortcut?, enabled?, action? (function) }.
// Disabled commands (enabled: false) are hidden. Activating closes the palette, then emits
// commandIdActivated(id) (its `id` as text, "" without one) and commandActivated(command), and
// calls its `action` if it has one (the older form: a function, not compiled).
BPalette {
    id: control

    property list<var> commands

    signal commandActivated(var command)
    signal commandIdActivated(string id)

    items: UiModels.enabledRows(commands)
    placeholderText: qsTr("Type a command…")
    emptyTitle: qsTr("No matching commands")
    defaultIcon: "command"

    onRowActivated: (row) => control.commandIdActivated(control.itemString(row, "id"))
    onItemActivated: (item) => control._activated(item)

    /// commandActivated(), then the older commands' `action` function; not compiled (it calls a
    /// function).
    function _activated(item: var): void {
        commandActivated(item)
        if (typeof item.action === "function")
            item.action()
    }
}
