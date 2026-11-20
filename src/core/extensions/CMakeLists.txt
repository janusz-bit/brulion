# extensions — Brulion's extensions in Luau (docs/EXTENSIONS.md): the engine (engine.h: one
# sandboxed VM, limits, questions, views and actions), what it asks of the app (host.h), the
# notebook-backed host (notebookhost.h: the index's blocks, content-preserving writes), the
# built-in extensions (builtin.h, builtin/*.luau as resources: the gym, silownia.luau) and the
# extensions written in the notebook (pageextension.h: pages tagged #rozszerzenie).
# Luau's type checker on save (typecheck.h, docs/EXTENSIONS.md "Types"): on by default except in the
# browser, whose Qt catches no C++ exceptions (the checker's own errors are exceptions) and whose
# download it would make several megabytes bigger. -DBRULION_LUAU_CHECK=OFF leaves it out anywhere.
if(EMSCRIPTEN)
    set(_brulion_luau_check_default OFF)
else()
    set(_brulion_luau_check_default ON)
endif()
option(BRULION_LUAU_CHECK "Luau's type checker for extension pages (3rdparty/luau/Analysis)" ${_brulion_luau_check_default})
if(NOT TARGET brulion_luau)
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/luau ${CMAKE_BINARY_DIR}/3rdparty/luau)
endif()
if(BRULION_LUAU_CHECK)
    set(_brulion_typecheck typecheck.cpp)
else()
    set(_brulion_typecheck typecheck_none.cpp)
endif()

brulion_add_core_module(extensions
    SOURCES
        engine.h host.h builtin.h notebookhost.h blockedit.h pageextension.h sandbox.h events.h share.h selftest.h typecheck.h
        engine.cpp builtin.cpp notebookhost.cpp blockedit.cpp pageextension.cpp sandbox.cpp events.cpp share.cpp selftest.cpp ${_brulion_typecheck}
    DEPS util text dates markdown vault index notebook sync)
target_link_libraries(brulion_core_extensions PRIVATE brulion_luau)
if(BRULION_LUAU_CHECK)
    target_link_libraries(brulion_core_extensions PRIVATE brulion_luau_analysis)
endif()
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
