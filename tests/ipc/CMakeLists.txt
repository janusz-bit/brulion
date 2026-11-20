# The single-instance socket and its messages (src/ipc, library brulion_ipc; desktop only).
brulion_add_test(tst_ipc_singleinstance SOURCES tst_ipc_singleinstance.cpp LIBS brulion_ipc)
# The remote control's protocol (parsing, every method's parameters, targets, routes) and its socket
# end to end (owner-only permissions, answers in any order, a live server never taken over).
brulion_add_test(tst_ipc_control SOURCES tst_ipc_control.cpp LIBS brulion_ipc)
# The app's profile (profile.h): --profile, BRULION_PROFILE, a test run's own "test" profile.
brulion_add_test(tst_ipc_profile SOURCES tst_ipc_profile.cpp LIBS brulion_ipc)
