brulion_add_test(tst_atrament_ui SOURCES tst_atrament_ui.cpp LIBS brulion_atrament_ui)
target_compile_definitions(tst_atrament_ui PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
