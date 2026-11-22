# The semantic module (src/core/semantic): chunks, calibration, the vector index — incremental, kept
# on the device —, the keys and the diff that keep it up to date, fusion and the hybrid search, the
# model the app downloads — no model runs here.
brulion_add_test(tst_semantic SOURCES tst_semantic.cpp LIBS brulion_core_semantic)

# With the model runner in the build (BRULION_LOCAL_AI): the Embedder over the pinned llama.cpp
# against a real GGUF model (skipped unless BRULION_SEMANTIC_MODEL names one), and the plugin
# brulion_semantic loaded as the app loads it (found, loaded with llama.cpp from its folder, a missing
# or broken model an error; with BRULION_SEMANTIC_MODEL: passages and queries, a query while passages
# are embedded).
if(TARGET brulion_semantic_llama)
    brulion_add_test(tst_semantic_embedder SOURCES tst_semantic_embedder.cpp LIBS brulion_semantic_llama)
    brulion_add_test(tst_semantic_plugin SOURCES tst_semantic_plugin.cpp LIBS brulion_core_semantic)
    add_dependencies(tst_semantic_plugin brulion_semantic)
    target_compile_definitions(tst_semantic_plugin PRIVATE BRULION_LOCAL_AI_OUTPUT_DIR="${BRULION_LOCAL_AI_OUTPUT_DIR}")
endif()
