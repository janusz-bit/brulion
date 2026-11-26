# The "Code" mode's git area (library brulion_gitpanel, src/app/gitpanel): CodeGit with real git in
# temporary repositories (skipped without git on PATH) — the panel's rows, refreshes by the watcher
# (also in a linked worktree) and the poll, diffs, commit / push / pull request (a fake gh),
# branches, worktrees, a turn's checkpoint and its restore, the actions' order, a folder change;
# the diff sources (a branch against its base, a commit, two checkpoints), files and hunks staged
# and thrown away, commits of chosen files, the pull request's badge and a fork's, the quick
# action, git init and publishing, a merge in progress, stashes, an older turn reverted.
brulion_add_test(tst_codegit SOURCES tst_codegit.cpp LIBS brulion_gitpanel)
set_tests_properties(tst_codegit PROPERTIES TIMEOUT 300)
# The panel's pure rows (gitdiffrows.h): diff hunks, split rows, word changes, whitespace, found
# rows and their rich text, the file tree, review comments for the chat.
brulion_add_test(tst_gitdiffrows SOURCES tst_gitdiffrows.cpp LIBS brulion_gitpanel)
# The fake gh of tests/core/gitcli (a program on every platform).
if(TARGET fake_gh)
    add_dependencies(tst_codegit fake_gh)
    target_compile_definitions(tst_codegit PRIVATE FAKE_GH="$<TARGET_FILE:fake_gh>")
endif()
