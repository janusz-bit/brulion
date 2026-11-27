# brulion_editor_atrament — the block editor drawn by Atrament (docs/ATRAMENT.md "The block editor
# in Atrament (M2)"): PageView, an Atrament view over BlockListModel, the rendered text's spans, and
# ForeignRowHost — the QML delegates of the rows PageView does not draw placed over them in an
# AtramentItem (foreignhost.h, through the Qt Quick host brulion_atrament_qt).
# Built where Atrament is (src/app/editor/CMakeLists.txt adds it when brulion_atrament_qt exists).
qt_add_library(brulion_editor_atrament STATIC
    renderedspans.h renderedspans.cpp
    pageview.h pageview.cpp
    pageediting.cpp
    foreignrows.h foreignrows.cpp
    foreignhost.h foreignhost.cpp
)
target_include_directories(brulion_editor_atrament PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_editor_atrament PUBLIC brulion_editor brulion_atrament_ui brulion_atrament_qt
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
brulion_set_warnings(brulion_editor_atrament)
