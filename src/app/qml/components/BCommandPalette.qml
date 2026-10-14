pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BCommandPalette — Ctrl+K: run any command by typing part of its name.
//
//   BCommandPalette {
//       id: palette
//       commands: [
//           { title: qsTr("Dzisiejszy dziennik"), icon: "book-open", shortcut: "Ctrl+J", action: () => app.openToday() },
//           { title: qsTr("Synchronizuj teraz"), subtitle: qsTr("Nextcloud"), icon: "refresh-cw", action: () => sync.now() }
//       ]
//   }
//   Shortcut { sequence: "Ctrl+K"; onActivated: palette.open() }
//
// Each command: { title, subtitle?, icon?, shortcut?, action (function), enabled? }.
// Disabled commands (enabled: false) are hidden. Activating closes the palette, then calls
// the action; commandActivated(command) is emitted too.
BPalette {
    id: control

    property var commands: []

    signal commandActivated(var command)

    items: (commands || []).filter(c => c.enabled !== false)
    placeholderText: qsTr("Type a command…")
    emptyTitle: qsTr("No matching commands")
    defaultIcon: "command"

    onItemActivated: (item) => {
        commandActivated(item)
        if (typeof item.action === "function")
            item.action()
    }
}
