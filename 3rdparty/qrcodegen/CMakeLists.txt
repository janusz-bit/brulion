# QR Code generator library 1.8.0 (https://github.com/nayuki/QR-Code-generator, MIT, see LICENSE) —
# the QR codes of src/core/qr. Vendored unmodified: cpp/qrcodegen.cpp and cpp/qrcodegen.hpp of the
# v1.8.0 tag, and the licence from their header as LICENSE.
#
# It throws (std::length_error and others) only for data that fits no code or for wrong arguments:
# src/core/qr checks the length first, so nothing is thrown where exceptions are not caught (the
# browser's Qt links without C++ exception catching). Warnings are disabled for this third-party
# code; Brulion's -Werror flags never apply to it.
add_library(brulion_qrcodegen STATIC qrcodegen.cpp qrcodegen.hpp)
set_target_properties(brulion_qrcodegen PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(brulion_qrcodegen SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(brulion_qrcodegen PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w> $<$<CXX_COMPILER_ID:MSVC>:/w>)
