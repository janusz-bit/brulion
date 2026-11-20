# attachments — the text of PDFs and pictures in the whole-notebook search (docs/UI.md "Search"):
# what one file says as the device's cache keeps it (record.h), the notebook's attachments by path
# (manifest.h), their full-text index (attachmentsearch.h, on index/searchindex.h) and the search
# field's `in:` filters (searchfilters.h). Pure and GUI-free; the app reads the files (PDF text
# layers, OCR) on its worker (src/app/attachments). Overview: attachments.h.
brulion_add_core_module(attachments
    SOURCES
        attachments.h
        codec_p.h
        record.h record.cpp
        manifest.h manifest.cpp
        attachmentsearch.h attachmentsearch.cpp
        searchfilters.h searchfilters.cpp
    DEPS util text index)
