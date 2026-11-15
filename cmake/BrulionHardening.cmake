# Exploit mitigations for every binary Brulion builds — its own code and the vendored libraries
# (3rdparty/, Hunspell). Notes, imports and synced files come from anywhere, so a memory error in
# one of the parsers has to end in a clean stop of the program, never in something worse.
#
# These are the options Qt builds its own libraries with (Qt 6.11's mkspecs/qconfig.pri:
# stack_protector, stack_clash_protection, glibc_fortify_source, libstdcpp_assertions,
# trivial_auto_var_init_pattern, relro_now_linker, intelcet) and the ones the OpenSSF "Compiler
# Options Hardening Guide for C and C++" recommends for production code. nixpkgs, Ubuntu's GCC
# and the Android NDK turn most of them on by themselves; setting them here makes every build
# the same — an AppImage built on another distribution, the Windows installer — and adds what
# those defaults leave out. Each option is tried first, so a compiler or linker that does not
# know one simply builds without it.
#
#   -fstack-protector-strong          a canary in every function with an array or a local whose
#                                     address is taken: a stack overflow stops the program
#   -fstack-clash-protection          a growing stack touches every page (no jump over the guard)
#   -fcf-protection=full (x86)        Intel CET: indirect branch tracking and the shadow stack
#   -mbranch-protection=standard (ARM64)  return addresses signed (PAC), branch targets marked (BTI)
#   -ftrivial-auto-var-init=zero      a local without an initialiser starts as zero, not as
#                                     whatever the stack held (GCC's own -fhardened choice; Qt
#                                     fills with a pattern, which finds bugs but hands a parser
#                                     huge lengths instead of none)
#   -D_FORTIFY_SOURCE=3               memcpy, strcpy, snprintf… checked against the object's size
#                                     (glibc; optimised builds only — it needs the optimiser)
#   -D_GLIBCXX_ASSERTIONS             libstdc++: operator[], front(), back(), *optional and the
#                                     iterators' preconditions checked (C++ Core Guidelines: Bounds
#                                     profile, SL.con.3)
#   -D_LIBCPP_HARDENING_MODE=…_FAST   the same for libc++ (Android)
#   -Wl,-z,relro -Wl,-z,now           the table of imported functions is read-only once loaded
#   -Wl,-z,noexecstack                no executable stack
#   position-independent executables  the program itself at a random address (ASLR)
#   MSVC: /guard:cf, /CETCOMPAT, /DYNAMICBASE, /NXCOMPAT, /HIGHENTROPYVA
#                                     Control Flow Guard, the shadow stack, ASLR and DEP
#
# The browser build is left as it is: WebAssembly keeps return addresses and code out of the
# reach of the program's memory, and the download is over its budget already. What the options
# cost: docs/PERFORMANCE.md "Hardened builds".
#
#   -DBRULION_HARDENING=OFF builds without them (a packager whose toolchain brings its own set,
#   or a measurement against a plain build).
option(BRULION_HARDENING "Build with exploit mitigations (stack protector, checked containers, RELRO, CFI)" ON)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(CheckLinkerFlag)
include(CheckPIESupported)

# _brulion_try_compile_option(<flag>): the flag for every target when the compiler takes it.
function(_brulion_try_compile_option flag)
    string(MAKE_C_IDENTIFIER "BRULION_HAVE${flag}" _var)
    check_cxx_compiler_flag("${flag}" ${_var})
    if(${_var})
        add_compile_options("${flag}")
    endif()
endfunction()

# _brulion_try_link_option(<flag>): the flag for every executable and shared library when the
# linker takes it.
function(_brulion_try_link_option flag)
    string(MAKE_C_IDENTIFIER "BRULION_HAVE_LINK${flag}" _var)
    check_linker_flag(CXX "${flag}" ${_var})
    if(${_var})
        add_link_options("${flag}")
    endif()
endfunction()

function(brulion_enable_hardening)
    if(NOT BRULION_HARDENING OR EMSCRIPTEN)
        return()
    endif()

    if(MSVC)
        # Optimised builds only: Control Flow Guard is not for "Edit and Continue" debug builds.
        set(_optimised "$<CONFIG:Release,RelWithDebInfo,MinSizeRel>")
        add_compile_options("$<${_optimised}:/guard:cf>")
        add_link_options("$<${_optimised}:/guard:cf>" /DYNAMICBASE /NXCOMPAT)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
            add_link_options(/HIGHENTROPYVA "$<${_optimised}:/CETCOMPAT>")
        endif()
        return()
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        return()
    endif()

    _brulion_try_compile_option(-fstack-protector-strong)
    _brulion_try_compile_option(-fstack-clash-protection)
    _brulion_try_compile_option(-ftrivial-auto-var-init=zero)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
        _brulion_try_compile_option(-fcf-protection=full)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)")
        _brulion_try_compile_option(-mbranch-protection=standard)
    endif()

    # Checked element access in the standard library's containers, views and optionals.
    check_cxx_source_compiles("
        #include <version>
        #if !defined(__GLIBCXX__)
        #error not libstdc++
        #endif
        int main() { return 0; }" BRULION_HAVE_LIBSTDCXX)
    if(BRULION_HAVE_LIBSTDCXX)
        add_compile_definitions(_GLIBCXX_ASSERTIONS)
    else()
        set(CMAKE_REQUIRED_DEFINITIONS -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST)
        check_cxx_source_compiles("
            #include <vector>
            #if !defined(_LIBCPP_VERSION) || _LIBCPP_VERSION < 180000
            #error no libc++ with hardening modes
            #endif
            int main() { std::vector<int> v{1}; return v[0] - 1; }" BRULION_HAVE_LIBCXX_HARDENING)
        unset(CMAKE_REQUIRED_DEFINITIONS)
        if(BRULION_HAVE_LIBCXX_HARDENING)
            add_compile_definitions(_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST)
        endif()
    endif()

    # glibc's checked string and memory functions. They need the optimiser (a plain -O0 build
    # only gets a warning), and under the sanitizers they would hide calls from AddressSanitizer;
    # the NDK sets its own level for Android.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT "${CMAKE_CXX_FLAGS}" MATCHES "-fsanitize=")
        set(_optimised "$<CONFIG:Release,RelWithDebInfo,MinSizeRel>")
        add_compile_options("$<${_optimised}:-U_FORTIFY_SOURCE>" "$<${_optimised}:-D_FORTIFY_SOURCE=3>")
    endif()

    _brulion_try_link_option(-Wl,-z,relro)
    _brulion_try_link_option(-Wl,-z,now)
    _brulion_try_link_option(-Wl,-z,noexecstack)

    # Executables at a random address. Qt's own flags keep the objects position independent
    # (-fPIC, which a Qt built with -reduce-relocations asks for); this adds the linker's -pie
    # where the compiler does not default to it.
    check_pie_supported(OUTPUT_VARIABLE _pie_output LANGUAGES CXX)
    if(CMAKE_CXX_LINK_PIE_SUPPORTED)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON PARENT_SCOPE)
    endif()
endfunction()
