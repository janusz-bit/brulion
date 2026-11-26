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
