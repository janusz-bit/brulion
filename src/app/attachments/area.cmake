# Attachments area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths
# are relative to src/app): the text of PDFs, documents and pictures in the whole-notebook search (docs/UI.md
# "Searching attachments") — the brulion_attachments library (attachments/CMakeLists.txt) and its
# QML registration (attachments/attachqml.h: the type of Workspace.attachments). Its views live
# with the search (qml/views/SearchView.qml), the settings (qml/attachments/AttachmentSettings.qml)
# and the picture view. Included before the content and shell areas (sorted): Workspace owns the
# indexer, the search model asks it.
add_subdirectory(attachments)
target_link_libraries(brulion PRIVATE brulion_attachments)
# The generated QML type registration includes the header by file name.
target_include_directories(brulion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/attachments)
target_sources(brulion PRIVATE attachments/attachqml.h)
qt_target_qml_sources(brulion
    QML_FILES
        qml/attachments/AttachmentSettings.qml
)
