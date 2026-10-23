# brulion_ipc — the desktop's sockets (QLocalServer/QLocalSocket): the single-instance socket — one
# Brulion per user session, and the messages a second launch or brulion-cli (`app open …`) sends to
# the running app (appmessage.h) — and the remote control of the running app (controlprotocol.h:
# JSON lines on a socket of its own; the app's ControlServer, brulion-cli's ControlClient). QtCore +
# QtNetwork; not built for Android or the browser.
qt_add_library(brulion_ipc STATIC
    singleinstance.h singleinstance.cpp
    appmessage.h appmessage.cpp
    controlprotocol.h controlprotocol.cpp
    controlserver.h controlserver.cpp
    controlclient.h controlclient.cpp
)
target_include_directories(brulion_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ipc PUBLIC Qt6::Core Qt6::Network)
brulion_set_warnings(brulion_ipc)
