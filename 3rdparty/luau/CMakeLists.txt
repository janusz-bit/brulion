# Luau 0.743 (https://github.com/luau-lang/luau, MIT, see LICENSE.txt and lua_LICENSE.txt) — the
# language of Brulion's extensions (src/core/extensions, docs/EXTENSIONS.md). Vendored: the VM, the
# compiler and what it needs (Common, Ast, Bytecode, Compiler, VM), and the type checker (Analysis,
# Config: brulion_luau_analysis, only with BRULION_LUAU_CHECK — src/core/extensions/typecheck.h);
# no CodeGen, Require, CLI, tests. Analysis and Config are upstream's files unchanged. Patches,
# marked "Brulion:":
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

# Luau's type checker (Analysis, Config), for the extensions' type check on save: desktop and
# Android (BRULION_LUAU_CHECK, src/core/extensions/CMakeLists.txt). It reports its own errors with
# C++ exceptions, caught at its edge (typecheck.cpp); the VM it runs nothing on stays longjmp's.
if(BRULION_LUAU_CHECK)
    add_library(brulion_luau_analysis STATIC
    Analysis/src/Anyification.cpp
    Analysis/src/ApplyTypeFunction.cpp
    Analysis/src/AstJsonEncoder.cpp
    Analysis/src/AstQuery.cpp
    Analysis/src/AstUtils.cpp
    Analysis/src/Autocomplete.cpp
    Analysis/src/AutocompleteCore.cpp
    Analysis/src/BuiltinDefinitions.cpp
    Analysis/src/BuiltinTypeFunctions.cpp
    Analysis/src/Clone.cpp
    Analysis/src/Constraint.cpp
    Analysis/src/ConstraintGenerator.cpp
    Analysis/src/ConstraintGraph.cpp
    Analysis/src/ConstraintSolver.cpp
    Analysis/src/ControlFlowGraph.cpp
    Analysis/src/DataFlowGraph.cpp
    Analysis/src/DcrLogger.cpp
    Analysis/src/Def.cpp
    Analysis/src/DumpCFG.cpp
    Analysis/src/EmbeddedBuiltinDefinitions.cpp
    Analysis/src/Error.cpp
    Analysis/src/ExpectedTypeVisitor.cpp
    Analysis/src/FileResolver.cpp
    Analysis/src/FragmentAutocomplete.cpp
    Analysis/src/Frontend.cpp
    Analysis/src/Generalization.cpp
    Analysis/src/GlobalTypes.cpp
    Analysis/src/Instantiation.cpp
    Analysis/src/Instantiation2.cpp
    Analysis/src/IostreamHelpers.cpp
    Analysis/src/IterativeTypeFunctionTypeVisitor.cpp
    Analysis/src/IterativeTypeVisitor.cpp
    Analysis/src/JsonEmitter.cpp
    Analysis/src/LValue.cpp
    Analysis/src/Linter.cpp
    Analysis/src/Module.cpp
    Analysis/src/NativeStackGuard.cpp
    Analysis/src/NonStrictTypeChecker.cpp
    Analysis/src/Normalize.cpp
    Analysis/src/OverloadResolver.cpp
    Analysis/src/Quantify.cpp
    Analysis/src/RecursionCounter.cpp
    Analysis/src/Refinement.cpp
    Analysis/src/RequireTracer.cpp
    Analysis/src/Scope.cpp
    Analysis/src/Simplify.cpp
    Analysis/src/StructuralTypeEquality.cpp
    Analysis/src/Substitution.cpp
    Analysis/src/Subtyping.cpp
    Analysis/src/Symbol.cpp
    Analysis/src/TableLiteralInference.cpp
    Analysis/src/ToDot.cpp
    Analysis/src/ToString.cpp
    Analysis/src/TopoSortStatements.cpp
    Analysis/src/TxnLog.cpp
    Analysis/src/Type.cpp
    Analysis/src/TypeArena.cpp
    Analysis/src/TypeAttach.cpp
    Analysis/src/TypeChecker2.cpp
    Analysis/src/TypeFunction.cpp
    Analysis/src/TypeFunctionError.cpp
    Analysis/src/TypeFunctionReductionGuesser.cpp
    Analysis/src/TypeFunctionRuntime.cpp
    Analysis/src/TypeFunctionRuntimeBuilder.cpp
    Analysis/src/TypeIds.cpp
    Analysis/src/TypeInfer.cpp
    Analysis/src/TypeOrPack.cpp
    Analysis/src/TypePack.cpp
    Analysis/src/TypePath.cpp
    Analysis/src/TypeStateMap.cpp
    Analysis/src/TypeUtils.cpp
    Analysis/src/TypedAllocator.cpp
    Analysis/src/Unifiable.cpp
    Analysis/src/Unifier.cpp
    Analysis/src/Unifier2.cpp
    Analysis/src/UserDefinedTypeFunction.cpp
        Config/src/Config.cpp
        Config/src/LinterConfig.cpp
        Config/src/LuauConfig.cpp)
    set_target_properties(brulion_luau_analysis PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
    target_include_directories(brulion_luau_analysis SYSTEM PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Analysis/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Config/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Ast/include)
    target_include_directories(brulion_luau_analysis PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Bytecode/include)
    target_link_libraries(brulion_luau_analysis PUBLIC brulion_luau)
    target_compile_options(brulion_luau_analysis PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w -fno-math-errno> $<$<CXX_COMPILER_ID:MSVC>:/w /EHsc /D_CRT_SECURE_NO_WARNINGS /bigobj>)
endif()
