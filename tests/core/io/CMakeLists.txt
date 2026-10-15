# Tests of the io module (brulion_core_io): archives and formats, HTML -> Markdown, every importer
# and exporter, and the Logseq round trip. QTEST_MAIN with the offscreen platform (QtGui is used
# for QTextDocument). iotest.h holds shared helpers (fixtures built in code, a tiny tar writer).
foreach(_t formats html import logseq import_apps import_ics export robust)
    brulion_add_test(tst_io_${_t} SOURCES tst_io_${_t}.cpp iotest.h LIBS brulion_core_io)
endforeach()
