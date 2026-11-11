# Luau 0.743 (https://github.com/luau-lang/luau, MIT, see LICENSE.txt and lua_LICENSE.txt) — the
# language of Brulion's extensions (src/core/extensions, docs/EXTENSIONS.md). Vendored: only the
# VM, the compiler and what it needs (Common, Ast, Bytecode, Compiler, VM — no Analysis, CodeGen,
# CLI, tests). Patches, marked "Brulion:":
#   * VM/src/lstrlib.cpp: the plain string search calls the interrupt callback, so the extensions'
#     time limit also stops a long `string.find(s, p, 1, true)`;
#   * Common/include/Luau/StrtodC.h (new), used by Ast/src/Parser.cpp and VM/src/lnumutils.h:
#     numbers are read with '.' whatever the C locale (Qt sets it from the environment; with a
#     Polish locale Luau's own strtod failed on 82.5 — "Malformed number"); string.format's %f / %g
#     write '.' too (VM/src/lstrlib.cpp).
#
# One static library; warnings are disabled for this third-party code (Brulion's -Werror never
# applies to it). Errors are setjmp/longjmp (LUA_USE_LONGJMP=1) on every platform: the browser's Qt
# links without C++ exception catching, and the engine's glue never keeps C++ objects alive across
# a Luau error (src/core/extensions/engine.cpp).
set(_luau_sources
    Common/src/BytecodeWire.cpp
    Common/src/StringUtils.cpp
    Common/src/TimeTrace.cpp
    Ast/src/Allocator.cpp
    Ast/src/Ast.cpp
    Ast/src/Confusables.cpp
    Ast/src/Cst.cpp
    Ast/src/Lexer.cpp
    Ast/src/Location.cpp
    Ast/src/Parser.cpp
    Ast/src/PrettyPrinter.cpp
    Bytecode/src/BytecodeBuilder.cpp
    Bytecode/src/BytecodeDump.cpp
    Bytecode/src/BytecodeGraph.cpp
    Bytecode/src/Sccp.cpp
    Compiler/src/Compiler.cpp
    Compiler/src/Builtins.cpp
    Compiler/src/BuiltinFolding.cpp
    Compiler/src/ConstantFolding.cpp
    Compiler/src/CostModel.cpp
    Compiler/src/TableShape.cpp
    Compiler/src/Types.cpp
    Compiler/src/ValueTracking.cpp
    Compiler/src/lcode.cpp
    VM/src/lapi.cpp
    VM/src/laux.cpp
    VM/src/lbaselib.cpp
    VM/src/lbitlib.cpp
    VM/src/lbuffer.cpp
    VM/src/lbuflib.cpp
    VM/src/lbuiltins.cpp
    VM/src/lcorolib.cpp
    VM/src/ldblib.cpp
    VM/src/ldebug.cpp
    VM/src/ldo.cpp
    VM/src/lfunc.cpp
    VM/src/lgc.cpp
    VM/src/lgcdebug.cpp
    VM/src/linit.cpp
    VM/src/lmathlib.cpp
    VM/src/lmem.cpp
    VM/src/lnumprint.cpp
    VM/src/lobject.cpp
    VM/src/loslib.cpp
    VM/src/lperf.cpp
    VM/src/lstate.cpp
    VM/src/lstring.cpp
    VM/src/lstrlib.cpp
    VM/src/ltable.cpp
    VM/src/ltablib.cpp
    VM/src/ltm.cpp
    VM/src/ludata.cpp
    VM/src/lutf8lib.cpp
    VM/src/lveclib.cpp
    VM/src/lintlib.cpp
    VM/src/lvmexecute.cpp
    VM/src/lclass.cpp
    VM/src/lclasslib.cpp
    VM/src/lvector.cpp
    VM/src/lvmload.cpp
    VM/src/lvmutils.cpp
)
add_library(brulion_luau STATIC ${_luau_sources})
set_target_properties(brulion_luau PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
target_include_directories(brulion_luau SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/VM/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Compiler/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Common/include)
target_include_directories(brulion_luau PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Ast/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Bytecode/include)
target_compile_definitions(brulion_luau PUBLIC LUA_USE_LONGJMP=1)
target_compile_options(brulion_luau PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w -fno-math-errno> $<$<CXX_COMPILER_ID:MSVC>:/w /D_CRT_SECURE_NO_WARNINGS>)
if(MSVC AND MSVC_VERSION GREATER_EQUAL 1924)
    # Luau's own setting: partial redundancy elimination makes MSVC's interpreter loop much slower.
    set_source_files_properties(VM/src/lvmexecute.cpp PROPERTIES COMPILE_OPTIONS /d2ssa-pre-)
endif()
