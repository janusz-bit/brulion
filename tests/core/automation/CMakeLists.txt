# Tests of the automation module (brulion_core_automation): brulion:// addresses read and built,
# what they may do without asking and the automation keys, the blocks they write (pure edits), and
# requests worked out and carried out in a notebook.
foreach(_t uri policy edits plan)
    brulion_add_test(tst_automation_${_t} SOURCES tst_automation_${_t}.cpp LIBS brulion_core_automation)
endforeach()
