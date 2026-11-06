# stats — word and character counts and the reading time of a note or a selection (the page's
# footer and More menu, docs/UI.md "Statistics and focus mode"): what a reader sees of the
# Markdown (markdown module: frontmatter, tokens, inline markup) counted by Polish-aware rules.
brulion_add_core_module(stats SOURCES textstats.h textstats.cpp DEPS markdown)
