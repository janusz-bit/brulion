# calc — Brulion's own expression engine: calc blocks, inline calculations, table formulas.
# Public headers: calc.h (overview), calclocale.h, errors.h, numbers.h, units.h, value.h, table.h.
# Internal (*_p.h): lexer, parser, AST, evaluator, Markdown line helpers.
brulion_add_core_module(calc
    SOURCES
        calc.h calclocale.h errors.h numbers.h table.h units.h value.h
        ast_p.h blocks_p.h evaluator_p.h lexer_p.h parser_p.h util_p.h
        blocks.cpp calc.cpp errors.cpp evaluator.cpp lexer.cpp numbers.cpp pagescope.cpp parser.cpp
        table.cpp tableeval.cpp units.cpp value.cpp
    DEPS text dates)
