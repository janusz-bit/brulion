# Mermaid diagrams (diagram/diagram.h): the type, the parsers of flowcharts, sequence diagrams and
# pie charts (every form, Polish labels, errors with their lines, limits), the layout's properties
# (no overlaps, links along the direction, subgraphs around their members, ends on the shapes,
# determinism), the scenes and the SVG (well-formed, escaped, nothing active).
brulion_add_test(tst_diagram SOURCES tst_diagram.cpp LIBS brulion_core_diagram)
brulion_add_test(tst_diagram_layout SOURCES tst_diagram_layout.cpp LIBS brulion_core_diagram)
