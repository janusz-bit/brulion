# The Qt Quick host (docs/ATRAMENT.md D12): the only part of Atrament that knows Qt. Built with the
# app (Qt Quick is there).
if(NOT BRULION_BUILD_APP OR NOT TARGET Qt6::Quick)
    return()
endif()
# QRhi (the GPU renderer) is Qt's semi-public API: its headers come with Qt6::GuiPrivate. Its
# shaders are compiled by Qt Shader Tools' qsb (Vulkan GLSL → SPIR-V, GLSL, HLSL, MSL); a Qt without
# Shader Tools builds the renderer without them, and the host then draws with the CPU renderer.
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
find_package(Qt6 QUIET COMPONENTS ShaderTools)
qt_add_library(brulion_atrament_qt STATIC
    qthost.cpp qthost.h
    gpuframe.cpp gpuframe.h
    qrhirenderer.cpp qrhirenderer.h
    offscreenrhi.cpp offscreenrhi.h)
target_include_directories(brulion_atrament_qt PUBLIC ${PROJECT_SOURCE_DIR}/src/atrament/qt)
target_link_libraries(brulion_atrament_qt PUBLIC brulion_atrament_ui Qt6::Quick Qt6::Gui PRIVATE Qt6::GuiPrivate)
if(COMMAND qt_add_shaders)
    # Instanced attributes, flat varyings and texelFetch: GLSL ES 3.0 / GLSL 3.30 at least (OpenGL
    # ES 2 has none of them; the host draws with the CPU renderer there).
    qt_add_shaders(brulion_atrament_qt "atrament_shaders"
        PREFIX "/atrament"
        GLSL "300es,330"
        HLSL 50
        MSL 12
        FILES
            shaders/quad.vert shaders/quad.frag
            shaders/shadow.vert shaders/shadow.frag
            shaders/sprite.vert shaders/sprite.frag
            shaders/image.vert shaders/image.frag
            shaders/underline.vert shaders/underline.frag)
else()
    message(STATUS "Atrament: no Qt Shader Tools, so no shaders: the Qt host draws with the CPU renderer")
endif()
brulion_set_warnings(brulion_atrament_qt)
