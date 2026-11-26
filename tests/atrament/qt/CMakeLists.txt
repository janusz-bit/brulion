# The Qt host's accessibility bridge, through Qt's own QAccessible API (an offscreen window).
brulion_add_test(tst_atrament_qt_accessibility SOURCES tst_atrament_qt_accessibility.cpp
    LIBS brulion_atrament_qt Qt6::Quick)
target_compile_definitions(tst_atrament_qt_accessibility PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
