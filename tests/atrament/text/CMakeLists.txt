# Tests of Atrament's text area: segmentation, fonts, paragraph layout, glyphs and the text
# system's caches. They load Brulion's bundled fonts from the source tree (assets/fonts).
foreach(_part segmentation fonts layout glyphs boxes shift)
    brulion_add_test(tst_atrament_text_${_part} SOURCES tst_atrament_text_${_part}.cpp textfixtures.h
        LIBS brulion_atrament_text)
    target_compile_definitions(tst_atrament_text_${_part} PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
endforeach()
