# Tests of the notebook module (brulion_core_notebook): quick capture lines, journals, pages and
# folders (create, rename and move keeping links and tags, delete), item references, JSON names
# and shapes, and Notebook (a vault opened headless: no folder created, lazy index, the write
# lock, compare-and-swap edits with retries and History).
foreach(_t capture journals pages itemref json session)
    brulion_add_test(tst_notebook_${_t} SOURCES tst_notebook_${_t}.cpp LIBS brulion_core_notebook)
endforeach()
