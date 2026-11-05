# Every binding and function of the app's QML is compiled to C++ (docs/UI.md "Compiled QML"):
# reads the ahead-of-time compilation statistics qmlcachegen writes next to each compiled QML
# file (<file>.cpp.aotstats, listed in .rcc/qmlcache/module_brulion.aotstatslist) and lists every
# binding or function left to the QML interpreter, as file:line:column: name: qmlcachegen's reason.
#
#   cmake -DLIST=<build>/src/app/.rcc/qmlcache/module_brulion.aotstatslist
#         [-DSOURCE_DIR=<repository>]   # paths relative to it in the report
#         [-DFILTER=<regex>]            # only files whose path matches (e.g. "/qml/editor/")
#         [-DREPORT_ONLY=ON]            # list and count, but do not fail
#         -P cmake/BrulionQmlCompiledCheck.cmake
#
# Fails (exit code 1) when anything is not compiled, or when the statistics are missing (the app
# not built, or QT_QML_GENERATE_AOTSTATS=OFF). A binding qmlcachegen skips because there is
# nothing to compile (a QQmlScriptString property such as Connections' `target` script) counts as
# compiled. The statistics are JSON in qmlcachegen's fixed layout (formatRevision 1, one key per
# line, keys sorted), read line by line: CMake's string(JSON) would parse the whole document again
# for every value.
cmake_minimum_required(VERSION 3.24)

if(NOT LIST)
    message(FATAL_ERROR "BrulionQmlCompiledCheck: pass -DLIST=<module_brulion.aotstatslist>")
endif()
if(NOT EXISTS "${LIST}")
    message(FATAL_ERROR "BrulionQmlCompiledCheck: ${LIST} does not exist (build the app with "
        "QT_QML_GENERATE_AOTSTATS on, the default)")
endif()

file(STRINGS "${LIST}" _stats_files)
set(_total 0)
set(_compiled 0)
set(_failures "")
set(_files 0)
foreach(_stats IN LISTS _stats_files)
    if(_stats STREQUAL "")
        continue()
    endif()
    if(NOT EXISTS "${_stats}")
        message(FATAL_ERROR "BrulionQmlCompiledCheck: ${_stats} is missing (build the app first)")
    endif()
    # As a list of lines: CMake would join the lines between "[" and "]" into one list element,
    # so brackets are swapped for placeholders (back in the report) and ";" for ",".
    file(READ "${_stats}" _content)
    string(REPLACE ";" "," _content "${_content}")
    string(REPLACE "[" "<lsb>" _content "${_content}")
    string(REPLACE "]" "<rsb>" _content "${_content}")
    string(REPLACE "\n" ";" _lines "${_content}")
    # A file's entries come before its "filePath" (keys are sorted): collected until then.
    set(_entries_total 0)
    set(_entries_compiled 0)
    set(_entries_failed "")
    set(_result "")
    set(_column "")
    set(_name "")
    set(_line "")
    foreach(_l IN LISTS _lines)
        if(_l MATCHES "^ *\"codegenResult\": ([0-9]+),?$")
            set(_result "${CMAKE_MATCH_1}")
        elseif(_l MATCHES "^ *\"column\": ([0-9]+),?$")
            set(_column "${CMAKE_MATCH_1}")
        elseif(_l MATCHES "^ *\"functionName\": \"(.*)\",?$")
            set(_name "${CMAKE_MATCH_1}")
        elseif(_l MATCHES "^ *\"line\": ([0-9]+),?$")
            set(_line "${CMAKE_MATCH_1}")
        elseif(_l MATCHES "^ *\"message\": \"(.*)\",?$")
            # "message" is the last key of an entry.
            set(_message "${CMAKE_MATCH_1}")
            math(EXPR _entries_total "${_entries_total} + 1")
            # 0 = compiled, 1 = skipped, 2 = failed.
            if(_result STREQUAL "0" OR (_result STREQUAL "1" AND _message MATCHES "nothing to do"))
                math(EXPR _entries_compiled "${_entries_compiled} + 1")
            else()
                if(_message STREQUAL "")
                    set(_message "skipped without a reason (an earlier error in this file?)")
                endif()
                string(REPLACE "\\\"" "\"" _message "${_message}")
                string(REPLACE "<lsb>" "[" _message "${_message}")
                string(REPLACE "<rsb>" "]" _message "${_message}")
                list(APPEND _entries_failed "${_line}:${_column}: ${_name}: ${_message}")
            endif()
            set(_result "")
        elseif(_l MATCHES "^ *\"filePath\": \"(.*)\",?$")
            set(_file "${CMAKE_MATCH_1}")
            if(SOURCE_DIR)
                string(REPLACE "${SOURCE_DIR}/" "" _file "${_file}")
            endif()
            if(NOT FILTER OR _file MATCHES "${FILTER}")
                math(EXPR _files "${_files} + 1")
                math(EXPR _total "${_total} + ${_entries_total}")
                math(EXPR _compiled "${_compiled} + ${_entries_compiled}")
                foreach(_e IN LISTS _entries_failed)
                    list(APPEND _failures "${_file}:${_e}")
                endforeach()
            endif()
            set(_entries_total 0)
            set(_entries_compiled 0)
            set(_entries_failed "")
        endif()
    endforeach()
endforeach()

if(_total EQUAL 0)
    message(FATAL_ERROR "BrulionQmlCompiledCheck: no compiled bindings or functions found (FILTER '${FILTER}'?)")
endif()
list(LENGTH _failures _failed)
math(EXPR _percent "${_compiled} * 1000 / ${_total}")
string(REGEX REPLACE "([0-9])$" ".\\1" _percent "${_percent}")
if(_percent MATCHES "^\\.")
    set(_percent "0${_percent}")
endif()
foreach(_f IN LISTS _failures)
    message("${_f}")
endforeach()
message("QML compiled to C++: ${_compiled} of ${_total} bindings and functions (${_percent} %) in ${_files} files; "
    "${_failed} left to the interpreter.")
if(_failed GREATER 0 AND NOT REPORT_ONLY)
    message(FATAL_ERROR "Not every binding and function of the app's QML is compiled to C++ "
        "(docs/UI.md \"Compiled QML\").")
endif()
