# brulion_editor — the C++ side of the block editor (docs/UI.md "The block editor"): BlockListModel,
# PageController (+ DayClock: the date for every open page), MarkdownHighlighter, SuggestionModel, EditorInput (TextEdit text and input-method
# helpers), QueryResultModel (live ```query results), the pure helpers of blocktext.h and the
# in-memory FakeEditorHost (playground, tests; queries run on the real index module).
# A static library so tests/app/editor links it without the app; the app's QML module "Brulion"
# registers the classes through QML_FOREIGN wrappers (editorforeign.h, included by area.cmake),
# which needs the metatypes extracted below.

# EditorHost (editorhost.h, the contract between the editor and the shell) is a header-only
# QObject interface whose moc output must be compiled exactly once: the tiny library
# brulion_editorhost provides it, with the picture size helper both sides use (picturesize.h:
# the editor for local files, the shell's Workspace for the browser's data: URLs). The editor area
# is included before the shell's, which reuses this target (src/app/shell/area.cmake defines the
# same one only when it is missing).
if(NOT TARGET brulion_editorhost)
    qt_add_library(brulion_editorhost STATIC ${CMAKE_CURRENT_SOURCE_DIR}/editorhost.h
        ${CMAKE_CURRENT_SOURCE_DIR}/picturesize.h ${CMAKE_CURRENT_SOURCE_DIR}/picturesize.cpp)
    target_include_directories(brulion_editorhost PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(brulion_editorhost PUBLIC Qt6::Core PRIVATE Qt6::Gui)
    qt_extract_metatypes(brulion_editorhost)
    brulion_set_warnings(brulion_editorhost)
endif()

qt_add_library(brulion_editor STATIC
    blocktext.h blocktext.cpp
    blockrender_p.h
    blocklistmodel.h blocklistmodel.cpp blocklistmodel_transclusions.cpp
    queryhost.h
    queryresultmodel.h queryresultmodel.cpp
    pagecontroller.h pagecontroller.cpp pagecontroller_selection.cpp
    dayclock.h dayclock.cpp
    markdownhighlighter.h markdownhighlighter.cpp
    suggestionmodel.h suggestionmodel.cpp
    fakeeditorhost.h fakeeditorhost.cpp
    editorinput.h editorinput.cpp
)
target_include_directories(brulion_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_editor
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick brulion_editorhost brulion_core_markdown
    PRIVATE brulion_core_calc brulion_core_dates brulion_core_text brulion_core_util brulion_core_index)
# External changes while the page has unsaved edits are merged three-way with sync::merge3 when
# the sync module is part of the build (otherwise the local text is kept and a conflict flagged).
if(TARGET brulion_core_sync)
    target_link_libraries(brulion_editor PRIVATE brulion_core_sync)
    target_compile_definitions(brulion_editor PRIVATE BRULION_EDITOR_HAVE_SYNC_MERGE=1)
endif()
# Browser: the browser's file picker for pictures (QFileDialog::getOpenFileContent).
if(EMSCRIPTEN)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    target_link_libraries(brulion_editor PRIVATE Qt6::Widgets)
endif()
qt_extract_metatypes(brulion_editor)
brulion_set_warnings(brulion_editor)
