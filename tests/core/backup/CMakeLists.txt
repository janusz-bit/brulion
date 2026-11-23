# Tests of the backup module (brulion_core_backup): names, pruning and the schedule (tst_backup);
# the streamed ZIP — a byte-exact round trip, files changing under the writer, cancelling, folders
# that cannot be written, malformed and hostile archives (tst_backup_archive); the restore plan and
# applying it through a real Vault, with the sync's mass-deletion guard (tst_backup_restore). Both
# ways of running: on a worker pool and in slices on the calling thread.
brulion_add_test(tst_backup SOURCES tst_backup.cpp LIBS brulion_core_backup)
brulion_add_test(tst_backup_archive SOURCES tst_backup_archive.cpp backuptest.h LIBS brulion_core_backup brulion_miniz)
brulion_add_test(tst_backup_zipwriter SOURCES tst_backup_zipwriter.cpp backuptest.h LIBS brulion_core_backup brulion_miniz)
brulion_add_test(tst_backup_restore SOURCES tst_backup_restore.cpp backuptest.h
    LIBS brulion_core_backup brulion_miniz brulion_sync_testsupport)
# A file over 1 GiB (sparse) through a backup and a restore: slow, so a test of its own.
brulion_add_test(tst_backup_large SOURCES tst_backup_large.cpp backuptest.h
    LIBS brulion_core_backup brulion_miniz)
set_tests_properties(tst_backup_large PROPERTIES TIMEOUT 1800)
