# The GPU renderer of the Qt host against the CPU renderer (offscreen QRhi: Vulkan from the loader,
# OpenGL where the platform plugin has it; skipped where neither can be created), AtramentItem's
# render node inside Qt Quick (a QQuickRenderControl on that QRhi), and the host's CPU fallback.
# Fonts come from the source tree (assets/fonts).
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
foreach(_test tst_atrament_qt tst_atrament_qt_item)
    brulion_add_test(${_test} SOURCES ${_test}.cpp qtfixtures.h
        LIBS brulion_atrament_qt brulion_atrament_raster Qt6::GuiPrivate Qt6::Quick)
    target_compile_definitions(${_test} PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
endforeach()
# The Qt host's accessibility bridge, through Qt's own QAccessible API (an offscreen window).
brulion_add_test(tst_atrament_qt_accessibility SOURCES tst_atrament_qt_accessibility.cpp
    LIBS brulion_atrament_qt Qt6::Quick)
target_compile_definitions(tst_atrament_qt_accessibility PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# The Qt host's input-method bridge: Qt's queries and QInputMethodEvents (the sequences of Qt's
# Android input context for Gboard, a desktop preedit) against Atrament's text inputs.
brulion_add_test(tst_atrament_qt_input SOURCES tst_atrament_qt_input.cpp
    LIBS brulion_atrament_qt Qt6::Quick)
target_compile_definitions(tst_atrament_qt_input PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
