# extensions — Brulion's extensions in Luau (docs/EXTENSIONS.md): the engine (engine.h: one
# sandboxed VM, limits, questions, views and actions), what it asks of the app (host.h), the
# notebook-backed host (notebookhost.h: the index's blocks, content-preserving writes) and the
# built-in extensions (builtin.h, builtin/*.luau as resources: the gym, silownia.luau).
if(NOT TARGET brulion_luau)
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/luau ${CMAKE_BINARY_DIR}/3rdparty/luau)
endif()

brulion_add_core_module(extensions
    SOURCES
        engine.h host.h builtin.h notebookhost.h blockedit.h
        engine.cpp builtin.cpp notebookhost.cpp blockedit.cpp
    DEPS util text dates markdown vault index notebook)
target_link_libraries(brulion_core_extensions PRIVATE brulion_luau)
file(GLOB _brulion_builtin_extensions CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/builtin/*)
qt_add_resources(brulion_core_extensions brulion_builtin_extensions
    PREFIX /extensions
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/builtin
    FILES ${_brulion_builtin_extensions})
set_property(TARGET brulion_core_extensions APPEND PROPERTY QT_EXCLUDE_SOURCES_FROM_TRANSLATION ${_brulion_builtin_extensions})
