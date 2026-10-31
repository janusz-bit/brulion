# The PDF viewer (library brulion_pdfview, src/app/pdf): the pages' exact layout (pure: fit width,
# centring, zoom anchors, hostile sizes, 20 000 pages fast), the thumbnails' disk cache (an injected
# renderer counting its calls: keys, PNG text chunks, failures cached, a broken PNG drawn again,
# pruning, an unwritable folder, one render for many asks; one real case), and the viewer's state
# with this build's renderer on tests/data/pdf (opening, locked and missing files, visible pages,
# zoom, detail pictures, search without diacritics, cancelling, a textless PDF, a reload keeping the
# page, a page that failed) — skipped where the build has no renderer.
if(TARGET brulion_pdfview)
    brulion_add_test(tst_pdflayout SOURCES tst_pdflayout.cpp LIBS brulion_pdfview)
    brulion_add_test(tst_pdfthumbnails SOURCES tst_pdfthumbnails.cpp LIBS brulion_pdfview brulion_platform)
    brulion_add_test(tst_pdfviewmodel SOURCES tst_pdfviewmodel.cpp LIBS brulion_pdfview brulion_platform Qt6::Quick)
    set_tests_properties(tst_pdfviewmodel PROPERTIES TIMEOUT 120)
endif()
