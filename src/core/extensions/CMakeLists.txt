# extensions — Brulion's extensions in Luau (docs/EXTENSIONS.md): the engine (engine.h: one
# sandboxed VM, limits, questions, views and actions), what it asks of the app (host.h), the
# notebook-backed host (notebookhost.h: the index's blocks, content-preserving writes), the
# built-in extensions (builtin.h, builtin/*.luau as resources: the gym, silownia.luau) and the
# extensions written in the notebook (pageextension.h: pages tagged #rozszerzenie).
if(NOT TARGET brulion_luau)
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/luau ${CMAKE_BINARY_DIR}/3rdparty/luau)
endif()

brulion_add_core_module(extensions
    SOURCES
        engine.h host.h builtin.h notebookhost.h blockedit.h pageextension.h sandbox.h
        engine.cpp builtin.cpp notebookhost.cpp blockedit.cpp pageextension.cpp sandbox.cpp
    DEPS util text dates markdown vault index notebook)
target_link_libraries(brulion_core_extensions PRIVATE brulion_luau)
file(GLOB _brulion_builtin_extensions CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/builtin/*)
qt_add_resources(brulion_core_extensions brulion_builtin_extensions
    PREFIX /extensions
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/builtin
    FILES ${_brulion_builtin_extensions})
# The reference for writing extensions, for those who write them without the app's documentation
# at hand: the AI assistant (b.data("reference")) and `brulion-cli extension docs`.
qt_add_resources(brulion_core_extensions brulion_extensions_reference
    PREFIX /extensions
    BASE ${PROJECT_SOURCE_DIR}/docs
    FILES ${PROJECT_SOURCE_DIR}/docs/EXTENSIONS.md)
set_property(TARGET brulion_core_extensions APPEND PROPERTY QT_EXCLUDE_SOURCES_FROM_TRANSLATION
    ${_brulion_builtin_extensions} ${PROJECT_SOURCE_DIR}/docs/EXTENSIONS.md)
