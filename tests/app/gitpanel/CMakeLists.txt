# The "Code" mode's git area (library brulion_gitpanel, src/app/gitpanel): CodeGit with real git in
# temporary repositories (skipped without git on PATH) — the panel's rows, refreshes by the watcher
# (also in a linked worktree) and the poll, diffs, commit / push / pull request (a fake gh),
# branches, worktrees, a turn's checkpoint and its restore, the actions' order, a folder change.
brulion_add_test(tst_codegit SOURCES tst_codegit.cpp LIBS brulion_gitpanel)
set_tests_properties(tst_codegit PROPERTIES TIMEOUT 300)
# The fake gh of tests/core/gitcli (a program on every platform).
if(TARGET fake_gh)
    add_dependencies(tst_codegit fake_gh)
    target_compile_definitions(tst_codegit PRIVATE FAKE_GH="$<TARGET_FILE:fake_gh>")
endif()
