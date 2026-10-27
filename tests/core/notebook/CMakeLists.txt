# Tests of the notebook module (brulion_core_notebook): quick capture lines, journals, pages and
# folders (create, rename and move keeping links and tags, delete), the links Brulion copies, item
# references, JSON names and shapes, and Notebook (a vault opened headless: no folder created,
# lazy index, the write lock, compare-and-swap edits with retries and History), and shares from
# other apps (capture text, page title and content, decoding, the platform's JSON).
foreach(_t capture journals pages links itemref json session share)
    brulion_add_test(tst_notebook_${_t} SOURCES tst_notebook_${_t}.cpp LIBS brulion_core_notebook)
endforeach()
