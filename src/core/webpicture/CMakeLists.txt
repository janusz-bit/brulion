# webpicture — pictures from the internet in notes: URL and format checks, the attachment name,
# the fetcher (limits, no cookies) and the device's cache (docs/SYNTAX.md §2, ARCHITECTURE.md).
brulion_add_core_module(webpicture
    SOURCES
        webpicture.h webpicture.cpp
        picturecache.h picturecache.cpp
        picturefetcher.h picturefetcher.cpp
    DEPS util
    QT Network)
