# brulion_platform — OS integration (notifications, storage access, WASM persistence).
qt_add_library(brulion_platform STATIC
    platform.h platform.cpp
)
target_include_directories(brulion_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_platform PUBLIC brulion_core Qt6::Gui)
brulion_set_warnings(brulion_platform)
