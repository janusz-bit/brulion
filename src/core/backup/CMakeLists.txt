# backup — automatic backups of a notebook (docs/ARCHITECTURE.md "Data safety"): one plain ZIP per
# copy written with streamed miniz (never the whole archive in memory), names only Brulion's pruning
# recognises, the schedule's decision, strict validation of a backup's entries, the restore plan and
# applying it through the Vault. No threads of its own: a notebook::WorkerPool is passed in, and
# without one the work runs in slices on the calling thread (WebAssembly).
# Public headers: backup.h (overview), archive.h, restore.h.
if(NOT TARGET brulion_miniz)
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/miniz ${CMAKE_BINARY_DIR}/3rdparty/miniz)
endif()

brulion_add_core_module(backup
    SOURCES
        backup.h backup.cpp
        archive.h archive.cpp
        restore.h restore.cpp
        taskrunner_p.h taskrunner.cpp
    DEPS util vault notebook log)
target_link_libraries(brulion_core_backup PRIVATE brulion_miniz)
