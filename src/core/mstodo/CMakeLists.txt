# mstodo — Microsoft To Do download for the import (docs/IMPORT.md "Microsoft To Do"): the OAuth
# device-code sign-in at the Microsoft identity platform (v2 endpoint, public client: the user's
# own app registration, no secret) and the download of every list with its tasks, steps, links
# and attachments from Microsoft Graph into the JSON bundle the io module's "mstodo" importer
# reads. HTTP only through sync::HttpTransport (tests: a fake Microsoft). The access token lives
# in memory for one import; nothing is stored, no refresh token is asked for.
# Public headers: mstodo.h (overview), deviceauth.h, download.h. Internal: messages_p.h.
brulion_add_core_module(mstodo
    SOURCES
        mstodo.h
        messages_p.h
        deviceauth.h deviceauth.cpp
        download.h download.cpp
    DEPS sync log
    QT Network)
