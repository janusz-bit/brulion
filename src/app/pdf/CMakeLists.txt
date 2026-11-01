# brulion_pdfview — the PDF viewer's C++ (docs/UI.md "PDF viewer", docs/PERFORMANCE.md "PDF
# viewer"): the threads PDFs are drawn and searched on and the open PDFs they share (pdfrenderpool.h:
# PdfRenderPool, PdfReaderPool), the thumbnails of embedded PDFs cached as PNG files (pdfthumbnails.h:
# PdfThumbnails, owned by the shell's Workspace), the pages' exact layout (pdflayout.h, pure), the
# viewer's state (pdfviewmodel.h: PdfViewModel, PdfVisiblePages) and its image provider "pdfview"
# (pdfviewimageprovider.h). PDFs through brulion_platform's pdfpages.h (PdfReader). A static library
# so tests/app/pdf links it without the app; the app's QML module registers PdfViewModel through
# pdfqml.h. The browser build compiles it too, but starts nothing (no renderer, no threads).
qt_add_library(brulion_pdfview STATIC
    pdfrenderpool.h pdfrenderpool.cpp
    pdfthumbnails.h pdfthumbnails.cpp
    pdflayout.h pdflayout.cpp
    pdfviewmodel.h pdfviewmodel.cpp
    pdfviewimageprovider.h pdfviewimageprovider.cpp
)
target_include_directories(brulion_pdfview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_pdfview
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick brulion_platform
    PRIVATE brulion_core_text)
qt_extract_metatypes(brulion_pdfview)
brulion_set_warnings(brulion_pdfview)
