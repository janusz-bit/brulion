# notebook — what the app and the command line do to a notebook, GUI-free (docs/ARCHITECTURE.md):
# quick capture, journals (template, appending), pages and folders (create, rename and move with
# every link and tag kept, delete), the links Brulion copies, item references, JSON of the model,
# what is shared to Brulion from another app,
# and Notebook (a vault opened headless: lazy index, lock, compare-and-swap edits with History);
# loading the index on worker threads (IndexLoader) and keeping its cache on the device
# (IndexCacheKeeper).
# The shell's Workspace delegates to it; brulion-cli (src/cli) is built on it.
# Public headers: notebook.h (overview), capture.h, journals.h, pages.h, links.h, itemref.h,
# json.h, session.h, share.h.
brulion_add_core_module(notebook
    SOURCES
        notebook.h
        capture.h capture.cpp
        indexloader.h indexloader.cpp
        indexcachekeeper.h indexcachekeeper.cpp
        itemref.h itemref.cpp
        journals.h journals.cpp
        json.h json.cpp
        links.h links.cpp
        pages.h pages.cpp
        session.h session.cpp
        share.h share.cpp
    DEPS util text dates markdown vault index reminders log)
