# Desktop integration files for Linux/freedesktop installs (`cmake --install`): the desktop entry,
# AppStream metainfo and the icon (scalable SVG + 256 px PNG), all named after the app id
# io.github.januszbit.brulion. Used by the Nix package (nix/package.nix, which validates them
# with desktop-file-validate and appstreamcli) and the AppImage (scripts/appimage-build.sh).
# The binary itself is installed by src/app/CMakeLists.txt.

if(NOT UNIX OR APPLE OR ANDROID OR EMSCRIPTEN)
    return()
endif()

include(GNUInstallDirs)

set(BRULION_APP_ID io.github.januszbit.brulion)

install(FILES ${PROJECT_SOURCE_DIR}/packaging/${BRULION_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${PROJECT_SOURCE_DIR}/packaging/${BRULION_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES ${PROJECT_SOURCE_DIR}/assets/logo.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME ${BRULION_APP_ID}.svg)
# Pre-rendered from assets/logo.svg (re-render after logo changes, see docs/NIXOS.md).
install(FILES ${PROJECT_SOURCE_DIR}/packaging/icons/hicolor/256x256/apps/${BRULION_APP_ID}.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
