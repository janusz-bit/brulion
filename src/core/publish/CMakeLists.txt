# publish — a page or chosen notes as a website at an address (docs/UI.md "Publishing"): which
# pages a target publishes (pages, folders, tags, the notebook; `publish:` properties; hidden
# folders, extension code, conversations with AI and encrypted fragments left out), the static
# site built by the io module's HTML site export, a manifest of what was published (hashes), the
# plan of a publish (upload / update / unchanged / delete / conflict — the dry run) and the
# publishers: GitHub Pages (one commit per publish through the github module's client), a WebDAV
# folder (PUT / MKCOL / DELETE with ETag checks), Nextcloud (WebDAV + a public link through the
# OCS Share API, optionally with a password and an expiry date) and a folder on this device.
# Targets and manifests live in the notebook's device state folder, secrets in owner-only files.
# Public headers: publish.h (overview) and the headers it includes; internal headers end in _p.h.
brulion_add_core_module(publish
    SOURCES
        publish.h
        messages.h messages.cpp
        selection.h selection.cpp
        target.h target.cpp
        site.h site.cpp
        manifest.h manifest.cpp
        plan.h plan.cpp
        store.h store.cpp
        ocs.h ocs.cpp
        publisher.h publisher.cpp
        protect.h protect.cpp
        backends_p.h
        backend_folder.cpp
        backend_github.cpp
        backend_webdav.cpp
    DEPS util text markdown index io github sync log chat crypto
    QT Network)
