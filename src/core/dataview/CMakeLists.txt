# dataview — Obsidian Dataview queries (```dataview blocks, docs/SYNTAX.md §10 "Dataview
# queries"): the Dataview Query Language read by our own parser (parse.cpp) and evaluated against
# the VaultIndex (run.cpp, eval.cpp) — nothing from a note is executed as a script. Standalone
# expressions (expression.h): the formulas of query views and Obsidian Bases' expressions
# (bases.cpp, translated into the same evaluator). Public headers: dataview.h (with value.h),
# expression.h; internal headers end in _p.h.
brulion_add_core_module(dataview
    SOURCES
        dataview.h value.h value.cpp
        ast_p.h parse.cpp
        eval_p.h eval.cpp functions.cpp objects.cpp
        run.cpp inline.cpp
        expression.h expression_p.h expression.cpp bases.cpp
        messages_p.h messages.cpp
    DEPS util text dates markdown index)
