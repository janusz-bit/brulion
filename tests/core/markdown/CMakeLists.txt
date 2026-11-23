# Tests of the markdown module (brulion_core_markdown), one executable per area.
foreach(_t tokens inline frontmatter parse model edit blockdoc render renderedtext notemode noteoptions pageprops pagelook highlight math mathsafety linkformat blockclipboard tokenedit taskdeps macros)
    brulion_add_test(tst_markdown_${_t} SOURCES tst_markdown_${_t}.cpp LIBS brulion_core_markdown)
endforeach()
# The renderer's HTML as seen by Qt's rich-text engine (QTextDocument, QtGui; offscreen).
brulion_add_test(tst_markdown_richtext SOURCES tst_markdown_richtext.cpp LIBS brulion_core_markdown Qt6::Gui)
# Time tracking in :LOGBOOK: drawers (local time with Polish daylight-saving rules: tests/common).
brulion_add_test(tst_markdown_logbook SOURCES tst_markdown_logbook.cpp LIBS brulion_core_markdown)
# RenderedText parity (markdown/renderedtext.h): the renderer's tests built again with a check of
# every rendering they make — runsToHtml(renderRuns(x)) == render(x) (renderparity.cpp).
foreach(_t render highlight pagelook mathsafety macros)
    brulion_add_test(tst_markdown_renderparity_${_t} SOURCES tst_markdown_${_t}.cpp renderparity.cpp
                     LIBS brulion_core_markdown)
endforeach()
brulion_add_test(tst_markdown_renderparity_richtext SOURCES tst_markdown_richtext.cpp renderparity.cpp
                 LIBS brulion_core_markdown Qt6::Gui)
