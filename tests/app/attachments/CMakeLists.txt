# The attachments' text for the whole-notebook search (library brulion_attachments,
# src/app/attachments): the indexer on a notebook with the fixtures of tests/data (a text PDF, a
# scanned page, a whiteboard's picture, a photo stored sideways) — the text layer, no PDF left open
# once read, the device cache reused, renames, changes, OCR where Tesseract is (pause, languages,
# highlights, the viewer's lookup), the cache cleared; documents (DOCX, PPTX, ODT, EPUB) of a notebook of
# their own, damaged and over-limit ones by name only; EXIF orientations, the OCR resolution.
if(TARGET brulion_attachments)
    brulion_add_test(tst_attachmentindexer SOURCES tst_attachmentindexer.cpp
        LIBS brulion_attachments brulion_pdfview brulion_platform brulion_core_vault brulion_core_text brulion_core_io)
    # OCR of a few pictures and pages: seconds, much longer under the sanitizers.
    set_tests_properties(tst_attachmentindexer PROPERTIES TIMEOUT 900)
endif()
