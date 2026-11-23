# attachments — the text of PDFs, pictures and documents in the whole-notebook search (docs/UI.md "Search"):
# what one file says as the device's cache keeps it (record.h), the notebook's attachments by path
# (manifest.h), their full-text index (attachmentsearch.h, on index/searchindex.h) and the search
# field's `in:` filters (searchfilters.h). Pure and GUI-free; the app reads the files (PDF text
# layers, OCR) on its worker (src/app/attachments). Overview: attachments.h.
if(NOT TARGET brulion_miniz)
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/miniz ${CMAKE_BINARY_DIR}/3rdparty/miniz)
endif()

brulion_add_core_module(attachments
    SOURCES
        attachments.h
        codec_p.h
        record.h record.cpp
        manifest.h manifest.cpp
        attachmentsearch.h attachmentsearch.cpp
        searchfilters.h searchfilters.cpp
        documenttext.h documenttext.cpp
    DEPS util text index)
# (The documents are ZIP files: miniz reads them through a device, nothing else of the archive.)
target_link_libraries(brulion_core_attachments PRIVATE brulion_miniz)
