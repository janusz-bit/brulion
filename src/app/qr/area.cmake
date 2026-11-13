# QR codes area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths
# are relative to src/app): a note — or the selected blocks — as QR codes another phone's camera
# reads as text (docs/UI.md "QR code"), the static library brulion_qrview and its QML.
#
#   QrShare      the dialog's text: qr::noteText() of the Markdown (as written or plain text, the
#                title first or not) cut into codes (qr::splitText()), the code shown
#   QrCodeView   one code drawn: black on white with its quiet zone, whole device pixels a module
# The codes and their text are the core module qr's (src/core/qr; Nayuki's QR Code generator in
# 3rdparty/qrcodegen). Tests: tests/app/qr. The dialog (qml/qr/QrCodeDialog.qml) is opened by
# PageView and JournalsView (the page menu, the top bar's More, the palette's "page.qrCode") and by
# the block editor (a block's menu, the selection's More).

set(_brulion_qr_dir ${CMAKE_CURRENT_LIST_DIR})

# EditorHost (editor/editorhost.h, QrShare.host) is moc-ed once, by brulion_editorhost: the editor
# area, included before this one, defines it.
qt_add_library(brulion_qrview STATIC
    ${_brulion_qr_dir}/qrshare.h ${_brulion_qr_dir}/qrshare.cpp
    ${_brulion_qr_dir}/qrcodeview.h ${_brulion_qr_dir}/qrcodeview.cpp
)
target_include_directories(brulion_qrview PUBLIC ${_brulion_qr_dir})
target_link_libraries(brulion_qrview
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick brulion_editorhost brulion_core_qr)
qt_extract_metatypes(brulion_qrview)
brulion_set_warnings(brulion_qrview)

# --- The app: QML registration and QML files ---------------------------------------------------
target_sources(brulion PRIVATE qr/qrqml.h)
# The generated QML type registration includes the area's headers by file name.
target_include_directories(brulion PRIVATE ${_brulion_qr_dir})
target_link_libraries(brulion PRIVATE brulion_qrview)

qt_target_qml_sources(brulion QML_FILES
    qml/qr/QrCodeDialog.qml
)
