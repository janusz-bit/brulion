# io — import from other apps and export to other formats (docs/ARCHITECTURE.md): importers
# (Obsidian, Logseq, Notion, Evernote, Google Keep, Joplin, Simplenote, Roam, Trilium, Bear,
# Markdown, HTML, iCalendar), exporters (Markdown / Obsidian / Logseq ZIP, HTML site, single
# HTML, JSON, iCalendar, OPML, CSV tasks), ZIP via vendored miniz, tar, CSV, iCalendar,
# HTML <-> Markdown, the clipboard's HTML of copied blocks.
# Public headers: io.h (overview) and the headers it includes; internal headers end in _p.h.
# QtGui is used for QTextDocument (HTML parsing).
if(NOT TARGET brulion_miniz)
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/miniz ${CMAKE_BINARY_DIR}/3rdparty/miniz)
endif()

brulion_add_core_module(io
    SOURCES
        io.h
        files.h
        messages_p.h
        zip.h zip.cpp
        tar.h tar.cpp
        csv.h csv.cpp
        ical.h ical.cpp
        htmlmarkdown.h htmlmarkdown.cpp htmlutil_p.h
        links_p.h links.cpp
        logseq_p.h logseq.cpp
        importer.h importer.cpp importers_p.h
        importutil_p.h importutil.cpp
        import_tree.cpp
        import_logseq.cpp
        import_notion.cpp
        import_evernote.cpp
        import_keep.cpp
        import_joplin.cpp
        import_simplenote.cpp
        import_roam.cpp
        import_trilium.cpp trilium_p.h trilium.cpp
        import_html.cpp
        import_ics.cpp
        exporter.h exporter.cpp exporters_p.h
        exportutil_p.h exportutil.cpp
        htmlrender_p.h htmlrender.cpp
        clipboardhtml.h clipboardhtml.cpp
        export_markdown.cpp
        export_logseq.cpp
        export_html.cpp
        export_data.cpp
    DEPS util text dates markdown index notebook
    QT Gui)
target_link_libraries(brulion_core_io PRIVATE brulion_miniz)
