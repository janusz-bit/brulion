# brulion_ui_helpers — the GUI-free C++ helpers of the design system (docs/UI.md): natural-language
# dates and recurrence for QML (DatesHelper), fuzzy matching (TextHelper), line diffs and
# conflict hunks (DiffHelper), Qt's own translations (QtTranslations), the on-screen keyboard's
# height (KeyboardInsets), the system's gesture zones at the window's edges (GestureInsets) and the
# appearance settings' colours with their contrast checks and the font pickers' filter
# (ThemePalette, ThemeColors), the rows of the components' JavaScript models and the palette's
# filter (UiModels, PaletteFilter), the JavaScript built-ins that compiled QML cannot use (Js, jshelper.h) and Qt.callLater() without a closure (Later, later.h) —
# their sources are connected by the app, ui/keyboardsources.cpp. A static
# library (QtCore + core modules only) so that tests/app links it without the app; the app's QML
# module "Brulion" registers the classes as QML singletons through QML_FOREIGN wrappers
# (src/app/ui/qmlforeign.h), which needs the metatypes extracted below. DevTools and DeviceInfo
# stay in the app (they need QtQuick/QtGui).
qt_add_library(brulion_ui_helpers STATIC
    dateshelper.h dateshelper.cpp
    diffhelper.h diffhelper.cpp
    texthelper.h texthelper.cpp
    jshelper.h jshelper.cpp
    later.h later.cpp
    mapholder.h
    qttranslations.h qttranslations.cpp
    keyboardinsets.h keyboardinsets.cpp
    gestureinsets.h gestureinsets.cpp
    themepalette.h themepalette.cpp
    uimodels.h uimodels.cpp
)
target_include_directories(brulion_ui_helpers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_helpers PUBLIC Qt6::Core PRIVATE brulion_core_dates brulion_core_text brulion_core_log
    brulion_core_markdown)
qt_extract_metatypes(brulion_ui_helpers)
brulion_set_warnings(brulion_ui_helpers)

# brulion_ui_touch — TouchFlick (touchflick.h): corrects Qt's touch flicks on screens that report
# no velocities (Android). Qt Quick, no private Qt headers; a static library so tests/app/touch
# drives it with Android-style input without the app; registered as the attached type
# "TouchFlick" by qmlforeign.h.
qt_add_library(brulion_ui_touch STATIC
    touchflick.h touchflick.cpp
)
target_include_directories(brulion_ui_touch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_touch PUBLIC Qt6::Core Qt6::Gui Qt6::Quick)
qt_extract_metatypes(brulion_ui_touch)
brulion_set_warnings(brulion_ui_touch)

