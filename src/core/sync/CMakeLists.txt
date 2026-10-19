# sync — Nextcloud/WebDAV sync (HTTP transport abstraction, WebDAV client, three-way merge,
# sync engine, scheduler) and Syncthing helpers (REST client, conflict copies, .stignore).
# Public headers: sync.h (overview), http.h, errors.h, url.h, davxml.h, webdavclient.h, diff.h,
# merge.h, conflicts.h, synctypes.h, syncengine.h, scheduler.h, syncthing.h, synchistory.h.
# Internal (*_p.h): request sending, job pool, one engine run, shared helpers.
brulion_add_core_module(sync
    SOURCES
        sync.h
        conflicts.h conflicts.cpp
        davxml.h davxml.cpp
        diff.h diff.cpp
        errors.h errors.cpp
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
        syncrun_p.h syncrun.cpp
        util_p.h util.cpp
    DEPS util text vault log
    QT Network)
