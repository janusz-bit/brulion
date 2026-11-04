# Charts (chart/chart.h): reading ```chart blocks (keys, Polish names, comments, errors with their
# lines), the data from tables (formulas, summary rows, columns by name or letter), calc blocks and
# query groups, hostile input (huge tables, NaN/inf, long labels), the layout's bounds and the SVG.
brulion_add_test(tst_chart SOURCES tst_chart.cpp LIBS brulion_core_chart)