# brulion_ui_icons — BIcon's pictures (iconprovider.h): Lucide's SVGs rasterised and recoloured as
# IconImage did, through Qt Quick's pixmap cache ("image://bicon/…", one image and texture per
# icon, size and colour), and the singleton "Icons" that names them (registered by qmlforeign.h).
# Qt Quick's public API; a static library so that tests/app/icons compares it with Qt's IconImage
# without the app.
qt_add_library(brulion_ui_icons STATIC
    iconprovider.h iconprovider.cpp
)
target_include_directories(brulion_ui_icons PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_icons PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
qt_extract_metatypes(brulion_ui_icons)
brulion_set_warnings(brulion_ui_icons)

# brulion_ui_smoothscroll — SmoothScroll (smoothscroll.h): the mouse wheel and the scroll bar's
# paging move Flickables like a web browser's smooth scrolling, not Qt's wheel flicks. Qt Quick's
# public API only; a static library so that tests/app/smoothscroll drives it with wheel events
# without the app; main() installs it, qmlforeign.h registers the singleton "SmoothScroll".
qt_add_library(brulion_ui_smoothscroll STATIC
    smoothscroll.h smoothscroll.cpp
)
target_include_directories(brulion_ui_smoothscroll PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_smoothscroll PUBLIC Qt6::Core Qt6::Gui Qt6::Quick)
qt_extract_metatypes(brulion_ui_smoothscroll)
brulion_set_warnings(brulion_ui_smoothscroll)

# brulion_ui_files — FilePicker (filepicker.h): the system's file picker for QML instead of
# QtQuick.Dialogs (docs/PERFORMANCE.md L9) — the platform theme's dialog helper (a QPA header:
# Qt6::GuiPrivate; Android's document picker, Windows' IFileDialog, GTK / KDE / XDG portal), Qt
# Widgets' QFileDialog as the desktop fallback (desktop builds only: Android and the browser stay
# without Qt Widgets; its palette from ThemePalette's colours, brulion_ui_helpers). A static
# library so that tests/app/filepicker drives it with fake backends without the app; registered as
# "FilePicker" by qmlforeign.h.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
qt_add_library(brulion_ui_files STATIC
    filepicker.h filepicker_p.h filepicker.cpp
)
target_include_directories(brulion_ui_files PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_files
    PUBLIC Qt6::Core Qt6::Gui
    PRIVATE Qt6::GuiPrivate Qt6::Quick brulion_core_log brulion_ui_helpers)
if(BRULION_DESKTOP)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    target_link_libraries(brulion_ui_files PRIVATE Qt6::Widgets)
    target_compile_definitions(brulion_ui_files PUBLIC BRULION_FILEPICKER_WIDGETS=1)
endif()
qt_extract_metatypes(brulion_ui_files)
brulion_set_warnings(brulion_ui_files)

# brulion_ui_scale — the interface scale (uiscale.h): "Automatic" by the screen's height, the
# zoom commands' steps, and Qt's global scale factor set before the first window and at run time
# (QHighDpiScaling: Qt6::GuiPrivate). A static library so that tests/app/tst_uiscale checks the
# choices without the app.
qt_add_library(brulion_ui_scale STATIC
    uiscale.h uiscale.cpp
)
target_include_directories(brulion_ui_scale PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_scale PUBLIC Qt6::Core Qt6::Gui PRIVATE Qt6::GuiPrivate)
brulion_set_warnings(brulion_ui_scale)

# brulion_ui_scroll — RowScroll (rowscroll.h): the scroll bar of long lists whose rows differ in
# height, from the rows at the view's edges instead of ListView's estimate of its content height.
# Qt Quick's public API only; a static library so that tests/app/scroll drives it with a ListView
# without the app; registered as "RowScroll" by qmlforeign.h (BRowScrollBar.qml uses it). With it
# placedRow() (viewrows.h): which row of an item view a child of its content item shows — none
# for a delegate kept in ListView's reuse pool (the scroll probe's anchor and delegates).
qt_add_library(brulion_ui_scroll STATIC
    rowscroll.h rowscroll.cpp
    viewrows.h viewrows.cpp
)
target_include_directories(brulion_ui_scroll PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_scroll PUBLIC Qt6::Core Qt6::Quick)
qt_extract_metatypes(brulion_ui_scroll)
brulion_set_warnings(brulion_ui_scroll)

# brulion_ui_memory — what keeps the app's memory bounded by what the screen shows
# (docs/PERFORMANCE.md "Pictures and memory"): NearView (nearview.h), the attached type that tells
# an item whether it is in or near its window's view — a picture holds its pixels only there —;
# PictureDecode (picturedecode.h), the size a picture is decoded at for the place that shows it;
# MemoryKeeper (memorykeeper.h), which hands freed memory back to the system once the app is quiet
# (platform/processmemory.h); MemoryPolicy (memorypolicy.h), which says what is kept for speed and
# lets go of everything hidden once the app is away (docs/PERFORMANCE.md "Memory: only what is in
# use"). Qt Quick's public API only; a static library so that tests/app/memory
# drives them with a window without the app; registered as "NearView", "PictureDecode" and
# "MemoryPolicy" by qmlforeign.h, the keeper and the policy are made by main().
qt_add_library(brulion_ui_memory STATIC
    nearview.h nearview.cpp
    picturedecode.h picturedecode.cpp
    memorykeeper.h memorykeeper.cpp
    memorypolicy.h memorypolicy.cpp
)
target_include_directories(brulion_ui_memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_memory PUBLIC Qt6::Core Qt6::Gui Qt6::Quick PRIVATE brulion_platform)
qt_extract_metatypes(brulion_ui_memory)
brulion_set_warnings(brulion_ui_memory)

# brulion_ui_qmlcall — QmlCall and QmlValue (qmlcall.h): a function of a QML object called, and a
# property read, written or followed, by name — for QML made from its file when first used and
# reached through a base type (docs/UI.md "QML files loaded when first used"). QtCore + QtQml; a
# static library so that tests/app/tst_qmlcall drives it without the app; registered as the
# singleton "QmlCall" and the type "QmlValue" by qmlforeign.h.
qt_add_library(brulion_ui_qmlcall STATIC
    qmlcall.h qmlcall.cpp
)
target_include_directories(brulion_ui_qmlcall PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_qmlcall PUBLIC Qt6::Core PRIVATE Qt6::Qml)
qt_extract_metatypes(brulion_ui_qmlcall)
brulion_set_warnings(brulion_ui_qmlcall)

# brulion_ui_layoutdump — LayoutDump (layoutdump.h): what a window shows (texts with colours and
# fonts, targets, boxes, the theme's tokens) as JSON for the UI benchmark (`--dump-layout`,
# tests/app/uibench). Qt Quick's public API only; a static library so that
# tests/app/uibench/tst_uibench_layoutdump checks it on a scene without the app. A few KB, linked
# on every platform (only `--dump-layout` calls it).
qt_add_library(brulion_ui_layoutdump STATIC
    layoutdump.h layoutdump.cpp
)
target_include_directories(brulion_ui_layoutdump PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_layoutdump PUBLIC Qt6::Core Qt6::Gui Qt6::Quick)
brulion_set_warnings(brulion_ui_layoutdump)

# brulion_ui_shutdown — the app's QML gone before the engine and the application object at quit
# (qmlshutdown.h): once the app quits, Qt's item views hand the delegates they give up to the
# application object, whose destructor crashed deleting one with a view still creating a delegate
# of its own. Qt Quick's public API only; a static library so that tests/app/shutdown quits scenes
# in the middle of their incubation without the app.
qt_add_library(brulion_ui_shutdown STATIC
    qmlshutdown.h qmlshutdown.cpp
)
target_include_directories(brulion_ui_shutdown PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_shutdown PUBLIC Qt6::Core Qt6::Qml Qt6::Quick)
brulion_set_warnings(brulion_ui_shutdown)

# brulion_ui_network — the QML engine's network access manager (localnetwork.h): local URLs only,
# so rich text never fetches a remote picture. A static library so that tests/app/network checks
# it without the app.
qt_add_library(brulion_ui_network STATIC
    localnetwork.h localnetwork.cpp
)
target_include_directories(brulion_ui_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_network PUBLIC Qt6::Core Qt6::Network Qt6::Qml)
brulion_set_warnings(brulion_ui_network)

# brulion_ui_fonts — the bundled text fonts (below) and the fallback fonts for emoji and symbols
# (fallbackfonts.h; assets/fonts,
# subsets of Noto Emoji and Noto Sans Math under the SIL OFL 1.1, made by
# scripts/fallback-fonts.py) as resources ":/fonts/Brulion*.{ttf,otf}" (~170 KB, ~108 KB
# compressed). Android leaves them out: it always has an emoji font, and the app never loads them
# there. A static library so that tests/app/fonts checks them without the app.
qt_add_library(brulion_ui_fonts STATIC
    fallbackfonts.h fallbackfonts.cpp
    uifonts.h uifonts.cpp
)
target_include_directories(brulion_ui_fonts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_fonts PUBLIC Qt6::Gui)
if(NOT ANDROID)
    qt_add_resources(brulion_ui_fonts brulion_fallback_fonts
        PREFIX /fonts
        BASE ${PROJECT_SOURCE_DIR}/assets/fonts
        FILES ${PROJECT_SOURCE_DIR}/assets/fonts/BrulionEmoji.ttf ${PROJECT_SOURCE_DIR}/assets/fonts/BrulionMath.otf)
endif()
# The bundled text fonts (uifonts.h; assets/fonts/ui, subsets of Inter, Literata and JetBrains Mono
# under the SIL OFL 1.1, made by scripts/ui-fonts.py) as resources ":/fonts/ui/*.ttf" (~1.4 MB),
# on every platform, Android and the browser included. Stored as they are (--no-compress):
# installBundledFonts() hands the font engine the program's own data, and no copy of the files
# is held on the heap (docs/PERFORMANCE.md "Memory: only what is in use").
file(GLOB _brulion_ui_font_files CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/fonts/ui/*.ttf)
qt_add_resources(brulion_ui_fonts brulion_ui_text_fonts
    PREFIX /fonts/ui
    BASE ${PROJECT_SOURCE_DIR}/assets/fonts/ui
    FILES ${_brulion_ui_font_files}
    OPTIONS --no-compress)
brulion_set_warnings(brulion_ui_fonts)

# Qt's own Polish texts (standard dialog buttons, the fallback QFileDialog of FilePicker —
# QtTranslations):
# qtbase_pl.qm and qtdeclarative_pl.qm from Qt's translations folder, bundled as resources
# ":/i18n/qt/…" so Android and the browser have them too. The folder: BRULION_QT_TRANSLATIONS_DIR
# (set by nix/package.nix), else "translations/" of a prefix on the CMake search path (nix: the
# qt6.qttranslations input, also in the dev shell), else the Qt being built against, else the
# host Qt of a cross build (Android and WebAssembly: aqt's desktop Qt has them). Without them the
# app uses Qt's installed translations at run time, when there are any.
set(BRULION_QT_TRANSLATIONS_DIR "" CACHE PATH
    "Folder with Qt's translations (qtbase_pl.qm, qtdeclarative_pl.qm) to bundle")
set(_brulion_qt_tr_dirs ${BRULION_QT_TRANSLATIONS_DIR})
foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
    list(APPEND _brulion_qt_tr_dirs "${_prefix}/translations")
endforeach()
foreach(_var CMAKE_PREFIX_PATH NIXPKGS_CMAKE_PREFIX_PATH)
    if(DEFINED ENV{${_var}})
        string(REPLACE ":" ";" _prefixes "$ENV{${_var}}")
        foreach(_prefix IN LISTS _prefixes)
            list(APPEND _brulion_qt_tr_dirs "${_prefix}/translations")
        endforeach()
    endif()
endforeach()
list(APPEND _brulion_qt_tr_dirs "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
if(QT_HOST_PATH)
    list(APPEND _brulion_qt_tr_dirs "${QT_HOST_PATH}/translations")
endif()
set(_brulion_qt_tr_files "")
foreach(_catalog qtbase qtdeclarative)
    foreach(_dir IN LISTS _brulion_qt_tr_dirs)
        if(_dir AND EXISTS "${_dir}/${_catalog}_pl.qm")
            # Copied next to the build files: resources need a single base folder.
            configure_file("${_dir}/${_catalog}_pl.qm" "${CMAKE_CURRENT_BINARY_DIR}/qt-translations/${_catalog}_pl.qm" COPYONLY)
            list(APPEND _brulion_qt_tr_files "${CMAKE_CURRENT_BINARY_DIR}/qt-translations/${_catalog}_pl.qm")
            break()
        endif()
    endforeach()
endforeach()
if(_brulion_qt_tr_files)
    qt_add_resources(brulion_ui_helpers brulion_qt_translations
        PREFIX /i18n/qt
        BASE ${CMAKE_CURRENT_BINARY_DIR}/qt-translations
        FILES ${_brulion_qt_tr_files})
else()
    message(WARNING "Qt's Polish translations (qtbase_pl.qm) not found: Qt's own texts (dialog buttons) "
                    "stay English unless Qt has them installed. Set BRULION_QT_TRANSLATIONS_DIR.")
endif()
