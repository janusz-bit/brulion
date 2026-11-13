# Tests of the QR codes area (library brulion_qrview, src/app/qr): QrShare's text and codes, and
# QrCodeView's code painted in whole pixels and read back (tests/core/qr/qrdecode.h).
brulion_add_test(tst_qrview SOURCES tst_qrview.cpp ../../core/qr/qrdecode.h LIBS brulion_qrview brulion_core_qr Qt6::Quick)
