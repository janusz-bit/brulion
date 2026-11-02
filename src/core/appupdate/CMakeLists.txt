# appupdate — is a newer Brulion released? Versions, GitHub's answer about the latest release and
# the request for it (ARCHITECTURE.md; the message: shell/appupdates.h).
brulion_add_core_module(appupdate
    SOURCES
        appupdate.h appupdate.cpp
    QT Network)
