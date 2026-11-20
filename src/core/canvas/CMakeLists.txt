# canvas — whiteboards in the JSON Canvas format (jsoncanvas.org; Obsidian Canvas, docs/SYNTAX.md §2
# "Canvases"): the model (text, file, link and group nodes, edges), the file read and written back
# with its layout and unknown keys kept (an own JSON reader that keeps spans), pure edits, the
# geometry of cards and edges, the merge of two versions, the Markdown text the index reads and the
# rewriting of paths and links on a rename, the SVG picture of a canvas.
# QtCore only.
brulion_add_core_module(canvas
    SOURCES
        canvas.h canvasfile.cpp canvasfile_p.h
        json_p.h json.cpp
        geometry.h geometry.cpp
        edit.h edit.cpp
        merge.h merge.cpp
        indextext.h indextext.cpp
        svg.h svg.cpp
    DEPS util text markdown)
