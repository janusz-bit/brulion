# Tests of the io module (brulion_core_io): archives and formats, HTML -> Markdown, every importer
# and exporter, and the Logseq round trip. QTEST_MAIN with the offscreen platform (QtGui is used
# for QTextDocument). iotest.h holds shared helpers (fixtures built in code, a tiny tar writer).
foreach(_t formats html import logseq import_apps import_ics export robust)
    brulion_add_test(tst_io_${_t} SOURCES tst_io_${_t}.cpp iotest.h LIBS brulion_core_io)
endforeach()
# Trilium: real exports of Trilium servers (fixtures/trilium, see its README) plus synthetic ones.
brulion_add_test(tst_io_import_trilium SOURCES tst_io_import_trilium.cpp iotest.h LIBS brulion_core_io)
target_compile_definitions(tst_io_import_trilium
    PRIVATE BRULION_TRILIUM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/trilium")
