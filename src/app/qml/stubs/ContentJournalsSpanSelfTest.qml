import QtQuick
import Brulion

// ContentJournalsSpanSelfTest stand-in for the Android and browser builds that are shipped
// (BRULION_DEV_QML off, src/app/CMakeLists.txt): JournalsView's `--editor-script
// journals-span-selftest` loader still finds the type, which does nothing here. The real one is
// qml/content/ContentJournalsSpanSelfTest.qml.
Item {
    property ListView list
    property JournalListModel days
    property StreamSelection stream
}
