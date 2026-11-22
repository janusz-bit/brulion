# brulion-searchbench — quality and speed of search by meaning (searchbench.cpp, docs/SEMANTIC.md).
# Built only when the model runner (brulion_semantic_llama, src/core/semantic: BRULION_LOCAL_AI) is.
qt_add_executable(brulion-searchbench searchbench.cpp)
target_link_libraries(brulion-searchbench PRIVATE brulion_semantic_llama brulion_core_index)
brulion_set_warnings(brulion-searchbench)
