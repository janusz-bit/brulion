# brulion_attachments — the text of the notebook's PDFs and pictures for the whole-notebook search
# (docs/UI.md "Searching attachments"): AttachmentIndexer (attachmentindexer.h) reads the PDFs' text
# layers (brulion_platform's pdfpages.h through brulion_pdfview's PdfReaderPool) and runs OCR
# (platform/ocr.h) on a worker thread, keeps the result in the device's cache (core attachments:
# record.h, manifest.h) and searches it (attachmentsearch.h). Owned by the shell's Workspace. A
# static library so tests/app/attachments links it without the app; the app's QML module registers
# the type through attachqml.h.
qt_add_library(brulion_attachments STATIC
    attachmentindexer.h attachmentindexer.cpp
)
target_include_directories(brulion_attachments PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_attachments
    PUBLIC Qt6::Core Qt6::Gui brulion_core_attachments
    PRIVATE brulion_platform brulion_pdfview brulion_core_vault brulion_core_index brulion_core_util
            brulion_core_text brulion_core_log)
qt_extract_metatypes(brulion_attachments)
brulion_set_warnings(brulion_attachments)
