brulion_add_test(tst_atrament_editor SOURCES tst_atrament_editor.cpp LIBS brulion_editor_atrament)
target_compile_definitions(tst_atrament_editor PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
brulion_add_test(tst_atrament_editing SOURCES tst_atrament_editing.cpp LIBS brulion_editor_atrament)
target_compile_definitions(tst_atrament_editing PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# Charts, Mermaid diagrams, formulas and page icons drawn by Atrament (diagramrows.cpp, mathrows.cpp).
brulion_add_test(tst_atrament_diagrams SOURCES tst_atrament_diagrams.cpp LIBS brulion_editor_atrament)
target_compile_definitions(tst_atrament_diagrams PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# Foreign rows hosted in an AtramentItem (foreignhost.h): QML delegates over a page's code blocks and
# tables, its header and footer, following the scroll, a finger's drag on them scrolling the page —
# an offscreen Qt Quick window with the software renderer (Qt Quick's QML module for the delegates).
get_filename_component(_brulion_atrament_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
brulion_add_test(tst_atrament_editor_hosting SOURCES tst_atrament_editor_hosting.cpp
    LIBS brulion_editor_atrament Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_atrament_editor_hosting PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_tests_properties(tst_atrament_editor_hosting PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_NO_TOUCH_COMPRESSION=1;QML_IMPORT_PATH=${_brulion_atrament_qml_root}")
# Tables drawn and edited by Atrament (tablerows.cpp, tablelayout.h).
brulion_add_test(tst_atrament_tables SOURCES tst_atrament_tables.cpp LIBS brulion_editor_atrament)
target_compile_definitions(tst_atrament_tables PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# Pictures, files, embedded blocks and query results drawn by Atrament (mediarows.cpp, queryrows.cpp),
# and the cache that decodes the pictures off the GUI thread (picturecache.h).
brulion_add_test(tst_atrament_media SOURCES tst_atrament_media.cpp LIBS brulion_editor_atrament)
target_compile_definitions(tst_atrament_media PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
brulion_add_test(tst_atrament_picturecache SOURCES tst_atrament_picturecache.cpp LIBS brulion_editor_atrament)
# The edited text's spell marks and the drawn rows' (pagespell.cpp): a SpellCheck over word lists.
brulion_add_test(tst_atrament_spell SOURCES tst_atrament_spell.cpp LIBS brulion_editor_atrament brulion_core_spell)
target_compile_definitions(tst_atrament_spell PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# Vim keys typed into the page drawn by Atrament (pagevim.cpp): tst_editor_vim's rows through the field.
brulion_add_test(tst_atrament_vim SOURCES tst_atrament_vim.cpp LIBS brulion_editor_atrament)
target_compile_definitions(tst_atrament_vim PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# Reminder chips in the edited text (pagechips.cpp) and code blocks' line numbers (pagecode.cpp).
brulion_add_test(tst_atrament_chips SOURCES tst_atrament_chips.cpp LIBS brulion_editor_atrament)
target_compile_definitions(tst_atrament_chips PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
brulion_add_test(tst_atrament_codelines SOURCES tst_atrament_codelines.cpp LIBS brulion_editor_atrament)
target_compile_definitions(tst_atrament_codelines PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
