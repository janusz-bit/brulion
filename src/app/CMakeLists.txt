qt_add_executable(brulion WIN32 MACOSX_BUNDLE
    main.cpp
)

qt_add_qml_module(brulion
    URI Brulion
    VERSION 1.0
    QML_FILES
        qml/Main.qml
    RESOURCES
        ../../assets/logo.svg
)

set_target_properties(brulion PROPERTIES
    OUTPUT_NAME brulion
    QT_ANDROID_PACKAGE_NAME io.github.januszbit.brulion
    QT_ANDROID_APP_NAME Brulion
    QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
    QT_ANDROID_MIN_SDK_VERSION 26
    QT_ANDROID_TARGET_SDK_VERSION 35
)

target_compile_definitions(brulion PRIVATE BRULION_VERSION="${PROJECT_VERSION}")
target_link_libraries(brulion PRIVATE brulion_core brulion_platform Qt6::Quick Qt6::QuickControls2 Qt6::Svg)
brulion_set_warnings(brulion)

qt_add_translations(brulion
    TS_FILE_DIR ${PROJECT_SOURCE_DIR}/i18n
    TS_FILE_BASE brulion
    RESOURCE_PREFIX /i18n)

include(GNUInstallDirs)
install(TARGETS brulion
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
