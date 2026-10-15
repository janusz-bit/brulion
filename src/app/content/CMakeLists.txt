# brulion_content — the C++ side of the content views (docs/UI.md: JournalsView, PageView,
# AllPagesView, TagsView, SearchView, TrashView, HistoryView and the side panel): JournalListModel,
# BacklinksModel / TagBlocksModel (block references grouped by page, unlinked mentions -> links),
# TagListModel, PageListModel, SearchResultModel, HistoryModel, OutlineModel and PageTools (rename
# impact, properties summary, wiki links, page export), on the WorkspaceListModel base (diffed
# updates, so delegates survive background changes) and the pure helpers of contentutil.h.
# A static library so tests/app/content links it without the app; the app's QML module "Brulion"
# registers the classes through QML_FOREIGN wrappers (contentqml.h, included by area.cmake), which
# needs the metatypes extracted below. Models take the shell's Workspace (brulion_shell, defined
# by shell/area.cmake — CMake resolves the name when it generates).
qt_add_library(brulion_content STATIC
    contentutil.h contentutil.cpp
    workspacemodel.h workspacemodel.cpp
    journallistmodel.h journallistmodel.cpp
    refmodels.h refmodels.cpp
    taglistmodel.h taglistmodel.cpp
    pagelistmodel.h pagelistmodel.cpp
    searchmodel.h searchmodel.cpp
    historymodel.h historymodel.cpp
    outlinemodel.h outlinemodel.cpp
    pagetools.h pagetools.cpp
)
target_include_directories(brulion_content PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_content
    PUBLIC Qt6::Core Qt6::Gui brulion_shell brulion_core_index brulion_core_markdown brulion_core_dates
    PRIVATE brulion_core_text brulion_core_util brulion_core_vault brulion_core_io)
qt_extract_metatypes(brulion_content)
brulion_set_warnings(brulion_content)
