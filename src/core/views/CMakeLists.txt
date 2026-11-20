# views — database views (docs/SYNTAX.md §10 "Views", "Obsidian Bases"): a query's or a base's rows
# as a table, a board, a gallery, a calendar or a list, with columns of built-ins, properties and
# formulas (dataview/expression.h), groups and summaries (view.h); pure edits of cells, board moves,
# new rows and the query's own text (edits.h); Obsidian's .base files and ```base blocks read and
# written back (bases.h). Public headers: view.h, edits.h, bases.h; internal headers end in _p.h.
brulion_add_core_module(views
    SOURCES
        view.h queryview.cpp summary.cpp
        edits.h edits.cpp
        bases.h bases.cpp
        cells_p.h cells.cpp
        messages_p.h messages.cpp
    DEPS util text dates markdown index dataview)
