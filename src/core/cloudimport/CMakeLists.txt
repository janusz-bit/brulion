# cloudimport — downloads for the import from web services (docs/IMPORT.md): Microsoft OneNote
# through Graph (with an access token of the mstodo module's device-code sign-in, scope
# Notes.Read), Todoist's Sync API and Airtable's REST API with the user's personal tokens, and the
# files of a CSV's attachment cells. HTTP only through sync::HttpTransport (tests: fake servers);
# the results are input files for the io module's importers. Tokens live in memory only.
# Public headers: cloudimport.h (overview), apiclient.h, downloads.h. Internal: messages_p.h.
brulion_add_core_module(cloudimport
    SOURCES
        cloudimport.h
        messages_p.h
        apiclient.h apiclient.cpp
        downloads.h downloads.cpp
    DEPS sync log io
    QT Network)
