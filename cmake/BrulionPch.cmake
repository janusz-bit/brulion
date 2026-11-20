# Precompiled headers for Brulion's own targets (docs/PERFORMANCE.md "Build times").
#
# Most of a source file's compile time at -O0, and a third or more at -O3, is spent parsing the
# same Qt and standard headers again. brulion_precompile_headers(), called once at the end of the
# top-level CMakeLists.txt, gives Brulion's targets (those of src/ and tests/; never the vendored
# code of 3rdparty/) a precompiled header of the headers their code includes most:
#
#   core   Qt Core and the standard library — every target, the core modules (GUI-free) included
#   quick  + Qt Gui, Qt Qml and Qt Quick, and what the C++ that qmlcachegen writes includes —
#          targets that link Qt Quick (the app and its libraries)
#
# A library or program compiles its own (its definitions, such as QT_GUI_LIB, shape Qt's headers)
# when it has at least BRULION_PCH_MIN_SOURCES C++ sources. The tests, one or two sources each,
# share one per set of Qt modules (_brulion_pch_test below).
#
# The headers are only included, never relied on: a source still includes what it uses, so a build
# without them (-DBRULION_PCH=OFF; CI's Linux job builds that way) catches a missing include.
#
# On by default where it is measured and checked: Clang on Linux (the dev shell, the Nix package,
# the AppImages). Elsewhere (MSVC, Android, the browser, GCC) -DBRULION_PCH=ON turns it on.
#
# ccache caches a compile with a precompiled header only with `sloppiness = pch_defines,time_macros`
# and Clang's -fno-pch-timestamp: both are set here, the former as an option of the launcher
# (`ccache sloppiness=… clang++ …`, ccache 4.8 and newer) for the targets with a header.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_brulion_pch_default ON)
else()
    set(_brulion_pch_default OFF)
endif()
option(BRULION_PCH "Precompile the Qt and standard headers most sources include (faster builds)"
    ${_brulion_pch_default})
set(BRULION_PCH_MIN_SOURCES 3 CACHE STRING
    "The fewest C++ sources a library or program needs for a precompiled header of its own")

set(_brulion_pch_core_headers
    <algorithm> <array> <atomic> <cmath> <functional> <limits> <memory> <optional> <utility>
    <vector>
    <QByteArray> <QByteArrayView> <QCoreApplication> <QDate> <QDateTime> <QDir> <QElapsedTimer>
    <QFile> <QFileInfo> <QHash> <QJsonArray> <QJsonDocument> <QJsonObject> <QList> <QLocale>
    <QLoggingCategory> <QMap> <QMetaObject> <QObject> <QPointer> <QRegularExpression> <QSet>
    <QString> <QStringList> <QStringView> <QTimer> <QUrl> <QVariant> <QVariantList> <QVariantMap>)
set(_brulion_pch_quick_headers
    ${_brulion_pch_core_headers}
    <QAbstractListModel> <QColor> <QFont> <QGuiApplication> <QImage> <QPainter> <QRectF>
    <QQmlEngine> <QQuickItem> <QQuickWindow>
    # The includes of qmlcachegen's C++ (.rcc/qmlcache/<target>_<file>_qml.cpp).
    <QtQml/qqmlprivate.h> <QtQml/qjsengine.h> <QtQml/qjsprimitivevalue.h> <QtQml/qjsvalue.h>
    <QtQml/qqmlcomponent.h> <QtQml/qqmlcontext.h> <QtQml/qqmllist.h> <QtCore/qtimezone.h>)

# _brulion_pch_links(<out> <target>): every target whose usage requirements (include folders,
# definitions) <target>'s sources get — the ones it links, and what those pass on. A dependency's
# private link ($<LINK_ONLY:…>) passes on none.
function(_brulion_pch_links out target)
    get_target_property(_todo ${target} LINK_LIBRARIES)
    if(NOT _todo)
        set(_todo "")
    endif()
    set(_seen "")
    while(_todo)
        list(POP_FRONT _todo _t)
        if(NOT TARGET ${_t} OR _t IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen ${_t})
        get_target_property(_aliased ${_t} ALIASED_TARGET)
        if(_aliased)
            list(APPEND _todo ${_aliased})
        endif()
        get_target_property(_libs ${_t} INTERFACE_LINK_LIBRARIES)
        foreach(_lib IN LISTS _libs)
            if(TARGET ${_lib})
                list(APPEND _todo ${_lib})
            endif()
        endforeach()
    endwhile()
    set(${out} ${_seen} PARENT_SCOPE)
endfunction()

# _brulion_pch_ccache(<target>): what ccache needs to cache the compiles of a target with a
# precompiled header — no timestamp in a header made by Clang, and the sloppiness above.
function(_brulion_pch_ccache target)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target} PRIVATE "SHELL:-Xclang -fno-pch-timestamp")
    endif()
    get_target_property(_launcher ${target} CXX_COMPILER_LAUNCHER)
    if(NOT _launcher)
        return()
    endif()
    list(GET _launcher 0 _ccache)
    get_filename_component(_name "${_ccache}" NAME_WE)
    if(NOT _name STREQUAL "ccache")
        return()
    endif()
    get_property(_version GLOBAL PROPERTY _BRULION_PCH_CCACHE_VERSION)
    if(NOT _version)
        execute_process(COMMAND "${_ccache}" --version OUTPUT_VARIABLE _out ERROR_QUIET)
        string(REGEX MATCH "version ([0-9]+\\.[0-9]+)" _match "${_out}")
        set(_version "${CMAKE_MATCH_1}")
        if(NOT _version)
            set(_version 0)
        endif()
        set_property(GLOBAL PROPERTY _BRULION_PCH_CCACHE_VERSION ${_version})
        if(_version VERSION_LESS 4.8)
            message(STATUS "Precompiled headers: ccache ${_version} cannot be told to cache their "
                "compiles (4.8 can); they are compiled every time")
        endif()
    endif()
    if(_version VERSION_GREATER_EQUAL 4.8)
        list(INSERT _launcher 1 "sloppiness=pch_defines,time_macros")
        set_property(TARGET ${target} PROPERTY CXX_COMPILER_LAUNCHER "${_launcher}")
    endif()
