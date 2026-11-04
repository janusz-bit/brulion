# chart — charts in notes (docs/SYNTAX.md §9 "Charts"): a ```chart block read (spec.cpp), its numbers
# from a table of the page (formulas evaluated), a ```calc block or a query's groups (data.cpp), laid
# out as a diagram scene — bars, lines, a pie with axes and a legend (layout.cpp) — painted by the
# app (MermaidView's painter, src/app/editor/chartview.h) or written as SVG by the HTML export.
# Nothing from a note is executed; every input is bounded (chart.h limits). QtCore only.
brulion_add_core_module(chart
    SOURCES
        chart.h messages_p.h
        spec.cpp data.cpp layout.cpp
    DEPS text calc diagram)
