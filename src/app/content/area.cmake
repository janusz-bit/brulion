# Content area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths are
# relative to src/app): the brulion_content library (content/CMakeLists.txt), its QML registration
# (content/contentqml.h) and the views' sub-components in qml/content (page header, backlinks,
# side panel, journal days, dialogs). The views themselves — JournalsView, PageView, AllPagesView,
# TagsView, SearchView, TrashView, HistoryView — are registered with the other views
# (shell/views.cmake).
add_subdirectory(content)
target_link_libraries(brulion PRIVATE brulion_content)
# The generated QML type registration includes the content headers by file name.
target_include_directories(brulion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/content)
target_sources(brulion PRIVATE content/contentqml.h)
qt_target_qml_sources(brulion
    QML_FILES
        qml/content/ContentBacklinks.qml
        qml/content/ContentRefRow.qml
        qml/content/ContentRefGroupHeader.qml
        qml/content/ContentPageHeader.qml
        qml/content/ContentSidePanel.qml
        qml/content/ContentOutline.qml
        qml/content/ContentMiniCalendar.qml
        qml/content/ContentJournalDay.qml
        qml/content/ContentMissedBanner.qml
        qml/content/ContentRenameDialog.qml
        qml/content/ContentExportDialog.qml
        qml/content/ContentNewPageDialog.qml
        qml/content/ContentNoteModeChoice.qml
        qml/content/ContentHighlightText.qml
        qml/content/ContentJournalsSelfTest.qml
)
