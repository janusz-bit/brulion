# caldav — calendars of other programs (docs/UI.md "Calendars", docs/SYNTAX.md §5 "Tasks synced with
# CalDAV"): iCalendar recurrence in full (rrule.h), time zones with VTIMEZONE (zones.h), events
# expanded into the days of a range (events.h), ICS subscriptions fetched with ETag /
# Last-Modified (feed.h), a CalDAV client with discovery, sync-collection and the ctag / ETag
# fallback (client.h), tasks (VTODO) mapped to task lines of the notes (taskmap.h) and synced both
# ways (tasksync.h), events kept on the device and edited on the server (eventstore.h), the
# configuration and caches on the device (store.h) and the run over all of them (calendarsync.h).
# HTTP only through sync::HttpTransport (tests: tests/core/caldav/fakecaldav.h).
# Public headers: caldav.h (overview) and the headers it includes.
brulion_add_core_module(caldav
    SOURCES
        caldav.h
        rrule.h rrule.cpp
        zones.h zones.cpp
        events.h events.cpp
        messages_p.h
        feed.h feed.cpp
        davxml_p.h davxml.cpp
        client.h client.cpp
        taskmap.h taskmap.cpp
        store.h store.cpp
        tasksync_p.h tasksync.cpp
        calendarsync.h calendarsync.cpp
    DEPS util text dates markdown index vault notebook log sync io
    QT Network)
