# markdown — the Brulion Markdown dialect (docs/SYNTAX.md): parseNote, inline links/tags, tokens,
# YAML frontmatter, pure content edits, journals, templates, BlockDocument (block editor model)
# and InlineRenderer (Qt rich text). Public headers: markdown.h (overview) and the headers it
# includes; internal headers end in _p.h.
brulion_add_core_module(markdown
    SOURCES
        markdown.h
        blockdocument.h blockdocument.cpp
        edit.h edit.cpp
        frontmatter.h frontmatter.cpp
        inline.h inline_p.h inline.cpp
        inlinerenderer.h inlinerenderer.cpp
        journal.h journal.cpp
        lines_p.h lines.cpp
        parse.h parse_p.h parse.cpp
        properties.h properties.cpp
        rename.h rename.cpp
        structure_p.h structure.cpp
        tasks.h tasks.cpp
        templates.h templates.cpp
        tokens.h tokens_p.h tokens.cpp
    DEPS util text dates)
