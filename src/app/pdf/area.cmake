# PDF area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths are
# relative to src/app): the PDF viewer of route "page" with a PDF's path and the thumbnails of
# embedded PDFs (docs/UI.md "PDF viewer") — the brulion_pdfview library (pdf/CMakeLists.txt), its QML
# registration (pdf/pdfqml.h: PdfViewModel) and the views: qml/content/ContentPdfView.qml (the
# viewer) and qml/editor/PdfEmbedTile.qml (an embed's thumbnail in a block). Included before the
# shell's area (sorted): Workspace links brulion_pdfview for its PdfThumbnails.
add_subdirectory(pdf)
target_link_libraries(brulion PRIVATE brulion_pdfview)
# The generated QML type registration includes the PDF headers by file name.
target_include_directories(brulion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pdf)
target_sources(brulion PRIVATE pdf/pdfqml.h)
qt_target_qml_sources(brulion
    QML_FILES
        qml/content/ContentPdfView.qml
        qml/editor/PdfEmbedTile.qml
)
