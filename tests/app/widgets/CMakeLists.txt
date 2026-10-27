# The Android widgets' C++ (library brulion_widgets, src/app/widgets): the snapshot they show —
# sections, order, plain text, the calendar's agenda, limits, the capture days of quick add — with
# the golden files the Kotlin tests read too (tests/data/widgets), and quick add's lines and
# appends. Offscreen (the planning models link Qt Quick).
if(TARGET brulion_widgets)
    brulion_add_test(tst_widgets SOURCES tst_widgets.cpp LIBS brulion_widgets brulion_planning
        brulion_core_notebook brulion_core_vault)
endif()
