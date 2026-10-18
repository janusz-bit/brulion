import QtQuick
import Brulion

// ContentJournalsSelfTest stand-in for the Android and browser builds that are shipped
// (BRULION_DEV_QML off, src/app/CMakeLists.txt): JournalsView's `--editor-script
// journals-selftest` loader still finds the type, which does nothing here. The real one is
// qml/content/ContentJournalsSelfTest.qml.
Item {
    property ListView list
    property JournalListModel days
}
