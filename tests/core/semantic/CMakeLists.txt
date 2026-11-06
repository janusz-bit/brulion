brulion_add_test(tst_semantic SOURCES tst_semantic.cpp LIBS brulion_core_semantic)

# The model runner against a real GGUF model: skipped unless BRULION_SEMANTIC_MODEL points to one.
if(TARGET brulion_semantic_llama)
    brulion_add_test(tst_semantic_embedder SOURCES tst_semantic_embedder.cpp LIBS brulion_semantic_llama)
endif()
