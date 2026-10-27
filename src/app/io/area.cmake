# Sync and import/export area of the app (included by src/app/CMakeLists.txt in its scope, so
# relative paths are relative to src/app): the static library brulion_ioviews — the C++ side of
# SyncView, ImportExportView and the "Synchronizacja" section of the settings — and its QML.
#
#   ImportController   importers with how-to texts (also the phone's steps), reading picked files /
#                      folders (desktop and Android: QML FileDialog / FolderDialog — on Android
#                      content:// documents and document trees through FileGatherer's
#                      DocumentProvider and the platform bridge; a typed path; browser:
#                      QFileDialog::getOpenFileContent), io::detectImporter with an override, the target
#                      folder, a dry-run preview, writing with the collision policy (existing
#                      daily note: appended under "## Import (<source>)"; other files: unique
#                      names), progress and the report
#   MsTodoImport       "Sign in to Microsoft" on the Microsoft To Do card: the device-code sign-in
#                      with the user's app registration (client id + authority in AppSettings),
#                      the Graph download (core module mstodo), the bundle handed to
#                      ImportController::setInput (preview, report)
#   ExportController   exporters, scope (vault / folder / page with its attachments), progress,
#                      saving (FileDialog URL incl. Android content:// documents, an export folder
#                      fallback, QFileDialog::saveFileContent in the browser)
#   NextcloudSetup     the Nextcloud form: drafts, validation, the WebDAV address, "Test
#                      connection" with explained errors (401, 404 + create the folder, TLS,
#                      CORS in the browser), saving into AppSettings
#   SyncthingSetup     Syncthing REST settings and the recommended .stignore
#   ConflictResolver   one conflict copy: both texts, a base from the version history, merge3
#                      hunks for BMergeView, keep mine / keep the copy / merge
# Tests: tests/app/io. The library uses the shell's Workspace and AppSettings (brulion_shell,
# defined by the shell area, which CMake includes after this one — target names resolve later).

set(_brulion_ioviews_dir ${CMAKE_CURRENT_LIST_DIR})

qt_add_library(brulion_ioviews STATIC
    ${_brulion_ioviews_dir}/iobackground.h
    ${_brulion_ioviews_dir}/fileexchange.h ${_brulion_ioviews_dir}/fileexchange.cpp
    ${_brulion_ioviews_dir}/importcontroller.h ${_brulion_ioviews_dir}/importcontroller.cpp
    ${_brulion_ioviews_dir}/importtexts.cpp
    ${_brulion_ioviews_dir}/mstodoimport.h ${_brulion_ioviews_dir}/mstodoimport.cpp
    ${_brulion_ioviews_dir}/exportcontroller.h ${_brulion_ioviews_dir}/exportcontroller.cpp
    ${_brulion_ioviews_dir}/syncsetup.h ${_brulion_ioviews_dir}/syncsetup.cpp
    ${_brulion_ioviews_dir}/conflictresolver.h ${_brulion_ioviews_dir}/conflictresolver.cpp
)
target_include_directories(brulion_ioviews PUBLIC ${_brulion_ioviews_dir})
target_link_libraries(brulion_ioviews
    PUBLIC Qt6::Core Qt6::Gui brulion_shell brulion_core_io brulion_core_sync brulion_core_vault
    PRIVATE Qt6::Network brulion_core_mstodo brulion_core_util brulion_core_markdown brulion_core_index brulion_core_dates)
if(ANDROID)
    # "Save to Downloads": MediaStore through the platform's Android bridge.
    target_link_libraries(brulion_ioviews PRIVATE brulion_platform)
endif()
if(EMSCRIPTEN)
    # QFileDialog::getOpenFileContent / saveFileContent: the browser's file picker and download.
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    target_link_libraries(brulion_ioviews PRIVATE Qt6::Widgets)
endif()
qt_extract_metatypes(brulion_ioviews)
brulion_set_warnings(brulion_ioviews)

# --- The app: QML registration and QML files ---------------------------------------------------
target_sources(brulion PRIVATE io/ioqml.h)
# The generated QML type registration includes the headers by file name.
target_include_directories(brulion PRIVATE ${_brulion_ioviews_dir})
target_link_libraries(brulion PRIVATE brulion_ioviews)

# Desktop builds: the import self-test of the real app (`--selftest-import`, ioselftest.h; ctest
# tst_appio_import_qml*), started by main.cpp like the shell's self-test.
if(BRULION_DESKTOP)
    target_sources(brulion PRIVATE ${_brulion_ioviews_dir}/ioselftest.h ${_brulion_ioviews_dir}/ioselftest.cpp)
endif()

# SyncView.qml and ImportExportView.qml are registered with the other views (shell/views.cmake).
qt_target_qml_sources(brulion QML_FILES
    qml/io/IoCard.qml
    qml/io/IoStep.qml
    qml/io/SyncStatusPanel.qml
    qml/io/SyncLiveProgress.qml
    qml/io/SyncHistoryList.qml
    qml/io/SyncRunDetails.qml
    qml/io/SyncDeletionGuard.qml
    qml/io/NextcloudForm.qml
    qml/io/SyncthingPanel.qml
    qml/io/SyncConflictList.qml
    qml/io/SyncConflictResolver.qml
    qml/io/SyncSettings.qml
    qml/io/ImportPanel.qml
    qml/io/ImportSourcePicker.qml
    qml/io/MsTodoSignIn.qml
    qml/io/ImportPreview.qml
    qml/io/ImportPathCard.qml
    qml/io/ExportPanel.qml
    qml/io/IoPathPickerDialog.qml
)
