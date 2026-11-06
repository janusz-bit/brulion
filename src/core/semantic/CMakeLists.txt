# semantic — search by meaning (prototype; semantic/semantic.h). The module itself (chunks,
# calibration, vector index, fusion) has no dependencies beyond QtCore and builds everywhere.
brulion_add_core_module(semantic
    SOURCES
        semantic.h
        chunker.h chunker.cpp
        calibration.h calibration.cpp
        vectorindex.h vectorindex.cpp
        fusion.h fusion.cpp)

# The model runner needs llama.cpp (nixpkgs#llama-cpp in the dev shell). It is a separate library
# outside brulion_core: the app and brulion-cli do not link it (yet).
find_package(llama CONFIG QUIET)
if(llama_FOUND)
    qt_add_library(brulion_semantic_llama STATIC embedder.h embedder.cpp)
    target_link_libraries(brulion_semantic_llama PUBLIC brulion_core_semantic PRIVATE llama ggml::ggml-base ggml::ggml)
    brulion_set_warnings(brulion_semantic_llama)
    # A GGML_BACKEND_DL build (nixpkgs) loads its CPU variants (AVX2, AVX-512…) at run time from
    # the directory of the llama.cpp programs; ggml_backend_load_all() looks only next to our
    # executable. BRULION_GGML_BACKEND_DIR in the environment overrides.
    get_target_property(_ggml_lib ggml::ggml IMPORTED_LOCATION)
    if(_ggml_lib)
        get_filename_component(_ggml_dir "${_ggml_lib}" DIRECTORY)
        get_filename_component(_ggml_bin "${_ggml_dir}/../bin" ABSOLUTE)
        target_compile_definitions(brulion_semantic_llama PRIVATE BRULION_GGML_BACKEND_DIR="${_ggml_bin}")
    endif()
    message(STATUS "semantic: llama.cpp ${llama_VERSION} found — building brulion_semantic_llama")
else()
    message(STATUS "semantic: llama.cpp not found — brulion_semantic_llama (the model runner) is not built")
endif()
