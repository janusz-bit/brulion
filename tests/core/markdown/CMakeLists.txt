# Tests of the markdown module (brulion_core_markdown), one executable per area.
foreach(_t tokens inline frontmatter parse edit blockdoc render notemode math mathsafety linkformat)
    brulion_add_test(tst_markdown_${_t} SOURCES tst_markdown_${_t}.cpp LIBS brulion_core_markdown)
endforeach()
# The renderer's HTML as seen by Qt's rich-text engine (QTextDocument, QtGui; offscreen).
brulion_add_test(tst_markdown_richtext SOURCES tst_markdown_richtext.cpp LIBS brulion_core_markdown Qt6::Gui)
