brulion_add_test(tst_atrament_scene SOURCES tst_atrament_scene.cpp LIBS brulion_atrament_scene)
# Vector paths, strokes and icons (scene/vectorpath.h, vectoricon.h): coverage through base's
# rasterizer; every icon of assets/icons read.
brulion_add_test(tst_atrament_vector SOURCES tst_atrament_vector.cpp LIBS brulion_atrament_scene)
target_compile_definitions(tst_atrament_vector PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
