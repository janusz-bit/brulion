# diagram — Mermaid diagrams in notes (docs/SYNTAX.md §11 "Mermaid diagrams"): Brulion's own parser and
# layout of ```mermaid blocks — flowcharts (a layered layout, subgraphs laid out inside out),
# sequence diagrams and pie charts — into a scene of paths and text in theme roles, painted by the
# app (src/app/editor/mermaidview.h) or written as safe SVG (svg.h, the HTML export). Nothing from
# a note is ever executed; every input is bounded (diagram.h limits). QtCore only.
brulion_add_core_module(diagram
    SOURCES
        diagram.h diagram.cpp
        common_p.h
        palette.cpp
        svg.h svg.cpp
        layered_p.h layered.cpp
        flowchart_p.h flowchart_parse.cpp flowchart_layout.cpp
        sequence_p.h sequence.cpp
        pie_p.h pie.cpp)
