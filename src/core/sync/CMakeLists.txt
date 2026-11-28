# sync — Nextcloud/WebDAV sync (HTTP transport abstraction, WebDAV client, three-way merge,
# sync engine, scheduler), the sync with a folder of this device (a FolderStorage behind the
# WebDAV client: folderstorage.h, foldertransport.h) and Syncthing helpers (REST client, conflict
# copies, .stignore).
# Public headers: sync.h (overview), http.h, errors.h, url.h, davxml.h, webdavclient.h, diff.h,
# merge.h, conflicts.h, synctypes.h, syncengine.h, scheduler.h, syncthing.h, synchistory.h,
# folderstorage.h, foldertransport.h, encryption.h, encryptionkeys.h and encryptingtransport.h (the
# end-to-end encrypted sync: formats, key file, the layer before the server).
# Internal (*_p.h): request sending, job pool, one engine run, shared helpers.
brulion_add_core_module(sync
    SOURCES
        sync.h
        cardmerge.h cardmerge.cpp
        conflicts.h conflicts.cpp
        davxml.h davxml.cpp
        diff.h diff.cpp
        encryption.h encryption.cpp
        encryptionkeys.h encryptionkeys.cpp
        encryptingtransport.h encryptingtransport.cpp
        errors.h errors.cpp
        folderstorage.h folderstorage.cpp
        foldertransport.h foldertransport.cpp
        http.h http.cpp
        merge.h merge.cpp
        scheduler.h scheduler.cpp
        syncengine.h syncengine.cpp
        synctypes.h synctypes.cpp
        syncthing.h syncthing.cpp
        synchistory.h synchistory.cpp
        url.h url.cpp
        webdavclient.h webdavclient.cpp
        pool_p.h pool.cpp
        request_p.h request.cpp
        syncrun_p.h syncrun.cpp syncfolders.cpp
        util_p.h util.cpp
    DEPS util text vault log drawing canvas crypto
    QT Network)
