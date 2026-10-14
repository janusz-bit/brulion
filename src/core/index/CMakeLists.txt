# index — the in-memory vault index (VaultIndex: link resolution, backlinks, unlinked references,
# block ids, tags, tasks, reminders, journals, attachments, search, suggestions, graph) and the
# query language (docs/SYNTAX.md §10). Public headers: index.h (overview), vaultindex.h,
# searchindex.h, query.h; internal headers end in _p.h.
brulion_add_core_module(index
    SOURCES
        index.h
        vaultindex.h vaultindex_p.h vaultindex.cpp vaultindex_views.cpp
        searchindex.h searchindex.cpp
        query.h queryparse.cpp queryrun.cpp querymessages_p.h querymessages.cpp
        resolve_p.h resolve.cpp
        suggest_p.h suggest.cpp
        textutil_p.h textutil.cpp
        unlinked_p.h unlinked.cpp
    DEPS util text dates markdown)
