# brulion_ipc — the desktop's single-instance socket (QLocalServer/QLocalSocket): one Brulion per
# user session, and the messages a second launch or brulion-cli (`app open …`) sends to the
# running app (appmessage.h). QtCore + QtNetwork; not built for Android or the browser.
qt_add_library(brulion_ipc STATIC
    singleinstance.h singleinstance.cpp
    appmessage.h appmessage.cpp
)
target_include_directories(brulion_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ipc PUBLIC Qt6::Core Qt6::Network)
brulion_set_warnings(brulion_ipc)
