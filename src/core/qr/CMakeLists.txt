# qr — QR codes (docs/UI.md "QR code"): a note or blocks shown as codes another phone's camera
# reads. The text a note's codes carry (Markdown as written or plain text, the title first; the
# markdown module) cut into several codes when one cannot hold it (qrtext.h), and one code of
# that text's UTF-8 (qrcode.h: Nayuki's QR Code generator, vendored in 3rdparty/qrcodegen, MIT).
if(NOT TARGET brulion_qrcodegen)
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/qrcodegen ${CMAKE_BINARY_DIR}/3rdparty/qrcodegen)
endif()
brulion_add_core_module(qr
    SOURCES
        qrcode.h qrcode.cpp
        qrtext.h qrtext.cpp
    DEPS markdown)
target_link_libraries(brulion_core_qr PRIVATE brulion_qrcodegen)
