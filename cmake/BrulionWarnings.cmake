# brulion_set_warnings(<target>) — strict warnings for Brulion's own code.
function(brulion_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
            -Wcast-align -Woverloaded-virtual
            -Wimplicit-fallthrough)
    endif()
    if(BRULION_WERROR)
        target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Werror>)
    endif()
    # Safer Qt defaults: no implicit QString <-> const char* conversions.
    target_compile_definitions(${target} PRIVATE
        QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_URL_CAST_FROM_STRING
        QT_NO_CAST_FROM_BYTEARRAY QT_USE_QSTRINGBUILDER QT_STRICT_ITERATORS
        QT_NO_KEYWORDS)
endfunction()