endfunction()

# _brulion_pch_test(<target> <links>): a test reuses the precompiled header of a stand-in target
# that links the same Qt modules. The tests' own definitions cannot be in it: Qt Test defines the
# test's folders (QT_TESTCASE_BUILDDIR), which differ, and Clang refuses a header made with another
# value — a definition the header was made without it takes as given. Qt Test itself stays out of
# the header for the same reason (its macros read those definitions and QT_GUI_LIB).
function(_brulion_pch_test target links)
    set(_qt ${links})
    list(FILTER _qt INCLUDE REGEX "^Qt6::")
    list(REMOVE_ITEM _qt Qt6::Test Qt6::TestPrivate)
    list(SORT _qt)
    list(REMOVE_DUPLICATES _qt)
    string(MD5 _key "${_qt}")
    string(SUBSTRING "${_key}" 0 8 _key)
    set(_donor brulion_pch_tests_${_key})
    if(NOT TARGET ${_donor})
        set(_main ${PROJECT_BINARY_DIR}/brulion_pch/main.cpp)
        if(NOT EXISTS ${_main})
            file(WRITE ${_main} "// The stand-in of tests' precompiled headers (cmake/BrulionPch.cmake).\nint main() { return 0; }\n")
        endif()
        add_executable(${_donor} EXCLUDE_FROM_ALL ${_main})
        set_target_properties(${_donor} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(${_donor} PRIVATE ${_qt})
        brulion_set_warnings(${_donor})
        if("Qt6::Quick" IN_LIST _qt)
            target_precompile_headers(${_donor} PRIVATE ${_brulion_pch_quick_headers})
        else()
            target_precompile_headers(${_donor} PRIVATE ${_brulion_pch_core_headers})
        endif()
        _brulion_pch_ccache(${_donor})
    endif()
    target_precompile_headers(${target} REUSE_FROM ${_donor})
    _brulion_pch_ccache(${target})
endfunction()

# _brulion_pch_target(<target>): the precompiled header of one target, when it gets one.
function(_brulion_pch_target target)
    get_target_property(_type ${target} TYPE)
    if(NOT _type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
        return()
    endif()
    get_target_property(_imported ${target} IMPORTED)
    if(_imported)
        return()
    endif()
    get_target_property(_sources ${target} SOURCES)
    set(_c ${_sources})
    list(FILTER _c INCLUDE REGEX "\\.c$")
    if(_c)
        return()
    endif()
    _brulion_pch_links(_links ${target})
    # Only Brulion's own code: what brulion_set_warnings() gives (QT_NO_KEYWORDS shapes Qt's headers).
    get_target_property(_definitions ${target} COMPILE_DEFINITIONS)
    if(NOT "QT_NO_KEYWORDS" IN_LIST _definitions)
        return()
    endif()
    if(_type STREQUAL "EXECUTABLE" AND "Qt6::Test" IN_LIST _links)
        _brulion_pch_test(${target} "${_links}")
        return()
    endif()
    set(_cxx ${_sources})
    list(FILTER _cxx INCLUDE REGEX "\\.(cpp|cc|cxx)$")
    list(LENGTH _cxx _count)
    if(_count LESS BRULION_PCH_MIN_SOURCES)
        return()
    endif()
    if("Qt6::Quick" IN_LIST _links)
        target_precompile_headers(${target} PRIVATE ${_brulion_pch_quick_headers})
    else()
        target_precompile_headers(${target} PRIVATE ${_brulion_pch_core_headers})
    endif()
    _brulion_pch_ccache(${target})
endfunction()

# brulion_precompile_headers(): every target whose sources are under src/ or tests/.
function(brulion_precompile_headers)
    if(NOT BRULION_PCH)
        return()
    endif()
    set(_dirs ${PROJECT_SOURCE_DIR})
    set(_all "")
    while(_dirs)
        list(POP_FRONT _dirs _dir)
        get_property(_targets DIRECTORY ${_dir} PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_t IN LISTS _targets)
            get_target_property(_source_dir ${_t} SOURCE_DIR)
            string(FIND "${_source_dir}/" "${PROJECT_SOURCE_DIR}/src/" _in_src)
            string(FIND "${_source_dir}/" "${PROJECT_SOURCE_DIR}/tests/" _in_tests)
            if(_in_src EQUAL 0 OR _in_tests EQUAL 0)
                list(APPEND _all ${_t})
            endif()
        endforeach()
        get_property(_sub DIRECTORY ${_dir} PROPERTY SUBDIRECTORIES)
        list(APPEND _dirs ${_sub})
    endwhile()
    foreach(_t IN LISTS _all)
        _brulion_pch_target(${_t})
    endforeach()
endfunction()
